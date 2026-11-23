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

/* App that was running when the ring hijacked the display. Restored
 * when the banner is dismissed; menu/face fall back to the face. */
static unsigned char xdata WatchRingPrevMode = APP_WATCH_FACE;

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

/* Dirty bitmap of the Time Set edit windows, bit0..5 = Year..Second.
 * A bit is set by Watch_TimeSet_Touch() when the user actually opens
 * (and therefore edits) a field. Save only takes the staged value for
 * dirty fields; every other field is re-read from the RTC, so the boot
 * snapshot in Watch_Stage* can no longer be written back days later.
 * Not clearing it on init is fine: nothing is dirty at boot (zero). */
static unsigned char xdata WatchTimeSetDirty = 0;

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
 * @brief Mark one Time Set field as edited and refresh its staging
 *        value from the live RTC, so an edit window always opens on
 *        the current time instead of the boot-time snapshot.
 *        field: 0 = Year, 1 = Month, 2 = Day, 3 = Hour, 4 = Minute,
 *        5 = Second; anything else is ignored.
 */
void Watch_TimeSet_Touch(unsigned char field)
{
    WatchTime t;

    if (field > 5U)
    {
        return;
    }
    WatchTime_Read(&t);
    switch (field)
    {
        case 0: Watch_StageYear   = (int16_t)(2000 + t.Year); break;
        case 1: Watch_StageMonth  = t.Month;  break;
        case 2: Watch_StageDay    = t.Day;    break;
        case 3: Watch_StageHour   = t.Hour;   break;
        case 4: Watch_StageMinute = t.Minute; break;
        case 5: Watch_StageSecond = t.Second; break;
        default: break;
    }
    WatchTimeSetDirty |= (unsigned char)(1U << field);
}

/*
 * @brief Apply the Time Set staging values to the RTC - but only for
 *        the fields the user actually edited (WatchTimeSetDirty).
 *        Every other field keeps the value just read from the RTC, so
 *        Save can no longer rewind the clock to the boot-time snapshot.
 *        Pressing Save without opening any edit window therefore writes
 *        the current time back unchanged (harmless no-op).
 *        Staged values are bounded by their windows' Min/Max and
 *        WatchTime_Set clamps month/day interactions (leap years,
 *        short months). It also restarts the sub-second fraction at 0.
 */
