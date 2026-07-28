#include "mpu6050.h"

#include "bsp_i2c.h"

#define MPU6050_WHO_AM_I   0x75U
#define MPU6050_PWR_MGMT_1 0x6BU
#define MPU6050_SMPLRT_DIV 0x19U
#define MPU6050_CONFIG     0x1AU
#define MPU6050_GYRO_CFG   0x1BU
#define MPU6050_ACCEL_CFG  0x1CU
#define MPU6050_DATA_REG   0x3BU

static uint8_t s_addr = 0x68U;

static uint8_t is_supported_id(uint8_t id)
{
    return (id == 0x68U || id == 0x74U) ? 1U : 0U;
}

static int16_t be16(const uint8_t *buf)
{
    return (int16_t)((uint16_t)buf[0] << 8 | buf[1]);
}

uint8_t MPU6050_Address(void)
{
    return s_addr;
}

int MPU6050_ReadWhoAmI(uint8_t *id)
{
    return BSP_I2C1_ReadReg(s_addr, MPU6050_WHO_AM_I, id, 1U) == BSP_I2C_OK;
}

int MPU6050_Init(void)
{
    uint8_t id = 0U;

    s_addr = 0x68U;
    if (!MPU6050_ReadWhoAmI(&id) || !is_supported_id(id)) {
        s_addr = 0x69U;
        if (!MPU6050_ReadWhoAmI(&id) || !is_supported_id(id)) {
            return 0;
        }
    }

    if (BSP_I2C1_WriteByte(s_addr, MPU6050_PWR_MGMT_1, 0x00U) != BSP_I2C_OK) {
        return 0;
    }
    (void)BSP_I2C1_WriteByte(s_addr, MPU6050_SMPLRT_DIV, 0x07U);
    (void)BSP_I2C1_WriteByte(s_addr, MPU6050_CONFIG, 0x03U);
    (void)BSP_I2C1_WriteByte(s_addr, MPU6050_GYRO_CFG, 0x00U);
    (void)BSP_I2C1_WriteByte(s_addr, MPU6050_ACCEL_CFG, 0x00U);
    return 1;
}

int MPU6050_ReadData(MPU6050_Data *data)
{
    uint8_t buf[14];

    if (data == 0) {
        return 0;
    }
    if (BSP_I2C1_ReadReg(s_addr, MPU6050_DATA_REG, buf, sizeof(buf)) != BSP_I2C_OK) {
        return 0;
    }

    data->ax_mg = (int16_t)((int32_t)be16(&buf[0]) * 1000 / 16384);
    data->ay_mg = (int16_t)((int32_t)be16(&buf[2]) * 1000 / 16384);
    data->az_mg = (int16_t)((int32_t)be16(&buf[4]) * 1000 / 16384);
    data->gx_dps = (int16_t)((int32_t)be16(&buf[8]) / 131);
    data->gy_dps = (int16_t)((int32_t)be16(&buf[10]) / 131);
    data->gz_dps = (int16_t)((int32_t)be16(&buf[12]) / 131);
    return 1;
}
