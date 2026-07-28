#include "bsp_i2c.h"

#include "board.h"
#include "bsp_time.h"

typedef struct {
    GPIO_TypeDef *port;
    uint16_t scl;
    uint16_t sda;
} SoftI2cPins;

static const SoftI2cPins s_i2c1 = {GPIOB, GPIO_PIN_6, GPIO_PIN_7};
static const SoftI2cPins s_i2c2 = {GPIOB, GPIO_PIN_10, GPIO_PIN_11};

static void i2c_stop(const SoftI2cPins *pins);

static void i2c_delay(void)
{
    BSP_DelayUs(8U);
}

static void pin_high(GPIO_TypeDef *port, uint16_t pin)
{
    port->BSRR = pin;
}

static void pin_low(GPIO_TypeDef *port, uint16_t pin)
{
    port->BSRR = (uint32_t)pin << 16U;
}

static GPIO_PinState pin_read(GPIO_TypeDef *port, uint16_t pin)
{
    return ((port->IDR & pin) != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET;
}

static void scl_high(const SoftI2cPins *pins)
{
    pin_high(pins->port, pins->scl);
    i2c_delay();
}

static void scl_low(const SoftI2cPins *pins)
{
    pin_low(pins->port, pins->scl);
    i2c_delay();
}

static void sda_high(const SoftI2cPins *pins)
{
    pin_high(pins->port, pins->sda);
    i2c_delay();
}

static void sda_low(const SoftI2cPins *pins)
{
    pin_low(pins->port, pins->sda);
    i2c_delay();
}

static void soft_i2c_init(const SoftI2cPins *pins)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();

    gpio.Pin = pins->scl | pins->sda;
    gpio.Mode = GPIO_MODE_OUTPUT_OD;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(pins->port, &gpio);

    pin_high(pins->port, pins->scl);
    pin_high(pins->port, pins->sda);
}

static void soft_i2c_recover_bus(const SoftI2cPins *pins)
{
    scl_high(pins);
    sda_high(pins);
    for (uint8_t i = 0U; i < 9U; i++) {
        scl_high(pins);
        scl_low(pins);
    }
    i2c_stop(pins);
}

static void soft_i2c_read_lines(const SoftI2cPins *pins, uint8_t *scl_high_level, uint8_t *sda_high_level)
{
    if (scl_high_level != 0) {
        *scl_high_level = (pin_read(pins->port, pins->scl) == GPIO_PIN_SET) ? 1U : 0U;
    }
    if (sda_high_level != 0) {
        *sda_high_level = (pin_read(pins->port, pins->sda) == GPIO_PIN_SET) ? 1U : 0U;
    }
}

static void i2c_start(const SoftI2cPins *pins)
{
    sda_high(pins);
    scl_high(pins);
    sda_low(pins);
    scl_low(pins);
}

static void i2c_stop(const SoftI2cPins *pins)
{
    sda_low(pins);
    scl_high(pins);
    sda_high(pins);
}

static uint8_t i2c_write_byte(const SoftI2cPins *pins, uint8_t byte)
{
    for (uint8_t i = 0U; i < 8U; i++) {
        if ((byte & 0x80U) != 0U) {
            sda_high(pins);
        } else {
            sda_low(pins);
        }
        scl_high(pins);
        scl_low(pins);
        byte <<= 1U;
    }

    sda_high(pins);
    scl_high(pins);
    uint8_t ack = (pin_read(pins->port, pins->sda) == GPIO_PIN_RESET) ? 1U : 0U;
    scl_low(pins);
    return ack;
}

static uint8_t i2c_read_byte(const SoftI2cPins *pins, uint8_t ack)
{
    uint8_t byte = 0U;

    sda_high(pins);
    for (uint8_t i = 0U; i < 8U; i++) {
        byte <<= 1U;
        scl_high(pins);
        if (pin_read(pins->port, pins->sda) == GPIO_PIN_SET) {
            byte |= 1U;
        }
        scl_low(pins);
    }

    if (ack) {
        sda_low(pins);
    } else {
        sda_high(pins);
    }
    scl_high(pins);
    scl_low(pins);
    sda_high(pins);
    return byte;
}

static BspI2cStatus soft_i2c_is_ready(const SoftI2cPins *pins, uint8_t addr7)
{
    i2c_start(pins);
    uint8_t ack = i2c_write_byte(pins, (uint8_t)(addr7 << 1));
    i2c_stop(pins);

    return ack ? BSP_I2C_OK : BSP_I2C_NACK;
}

static BspI2cStatus soft_i2c_write_reg(const SoftI2cPins *pins,
                                       uint8_t addr7,
                                       uint8_t reg,
                                       const uint8_t *data,
                                       size_t len);

