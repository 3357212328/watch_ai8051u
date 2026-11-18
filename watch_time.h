#ifndef __WATCH_TIME_H
#define __WATCH_TIME_H

#include "AI8051U.H"
#include "DEF.H"

/* On-chip RTC driver for AI8051U.
 * Registers live in XSFR space 0x7EFE60+, all values are plain HEX
 * (not BCD). The clock source is the internal 32.768 kHz IRC, so no
 * external crystal is required. SubSec is the 1/128 s counter, which
 * gives free sub-second smoothness for colon blink and progress bars. */

typedef struct WatchTime {
    uint8_t Year;     /* 0..99 = 20YY */
    uint8_t Month;    /* 1..12 */
    uint8_t Day;      /* 1..31 */
    uint8_t Hour;     /* 0..23 */
    uint8_t Minute;   /* 0..59 */
    uint8_t Second;   /* 0..59 */
    uint8_t SubSec;   /* 0..127, 1/128 s */
} WatchTime;

/* Start the internal 32K IRC, preload a sane default and run the RTC. */
void WatchTime_Init(void);

/* Read the current counters into t. Values are guaranteed valid unless
 * the user program wrote garbage via WatchTime_Set earlier. */
void WatchTime_Read(WatchTime *t);

/* Clamp, validate and write a new time. Triggers the SETRTC reload.
 * The RTC must already be running (WatchTime_Init called once). */
void WatchTime_Set(const WatchTime *t);

/* Number of days in month (1..12) of year 20xx (0..99), leap aware. */
uint8_t WatchTime_DaysInMonth(uint8_t year2, uint8_t month);

/* Weekday via Zeller's congruence, exact for 2000..2099.
 * year2  = 0..99 meaning 20YY, month 1..12, day 1..31.
 * Returns 0 = Sunday .. 6 = Saturday.
 * Public so usb_ctrl.c can sanity-check frames from the STC-ISP
 * "Set RTC" tool (frame carries a weekday byte). */
uint8_t WatchTime_Weekday(uint8_t year2, uint8_t month, uint8_t day);

#endif
