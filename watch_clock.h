#ifndef __WATCH_CLOCK_H
#define __WATCH_CLOCK_H

#include "AI8051U.H"
#include "DEF.H"

/* Alarm clock / ring / up timer / countdown / calendar module.
 * Storage contract: stage ints are `int16_t edata` so they can be bound
 * to the OLED_UI edit windows; the module never touches the app's
 * WatchMode (watch_face owns it), Tick() return values + the ring
 * flag are the only side channels. */

/* called from Timer11 ISR hook context (watch_face integration), every 20ms */
void Watch_ClockTick20(void);

/* ---- Alarm clock ---- */
void Watch_Alarm_Open(void);            /* parks internal trackers from live pins */
unsigned char Watch_Alarm_Tick(void);   /* 20ms tick; returns 1 = user requests exit */
void Watch_Alarm_Render(void);          /* full-screen "ALARM" status page */
void Watch_Alarm_Toggle(void);          /* menu callback: flips enabled flag */
unsigned char Watch_Alarm_Enabled(void);
/* stage ints for framework edit windows (bound later by integration) */
extern int16_t edata Watch_StageAlarmHour;    /* 0..23 */
extern int16_t edata Watch_StageAlarmMinute;  /* 0..59 */

/* ---- Ring (shared by Alarm + Countdown finish) ---- */
/* raises the ring request; watch_face polls Watch_Ring_Active() from main loop
 * and switches to its ring mode; MUST be ISR-context safe (flag set only) */
void Watch_Ring_Fire(const char code *text);
unsigned char Watch_Ring_Active(void);
void Watch_Ring_Stop(void);             /* clears flag, silences P5.0 */
void Watch_Ring_Render(void);           /* flashing full-screen banner + beep bursts */

/* ---- Up-running timer (count-up "timer" page, separate from stopwatch) ---- */
void Watch_Uptimer_Open(void);
unsigned char Watch_Uptimer_Tick(void); /* 1 = exit request */
void Watch_Uptimer_Render(void);

/* ---- Countdown ---- */
void Watch_Countdown_Open(void);        /* parks trackers */
unsigned char Watch_Countdown_Tick(void);/* 1 = exit request */
void Watch_Countdown_Render(void);
extern int16_t edata Watch_StageCountMin;     /* 0..99 */
extern int16_t edata Watch_StageCountSec;     /* 0..59 */

/* ---- Calendar ---- */
void Watch_Calendar_Open(void);
unsigned char Watch_Calendar_Tick(void);/* 1 = exit request */
void Watch_Calendar_Render(void);

#endif
