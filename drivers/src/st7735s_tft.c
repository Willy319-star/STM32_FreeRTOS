#include "st7735s_tft.h"

#include "FreeRTOS.h"
#include "board.h"
#include "task.h"

#define TFT_CLK_PORT   GPIOA
#define TFT_CLK_PIN    GPIO_PIN_5
#define TFT_MOSI_PORT  GPIOA
#define TFT_MOSI_PIN   GPIO_PIN_7
#define TFT_RES_PORT   GPIOC
#define TFT_RES_PIN    GPIO_PIN_4
#define TFT_DC_PORT    GPIOC
#define TFT_DC_PIN     GPIO_PIN_5
#define TFT_CS1_PORT   GPIOC
#define TFT_CS1_PIN    GPIO_PIN_6

#define CMD_SWRESET    0x01U
#define CMD_SLPOUT     0x11U
#define CMD_NORON      0x13U
#define CMD_INVOFF     0x20U
#define CMD_DISPON     0x29U
#define CMD_CASET      0x2AU
#define CMD_RASET      0x2BU
#define CMD_RAMWR      0x2CU
#define CMD_MADCTL     0x36U
#define CMD_COLMOD     0x3AU
#define CMD_FRMCTR1    0xB1U
#define CMD_FRMCTR2    0xB2U
#define CMD_FRMCTR3    0xB3U
#define CMD_INVCTR     0xB4U
#define CMD_PWCTR1     0xC0U
#define CMD_PWCTR2     0xC1U
#define CMD_PWCTR3     0xC2U
#define CMD_PWCTR4     0xC3U
#define CMD_PWCTR5     0xC4U
#define CMD_VMCTR1     0xC5U
#define CMD_GMCTRP1    0xE0U
#define CMD_GMCTRN1    0xE1U

/*
 * The 0.96 inch 80x160 ST7735S modules used here expose a visible panel
 * window inside a larger controller memory area. In landscape mode the
 * first logical rows can otherwise be written outside the visible area.
 */
#define TFT_VISIBLE_X_OFFSET  0U
#define TFT_VISIBLE_Y_OFFSET  24U

static void delay_ms(uint32_t ms)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) {
        HAL_Delay(ms);
    } else {
        vTaskDelay(pdMS_TO_TICKS(ms));
    }
}

static void pin_high(GPIO_TypeDef *port, uint16_t pin)
{
    port->BSRR = pin;
}

static void pin_low(GPIO_TypeDef *port, uint16_t pin)
{
    port->BSRR = (uint32_t)pin << 16U;
}

static void spi_delay(void)
{
    for (volatile uint32_t i = 0U; i < 80U; i++) {
        __NOP();
    }
}

static void write_byte(uint8_t byte)
{
    for (uint8_t i = 0U; i < 8U; i++) {
        pin_low(TFT_CLK_PORT, TFT_CLK_PIN);
        if ((byte & 0x80U) != 0U) {
            pin_high(TFT_MOSI_PORT, TFT_MOSI_PIN);
        } else {
            pin_low(TFT_MOSI_PORT, TFT_MOSI_PIN);
        }
        spi_delay();
        pin_high(TFT_CLK_PORT, TFT_CLK_PIN);
        spi_delay();
        byte <<= 1U;
    }
    pin_low(TFT_CLK_PORT, TFT_CLK_PIN);
}

static void write_command(uint8_t command)
{
    pin_low(TFT_CS1_PORT, TFT_CS1_PIN);
    pin_low(TFT_DC_PORT, TFT_DC_PIN);
    write_byte(command);
    pin_high(TFT_CS1_PORT, TFT_CS1_PIN);
}

static void write_command_data(uint8_t command, const uint8_t *data, uint8_t len)
{
    pin_low(TFT_CS1_PORT, TFT_CS1_PIN);
    pin_low(TFT_DC_PORT, TFT_DC_PIN);
    write_byte(command);
    pin_high(TFT_DC_PORT, TFT_DC_PIN);
    for (uint8_t i = 0U; i < len; i++) {
        write_byte(data[i]);
    }
    pin_high(TFT_CS1_PORT, TFT_CS1_PIN);
}

