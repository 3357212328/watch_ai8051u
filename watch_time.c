#include "watch_time.h"
#include "ai_usb.h"

/* Fit a 32.768 kHz crystal between the X32K pins and set this to 1 to
 * clock the RTC from it instead of the internal IRC32K. A crystal is
 * accurate to a few ppm; the internal RC is spec'd at -0.9%..+1.1% over
 * -20..65 C, i.e. up to ~40 s of drift per hour. With no crystal fitted
 * the code below waits for the oscillator and falls back to IRC32K. */
#ifndef WATCH_USE_X32K
#define WATCH_USE_X32K 0
#endif

/* ---- USB host time base (SOF discipline) --------------------------
 * A USB host emits one SOF packet per millisecond and the SIE exposes
 * its 11-bit frame counter in FRAME1/FRAME2. That 1 kHz stream is a far
 * better time base than the internal 32K RC, so the firmware times the
 * interval between two RTC second boundaries against it. The ideal
 * interval is 1000 host ms; the difference is integrated, and every
 * whole second of accumulated error is removed with a one second nudge.
 * Timing intervals instead of comparing absolute positions keeps the
 * 20 ms poll granularity and the RTC sub-second phase out of the
 * measurement (no bias, noise averages out). With no host (charger,
 * power bank, unplugged) DeviceState stays 0 and this returns at once.
 * All state is xdata; nothing here runs in an ISR.
 */
static unsigned int  xdata WtFrame;   /* last 11-bit SOF frame number */
static unsigned long xdata WtUsbMs;   /* host ms since the baseline */
static unsigned long xdata WtLastMs;  /* WtUsbMs at the previous RTC second */
static long          xdata WtErr;     /* accumulated drift, ms, >0 = fast */
static unsigned char xdata WtSec;     /* last RTCSEC sample */
static unsigned char xdata WtArm;     /* 1 = baseline valid */
static unsigned char xdata WtDev;     /* last DeviceState */

/* Manual calibration of the IRC32K. WATCH_TRIM_SPD is how many seconds
 * per day the clock runs FAST; negative means it runs slow. Pick it by
 * timing the watch against a reference for a few hours:
 *     trim = (watch - reference) in seconds * 86400 / elapsed seconds
 * then rebuild and reflash. Compile-time only: this build fills the 64K
 * code space, so there is no EEPROM sector left to keep a run-time value
 * in, and the RTC state is lost at power off anyway. |trim| should be 2
 * or more; the nudge period is 86400/|trim| seconds and must fit in 16
 * bits. The RC drift is temperature dependent, so a constant trim buys
 * roughly a 3x improvement, not perfection. */
#ifndef WATCH_TRIM_SPD
#define WATCH_TRIM_SPD 0
#endif

#if WATCH_TRIM_SPD != 0
#define WATCH_TRIM_MAG (((WATCH_TRIM_SPD) > 0) ? (WATCH_TRIM_SPD) : -(WATCH_TRIM_SPD))
#define WATCH_TRIM_PERIOD ((unsigned int)(86400UL / (unsigned long)WATCH_TRIM_MAG))
static unsigned char xdata WtTrimSec;  /* last RTCSEC seen by the trim */
static unsigned int  xdata WtTrimCnt;  /* seconds since the last nudge */
#endif

/* Days per month for a non-February month */
static const uint8_t code WatchDaysInTable[12] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

/*
 * @brief Days in a month of year 20xx. Uses the simple %4 leap rule,
 *        which is exact for the 2000..2099 range the RTC covers.
 */
uint8_t WatchTime_DaysInMonth(uint8_t year2, uint8_t month)
{
    if (month < 1 || month > 12)
    {
        return 31;
    }
    if (month == 2)
    {
        if ((year2 % 4U) == 0)
        {
            return 29;
        }
        return 28;
    }
    return WatchDaysInTable[month - 1];
}

