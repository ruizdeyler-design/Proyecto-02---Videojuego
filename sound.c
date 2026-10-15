#include "sound.h"
#include <stddef.h>

/* TIM3: 84 MHz, PSC=83 -> contador 1 MHz. PC6 = TIM3_CH1.
   Modulo Low Level Trigger: salida ALTA = apagado. */
typedef struct { uint16_t hz; uint16_t ms; } Note;
static TIM_HandleTypeDef *buzzer = NULL;
static const Note *melody = NULL;
static uint8_t noteCount = 0, noteIndex = 0;
static uint32_t nextAt = 0;

/* Notas cortas, silencios hz=0, una sola reproduccion por evento. */
static const Note menuNotes[] = {
    {659,110},{0,40},{784,130},{0,50},{988,180},{0,60}
};
static const Note pauseNotes[] = {
    {740,95},{0,45},{587,115}
};
static const Note victoryNotes[] = {
    {523,140},{0,35},{659,140},{0,35},{784,140},{0,35},
    {1047,320},{0,75},{988,120},{1047,390}
};
static const Note defeatNotes[] = {
    {659,190},{0,60},{494,210},{0,60},{392,250},
    {0,70},{262,430}
};

static void frequency(uint16_t hz)
{
    if (buzzer == NULL) return;
    if (hz == 0) {
        __HAL_TIM_SET_COMPARE(buzzer, TIM_CHANNEL_1, 0);
        return;
    }
    uint32_t period = 1000000u / hz;
    if (period < 4u) period = 4u;
    if (period > 65536u) period = 65536u;
    __HAL_TIM_SET_COMPARE(buzzer, TIM_CHANNEL_1, 0);
    __HAL_TIM_SET_AUTORELOAD(buzzer, period - 1u);
    __HAL_TIM_SET_COUNTER(buzzer, 0u);
    __HAL_TIM_SET_COMPARE(buzzer, TIM_CHANNEL_1, period / 2u);
}
void SOUND_Init(TIM_HandleTypeDef *timer)
{
    buzzer = timer;
    melody = NULL;
    noteCount = noteIndex = 0;
    if (buzzer != NULL) {
        __HAL_TIM_SET_COMPARE(buzzer, TIM_CHANNEL_1, 0);
        HAL_TIM_PWM_Start(buzzer, TIM_CHANNEL_1);
    }
}
void SOUND_Stop(void)
{
    melody = NULL; noteCount = noteIndex = 0;
    frequency(0);
}
static void play(const Note *notes, uint8_t count)
{
    if (buzzer == NULL || count == 0u) return;
    SOUND_Stop();
    melody = notes;
    noteCount = count;
    noteIndex = 0;
    frequency(melody[0].hz);
    nextAt = HAL_GetTick() + melody[0].ms;
}
void SOUND_Update(void)
{
    if (melody == NULL) return;
    uint32_t now = HAL_GetTick();
    if ((int32_t)(now - nextAt) < 0) return;
    ++noteIndex;
    if (noteIndex >= noteCount) { SOUND_Stop(); return; }
    frequency(melody[noteIndex].hz);
    nextAt = now + melody[noteIndex].ms;
}
#define NOTES(a) ((uint8_t)(sizeof(a)/sizeof((a)[0])))
void SOUND_PlayMenu(void) { play(menuNotes, NOTES(menuNotes)); }
void SOUND_PlayPause(void) { play(pauseNotes, NOTES(pauseNotes)); }
void SOUND_PlayVictory(void) { play(victoryNotes, NOTES(victoryNotes)); }
void SOUND_PlayDefeat(void) { play(defeatNotes, NOTES(defeatNotes)); }
