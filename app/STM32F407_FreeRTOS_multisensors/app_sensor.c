#include "app_sensor.h"

#include "FreeRTOS.h"
#include "app_comm.h"
#include "app_control.h"
#include "app_health.h"
#include "app_messages.h"
#include "app_observe.h"
#include "app_snapshot.h"
#include "board.h"
#include "bsp_adc.h"
#include "bsp_i2c.h"
#include "dht11.h"
#include "mpu6050.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"
#include "vl53l0x.h"
#include "st7735s_tft.h"

#include <stdio.h>
#include <string.h>

#define VL53_FILTER_WINDOW            5U
#define POT_FILTER_WINDOW             4U
#define POT_FILTER_DEADBAND_RAW       8U
#define MPU_MOTION_GYRO_DPS           20
#define MPU_POSTURE_DELTA_MG          120
#define VL53_INIT_RETRY_MS            2000U
#define VL53_POWERUP_DELAY_MS         500U
#define VL53_PROBE_ATTEMPTS           3U

static QueueHandle_t s_sensor_queue;
static SemaphoreHandle_t s_i2c1_mutex;
static SemaphoreHandle_t s_i2c2_mutex;

static int16_t abs_i16_local(int16_t value)
{
    return (value < 0) ? (int16_t)-value : value;
}

static uint16_t avg_u16(const uint16_t *values, uint8_t count)
{
    uint32_t sum = 0U;

    if (values == NULL || count == 0U) {
        return 0U;
    }

    for (uint8_t i = 0U; i < count; i++) {
        sum += values[i];
    }

    return (uint16_t)(sum / count);
}

static void display_draw_line(uint16_t y, const char *text)
{
    ST7735S_TFT_FillRect(0U, y, ST7735S_TFT_WIDTH, 10U, ST7735S_TFT_BLACK);
    ST7735S_TFT_DrawString(2U, y, text, ST7735S_TFT_WHITE, ST7735S_TFT_BLACK);
}

static void display_draw_status(uint16_t y, const char *label, uint8_t valid)
{
    char line[24];

    snprintf(line, sizeof(line), "%s:%s", label, valid ? "OK" : "FAIL");
    display_draw_line(y, line);
}

static void display_draw_page_env(const AppSystemSnapshot *state)
{
    char line[32];

    snprintf(line, sizeof(line), "P1 ENV POT:%u%%", (unsigned int)state->pot_percent);
    display_draw_line(0U, line);

    if (state->dht_valid) {
        snprintf(line, sizeof(line), "TEMP:%uC", (unsigned int)state->temperature);
        display_draw_line(12U, line);
        snprintf(line, sizeof(line), "HUM :%u%%", (unsigned int)state->humidity);
        display_draw_line(24U, line);
    } else {
        display_draw_status(12U, "DHT", 0U);
        display_draw_line(24U, "TEMP:-- HUM:--");
    }

    if (state->vl53_valid && state->vl53_range_valid) {
        snprintf(line, sizeof(line), "RNG :%uMM", (unsigned int)state->distance_mm);
        display_draw_line(38U, line);
    } else {
        display_draw_status(38U, "RNG", 0U);
    }

    snprintf(line, sizeof(line), "RAW :%u", (unsigned int)state->pot_raw);
    display_draw_line(50U, line);
}

static void display_draw_page_motion(const AppSystemSnapshot *state)
{
    char line[32];

    snprintf(line, sizeof(line), "P2 IMU POT:%u%%", (unsigned int)state->pot_percent);
    display_draw_line(0U, line);

    if (state->mpu_valid) {
        snprintf(line, sizeof(line), "AX:%d", (int)state->ax_mg);
        display_draw_line(12U, line);
        snprintf(line, sizeof(line), "AY:%d", (int)state->ay_mg);
        display_draw_line(24U, line);
        snprintf(line, sizeof(line), "AZ:%d", (int)state->az_mg);
        display_draw_line(36U, line);
    } else {
        display_draw_status(12U, "MPU", 0U);
        display_draw_line(24U, "AX:-- AY:--");
        display_draw_line(36U, "AZ:--");
    }

    snprintf(line, sizeof(line), "RAW:%u", (unsigned int)state->pot_raw);
    display_draw_line(50U, line);
}

