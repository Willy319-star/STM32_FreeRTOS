#include "vl53l0x.h"

#include "FreeRTOS.h"
#include "bsp_i2c.h"
#include "task.h"

#define REG_SYSRANGE_START                        0x00U
#define REG_SYSTEM_SEQUENCE_CONFIG                0x01U
#define REG_SYSTEM_INTERRUPT_CONFIG_GPIO          0x0AU
#define REG_SYSTEM_INTERRUPT_CLEAR                0x0BU
#define REG_RESULT_INTERRUPT_STATUS               0x13U
#define REG_RESULT_RANGE_STATUS                   0x14U
#define REG_FINAL_RANGE_CONFIG_MIN_COUNT_RATE_RTN 0x44U
#define REG_GLOBAL_CONFIG_SPAD_ENABLES_REF_0      0xB0U
#define REG_MSRC_CONFIG_CONTROL                   0x60U
#define REG_MODEL_ID                              0xC0U
#define REG_VHV_CONFIG_PAD_SCL_SDA_EXTSUP_HV      0x89U

static uint8_t s_present;
static uint8_t s_ready;
static uint8_t s_stop_variable;
static const char *s_last_status = "not-init";

static int wr8(uint8_t reg, uint8_t value)
{
    return BSP_I2C2_WriteByte(VL53L0X_ADDR, reg, value) == BSP_I2C_OK;
}

static int rd8(uint8_t reg, uint8_t *value)
{
    return BSP_I2C2_ReadReg(VL53L0X_ADDR, reg, value, 1U) == BSP_I2C_OK;
}

static void select_default_page(void)
{
    /*
     * The ranging init sequence switches VL53L0X internal register pages.
     * If that sequence exits early, a later MODEL_ID read can hit the wrong
     * page and look like model=0x00 even though the device still ACKs.
     */
    (void)BSP_I2C2_WriteByte(VL53L0X_ADDR, 0xFFU, 0x00U);
    (void)BSP_I2C2_WriteByte(VL53L0X_ADDR, 0x00U, 0x00U);
}

static int wr16(uint8_t reg, uint16_t value)
{
    uint8_t data[2] = {(uint8_t)(value >> 8), (uint8_t)value};

    return BSP_I2C2_WriteReg(VL53L0X_ADDR, reg, data, sizeof(data)) == BSP_I2C_OK;
}

static int rd16(uint8_t reg, uint16_t *value)
{
    uint8_t data[2];

    if (BSP_I2C2_ReadReg(VL53L0X_ADDR, reg, data, sizeof(data)) != BSP_I2C_OK) {
        return 0;
    }
    *value = (uint16_t)((uint16_t)data[0] << 8 | data[1]);
    return 1;
}

static int update8(uint8_t reg, uint8_t mask, uint8_t value)
{
    uint8_t old = 0U;

    if (!rd8(reg, &old)) {
        return 0;
    }
    old = (uint8_t)((old & (uint8_t)~mask) | (value & mask));
    return wr8(reg, old);
}

