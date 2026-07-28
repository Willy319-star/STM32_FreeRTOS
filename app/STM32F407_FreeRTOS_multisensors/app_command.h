#pragma once

#include <stdint.h>

#include "FreeRTOS.h"

void AppCommand_Start(void);
void AppCommand_OnRxByteFromISR(uint8_t byte, BaseType_t *higher_priority_woken);

