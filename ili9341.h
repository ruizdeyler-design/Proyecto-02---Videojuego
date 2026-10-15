#ifndef INC_ILI9341_H_
#define INC_ILI9341_H_
#include "main.h"
#include <stdint.h>
#define ILI9341_WIDTH 320
#define ILI9341_HEIGHT 240
#define ILI9341_BLACK 0x0000u
#define ILI9341_WHITE 0xFFFFu
#define ILI9341_RED 0xF800u
#define ILI9341_GREEN 0x07E0u
#define ILI9341_BLUE 0x001Fu
#define ILI9341_YELLOW 0xFFE0u
#define ILI9341_CYAN 0x07FFu
#define ILI9341_MAGENTA 0xF81Fu
#define ILI9341_GRAY 0x8410u
void ILI9341_Init(void);
void ILI9341_SetRotation(uint8_t rotation);
void ILI9341_DrawPixel(uint16_t x,uint16_t y,uint16_t color);
void ILI9341_FillRectangle(uint16_t x,uint16_t y,uint16_t width,uint16_t height,uint16_t color);
void ILI9341_FillScreen(uint16_t color);

/* Escritura rapida RGB565: una ventana por region, no una por pixel.
 * Las llamadas Begin/Write/End siempre deben ir en ese orden. */
void ILI9341_BeginRGB565(uint16_t x, uint16_t y, uint16_t width, uint16_t height);
void ILI9341_WriteRGB565(const uint16_t *pixels, uint32_t count);
void ILI9341_EndRGB565(void);
#endif