static void set_window_raw(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint8_t data[4];

    data[0] = (uint8_t)(x0 >> 8);
    data[1] = (uint8_t)x0;
    data[2] = (uint8_t)(x1 >> 8);
    data[3] = (uint8_t)x1;
    write_command_data(CMD_CASET, data, sizeof(data));

    data[0] = (uint8_t)(y0 >> 8);
    data[1] = (uint8_t)y0;
    data[2] = (uint8_t)(y1 >> 8);
    data[3] = (uint8_t)y1;
    write_command_data(CMD_RASET, data, sizeof(data));

    write_command(CMD_RAMWR);
}

static void set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    set_window_raw((uint16_t)(x0 + TFT_VISIBLE_X_OFFSET),
                   (uint16_t)(y0 + TFT_VISIBLE_Y_OFFSET),
                   (uint16_t)(x1 + TFT_VISIBLE_X_OFFSET),
                   (uint16_t)(y1 + TFT_VISIBLE_Y_OFFSET));
}

static void gpio_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    gpio.Pin = TFT_CLK_PIN | TFT_MOSI_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &gpio);

    gpio.Pin = TFT_RES_PIN | TFT_DC_PIN | TFT_CS1_PIN;
    HAL_GPIO_Init(GPIOC, &gpio);

    pin_low(TFT_CLK_PORT, TFT_CLK_PIN);
    pin_low(TFT_MOSI_PORT, TFT_MOSI_PIN);
    pin_high(TFT_RES_PORT, TFT_RES_PIN);
    pin_low(TFT_DC_PORT, TFT_DC_PIN);
    pin_high(TFT_CS1_PORT, TFT_CS1_PIN);
}

void ST7735S_TFT_BusRelease(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;

    gpio.Pin = TFT_CLK_PIN | TFT_MOSI_PIN;
    HAL_GPIO_Init(GPIOA, &gpio);

    gpio.Pin = TFT_RES_PIN | TFT_DC_PIN | TFT_CS1_PIN;
    HAL_GPIO_Init(GPIOC, &gpio);
}

static const uint8_t *font5x7(char c)
{
    static const uint8_t blank[5] = {0x00,0x00,0x00,0x00,0x00};
    static const uint8_t colon[5] = {0x00,0x36,0x36,0x00,0x00};
    static const uint8_t percent[5] = {0x63,0x13,0x08,0x64,0x63};
    static const uint8_t minus[5] = {0x08,0x08,0x08,0x08,0x08};
    static const uint8_t d0[5] = {0x3E,0x51,0x49,0x45,0x3E};
    static const uint8_t d1[5] = {0x00,0x42,0x7F,0x40,0x00};
    static const uint8_t d2[5] = {0x42,0x61,0x51,0x49,0x46};
    static const uint8_t d3[5] = {0x21,0x41,0x45,0x4B,0x31};
    static const uint8_t d4[5] = {0x18,0x14,0x12,0x7F,0x10};
    static const uint8_t d5[5] = {0x27,0x45,0x45,0x45,0x39};
    static const uint8_t d6[5] = {0x3C,0x4A,0x49,0x49,0x30};
    static const uint8_t d7[5] = {0x01,0x71,0x09,0x05,0x03};
    static const uint8_t d8[5] = {0x36,0x49,0x49,0x49,0x36};
    static const uint8_t d9[5] = {0x06,0x49,0x49,0x29,0x1E};
    static const uint8_t A[5] = {0x7E,0x11,0x11,0x11,0x7E};
    static const uint8_t B[5] = {0x7F,0x49,0x49,0x49,0x36};
    static const uint8_t C[5] = {0x3E,0x41,0x41,0x41,0x22};
    static const uint8_t D[5] = {0x7F,0x41,0x41,0x22,0x1C};
    static const uint8_t E[5] = {0x7F,0x49,0x49,0x49,0x41};
    static const uint8_t F[5] = {0x7F,0x09,0x09,0x09,0x01};
    static const uint8_t G[5] = {0x3E,0x41,0x49,0x49,0x7A};
    static const uint8_t H[5] = {0x7F,0x08,0x08,0x08,0x7F};
    static const uint8_t I[5] = {0x00,0x41,0x7F,0x41,0x00};
    static const uint8_t K[5] = {0x7F,0x08,0x14,0x22,0x41};
    static const uint8_t L[5] = {0x7F,0x40,0x40,0x40,0x40};
    static const uint8_t M[5] = {0x7F,0x02,0x0C,0x02,0x7F};
    static const uint8_t N[5] = {0x7F,0x04,0x08,0x10,0x7F};
    static const uint8_t O[5] = {0x3E,0x41,0x41,0x41,0x3E};
    static const uint8_t P[5] = {0x7F,0x09,0x09,0x09,0x06};
    static const uint8_t R[5] = {0x7F,0x09,0x19,0x29,0x46};
    static const uint8_t S[5] = {0x46,0x49,0x49,0x49,0x31};
    static const uint8_t T[5] = {0x01,0x01,0x7F,0x01,0x01};
    static const uint8_t U[5] = {0x3F,0x40,0x40,0x40,0x3F};
    static const uint8_t W[5] = {0x7F,0x20,0x18,0x20,0x7F};
    static const uint8_t X[5] = {0x63,0x14,0x08,0x14,0x63};
    static const uint8_t Y[5] = {0x07,0x08,0x70,0x08,0x07};
    static const uint8_t V[5] = {0x1F,0x20,0x40,0x20,0x1F};

    if (c >= 'a' && c <= 'z') {
        c = (char)(c - 'a' + 'A');
    }

    switch (c) {
    case '0': return d0; case '1': return d1; case '2': return d2; case '3': return d3; case '4': return d4;
    case '5': return d5; case '6': return d6; case '7': return d7; case '8': return d8; case '9': return d9;
    case 'A': return A; case 'B': return B; case 'C': return C; case 'D': return D; case 'E': return E;
    case 'F': return F; case 'G': return G; case 'H': return H; case 'I': return I; case 'K': return K;
    case 'L': return L; case 'M': return M; case 'N': return N; case 'O': return O; case 'P': return P;
    case 'R': return R; case 'S': return S; case 'T': return T; case 'U': return U; case 'W': return W;
    case 'V': return V;
    case 'X': return X; case 'Y': return Y; case ':': return colon; case '%': return percent; case '-': return minus;
    default: return blank;
    }
}