/*
 * @brief Weekday via Zeller's congruence, exact for 2000..2099.
 *        Same algorithm as the private copies in watch_face.c and
 *        watch_clock.c, but shifted to 0 = Sunday .. 6 = Saturday
 *        (the convention STC-ISP uses in its "Set RTC" frame).
 */
uint8_t WatchTime_Weekday(uint8_t year2, uint8_t month, uint8_t day)
{
    uint16_t yy;
    unsigned int k, j, h, mm;

    yy  = (uint16_t)(2000U + year2);
    mm  = month;
    if (mm < 3U)
    {
        mm += 12U;              /* Jan/Feb count as months 13/14 of prior year */
        yy--;
    }
    k = yy % 100U;
    j = yy / 100U;
    h = (day + (13U * (mm + 1U)) / 5U + k + k / 4U + j / 4U + 5U * j) % 7U;
    /* Zeller h: 0 = Saturday, 1 = Sunday, 2 = Monday ... -> shift so
     * Sunday becomes 0 and Saturday becomes 6. */
    return (uint8_t)((h + 6U) % 7U);
}

/*
 * @brief Enable the internal 32K IRC and wait for it to become stable.
 *        Bounded wait so a broken crystal-less source cannot hang boot.
 */
static void WatchTime_StartIRC32K(void)
{
    unsigned long timeout = 2000000UL;

    IRC32KCR = 0x80;                /* enable internal 32.768 kHz IRC */
    while (timeout != 0UL)
    {
        if ((IRC32KCR & 0x01) != 0) /* bit0 = oscillator stable flag */
        {
            break;
        }
        timeout--;
    }
}

/*
 * @brief Start the RTC with the internal IRC32K and a fixed default.
 *        Polling mode: RTCIEN disabled, the watch face samples the
 *        counters itself every 20 ms. No ISR needed.
 */
void WatchTime_Init(void)
{
    unsigned long timeout;
    unsigned char src;

    EAXFR = 1;                      /* all RTC regs are XSFR (far) */

    src = 0;                        /* 0 = internal IRC32K, 1 = X32K */
#if WATCH_USE_X32K
    {
        /* ENX32K + GAIN32K, then give the crystal time to start. A
         * 32.768 kHz blank can take close to a second on a cold board. */
        X32KCR = 0xC0;
        timeout = 400000UL;         /* ~1 s at 40 MHz */
        while (timeout != 0UL)
        {
            if ((X32KCR & 0x01) != 0)
            {
                src = 1;
                break;
            }
            timeout--;
        }
        if (src == 0)
        {
            X32KCR = 0x00;          /* no crystal fitted: park it */
        }
    }
#endif
    if (src == 0)
    {
        WatchTime_StartIRC32K();
    }

    /* RTCCKS (RTCCFG bit1): 0 = external crystal, 1 = internal 32K */
    if (src != 0)
    {
        RTCCFG &= (unsigned char)~0x02;
    }
    else
    {
        RTCCFG = (RTCCFG & ~0x01) | 0x02;
    }

    /* Default counters: 2026-01-01 12:00:00 (plain HEX codes) */
    INIYEAR  = 26;
    INIMONTH = 1;
    INIDAY   = 1;
    INIHOUR  = 12;
    INIMIN   = 0;
    INISEC   = 0;
    INISSEC  = 0;

    RTCIF  = 0x00;                  /* clear all pending RTC flags */
    RTCIEN = 0x00;                  /* no interrupts, pure polling */

    RTCCR = 0x01;                   /* RUNRTC = 1, start the clock */

    /* Load INI* into the counters, poll until the hardware clears.
     * Bounded wait: on a stuck trigger we bail without a fresh time
     * rather than hang boot forever. */
    RTCCFG |= 0x01;
    timeout = 2000000UL;
    while ((RTCCFG & 0x01) != 0)
    {
        if (timeout == 0UL)
        {
            break;
        }
        timeout--;
    }
}

/*
 * @brief Read all RTC counters (HEX values).
 */
