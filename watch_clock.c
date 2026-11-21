#include "watch_clock.h"
#include "oled.h"
#include "oled_driver.h"
#include "oled_ui.h"
#include "oled_ui_driver.h"
#include "watch_time.h"
#include "usb_ctrl.h"
#include "set_io.h"

/* Alarm clock / ring / up-running timer / countdown / calendar pages.
 * Storage: ISR counters are `static volatile unsigned long edata`; the
 * stage ints are `int16_t edata` globals (edit windows bind them); all
 * app state, buffers and bools live in xdata; strings/tables in code.
 * The 20 ms ISR hook below only counts and flags: no OLED, no I2C.
 * This module never touches the app WatchMode; Ticks return 1 for
 * "user requests exit" and Watch_Ring_Active() flags the ring page. */

/* 20 ms ticks per second */
#define CLK_TPS  (50UL)

/*
 * Buzzer pin select. P5.0 is bonded out on LQFP48 only: manual 4.3.5 lists
 * PDIP40 pin 25 for BOTH P2.4 and P5.0, while the PDIP40 pinout otherwise
 * follows STC89C52 (P1.0=1 ... P2.4=25, XTAL=18/19, VCC=40, GND=20), where
 * pin 25 is P2.4 - so P5.0 is simply not available on the 40-pin part.
 * Boards built on AI8051U-PDIP40 set WATCH_BUZZ_P34 to 1 and wire the
 * buzzer to P3.4 (PDIP40 pin 14). P3.4 is this project's spare pin: it is
 * only the Timer0 clock input (T0), and no timer here uses an external
 * clock. Default 0 keeps every existing board working unchanged.
 */
#ifndef WATCH_BUZZ_P34
#define WATCH_BUZZ_P34 0
#endif

#if WATCH_BUZZ_P34
#define BUZZ_IO   P34
#define BUZZ_PIN  Pin34
#else
#define BUZZ_IO   P50
#define BUZZ_PIN  Pin50
#endif

/* ===== ISR-context counters (drained behind EA) ======================== */
static volatile unsigned long edata ClkAcc = 0;      /* generic 20ms fires */
static volatile unsigned long edata ClkRingAcc = 0;  /* ring flash rhythm */

/* ===== app state (xdata) =============================================== */
static unsigned char xdata ClkBooted = 0;  /* EAXFR + P5.0 configured once */

/* Alarm on/off flag. Public because the menu binds it to a radio item,
 * which stores an `unsigned char edata *` (oled_ui.h:303) and writes it
 * with edata addressing, so this byte must live in edata, not xdata.
 * Single byte read by the 20 ms ISR: no tearing. */
unsigned char edata ClkAlarmEnabled = 0;
static unsigned char xdata ClkAlarmLastH = 0xFF;   /* last ringed minute stamp */
static unsigned char xdata ClkAlarmLastM = 0xFF;
static unsigned char xdata ClkAlarmMatchCnt = 0;   /* consecutive match 20ms ticks */

/* Up-running timer */
static unsigned char xdata ClkUpRunning = 0;
static unsigned long xdata ClkUpTicks = 0;         /* 20ms ticks elapsed */
static unsigned char xdata ClkUpLastEnter = 1;
static unsigned char xdata ClkUpLastBack = 1;
static unsigned char xdata ClkUpLastUp = 1;        /* press-edge trackers */
static unsigned char xdata ClkUpLastDown = 1;

/* Countdown */
static unsigned long xdata ClkCdRem = 0;           /* remaining 20ms ticks */
static unsigned char xdata ClkCdRunning = 0;
static unsigned char xdata ClkCdHold = 0;          /* 1 = Back left a remaining
                                                    * value to keep on re-open */
static unsigned char xdata ClkCdLastEnter = 1;
static unsigned char xdata ClkCdLastBack = 1;
static unsigned char xdata ClkCdLastUp = 1;        /* press-edge trackers */
static unsigned char xdata ClkCdLastDown = 1;

/* Calendar (viewed month/weekday edge trackers) */
static unsigned char xdata ClkCalYear2 = 0;        /* 0..99 = 20xx */
static unsigned char xdata ClkCalMonth = 1;        /* 1..12 */
static unsigned char xdata ClkCalLastEnter = 1;
static unsigned char xdata ClkCalLastBack = 1;
static unsigned char xdata ClkCalLastUp = 1;
static unsigned char xdata ClkCalLastDown = 1;

