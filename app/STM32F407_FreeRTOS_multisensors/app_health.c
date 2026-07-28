#include "app_health.h"

#include "FreeRTOS.h"
#include "app_observe.h"
#include "board.h"
#include "bsp_debug_uart.h"
#include "stm32f407xx.h"
#include "task.h"

#include <stdio.h>
#include <string.h>

#define HEALTH_CHECK_PERIOD_MS       1000U
#define HEALTH_STARTUP_GRACE_MS      3000U
#define IWDG_RELOAD_VALUE            3000U
#define IWDG_PRESCALER_DIV64         4U
#define IWDG_KEY_ENABLE              0xCCCCU
#define IWDG_KEY_RELOAD              0xAAAAU
#define IWDG_KEY_WRITE_ACCESS        0x5555U

typedef struct {
    const char *name;
    TickType_t timeout_ticks;
} HealthEntry;

static const HealthEntry s_health_table[APP_HEALTH_COUNT] = {
    [APP_HEALTH_HEART]   = {"HEART",   pdMS_TO_TICKS(3000U)},
    [APP_HEALTH_WORKER]  = {"WORKER",  pdMS_TO_TICKS(6000U)},
    [APP_HEALTH_SYSMON]  = {"SYSMON",  pdMS_TO_TICKS(8000U)},
    [APP_HEALTH_CONTROL] = {"CONTROL", pdMS_TO_TICKS(4000U)},
    [APP_HEALTH_DHT11]   = {"DHT11",   pdMS_TO_TICKS(7000U)},
    [APP_HEALTH_MPU]     = {"MPU",     pdMS_TO_TICKS(2000U)},
    [APP_HEALTH_VL53]    = {"VL53",    pdMS_TO_TICKS(3000U)},
    [APP_HEALTH_POT]     = {"POT",     pdMS_TO_TICKS(3000U)},
    [APP_HEALTH_LCD]     = {"LCD",     pdMS_TO_TICKS(4000U)},
    [APP_HEALTH_SLOG]    = {"SLOG",    pdMS_TO_TICKS(3000U)},
};

static volatile TickType_t s_last_report[APP_HEALTH_COUNT];
static volatile uint32_t s_report_mask;
static volatile uint32_t s_injected_fault_mask;
static volatile uint8_t s_boot_was_iwdg_reset;
static volatile uint8_t s_iwdg_started;
static volatile uint32_t s_health_check_count;
static volatile uint32_t s_health_fail_count;

void AppHealth_PrintResetCauseEarly(void)
{
    if ((RCC->CSR & RCC_CSR_IWDGRSTF) != 0U) {
        s_boot_was_iwdg_reset = 1U;
        BSP_DebugUART_WriteString("BOOT RESET CAUSE: IWDG\r\n");
    }

    RCC->CSR |= RCC_CSR_RMVF;
}

void AppHealth_Report(AppHealthTaskId id)
{
    uint32_t bit;

    if ((uint32_t)id >= APP_HEALTH_COUNT) {
        return;
    }

    bit = (1UL << (uint32_t)id);
    if ((s_injected_fault_mask & bit) != 0UL) {
        return;
    }

    s_last_report[id] = xTaskGetTickCount();
    s_report_mask |= bit;
}

const char *AppHealth_TaskName(AppHealthTaskId id)
{
    if ((uint32_t)id >= APP_HEALTH_COUNT) {
        return "UNKNOWN";
    }

    return s_health_table[id].name;
}

uint8_t AppHealth_FindTaskId(const char *name, AppHealthTaskId *id)
{
    if (name == NULL || id == NULL) {
        return 0U;
    }

    for (uint8_t i = 0U; i < APP_HEALTH_COUNT; i++) {
        if (strcmp(name, s_health_table[i].name) == 0) {
            *id = (AppHealthTaskId)i;
            return 1U;
        }
    }

    return 0U;
}

void AppHealth_InjectFault(AppHealthTaskId id)
{
    uint32_t bit;

    if ((uint32_t)id >= APP_HEALTH_COUNT) {
        return;
    }

    bit = (1UL << (uint32_t)id);
    s_injected_fault_mask |= bit;
    s_report_mask &= ~bit;
    s_last_report[id] = 0U;
}

void AppHealth_ClearFaults(void)
{
    s_injected_fault_mask = 0UL;
}

uint32_t AppHealth_GetFaultMask(void)
{
    return s_injected_fault_mask;
}

