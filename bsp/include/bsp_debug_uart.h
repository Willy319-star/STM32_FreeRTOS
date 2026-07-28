#pragma once

#include <stdint.h>

void BSP_DebugUART_Init(void);
void BSP_DebugUART_InitEarlyHSI(void);
void BSP_DebugUART_EnableRxInterrupt(void);
uint8_t BSP_DebugUART_ReadRxByteFromISR(uint8_t *byte);
void BSP_DebugUART_WriteChar(char ch);
void BSP_DebugUART_WriteBytes(const uint8_t *data, uint16_t len);
void BSP_DebugUART_WriteString(const char *text);
