#ifndef __WATCH_POWER_H
#define __WATCH_POWER_H

#include "AI8051U.H"
#include "DEF.H"

/* Power, key and storage services for the second-generation board.
 *
 * Hardware assumed (see the hardware design doc, section 10, all
 * optional):
 *   P2.6  PWR_HOLD   drives the NPN that clamps the P-MOS gate. High
 *                    keeps the board powered; the firmware must raise it
 *                    within ~200 ms of a key-on boot.
 *   P5.1  TORCH      gate of the white-LED NMOS (PWM6_4 capable).
 *   P3.5  HOME       new key, active low. One press returns to the face
 *                    and switches the torch off.
 *
 * On a board without the latch P2.6 is simply a spare output that does
 * nothing, and P5.1/P3.5 stay harmless inputs, so this module is safe
 * on the first-generation board too.
 */

/* Raise PWR_HOLD, park the torch, arm the IAP timing and restore the
 * saved settings. Call first thing in main(), right after the system
 * clock, so the latch is held before the OLED init delay. */
void Watch_PowerInit(void);

/* Every 20 ms, before the mode switch. Returns 1 exactly once per BACK
 * press when the key has been held for 1.5 s: the caller shows the
 * confirmation popup and, if accepted, calls Watch_PowerOff(). */
uint8_t Watch_PowerTick(void);

/* Save settings + time to the on-chip EEPROM sector. Never call per
 * keystroke: one save erases and reprograms a 512 B sector. */
void Watch_PowerSave(void);

/* Read the record back. Called once from Watch_PowerInit(). Applies
 * brightness / dark mode / high scores; time is handled separately by
 * the first tick (external RTC probe needs the I2C bus up first). */
void Watch_PowerRestore(void);

/* Save, switch the torch off, release the latch, wait for the button
 * to be released and park. Does not return when the board is on
 * battery; parks with a blank screen when USB keeps it alive. */
void Watch_PowerOff(void);

/* Battery voltage in mV, 0 when the reading is not trustworthy.
 * WATCH_VBAT_DIVIDER 0 (default) measures VCC through the internal
 * 1.19 V reference, which is correct while the MCU runs straight off
 * the cell. Set it to 1 after fitting the 100k/100k divider on P1.2
 * behind a 3.3 V LDO. */
unsigned int Watch_PowerBatteryMv(void);

/* Last cached reading in mV (0 = unknown). Refreshed once a minute and
 * on the first tick, so drawing a gauge every frame costs no ADC time. */
unsigned int Watch_PowerBatteryMvCached(void);

/* Flashlight. The menu radio button edits Watch_TorchFlag directly and
 * the 20 ms tick follows it, so no callback is needed. */
void Watch_TorchSet(uint8_t on);
uint8_t Watch_TorchIsOn(void);
extern uint8_t edata Watch_TorchFlag;

#endif
