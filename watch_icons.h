#ifndef __WATCH_ICONS_H
#define __WATCH_ICONS_H

#include "AI8051U.H"
#include "DEF.H"

/* 32x32 watch/clock tile icon, same 128-byte column format as the
 * Image_* icons in oled_fonts.c (byte index = page*32 + x). */
extern const uint8_t code Image_watch[];

/* 32x32 menu tile icons, same page*32+x byte layout as Image_watch */
extern const uint8_t code Image_alarm[];
extern const uint8_t code Image_calendar[];
extern const uint8_t code Image_cdown[];
extern const uint8_t code Image_uptime[];
extern const uint8_t code Image_dino[];
extern const uint8_t code Image_plane[];

#endif
