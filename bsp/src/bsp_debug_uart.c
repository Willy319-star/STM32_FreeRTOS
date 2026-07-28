#include "bsp_debug_uart.h"

#include "board.h"
#include "stm32f407xx.h"

static void uart1_gpio_init_registers(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    (void)RCC->AHB1ENR;
    (void)RCC->APB2ENR;

    GPIOA->MODER &= ~((3U << (9U * 2U)) | (3U << (10U * 2U)));
    GPIOA->MODER |= (2U << (9U * 2U)) | (2U << (10U * 2U));

    GPIOA->OTYPER &= ~((1U << 9U) | (1U << 10U));

    GPIOA->OSPEEDR |= (3U << (9U * 2U)) | (3U << (10U * 2U));

    GPIOA->PUPDR &= ~((3U << (9U * 2U)) | (3U << (10U * 2U)));
    GPIOA->PUPDR |= (1U << (9U * 2U)) | (1U << (10U * 2U));

    GPIOA->AFR[1] &= ~((0xFU << ((9U - 8U) * 4U)) | (0xFU << ((10U - 8U) * 4U)));
    GPIOA->AFR[1] |= (7U << ((9U - 8U) * 4U)) | (7U << ((10U - 8U) * 4U));
}

void BSP_DebugUART_InitEarlyHSI(void)
{
    uart1_gpio_init_registers();

    USART1->CR1 = 0U;
    USART1->CR2 = 0U;
    USART1->CR3 = 0U;

    /*
     * Before Board_Init(), APB2 is still driven from reset-clock HSI 16 MHz.
     * 16 MHz / 115200 = 138.8889 => BRR 0x008B.
     */
    USART1->BRR = 0x008BU;
    USART1->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
}

void BSP_DebugUART_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();

    gpio.Pin = GPIO_PIN_9 | GPIO_PIN_10;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOA, &gpio);

    USART1->CR1 = 0U;
    USART1->CR2 = 0U;
    USART1->CR3 = 0U;

    /* USART1 is on APB2. Board_Clock_Init sets APB2 to 84 MHz. */
    USART1->BRR = 0x02D9U;
    USART1->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
}

void BSP_DebugUART_EnableRxInterrupt(void)
{
    volatile uint32_t clear;

    clear = USART1->SR;
    clear = USART1->DR;
    (void)clear;

    HAL_NVIC_SetPriority(USART1_IRQn, 12U, 0U);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
    USART1->CR1 |= USART_CR1_RXNEIE;
}

uint8_t BSP_DebugUART_ReadRxByteFromISR(uint8_t *byte)
{
    uint32_t sr;

    if (byte == 0) {
        return 0U;
    }

    sr = USART1->SR;
    if ((sr & USART_SR_RXNE) != 0U) {
        *byte = (uint8_t)USART1->DR;
        return 1U;
    }

    if ((sr & (USART_SR_ORE | USART_SR_NE | USART_SR_FE | USART_SR_PE)) != 0U) {
        volatile uint32_t clear = USART1->DR;
        (void)clear;
    }

    return 0U;
}

void BSP_DebugUART_WriteChar(char ch)
{
    if (ch == '\n') {
        BSP_DebugUART_WriteChar('\r');
    }
    while ((USART1->SR & USART_SR_TXE) == 0U) {
    }
    USART1->DR = (uint8_t)ch;
}

void BSP_DebugUART_WriteBytes(const uint8_t *data, uint16_t len)
{
    if (data == 0) {
        return;
    }

    for (uint16_t i = 0U; i < len; i++) {
        while ((USART1->SR & USART_SR_TXE) == 0U) {
        }
        USART1->DR = data[i];
    }
}

void BSP_DebugUART_WriteString(const char *text)
{
    while (*text != '\0') {
        BSP_DebugUART_WriteChar(*text++);
    }
}
