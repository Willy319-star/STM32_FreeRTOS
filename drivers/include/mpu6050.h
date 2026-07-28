#pragma once

#include <stdint.h>

typedef struct {
    int16_t ax_mg;
    int16_t ay_mg;
    int16_t az_mg;
    int16_t gx_dps;
    int16_t gy_dps;
    int16_t gz_dps;
} MPU6050_Data;

int MPU6050_Init(void);
int MPU6050_ReadData(MPU6050_Data *data);
int MPU6050_ReadWhoAmI(uint8_t *id);
uint8_t MPU6050_Address(void);
