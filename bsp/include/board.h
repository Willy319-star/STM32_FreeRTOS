#pragma once

#include "stm32f4xx_hal.h"

#define BOARD_LED_GPIO_PORT      GPIOB
#define BOARD_LED_GPIO_PIN       GPIO_PIN_2
#define BOARD_LED_ACTIVE_LOW     0

void Board_Init(void);
void Board_Clock_Init(void);
void Board_LED_Init(void);
void Board_LED_On(void);
void Board_LED_Off(void);
void Board_LED_Toggle(void);
void Board_Error_Handler(void);