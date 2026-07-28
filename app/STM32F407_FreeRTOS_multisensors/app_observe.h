#pragma once

#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

typedef enum {
    APP_LOG_ERROR = 0,
    APP_LOG_WARN = 1,
    APP_LOG_INFO = 2,
    APP_LOG_DEBUG = 3,
} AppLogLevel;

void AppObserve_Start(void);
void AppObserve_RegisterTask(const char *name, TaskHandle_t handle);
void AppObserve_WriteBytes(const uint8_t *data, uint16_t len);
void AppObserve_WriteLine(const char *text);
void AppObserve_WriteLineLevel(AppLogLevel level, const char *text);
void AppObserve_SetLogLevel(AppLogLevel level);
AppLogLevel AppObserve_GetLogLevel(void);
const char *AppObserve_LogLevelName(AppLogLevel level);
