#pragma once

#include "app_messages.h"

typedef struct {
    uint8_t temp_hot_c;
    uint8_t humidity_dry_pct;
    uint16_t distance_near_mm;
    uint8_t pot_high_pct;
    int16_t gyro_motion_dps;
} AppControlConfig;

void AppControl_Start(void);
void AppControl_UpdateFromSensor(const AppMessage *msg);
uint32_t AppControl_GetAlertBits(void);
void AppControl_GetConfig(AppControlConfig *config);
void AppControl_ResetConfigDefaults(void);
uint8_t AppControl_SetTempHot(uint8_t value);
uint8_t AppControl_SetHumidityDry(uint8_t value);
uint8_t AppControl_SetDistanceNear(uint16_t value);
uint8_t AppControl_SetPotHigh(uint8_t value);
uint8_t AppControl_SetGyroMotion(int16_t value);
uint8_t AppControl_LoadConfigFromFlash(void);
uint8_t AppControl_SaveConfigToFlash(void);
