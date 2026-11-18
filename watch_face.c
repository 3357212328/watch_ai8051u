#include "watch_face.h"
#include "oled.h"
#include "oled_driver.h"
#include "oled_ui_driver.h"
#include "set_timer.h"
#include "usb_ctrl.h"
#include "watch_clock.h"
#include "watch_dino.h"
#include "watch_plane.h"

/* Current and pending app mode */
static AppMode xdata WatchMode = APP_WATCH_FACE;
static unsigned char xdata WatchPendingFace = 0;

/* Page the stopwatch was entered from, returned to on Back */
static MenuPage * xdata WatchSWReturnPage = NULL;

/* Stopwatch state, catches up from the timer ISR hook each tick */
static unsigned long xdata WatchStopwatchCS = 0;
static unsigned char xdata WatchStopwatchRunning = 0;
static unsigned char xdata WatchSWLastEnter = 1;
static unsigned char xdata WatchSWLastBack = 1;

/* Watch face previous key states (release-edge menu trigger) */
static unsigned char xdata WatchFaceLastEnter = 1;
static unsigned char xdata WatchFaceLastBack = 1;
static unsigned char xdata WatchFaceLastUp = 1;
static unsigned char xdata WatchFaceLastDown = 1;

/* Fires with the true 20 ms timer ISR, immune to main-loop blocking.
 * Counted and cleared by the stopwatch tick each cycle. */
static volatile unsigned long edata WatchTickFire = 0;

/* Time Set staging, bound to the edit windows in oled_ui_menudata.c */
int16_t edata Watch_StageYear = 2026;
int16_t edata Watch_StageMonth = 1;
int16_t edata Watch_StageDay = 1;
int16_t edata Watch_StageHour = 12;
int16_t edata Watch_StageMinute = 0;
int16_t edata Watch_StageSecond = 0;

/* Working area for the watch face renderer */
static WatchTime xdata WatchFaceTime;
static char xdata WatchFaceBuf[20];

/* Weekday names, Monday first */
static char code WatchWeekNames[7][4] = {
    "MON", "TUE", "WED", "THU", "FRI", "SAT", "SUN"
};

/*
 * @brief Weekday via Zeller's congruence, exact for 2000..2099.
 *        Returns 0..6 = Mon..Sun.
 */
static uint8_t Watch_Weekday(uint8_t y, uint8_t m, uint8_t d)
{
    uint16_t yy;
    unsigned int k, j, h, mm;

    yy = (uint16_t)(2000U + y);
    mm = m;
    if (mm < 3)
    {
        mm += 12;
        yy--;
    }
    k = yy % 100;
    j = yy / 100;
    h = (d + (13U * (mm + 1)) / 5 + k + k / 4 + j / 4 + 5 * j) % 7;
    /* h: 0=Sat 1=Sun 2=Mon ... -> shift to Mon=0 */
    return (uint8_t)((h + 5) % 7);
}

/*
 * @brief Fade from whatever menu shows to black, present, watch face.
 */
static void Watch_EnterFaceFromMenu(void)
{
    /* Park the release-edge tracker with the live key state: a key
     * held while the face opens cannot fire a phantom release edge. */
    WatchFaceLastEnter = (unsigned char)(Key_GetEnterStatus() == 0);
    WatchFaceLastBack  = (unsigned char)(Key_GetBackStatus()  == 0);
    WatchFaceLastUp    = (unsigned char)(Key_GetUpStatus()    == 0);
    WatchFaceLastDown  = (unsigned char)(Key_GetDownStatus()  == 0);
    OLED_UI_FadeoutAllArea();
    OLED_Update();
    WatchMode = APP_WATCH_FACE;
}

/*
 * @brief Fade out, then hand control to the OLED_UI menu tiles.
 */
static void Watch_BootMenu(void)
{
    /* The page must exist BEFORE the fade: FadeoutAllArea reads
     * CurrentMenuPage->..., which is NULL on the first menu entry. */
    WatchMode = APP_MENU;
    OLED_UI_OpenMenu(&MainMenuPage);
    OLED_UI_FadeoutAllArea();
    OLED_Update();
}

/*
 * @brief Fade out and go back to the page the current app came from.
 *        Shared by the stopwatch and every new watch app (calendar,
 *        alarm status, up timer, countdown, dino, plane): the Open()
 *        wrapper for each app sets WatchSWReturnPage.
 */
