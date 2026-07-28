#include "bsp_adc.h"

#include "board.h"

#define ADC_TIMEOUT_LOOP 100000U

void BSP_ADC1_PA0_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_ADC1_CLK_ENABLE();

    gpio.Pin = GPIO_PIN_0;
    gpio.Mode = GPIO_MODE_ANALOG;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &gpio);

    ADC->CCR &= ~ADC_CCR_ADCPRE;
    ADC->CCR |= ADC_CCR_ADCPRE_0;

    ADC1->CR1 = 0U;
    ADC1->CR2 = 0U;
    ADC1->SMPR2 &= ~ADC_SMPR2_SMP0;
    ADC1->SMPR2 |= ADC_SMPR2_SMP0_2 | ADC_SMPR2_SMP0_1;
    ADC1->SQR1 = 0U;
    ADC1->SQR2 = 0U;
    ADC1->SQR3 = 0U;
    ADC1->CR2 |= ADC_CR2_ADON;
}

BspAdcStatus BSP_ADC1_PA0_Read(uint16_t *raw)
{
    uint32_t timeout = ADC_TIMEOUT_LOOP;

    if (raw == 0) {
        return BSP_ADC_TIMEOUT;
    }

    ADC1->SR = 0U;
    ADC1->CR2 |= ADC_CR2_SWSTART;

    while ((ADC1->SR & ADC_SR_EOC) == 0U) {
        if (timeout-- == 0U) {
            return BSP_ADC_TIMEOUT;
        }
    }

    *raw = (uint16_t)(ADC1->DR & 0x0FFFU);
    return BSP_ADC_OK;
}

uint8_t BSP_ADC_RawToPercent(uint16_t raw)
{
    if (raw > 4095U) {
        raw = 4095U;
    }
    return (uint8_t)(((uint32_t)raw * 100U + 2047U) / 4095U);
}
