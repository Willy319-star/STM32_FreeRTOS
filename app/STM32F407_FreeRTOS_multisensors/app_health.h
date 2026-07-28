#pragma once

#include <stdint.h>

typedef enum {
    APP_HEALTH_HEART = 0,
    APP_HEALTH_WORKER,
    APP_HEALTH_SYSMON,
    APP_HEALTH_CONTROL,
    APP_HEALTH_DHT11,
    APP_HEALTH_MPU,
    APP_HEALTH_VL53,
    APP_HEALTH_POT,
    APP_HEALTH_LCD,
    APP_HEALTH_SLOG,
    APP_HEALTH_COUNT
} AppHealthTaskId;

void AppHealth_Start(void);
void AppHealth_PrintResetCauseEarly(void);
void AppHealth_Report(AppHealthTaskId id);
const char *AppHealth_TaskName(AppHealthTaskId id);
uint8_t AppHealth_FindTaskId(const char *name, AppHealthTaskId *id);
void AppHealth_InjectFault(AppHealthTaskId id);
void AppHealth_ClearFaults(void);
uint32_t AppHealth_GetFaultMask(void);
void AppHealth_PrintStatus(void);
