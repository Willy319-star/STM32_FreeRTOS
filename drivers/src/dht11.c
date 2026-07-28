#include "dht11.h"

#include "FreeRTOS.h"
#include "board.h"
#include "bsp_time.h"
#include "task.h"

#define DHT11_PORT GPIOC
#define DHT11_PIN  GPIO_PIN_0

static const char *s_last_error = "not-read";

static void dht_as_output(void)
{
    GPIO_InitTypeDef gpio = {0};

    gpio.Pin = DHT11_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_OD;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(DHT11_PORT, &gpio);
}

static void dht_as_input(void)
{
    GPIO_InitTypeDef gpio = {0};

    gpio.Pin = DHT11_PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(DHT11_PORT, &gpio);
}

void DHT11_GPIO_Init(void)
{
    __HAL_RCC_GPIOC_CLK_ENABLE();
    dht_as_output();
    HAL_GPIO_WritePin(DHT11_PORT, DHT11_PIN, GPIO_PIN_SET);
    s_last_error = "init";
}

static int wait_level(GPIO_PinState level, uint32_t timeout_us)
{
    while (HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN) != level) {
        if (timeout_us-- == 0U) {
            return 0;
        }
        BSP_DelayUs(1U);
    }
    return 1;
}

int DHT11_Read(DHT11_Data *data)
{
    uint8_t bytes[5] = {0};

    if (data == 0) {
        s_last_error = "null";
        return 0;
    }

    dht_as_input();
    if (HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN) == GPIO_PIN_RESET) {
        s_last_error = "idle-low";
        return 0;
    }

    dht_as_output();
    HAL_GPIO_WritePin(DHT11_PORT, DHT11_PIN, GPIO_PIN_RESET);
    vTaskDelay(pdMS_TO_TICKS(20));

    taskENTER_CRITICAL();
    HAL_GPIO_WritePin(DHT11_PORT, DHT11_PIN, GPIO_PIN_SET);
    BSP_DelayUs(30U);
    dht_as_input();

    if (!wait_level(GPIO_PIN_RESET, 100U)) {
        s_last_error = "no-response-low";
        taskEXIT_CRITICAL();
        return 0;
    }
    if (!wait_level(GPIO_PIN_SET, 100U)) {
        s_last_error = "no-response-high";
        taskEXIT_CRITICAL();
        return 0;
    }
    if (!wait_level(GPIO_PIN_RESET, 100U)) {
        s_last_error = "response-end-timeout";
        taskEXIT_CRITICAL();
        return 0;
    }

    for (uint8_t i = 0U; i < 40U; i++) {
        if (!wait_level(GPIO_PIN_SET, 70U)) {
            s_last_error = "bit-high-timeout";
            taskEXIT_CRITICAL();
            return 0;
        }

        BSP_DelayUs(40U);
        if (HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN) == GPIO_PIN_SET) {
            bytes[i / 8U] |= (uint8_t)(1U << (7U - (i % 8U)));
            if (!wait_level(GPIO_PIN_RESET, 100U)) {
                s_last_error = "bit-low-timeout";
                taskEXIT_CRITICAL();
                return 0;
            }
        }
    }
    taskEXIT_CRITICAL();

    if ((uint8_t)(bytes[0] + bytes[1] + bytes[2] + bytes[3]) != bytes[4]) {
        s_last_error = "checksum";
        return 0;
    }

    data->humidity = bytes[0];
    data->temperature = bytes[2];
    s_last_error = "ok";
    return 1;
}

const char *DHT11_LastError(void)
{
    return s_last_error;
}
