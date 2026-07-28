#pragma once

#include "app_messages.h"

#include <stdint.h>

typedef struct {
    uint32_t tick;
    uint32_t heap_free;
    uint32_t heap_min;
    uint32_t alert_bits;

    uint8_t dht_valid;
    uint8_t dht_stale;
    uint8_t temperature;
    uint8_t humidity;

    uint8_t mpu_valid;
    int16_t ax_mg;
    int16_t ay_mg;
    int16_t az_mg;
    int16_t gx_dps;
    int16_t gy_dps;
    int16_t gz_dps;
    uint8_t motion;
    uint8_t posture_changed;

    uint8_t vl53_valid;
    uint8_t vl53_range_valid;
    uint16_t distance_mm;

    uint8_t pot_valid;
    uint8_t pot_percent;
    uint16_t pot_raw;
} AppSystemSnapshot;

void AppSnapshot_Init(void);
void AppSnapshot_UpdateFromSensor(const AppMessage *msg);
uint8_t AppSnapshot_Get(AppSystemSnapshot *snapshot);
