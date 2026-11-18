#ifndef __WATCH_PLANE_H
#define __WATCH_PLANE_H

#include "AI8051U.H"
#include "DEF.H"

/* (re)start: reset state, park trackers */
void Watch_Plane_Open(void);

/* 20ms tick; returns 1 = request exit */
unsigned char Watch_Plane_Tick(void);

/* full-screen draw, one OLED_Update at end */
void Watch_Plane_Render(void);

#endif
