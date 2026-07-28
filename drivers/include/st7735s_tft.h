#ifndef ST7735S_TFT_H
#define ST7735S_TFT_H

#include <stdint.h>

#define ST7735S_TFT_WIDTH   160U
#define ST7735S_TFT_HEIGHT  80U

#define ST7735S_TFT_BLACK   0x0000U
#define ST7735S_TFT_WHITE   0xFFFFU
#define ST7735S_TFT_RED     0xF800U
#define ST7735S_TFT_GREEN   0x07E0U
#define ST7735S_TFT_BLUE    0x001FU
#define ST7735S_TFT_YELLOW  0xFFE0U
#define ST7735S_TFT_CYAN    0x07FFU

void ST7735S_TFT_Init(void);
void ST7735S_TFT_BusRelease(void);
void ST7735S_TFT_ClearMemory(uint16_t color);
void ST7735S_TFT_FillScreen(uint16_t color);
void ST7735S_TFT_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void ST7735S_TFT_DrawString(uint16_t x, uint16_t y, const char *text, uint16_t color, uint16_t bg);
void ST7735S_TFT_DrawStringScale(uint16_t x, uint16_t y, const char *text,
                                 uint16_t color, uint16_t bg, uint8_t scale);

#endif
