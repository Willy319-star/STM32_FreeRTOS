#include "FreeRTOS.h"
#include "task.h"
#include "stm32f4xx_hal.h"

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

void vApplicationGetIdleTaskMemory(StaticTask_t **tcb,
                                   StackType_t **stack,
                                   uint32_t *stack_size)
{
    (void)tcb;
    (void)stack;
    (void)stack_size;
}