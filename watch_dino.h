/* watch_dino.h - Chrome-style dino runner mini-game (Keil C251, C89)
 * Self-contained module: it never touches the WatchMode app state
 * machine or watch_face. Integration pass wires the calls below in.
 */
#ifndef __WATCH_DINO_H
#define __WATCH_DINO_H

void Watch_Dino_Open(void);               /* (re)start: reset state, park trackers */
unsigned char Watch_Dino_Tick(void);      /* 20ms tick; returns 1 = request exit */
void Watch_Dino_Render(void);             /* full-screen draw, call once per frame */
unsigned int Watch_Dino_GetHighScore(void); /* best score, survives a restart */
void Watch_Dino_SetHighScore(unsigned int value); /* used by the EEPROM restore */

#endif