void ST7735S_TFT_Init(void)
{
    static const uint8_t frmctr[3] = {0x01U, 0x2CU, 0x2DU};
    static const uint8_t frmctr3[6] = {0x01U, 0x2CU, 0x2DU, 0x01U, 0x2CU, 0x2DU};
    static const uint8_t pwctr1[3] = {0xA2U, 0x02U, 0x84U};
    static const uint8_t pwctr3[2] = {0x0AU, 0x00U};
    static const uint8_t pwctr4[2] = {0x8AU, 0x2AU};
    static const uint8_t pwctr5[2] = {0x8AU, 0xEEU};
    static const uint8_t gamma_p[16] = {
        0x02U,0x1CU,0x07U,0x12U,0x37U,0x32U,0x29U,0x2DU,
        0x29U,0x25U,0x2BU,0x39U,0x00U,0x01U,0x03U,0x10U
    };
    static const uint8_t gamma_n[16] = {
        0x03U,0x1DU,0x07U,0x06U,0x2EU,0x2CU,0x29U,0x2DU,
        0x2EU,0x2EU,0x37U,0x3FU,0x00U,0x00U,0x02U,0x10U
    };
    const uint8_t invctr = 0x07U;
    const uint8_t pwctr2 = 0xC5U;
    const uint8_t vmctr1 = 0x0EU;
    const uint8_t colmod = 0x05U;
    const uint8_t madctl = 0x60U;

    gpio_init();
    delay_ms(300U);

    pin_low(TFT_RES_PORT, TFT_RES_PIN);
    delay_ms(100U);
    pin_high(TFT_RES_PORT, TFT_RES_PIN);
    delay_ms(300U);

    write_command(CMD_SWRESET);
    delay_ms(150U);
    write_command(CMD_SLPOUT);
    delay_ms(150U);
    write_command_data(CMD_FRMCTR1, frmctr, sizeof(frmctr));
    write_command_data(CMD_FRMCTR2, frmctr, sizeof(frmctr));
    write_command_data(CMD_FRMCTR3, frmctr3, sizeof(frmctr3));
    write_command_data(CMD_INVCTR, &invctr, 1U);
    write_command_data(CMD_PWCTR1, pwctr1, sizeof(pwctr1));
    write_command_data(CMD_PWCTR2, &pwctr2, 1U);
    write_command_data(CMD_PWCTR3, pwctr3, sizeof(pwctr3));
    write_command_data(CMD_PWCTR4, pwctr4, sizeof(pwctr4));
    write_command_data(CMD_PWCTR5, pwctr5, sizeof(pwctr5));
    write_command_data(CMD_VMCTR1, &vmctr1, 1U);
    write_command_data(CMD_COLMOD, &colmod, 1U);
    write_command_data(CMD_MADCTL, &madctl, 1U);
    write_command_data(CMD_GMCTRP1, gamma_p, sizeof(gamma_p));
    write_command_data(CMD_GMCTRN1, gamma_n, sizeof(gamma_n));
    write_command(CMD_NORON);
    delay_ms(20U);
    write_command(CMD_INVOFF);
    write_command(CMD_DISPON);
    delay_ms(150U);

    ST7735S_TFT_ClearMemory(ST7735S_TFT_BLACK);
}