/* Ring */
static const char code * xdata ClkRingText = (const char code *)0;
static unsigned char xdata ClkRingOn = 0;

/* staging values shared with the framework edit windows */
int16_t edata Watch_StageAlarmHour = 7;
int16_t edata Watch_StageAlarmMinute = 0;
int16_t edata Watch_StageCountMin = 1;
int16_t edata Watch_StageCountSec = 0;

/* display scratch */
static WatchTime xdata ClkCalTime;
static char xdata ClkBuf[16];

/* ===== flash-resident strings / tables ================================= */
static char code ClkDefRing[]  = "ALARM!";
static char code ClkCdRing[]   = "COUNT DOWN!";
static char code ClkPressKey[] = "PRESS ANY KEY";
static char code ClkRunTxt[]   = "RUNNING";
static char code ClkPauseTxt[] = "PAUSED";
static char code ClkTitleUp[]  = "UP TIMER";
static char code ClkTitleCd[]  = "COUNT DOWN";
static char code ClkHintUp[]   = "ENT:PAUSE UP:RESET";
static char code ClkHintCd[]     = "ENT:PAUSE UP:RELOAD";  /* while running */
static char code ClkHintCdIdle[] = "ENT:RUN UP:RELOAD";    /* while paused */
static char code ClkMoNames[12][10] =
{
    "JANUARY", "FEBRUARY", "MARCH", "APRIL", "MAY", "JUNE",
    "JULY", "AUGUST", "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER"
};
static char code ClkWkNames[7][3] =
{
    "MO", "TU", "WE", "TH", "FR", "SA", "SU"
};

/* ===== small helpers =================================================== */

/*
 * @brief Buzzer line (BUZZ_IO: P5.0 on LQFP48, P3.4 on PDIP40): push-pull output, parked low. EAXFR is armed
 *        beside it so Watch_ClockTick20's direct RTC reads always work
 *        (watch_time.c leaves EAXFR on after Init; we re-arm defensively).
 */
static void Clk_PinInit(void)
{
    EAXFR = 1;
    set_io_mode(pp_mode, BUZZ_PIN, Pin_End);
    BUZZ_IO = 0;
}

/* Draw a code-space string centered on one row (advance = font width). */
static void Clk_DrawC(const char code *s, uint8_t font, int16_t y)
{
    uint16_t n, i, x;

    n = 0;
    while (s[n] != 0 && n < 128U / font) { n++; }
    x = (128U - n * (uint16_t)font) / 2U;
    for (i = 0; i < n; i++)
    {
        OLED_ShowChar((int16_t)x, y, s[i], font);
        x += font;
    }
}

/* Copy a code string into the xdata scratch buffer, return its length. */
static uint16_t Clk_Copy(const char code *s)
{
    uint16_t i = 0;

    while (s[i] != 0) { ClkBuf[i] = s[i]; i++; }
    ClkBuf[i] = '\0';
    return i;
}

/* Build "MM:SS" in the scratch buffer. */
static void Clk_FillMMSS(unsigned char mm, unsigned char ss)
{
    ClkBuf[0] = (char)('0' + mm / 10U);
    ClkBuf[1] = (char)('0' + mm % 10U);
    ClkBuf[2] = ':';
    ClkBuf[3] = (char)('0' + ss / 10U);
    ClkBuf[4] = (char)('0' + ss % 10U);
    ClkBuf[5] = '\0';
}

/* Shared layout for the up timer and countdown pages. */
static void Clk_PageMMSS(const char code *title, const char code *hint,
                         unsigned char mm, unsigned char ss,
                         unsigned char running)
{
    OLED_SetColorMode(ColorMode);
    OLED_Brightness(OLED_UI_Brightness);
    OLED_Clear();
    Clk_DrawC(title, OLED_6X8_HALF, 2);
    Clk_FillMMSS(mm, ss);
    OLED_ShowString(39, 16, ClkBuf, OLED_10X20_HALF);
    Clk_DrawC((running != 0) ? ClkRunTxt : ClkPauseTxt, OLED_6X8_HALF, 42);
    Clk_DrawC(hint, OLED_6X8_HALF, 55);
    OLED_Update();
}

