#ifndef __USB_CTRL_H
#define __USB_CTRL_H

#include "AI8051U.H"
#include "DEF.H"

/* USB-CDC console driving the OLED_UI input path (no physical UART used).
 * Open the STC USB-CDC virtual serial port at any baud (ignored by CDC).
 * Single-char commands (lower or upper case):
 *   e = Enter, b = Back, w = Up, s = Down (one press per char)
 *   + = encoder +1 detent, - = encoder -1 detent
 *   h or ? = print help
 * Anything else is ignored.
 */
void USBCtrl_Init(void);

/* Call every main-loop iteration: drains one OUT packet into pulses. */
void USBCtrl_Poll(void);

/* One-shot takes consumed by OLED_KeyAndEncoderRecord (1 = pressed). */
unsigned char USBCtrl_TakeEnter(void);
unsigned char USBCtrl_TakeBack(void);
unsigned char USBCtrl_TakeUp(void);
unsigned char USBCtrl_TakeDown(void);
/* Accumulated encoder detents since last take. */
short USBCtrl_TakeEncoder(void);

#endif