static void Watch_ExitNewApp(void)
{
    OLED_UI_FadeoutAllArea();
    OLED_Update();
    WatchMode = APP_MENU;
    OLED_UI_OpenMenu(WatchSWReturnPage != NULL ? WatchSWReturnPage : &MainMenuPage);
}

/*
 * @brief "Watch Face" menu tile callback: leave the menu for the face.
 */
void Watch_OpenFace(void)
{
    Watch_EnterFaceFromMenu();
}

/*
 * @brief "Stopwatch" item callback: start a fresh stopwatch.
 */
void Watch_OpenStopwatch(void)
{
    WatchSWReturnPage = CurrentMenuPage;
    WatchStopwatchCS = 0;
    WatchStopwatchRunning = 0;
    /* Park the release-edge trackers with the live key state so a key
     * held while the stopwatch opens cannot fire a phantom edge. */
    WatchSWLastEnter = (unsigned char)(Key_GetEnterStatus() == 0);
    WatchSWLastBack  = (unsigned char)(Key_GetBackStatus()  == 0);
    OLED_UI_FadeoutAllArea();
    OLED_Update();
    WatchMode = APP_STOPWATCH;
}

/*
 * @brief Apply the Time Set staging values to the RTC. Stage values
 *        are bounded by their windows' Min/Max; WatchTime_Set clamps
 *        month/day interactions (leap years, short months). Saving
 *        restarts the sub-second fraction at zero.
 */
void Watch_TimeSet_Save(void)
{
    WatchTime t;

    t.SubSec = 0;   /* saving restarts the sub-second fraction */
    t.Year   = (uint8_t)(Watch_StageYear - 2000);
    t.Month  = (uint8_t)Watch_StageMonth;
    t.Day    = (uint8_t)Watch_StageDay;
    t.Hour   = (uint8_t)Watch_StageHour;
    t.Minute = (uint8_t)Watch_StageMinute;
    t.Second = (uint8_t)Watch_StageSecond;
    WatchTime_Set(&t);
}

/*
 * @brief Idle timeout hook: park on the watch face again.
 */
static void Watch_MenuIdleHook(void)
{
    WatchPendingFace = 1;
}

/*
 * @brief Back on a parentless page (main menu) returns to the face.
 */
static void Watch_MenuBackHook(void)
{
    WatchPendingFace = 1;
}

/*
 * @brief Wants the menu? Physical keys trigger on the RELEASE edge
 *        (same semantics as the framework, so a key held while the
 *        menu opens does not fire a spurious Enter/Back there).
 *        Encoder detents and USB virtual pulses trigger instantly.
 *        Consumes every virtual pulse so none leaks into the menu.
 */
static unsigned char Watch_AnyInputEvent(void)
{
    unsigned char any = 0;
    unsigned char enter, back, up, down;

    enter = (unsigned char)(Key_GetEnterStatus() == 0);
    back = (unsigned char)(Key_GetBackStatus() == 0);
    up = (unsigned char)(Key_GetUpStatus() == 0);
    down = (unsigned char)(Key_GetDownStatus() == 0);

    if (enter == 0 && WatchFaceLastEnter != 0)  { any = 1; }
    if (back == 0 && WatchFaceLastBack != 0)    { any = 1; }
    if (up == 0 && WatchFaceLastUp != 0)        { any = 1; }
    if (down == 0 && WatchFaceLastDown != 0)    { any = 1; }
    WatchFaceLastEnter = enter;
    WatchFaceLastBack = back;
    WatchFaceLastUp = up;
    WatchFaceLastDown = down;

    if (Encoder_Get() != 0)         { any = 1; }
    if (USBCtrl_TakeEnter() != 0)   { any = 1; }
    if (USBCtrl_TakeBack() != 0)    { any = 1; }
    if (USBCtrl_TakeUp() != 0)      { any = 1; }
    if (USBCtrl_TakeDown() != 0)    { any = 1; }
    if (USBCtrl_TakeEncoder() != 0) { any = 1; }
    return any;
}

/*
 * @brief 20 ms tick of the stopwatch: key handling + timing. Uses the
 *        same release-edge semantics as the face and the framework.
 *        All USB virtual pulses are consumed up front so none leaks
 *        even when a Back exits early.
 */