/*
 * @brief Weekday via Zeller's congruence, exact for 2000..2099,
 *        Mon = 0 .. Sun = 6 (same math as watch_face's private copy).
 */
static unsigned char Clk_Weekday(int16_t y, unsigned char m, unsigned char d)
{
    unsigned int k, j, h, mm, yy;

    yy = (unsigned int)y;
    mm = (unsigned int)m;
    if (mm < 3U)
    {
        mm += 12U;
        yy -= 1U;
    }
    k = yy % 100U;
    j = yy / 100U;
    h = ((unsigned int)d + (13U * (mm + 1U)) / 5U + k + k / 4U + j / 4U + 5U * j) % 7U;
    /* h: 0=Sat 1=Sun 2=Mon ... -> shift into Mon = 0 */
    return (unsigned char)((h + 5U) % 7U);
}

/* Countdown value from the staging ints, capped at 99:59, in ticks. */
static unsigned long Clk_StagedTicks(void)
{
    unsigned long sec;

    sec = (unsigned long)((unsigned int)Watch_StageCountMin * 60U) +
          (unsigned long)((unsigned int)Watch_StageCountSec);
    if (sec > 5999UL) { sec = 5999UL; }
    return sec * CLK_TPS;
}

/* Calendar view: back to the current RTC month. */
static void Clk_CalToday(void)
{
    WatchTime_Read(&ClkCalTime);
    ClkCalYear2 = ClkCalTime.Year;
    ClkCalMonth = ClkCalTime.Month;
}

static void Clk_CalPrev(void)
{
    if (ClkCalMonth <= 1)
    {
        ClkCalMonth = 12;
        if (ClkCalYear2 == 0) { ClkCalYear2 = 99; }
        else { ClkCalYear2--; }
    }
    else { ClkCalMonth--; }
}

static void Clk_CalNext(void)
{
    if (ClkCalMonth >= 12)
    {
        ClkCalMonth = 1;
        if (ClkCalYear2 == 99) { ClkCalYear2 = 0; }
        else { ClkCalYear2++; }
    }
    else { ClkCalMonth++; }
}

/* Discard stale virtual pulses + encoder detents on every Open. */
static void Clk_DrainAll(void)
{
    USBCtrl_TakeEnter();
    USBCtrl_TakeBack();
    USBCtrl_TakeUp();
    USBCtrl_TakeDown();
    USBCtrl_TakeEncoder();
    Encoder_Get();
}

/* Drain the shared 20 ms fire counter so old fires cannot replay. */
static void Clk_DrainAcc(void)
{
    EA = 0;
    if (ClkAcc != 0UL) { ClkAcc = 0; }
    EA = 1;
}

/* ===== 20 ms ISR hook =================================================== */

/*
 * @brief Timer11 ISR hook context (watch_face integration), every 20 ms.
 *        Counting and flagging only: no OLED access, no I2C. The alarm
 *        match reads the RTC HOUR/MIN/SEC registers directly (plain HEX),
 *        same EAXFR discipline watch_time.c uses (armed once and left on;
 *        we re-arm once more on the first fire for safety). The match
 *        must hold for 2 consecutive ticks (anti-glitch) and the minute
 *        stamp is re-armed in every non-matching minute, so the alarm
 *        rings once per matching minute, every matching minute.
 */
