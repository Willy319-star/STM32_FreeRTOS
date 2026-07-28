#include "app_observe.h"

#include "FreeRTOS.h"
#include "app_health.h"
#include "app_power.h"
#include "board.h"
#include "bsp_debug_uart.h"
#include "semphr.h"
#include "task.h"

#include <stdio.h>
#include <string.h>

#define APP_OBSERVE_MAX_TASKS 14U

typedef struct {
    const char *name;
    TaskHandle_t handle;
} ObservedTask;

static SemaphoreHandle_t s_uart_mutex;
static ObservedTask s_observed_tasks[APP_OBSERVE_MAX_TASKS];
static uint8_t s_observed_task_count;
static volatile AppLogLevel s_log_level = APP_LOG_INFO;

void AppObserve_SetLogLevel(AppLogLevel level)
{
    if (level > APP_LOG_DEBUG) {
        level = APP_LOG_INFO;
    }
    s_log_level = level;
}

AppLogLevel AppObserve_GetLogLevel(void)
{
    return s_log_level;
}

const char *AppObserve_LogLevelName(AppLogLevel level)
{
    switch (level) {
    case APP_LOG_ERROR:
        return "ERROR";
    case APP_LOG_WARN:
        return "WARN";
    case APP_LOG_INFO:
        return "INFO";
    case APP_LOG_DEBUG:
        return "DEBUG";
    default:
        return "INFO";
    }
}

static AppLogLevel infer_log_level(const char *text)
{
    if (text == NULL) {
        return APP_LOG_INFO;
    }

    if (strstr(text, "FAIL") != NULL ||
        strstr(text, "ERROR") != NULL ||
        strstr(text, "IWDG feed skipped") != NULL ||
        strstr(text, "BOOT RESET CAUSE") != NULL) {
        return APP_LOG_ERROR;
    }

    if (strstr(text, "WARN") != NULL ||
        strstr(text, "STALE") != NULL ||
        strstr(text, "queue full") != NULL ||
        strstr(text, "FAULT") != NULL ||
        ((strstr(text, "ALERT bits=0x0000") == NULL) && strncmp(text, "ALERT bits=0x", 13U) == 0) ||
        strncmp(text, "CMD", 3U) == 0 ||
        strncmp(text, "\r\nCMD", 5U) == 0 ||
        strncmp(text, "  ", 2U) == 0 ||
        strncmp(text, "XCOM tip", 8U) == 0) {
        return APP_LOG_WARN;
    }

    if (strncmp(text, "HEARTBEAT", 9U) == 0 ||
        strncmp(text, "WORKER alive", 12U) == 0 ||
        strncmp(text, "SYS ", 4U) == 0 ||
        strncmp(text, "POWER ", 6U) == 0 ||
        strncmp(text, "STACK ", 6U) == 0 ||
        strncmp(text, "ALERT bits=0x", 13U) == 0 ||
        strncmp(text, "DHT11 OK", 8U) == 0 ||
        strncmp(text, "MPU6050 OK", 10U) == 0 ||
        strncmp(text, "VL53L0X RANGE_OK", 16U) == 0 ||
        strncmp(text, "POT OK", 6U) == 0 ||
        strncmp(text, "HEALTH OK", 9U) == 0) {
        return APP_LOG_INFO;
    }

    return APP_LOG_INFO;
}

static uint8_t should_suppress(AppLogLevel level)
{
    return (level > s_log_level) ? 1U : 0U;
}

void AppObserve_RegisterTask(const char *name, TaskHandle_t handle)
{
    if (name == NULL || handle == NULL) {
        return;
    }

    taskENTER_CRITICAL();
    if (s_observed_task_count < APP_OBSERVE_MAX_TASKS) {
        s_observed_tasks[s_observed_task_count].name = name;
        s_observed_tasks[s_observed_task_count].handle = handle;
        s_observed_task_count++;
    }
    taskEXIT_CRITICAL();
}

void AppObserve_WriteBytes(const uint8_t *data, uint16_t len)
{
    if (s_uart_mutex == NULL) {
        BSP_DebugUART_WriteBytes(data, len);
        return;
    }

    if (xSemaphoreTake(s_uart_mutex, portMAX_DELAY) == pdTRUE) {
        BSP_DebugUART_WriteBytes(data, len);
        xSemaphoreGive(s_uart_mutex);
    }
}