void AppHealth_PrintStatus(void)
{
    char line[160];

    snprintf(line, sizeof(line),
             "HEALTH STATUS tick=%lu iwdg_started=%u checks=%lu fails=%lu fault_mask=0x%04lX report_mask=0x%04lX\r\n",
             (unsigned long)xTaskGetTickCount(),
             (unsigned int)s_iwdg_started,
             (unsigned long)s_health_check_count,
             (unsigned long)s_health_fail_count,
             (unsigned long)s_injected_fault_mask,
             (unsigned long)s_report_mask);
    AppObserve_WriteLine(line);
}

static void iwdg_start(void)
{
    IWDG->KR = IWDG_KEY_WRITE_ACCESS;
    IWDG->PR = IWDG_PRESCALER_DIV64;
    IWDG->RLR = IWDG_RELOAD_VALUE;
    while ((IWDG->SR & (IWDG_SR_PVU | IWDG_SR_RVU)) != 0U) {
    }
    IWDG->KR = IWDG_KEY_RELOAD;
    IWDG->KR = IWDG_KEY_ENABLE;
    s_iwdg_started = 1U;
}

static void iwdg_feed(void)
{
    IWDG->KR = IWDG_KEY_RELOAD;
}

static uint8_t all_tasks_healthy(TickType_t now, char *line, uint16_t line_size)
{
    uint32_t mask = s_report_mask;
    uint32_t injected_mask = s_injected_fault_mask;

    for (uint8_t i = 0U; i < APP_HEALTH_COUNT; i++) {
        TickType_t last = s_last_report[i];
        if ((injected_mask & (1UL << i)) != 0U) {
            snprintf(line, line_size,
                     "HEALTH FAIL task=%s reason=injected now=%lu fault_mask=0x%04lX\r\n",
                     s_health_table[i].name,
                     (unsigned long)now,
                     (unsigned long)injected_mask);
            return 0U;
        }

        if ((mask & (1UL << i)) == 0U ||
            (TickType_t)(now - last) > s_health_table[i].timeout_ticks) {
            snprintf(line, line_size,
                     "HEALTH FAIL task=%s now=%lu last=%lu timeout=%lu\r\n",
                     s_health_table[i].name,
                     (unsigned long)now,
                     (unsigned long)last,
                     (unsigned long)s_health_table[i].timeout_ticks);
            return 0U;
        }
    }

    return 1U;
}

static void TaskHealthMonitor(void *argument)
{
    (void)argument;
    TickType_t last_wake = xTaskGetTickCount();
    char line[128];
    uint32_t ok_count = 0U;

    if (s_boot_was_iwdg_reset || (RCC->CSR & RCC_CSR_IWDGRSTF) != 0U) {
        AppObserve_WriteLine("HEALTH boot evidence: previous reset was IWDG\r\n");
    }
    RCC->CSR |= RCC_CSR_RMVF;

    AppObserve_WriteLine("HealthMonitor start: task heartbeat gate before IWDG feed\r\n");
    vTaskDelay(pdMS_TO_TICKS(HEALTH_STARTUP_GRACE_MS));

    TickType_t now = xTaskGetTickCount();
    for (uint8_t i = 0U; i < APP_HEALTH_COUNT; i++) {
        s_last_report[i] = now;
        s_report_mask |= (1UL << i);
    }

    iwdg_start();
    AppObserve_WriteLine("IWDG start: LSI/64 reload=3000 approx_timeout=6s\r\n");

    for (;;) {
        now = xTaskGetTickCount();
        s_health_check_count++;
        if (all_tasks_healthy(now, line, sizeof(line))) {
            iwdg_feed();
            ok_count++;
            if ((ok_count % 5UL) == 0UL) {
                snprintf(line, sizeof(line),
                         "HEALTH OK checked=%lu iwdg=fed\r\n",
                         (unsigned long)ok_count);
                AppObserve_WriteLine(line);
            }
        } else {
            s_health_fail_count++;
            AppObserve_WriteLine(line);
            AppObserve_WriteLine("HEALTH IWDG feed skipped, waiting for reset if fault persists\r\n");
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(HEALTH_CHECK_PERIOD_MS));
    }
}

void AppHealth_Start(void)
{
    TaskHandle_t health_handle = NULL;

    if (xTaskCreate(TaskHealthMonitor, "HEALTH", 512U, NULL,
                    tskIDLE_PRIORITY + 1U, &health_handle) != pdPASS) {
        Board_Error_Handler();
    }

    AppObserve_RegisterTask("HEALTH", health_handle);
}