void Watch_ClockTick20(void)
{
    if (ClkBooted == 0)
    {
        Clk_PinInit();
        ClkBooted = 1;
    }

    ClkAcc++;       /* consumed by the mm:ss pages */
    ClkRingAcc++;   /* drives the ring banner rhythm */

    if (ClkAlarmEnabled != 0)
    {
        unsigned char h, m, s;
        unsigned char match;

        h = (unsigned char)RTCHOUR;
        m = (unsigned char)RTCMIN;
        s = (unsigned char)RTCSEC;
        match = (unsigned char)(s == 0 &&
            (unsigned char)Watch_StageAlarmHour == h &&
            (unsigned char)Watch_StageAlarmMinute == m);
        if (match != 0)
        {
            if (!(ClkAlarmLastH == h && ClkAlarmLastM == m))
            {
                ClkAlarmMatchCnt++;
                if (ClkAlarmMatchCnt >= 2)   /* second consecutive match tick */
                {
                    ClkAlarmLastH = h;       /* one shot per matching minute */
                    ClkAlarmLastM = m;
                    ClkAlarmMatchCnt = 0;
                    Watch_Ring_Fire(ClkDefRing);   /* flag set only, ISR safe */
                }
            }
            else { ClkAlarmMatchCnt = 0; }   /* already rung this minute */
        }
        else
        {
            ClkAlarmMatchCnt = 0;    /* interrupted run restarts */
            ClkAlarmLastH = 0xFF;    /* re-arm: non-matching minute */
            ClkAlarmLastM = 0xFF;
        }
    }
}

/* ===== ring ============================================================= */

/*
 * @brief Raise the ring request. Passing (const char code *)0 selects
 *        the default "ALARM!" banner text.
 */
void Watch_Ring_Fire(const char code *text)
{
    ClkRingText = (text == (const char code *)0) ? ClkDefRing : text;
    ClkRingOn = 1;
}

unsigned char Watch_Ring_Active(void)
{
    return ClkRingOn;
}

/* Clears the flag and parks the buzzer line. */
void Watch_Ring_Stop(void)
{
    BUZZ_IO = 0;
    ClkRingOn = 0;
}

/*
 * @brief Flashing full-screen banner: alternate color mode each 0.5 s
 *        (1 = normal, 0 = inverted), big text when short, one ~10 ms
 *        2 kHz P5.0 burst before each OLED_Update. Consumes nothing;
 *        the ring mode and its input draining belong to watch_face.
 */
void Watch_Ring_Render(void)
{
    unsigned long ticks;
    const char code *txt;
    unsigned char phase, len, i;

    if (ClkBooted == 0)           /* belt: pin setup even without Tick20 */
    {
        Clk_PinInit();
        ClkBooted = 1;
    }

    EA = 0;
    ticks = ClkRingAcc;
    txt = ClkRingText;      /* snapshot both behind EA, ISR may rebind */
    EA = 1;
    phase = (unsigned char)((ticks / 25UL) & 1UL);   /* 500 ms half-period */

    if (txt == (const char code *)0) { txt = ClkDefRing; }
    len = 0;
    while (txt[len] != 0 && len < 24U) { len++; }    /* bounded, no tear */
    if (len == 0) { txt = ClkDefRing; len = 6; }

    OLED_SetColorMode((phase != 0) ? 1 : 0);
    OLED_Clear();
    if (len <= 6)
    {
        Clk_DrawC(txt, OLED_10X20_HALF, 18);
        Clk_DrawC(ClkPressKey, OLED_6X8_HALF, 44);
    }
    else
    {
        Clk_DrawC(txt, OLED_6X8_HALF, 20);
        Clk_DrawC(ClkPressKey, OLED_6X8_HALF, 36);
    }

    /* one bounded 2 kHz burst before presenting the frame */
    for (i = 0; i < 20; i++)
    {
        BUZZ_IO = 0;
        Delay_us(250);
        BUZZ_IO = 1;
        Delay_us(250);
    }
    BUZZ_IO = 0;
    OLED_Update();
}

/* ===== alarm ============================================================ */
/* Flag only: the alarm STATUS page is gone (nothing ever entered APP_ALARM,
 * so its renderer and tick were dead weight) and the ON/OFF state is now
 * shown by the menu radio item bound to ClkAlarmEnabled. */

/* ===== up-running timer ================================================= */

void Watch_Uptimer_Open(void)
{
    Clk_DrainAll();
    Clk_DrainAcc();
    ClkUpRunning = 0;
    ClkUpTicks = 0;
    ClkUpLastEnter = (unsigned char)(Key_GetEnterStatus() == 0);
    ClkUpLastBack  = (unsigned char)(Key_GetBackStatus()  == 0);
    ClkUpLastUp    = (unsigned char)(Key_GetUpStatus()    == 0);
    ClkUpLastDown  = (unsigned char)(Key_GetDownStatus()  == 0);
}

