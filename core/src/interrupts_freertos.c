#include "FreeRTOS.h"
#include "app_command.h"
#include "app_power.h"
#include "bsp_debug_uart.h"
#include "task.h"
#include "stm32f4xx_hal.h"

void xPortSysTickHandler(void);

void SysTick_Handler(void)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
        HAL_IncTick();
    } else {
        xPortSysTickHandler();
    }
}

void USART1_IRQHandler(void)
{
    uint8_t byte;
    BaseType_t higher_priority_woken = pdFALSE;

    if (BSP_DebugUART_ReadRxByteFromISR(&byte)) {
        AppCommand_OnRxByteFromISR(byte, &higher_priority_woken);
    }

    portYIELD_FROM_ISR(higher_priority_woken);
}

void vApplicationMallocFailedHook(void)
{
    taskDISABLE_INTERRUPTS();
    while (1) {
    }
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    (void)task;
    (void)task_name;
    taskDISABLE_INTERRUPTS();
    while (1) {
    }
}

void vApplicationIdleHook(void)
{
    AppPower_IdleHookNotify();
    __WFI();
}

void vApplicationGetIdleTaskMemory(StaticTask_t **tcb,
                                   StackType_t **stack,
                                   uint32_t *stack_size)
{
    (void)tcb;
    (void)stack;
    (void)stack_size;
}
