#ifndef __WATCH_FLASH_H
#define __WATCH_FLASH_H

#include "AI8051U.H"
#include "DEF.H"

/* Optional 25-series SPI NOR (W25Q64JV etc.) on SPI0:
 *   P1.7 SCLK, P1.5 MOSI, P1.6 MISO, P5.2 CS (plain GPIO).
 * None of those pins is used by anything else, and the probe simply
 * fails on a board without the chip, so this module is safe to build
 * and run everywhere: every accessor then reports "absent".
 *
 * Only sector 0 (first 4 KB) is used, as a mirror of the settings
 * record that survives a code reflash. The on-chip EEPROM sector at
 * 0xF000 stays as the fallback.
 */

/* Configure the SPI pins and probe the JEDEC ID. Call once at boot,
 * after the system clock is up. */
void Watch_FlashInit(void);

/* 1 when a 25-series chip answered the probe. */
uint8_t Watch_FlashPresent(void);

/* Raw access. Return 0 on success, 1 when absent or timed out. */
uint8_t Watch_FlashRead(uint32_t addr, uint8_t *buf, uint16_t len);
uint8_t Watch_FlashWrite(uint32_t addr, const uint8_t *buf, uint16_t len);
uint8_t Watch_FlashEraseSector(uint32_t addr);

#endif
