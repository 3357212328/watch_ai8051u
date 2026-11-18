#include "watch_time.h"

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

    EAXFR = 1;                      /* all RTC regs are XSFR (far) */

    WatchTime_StartIRC32K();

    /* Select the internal 32K source (RTCCKS = RTCCFG bit1) */
    RTCCFG = (RTCCFG & ~0x01) | 0x02;

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
