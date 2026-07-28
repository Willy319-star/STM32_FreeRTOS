#include "FreeRTOS.h"
#include "app_command.h"
#include "app_health.h"
#include "app_observe.h"
#include "app_sensor.h"
#include "board.h"
#include "bsp_debug_uart.h"
#include "bsp_time.h"
#include "task.h"

int main(void)
{
    BSP_DebugUART_InitEarlyHSI();
    BSP_DebugUART_WriteString("\r\nBOOT: early UART ready\r\n");
    AppHealth_PrintResetCauseEarly();

    Board_Init();
    Board_LED_Release();
    BSP_DebugUART_Init();
    BSP_DebugUART_WriteString("BOOT: board and UART initialized\r\n");
    BSP_Time_Init();
    BSP_DebugUART_WriteString("BOOT: DWT microsecond timer initialized\r\n");

    AppObserve_Start();
    AppCommand_Start();
    AppSensor_Start();
    AppHealth_Start();

    BSP_DebugUART_WriteString("BOOT: starting FreeRTOS scheduler\r\n");
    vTaskStartScheduler();

    Board_Error_Handler();
}
