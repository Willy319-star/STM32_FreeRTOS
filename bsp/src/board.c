#include "board.h"

void Board_Init(void)
{
    HAL_Init();
    Board_Clock_Init();
    Board_LED_Init();
}

void Board_Clock_Init(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_ON;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM = 8;
    osc.PLL.PLLN = 336;
    osc.PLL.PLLP = RCC_PLLP_DIV2;
    osc.PLL.PLLQ = 7;

    if (HAL_RCC_OscConfig(&osc) != HAL_OK) {
        Board_Error_Handler();
    }

    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                    RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV4;
    clk.APB2CLKDivider = RCC_HCLK_DIV2;

    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_5) != HAL_OK) {
        Board_Error_Handler();
    }
}

void Board_LED_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();

    gpio.Pin = BOARD_LED_GPIO_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(BOARD_LED_GPIO_PORT, &gpio);

    Board_LED_Off();
}

void Board_LED_On(void)
{
#if BOARD_LED_ACTIVE_LOW
    HAL_GPIO_WritePin(BOARD_LED_GPIO_PORT, BOARD_LED_GPIO_PIN, GPIO_PIN_RESET);
#else
    HAL_GPIO_WritePin(BOARD_LED_GPIO_PORT, BOARD_LED_GPIO_PIN, GPIO_PIN_SET);
#endif
}

void Board_LED_Off(void)
{
#if BOARD_LED_ACTIVE_LOW
    HAL_GPIO_WritePin(BOARD_LED_GPIO_PORT, BOARD_LED_GPIO_PIN, GPIO_PIN_SET);
#else
    HAL_GPIO_WritePin(BOARD_LED_GPIO_PORT, BOARD_LED_GPIO_PIN, GPIO_PIN_RESET);
#endif
}

void Board_LED_Toggle(void)
{
    HAL_GPIO_TogglePin(BOARD_LED_GPIO_PORT, BOARD_LED_GPIO_PIN);
}

void Board_Error_Handler(void)
{
    __disable_irq();
    while (1) {
    }
}