/*
 * @brief Re-park the up timer after a ring hijack without losing the
 *        accumulated ticks: drops the virtual keys/encoder queued
 *        during the ring and re-parks the four edge trackers.
 *        ClkUpTicks / ClkUpRunning are untouched, and the 20 ms fires
 *        are deliberately NOT drained: like the stopwatch (whose time
 *        base is the same ISR counter), a count-up timer must keep
 *        counting through the alarm, so the first tick after the
 *        banner adds the interrupted interval.
 */
void Watch_Uptimer_Park(void)
{
    Clk_DrainAll();          /* discard ring-period virtual keys/encoder */
    ClkUpLastEnter = (unsigned char)(Key_GetEnterStatus() == 0);
    ClkUpLastBack  = (unsigned char)(Key_GetBackStatus()  == 0);
    ClkUpLastUp    = (unsigned char)(Key_GetUpStatus()    == 0);
    ClkUpLastDown  = (unsigned char)(Key_GetDownStatus()  == 0);
}

/*
 * @brief Enter toggles run/pause (pulse or release edge); the encoder
 *        (USB or physical), the USB Up pulse and the Up/Down physical
 *        press edges reset to 00:00 but only while paused. Back pulse
 *        or release edge returns 1. Every 20 ms fire is drained
 *        behind EA even while paused.
 */
unsigned char Watch_Uptimer_Tick(void)
{
    unsigned char usb_e, usb_b, usb_u;
    unsigned char pressEnter, pressBack, pressUp, pressDown, exitReq;
    unsigned long n;
    short encU, encP;

    usb_e = USBCtrl_TakeEnter();
    usb_b = USBCtrl_TakeBack();
    usb_u = USBCtrl_TakeUp();
    USBCtrl_TakeDown();   /* drained, no behavior on this page */
    encU = USBCtrl_TakeEncoder();
    encP = Encoder_Get();

    exitReq = 0;
    if (usb_b != 0) { exitReq = 1; }

    pressEnter = (unsigned char)(Key_GetEnterStatus() == 0);
    pressBack = (unsigned char)(Key_GetBackStatus() == 0);
    pressUp = (unsigned char)(Key_GetUpStatus() == 0);
    pressDown = (unsigned char)(Key_GetDownStatus() == 0);
    if (pressEnter == 0 && ClkUpLastEnter != 0)
    {
        if (ClkUpRunning != 0) { ClkUpRunning = 0; }
        else { ClkUpRunning = 1; }
    }
    if (pressBack == 0 && ClkUpLastBack != 0) { exitReq = 1; }

    if ((encU != 0 || encP != 0) && ClkUpRunning == 0)
    {
        ClkUpTicks = 0;
    }
    /* USB Up pulse: same behavior as the encoder, paused only; the
     * Up/Down physical press edges mirror it, running ignores both. */
    if (usb_u != 0 && ClkUpRunning == 0)
    {
        ClkUpTicks = 0;
    }
    if (ClkUpRunning == 0 &&
        ((pressUp != 0 && ClkUpLastUp == 0) ||
         (pressDown != 0 && ClkUpLastDown == 0)))
    {
        ClkUpTicks = 0;
    }

    ClkUpLastEnter = pressEnter;
    ClkUpLastBack = pressBack;
    ClkUpLastUp = pressUp;
    ClkUpLastDown = pressDown;

    if (exitReq != 0) { return 1; }

    EA = 0;
    n = ClkAcc;
    ClkAcc = 0;
    EA = 1;
    if (ClkUpRunning != 0) { ClkUpTicks += n; }
    return 0;
}

void Watch_Uptimer_Render(void)
{
    unsigned long sec;
    unsigned int mm, ss;

    /* Display wraps every 100 minutes instead of freezing at 99:59;
     * the internal tick count (ClkUpTicks) still accumulates fully. */
    sec = (ClkUpTicks / CLK_TPS) % 6000UL;
    mm = (unsigned int)(sec / 60UL);
    ss = (unsigned int)(sec % 60UL);
    Clk_PageMMSS(ClkTitleUp, ClkHintUp, (unsigned char)mm, (unsigned char)ss,
                 ClkUpRunning);
}

/* ===== countdown ======================================================== */