static void Watch_StopwatchTick(void)
{
    unsigned char usb_e, usb_b, usb_ud;
    unsigned char pressEnter, pressBack;
    unsigned long n;
    short enc;

    usb_e = USBCtrl_TakeEnter();
    usb_b = USBCtrl_TakeBack();
    usb_ud = (unsigned char)(USBCtrl_TakeUp() != 0 || USBCtrl_TakeDown() != 0);
    enc = USBCtrl_TakeEncoder();

    if (usb_e != 0)
    {
        WatchStopwatchRunning = !WatchStopwatchRunning;
    }
    if (usb_b != 0)
    {
        Watch_ExitNewApp();
        return;              /* other takes already drained above */
    }
    if (usb_ud != 0 && !WatchStopwatchRunning)
    {
        WatchStopwatchCS = 0;
    }

    /* 1 = pressed, 0 = released; edge fires when it just released */
    pressEnter = (unsigned char)(Key_GetEnterStatus() == 0);
    pressBack = (unsigned char)(Key_GetBackStatus() == 0);
    if (pressEnter == 0 && WatchSWLastEnter != 0)
    {
        WatchStopwatchRunning = !WatchStopwatchRunning;
    }
    if (pressBack == 0 && WatchSWLastBack != 0)
    {
        Watch_ExitNewApp();
        return;              /* re-parked on the next menu entry */
    }
    WatchSWLastEnter = pressEnter;
    WatchSWLastBack = pressBack;

    if (enc != 0 && !WatchStopwatchRunning)
    {
        WatchStopwatchCS = 0;
    }
    if (Encoder_Get() != 0 && !WatchStopwatchRunning)
    {
        WatchStopwatchCS = 0;
    }

    /* n counts the true 20 ms fires since the last tick (ISR hook),
     * immune to main-loop blocking; a worst-case fade block of
     * ~200 ms = 10 ticks, no overflow risk with unsigned long. It is
     * drained every tick (also while paused) so a long pause cannot
     * replay as a burst of time when the stopwatch is resumed. */
    EA = 0;
    n = WatchTickFire;
    WatchTickFire = 0;
    EA = 1;
    if (WatchStopwatchRunning)
    {
        WatchStopwatchCS += n * 2UL;   /* one 20 ms fire = 2 centiseconds */
    }
}

static void Watch_RingTick(void);

/*
 * @brief 20 ms tick of the shared ring banner: drain every virtual
 *        source once, sample the four keys, then ANY input event
 *        (release edge, USB pulse or encoder detent) stops the ring
 *        and returns to the watch face. There is no exit to the menu
 *        from the ring; Watch_Ring_Stop clears the flag before the
 *        mode switch so the banner cannot double-fire.
 */
static void Watch_RingTick(void)
{
    unsigned char any = 0;
    unsigned char enter, back, up, down;

    /* Drain all six virtual sources so no pulse leaks on transit. */
    if (USBCtrl_TakeEnter() != 0)   { any = 1; }
    if (USBCtrl_TakeBack() != 0)    { any = 1; }
    if (USBCtrl_TakeUp() != 0)      { any = 1; }
    if (USBCtrl_TakeDown() != 0)    { any = 1; }
    if (USBCtrl_TakeEncoder() != 0) { any = 1; }
    if (Encoder_Get() != 0)         { any = 1; }

    /* Sample the keys and park the trackers live: a key held while
     * the ring opens cannot fire a phantom release edge here. */
    enter = (unsigned char)(Key_GetEnterStatus() == 0);
    back = (unsigned char)(Key_GetBackStatus() == 0);
    up = (unsigned char)(Key_GetUpStatus() == 0);
    down = (unsigned char)(Key_GetDownStatus() == 0);
    if (enter == 0 && WatchFaceLastEnter != 0) { any = 1; }
    if (back == 0 && WatchFaceLastBack != 0)   { any = 1; }
    if (up == 0 && WatchFaceLastUp != 0)       { any = 1; }
    if (down == 0 && WatchFaceLastDown != 0)   { any = 1; }
    WatchFaceLastEnter = enter;
    WatchFaceLastBack = back;
    WatchFaceLastUp = up;
    WatchFaceLastDown = down;

    if (any != 0)
    {
        /* Beep off and clear the flag BEFORE switching mode, so the
         * banner cannot re-trigger on the next tick. */
        Watch_Ring_Stop();
        /* Face entry parks the face trackers live, fades, updates. */
        Watch_EnterFaceFromMenu();
    }
}

