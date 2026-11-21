#ifndef __WATCH_FACE_H
#define __WATCH_FACE_H

#include "AI8051U.H"
#include "DEF.H"
#include "oled_ui.h"
#include "watch_time.h"

/* Top level app modes: the watch face is the boot mode and idle home,
 * the OLED_UI framework is the menu, plus a centisecond stopwatch and
 * the clock/game apps (owned by watch_clock/watch_dino/watch_plane).
 * APP_ALARM (alarm STATUS page) is kept only to preserve the enum
 * numbering: the page itself was removed and no mode ever enters it.
 * APP_RING is the full-screen ring banner shared by the alarm match
 * and countdown finish. */
typedef enum AppMode {
    APP_WATCH_FACE = 0,  /* clock face */
    APP_MENU = 1,        /* OLED_UI menu framework */
    APP_STOPWATCH = 2,   /* stopwatch */
    APP_CALENDAR = 3,    /* calendar (watch_clock.c) */
    APP_ALARM = 4,       /* unused: legacy alarm status page (value kept) */
    APP_RING = 5,        /* shared ring banner (watch_clock.c) */
    APP_UPTIMER = 6,     /* up-running timer (watch_clock.c) */
    APP_COUNTDN = 7,     /* countdown (watch_clock.c) */
    APP_DINO = 8,        /* dino runner game (watch_dino.c) */
    APP_PLANE = 9        /* plane war game (watch_plane.c) */
} AppMode;

/* Init RTC, install the framework hooks and park on the watch face.
 * Call once after the clocks/USB are up and before the main loop. */
void Watch_Init(void);

/* Every 20 ms: sample input, advance modes, drive transitions. */
void Watch_Tick(void);

/* Every frame: render the active mode (face / menu / stopwatch). */
void Watch_Render(void);

/* Menu callbacks (bound in oled_ui_menudata.c) */
void Watch_OpenFace(void);           /* "Watch Face" tile on the main menu */
void Watch_OpenStopwatch(void);      /* "Stopwatch" item in the More menu */
void Watch_TimeSet_Save(void);       /* "Save Time" item applies dirty staging */
void Watch_TimeSet_Touch(unsigned char field); /* call from an edit window Open:
                                      * 0=Year 1=Month 2=Day 3=Hour 4=Minute
                                      * 5=Second; refreshes that field from the
                                      * RTC and marks it dirty for Save */

/* New-app wrappers, also bound in oled_ui_menudata.c (More menu and
 * the Alarm / Count Down submenus). Each parks the module state, fades
 * the screen and switches WatchMode, with WatchSWReturnPage saved. */
void Watch_OpenCalendar(void);
void Watch_OpenUptimer(void);
void Watch_OpenCountdown(void);      /* enters the countdown app (paused, armed from staging) */
void Watch_OpenDino(void);
void Watch_OpenPlane(void);

/* Time Set staging values (Live data of the edit windows).
 * Year is kept as the full 2000..2099 value, the rest 0..59 / 0..23. */
extern int16_t edata Watch_StageYear;
extern int16_t edata Watch_StageMonth;
extern int16_t edata Watch_StageDay;
extern int16_t edata Watch_StageHour;
extern int16_t edata Watch_StageMinute;
extern int16_t edata Watch_StageSecond;

#endif