/*
 * @brief Enter the countdown page. A fresh entry adopts the staged
 *        Minute/Second values (paused); after a Back exit the remaining
 *        ticks are kept (ClkCdHold), so re-entering resumes instead of
 *        reloading. Use Watch_Countdown_Reset() for an explicit reload.
 */
void Watch_Countdown_Open(void)
{
    Clk_DrainAll();
    Clk_DrainAcc();
    if (ClkCdHold != 0)
    {
        /* Coming back from a Back exit: keep ClkCdRem, stay paused. */
        ClkCdHold = 0;
        ClkCdRunning = 0;
    }
    else
    {
        ClkCdRem = Clk_StagedTicks();
        ClkCdRunning = 0;
    }
    ClkCdLastEnter = (unsigned char)(Key_GetEnterStatus() == 0);
    ClkCdLastBack  = (unsigned char)(Key_GetBackStatus()  == 0);
    ClkCdLastUp    = (unsigned char)(Key_GetUpStatus()    == 0);
    ClkCdLastDown  = (unsigned char)(Key_GetDownStatus()  == 0);
}

/*
 * @brief Re-park the countdown after a ring hijack without losing the
 *        remaining ticks: drains queued input + stale 20 ms fires and
 *        re-parks the edge trackers. ClkCdRem / ClkCdRunning are kept.
 */
void Watch_Countdown_Park(void)
{
    Clk_DrainAll();          /* discard ring-period virtual keys/encoder */
    Clk_DrainAcc();          /* discard ring-period 20 ms fires */
    ClkCdLastEnter = (unsigned char)(Key_GetEnterStatus() == 0);
    ClkCdLastBack  = (unsigned char)(Key_GetBackStatus()  == 0);
    ClkCdLastUp    = (unsigned char)(Key_GetUpStatus()    == 0);
    ClkCdLastDown  = (unsigned char)(Key_GetDownStatus()  == 0);
}

/*
 * @brief Explicit reload from the staged Minute/Second value (menu
 *        "Start!" item). Drops any held remaining time.
 */
void Watch_Countdown_Reset(void)
{
    ClkCdHold = 0;
    ClkCdRem = Clk_StagedTicks();
}

/*
 * @brief Enter: pause / resume / rearm from the staging values when it
 *        sits at zero. The encoder (USB or physical), the USB Up pulse
 *        and the Up/Down physical press edges reload the staged values
 *        while paused. Back pulse or release edge returns 1. When a
 *        running countdown reaches zero it stops and fires the ring
 *        (this runs in the main loop, so raising the ring is safe).
 */
unsigned char Watch_Countdown_Tick(void)
{
    unsigned char usb_e, usb_b, usb_u;
    unsigned char pressEnter, pressBack, pressUp, pressDown, exitReq;
    unsigned long n;
    short encU, encP;

    usb_e = USBCtrl_TakeEnter();
    usb_b = USBCtrl_TakeBack();
    usb_u = USBCtrl_TakeUp();
    USBCtrl_TakeDown();   /* drained, no behavior on this page */
    encU = USBCtrl_TakeEncoder();
    encP = Encoder_Get();

    exitReq = 0;
    if (usb_b != 0) { exitReq = 1; }

    pressEnter = (unsigned char)(Key_GetEnterStatus() == 0);
    pressBack = (unsigned char)(Key_GetBackStatus() == 0);
    pressUp = (unsigned char)(Key_GetUpStatus() == 0);
    pressDown = (unsigned char)(Key_GetDownStatus() == 0);

    if (usb_e != 0 || (pressEnter == 0 && ClkCdLastEnter != 0))
    {
        if (ClkCdRunning != 0) { ClkCdRunning = 0; }
        else if (ClkCdRem == 0UL) { ClkCdRem = Clk_StagedTicks(); }
        else { ClkCdRunning = 1; }
    }
    if (pressBack == 0 && ClkCdLastBack != 0) { exitReq = 1; }

    if ((encU != 0 || encP != 0) && ClkCdRunning == 0)
    {
        ClkCdRem = Clk_StagedTicks();
    }
    /* USB Up pulse: same behavior as the encoder, paused only; the
     * Up/Down physical press edges mirror it, running ignores both. */
    if (usb_u != 0 && ClkCdRunning == 0)
    {
        ClkCdRem = Clk_StagedTicks();
    }
    if (ClkCdRunning == 0 &&
        ((pressUp != 0 && ClkCdLastUp == 0) ||
         (pressDown != 0 && ClkCdLastDown == 0)))
    {
        ClkCdRem = Clk_StagedTicks();
    }

    ClkCdLastEnter = pressEnter;
    ClkCdLastBack = pressBack;
    ClkCdLastUp = pressUp;
    ClkCdLastDown = pressDown;

    if (exitReq != 0)
    {
        /* Remember that the remaining ticks must survive the exit, so
         * Watch_Countdown_Open() resumes instead of reloading staging. */
        ClkCdHold = 1;
        return 1;
    }

    EA = 0;
    n = ClkAcc;
    ClkAcc = 0;
    EA = 1;

    if (ClkCdRunning != 0)
    {
        if (ClkCdRem > n) { ClkCdRem -= n; }
        else
        {
            ClkCdRem = 0;
            ClkCdRunning = 0;
            Watch_Ring_Fire(ClkCdRing);
        }
    }
    return 0;
}