/*
 * @brief "Calendar" item callback: start a fresh calendar page.
 */
void Watch_OpenCalendar(void)
{
    WatchSWReturnPage = CurrentMenuPage;
    Watch_Calendar_Open();        /* parks the module trackers */
    OLED_UI_FadeoutAllArea();
    OLED_Update();
    WatchMode = APP_CALENDAR;
}

/*
 * @brief "Alarm" item callback: open the alarm status page (this is
 *        NOT the ring, which owns APP_RING and the "ALARM!" banner).
 */
void Watch_OpenAlarm(void)
{
    WatchSWReturnPage = CurrentMenuPage;
    Watch_Alarm_Open();           /* parks the module trackers */
    OLED_UI_FadeoutAllArea();
    OLED_Update();
    WatchMode = APP_ALARM;
}

/*
 * @brief "Up Timer" item callback: start a fresh up-running timer.
 */
void Watch_OpenUptimer(void)
{
    WatchSWReturnPage = CurrentMenuPage;
    Watch_Uptimer_Open();         /* parks the module trackers */
    OLED_UI_FadeoutAllArea();
    OLED_Update();
    WatchMode = APP_UPTIMER;
}

/*
 * @brief "Count Down" Start item callback: enter the countdown app.
 *        Watch_Countdown_Open adopts the staged Minute/Second values
 *        and opens PAUSED, so nothing runs until Enter is pressed.
 */
void Watch_OpenCountdown(void)
{
    WatchSWReturnPage = CurrentMenuPage;
    Watch_Countdown_Open();       /* parks trackers, arms from staging */
    OLED_UI_FadeoutAllArea();
    OLED_Update();
    WatchMode = APP_COUNTDN;
}

/*
 * @brief "Dino Game" item callback: start a fresh dino run.
 */
void Watch_OpenDino(void)
{
    WatchSWReturnPage = CurrentMenuPage;
    Watch_Dino_Open();            /* parks the module trackers */
    OLED_UI_FadeoutAllArea();
    OLED_Update();
    WatchMode = APP_DINO;
}

/*
 * @brief "Plane War" item callback: start a fresh plane game run.
 */
void Watch_OpenPlane(void)
{
    WatchSWReturnPage = CurrentMenuPage;
    Watch_Plane_Open();           /* parks the module trackers */
    OLED_UI_FadeoutAllArea();
    OLED_Update();
    WatchMode = APP_PLANE;
}

/*
 * @brief 20 ms tick dispatcher: input sampling and mode transitions.
 */
