#ifndef __WATCH_RTC_H
#define __WATCH_RTC_H

#include "AI8051U.H"
#include "DEF.H"
#include "watch_time.h"

/* External battery-backed RTC driver (DS3231 or SD3078) on the same
 * I2C0 bus as the OLED (SDA P2.3, SCL P2.4, 400 kHz).
 *
 * Why it exists: the on-chip RTC runs from the internal 32.768 kHz IRC
 * and drifts up to ~40 s per hour, and it loses the time completely
 * when the power latch opens. Both external chips keep counting from
 * a coin cell at a few microamps and are TCXO compensated, so the
 * watch can be switched off for weeks and still come back on time.
 *
 * Register map used here is the DS3231 / PCF8563 style layout, which
 * SD3078 follows too:
 *   0x00 seconds (BCD, bit7 = CH/oscillator stop on DS3231)
 *   0x01 minutes
 *   0x02 hours   (BCD; bit6 = 12/24 flag, forced to 24 h on write)
 *   0x03 weekday (1..7)
 *   0x04 date
 *   0x05 month   (bit7 = century on some parts, ignored)
 *   0x06 year    (00..99 = 20xx)
 *
 * I2C addresses (7 bit): DS3231 = 0x68, SD3078 = 0x32. Probe tries
 * DS3231 first, then SD3078, and caches the answer.
 */

#define WATCH_RTC_NONE      0
#define WATCH_RTC_DS3231    1
#define WATCH_RTC_SD3078    2

/* Probe the bus once. Returns one of WATCH_RTC_*. Safe to call again;
 * a failed probe is retried, a successful one is cached. */
uint8_t Watch_RTC_Probe(void);

/* Chip found by the last successful probe (WATCH_RTC_NONE before). */
uint8_t Watch_RTC_Type(void);

/* 1 when a chip answered AND its time registers look sane (CH bit
 * clear, fields in range). 0 means "no external RTC" or "time lost",
 * and the caller should fall back to the on-chip RTC. */
uint8_t Watch_RTC_Valid(void);

/* Read the external RTC into t. SubSec is set to 0. Returns 0 on
 * success, 1 on bus error or invalid data (t untouched). */
uint8_t Watch_RTC_Read(WatchTime *t);

/* Write t to the external RTC, forcing 24 h mode and clearing the
 * oscillator-stop flags. Returns 0 on success. */
uint8_t Watch_RTC_Write(const WatchTime *t);

/* Raw register access, exposed for the battery/temperature registers
 * and for bring-up tests. len is limited to 8 bytes. Returns 0 on
 * success. */
uint8_t Watch_RTC_ReadRegs(uint8_t reg, uint8_t *buf, uint8_t len);
uint8_t Watch_RTC_WriteRegs(uint8_t reg, const uint8_t *buf, uint8_t len);

#endif
