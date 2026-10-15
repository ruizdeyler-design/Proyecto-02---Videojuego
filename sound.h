#ifndef SOUND_H
#define SOUND_H
#include "stm32f4xx_hal.h"
void SOUND_Init(TIM_HandleTypeDef *timer);
void SOUND_Update(void);
void SOUND_PlayMenu(void);
void SOUND_PlayPause(void);
void SOUND_PlayVictory(void);
void SOUND_PlayDefeat(void);
void SOUND_Stop(void);
#endif
