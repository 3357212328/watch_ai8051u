#ifndef __OLED_UI_DRIVER_H
#define __OLED_UI_DRIVER_H

#include "AI8051U.H"
#include "DEF.H"

/* Key status readers, active low: 0 = pressed, 1 = released */
unsigned char Key_GetEnterStatus(void);
unsigned char Key_GetBackStatus(void);
unsigned char Key_GetUpStatus(void);
unsigned char Key_GetDownStatus(void);

/* 20ms tick for UI timing */
void Timer_Init(void);

/* Active-low keys with pull-up */
void Key_Init(void);

/* Software quadrature encoder on P10(A) / P11(B) */
void Encoder_Init(void);
void Encoder_Enable(void);
void Encoder_Disable(void);
/* Sample pins at main-loop rate (debounced, accumulates quarter steps) */
void Encoder_FastPoll(void);
/* Called every 20ms tick, returns detents with /4 remainder kept */
short Encoder_Get(void);

/* Software delays calibrated for 40MHz 1T mode */
void Delay_us(unsigned long x);
void Delay_ms(unsigned long x);
void Delay_s(unsigned long x);

#endif