void Watch_Countdown_Render(void)
{
    unsigned long sec;
    unsigned int mm, ss;

    sec = (ClkCdRem + CLK_TPS - 1UL) / CLK_TPS;   /* ceil, shows the start value */
    if (sec > 5999UL) { sec = 5999UL; }
    mm = (unsigned int)(sec / 60UL);
    ss = (unsigned int)(sec % 60UL);
    Clk_PageMMSS(ClkTitleCd, (ClkCdRunning != 0) ? ClkHintCd : ClkHintCdIdle,
                 (unsigned char)mm, (unsigned char)ss, ClkCdRunning);
}

/* ===== calendar ========================================================= */

void Watch_Calendar_Open(void)
{
    Clk_DrainAll();
    Clk_CalToday();
    ClkCalLastEnter = (unsigned char)(Key_GetEnterStatus() == 0);
    ClkCalLastBack  = (unsigned char)(Key_GetBackStatus()  == 0);
    ClkCalLastUp    = (unsigned char)(Key_GetUpStatus()    == 0);
    ClkCalLastDown  = (unsigned char)(Key_GetDownStatus()  == 0);
}

/*
 * @brief Up (pulse or release edge) = previous month, Down = next month,
 *        +/- encoder detents step the same way (year wraps 2099/2000),
 *        Enter jumps back to today, Back pulse / release edge returns 1.
 *        All six input sources are drained on every call.
 */
unsigned char Watch_Calendar_Tick(void)
{
    unsigned char pressEnter, pressBack, pressUp, pressDown, exitReq;
    short encU, encP;

    pressEnter = (unsigned char)(Key_GetEnterStatus() == 0);
    pressBack = (unsigned char)(Key_GetBackStatus() == 0);
    pressUp = (unsigned char)(Key_GetUpStatus() == 0);
    pressDown = (unsigned char)(Key_GetDownStatus() == 0);

    exitReq = 0;
    if (USBCtrl_TakeBack() != 0) { exitReq = 1; }
    if (USBCtrl_TakeEnter() != 0) { Clk_CalToday(); }
    if (USBCtrl_TakeUp() != 0) { Clk_CalPrev(); }
    if (USBCtrl_TakeDown() != 0) { Clk_CalNext(); }
    encU = USBCtrl_TakeEncoder();
    encP = Encoder_Get();
    if (encU > 0 || encP > 0) { Clk_CalNext(); }
    else if (encU < 0 || encP < 0) { Clk_CalPrev(); }

    if (pressEnter == 0 && ClkCalLastEnter != 0) { Clk_CalToday(); }
    if (pressUp == 0 && ClkCalLastUp != 0) { Clk_CalPrev(); }
    if (pressDown == 0 && ClkCalLastDown != 0) { Clk_CalNext(); }
    if (pressBack == 0 && ClkCalLastBack != 0) { exitReq = 1; }

    ClkCalLastEnter = pressEnter;
    ClkCalLastBack = pressBack;
    ClkCalLastUp = pressUp;
    ClkCalLastDown = pressDown;

    if (exitReq != 0) { return 1; }
    return 0;
}

