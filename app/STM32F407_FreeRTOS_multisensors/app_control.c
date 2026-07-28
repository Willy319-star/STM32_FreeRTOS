#include "app_control.h"

#include "FreeRTOS.h"
#include "app_health.h"
#include "app_observe.h"
#include "board.h"
#include "event_groups.h"
#include "stm32f4xx_hal.h"
#include "task.h"

#include <stdio.h>
#include <string.h>

#define ALERT_HOT_BIT       (1UL << 0)
#define ALERT_DRY_BIT       (1UL << 1)
#define ALERT_NEAR_BIT      (1UL << 2)
#define ALERT_POT_HIGH_BIT  (1UL << 3)
#define ALERT_MOTION_BIT    (1UL << 4)
#define ALERT_DHT_FAIL_BIT  (1UL << 8)
#define ALERT_MPU_FAIL_BIT  (1UL << 9)
#define ALERT_VL53_FAIL_BIT (1UL << 10)
#define ALERT_POT_FAIL_BIT  (1UL << 11)

#define ALERT_ANY_BIT       (ALERT_HOT_BIT | ALERT_DRY_BIT | ALERT_NEAR_BIT | \
                             ALERT_POT_HIGH_BIT | ALERT_MOTION_BIT | \
                             ALERT_DHT_FAIL_BIT | ALERT_MPU_FAIL_BIT | \
                             ALERT_VL53_FAIL_BIT | ALERT_POT_FAIL_BIT)

#define APP_CONFIG_FLASH_ADDR   0x08060000UL
#define APP_CONFIG_FLASH_SECTOR FLASH_SECTOR_7
#define APP_CONFIG_MAGIC        0x43464731UL
#define APP_CONFIG_VERSION      1UL

static EventGroupHandle_t s_alert_events;

static const AppControlConfig s_default_config = {
    .temp_hot_c = 32U,
    .humidity_dry_pct = 30U,
    .distance_near_mm = 120U,
    .pot_high_pct = 80U,
    .gyro_motion_dps = 100,
};

static AppControlConfig s_config = {
    .temp_hot_c = 32U,
    .humidity_dry_pct = 30U,
    .distance_near_mm = 120U,
    .pot_high_pct = 80U,
    .gyro_motion_dps = 100,
};

typedef struct {
    uint32_t magic;
    uint32_t version;
    AppControlConfig config;
    uint32_t crc;
} AppControlFlashRecord;

static int16_t abs_i16(int16_t value)
{
    return (value < 0) ? (int16_t)-value : value;
}

static uint32_t crc32_step(uint32_t crc, uint8_t byte)
{
    crc ^= byte;
    for (uint8_t bit = 0U; bit < 8U; bit++) {
        if ((crc & 1UL) != 0UL) {
            crc = (crc >> 1) ^ 0xEDB88320UL;
        } else {
            crc >>= 1;
        }
    }

    return crc;
}

static uint32_t crc32_calc(const void *data, uint32_t length)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFUL;

    for (uint32_t i = 0UL; i < length; i++) {
        crc = crc32_step(crc, bytes[i]);
    }

    return ~crc;
}

static uint8_t is_config_valid(const AppControlConfig *config)
{
    if (config == NULL) {
        return 0U;
    }

    if (config->temp_hot_c > 80U) {
        return 0U;
    }
    if (config->humidity_dry_pct > 100U) {
        return 0U;
    }
    if (config->distance_near_mm < 10U || config->distance_near_mm > 2000U) {
        return 0U;
    }
    if (config->pot_high_pct > 100U) {
        return 0U;
    }
    if (config->gyro_motion_dps < 1 || config->gyro_motion_dps > 2000) {
        return 0U;
    }

    return 1U;
}

static uint8_t is_flash_record_valid(const AppControlFlashRecord *record)
{
    uint32_t expected_crc;

    if (record == NULL) {
        return 0U;
    }
    if (record->magic != APP_CONFIG_MAGIC || record->version != APP_CONFIG_VERSION) {
        return 0U;
    }
    if (!is_config_valid(&record->config)) {
        return 0U;
    }

    expected_crc = crc32_calc(record, sizeof(*record) - sizeof(record->crc));
    return (record->crc == expected_crc) ? 1U : 0U;
}

static void update_bit(EventBits_t bit, uint8_t enabled)
{
    if (s_alert_events == NULL) {
        return;
    }

    if (enabled) {
        (void)xEventGroupSetBits(s_alert_events, bit);
    } else {
        (void)xEventGroupClearBits(s_alert_events, bit);
    }
}