void ST7735S_TFT_ClearMemory(uint16_t color)
{
    uint8_t hi = (uint8_t)(color >> 8);
    uint8_t lo = (uint8_t)color;

    /*
     * Some 0.96 inch ST7735S modules expose an 80x160 panel backed by a
     * larger controller address area. Clear a conservative 160x160 region
     * once during startup to remove pixels left from previous orientations.
     */
    set_window_raw(0U, 0U, 159U, 159U);
    pin_low(TFT_CS1_PORT, TFT_CS1_PIN);
    pin_high(TFT_DC_PORT, TFT_DC_PIN);
    for (uint32_t i = 0U; i < (160UL * 160UL); i++) {
        write_byte(hi);
        write_byte(lo);
    }
    pin_high(TFT_CS1_PORT, TFT_CS1_PIN);
}

void ST7735S_TFT_FillScreen(uint16_t color)
{
    ST7735S_TFT_FillRect(0U, 0U, ST7735S_TFT_WIDTH, ST7735S_TFT_HEIGHT, color);
}

void ST7735S_TFT_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    uint8_t hi = (uint8_t)(color >> 8);
    uint8_t lo = (uint8_t)color;

    if (x >= ST7735S_TFT_WIDTH || y >= ST7735S_TFT_HEIGHT || w == 0U || h == 0U) {
        return;
    }
    if ((x + w) > ST7735S_TFT_WIDTH) {
        w = (uint16_t)(ST7735S_TFT_WIDTH - x);
    }
    if ((y + h) > ST7735S_TFT_HEIGHT) {
        h = (uint16_t)(ST7735S_TFT_HEIGHT - y);
    }

    set_window(x, y, (uint16_t)(x + w - 1U), (uint16_t)(y + h - 1U));
    pin_low(TFT_CS1_PORT, TFT_CS1_PIN);
    pin_high(TFT_DC_PORT, TFT_DC_PIN);
    for (uint32_t i = 0U; i < ((uint32_t)w * (uint32_t)h); i++) {
        write_byte(hi);
        write_byte(lo);
    }
    pin_high(TFT_CS1_PORT, TFT_CS1_PIN);
}

void ST7735S_TFT_DrawString(uint16_t x, uint16_t y, const char *text, uint16_t color, uint16_t bg)
{
    ST7735S_TFT_DrawStringScale(x, y, text, color, bg, 1U);
}

void ST7735S_TFT_DrawStringScale(uint16_t x, uint16_t y, const char *text,
                                 uint16_t color, uint16_t bg, uint8_t scale)
{
    while (text != 0 && *text != '\0') {
        const uint8_t *glyph = font5x7(*text++);
        for (uint8_t col = 0U; col < 6U; col++) {
            uint8_t bits = (col < 5U) ? glyph[col] : 0U;
            for (uint8_t row = 0U; row < 8U; row++) {
                ST7735S_TFT_FillRect((uint16_t)(x + ((uint16_t)col * scale)),
                                     (uint16_t)(y + ((uint16_t)row * scale)),
                                     scale,
                                     scale,
                                     ((bits & (1U << row)) != 0U) ? color : bg);
            }
        }
        x = (uint16_t)(x + (6U * scale));
        if (x > (ST7735S_TFT_WIDTH - (6U * scale))) {
            break;
        }
    }
}