static BspI2cStatus soft_i2c_write_byte(const SoftI2cPins *pins,
                                        uint8_t addr7,
                                        uint8_t reg,
                                        uint8_t value)
{
    return soft_i2c_write_reg(pins, addr7, reg, &value, 1U);
}

static BspI2cStatus soft_i2c_write_reg(const SoftI2cPins *pins,
                                       uint8_t addr7,
                                       uint8_t reg,
                                       const uint8_t *data,
                                       size_t len)
{
    i2c_start(pins);
    if (data == 0 || len == 0U) {
        i2c_stop(pins);
        return BSP_I2C_ERROR;
    }

    if (!i2c_write_byte(pins, (uint8_t)(addr7 << 1)) ||
        !i2c_write_byte(pins, reg)) {
        i2c_stop(pins);
        return BSP_I2C_NACK;
    }

    for (size_t i = 0U; i < len; i++) {
        if (!i2c_write_byte(pins, data[i])) {
            i2c_stop(pins);
            return BSP_I2C_NACK;
        }
    }

    i2c_stop(pins);
    return BSP_I2C_OK;
}

static BspI2cStatus soft_i2c_read_reg(const SoftI2cPins *pins,
                                      uint8_t addr7,
                                      uint8_t reg,
                                      uint8_t *data,
                                      size_t len)
{
    if (data == 0 || len == 0U) {
        return BSP_I2C_ERROR;
    }

    i2c_start(pins);
    if (!i2c_write_byte(pins, (uint8_t)(addr7 << 1)) || !i2c_write_byte(pins, reg)) {
        i2c_stop(pins);
        return BSP_I2C_NACK;
    }

    i2c_start(pins);
    if (!i2c_write_byte(pins, (uint8_t)((addr7 << 1) | 1U))) {
        i2c_stop(pins);
        return BSP_I2C_NACK;
    }

    for (size_t i = 0U; i < len; i++) {
        data[i] = i2c_read_byte(pins, (i + 1U) < len);
    }

    i2c_stop(pins);
    return BSP_I2C_OK;
}

void BSP_I2C1_Init(void)
{
    soft_i2c_init(&s_i2c1);
}

void BSP_I2C1_RecoverBus(void)
{
    soft_i2c_recover_bus(&s_i2c1);
}

void BSP_I2C1_ReadLines(uint8_t *scl_high_level, uint8_t *sda_high_level)
{
    soft_i2c_read_lines(&s_i2c1, scl_high_level, sda_high_level);
}

BspI2cStatus BSP_I2C1_IsReady(uint8_t addr7)
{
    return soft_i2c_is_ready(&s_i2c1, addr7);
}

BspI2cStatus BSP_I2C1_WriteByte(uint8_t addr7, uint8_t reg, uint8_t value)
{
    return soft_i2c_write_byte(&s_i2c1, addr7, reg, value);
}

BspI2cStatus BSP_I2C1_WriteReg(uint8_t addr7, uint8_t reg, const uint8_t *data, size_t len)
{
    return soft_i2c_write_reg(&s_i2c1, addr7, reg, data, len);
}

BspI2cStatus BSP_I2C1_ReadReg(uint8_t addr7, uint8_t reg, uint8_t *data, size_t len)
{
    return soft_i2c_read_reg(&s_i2c1, addr7, reg, data, len);
}

void BSP_I2C2_Init(void)
{
    soft_i2c_init(&s_i2c2);
}

void BSP_I2C2_RecoverBus(void)
{
    soft_i2c_recover_bus(&s_i2c2);
}

void BSP_I2C2_ReadLines(uint8_t *scl_high_level, uint8_t *sda_high_level)
{
    soft_i2c_read_lines(&s_i2c2, scl_high_level, sda_high_level);
}

BspI2cStatus BSP_I2C2_IsReady(uint8_t addr7)
{
    return soft_i2c_is_ready(&s_i2c2, addr7);
}

BspI2cStatus BSP_I2C2_WriteByte(uint8_t addr7, uint8_t reg, uint8_t value)
{
    return soft_i2c_write_byte(&s_i2c2, addr7, reg, value);
}

BspI2cStatus BSP_I2C2_WriteReg(uint8_t addr7, uint8_t reg, const uint8_t *data, size_t len)
{
    return soft_i2c_write_reg(&s_i2c2, addr7, reg, data, len);
}

BspI2cStatus BSP_I2C2_ReadReg(uint8_t addr7, uint8_t reg, uint8_t *data, size_t len)
{
    return soft_i2c_read_reg(&s_i2c2, addr7, reg, data, len);
}
