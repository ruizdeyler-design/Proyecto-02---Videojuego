#include "ili9341.h"







/* =========================================================

   ESCRITURA DEL BUS DE 8 BITS

   ========================================================= */





/* Tabla GPIO BSRR: 256 bytes x 3 puertos x 4 bytes = 3072 bytes SRAM.
 * Evita ocho comprobaciones de bits por cada byte enviado a la TFT. */
static uint32_t busA[256],busB[256],busC[256];
static void ILI9341_PrepareBus(void)
{
    const uint32_t maskA=LCD_D0_Pin|LCD_D2_Pin|LCD_D7_Pin;
    const uint32_t maskB=LCD_D3_Pin|LCD_D4_Pin|LCD_D5_Pin|LCD_D6_Pin;
    const uint32_t maskC=LCD_D1_Pin;
    for(uint32_t d=0;d<256;d++) {
        uint32_t a=0,b=0,c=0;
        if(d&0x01u)a|=LCD_D0_Pin;
        if(d&0x04u)a|=LCD_D2_Pin;
        if(d&0x80u)a|=LCD_D7_Pin;
        if(d&0x08u)b|=LCD_D3_Pin;
        if(d&0x10u)b|=LCD_D4_Pin;
        if(d&0x20u)b|=LCD_D5_Pin;
        if(d&0x40u)b|=LCD_D6_Pin;
        if(d&0x02u)c|=LCD_D1_Pin;
        busA[d]=a|((maskA&~a)<<16);
        busB[d]=b|((maskB&~b)<<16);
        busC[d]=c|((maskC&~c)<<16);
    }
}
static inline void ILI9341_WriteBus(uint8_t data)
{
    GPIOA->BSRR=busA[data];
    GPIOB->BSRR=busB[data];
    GPIOC->BSRR=busC[data];
}

/* Pulso de escritura */

static inline void ILI9341_WriteStrobe(void)

{

    LCD_WR_GPIO_Port->BSRR = ((uint32_t)LCD_WR_Pin << 16);

    __NOP();

    LCD_WR_GPIO_Port->BSRR = LCD_WR_Pin;

}





/* =========================================================

   ESCRITURA DE COMANDOS Y DATOS

   ========================================================= */



static void ILI9341_WriteCommand(uint8_t command)

{

    /* RS/DC = 0 -> comando */

    HAL_GPIO_WritePin(LCD_RS_GPIO_Port,

                      LCD_RS_Pin,

                      GPIO_PIN_RESET);



    /* CS = 0 -> seleccionar pantalla */

    HAL_GPIO_WritePin(LCD_CS_GPIO_Port,

                      LCD_CS_Pin,

                      GPIO_PIN_RESET);



    ILI9341_WriteBus(command);

    ILI9341_WriteStrobe();



    HAL_GPIO_WritePin(LCD_CS_GPIO_Port,

                      LCD_CS_Pin,

                      GPIO_PIN_SET);

}





static void ILI9341_WriteData8(uint8_t data)

{

    /* RS/DC = 1 -> dato */

    HAL_GPIO_WritePin(LCD_RS_GPIO_Port,

                      LCD_RS_Pin,

                      GPIO_PIN_SET);



    HAL_GPIO_WritePin(LCD_CS_GPIO_Port,

                      LCD_CS_Pin,

                      GPIO_PIN_RESET);



    ILI9341_WriteBus(data);

    ILI9341_WriteStrobe();



    HAL_GPIO_WritePin(LCD_CS_GPIO_Port,

                      LCD_CS_Pin,

                      GPIO_PIN_SET);

}





static void ILI9341_WriteData16(uint16_t data)

{

    ILI9341_WriteData8(data >> 8);

    ILI9341_WriteData8(data & 0xFF);

}





/* =========================================================

   RESET

   ========================================================= */



static void ILI9341_Reset(void)

{

    HAL_GPIO_WritePin(LCD_RST_GPIO_Port,

                      LCD_RST_Pin,

                      GPIO_PIN_SET);



    HAL_Delay(5);



    HAL_GPIO_WritePin(LCD_RST_GPIO_Port,

                      LCD_RST_Pin,

                      GPIO_PIN_RESET);



    HAL_Delay(20);



    HAL_GPIO_WritePin(LCD_RST_GPIO_Port,

                      LCD_RST_Pin,

                      GPIO_PIN_SET);



    HAL_Delay(150);

}





/* =========================================================

   ROTACIÓN

   ========================================================= */



void ILI9341_SetRotation(uint8_t rotation)

{

    ILI9341_WriteCommand(0x36);



    switch (rotation)

    {

        case 0:

            ILI9341_WriteData8(0x48);

            break;



        case 1:

            ILI9341_WriteData8(0x28);

            break;



        case 2:

            ILI9341_WriteData8(0x88);

            break;



        case 3:

            ILI9341_WriteData8(0xE8);

            break;



        default:

            ILI9341_WriteData8(0x28);

            break;

    }

}





/* =========================================================

   INICIALIZACIÓN

   ========================================================= */



void ILI9341_Init(void)