void Watch_Tick(void)
{
    /* Ring hijack: the alarm match runs in the 20 ms ISR regardless of
     * mode, so it can fire while a dino/plane game is open (the
     * countdown finish check runs in Countdown_Tick, main context,
     * whose tick is paused mid-game, so it cannot fire there). The
     * games deliberately do NOT consume the flag mid-play: it stays
     * pending (never cleared) and the banner shows the moment a
     * non-game mode resumes. */
    if (WatchMode != APP_DINO && WatchMode != APP_PLANE &&
        Watch_Ring_Active() != 0)
    {
        if (WatchMode == APP_MENU)
        {
            /* Kill the framework's live window state before leaving the
             * menu mid-frame: stop the sustain timer, drop any pending
             * window delta and park both mutex flags at FLAGEND. */
            OLED_SustainCounter.SustainFlag = 0;
            OLED_SustainCounter.count = 0;
            WindowProbDeltaData = 0;
            KeyEnterFlag = FLAGEND;
            FadeOutFlag = FLAGEND;
            /* Drop the live window reference too, or the stale popup
             * keeps drawing its leave animation over the reopened
             * menu and the next CreateWindow skips its reset path. */
            CurrentWindow = NULL;
        }
        /* The ring page samples the keys itself: park the face-facing
         * release-edge trackers with the live pin state here, so a key
         * held into the hijack cannot fire a phantom edge there. */
        WatchFaceLastEnter = (unsigned char)(Key_GetEnterStatus() == 0);
        WatchFaceLastBack  = (unsigned char)(Key_GetBackStatus()  == 0);
        WatchFaceLastUp    = (unsigned char)(Key_GetUpStatus()    == 0);
        WatchFaceLastDown  = (unsigned char)(Key_GetDownStatus()  == 0);
        WatchMode = APP_RING;
    }

    switch (WatchMode)
    {
        case APP_WATCH_FACE:
            if (Watch_AnyInputEvent() != 0)
            {
                Watch_BootMenu();
            }
            break;

        case APP_MENU:
            OLED_UI_InterruptHandler();
            if (WatchPendingFace != 0)
            {
                WatchPendingFace = 0;
                Watch_EnterFaceFromMenu();
            }
            break;

        case APP_STOPWATCH:
            Watch_StopwatchTick();
            break;

        case APP_CALENDAR:
            if (Watch_Calendar_Tick() != 0) { Watch_ExitNewApp(); }
            break;

        case APP_ALARM:
            if (Watch_Alarm_Tick() != 0) { Watch_ExitNewApp(); }
            break;

        case APP_UPTIMER:
            if (Watch_Uptimer_Tick() != 0) { Watch_ExitNewApp(); }
            break;

        case APP_COUNTDN:
            if (Watch_Countdown_Tick() != 0) { Watch_ExitNewApp(); }
            break;

        case APP_DINO:
            if (Watch_Dino_Tick() != 0) { Watch_ExitNewApp(); }
            break;

        case APP_PLANE:
            if (Watch_Plane_Tick() != 0) { Watch_ExitNewApp(); }
            break;

        case APP_RING:
            Watch_RingTick();
            break;

        default:
            break;
    }
}

/*
 * @brief One public renderer: called by main() every loop, dispatches
 *        to the face or to the stopwatch layout. In menu mode the
 *        OLED_UI framework owns the screen, so we drive its frame
 *        loop here instead; the new apps render through their module
 *        Render() entry points.
 */
static void Watch_RenderFace(void);
static void Watch_RenderStopwatch(void);

void Watch_Render(void)
{
    if (WatchMode == APP_STOPWATCH)
    {
        Watch_RenderStopwatch();
    }
    else if (WatchMode == APP_WATCH_FACE)
    {
        Watch_RenderFace();
    }
    else if (WatchMode == APP_CALENDAR)
    {
        Watch_Calendar_Render();
    }
    else if (WatchMode == APP_ALARM)
    {
        Watch_Alarm_Render();
    }
    else if (WatchMode == APP_RING)
    {
        Watch_Ring_Render();
    }
    else if (WatchMode == APP_UPTIMER)
    {
        Watch_Uptimer_Render();
    }
    else if (WatchMode == APP_COUNTDN)
    {
        Watch_Countdown_Render();
    }
    else if (WatchMode == APP_DINO)
    {
        Watch_Dino_Render();
    }
    else if (WatchMode == APP_PLANE)
    {
        Watch_Plane_Render();
    }
    else
    {
        OLED_UI_MainLoop();  /* menu mode: framework owns the screen */
    }
}

/*
 * @brief Watch face: weekday+date, big time with blinking colon,
 *        second progress bar, weekday. Uses RTCSSEC (1/128 s) so the
 *        colon and the bar move sub-second smooth without any ISR.
 */
