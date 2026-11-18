/* oled.h - OLED 128x64 base graphics layer for AI8051U (Keil C251, C89) */
#ifndef __OLED_H
#define __OLED_H

#include "AI8051U.H"
#include "DEF.H"
#include "oled_driver.h"
#include "oled_fonts.h"

/* Display geometry in pixels */
#define OLED_WIDTH                       (128)
#define OLED_HEIGHT                      (64)

/* Half width (advance) of each ASCII font size */
#define OLED_10X20_HALF                  (10)
#define OLED_8X16_HALF                   (8)
#define OLED_7X12_HALF                   (7)
#define OLED_6X8_HALF                    (6)

/* Full width of each Chinese font size */
#define OLED_8X8_FULL                    (8)
#define OLED_12X12_FULL                  (12)
#define OLED_16X16_FULL                  (16)
#define OLED_20X20_FULL                  (20)

/* Fill flag for shape drawing */
#define OLED_UNFILLED                    (0)
#define OLED_FILLED                      (1)

/* Longest formatted string accepted by OLED_Printf family */
#define MAX_STRING_LENGTH                128

/* Frame buffer lives in oled_driver.c, kept in fast on-chip RAM */
extern unsigned char edata OLED_DisplayBuf[8][128];

/* Clear the whole screen or a rectangular region */
void OLED_Clear(void);
void OLED_ClearArea(int16_t X, int16_t Y, int16_t Width, int16_t Height);

/* Invert the whole screen or a rectangular region */
void OLED_Reverse(void);
void OLED_ReverseArea(int16_t X, int16_t Y, int16_t Width, int16_t Height);

/* Draw a raw bitmap image (bitmap lives in code flash) */
void OLED_ShowImage(int16_t X, int16_t Y, uint16_t Width, uint16_t Height, const uint8_t code *Image);

/* Show one ASCII character or numbers in an ASCII font */
void OLED_ShowChar(int16_t X, int16_t Y, char Char, uint8_t FontSize);
void OLED_ShowNum(int16_t X, int16_t Y, uint32_t Number, uint8_t Length, uint8_t FontSize);
void OLED_ShowSignedNum(int16_t X, int16_t Y, int32_t Number, uint8_t Length, uint8_t FontSize);
void OLED_ShowHexNum(int16_t X, int16_t Y, uint32_t Number, uint8_t Length, uint8_t FontSize);
void OLED_ShowBinNum(int16_t X, int16_t Y, uint32_t Number, uint8_t Length, uint8_t FontSize);
void OLED_ShowFloatNum(int16_t X, int16_t Y, float Number, uint8_t IntLength, uint8_t FraLength, uint8_t FontSize);

/* Show strings, GBK Chinese text, and formatted output */
void OLED_ShowString(int16_t X, int16_t Y, char *String, uint8_t FontSize);
void OLED_ShowMixString(int16_t X, int16_t Y, char *String, uint8_t ChineseFontSize, uint8_t ASCIIFontSize);
void OLED_ShowChinese(int16_t X, int16_t Y, char *Chinese, uint8_t FontSize);
void OLED_Printf(int16_t X, int16_t Y, uint8_t FontSize, char *format, ...);
void OLED_PrintfMix(int16_t X, int16_t Y, uint8_t ChineseFontSize, uint8_t ASCIIFontSize, const char *format, ...);

/* Clipped drawing inside a rectangular window, used by the UI layer */
void OLED_ShowImageArea(int16_t X_Pic, int16_t Y_Pic, int16_t PictureWidth, int16_t PictureHeight, int16_t X_Area, int16_t Y_Area, int16_t AreaWidth, int16_t AreaHeight, const uint8_t code *Image);
void OLED_ShowCharArea(int16_t RangeX, int16_t RangeY, int16_t RangeWidth, int16_t RangeHeight, int16_t X, int16_t Y, char Char, uint8_t FontSize);
void OLED_ShowStringArea(int16_t RangeX, int16_t RangeY, int16_t RangeWidth, int16_t RangeHeight, int16_t X, int16_t Y, char *String, uint8_t FontSize);
void OLED_ShowChineseArea(int16_t RangeX, int16_t RangeY, int16_t RangeWidth, int16_t RangeHeight, int16_t X, int16_t Y, char *Chinese, uint8_t FontSize);
void OLED_PrintfArea(int16_t RangeX, int16_t RangeY, int16_t RangeWidth, int16_t RangeHeight, int16_t X, int16_t Y, uint8_t FontSize, char *format, ...);
void OLED_ShowMixStringArea(int16_t RangeX, int16_t RangeY, int16_t RangeWidth, int16_t RangeHeight, int16_t X, int16_t Y, char *String, uint8_t ChineseFontSize, uint8_t ASCIIFontSize);
void OLED_PrintfMixArea(int16_t RangeX, int16_t RangeY, int16_t RangeWidth, int16_t RangeHeight, int16_t X, int16_t Y, uint8_t ChineseFontSize, uint8_t ASCIIFontSize, char *format, ...);

/* Basic vector graphics for the UI layer */
void OLED_DrawPoint(int16_t X, int16_t Y);
uint8_t OLED_GetPoint(uint8_t X, uint8_t Y);
void OLED_DrawLine(int16_t X0, int16_t Y0, int16_t X1, int16_t Y1);
void OLED_DrawRectangle(int16_t X, int16_t Y, int16_t Width, int16_t Height, uint8_t IsFilled);
void OLED_DrawTriangle(int16_t X0, int16_t Y0, int16_t X1, int16_t Y1, int16_t X2, int16_t Y2, uint8_t IsFilled);
void OLED_DrawCircle(int16_t X, int16_t Y, int16_t Radius, uint8_t IsFilled);
void OLED_DrawEllipse(int16_t X, int16_t Y, int16_t A, int16_t B, uint8_t IsFilled);
void OLED_DrawArc(int16_t X, int16_t Y, int16_t Radius, int16_t StartAngle, int16_t EndAngle, uint8_t IsFilled);
void OLED_DrawRoundedRectangle(int16_t X, int16_t Y, int16_t Width, int16_t Height, int16_t Radius, uint8_t IsFilled);

#endif