void WatchTime_Read(WatchTime *t)
{
    t->Year   = RTCYEAR;
    t->Month  = RTCMONTH;
    t->Day    = RTCDAY;
    t->Hour   = RTCHOUR;
    t->Minute = RTCMIN;
    t->Second = RTCSEC;
    t->SubSec = RTCSSEC;

    /* Tear guard: when SubSec reads 0 the second counter may have
     * rolled over between the two reads; re-read SEC once and use
     * that value for this frame. */
    if (t->SubSec == 0)
    {
        t->Second = RTCSEC;
    }

    /* Defensive clamps: never render or write an impossible date */
    if (t->Year > 99)   { t->Year = 0; }
    if (t->Month > 12)  { t->Month = 1; }
    if (t->Day < 1)     { t->Day = 1; }
    if (t->Hour > 23)   { t->Hour = 0; }
    if (t->Minute > 59) { t->Minute = 0; }
    if (t->Second > 59) { t->Second = 0; }
    if (t->SubSec > 127){ t->SubSec = 0; }
    if (t->Day > WatchTime_DaysInMonth(t->Year, t->Month))
    {
        t->Day = WatchTime_DaysInMonth(t->Year, t->Month);
    }
}

/*
 * @brief Clamp a full time and store it into the RTC.
 */
void WatchTime_Set(const WatchTime *t)
{
    uint8_t year2, month, day, hour, minute, second, maxday;
    unsigned long timeout;

    EAXFR = 1;
    WtArm = 0;      /* a fresh time invalidates the SOF baseline */

    year2  = t->Year;
    month  = t->Month;
    day    = t->Day;
    hour   = t->Hour;
    minute = t->Minute;
    second = t->Second;

    if (year2 > 99)   { year2 = 99; }
    if (month < 1)    { month = 1; }
    if (month > 12)   { month = 12; }
    if (hour > 23)    { hour = 23; }
    if (minute > 59)  { minute = 59; }
    if (second > 59)  { second = 59; }

    maxday = WatchTime_DaysInMonth(year2, month);
    if (day < 1)     { day = 1; }
    if (day > maxday){ day = maxday; }

    INIYEAR  = year2;
    INIMONTH = month;
    INIDAY   = day;
    INIHOUR  = hour;
    INIMIN   = minute;
    INISEC   = second;
    INISSEC  = 0;

    /* Trigger the SETRTC reload; the bit self-clears on completion.
     * RTC is already running, so only poll the trigger flag. */
    RTCCFG |= 0x01;
    timeout = 2000000UL;
    while ((RTCCFG & 0x01) != 0)
    {
        if (timeout == 0UL)
        {
            /* Hardware stuck: INI values are already written, so the
             * RTC state stays consistent. Bail without confirmation. */
            return;
        }
        timeout--;
    }
}

/*
 * @brief Rewind (dir < 0) or advance (dir > 0) the RTC by exactly one
 *        second. Only the second field is touched, so a nudge that
 *        would borrow from or carry into the minute is deferred: the
 *        caller simply retries on the next tick, one second later.
 *        This keeps the whole calendar out of the hot path.
 *        Returns 1 when the write happened.
 */
static unsigned char WatchTime_Nudge(signed char dir)
{
    WatchTime t;
    unsigned char armed;

    armed = WtArm;          /* a nudge must not fake an armed tracker */
    WatchTime_Read(&t);
    if (dir < 0)
    {
        if (t.Second == 0)
        {
            return 0;
        }
        t.Second--;
    }
    else
    {
        if (t.Second >= 59)
        {
            return 0;
        }
        t.Second++;
    }
    WatchTime_Set(&t);      /* this drops the SOF baseline */
    if (armed != 0)
    {
        /* The nudge only moved the seconds field, so the interval based
         * drift integrator is still valid: just re-arm it. */
        WtSec = RTCSEC;
        WtArm = 1;
    }
    return 1;
}

/*
 * @brief Discipline the RTC against the USB host frame counter.
 *        Call once per 20 ms UI tick. While a host is present each RTC
 *        second boundary is timed against the 1 kHz SOF stream, the
 *        error is integrated, and every whole second of accumulated
 *        drift is removed with a one second nudge. Without a host
 *        (charger, power bank, unplugged) this returns immediately and
 *        the RTC free-runs untouched.
 */