static int wait_reg_bits(uint8_t reg, uint8_t mask, uint8_t expected, uint32_t timeout_ms)
{
    TickType_t start = xTaskGetTickCount();
    uint8_t value = 0U;

    do {
        if (!rd8(reg, &value)) {
            return 0;
        }
        if ((value & mask) == expected) {
            return 1;
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    } while ((xTaskGetTickCount() - start) < pdMS_TO_TICKS(timeout_ms));

    return 0;
}

static int wait_reg_nonzero(uint8_t reg, uint8_t mask, uint32_t timeout_ms)
{
    TickType_t start = xTaskGetTickCount();
    uint8_t value = 0U;

    do {
        if (!rd8(reg, &value)) {
            return 0;
        }
        if ((value & mask) != 0U) {
            return 1;
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    } while ((xTaskGetTickCount() - start) < pdMS_TO_TICKS(timeout_ms));

    return 0;
}

int VL53L0X_ReadModelId(uint8_t *model_id)
{
    if (model_id == 0) {
        return 0;
    }

    select_default_page();
    return rd8(REG_MODEL_ID, model_id);
}

const char *VL53L0X_LastStatus(void)
{
    return s_last_status;
}

static int get_spad_info(uint8_t *count, uint8_t *type_is_aperture)
{
    TickType_t start;
    uint8_t tmp = 0U;

    if (!wr8(0x80U, 0x01U) || !wr8(0xFFU, 0x01U) || !wr8(0x00U, 0x00U) ||
        !wr8(0xFFU, 0x06U) || !update8(0x83U, 0x04U, 0x04U) ||
        !wr8(0xFFU, 0x07U) || !wr8(0x81U, 0x01U) || !wr8(0x80U, 0x01U) ||
        !wr8(0x94U, 0x6BU) || !wr8(0x83U, 0x00U)) {
        s_last_status = "spad-sequence-fail";
        return 0;
    }

    start = xTaskGetTickCount();
    do {
        if (!rd8(0x83U, &tmp)) {
            s_last_status = "spad-wait-read-fail";
            return 0;
        }
        if (tmp != 0x00U) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    } while ((xTaskGetTickCount() - start) < pdMS_TO_TICKS(100U));

    if (tmp == 0x00U) {
        s_last_status = "spad-wait-timeout";
        return 0;
    }

    if (!wr8(0x83U, 0x01U) || !rd8(0x92U, &tmp)) {
        s_last_status = "spad-value-read-fail";
        return 0;
    }

    *count = (uint8_t)(tmp & 0x7FU);
    *type_is_aperture = (uint8_t)((tmp >> 7) & 0x01U);

    if (!wr8(0x81U, 0x00U) || !wr8(0xFFU, 0x06U) ||
        !update8(0x83U, 0x04U, 0x00U) ||
        !wr8(0xFFU, 0x01U) || !wr8(0x00U, 0x01U) ||
        !wr8(0xFFU, 0x00U) || !wr8(0x80U, 0x00U)) {
        s_last_status = "spad-restore-fail";
        return 0;
    }

    return 1;
}

static int load_tuning_settings(void)
{
    static const uint8_t seq[][2] = {
        {0xFF,0x01},{0x00,0x00},{0xFF,0x00},{0x09,0x00},{0x10,0x00},{0x11,0x00},
        {0x24,0x01},{0x25,0xFF},{0x75,0x00},{0xFF,0x01},{0x4E,0x2C},{0x48,0x00},
        {0x30,0x20},{0xFF,0x00},{0x30,0x09},{0x54,0x00},{0x31,0x04},{0x32,0x03},
        {0x40,0x83},{0x46,0x25},{0x60,0x00},{0x27,0x00},{0x50,0x06},{0x51,0x00},
        {0x52,0x96},{0x56,0x08},{0x57,0x30},{0x61,0x00},{0x62,0x00},{0x64,0x00},
        {0x65,0x00},{0x66,0xA0},{0xFF,0x01},{0x22,0x32},{0x47,0x14},{0x49,0xFF},
        {0x4A,0x00},{0xFF,0x00},{0x7A,0x0A},{0x7B,0x00},{0x78,0x21},{0xFF,0x01},
        {0x23,0x34},{0x42,0x00},{0x44,0xFF},{0x45,0x26},{0x46,0x05},{0x40,0x40},
        {0x0E,0x06},{0x20,0x1A},{0x43,0x40},{0xFF,0x00},{0x34,0x03},{0x35,0x44},
        {0xFF,0x01},{0x31,0x04},{0x4B,0x09},{0x4C,0x05},{0x4D,0x04},{0xFF,0x00},
        {0x44,0x00},{0x45,0x20},{0x47,0x08},{0x48,0x28},{0x67,0x00},{0x70,0x04},
        {0x71,0x01},{0x72,0xFE},{0x76,0x00},{0x77,0x00},{0xFF,0x01},{0x0D,0x01},
        {0xFF,0x00},{0x80,0x01},{0x01,0xF8},{0xFF,0x01},{0x8E,0x01},{0x00,0x01},
        {0xFF,0x00},{0x80,0x00},
    };

    for (uint32_t i = 0U; i < (sizeof(seq) / sizeof(seq[0])); i++) {
        if (!wr8(seq[i][0], seq[i][1])) {
            return 0;
        }
    }
    return 1;
}

static int perform_ref_calibration(uint8_t vhv_init_byte)
{
    if (!wr8(REG_SYSRANGE_START, (uint8_t)(0x01U | vhv_init_byte))) {
        s_last_status = "cal-start-fail";
        return 0;
    }
    if (!wait_reg_nonzero(REG_RESULT_INTERRUPT_STATUS, 0x07U, 500U)) {
        s_last_status = "cal-timeout";
        return 0;
    }
    if (!wr8(REG_SYSTEM_INTERRUPT_CLEAR, 0x01U) || !wr8(REG_SYSRANGE_START, 0x00U)) {
        s_last_status = "cal-clear-fail";
        return 0;
    }
    return 1;
}

int VL53L0X_Init(void)
{
    uint8_t model_id = 0U;
    uint8_t spad_count = 0U;
    uint8_t spad_type_is_aperture = 0U;
    uint8_t ref_spad_map[6] = {0};
    uint8_t first_spad_to_enable;
    uint8_t spads_enabled = 0U;

    s_present = 0U;
    s_ready = 0U;
    s_last_status = "init-start";

    if (BSP_I2C2_IsReady(VL53L0X_ADDR) != BSP_I2C_OK) {
        s_last_status = "addr-nack";
        return 0;
    }
    if (!VL53L0X_ReadModelId(&model_id)) {
        s_last_status = "model-read-fail";
        return 0;
    }
    if (model_id != 0xEEU) {
        s_last_status = "model-id-mismatch";
        return 0;
    }
    s_present = 1U;

    s_last_status = "basic-config";
    if (!update8(REG_VHV_CONFIG_PAD_SCL_SDA_EXTSUP_HV, 0x01U, 0x01U) ||
        !wr8(0x88U, 0x00U) ||
        !wr8(0x80U, 0x01U) || !wr8(0xFFU, 0x01U) || !wr8(0x00U, 0x00U) ||
        !rd8(0x91U, &s_stop_variable) ||
        !wr8(0x00U, 0x01U) || !wr8(0xFFU, 0x00U) || !wr8(0x80U, 0x00U)) {
        s_last_status = "basic-config-fail";
        return 1;
    }

    s_last_status = "spad-config";
    if (!update8(REG_MSRC_CONFIG_CONTROL, 0x12U, 0x12U) ||
        !wr16(REG_FINAL_RANGE_CONFIG_MIN_COUNT_RATE_RTN, 32U) ||
        !wr8(REG_SYSTEM_SEQUENCE_CONFIG, 0xFFU)) {
        s_last_status = "spad-prep-fail";
        return 1;
    }

    if (!get_spad_info(&spad_count, &spad_type_is_aperture)) {
        select_default_page();
        return 1;
    }

    select_default_page();
    if (BSP_I2C2_ReadReg(VL53L0X_ADDR, REG_GLOBAL_CONFIG_SPAD_ENABLES_REF_0,
                         ref_spad_map, sizeof(ref_spad_map)) != BSP_I2C_OK) {
        s_last_status = "spad-map-read-fail";
        return 1;
    }

    first_spad_to_enable = spad_type_is_aperture ? 12U : 0U;
    for (uint8_t i = 0U; i < 48U; i++) {
        if (i < first_spad_to_enable || spads_enabled == spad_count) {
            ref_spad_map[i / 8U] &= (uint8_t)~(1U << (i % 8U));
        } else if ((ref_spad_map[i / 8U] & (1U << (i % 8U))) != 0U) {
            spads_enabled++;
        }
    }

    for (uint8_t i = 0U; i < sizeof(ref_spad_map); i++) {
        if (!wr8((uint8_t)(REG_GLOBAL_CONFIG_SPAD_ENABLES_REF_0 + i), ref_spad_map[i])) {
            s_last_status = "spad-map-write-fail";
            return 1;
        }
    }

    s_last_status = "tuning-config";
    if (!load_tuning_settings() ||
        !wr8(REG_SYSTEM_INTERRUPT_CONFIG_GPIO, 0x04U) ||
        !update8(0x84U, 0x10U, 0x00U) ||
        !wr8(REG_SYSTEM_INTERRUPT_CLEAR, 0x01U) ||
        !wr8(REG_SYSTEM_SEQUENCE_CONFIG, 0xE8U)) {
        s_last_status = "tuning-config-fail";
        return 1;
    }

    s_last_status = "ref-calibration";
    if (!wr8(REG_SYSTEM_SEQUENCE_CONFIG, 0x01U) ||
        !perform_ref_calibration(0x40U) ||
        !wr8(REG_SYSTEM_SEQUENCE_CONFIG, 0x02U) ||
        !perform_ref_calibration(0x00U) ||
        !wr8(REG_SYSTEM_SEQUENCE_CONFIG, 0xE8U)) {
        select_default_page();
        return 1;
    }

    select_default_page();
    s_ready = 1U;
    s_last_status = "ready";
    return 1;
}

VL53L0X_Status VL53L0X_ReadDistance(uint16_t *distance_mm)
{
    uint16_t range = 0U;
    uint8_t status = 0U;

    if (!s_present) {
        s_last_status = "not-found";
        return VL53L0X_NOT_FOUND;
    }
    if (!s_ready) {
        if (s_last_status == 0) {
            s_last_status = "not-ready";
        }
        return VL53L0X_RANGE_PENDING;
    }

    s_last_status = "range-start";
    if (!wr8(0x80U, 0x01U) || !wr8(0xFFU, 0x01U) || !wr8(0x00U, 0x00U) ||
        !wr8(0x91U, s_stop_variable) ||
        !wr8(0x00U, 0x01U) || !wr8(0xFFU, 0x00U) || !wr8(0x80U, 0x00U) ||
        !wr8(REG_SYSRANGE_START, 0x01U)) {
        s_ready = 0U;
        s_last_status = "range-start-fail";
        return VL53L0X_RANGE_PENDING;
    }

    if (!wait_reg_bits(REG_SYSRANGE_START, 0x01U, 0x00U, 100U)) {
        s_last_status = "range-start-timeout";
        return VL53L0X_RANGE_PENDING;
    }

    if (!wait_reg_nonzero(REG_RESULT_INTERRUPT_STATUS, 0x07U, 500U)) {
        s_last_status = "range-interrupt-timeout";
        return VL53L0X_RANGE_PENDING;
    }

    if (!rd8(REG_RESULT_RANGE_STATUS, &status) ||
        !rd16((uint8_t)(REG_RESULT_RANGE_STATUS + 10U), &range) ||
        !wr8(REG_SYSTEM_INTERRUPT_CLEAR, 0x01U)) {
        s_ready = 0U;
        s_last_status = "range-read-fail";
        return VL53L0X_RANGE_PENDING;
    }

    if ((status & 0x01U) == 0U || range == 0U || range == 0xFFFFU) {
        s_last_status = "invalid-range";
        return VL53L0X_RANGE_PENDING;
    }

    if (distance_mm != 0) {
        *distance_mm = range;
    }
    s_last_status = "range-ok";
    return VL53L0X_PRESENT;
}
