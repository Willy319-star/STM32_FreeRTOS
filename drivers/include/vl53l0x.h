#pragma once

#include <stdint.h>

#define VL53L0X_ADDR 0x29U

typedef enum {
    VL53L0X_NOT_FOUND = 0,
    VL53L0X_PRESENT = 1,
    VL53L0X_RANGE_PENDING = 2,
} VL53L0X_Status;

int VL53L0X_Init(void);
int VL53L0X_ReadModelId(uint8_t *model_id);
const char *VL53L0X_LastStatus(void);
VL53L0X_Status VL53L0X_ReadDistance(uint16_t *distance_mm);