static AppControlConfig get_config_snapshot(void)
{
    AppControlConfig config;

    taskENTER_CRITICAL();
    config = s_config;
    taskEXIT_CRITICAL();

    return config;
}

void AppControl_UpdateFromSensor(const AppMessage *msg)
{
    AppControlConfig config = get_config_snapshot();

    if (msg == NULL || s_alert_events == NULL) {
        return;
    }

    if (msg->type == APP_MSG_DHT11) {
        update_bit(ALERT_DHT_FAIL_BIT, msg->valid ? 0U : 1U);
        if (msg->valid) {
            update_bit(ALERT_HOT_BIT, msg->data.dht11.temperature >= config.temp_hot_c);
            update_bit(ALERT_DRY_BIT, msg->data.dht11.humidity <= config.humidity_dry_pct);
        } else {
            update_bit(ALERT_HOT_BIT, 0U);
            update_bit(ALERT_DRY_BIT, 0U);
        }
    } else if (msg->type == APP_MSG_MPU6050) {
        update_bit(ALERT_MPU_FAIL_BIT, msg->valid ? 0U : 1U);
        if (msg->valid) {
            uint8_t motion = (abs_i16(msg->data.mpu6050.gx_dps) > config.gyro_motion_dps ||
                              abs_i16(msg->data.mpu6050.gy_dps) > config.gyro_motion_dps ||
                              abs_i16(msg->data.mpu6050.gz_dps) > config.gyro_motion_dps)
                                 ? 1U
                                 : 0U;
            update_bit(ALERT_MOTION_BIT, motion);
        } else {
            update_bit(ALERT_MOTION_BIT, 0U);
        }
    } else if (msg->type == APP_MSG_VL53L0X) {
        update_bit(ALERT_VL53_FAIL_BIT, msg->valid ? 0U : 1U);
        if (msg->valid && msg->data.vl53l0x.range_valid) {
            update_bit(ALERT_NEAR_BIT, msg->data.vl53l0x.distance_mm < config.distance_near_mm);
        } else {
            update_bit(ALERT_NEAR_BIT, 0U);
        }
    } else if (msg->type == APP_MSG_POT) {
        update_bit(ALERT_POT_FAIL_BIT, msg->valid ? 0U : 1U);
        if (msg->valid) {
            update_bit(ALERT_POT_HIGH_BIT, msg->data.pot.percent >= config.pot_high_pct);
        } else {
            update_bit(ALERT_POT_HIGH_BIT, 0U);
        }
    }
}

uint32_t AppControl_GetAlertBits(void)
{
    if (s_alert_events == NULL) {
        return 0U;
    }

    return (uint32_t)(xEventGroupGetBits(s_alert_events) & ALERT_ANY_BIT);
}

void AppControl_GetConfig(AppControlConfig *config)
{
    if (config == NULL) {
        return;
    }

    *config = get_config_snapshot();
}

void AppControl_ResetConfigDefaults(void)
{
    taskENTER_CRITICAL();
    s_config = s_default_config;
    taskEXIT_CRITICAL();
}

uint8_t AppControl_SetTempHot(uint8_t value)
{
    if (value > 80U) {
        return 0U;
    }

    taskENTER_CRITICAL();
    s_config.temp_hot_c = value;
    taskEXIT_CRITICAL();
    return 1U;
}

uint8_t AppControl_SetHumidityDry(uint8_t value)
{
    if (value > 100U) {
        return 0U;
    }

    taskENTER_CRITICAL();
    s_config.humidity_dry_pct = value;
    taskEXIT_CRITICAL();
    return 1U;
}

uint8_t AppControl_SetDistanceNear(uint16_t value)
{
    if (value < 10U || value > 2000U) {
        return 0U;
    }

    taskENTER_CRITICAL();
    s_config.distance_near_mm = value;
    taskEXIT_CRITICAL();
    return 1U;
}

uint8_t AppControl_SetPotHigh(uint8_t value)
{
    if (value > 100U) {
        return 0U;
    }

    taskENTER_CRITICAL();
    s_config.pot_high_pct = value;
    taskEXIT_CRITICAL();
    return 1U;
}

uint8_t AppControl_SetGyroMotion(int16_t value)
{
    if (value < 1 || value > 2000) {
        return 0U;
    }

    taskENTER_CRITICAL();
    s_config.gyro_motion_dps = value;
    taskEXIT_CRITICAL();
    return 1U;
}

uint8_t AppControl_LoadConfigFromFlash(void)
{
    const AppControlFlashRecord *record = (const AppControlFlashRecord *)APP_CONFIG_FLASH_ADDR;

    if (!is_flash_record_valid(record)) {
        AppControl_ResetConfigDefaults();
        return 0U;
    }

    taskENTER_CRITICAL();
    s_config = record->config;
    taskEXIT_CRITICAL();
    return 1U;
}

