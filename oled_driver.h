#ifndef __OLED_DRIVER_H
#define __OLED_DRIVER_H

#include "AI8051U.H"
#include "DEF.H"

/* Display RAM mirror: 8 pages x 128 columns for SSD1306 128x64 */
extern unsigned char edata OLED_DisplayBuf[8][128];

/* Color mode flag: 1 = normal, 0 = inverted */
extern unsigned char OLED_ColorMode;

void OLED_Init(void);
void OLED_SetColorMode(unsigned char m);
void OLED_Update(void);
void OLED_UpdateArea(unsigned char X, unsigned char Y, unsigned char W, unsigned char H);
void OLED_WriteCommand(unsigned char cmd);
void OLED_WriteData(unsigned char edata *d, unsigned int n);
void OLED_SetCursor(unsigned char page, unsigned char x);
void OLED_Brightness(int b);

#endif