/*
 * @brief Month grid: title, weekday header, 2-digit dates on a 6 px /
 *        7 px cell grid, today's cell marked with a 1 px underline
 *        under the date. Fast: a single OLED_Update at the end.
 *        Height budget: title y=1, header y=11, dates y=21..63 (6 rows
 *        of 7 px, bottom row within the 64 px screen).
 */
void Watch_Calendar_Render(void)
{
    unsigned char w, dim, d, col, row, i, nlen, rows, pitch;
    uint16_t x;
    int16_t y, dateY, hdrY, titleY;

    WatchTime_Read(&ClkCalTime);

    OLED_SetColorMode(ColorMode);
    OLED_Brightness(OLED_UI_Brightness);
    OLED_Clear();

    /* Grid geometry. rows is how many week rows this month needs (4..6).
     * A 5-row month gets a 9 px row pitch, one pixel of leading between
     * the 8 px glyph cells. A 6-row month falls back to 8 px pitch and
     * starts 2 px higher so the last row still ends inside the panel.
     * The old layout always used 7 px, i.e. glyph cells overlapped by a
     * pixel, which is what made the dates look crammed. */
    w = Clk_Weekday((int16_t)(2000 + (unsigned int)ClkCalYear2),
                    ClkCalMonth, 1);
    dim = WatchTime_DaysInMonth(ClkCalYear2, ClkCalMonth);
    rows = (unsigned char)(((uint16_t)w + (uint16_t)dim + 6U) / 7U);
    if (rows <= 5U)
    {
        titleY = 1; hdrY = 10; dateY = 19; pitch = 9;
    }
    else
    {
        titleY = 0; hdrY = 8; dateY = 16; pitch = 8;
    }

    /* title: "OCTOBER 2026" */
    nlen = (unsigned char)Clk_Copy(ClkMoNames[ClkCalMonth - 1]);
    ClkBuf[nlen++] = ' ';
    ClkBuf[nlen++] = '2';
    ClkBuf[nlen++] = '0';
    ClkBuf[nlen++] = (char)('0' + ClkCalYear2 / 10U);
    ClkBuf[nlen++] = (char)('0' + ClkCalYear2 % 10U);
    ClkBuf[nlen] = '\0';
    x = (128U - (uint16_t)nlen * 6U) / 2U;
    OLED_ShowString((int16_t)x, titleY, ClkBuf, OLED_6X8_HALF);

    /* weekday header, Monday first */
    for (i = 0; i < 7; i++)
    {
        ClkBuf[0] = ClkWkNames[i][0];
        ClkBuf[1] = ClkWkNames[i][1];
        ClkBuf[2] = '\0';
        OLED_ShowString((int16_t)((uint16_t)i * 18U + 4U), hdrY, ClkBuf,
                        OLED_6X8_HALF);
    }

    for (d = 1; d <= dim; d++)
    {
        col = (unsigned char)(((uint16_t)w + (uint16_t)d - 1U) % 7U);
        row = (unsigned char)(((uint16_t)w + (uint16_t)d - 1U) / 7U);
        x = (uint16_t)col * 18U + 4U;
        y = (int16_t)((uint16_t)dateY + (uint16_t)row * (uint16_t)pitch);
        ClkBuf[0] = (char)('0' + d / 10U);
        ClkBuf[1] = (char)('0' + d % 10U);
        ClkBuf[2] = '\0';
        OLED_ShowString((int16_t)x, y, ClkBuf, OLED_6X8_HALF);
    }

    /* underline today's cell when its month is on screen */
    if (ClkCalYear2 == ClkCalTime.Year && ClkCalMonth == ClkCalTime.Month)
    {
        col = (unsigned char)(((uint16_t)w + (uint16_t)ClkCalTime.Day - 1U) % 7U);
        row = (unsigned char)(((uint16_t)w + (uint16_t)ClkCalTime.Day - 1U) / 7U);
        x = (uint16_t)col * 18U + 4U;
        y = (int16_t)((uint16_t)dateY + (uint16_t)row * (uint16_t)pitch);
        OLED_DrawRectangle((int16_t)x, (int16_t)(y + 7), 12, 1, 1);
    }

    OLED_Update();
}