void AppObserve_WriteLine(const char *text)
{
    AppObserve_WriteLineLevel(infer_log_level(text), text);
}

void AppObserve_WriteLineLevel(AppLogLevel level, const char *text)
{
    if (should_suppress(level)) {
        return;
    }

    if (s_uart_mutex == NULL) {
        BSP_DebugUART_WriteString(text);
        return;
    }

    if (xSemaphoreTake(s_uart_mutex, portMAX_DELAY) == pdTRUE) {
        BSP_DebugUART_WriteString(text);
        xSemaphoreGive(s_uart_mutex);
    }
}

/*
 * Context: FreeRTOS task.
 * Purpose: Prove that the scheduler is alive and expose basic resource data.
 */
static void TaskHeartbeat(void *argument)
{
    (void)argument;
    TickType_t last_wake = xTaskGetTickCount();
    uint32_t count = 0U;
    char line[96];

    for (;;) {
        AppHealth_Report(APP_HEALTH_HEART);
        snprintf(line, sizeof(line),
                 "HEARTBEAT count=%lu tick=%lu heap=%lu\r\n",
                 (unsigned long)count++,
                 (unsigned long)xTaskGetTickCount(),
                 (unsigned long)xPortGetFreeHeapSize());
        AppObserve_WriteLine(line);
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(1000));
    }
}

/*
 * Context: FreeRTOS task.
 * Purpose: Provide a second periodic task so scheduling can be observed.
 */
static void TaskWorker(void *argument)
{
    (void)argument;
    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {
        AppHealth_Report(APP_HEALTH_WORKER);
        AppObserve_WriteLine("WORKER alive\r\n");
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(3000));
    }
}

/*
 * Context: FreeRTOS task.
 * Purpose: Expose heap and per-task stack high-water marks.
 */
static void TaskSystemMonitor(void *argument)
{
    (void)argument;
    TickType_t last_wake = xTaskGetTickCount();
    char line[128];

    for (;;) {
        ObservedTask local[APP_OBSERVE_MAX_TASKS];
        uint8_t count;

        AppHealth_Report(APP_HEALTH_SYSMON);
        taskENTER_CRITICAL();
        count = s_observed_task_count;
        for (uint8_t i = 0U; i < count; i++) {
            local[i] = s_observed_tasks[i];
        }
        taskEXIT_CRITICAL();

        snprintf(line, sizeof(line),
                 "SYS heap=%lu min_heap=%lu task_count=%u\r\n",
                 (unsigned long)xPortGetFreeHeapSize(),
                 (unsigned long)xPortGetMinimumEverFreeHeapSize(),
                 (unsigned int)count);
        AppObserve_WriteLine(line);

        snprintf(line, sizeof(line),
                 "POWER idle_hook=%lu wfi=enabled period_ms=5000\r\n",
                 (unsigned long)AppPower_GetAndResetIdleHookCount());
        AppObserve_WriteLine(line);

        for (uint8_t i = 0U; i < count; i++) {
            snprintf(line, sizeof(line),
                     "STACK %s high_water=%lu words\r\n",
                     local[i].name,
                     (unsigned long)uxTaskGetStackHighWaterMark(local[i].handle));
            AppObserve_WriteLine(line);
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(5000));
    }
}

void AppObserve_Start(void)
{
    TaskHandle_t heartbeat_handle = NULL;
    TaskHandle_t worker_handle = NULL;
    TaskHandle_t monitor_handle = NULL;

    s_uart_mutex = xSemaphoreCreateMutex();
    if (s_uart_mutex == NULL) {
        Board_Error_Handler();
    }

    if (xTaskCreate(TaskHeartbeat, "HEART", 256U, NULL,
                    tskIDLE_PRIORITY + 1U, &heartbeat_handle) != pdPASS ||
        xTaskCreate(TaskWorker, "WORKER", 256U, NULL,
                    tskIDLE_PRIORITY + 1U, &worker_handle) != pdPASS ||
        xTaskCreate(TaskSystemMonitor, "SYSMON", 512U, NULL,
                    tskIDLE_PRIORITY + 1U, &monitor_handle) != pdPASS) {
        Board_Error_Handler();
    }

    AppObserve_RegisterTask("HEART", heartbeat_handle);
    AppObserve_RegisterTask("WORKER", worker_handle);
    AppObserve_RegisterTask("SYSMON", monitor_handle);
}
