#pragma once

#include <stdint.h>

typedef enum {
    APP_MSG_DHT11 = 0,
    APP_MSG_MPU6050,
    APP_MSG_VL53L0X,
    APP_MSG_POT,
} AppMessageType;

typedef struct {
    AppMessageType type;
    uint32_t tick;
    uint8_t valid;
    union {
        struct {
            uint8_t temperature;
            uint8_t humidity;
            uint8_t stale;
            const char *error;
        } dht11;
        struct {
            int16_t ax_mg;
            int16_t ay_mg;
            int16_t az_mg;
            int16_t gx_dps;
            int16_t gy_dps;
            int16_t gz_dps;
            uint8_t addr;
            uint8_t whoami;
            uint8_t motion;
            uint8_t posture_changed;
        } mpu6050;
        struct {
            uint8_t addr;
            uint8_t model_id;
            uint16_t distance_mm;
            uint8_t range_valid;
            uint8_t filtered;
            const char *status;
        } vl53l0x;
        struct {
            uint16_t raw;
            uint8_t percent;
            uint8_t filtered;
            const char *status;
        } pot;
    } data;
} AppMessage;