{
    ILI9341_PrepareBus();

    /* No utilizaremos lectura por ahora */

    HAL_GPIO_WritePin(LCD_RD_GPIO_Port,

                      LCD_RD_Pin,

                      GPIO_PIN_SET);



    HAL_GPIO_WritePin(LCD_WR_GPIO_Port,

                      LCD_WR_Pin,

                      GPIO_PIN_SET);



    HAL_GPIO_WritePin(LCD_CS_GPIO_Port,

                      LCD_CS_Pin,

                      GPIO_PIN_SET);



    ILI9341_Reset();



    /* Software reset */

    ILI9341_WriteCommand(0x01);

    HAL_Delay(150);



    /* Power Control 1 */

    ILI9341_WriteCommand(0xC0);

    ILI9341_WriteData8(0x23);



    /* Power Control 2 */

    ILI9341_WriteCommand(0xC1);

    ILI9341_WriteData8(0x10);



    /* VCOM Control 1 */

    ILI9341_WriteCommand(0xC5);

    ILI9341_WriteData8(0x3E);

    ILI9341_WriteData8(0x28);



    /* VCOM Control 2 */

    ILI9341_WriteCommand(0xC7);

    ILI9341_WriteData8(0x86);



    /* Memory Access Control */

    ILI9341_SetRotation(1);



    /* Pixel Format = RGB565, 16 bits */

    ILI9341_WriteCommand(0x3A);

    ILI9341_WriteData8(0x55);



    /* Frame Rate */

    ILI9341_WriteCommand(0xB1);

    ILI9341_WriteData8(0x00);

    ILI9341_WriteData8(0x18);



    /* Display Function Control */

    ILI9341_WriteCommand(0xB6);

    ILI9341_WriteData8(0x08);

    ILI9341_WriteData8(0x82);

    ILI9341_WriteData8(0x27);



    /* Sleep Out */

    ILI9341_WriteCommand(0x11);

    HAL_Delay(120);



    /* Display ON */

    ILI9341_WriteCommand(0x29);

    HAL_Delay(20);

}





/* =========================================================

   VENTANA DE DIBUJO

   ========================================================= */



static void ILI9341_SetAddressWindow(uint16_t x0,

                                     uint16_t y0,

                                     uint16_t x1,

                                     uint16_t y1)

{

    /* Column Address Set */

    ILI9341_WriteCommand(0x2A);



    ILI9341_WriteData16(x0);

    ILI9341_WriteData16(x1);



    /* Page Address Set */

    ILI9341_WriteCommand(0x2B);



    ILI9341_WriteData16(y0);

    ILI9341_WriteData16(y1);



    /* Memory Write */

    ILI9341_WriteCommand(0x2C);

}





/* =========================================================

   PIXEL

   ========================================================= */



void ILI9341_DrawPixel(uint16_t x,

                       uint16_t y,

                       uint16_t color)

{

    if (x >= ILI9341_WIDTH ||

        y >= ILI9341_HEIGHT)

    {

        return;

    }



    ILI9341_SetAddressWindow(x, y, x, y);



    ILI9341_WriteData16(color);

}





/* =========================================================

   RECTÁNGULO

   ========================================================= */



void ILI9341_FillRectangle(uint16_t x,

                           uint16_t y,

                           uint16_t width,

                           uint16_t height,

                           uint16_t color)

{

    if (x >= ILI9341_WIDTH ||

        y >= ILI9341_HEIGHT ||

        width == 0 ||

        height == 0)

    {

        return;

    }



    if ((x + width) > ILI9341_WIDTH)

        width = ILI9341_WIDTH - x;



    if ((y + height) > ILI9341_HEIGHT)

        height = ILI9341_HEIGHT - y;



    ILI9341_SetAddressWindow(

        x,

        y,

        x + width - 1,

        y + height - 1

    );



    uint8_t high = color >> 8;

    uint8_t low  = color & 0xFF;



    uint32_t pixels = (uint32_t)width * height;



    /*

     * Aquí mantenemos RS y CS activos durante toda

     * la transferencia para ganar bastante velocidad.

     */



    HAL_GPIO_WritePin(LCD_RS_GPIO_Port,

                      LCD_RS_Pin,

                      GPIO_PIN_SET);



    HAL_GPIO_WritePin(LCD_CS_GPIO_Port,

                      LCD_CS_Pin,

                      GPIO_PIN_RESET);



    while (pixels--)

    {

        ILI9341_WriteBus(high);

        ILI9341_WriteStrobe();



        ILI9341_WriteBus(low);

        ILI9341_WriteStrobe();

    }



    HAL_GPIO_WritePin(LCD_CS_GPIO_Port,

                      LCD_CS_Pin,

                      GPIO_PIN_SET);

}





/* =========================================================

   PANTALLA COMPLETA

   ========================================================= */



void ILI9341_FillScreen(uint16_t color)

{

    ILI9341_FillRectangle(

        0,

        0,

        ILI9341_WIDTH,

        ILI9341_HEIGHT,

        color

    );

}

/* Optimized streaming of row-major RGB565 rectangles. */
void ILI9341_BeginRGB565(uint16_t x,uint16_t y,uint16_t w,uint16_t h)
{
    if(!w||!h||x>=ILI9341_WIDTH||y>=ILI9341_HEIGHT)return;
    ILI9341_SetAddressWindow(x,y,(uint16_t)(x+w-1),(uint16_t)(y+h-1));
    HAL_GPIO_WritePin(LCD_RS_GPIO_Port,LCD_RS_Pin,GPIO_PIN_SET);
    HAL_GPIO_WritePin(LCD_CS_GPIO_Port,LCD_CS_Pin,GPIO_PIN_RESET);
}
void ILI9341_WriteRGB565(const uint16_t *pixels,uint32_t count)
{
    while(count--) {
        uint16_t c=*pixels++;
        ILI9341_WriteBus((uint8_t)(c>>8));
        ILI9341_WriteStrobe();
        ILI9341_WriteBus((uint8_t)c);
        ILI9341_WriteStrobe();
    }
}
void ILI9341_EndRGB565(void)
{
    HAL_GPIO_WritePin(LCD_CS_GPIO_Port,LCD_CS_Pin,GPIO_PIN_SET);
}
