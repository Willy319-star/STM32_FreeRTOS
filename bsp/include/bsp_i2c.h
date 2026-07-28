#pragma once

#include <stddef.h>
#include <stdint.h>

typedef enum {
    BSP_I2C_OK = 0,
    BSP_I2C_NACK,
    BSP_I2C_ERROR,
} BspI2cStatus;

void BSP_I2C1_Init(void);
void BSP_I2C1_RecoverBus(void);
void BSP_I2C1_ReadLines(uint8_t *scl_high_level, uint8_t *sda_high_level);
BspI2cStatus BSP_I2C1_IsReady(uint8_t addr7);
BspI2cStatus BSP_I2C1_WriteByte(uint8_t addr7, uint8_t reg, uint8_t value);
BspI2cStatus BSP_I2C1_WriteReg(uint8_t addr7, uint8_t reg, const uint8_t *data, size_t len);
BspI2cStatus BSP_I2C1_ReadReg(uint8_t addr7, uint8_t reg, uint8_t *data, size_t len);

void BSP_I2C2_Init(void);
void BSP_I2C2_RecoverBus(void);
void BSP_I2C2_ReadLines(uint8_t *scl_high_level, uint8_t *sda_high_level);
BspI2cStatus BSP_I2C2_IsReady(uint8_t addr7);
BspI2cStatus BSP_I2C2_WriteByte(uint8_t addr7, uint8_t reg, uint8_t value);
BspI2cStatus BSP_I2C2_WriteReg(uint8_t addr7, uint8_t reg, const uint8_t *data, size_t len);
BspI2cStatus BSP_I2C2_ReadReg(uint8_t addr7, uint8_t reg, uint8_t *data, size_t len);
