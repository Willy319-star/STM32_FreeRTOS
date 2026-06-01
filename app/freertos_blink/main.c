#include "FreeRTOS.h"
#include "task.h"
#include "board.h"

static void LED_Task(void *argument)
{
    (void)argument;

    for (;;) {
        Board_LED_Toggle();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

int main(void)
{
    Board_Init();

    BaseType_t ok = xTaskCreate(
        LED_Task,
        "LED",
        256,
        NULL,
        tskIDLE_PRIORITY + 1,
        NULL);

    if (ok != pdPASS) {
        Board_Error_Handler();
    }

    vTaskStartScheduler();
    Board_Error_Handler();
}