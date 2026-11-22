/* oled_fonts.h - OLED font tables and icon bitmaps for AI8051U (Keil C251, C89) */
#ifndef __OLED_FONTS_H
#define __OLED_FONTS_H

#include "AI8051U.H"
#include "DEF.H"

/* One GBK Chinese character occupies 2 bytes, plus string terminator */
#define OLED_CHN_CHAR_WIDTH          (2)

/* Chinese glyph cells, each holds one 2-byte GBK index plus bitmap data */
typedef struct
{
    char Index[OLED_CHN_CHAR_WIDTH + 1];    /* GBK code of the character */
    uint8_t Data[60];                       /* 20x20 bitmap, 60 bytes */
} ChineseCell20x20_t;

typedef struct
{
    char Index[OLED_CHN_CHAR_WIDTH + 1];    /* GBK code of the character */
    uint8_t Data[32];                       /* 16x16 bitmap, 32 bytes */
} ChineseCell16x16_t;

typedef struct
{
    char Index[OLED_CHN_CHAR_WIDTH + 1];    /* GBK code of the character */
    uint8_t Data[24];                       /* 12x12 bitmap, 24 bytes */
} ChineseCell12x12_t;

typedef struct
{
    char Index[OLED_CHN_CHAR_WIDTH + 1];    /* GBK code of the character */
    uint8_t Data[8];                        /* 8x8 bitmap, 8 bytes */
} ChineseCell8x8_t;

/* ASCII fonts, stored in flash (code memory) */
extern const unsigned char code OLED_F8x16[][16];
extern const unsigned char code OLED_F7x12[][14];
extern const unsigned char code OLED_F6x8[][6];

/* Chinese fonts, stored in flash (code memory) */
extern const ChineseCell20x20_t code OLED_CF20x20[];
extern const ChineseCell16x16_t code OLED_CF16x16[];
extern const ChineseCell12x12_t code OLED_CF12x12[];
extern const ChineseCell8x8_t code OLED_CF8x8[];

/* Small icons, stored in flash (code memory) */
extern const unsigned char code Arrow[];
extern const unsigned char code UnKnown[];
extern const unsigned char code Image_setings[];
extern const unsigned char code Image_more[];

/* Large 64x64 icons, stored in flash (code memory) */

/* Logo and QR code bitmaps, stored in flash (code memory) */
extern const unsigned char code OLED_UI_LOGOTEXT64[];
extern const unsigned char code OLED_UI_LOGOGithub[];
extern const unsigned char code OLED_UI_SettingsLogo[];

#endif