void WatchTime_Discipline(void)
{
    unsigned char lo, hi, ea, sec;
    unsigned int frame, d;
    unsigned long gap;

    if (DeviceState == 0)
    {
        /* No host at all (charger, power bank, unplugged). Do not even
         * touch the SIE registers: the block may not be clocked, and
         * there is no SOF stream to track anyway. */
        if (WtDev != 0)
        {
            WtDev = 0;
            WtArm = 0;
        }
        return;
    }
    if (DeviceState != WtDev)
    {
        WtDev = DeviceState;        /* re-enumerated: drop the baseline */
        WtArm = 0;
    }

    /* Atomic SIE window read: the USB library also owns USBADR/USBDAT
     * inside its interrupt, so keep this short and interrupt free. */
    ea = (unsigned char)EA;
    EA = 0;
    USBADR = FRAME1;
    _nop_();
    _nop_();
    lo = USBDAT;
    USBADR = FRAME2;
    _nop_();
    _nop_();
    hi = USBDAT;
    EA = ea;

    frame = ((unsigned int)(hi & 0x07) << 8) | (unsigned int)lo;

    if (WtArm == 0)
    {
        WtFrame = frame;
        WtUsbMs = 0UL;
        WtLastMs = 0UL;
        WtErr = 0L;
        WtSec = RTCSEC;
        WtArm = 1;
        return;
    }

    /* 11-bit frame counter: the difference is the ms elapsed. A zero or
     * huge delta means there was no SOF in between (charger, or the UI
     * stalled across a wrap), so the interval is meaningless. */
    d = (unsigned int)(frame - WtFrame) & 0x07FFU;
    WtFrame = frame;
    if (d == 0U || d > 300U)
    {
        WtArm = 0;
        return;
    }
    WtUsbMs += (unsigned long)d;

    sec = RTCSEC;
    if (sec == WtSec)
    {
        return;                     /* no second boundary yet */
    }
    WtSec = sec;

    /* One RTC second ended. The host should have advanced 1000 ms in
     * that interval; whatever it did not is this second's drift. */
    gap = WtUsbMs - WtLastMs;
    WtLastMs = WtUsbMs;
    if (gap > 1500UL)
    {
        WtArm = 0;                  /* seconds were missed: resync */
        return;
    }
    WtErr += (long)gap - 1000L;

    if (WtErr >= 1000L)
    {
        /* RTC ahead of the host: rewind it one second. */
        if (WatchTime_Nudge(-1) != 0)
        {
            WtErr -= 1000L;
        }
    }
    else if (WtErr <= -1000L)
    {
        if (WatchTime_Nudge(1) != 0)
        {
            WtErr += 1000L;
        }
    }
}

/*
 * @brief Apply the manual drift trim. Once per RTC second count time and,
 *        every WATCH_TRIM_PERIOD seconds, nudge the RTC by one second
 *        (back when the clock runs fast, forward when it runs slow).
 *        Call from the same 20 ms tick as WatchTime_Discipline.
 *        With WATCH_TRIM_SPD at its default 0 this compiles to nothing.
 */
void WatchTime_TrimTick(void)
{
#if WATCH_TRIM_SPD != 0
    unsigned char sec;

    sec = RTCSEC;
    if (sec == WtTrimSec)
    {
        return;
    }
    WtTrimSec = sec;

    if (WtTrimCnt < WATCH_TRIM_PERIOD)
    {
        WtTrimCnt++;
    }
    if (WtTrimCnt >= WATCH_TRIM_PERIOD)
    {
        /* A nudge is refused when the seconds field sits at 0 or 59;
         * keep the counter saturated and retry on the next second. */
        if (WatchTime_Nudge((signed char)((WATCH_TRIM_SPD) > 0 ? -1 : 1)) != 0)
        {
            WtTrimCnt = 0;
        }
    }
#endif
}