/*
 * Context: FreeRTOS task.
 * Displays the latest sensor snapshot. Potentiometer selects page.
 */
static void TaskDisplay(void *argument)
{
    (void)argument;

    AppObserve_WriteLine("ST7735S task start\r\n");
    AppObserve_WriteLine("ST7735S init begin: SCL=PA5 SDA=PA7 RES=PC4 DC=PC5 CS=PC6 BLK=3V3\r\n");
    ST7735S_TFT_Init();
    ST7735S_TFT_ClearMemory(ST7735S_TFT_BLACK);
    AppObserve_WriteLine("ST7735S init done\r\n");
    AppObserve_WriteLine("ST7735S UI MODE: landscape 160x80, compact text, POT page switch\r\n");

    for (;;) {
        AppSystemSnapshot state;
        uint8_t page;

        AppHealth_Report(APP_HEALTH_LCD);
        (void)AppSnapshot_Get(&state);
        page = (state.pot_percent >= 50U) ? 1U : 0U;
        ST7735S_TFT_FillScreen(ST7735S_TFT_BLACK);

        if (page == 0U) {
            display_draw_page_env(&state);
        } else {
            display_draw_page_motion(&state);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/*
 * Context: FreeRTOS task.
 * Producer: samples the potentiometer on ADC1_IN0/PA0 and sends copied data.
 */
static void TaskPotentiometer(void *argument)
{
    (void)argument;
    TickType_t last_wake = xTaskGetTickCount();
    uint16_t raw_window[POT_FILTER_WINDOW] = {0};
    uint8_t raw_index = 0U;
    uint8_t raw_count = 0U;
    uint16_t last_filtered_raw = 0U;
    uint8_t has_filtered_raw = 0U;

    BSP_ADC1_PA0_Init();
    AppObserve_WriteLine("POT init: ADC1_IN0=PA0 period=500ms avg_window=4 deadband=8raw\r\n");

    for (;;) {
        AppMessage msg;
        uint16_t raw = 0U;

        AppHealth_Report(APP_HEALTH_POT);
        memset(&msg, 0, sizeof(msg));
        msg.type = APP_MSG_POT;
        msg.tick = xTaskGetTickCount();

        if (BSP_ADC1_PA0_Read(&raw) == BSP_ADC_OK) {
            uint16_t averaged_raw;

            raw_window[raw_index] = raw;
            raw_index = (uint8_t)((raw_index + 1U) % POT_FILTER_WINDOW);
            if (raw_count < POT_FILTER_WINDOW) {
                raw_count++;
            }

            averaged_raw = avg_u16(raw_window, raw_count);
            if (!has_filtered_raw ||
                (averaged_raw > (uint16_t)(last_filtered_raw + POT_FILTER_DEADBAND_RAW)) ||
                (last_filtered_raw > (uint16_t)(averaged_raw + POT_FILTER_DEADBAND_RAW))) {
                last_filtered_raw = averaged_raw;
                has_filtered_raw = 1U;
            }

            msg.valid = 1U;
            msg.data.pot.raw = last_filtered_raw;
            msg.data.pot.percent = BSP_ADC_RawToPercent(last_filtered_raw);
            msg.data.pot.filtered = 1U;
            msg.data.pot.status = "avg";
        } else {
            msg.valid = 0U;
            msg.data.pot.status = "timeout";
        }

        if (xQueueSend(s_sensor_queue, &msg, 0U) != pdTRUE) {
            AppObserve_WriteLine("POT queue full\r\n");
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(500));
    }
}

/*
 * Context: FreeRTOS task.
 * Producer: reads DHT11 periodically and sends copied data to s_sensor_queue.
 */
static void TaskDHT11(void *argument)
{
    (void)argument;
    TickType_t last_wake = xTaskGetTickCount();
    uint32_t reads = 0U;
    uint32_t errors = 0U;
    uint8_t last_temp = 0U;
    uint8_t last_hum = 0U;
    uint8_t has_last_valid = 0U;

    DHT11_GPIO_Init();
    AppObserve_WriteLine("DHT11 init: DATA=PC0 period=2000ms keep-last-valid=enabled\r\n");

    for (;;) {
        AppMessage msg;
        DHT11_Data data;

        AppHealth_Report(APP_HEALTH_DHT11);
        memset(&msg, 0, sizeof(msg));
        msg.type = APP_MSG_DHT11;
        msg.tick = xTaskGetTickCount();

        reads++;
        if (DHT11_Read(&data)) {
            msg.valid = 1U;
            msg.data.dht11.temperature = data.temperature;
            msg.data.dht11.humidity = data.humidity;
            msg.data.dht11.stale = 0U;
            msg.data.dht11.error = "ok";
            last_temp = data.temperature;
            last_hum = data.humidity;
            has_last_valid = 1U;
        } else {
            errors++;
            if (has_last_valid) {
                msg.valid = 1U;
                msg.data.dht11.temperature = last_temp;
                msg.data.dht11.humidity = last_hum;
                msg.data.dht11.stale = 1U;
                msg.data.dht11.error = DHT11_LastError();
            } else {
                msg.valid = 0U;
                msg.data.dht11.error = DHT11_LastError();
            }
        }

        if (xQueueSend(s_sensor_queue, &msg, 0U) != pdTRUE) {
            errors++;
            AppObserve_WriteLine("DHT11 queue full\r\n");
        }

        (void)reads;
        (void)errors;
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(2000));
    }
}

static void i2c1_scan_log(void)
{
    char line[24];
    uint8_t found = 0U;

    AppObserve_WriteLine("I2C1 scan:");
    for (uint8_t addr = 1U; addr < 0x7FU; addr++) {
        if (BSP_I2C1_IsReady(addr) == BSP_I2C_OK) {
            snprintf(line, sizeof(line), " 0x%02X", addr);
            AppObserve_WriteLine(line);
            found = 1U;
        }
    }
    if (!found) {
        AppObserve_WriteLine(" none");
    }
    AppObserve_WriteLine("\r\n");
}

static void i2c2_scan_log(void)
{
    char line[24];
    uint8_t found = 0U;

    AppObserve_WriteLine("I2C2 scan:");
    for (uint8_t addr = 1U; addr < 0x7FU; addr++) {
        if (BSP_I2C2_IsReady(addr) == BSP_I2C_OK) {
            snprintf(line, sizeof(line), " 0x%02X", addr);
            AppObserve_WriteLine(line);
            found = 1U;
        }
    }
    if (!found) {
        AppObserve_WriteLine(" none");
    }
    AppObserve_WriteLine("\r\n");
}

static uint8_t vl53_probe_with_recovery(uint8_t *scl_high, uint8_t *sda_high)
{
    uint8_t ready = 0U;

    for (uint8_t attempt = 0U; attempt < VL53_PROBE_ATTEMPTS; attempt++) {
        BSP_I2C2_RecoverBus();
        vTaskDelay(pdMS_TO_TICKS(10));
        BSP_I2C2_ReadLines(scl_high, sda_high);

        if (*scl_high != 0U && *sda_high != 0U &&
            BSP_I2C2_IsReady(VL53L0X_ADDR) == BSP_I2C_OK) {
            ready = 1U;
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }

    return ready;
}

static uint8_t vl53_try_init(uint8_t *model_id, uint8_t *i2c_ready, uint8_t *scl_high, uint8_t *sda_high)
{
    uint8_t ready;
    uint8_t ok;

    ready = vl53_probe_with_recovery(scl_high, sda_high);
    *i2c_ready = ready;

    /*
     * If probing still fails, call Init once anyway so the VL53 driver records
     * a precise LastStatus such as addr-nack for the UART log.
     */
    ok = (uint8_t)VL53L0X_Init();
    (void)VL53L0X_ReadModelId(model_id);

    return ok;
}

/*
 * Context: FreeRTOS task.
 * Producer: reads MPU6050/compatible IMU and sends copied data to s_sensor_queue.
 */
static void TaskMPU6050(void *argument)
{
    (void)argument;
    TickType_t last_wake = xTaskGetTickCount();
    uint8_t ok = 0U;
    uint8_t whoami = 0U;
    int16_t last_ax = 0;
    int16_t last_ay = 0;
    int16_t last_az = 0;
    uint8_t has_last_accel = 0U;

    if (xSemaphoreTake(s_i2c1_mutex, portMAX_DELAY) == pdTRUE) {
        BSP_I2C1_Init();
        AppObserve_WriteLine("I2C1 init: SCL=PB6 SDA=PB7\r\n");
        i2c1_scan_log();
        ok = (uint8_t)MPU6050_Init();
        (void)MPU6050_ReadWhoAmI(&whoami);
        xSemaphoreGive(s_i2c1_mutex);
    }

    if (ok) {
        char line[64];
        snprintf(line, sizeof(line), "MPU6050 init OK addr=0x%02X whoami=0x%02X\r\n",
                 (unsigned int)MPU6050_Address(), (unsigned int)whoami);
        AppObserve_WriteLine(line);
    } else {
        AppObserve_WriteLine("MPU6050 init FAIL\r\n");
    }

    for (;;) {
        AppMessage msg;
        MPU6050_Data data;

        AppHealth_Report(APP_HEALTH_MPU);
        memset(&msg, 0, sizeof(msg));
        msg.type = APP_MSG_MPU6050;
        msg.tick = xTaskGetTickCount();
        msg.data.mpu6050.addr = MPU6050_Address();
        msg.data.mpu6050.whoami = whoami;

        if (!ok) {
            if (xSemaphoreTake(s_i2c1_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                ok = (uint8_t)MPU6050_Init();
                (void)MPU6050_ReadWhoAmI(&whoami);
                xSemaphoreGive(s_i2c1_mutex);
            }
        }

        if (ok && xSemaphoreTake(s_i2c1_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            if (MPU6050_ReadData(&data)) {
                msg.valid = 1U;
                msg.data.mpu6050.ax_mg = data.ax_mg;
                msg.data.mpu6050.ay_mg = data.ay_mg;
                msg.data.mpu6050.az_mg = data.az_mg;
                msg.data.mpu6050.gx_dps = data.gx_dps;
                msg.data.mpu6050.gy_dps = data.gy_dps;
                msg.data.mpu6050.gz_dps = data.gz_dps;
                msg.data.mpu6050.addr = MPU6050_Address();
                msg.data.mpu6050.whoami = whoami;
                msg.data.mpu6050.motion =
                    (abs_i16_local(data.gx_dps) > MPU_MOTION_GYRO_DPS ||
                     abs_i16_local(data.gy_dps) > MPU_MOTION_GYRO_DPS ||
                     abs_i16_local(data.gz_dps) > MPU_MOTION_GYRO_DPS)
                        ? 1U
                        : 0U;
                if (has_last_accel) {
                    msg.data.mpu6050.posture_changed =
                        (abs_i16_local((int16_t)(data.ax_mg - last_ax)) > MPU_POSTURE_DELTA_MG ||
                         abs_i16_local((int16_t)(data.ay_mg - last_ay)) > MPU_POSTURE_DELTA_MG ||
                         abs_i16_local((int16_t)(data.az_mg - last_az)) > MPU_POSTURE_DELTA_MG)
                            ? 1U
                            : 0U;
                }
                last_ax = data.ax_mg;
                last_ay = data.ay_mg;
                last_az = data.az_mg;
                has_last_accel = 1U;
            } else {
                ok = 0U;
            }
            xSemaphoreGive(s_i2c1_mutex);
        }

        if (xQueueSend(s_sensor_queue, &msg, 0U) != pdTRUE) {
            AppObserve_WriteLine("MPU6050 queue full\r\n");
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(100));
    }
}

/*
 * Context: FreeRTOS task.
 * Producer: verifies VL53L0X presence on I2C2 and reports online evidence.
 */
static void TaskVL53L0X(void *argument)
{
    (void)argument;
    TickType_t last_wake = xTaskGetTickCount();
    uint8_t ok = 0U;
    uint8_t model_id = 0U;
    uint16_t distance_window[VL53_FILTER_WINDOW] = {0};
    uint8_t distance_index = 0U;
    uint8_t distance_count = 0U;
    uint32_t offline_count = 0U;
    uint8_t last_i2c_ready = 0U;
    uint8_t last_scl_high = 0U;
    uint8_t last_sda_high = 0U;

    vTaskDelay(pdMS_TO_TICKS(VL53_POWERUP_DELAY_MS));
    if (xSemaphoreTake(s_i2c2_mutex, portMAX_DELAY) == pdTRUE) {
        BSP_I2C2_Init();
        AppObserve_WriteLine("I2C2 init: SCL=PB10 SDA=PB11\r\n");
        i2c2_scan_log();
        ok = vl53_try_init(&model_id, &last_i2c_ready, &last_scl_high, &last_sda_high);
        xSemaphoreGive(s_i2c2_mutex);
    }

    if (ok) {
        char line[64];
        snprintf(line, sizeof(line), "VL53L0X init OK addr=0x%02X model=0x%02X avg_window=5\r\n",
                 (unsigned int)VL53L0X_ADDR, (unsigned int)model_id);
        AppObserve_WriteLine(line);
    } else {
        char line[128];
        snprintf(line, sizeof(line),
                 "VL53L0X init FAIL i2c_ready=%u scl=%u sda=%u status=%s\r\n",
                 (unsigned int)last_i2c_ready,
                 (unsigned int)last_scl_high,
                 (unsigned int)last_sda_high,
                 VL53L0X_LastStatus());
        AppObserve_WriteLine(line);
    }

    for (;;) {
        AppMessage msg;
        uint16_t distance_mm = 0U;
        VL53L0X_Status range_status = VL53L0X_NOT_FOUND;
        TickType_t now = xTaskGetTickCount();
        static TickType_t next_init_retry_tick;

        AppHealth_Report(APP_HEALTH_VL53);
        memset(&msg, 0, sizeof(msg));
        msg.type = APP_MSG_VL53L0X;
        msg.tick = xTaskGetTickCount();
        msg.data.vl53l0x.addr = VL53L0X_ADDR;
        msg.data.vl53l0x.model_id = model_id;

        if (xSemaphoreTake(s_i2c2_mutex, pdMS_TO_TICKS(30)) == pdTRUE) {
            if (!ok && (TickType_t)(now - next_init_retry_tick) < (TickType_t)0x80000000UL) {
                ok = vl53_try_init(&model_id, &last_i2c_ready, &last_scl_high, &last_sda_high);
                next_init_retry_tick = (TickType_t)(xTaskGetTickCount() + pdMS_TO_TICKS(VL53_INIT_RETRY_MS));
            } else {
                BSP_I2C2_ReadLines(&last_scl_high, &last_sda_high);
            }
            (void)VL53L0X_ReadModelId(&model_id);
            if (ok) {
                range_status = VL53L0X_ReadDistance(&distance_mm);
            }
            xSemaphoreGive(s_i2c2_mutex);
        } else {
            ok = 0U;
        }

        if (range_status == VL53L0X_NOT_FOUND) {
            ok = 0U;
        }

        msg.valid = ok ? 1U : 0U;
        msg.data.vl53l0x.model_id = model_id;
        msg.data.vl53l0x.range_valid = (range_status == VL53L0X_PRESENT) ? 1U : 0U;
        if (!ok) {
            offline_count++;
            msg.data.vl53l0x.distance_mm = 0U;
            msg.data.vl53l0x.status = VL53L0X_LastStatus();
            if (offline_count == 1U || (offline_count % 10U) == 0U) {
                char diag[128];
                snprintf(diag, sizeof(diag),
                         "VL53L0X diag i2c_ready=%u scl=%u sda=%u model=0x%02X status=%s retry_ms=%u offline_count=%lu\r\n",
                         (unsigned int)last_i2c_ready,
                         (unsigned int)last_scl_high,
                         (unsigned int)last_sda_high,
                         (unsigned int)model_id,
                         VL53L0X_LastStatus(),
                         (unsigned int)VL53_INIT_RETRY_MS,
                         (unsigned long)offline_count);
                AppObserve_WriteLine(diag);
            }
        } else if (range_status == VL53L0X_PRESENT) {
            offline_count = 0U;
            distance_window[distance_index] = distance_mm;
            distance_index = (uint8_t)((distance_index + 1U) % VL53_FILTER_WINDOW);
            if (distance_count < VL53_FILTER_WINDOW) {
                distance_count++;
            }
            msg.data.vl53l0x.distance_mm = avg_u16(distance_window, distance_count);
            msg.data.vl53l0x.filtered = 1U;
            msg.data.vl53l0x.status = "range-ok";
        } else {
            msg.data.vl53l0x.distance_mm = 0U;
            msg.data.vl53l0x.status = VL53L0X_LastStatus();
        }

        if (xQueueSend(s_sensor_queue, &msg, 0U) != pdTRUE) {
            AppObserve_WriteLine("VL53L0X queue full\r\n");
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(500));
    }
}

/*
 * Context: FreeRTOS task.
 * Consumer: receives sensor messages and prints observable UART evidence.
 */
static void TaskSensorLog(void *argument)
{
    (void)argument;
    AppMessage msg;
    uint32_t dht_reads = 0U;
    uint32_t dht_errors = 0U;
    uint32_t mpu_reads = 0U;
    uint32_t mpu_errors = 0U;
    uint32_t vl53_reads = 0U;
    uint32_t vl53_errors = 0U;
    char line[160];

    for (;;) {
        if (xQueueReceive(s_sensor_queue, &msg, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        AppHealth_Report(APP_HEALTH_SLOG);
        AppSnapshot_UpdateFromSensor(&msg);
        AppComm_SubmitSensorMessage(&msg);
        AppControl_UpdateFromSensor(&msg);

        if (msg.type == APP_MSG_DHT11) {
            dht_reads++;
            if (!msg.valid) {
                dht_errors++;
            }

            if (msg.valid && msg.data.dht11.stale) {
                snprintf(line, sizeof(line),
                         "DHT11 STALE tick=%lu temp=%uC hum=%u%% last=%s reads=%lu err=%lu\r\n",
                         (unsigned long)msg.tick,
                         (unsigned int)msg.data.dht11.temperature,
                         (unsigned int)msg.data.dht11.humidity,
                         msg.data.dht11.error,
                         (unsigned long)dht_reads,
                         (unsigned long)dht_errors);
            } else if (msg.valid) {
                snprintf(line, sizeof(line),
                         "DHT11 OK tick=%lu temp=%uC hum=%u%% reads=%lu err=%lu\r\n",
                         (unsigned long)msg.tick,
                         (unsigned int)msg.data.dht11.temperature,
                         (unsigned int)msg.data.dht11.humidity,
                         (unsigned long)dht_reads,
                         (unsigned long)dht_errors);
            } else {
                snprintf(line, sizeof(line),
                         "DHT11 FAIL tick=%lu last=%s reads=%lu err=%lu\r\n",
                         (unsigned long)msg.tick,
                         msg.data.dht11.error,
                         (unsigned long)dht_reads,
                         (unsigned long)dht_errors);
            }
            AppObserve_WriteLine(line);
        } else if (msg.type == APP_MSG_MPU6050) {
            mpu_reads++;
            if (!msg.valid) {
                mpu_errors++;
            }

            if (!msg.valid || (mpu_reads % 10U) == 0U) {
                if (msg.valid) {
                    snprintf(line, sizeof(line),
                             "MPU6050 OK tick=%lu addr=0x%02X id=0x%02X acc=%d,%d,%d gyro=%d,%d,%d motion=%u posture=%u reads=%lu err=%lu\r\n",
                             (unsigned long)msg.tick,
                             (unsigned int)msg.data.mpu6050.addr,
                             (unsigned int)msg.data.mpu6050.whoami,
                             (int)msg.data.mpu6050.ax_mg,
                             (int)msg.data.mpu6050.ay_mg,
                             (int)msg.data.mpu6050.az_mg,
                             (int)msg.data.mpu6050.gx_dps,
                             (int)msg.data.mpu6050.gy_dps,
                             (int)msg.data.mpu6050.gz_dps,
                             (unsigned int)msg.data.mpu6050.motion,
                             (unsigned int)msg.data.mpu6050.posture_changed,
                             (unsigned long)mpu_reads,
                             (unsigned long)mpu_errors);
                } else {
                    snprintf(line, sizeof(line),
                             "MPU6050 FAIL tick=%lu addr=0x%02X id=0x%02X reads=%lu err=%lu\r\n",
                             (unsigned long)msg.tick,
                             (unsigned int)msg.data.mpu6050.addr,
                             (unsigned int)msg.data.mpu6050.whoami,
                             (unsigned long)mpu_reads,
                             (unsigned long)mpu_errors);
                }
                AppObserve_WriteLine(line);
            }
        } else if (msg.type == APP_MSG_VL53L0X) {
            vl53_reads++;
            if (!msg.valid) {
                vl53_errors++;
            }

            if (msg.valid && msg.data.vl53l0x.range_valid) {
                snprintf(line, sizeof(line),
                         "VL53L0X RANGE_OK tick=%lu addr=0x%02X model=0x%02X dist=%umm filtered=%u reads=%lu err=%lu\r\n",
                         (unsigned long)msg.tick,
                         (unsigned int)msg.data.vl53l0x.addr,
                         (unsigned int)msg.data.vl53l0x.model_id,
                         (unsigned int)msg.data.vl53l0x.distance_mm,
                         (unsigned int)msg.data.vl53l0x.filtered,
                         (unsigned long)vl53_reads,
                         (unsigned long)vl53_errors);
            } else {
                snprintf(line, sizeof(line),
                         "VL53L0X %s tick=%lu addr=0x%02X model=0x%02X status=%s reads=%lu err=%lu\r\n",
                         msg.valid ? "ONLINE" : "FAIL",
                         (unsigned long)msg.tick,
                         (unsigned int)msg.data.vl53l0x.addr,
                         (unsigned int)msg.data.vl53l0x.model_id,
                         msg.data.vl53l0x.status,
                         (unsigned long)vl53_reads,
                         (unsigned long)vl53_errors);
            }
            AppObserve_WriteLine(line);
        } else if (msg.type == APP_MSG_POT) {
            if (msg.valid) {
                snprintf(line, sizeof(line),
                         "POT OK tick=%lu raw=%u percent=%u%% filtered=%u status=%s\r\n",
                         (unsigned long)msg.tick,
                         (unsigned int)msg.data.pot.raw,
                         (unsigned int)msg.data.pot.percent,
                         (unsigned int)msg.data.pot.filtered,
                         msg.data.pot.status);
            } else {
                snprintf(line, sizeof(line),
                         "POT FAIL tick=%lu status=%s\r\n",
                         (unsigned long)msg.tick,
                         msg.data.pot.status);
            }
            AppObserve_WriteLine(line);
        }
    }
}

void AppSensor_Start(void)
{
    TaskHandle_t dht_handle = NULL;
    TaskHandle_t mpu_handle = NULL;
    TaskHandle_t vl53_handle = NULL;
    TaskHandle_t pot_handle = NULL;
    TaskHandle_t lcd_handle = NULL;
    TaskHandle_t slog_handle = NULL;

    s_sensor_queue = xQueueCreate(12U, sizeof(AppMessage));
    s_i2c1_mutex = xSemaphoreCreateMutex();
    s_i2c2_mutex = xSemaphoreCreateMutex();
    if (s_sensor_queue == NULL || s_i2c1_mutex == NULL ||
        s_i2c2_mutex == NULL) {
        Board_Error_Handler();
    }
    AppSnapshot_Init();

    AppComm_Start();
    AppControl_Start();

    if (xTaskCreate(TaskDHT11, "DHT11", 384U, NULL,
                    tskIDLE_PRIORITY + 2U, &dht_handle) != pdPASS ||
        xTaskCreate(TaskMPU6050, "MPU", 512U, NULL,
                    tskIDLE_PRIORITY + 2U, &mpu_handle) != pdPASS ||
        xTaskCreate(TaskVL53L0X, "VL53", 512U, NULL,
                    tskIDLE_PRIORITY + 2U, &vl53_handle) != pdPASS ||
        xTaskCreate(TaskPotentiometer, "POT", 384U, NULL,
                    tskIDLE_PRIORITY + 2U, &pot_handle) != pdPASS ||
        xTaskCreate(TaskDisplay, "LCD", 512U, NULL,
                    tskIDLE_PRIORITY + 1U, &lcd_handle) != pdPASS ||
        xTaskCreate(TaskSensorLog, "SLOG", 512U, NULL,
                    tskIDLE_PRIORITY + 1U, &slog_handle) != pdPASS) {
        Board_Error_Handler();
    }

    AppObserve_RegisterTask("DHT11", dht_handle);
    AppObserve_RegisterTask("MPU", mpu_handle);
    AppObserve_RegisterTask("VL53", vl53_handle);
    AppObserve_RegisterTask("POT", pot_handle);
    AppObserve_RegisterTask("LCD", lcd_handle);
    AppObserve_RegisterTask("SLOG", slog_handle);

    AppObserve_WriteLine("AppSensor tasks created: DHT11 MPU VL53 POT LCD SLOG\r\n");
}
