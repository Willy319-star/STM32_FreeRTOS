#include "bsp_time.h"

#include "stm32f407xx.h"

void BSP_Time_Init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

void BSP_DelayUs(uint32_t us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t cycles = (SystemCoreClock / 1000000U) * us;

    while ((uint32_t)(DWT->CYCCNT - start) < cycles) {
    }
}