uint8_t AppControl_SaveConfigToFlash(void)
{
    AppControlFlashRecord record;
    FLASH_EraseInitTypeDef erase;
    uint32_t sector_error = 0UL;
    uint32_t address = APP_CONFIG_FLASH_ADDR;
    const uint32_t *words = (const uint32_t *)&record;
    uint32_t word_count = (uint32_t)(sizeof(record) / sizeof(uint32_t));
    HAL_StatusTypeDef status;

    memset(&record, 0, sizeof(record));
    record.magic = APP_CONFIG_MAGIC;
    record.version = APP_CONFIG_VERSION;
    record.config = get_config_snapshot();
    record.crc = crc32_calc(&record, sizeof(record) - sizeof(record.crc));

    if (!is_config_valid(&record.config)) {
        return 0U;
    }

    memset(&erase, 0, sizeof(erase));
    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.Sector = APP_CONFIG_FLASH_SECTOR;
    erase.NbSectors = 1U;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;

    status = HAL_FLASH_Unlock();
    if (status == HAL_OK) {
        status = HAL_FLASHEx_Erase(&erase, &sector_error);
    }
    if (status == HAL_OK) {
        for (uint32_t i = 0UL; i < word_count; i++) {
            status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, address, words[i]);
            if (status != HAL_OK) {
                break;
            }
            address += sizeof(uint32_t);
        }
    }
    (void)HAL_FLASH_Lock();

    if (status != HAL_OK || sector_error != 0xFFFFFFFFUL) {
        return 0U;
    }

    return is_flash_record_valid((const AppControlFlashRecord *)APP_CONFIG_FLASH_ADDR);
}

static void TaskControl(void *argument)
{
    (void)argument;
    TickType_t last_wake = xTaskGetTickCount();
    EventBits_t last_bits = (EventBits_t)0xFFFFFFFFUL;
    uint8_t heartbeat = 0U;
    char line[192];

    AppObserve_WriteLine("ControlTask start: EventGroup alert monitor\r\n");

    for (;;) {
        EventBits_t bits = xEventGroupGetBits(s_alert_events) & ALERT_ANY_BIT;
        uint8_t should_log = (bits != last_bits) ? 1U : 0U;

        AppHealth_Report(APP_HEALTH_CONTROL);
        heartbeat++;
        if (heartbeat >= 5U) {
            heartbeat = 0U;
            should_log = 1U;
        }

        if (should_log) {
            snprintf(line, sizeof(line),
                     "ALERT bits=0x%04lX hot=%u dry=%u near=%u pot_hi=%u motion=%u fail=0x%02lX\r\n",
                     (unsigned long)bits,
                     (unsigned int)((bits & ALERT_HOT_BIT) != 0U),
                     (unsigned int)((bits & ALERT_DRY_BIT) != 0U),
                     (unsigned int)((bits & ALERT_NEAR_BIT) != 0U),
                     (unsigned int)((bits & ALERT_POT_HIGH_BIT) != 0U),
                     (unsigned int)((bits & ALERT_MOTION_BIT) != 0U),
                     (unsigned long)((bits >> 8) & 0x0FUL));
            AppObserve_WriteLine(line);
            last_bits = bits;
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(1000));
    }
}

void AppControl_Start(void)
{
    TaskHandle_t control_handle = NULL;
    char line[128];

    if (AppControl_LoadConfigFromFlash()) {
        AppObserve_WriteLine("CONFIG load: flash valid\r\n");
    } else {
        AppObserve_WriteLine("CONFIG load: flash empty/invalid, defaults active\r\n");
    }

    AppControlConfig config = get_config_snapshot();
    snprintf(line, sizeof(line),
             "CONFIG active hot=%uC dry=%u%% near=%umm pot=%u%% motion=%ddps\r\n",
             (unsigned int)config.temp_hot_c,
             (unsigned int)config.humidity_dry_pct,
             (unsigned int)config.distance_near_mm,
             (unsigned int)config.pot_high_pct,
             (int)config.gyro_motion_dps);
    AppObserve_WriteLine(line);

    s_alert_events = xEventGroupCreate();
    if (s_alert_events == NULL) {
        Board_Error_Handler();
    }

    if (xTaskCreate(TaskControl, "CONTROL", 512U, NULL,
                    tskIDLE_PRIORITY + 1U, &control_handle) != pdPASS) {
        Board_Error_Handler();
    }

    AppObserve_RegisterTask("CONTROL", control_handle);
}