void Watch_TimeSet_Save(void)
{
    WatchTime t;

    WatchTime_Read(&t);   /* base = current time, not the boot snapshot */

    if ((WatchTimeSetDirty & 0x01U) != 0)
    {
        t.Year = (uint8_t)(Watch_StageYear - 2000);
    }
    if ((WatchTimeSetDirty & 0x02U) != 0)
    {
        t.Month = (uint8_t)Watch_StageMonth;
    }
    if ((WatchTimeSetDirty & 0x04U) != 0)
    {
        t.Day = (uint8_t)Watch_StageDay;
    }
    if ((WatchTimeSetDirty & 0x08U) != 0)
    {
        t.Hour = (uint8_t)Watch_StageHour;
    }
    if ((WatchTimeSetDirty & 0x10U) != 0)
    {
        t.Minute = (uint8_t)Watch_StageMinute;
    }
    if ((WatchTimeSetDirty & 0x20U) != 0)
    {
        t.Second = (uint8_t)Watch_StageSecond;
    }

    WatchTime_Set(&t);
    WatchTimeSetDirty = 0;   /* next save starts from the live RTC again */
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
 * @brief Re-park the stopwatch release-edge trackers after a ring
 *        hijack. The accumulated centiseconds and the run/pause state
 *        are deliberately left untouched, so the user resumes exactly
 *        where the alarm interrupted.
 */
static void Watch_StopwatchPark(void)
{
    WatchSWLastEnter = (unsigned char)(Key_GetEnterStatus() == 0);
    WatchSWLastBack  = (unsigned char)(Key_GetBackStatus()  == 0);
}

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
        unsigned char prev;

        /* Beep off and clear the flag BEFORE switching mode, so the
         * banner cannot re-trigger on the next tick. */
        Watch_Ring_Stop();
        prev = WatchRingPrevMode;
        WatchRingPrevMode = APP_WATCH_FACE;

        /* Return to the app the ring hijacked. The Park() helpers only
         * drain queued input and stale 20 ms fires and re-park the edge
         * trackers - they never touch the accumulated progress, so the
         * user resumes where the alarm hit (games are never hijacked).
         * Menu / face keep the old "back to the face" behaviour; the
         * menu framework state is not restored. */
        if (prev == (unsigned char)APP_STOPWATCH)
        {
            Watch_StopwatchPark();
            WatchMode = APP_STOPWATCH;
        }
        else if (prev == (unsigned char)APP_UPTIMER)
        {
            Watch_Uptimer_Park();
            WatchMode = APP_UPTIMER;
        }
        else if (prev == (unsigned char)APP_COUNTDN)
        {
            Watch_Countdown_Park();
            WatchMode = APP_COUNTDN;
        }
        else if (prev == (unsigned char)APP_CALENDAR)
        {
            /* No progress to keep: reopening today's month is enough. */
            Watch_Calendar_Open();
            WatchMode = APP_CALENDAR;
        }
        else
        {
            /* Face entry parks the face trackers live, fades, updates. */
            Watch_EnterFaceFromMenu();
        }
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
     * non-game mode resumes. APP_RING must be excluded too: the flag
     * stays up for the whole banner, so re-entering this block every
     * tick would overwrite the remembered mode with APP_WATCH_FACE and
     * the hand-back would always land on the face. */
    if (WatchMode != APP_DINO && WatchMode != APP_PLANE &&
        WatchMode != APP_RING &&
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
        /* Remember the interrupted app so the ring can hand control
         * back to it instead of always landing on the face. Menu and
         * face collapse to APP_WATCH_FACE (no menu state to restore). */
        if (WatchMode == APP_STOPWATCH || WatchMode == APP_CALENDAR ||
            WatchMode == APP_UPTIMER || WatchMode == APP_COUNTDN)
        {
            WatchRingPrevMode = (unsigned char)WatchMode;
        }
        else
        {
            WatchRingPrevMode = (unsigned char)APP_WATCH_FACE;
        }
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

/* --------------------------------------------------------------------
 * Big 7-segment time
 * Drawn from filled rectangles instead of a bitmap font. One cell is
 * 20 x 32 px with 3 px strokes and 1 px gaps between the segments, so
 * four digits plus a colon fit in 100 px without touching flash.
 * Cell layout: top bar rows 0..2, upper stems 4..12, middle bar
 * 14..16, lower stems 18..27, bottom bar 29..31. The stems sit on the
 * 3 px side columns, the bars start at x + 4: that leaves a 1 px
 * corner gap, which reads as a real segmented display.
 * ------------------------------------------------------------------ */
static unsigned char code WatchSegCode[10] = {
    /* bit0 a top, bit1 b upper right, bit2 c lower right, bit3 d
     * bottom, bit4 e lower left, bit5 f upper left, bit6 g middle */
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
};

/*
 * @brief One big digit with its top-left corner at (x, y).
 */
static void Watch_DrawBigDigit(int16_t x, int16_t y, unsigned char digit)
{
    unsigned char seg;

    if (digit > 9U)
    {
        digit = 0U;              /* never index outside the table */
    }
    seg = WatchSegCode[digit];

    if ((seg & 0x01U) != 0)      /* a: top */
    {
        OLED_DrawRectangle((int16_t)(x + 4), y, 12, 3, OLED_FILLED);
    }
    if ((seg & 0x40U) != 0)      /* g: middle */
    {
        OLED_DrawRectangle((int16_t)(x + 4), (int16_t)(y + 14), 12, 3, OLED_FILLED);
    }
    if ((seg & 0x08U) != 0)      /* d: bottom */
    {
        OLED_DrawRectangle((int16_t)(x + 4), (int16_t)(y + 29), 12, 3, OLED_FILLED);
    }
    if ((seg & 0x02U) != 0)      /* b: upper right */
    {
        OLED_DrawRectangle((int16_t)(x + 17), (int16_t)(y + 4), 3, 9, OLED_FILLED);
    }
    if ((seg & 0x04U) != 0)      /* c: lower right */
    {
        OLED_DrawRectangle((int16_t)(x + 17), (int16_t)(y + 18), 3, 10, OLED_FILLED);
    }
    if ((seg & 0x20U) != 0)      /* f: upper left */
    {
        OLED_DrawRectangle(x, (int16_t)(y + 4), 3, 9, OLED_FILLED);
    }
    if ((seg & 0x10U) != 0)      /* e: lower left */
    {
        OLED_DrawRectangle(x, (int16_t)(y + 18), 3, 10, OLED_FILLED);
    }
}

/*
 * @brief A 100 x 32 px "AA:BB" group at (x, y). Digits inside a pair
 *        sit 24 px apart (20 + 4 gap), the colon gets an 8 px slot
 *        centred on the cell height. colonOn = 0 blanks the two 4 x 4
 *        dots for the half-second blink.
 *        Span = x .. x + 99, so x = 14 gives 14 px of margin a side.
 */
static void Watch_DrawBigPair(int16_t x, int16_t y, unsigned char a,
                              unsigned char b, unsigned char colonOn)
{
    Watch_DrawBigDigit(x, y, (unsigned char)(a / 10U));
    Watch_DrawBigDigit((int16_t)(x + 24), y, (unsigned char)(a % 10U));

    if (colonOn != 0)
    {
        OLED_DrawRectangle((int16_t)(x + 48), (int16_t)(y + 8), 4, 4, OLED_FILLED);
        OLED_DrawRectangle((int16_t)(x + 48), (int16_t)(y + 20), 4, 4, OLED_FILLED);
    }

    Watch_DrawBigDigit((int16_t)(x + 56), y, (unsigned char)(b / 10U));
    Watch_DrawBigDigit((int16_t)(x + 80), y, (unsigned char)(b % 10U));
}

/*
 * @brief Watch face. Four bands, everything centred on x = 64:
 *          date            6x8    y  3..10   x 34..93
 *          big HH:MM       7seg   y 13..44   x 14..113
 *          MON | ss        6x8    y 50..57   x 44..83
 *          second rail     2 px   y 60..61   x 14..113
 *        The colon blinks on the half second and the rail grows by
 *        sub-second, both driven by RTCSSEC, so neither needs an ISR.
 */
static void Watch_RenderFace(void)
{
    uint16_t rail;
    unsigned char w, i, showColon;

    OLED_SetColorMode(ColorMode);
    OLED_Brightness(OLED_UI_Brightness);
    OLED_Clear();
    WatchTime_Read(&WatchFaceTime);

    /* Band 1: 2026-10-21 (6x8), 60 px wide, centred at x = 34 */
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

    /* Band 2: big HH:MM, colon blinks each half second */
    showColon = (unsigned char)((WatchFaceTime.SubSec & 0x40U) ? 1 : 0);
    Watch_DrawBigPair(14, 13, WatchFaceTime.Hour, WatchFaceTime.Minute, showColon);

    /* Band 3: MON | 45, a 40 px group centred at x = 64
     * (weekday 44..61, divider 66, seconds 72..83) */
    w = Watch_Weekday(WatchFaceTime.Year, WatchFaceTime.Month, WatchFaceTime.Day);
    for (i = 0; i < 3U; i++)
    {
        OLED_ShowChar((int16_t)(44 + i * 6), 50, WatchWeekNames[w][i], OLED_6X8_HALF);
    }
    OLED_DrawRectangle(66, 51, 1, 6, OLED_FILLED);
    OLED_ShowNum(72, 50, WatchFaceTime.Second, 2, OLED_6X8_HALF);

    /* Band 4: second rail. 1 px track, 2 px fill that reaches the full
     * 100 px at :59.984 and stays at 0 at :00.000 (rounds to nearest) */
    OLED_DrawRectangle(14, 61, 100, 1, OLED_FILLED);
    rail = (uint16_t)(((unsigned long)(WatchFaceTime.Second * 128U + WatchFaceTime.SubSec) * 100UL + 3839UL) / 7679UL);
    if (rail != 0)
    {
        OLED_DrawRectangle(14, 60, (int16_t)rail, 2, OLED_FILLED);
    }
    OLED_Update();
}

/*
 * @brief Stopwatch: same big digit block as the face, MM:SS with a
 *        solid colon (the centiseconds below carry the motion).
 *        State bottom left, running centiseconds bottom right.
 */
static void Watch_RenderStopwatch(void)
{
    unsigned long cs;
    unsigned int minutes;
    unsigned int seconds;
    unsigned int cents;
    unsigned char i, n;

    OLED_SetColorMode(ColorMode);
    OLED_Brightness(OLED_UI_Brightness);
    OLED_Clear();

    /* Title: 54 px wide, centred at x = 37 */
    WatchFaceBuf[0] = 'S'; WatchFaceBuf[1] = 'T'; WatchFaceBuf[2] = 'O';
    WatchFaceBuf[3] = 'P'; WatchFaceBuf[4] = 'W'; WatchFaceBuf[5] = 'A';
    WatchFaceBuf[6] = 'T'; WatchFaceBuf[7] = 'C'; WatchFaceBuf[8] = 'H';
    WatchFaceBuf[9] = '\0';
    OLED_ShowString(37, 2, WatchFaceBuf, OLED_6X8_HALF);

    /* Big MM:SS (same 100 px block as the face, y 13..44) */
    cs = WatchStopwatchCS;
    minutes = (unsigned int)((cs / 6000UL) % 100UL);
    seconds = (unsigned int)((cs / 100UL) % 60UL);
    cents = (unsigned int)(cs % 100UL);
    Watch_DrawBigPair(14, 13, (unsigned char)minutes, (unsigned char)seconds, 1);

    /* State row, bottom left (y 50..57) */
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
    }
    else
    {
        WatchFaceBuf[i++] = 'P';
        WatchFaceBuf[i++] = 'A';
        WatchFaceBuf[i++] = 'U';
        WatchFaceBuf[i++] = 'S';
        WatchFaceBuf[i++] = 'E';
        WatchFaceBuf[i++] = 'D';
    }
    n = i;
    for (i = 0; i < n; i++)
    {
        OLED_ShowChar((int16_t)(8 + i * 6), 50, WatchFaceBuf[i], OLED_6X8_HALF);
    }

    /* Centiseconds, bottom right: ".45" ends at x = 119 */
    OLED_ShowChar(102, 50, '.', OLED_6X8_HALF);
    OLED_ShowNum(108, 50, cents, 2, OLED_6X8_HALF);

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
