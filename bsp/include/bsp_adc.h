#pragma once

#include <stdint.h>

typedef enum {
    BSP_ADC_OK = 0,
    BSP_ADC_TIMEOUT,
} BspAdcStatus;

void BSP_ADC1_PA0_Init(void);
BspAdcStatus BSP_ADC1_PA0_Read(uint16_t *raw);
uint8_t BSP_ADC_RawToPercent(uint16_t raw);