static void Watch_RenderFace(void)
{
    uint16_t bar;
    unsigned char w, i, showColon;

    OLED_SetColorMode(ColorMode);
    OLED_Brightness(OLED_UI_Brightness);
    OLED_Clear();
    WatchTime_Read(&WatchFaceTime);

    /* Top row: 2026-10-21 (6x8), centered */
    WatchFaceBuf[0] = '2';
    WatchFaceBuf[1] = '0';
    WatchFaceBuf[2] = (char)('0' + WatchFaceTime.Year / 10);
    WatchFaceBuf[3] = (char)('0' + WatchFaceTime.Year % 10);
    WatchFaceBuf[4] = '-';
    WatchFaceBuf[5] = (char)('0' + WatchFaceTime.Month / 10);
    WatchFaceBuf[6] = (char)('0' + WatchFaceTime.Month % 10);
    WatchFaceBuf[7] = '-';
    WatchFaceBuf[8] = (char)('0' + WatchFaceTime.Day / 10);
    WatchFaceBuf[9] = (char)('0' + WatchFaceTime.Day % 10);
    WatchFaceBuf[10] = '\0';
    OLED_ShowString(34, 3, WatchFaceBuf, OLED_6X8_HALF);

    /* Big time HH:MM:SS (10x20), colon blinks each half second */
    showColon = (WatchFaceTime.SubSec & 0x40) ? 1 : 0;
    WatchFaceBuf[0] = (char)('0' + WatchFaceTime.Hour / 10);
    WatchFaceBuf[1] = (char)('0' + WatchFaceTime.Hour % 10);
    WatchFaceBuf[2] = (char)(showColon ? ':' : ' ');
    WatchFaceBuf[3] = (char)('0' + WatchFaceTime.Minute / 10);
    WatchFaceBuf[4] = (char)('0' + WatchFaceTime.Minute % 10);
    WatchFaceBuf[5] = (char)(showColon ? ':' : ' ');
    WatchFaceBuf[6] = (char)('0' + WatchFaceTime.Second / 10);
    WatchFaceBuf[7] = (char)('0' + WatchFaceTime.Second % 10);
    WatchFaceBuf[8] = '\0';
    OLED_ShowString(24, 15, WatchFaceBuf, OLED_10X20_HALF);

    /* Second progress bar, inner reaches 117 px at :59.984 and 0 at
     * :00.000, no clamp or end snap needed (rounds to nearest px) */
    bar = (uint16_t)(((unsigned long)(WatchFaceTime.Second * 128U + WatchFaceTime.SubSec) * 117UL + 3839UL) / 7679UL);
    OLED_DrawRectangle(4, 42, 120, 8, 0);
    if (bar != 0)
    {
        OLED_DrawRectangle(6, 43, (int16_t)bar, 6, 1);
    }

    /* Bottom row: weekday (6x8), centered */
    w = Watch_Weekday(WatchFaceTime.Year, WatchFaceTime.Month, WatchFaceTime.Day);
    for (i = 0; i < 3; i++)
    {
        OLED_ShowChar(55 + i * 6, 53, WatchWeekNames[w][i], OLED_6X8_HALF);
    }
    OLED_Update();
}

/*
 * @brief Stopwatch screen: big centiseconds, state and key hints.
 */
