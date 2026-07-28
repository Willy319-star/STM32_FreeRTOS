#include "app_power.h"

#include "FreeRTOS.h"
#include "task.h"

static volatile uint32_t s_idle_hook_count;

void AppPower_IdleHookNotify(void)
{
    s_idle_hook_count++;
}

uint32_t AppPower_GetAndResetIdleHookCount(void)
{
    uint32_t count;

    taskENTER_CRITICAL();
    count = s_idle_hook_count;
    s_idle_hook_count = 0U;
    taskEXIT_CRITICAL();

    return count;
}

