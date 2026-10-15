#ifndef GAME_H
#define GAME_H
#include <stdint.h>
#include "ili9341.h"
#define GAME_WIDTH 320
#define GAME_HEIGHT 240
#define BTN_IZQUIERDA 0x01u
#define BTN_DERECHA 0x02u
#define BTN_SALTO 0x04u
#define BTN_DISPARO 0x08u
#define BTN_POWERUP 0x10u
#define BTN_PAUSA 0x20u
#define BTN_CUADRADO 0x40u
void GAME_Init(void);
void GAME_MenuInit(void);
void GAME_SaveSystemReady(uint8_t mounted);
void GAME_SetControls(uint8_t estado);
void GAME_Update(void);
void GAME_DrawScene(void);
void GAME_DrawSamus(uint16_t x,uint16_t y);
#endif