static void Watch_RenderStopwatch(void)
{
    unsigned long cs;
    unsigned int minutes;
    unsigned int seconds;
    unsigned int cents;
    unsigned char i;

    OLED_SetColorMode(ColorMode);
    OLED_Brightness(OLED_UI_Brightness);
    OLED_Clear();

    /* Title */
    WatchFaceBuf[0] = 'S'; WatchFaceBuf[1] = 'T'; WatchFaceBuf[2] = 'O';
    WatchFaceBuf[3] = 'P'; WatchFaceBuf[4] = 'W'; WatchFaceBuf[5] = 'A';
    WatchFaceBuf[6] = 'T'; WatchFaceBuf[7] = 'C'; WatchFaceBuf[8] = 'H';
    WatchFaceBuf[9] = '\0';
    OLED_ShowString(37, 2, WatchFaceBuf, OLED_6X8_HALF);

    /* Big MM:SS.CC */
    cs = WatchStopwatchCS;
    minutes = (unsigned int)((cs / 6000UL) % 100UL);
    seconds = (unsigned int)((cs / 100UL) % 60UL);
    cents = (unsigned int)(cs % 100UL);
    WatchFaceBuf[0] = (char)('0' + minutes / 10);
    WatchFaceBuf[1] = (char)('0' + minutes % 10);
    WatchFaceBuf[2] = ':';
    WatchFaceBuf[3] = (char)('0' + seconds / 10);
    WatchFaceBuf[4] = (char)('0' + seconds % 10);
    WatchFaceBuf[5] = '.';
    WatchFaceBuf[6] = (char)('0' + cents / 10);
    WatchFaceBuf[7] = (char)('0' + cents % 10);
    WatchFaceBuf[8] = '\0';
    OLED_ShowString(24, 18, WatchFaceBuf, OLED_10X20_HALF);

    /* State row: static RUNNING / PAUSED status */
    i = 0;
    if (WatchStopwatchRunning)
    {
        WatchFaceBuf[i++] = 'R';
        WatchFaceBuf[i++] = 'U';
        WatchFaceBuf[i++] = 'N';
        WatchFaceBuf[i++] = 'N';
        WatchFaceBuf[i++] = 'I';
        WatchFaceBuf[i++] = 'N';
        WatchFaceBuf[i++] = 'G';
        for (i = 0; i < 7; i++)
        {
            OLED_ShowChar(43 + i * 6, 42, WatchFaceBuf[i], OLED_6X8_HALF);
        }
    }
    else
    {
        WatchFaceBuf[i++] = 'P';
        WatchFaceBuf[i++] = 'A';
        WatchFaceBuf[i++] = 'U';
        WatchFaceBuf[i++] = 'S';
        WatchFaceBuf[i++] = 'E';
        WatchFaceBuf[i++] = 'D';
        for (i = 0; i < 6; i++)
        {
            OLED_ShowChar(46 + i * 6, 42, WatchFaceBuf[i], OLED_6X8_HALF);
        }
    }

    /* Key hints */
    WatchFaceBuf[0] = 'E';
    WatchFaceBuf[1] = 'N';
    WatchFaceBuf[2] = 'T';
    WatchFaceBuf[3] = ':';
    WatchFaceBuf[4] = 'O';
    WatchFaceBuf[5] = 'N';
    WatchFaceBuf[6] = '/';
    WatchFaceBuf[7] = 'O';
    WatchFaceBuf[8] = 'F';
    WatchFaceBuf[9] = 'F';
    WatchFaceBuf[10] = ' ';
    WatchFaceBuf[11] = 'E';
    WatchFaceBuf[12] = 'N';
    WatchFaceBuf[13] = 'C';
    WatchFaceBuf[14] = ':';
    WatchFaceBuf[15] = 'C';
    WatchFaceBuf[16] = 'L';
    WatchFaceBuf[17] = 'R';
    WatchFaceBuf[18] = '\0';
    OLED_ShowString(10, 55, WatchFaceBuf, OLED_6X8_HALF);

    OLED_Update();
}

/*
 * @brief Timer11 20 ms hook, runs in the real interrupt context.
 *        Increments the true tick counter; the stopwatch consumes it
 *        so its time base survives main loops blocked long past one
 *        tick (OLED_Update ~30 ms, fades ~200 ms).
 */
static void Watch_TimerHook(void)
{
    WatchTickFire++;
    /* Feed the clock services (alarm match, ring/up-timer/countdown
     * time base) from the same true 20 ms ISR context. */
    Watch_ClockTick20();
}

/*
 * @brief Setup: RTC on hooks installed, mode = face. Call once.
 */
void Watch_Init(void)
{
    WatchTime_Init();

    WatchTime_Read(&WatchFaceTime);
    Watch_StageYear   = 2000 + WatchFaceTime.Year;
    Watch_StageMonth  = WatchFaceTime.Month;
    Watch_StageDay    = WatchFaceTime.Day;
    Watch_StageHour   = WatchFaceTime.Hour;
    Watch_StageMinute = WatchFaceTime.Minute;
    Watch_StageSecond = WatchFaceTime.Second;

    WatchStopwatchCS = 0;
    WatchStopwatchRunning = 0;
    WatchSWLastEnter = 1;
    WatchSWLastBack = 1;
    WatchPendingFace = 0;
    WatchMode = APP_WATCH_FACE;

    /* Park the face release-edge trackers with the live key state so
     * a key held across init cannot fire a phantom release edge. */
    WatchFaceLastEnter = (unsigned char)(Key_GetEnterStatus() == 0);
    WatchFaceLastBack  = (unsigned char)(Key_GetBackStatus()  == 0);
    WatchFaceLastUp    = (unsigned char)(Key_GetUpStatus()    == 0);
    WatchFaceLastDown  = (unsigned char)(Key_GetDownStatus()  == 0);

    /* Count the true 20 ms fires on the same timer the UI tick uses. */
    set_timer_isr(Timer11, Watch_TimerHook);

    OLED_UI_BackHook = Watch_MenuBackHook;
    OLED_UI_IdleHook = Watch_MenuIdleHook;
    OLED_UI_IdleTimeout = 1500;          /* 30 s of inactivity */
}
