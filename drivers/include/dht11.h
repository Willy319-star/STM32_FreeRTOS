#pragma once

#include <stdint.h>

typedef struct {
    uint8_t humidity;
    uint8_t temperature;
} DHT11_Data;

void DHT11_GPIO_Init(void);
int DHT11_Read(DHT11_Data *data);
const char *DHT11_LastError(void);
