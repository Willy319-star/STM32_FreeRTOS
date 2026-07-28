#include "app_snapshot.h"

#include "FreeRTOS.h"
#include "app_control.h"
#include "board.h"
#include "semphr.h"
#include "task.h"

#include <string.h>

static SemaphoreHandle_t s_snapshot_mutex;
static AppSystemSnapshot s_snapshot;

void AppSnapshot_Init(void)
{
    s_snapshot_mutex = xSemaphoreCreateMutex();
    if (s_snapshot_mutex == NULL) {
        Board_Error_Handler();
    }

    memset(&s_snapshot, 0, sizeof(s_snapshot));
}

void AppSnapshot_UpdateFromSensor(const AppMessage *msg)
{
    if (msg == NULL || s_snapshot_mutex == NULL) {
        return;
    }

    if (xSemaphoreTake(s_snapshot_mutex, pdMS_TO_TICKS(5)) != pdTRUE) {
        return;
    }

    if (msg->type == APP_MSG_DHT11) {
        s_snapshot.dht_valid = msg->valid;
        s_snapshot.dht_stale = msg->data.dht11.stale;
        if (msg->valid) {
            s_snapshot.temperature = msg->data.dht11.temperature;
            s_snapshot.humidity = msg->data.dht11.humidity;
        }
    } else if (msg->type == APP_MSG_MPU6050) {
        s_snapshot.mpu_valid = msg->valid;
        if (msg->valid) {
            s_snapshot.ax_mg = msg->data.mpu6050.ax_mg;
            s_snapshot.ay_mg = msg->data.mpu6050.ay_mg;
            s_snapshot.az_mg = msg->data.mpu6050.az_mg;
            s_snapshot.gx_dps = msg->data.mpu6050.gx_dps;
            s_snapshot.gy_dps = msg->data.mpu6050.gy_dps;
            s_snapshot.gz_dps = msg->data.mpu6050.gz_dps;
            s_snapshot.motion = msg->data.mpu6050.motion;
            s_snapshot.posture_changed = msg->data.mpu6050.posture_changed;
        }
    } else if (msg->type == APP_MSG_VL53L0X) {
        s_snapshot.vl53_valid = msg->valid;
        s_snapshot.vl53_range_valid = msg->data.vl53l0x.range_valid;
        if (msg->valid && msg->data.vl53l0x.range_valid) {
            s_snapshot.distance_mm = msg->data.vl53l0x.distance_mm;
        }
    } else if (msg->type == APP_MSG_POT) {
        s_snapshot.pot_valid = msg->valid;
        if (msg->valid) {
            s_snapshot.pot_percent = msg->data.pot.percent;
            s_snapshot.pot_raw = msg->data.pot.raw;
        }
    }

    xSemaphoreGive(s_snapshot_mutex);
}

uint8_t AppSnapshot_Get(AppSystemSnapshot *snapshot)
{
    if (snapshot == NULL) {
        return 0U;
    }

    memset(snapshot, 0, sizeof(*snapshot));

    if (s_snapshot_mutex != NULL) {
        if (xSemaphoreTake(s_snapshot_mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
            return 0U;
        }
        *snapshot = s_snapshot;
        xSemaphoreGive(s_snapshot_mutex);
    }

    snapshot->tick = xTaskGetTickCount();
    snapshot->heap_free = xPortGetFreeHeapSize();
    snapshot->heap_min = xPortGetMinimumEverFreeHeapSize();
    snapshot->alert_bits = AppControl_GetAlertBits();

    return 1U;
}
