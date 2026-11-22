/* oled.c - OLED 128x64 base graphics layer for AI8051U (Keil C251, C89) */
/* Ported from the STM32 SSD1306 version. All drawing goes through the   */
/* frame buffer OLED_DisplayBuf defined in oled_driver.c. Call the      */
/* driver flush routine from oled_driver.c to push the buffer to panel. */
#include "oled.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>

/* Forward declarations of file-local helpers (defined below) */
uint32_t OLED_Pow(uint32_t X, uint32_t Y);
uint8_t OLED_pnpoly(uint8_t nvert, int16_t *vertx, int16_t *verty, int16_t testx, int16_t testy);
int16_t OLED_Max4(int16_t a, int16_t b, int16_t c, int16_t d);
float numabs(float num);
uint8_t OLED_IsInAngle(int16_t X, int16_t Y, int16_t StartAngle, int16_t EndAngle);

/* Calculate X raised to the power Y with 32-bit integer multiply.      */
/* The MDU32 unit accelerates the 32-bit multiply automatically.        */
uint32_t OLED_Pow(uint32_t X, uint32_t Y)
{
    uint32_t Result = 1;
    while (Y--)
    {
        Result *= X;
    }
    return Result;
}

/* Point-in-polygon test (W. Randolph Franklin pnpoly algorithm).      */
/* Returns 1 when (testx, testy) is inside the polygon, else 0.         */
uint8_t OLED_pnpoly(uint8_t nvert, int16_t *vertx, int16_t *verty, int16_t testx, int16_t testy)
{
    int16_t i = 0;
    int16_t j = 0;
    uint8_t c = 0;
    for (i = 0, j = nvert - 1; i < nvert; j = i++)
    {
        if (((verty[i] > testy) != (verty[j] > testy)) &&
            (testx < (vertx[j] - vertx[i]) * (testy - verty[i]) / (verty[j] - verty[i]) + vertx[i]))
        {
            c = !c;
        }
    }
    return c;
}

/* Maximum of four signed values. Named OLED_Max4 so it never clashes  */
/* with the two-argument min/max macros from DEF.H.                    */
int16_t OLED_Max4(int16_t a, int16_t b, int16_t c, int16_t d)
{
    int16_t max_val = a;
    if (b > max_val)
    {
        max_val = b;
    }
    if (c > max_val)
    {
        max_val = c;
    }
    if (d > max_val)
    {
        max_val = d;
    }
    return max_val;
}

/* Absolute value of a single-precision float. TFPU handles float math. */
float numabs(float num)
{
    if (num > 0.0f)
    {
        return num;
    }
    if (num < 0.0f)
    {
        return -num;
    }
    return 0.0f;
}

/* Test whether vector (X, Y) points inside [StartAngle, EndAngle].      */
/* Angles are in degrees, -180 to 180, 0 points to the right.           */
uint8_t OLED_IsInAngle(int16_t X, int16_t Y, int16_t StartAngle, int16_t EndAngle)
{
    int16_t PointAngle;
    /* atan2 is double precision in Keil C251; TFPU MATH lib accelerates it */
    PointAngle = (int16_t)((float)atan2((double)Y, (double)X) / 3.1415926f * 180.0f);
    if (StartAngle < EndAngle)
    {
        if (PointAngle >= StartAngle && PointAngle <= EndAngle)
        {
            return 1;
        }
    }
    else
    {
        if (PointAngle >= StartAngle || PointAngle <= EndAngle)
        {
            return 1;
        }
    }
    return 0;
}

/* Clear the whole frame buffer (all pixels off). */
void OLED_Clear(void)
{
    int16_t i;
    int16_t j;
    for (j = 0; j < OLED_HEIGHT / 8; j++)
    {
        for (i = 0; i < OLED_WIDTH; i++)
        {
            OLED_DisplayBuf[j][i] = 0x00;
        }
    }
}

/* Clear a rectangular region: X, Y is the top-left corner, Width and   */
/* Height are the region size. Parts outside the screen are clipped.    */
void OLED_ClearArea(int16_t X, int16_t Y, int16_t Width, int16_t Height)
{
    int16_t x_start;
    int16_t y_start;
    int16_t x_end;
    int16_t y_end;
    int16_t i;
    int16_t j;

    if (Width <= 0 || Height <= 0)
    {
        return;
    }

    x_start = (X < 0) ? 0 : X;
    x_end = X + Width;
    if (x_end > OLED_WIDTH)
    {
        x_end = OLED_WIDTH;
    }
    if (x_start >= x_end)
    {
        return;
    }

    y_start = (Y < 0) ? 0 : Y;
    y_end = Y + Height;
    if (y_end > OLED_HEIGHT)
    {
        y_end = OLED_HEIGHT;
    }
    if (y_start >= y_end)
    {
        return;
    }

    Width = x_end - x_start;
    Height = y_end - y_start;

    for (j = y_start; j < y_end; j++)
    {
        for (i = x_start; i < x_end; i++)
        {
            OLED_DisplayBuf[j / 8][i] &= (uint8_t)(~(0x01 << (j % 8)));
        }
    }
}

/* Invert every pixel of the whole screen. */
void OLED_Reverse(void)
{
    uint8_t i;
    uint8_t j;
    for (j = 0; j < OLED_HEIGHT / 8; j++)
    {
        for (i = 0; i < OLED_WIDTH; i++)
        {
            OLED_DisplayBuf[j][i] ^= 0xFF;
        }
    }
}

/* Invert every pixel inside a rectangular region, clipped to screen. */
void OLED_ReverseArea(int16_t X, int16_t Y, int16_t Width, int16_t Height)
{
    int16_t i;
    int16_t j;
    int16_t x;
    int16_t y;

    if (Width <= 0 || Height <= 0)
    {
        return;
    }
    if (X > OLED_WIDTH - 1)
    {
        return;
    }
    if (Y > OLED_HEIGHT - 1)
    {
        return;
    }
    if (X + Width > OLED_WIDTH)
    {
        Width = OLED_WIDTH - X;
    }
    if (Y + Height > OLED_HEIGHT)
    {
        Height = OLED_HEIGHT - Y;
    }
    if (X + Width < 0)
    {
        return;
    }
    if (Y + Height < 0)
    {
        return;
    }
    if (X < 0)
    {
        x = 0;
    }
    else
    {
        x = X;
    }
    if (Y < 0)
    {
        y = 0;
    }
    else
    {
        y = Y;
    }

    for (j = y; j < Y + Height; j++)
    {
        for (i = x; i < X + Width; i++)
        {
            OLED_DisplayBuf[j / 8][i] ^= (uint8_t)(0x01 << (j % 8));
        }
    }
}
/* Draw a Width x Height bitmap with top-left corner at (X, Y).        */
/* The bitmap is stored page by page, 8 vertical pixels per byte. Off-  */
/* screen parts are clipped instead of drawn.                           */
void OLED_ShowImage(int16_t X, int16_t Y, uint16_t Width, uint16_t Height, const uint8_t code *Image)
{
    uint8_t i;
    uint8_t j;
    uint8_t startX;
    uint8_t startY;
    uint8_t endX;
    uint8_t endY;
    int16_t currX;
    int16_t currY;
    int32_t edge;

    if (Width == 0 || Height == 0)
    {
        return;
    }
    if (X > OLED_WIDTH - 1)
    {
        return;
    }
    if (Y > OLED_HEIGHT - 1)
    {
        return;
    }

    startX = (X < 0) ? 0 : (uint8_t)X;
    startY = (Y < 0) ? 0 : (uint8_t)Y;
    /* Widen to 32 bits: int is 16-bit on C251 and X may be negative */
    edge = (int32_t)X + (int32_t)Width - 1;
    endX = (edge > OLED_WIDTH - 1) ? OLED_WIDTH - 1 : (uint8_t)edge;
    edge = (int32_t)Y + (int32_t)Height - 1;
    endY = (edge > OLED_HEIGHT - 1) ? OLED_HEIGHT - 1 : (uint8_t)edge;

    OLED_ClearArea(startX, startY, endX - startX + 1, endY - startY + 1);

    for (j = 0; j < (Height - 1) / 8 + 1; j++)
    {
        for (i = 0; i < Width; i++)
        {
            currX = X + i;
            currY = Y + j * 8;

            if (currX < 0 || currX > OLED_WIDTH - 1 || currY < 0 || currY > OLED_HEIGHT - 1)
            {
                continue;
            }
            OLED_DisplayBuf[currY / 8][currX] |= (uint8_t)(Image[j * Width + i] << (currY % 8));
            if (currY + 8 <= OLED_HEIGHT - 1)
            {
                OLED_DisplayBuf[currY / 8 + 1][currX] |= (uint8_t)(Image[j * Width + i] >> (8 - currY % 8));
            }
        }
    }
    if (Y < 0)
    {
        for (i = 0; i < Width; i++)
        {
            currX = X + i;
            if (currX < 0 || currX > OLED_WIDTH - 1)
            {
                continue;
            }
            OLED_DisplayBuf[0][currX] |= (uint8_t)(Image[-Y / 8 * Width + i] >> (-Y % 8));
        }
    }
}

/* Show one ASCII character in the given font size. FontSize is one of  */
/* OLED_6X8_HALF, OLED_7X12_HALF, OLED_8X16_HALF, OLED_10X20_HALF.      */
void OLED_ShowChar(int16_t X, int16_t Y, char Char, uint8_t FontSize)
{
    if (FontSize == OLED_8X16_HALF)
    {
        OLED_ShowImage(X, Y, 8, 16, OLED_F8x16[Char - ' ']);
    }
    else if (FontSize == OLED_6X8_HALF)
    {
        OLED_ShowImage(X, Y, 6, 8, OLED_F6x8[Char - ' ']);
    }
    else if (FontSize == OLED_7X12_HALF)
    {
        OLED_ShowImage(X, Y, 7, 12, OLED_F7x12[Char - ' ']);
    }
}

/* Show an unsigned decimal number, padded with leading zeros to Length. */
void OLED_ShowNum(int16_t X, int16_t Y, uint32_t Number, uint8_t Length, uint8_t FontSize)
{
    uint8_t i;
    for (i = 0; i < Length; i++)
    {
        OLED_ShowChar(X + i * FontSize, Y, (char)(Number / OLED_Pow(10, Length - i - 1) % 10 + '0'), FontSize);
    }
}

/* Show a signed decimal number with a leading '+' or '-' sign. */
void OLED_ShowSignedNum(int16_t X, int16_t Y, int32_t Number, uint8_t Length, uint8_t FontSize)
{
    uint8_t i;
    uint32_t Number1;

    if (Number >= 0)
    {
        OLED_ShowChar(X, Y, '+', FontSize);
        Number1 = (uint32_t)Number;
    }
    else
    {
        OLED_ShowChar(X, Y, '-', FontSize);
        Number1 = (uint32_t)(-Number);
    }

    for (i = 0; i < Length; i++)
    {
        OLED_ShowChar(X + (i + 1) * FontSize, Y, (char)(Number1 / OLED_Pow(10, Length - i - 1) % 10 + '0'), FontSize);
    }
}

/* Show an unsigned number in hexadecimal, padded to Length digits. */
void OLED_ShowHexNum(int16_t X, int16_t Y, uint32_t Number, uint8_t Length, uint8_t FontSize)
{
    uint8_t i;
    uint8_t SingleNumber;
    for (i = 0; i < Length; i++)
    {
        SingleNumber = (uint8_t)(Number / OLED_Pow(16, Length - i - 1) % 16);
        if (SingleNumber < 10)
        {
            OLED_ShowChar(X + i * FontSize, Y, (char)(SingleNumber + '0'), FontSize);
        }
        else
        {
            OLED_ShowChar(X + i * FontSize, Y, (char)(SingleNumber - 10 + 'A'), FontSize);
        }
    }
}

/* Show an unsigned number in binary, padded to Length digits. */
void OLED_ShowBinNum(int16_t X, int16_t Y, uint32_t Number, uint8_t Length, uint8_t FontSize)
{
    uint8_t i;
    for (i = 0; i < Length; i++)
    {
        OLED_ShowChar(X + i * FontSize, Y, (char)(Number / OLED_Pow(2, Length - i - 1) % 2 + '0'), FontSize);
    }
}

/* Show a float with sign, IntLength integer digits and FraLength        */
/* fractional digits. Single-precision only so TFPU can accelerate it.   */
void OLED_ShowFloatNum(int16_t X, int16_t Y, float Number, uint8_t IntLength, uint8_t FraLength, uint8_t FontSize)
{
    uint32_t PowNum;
    uint32_t IntNum;
    uint32_t FraNum;

    if (Number >= 0.0f)
    {
        OLED_ShowChar(X, Y, '+', FontSize);
    }
    else
    {
        OLED_ShowChar(X, Y, '-', FontSize);
        Number = -Number;
    }

    IntNum = (uint32_t)Number;
    Number -= (float)IntNum;
    PowNum = OLED_Pow(10, FraLength);
    /* Manual half-up adjustment avoids pulling in the float lib */
    FraNum = (uint32_t)(Number * (float)PowNum + 0.5f);
    IntNum += FraNum / PowNum;

    OLED_ShowNum(X + FontSize, Y, IntNum, IntLength, FontSize);

    OLED_ShowChar(X + (IntLength + 1) * FontSize, Y, '.', FontSize);

    OLED_ShowNum(X + (IntLength + 2) * FontSize, Y, FraNum, FraLength, FontSize);
}

/* Show a plain ASCII string starting at (X, Y). */
void OLED_ShowString(int16_t X, int16_t Y, char *String, uint8_t FontSize)
{
    uint8_t i;
    for (i = 0; String[i] != '\0'; i++)
    {
        OLED_ShowChar(X + i * FontSize, Y, String[i], FontSize);
    }
}
/* Show GBK Chinese text. Each character takes 2 bytes and is looked  */
/* up in the font table matching FontSize (8, 12, 16 or 20 pixels).   */
void OLED_ShowChinese(int16_t X, int16_t Y, char *Chinese, uint8_t FontSize)
{
    uint8_t pChinese = 0;
    uint8_t pIndex;
    uint8_t i;
    const void *fontArray = (const void *)0;
    char SingleChinese[OLED_CHN_CHAR_WIDTH + 1] = {0};

    for (i = 0; Chinese[i] != '\0'; i++)
    {
        SingleChinese[pChinese] = Chinese[i];
        pChinese++;

        if (pChinese >= OLED_CHN_CHAR_WIDTH)
        {
            pChinese = 0;

            if (FontSize == OLED_8X8_FULL)
            {
                fontArray = (const void *)OLED_CF8x8;
            }
            else if (FontSize == OLED_12X12_FULL)
            {
                fontArray = (const void *)OLED_CF12x12;
            }
            else if (FontSize == OLED_16X16_FULL)
            {
                fontArray = (const void *)OLED_CF16x16;
            }
            else if (FontSize == OLED_20X20_FULL)
            {
                fontArray = (const void *)OLED_CF20x20;
            }

            if (FontSize == OLED_8X8_FULL)
            {
                for (pIndex = 0; strcmp(((const ChineseCell8x8_t *)fontArray)[pIndex].Index, "") != 0; pIndex++)
                {
                    if (strcmp(((const ChineseCell8x8_t *)fontArray)[pIndex].Index, SingleChinese) == 0)
                    {
                        break;
                    }
                }
                OLED_ShowImage(X + ((i + 1) / OLED_CHN_CHAR_WIDTH - 1) * OLED_8X8_FULL, Y, OLED_8X8_FULL, OLED_8X8_FULL, (const uint8_t code *)(((const ChineseCell8x8_t *)fontArray)[pIndex].Data));
            }
            else if (FontSize == OLED_12X12_FULL)
            {
                for (pIndex = 0; strcmp(((const ChineseCell12x12_t *)fontArray)[pIndex].Index, "") != 0; pIndex++)
                {
                    if (strcmp(((const ChineseCell12x12_t *)fontArray)[pIndex].Index, SingleChinese) == 0)
                    {
                        break;
                    }
                }
                OLED_ShowImage(X + ((i + 1) / OLED_CHN_CHAR_WIDTH - 1) * OLED_12X12_FULL, Y, OLED_12X12_FULL, OLED_12X12_FULL, (const uint8_t code *)(((const ChineseCell12x12_t *)fontArray)[pIndex].Data));
            }
            else if (FontSize == OLED_16X16_FULL)
            {
                for (pIndex = 0; strcmp(((const ChineseCell16x16_t *)fontArray)[pIndex].Index, "") != 0; pIndex++)
                {
                    if (strcmp(((const ChineseCell16x16_t *)fontArray)[pIndex].Index, SingleChinese) == 0)
                    {
                        break;
                    }
                }
                OLED_ShowImage(X + ((i + 1) / OLED_CHN_CHAR_WIDTH - 1) * OLED_16X16_FULL, Y, OLED_16X16_FULL, OLED_16X16_FULL, (const uint8_t code *)(((const ChineseCell16x16_t *)fontArray)[pIndex].Data));
            }
            else if (FontSize == OLED_20X20_FULL)
            {
                for (pIndex = 0; strcmp(((const ChineseCell20x20_t *)fontArray)[pIndex].Index, "") != 0; pIndex++)
                {
                    if (strcmp(((const ChineseCell20x20_t *)fontArray)[pIndex].Index, SingleChinese) == 0)
                    {
                        break;
                    }
                }
                OLED_ShowImage(X + ((i + 1) / OLED_CHN_CHAR_WIDTH - 1) * OLED_20X20_FULL, Y, OLED_20X20_FULL, OLED_20X20_FULL, (const uint8_t code *)(((const ChineseCell20x20_t *)fontArray)[pIndex].Data));
            }
        }
    }
}

/* Printf-style output of a plain ASCII string. */
void OLED_Printf(int16_t X, int16_t Y, uint8_t FontSize, char *format, ...)
{
    char String[MAX_STRING_LENGTH];
    va_list arg;
    va_start(arg, format);
    vsprintf(String, format, arg);
    va_end(arg);
    OLED_ShowString(X, Y, String, FontSize);
}

/* Show a mixed string: bytes with the top bit set are treated as one  */
/* 2-byte GBK Chinese character, the rest as ASCII characters.         */
void OLED_ShowMixString(int16_t X, int16_t Y, char *String, uint8_t ChineseFontSize, uint8_t ASCIIFontSize)
{
    uint8_t i;
    char Chinese[OLED_CHN_CHAR_WIDTH + 1];
    while (*String != '\0')
    {
        if (*String & 0x80)
        {
            for (i = 0; i < OLED_CHN_CHAR_WIDTH; i++)
            {
                Chinese[i] = *(String + i);
            }
            Chinese[OLED_CHN_CHAR_WIDTH] = '\0';
            OLED_ShowChinese(X, Y, Chinese, ChineseFontSize);
            X += ChineseFontSize;
            String += OLED_CHN_CHAR_WIDTH;
        }
        else
        {
            OLED_ShowChar(X, Y, *String, ASCIIFontSize);
            X += ASCIIFontSize;
            String++;
        }
    }
}

/* Printf-style output of a mixed Chinese/ASCII string. */
void OLED_PrintfMix(int16_t X, int16_t Y, uint8_t ChineseFontSize, uint8_t ASCIIFontSize, const char *format, ...)
{
    char String[MAX_STRING_LENGTH];
    va_list arg;
    va_start(arg, format);
    vsprintf(String, format, arg);
    va_end(arg);
    OLED_ShowMixString(X, Y, String, ChineseFontSize, ASCIIFontSize);
}

/* Draw a bitmap clipped to a rectangular window. Only the overlap of  */
/* the picture rect and the window rect reaches the frame buffer. The  */
/* UI layer uses this to render inside scrollable views.               */
void OLED_ShowImageArea(int16_t X_Pic, int16_t Y_Pic, int16_t PictureWidth, int16_t PictureHeight, int16_t X_Area, int16_t Y_Area, int16_t AreaWidth, int16_t AreaHeight, const uint8_t code *Image)
{
    int16_t startX;
    int16_t endX;
    int16_t startY;
    int16_t endY;
    uint8_t j;
    uint8_t i;
    uint8_t bti;
    uint8_t currX;
    uint8_t currY;
    uint8_t page;
    uint8_t bit_pos;
    uint8_t px;

    if (PictureWidth == 0 || PictureHeight == 0 || AreaWidth == 0 || AreaHeight == 0 || X_Pic > OLED_WIDTH - 1 || X_Area > OLED_WIDTH - 1 || Y_Pic > OLED_HEIGHT - 1 || Y_Area > OLED_HEIGHT - 1)
    {
        return;
    }
    startX = (X_Pic < X_Area) ? X_Area : X_Pic;
    endX = ((X_Area + AreaWidth - 1) < (X_Pic + PictureWidth - 1)) ? (X_Area + AreaWidth - 1) : (X_Pic + PictureWidth - 1);
    startY = (Y_Pic < Y_Area) ? Y_Area : Y_Pic;
    endY = ((Y_Area + AreaHeight - 1) < (Y_Pic + PictureHeight - 1)) ? (Y_Area + AreaHeight - 1) : (Y_Pic + PictureHeight - 1);
    endX = (endX > OLED_WIDTH - 1) ? OLED_WIDTH - 1 : endX;
    endY = (endY > OLED_HEIGHT - 1) ? OLED_HEIGHT - 1 : endY;
    if (startX > endX || startY > endY)
    {
        return;
    }
    for (j = 0; j <= (PictureHeight - 1) / 8; j++)
    {
        for (i = 0; i < PictureWidth; i++)
        {
            currX = (uint8_t)(X_Pic + i);
            if (currX < startX || currX > endX)
            {
                continue;
            }
            for (bti = 0; bti < 8; bti++)
            {
                currY = (uint8_t)(Y_Pic + j * 8 + bti);
                if (currY < startY || currY > endY)
                {
                    continue;
                }
                page = (uint8_t)(currY / 8);
                bit_pos = (uint8_t)(currY % 8);
                px = Image[j * PictureWidth + i];
                if (px & (1 << bti))
                {
                    OLED_DisplayBuf[page][currX] |= (uint8_t)(1 << bit_pos);
                }
            }
        }
    }
}

/* Show one ASCII character clipped to a rectangular window. */
void OLED_ShowCharArea(int16_t RangeX, int16_t RangeY, int16_t RangeWidth, int16_t RangeHeight, int16_t X, int16_t Y, char Char, uint8_t FontSize)
{
    if (FontSize == OLED_8X16_HALF)
    {
        OLED_ShowImageArea(X, Y, 8, 16, RangeX, RangeY, RangeWidth, RangeHeight, OLED_F8x16[Char - ' ']);
    }
    else if (FontSize == OLED_6X8_HALF)
    {
        OLED_ShowImageArea(X, Y, 6, 8, RangeX, RangeY, RangeWidth, RangeHeight, OLED_F6x8[Char - ' ']);
    }
    else if (FontSize == OLED_7X12_HALF)
    {
        OLED_ShowImageArea(X, Y, 7, 12, RangeX, RangeY, RangeWidth, RangeHeight, OLED_F7x12[Char - ' ']);
    }
}

/* Show a plain ASCII string clipped to a rectangular window. */
void OLED_ShowStringArea(int16_t RangeX, int16_t RangeY, int16_t RangeWidth, int16_t RangeHeight, int16_t X, int16_t Y, char *String, uint8_t FontSize)
{
    uint16_t i;
    for (i = 0; String[i] != '\0'; i++)
    {
        OLED_ShowCharArea(RangeX, RangeY, RangeWidth, RangeHeight, X + i * FontSize, Y, String[i], FontSize);
    }
}
/* Show GBK Chinese text clipped to a rectangular window. */
void OLED_ShowChineseArea(int16_t RangeX, int16_t RangeY, int16_t RangeWidth, int16_t RangeHeight, int16_t X, int16_t Y, char *Chinese, uint8_t FontSize)
{
    uint8_t pChinese = 0;
    uint8_t pIndex;
    uint8_t i;
    const void *fontArray = (const void *)0;
    char SingleChinese[OLED_CHN_CHAR_WIDTH + 1] = {0};
    for (i = 0; Chinese[i] != '\0'; i++)
    {
        SingleChinese[pChinese] = Chinese[i];
        pChinese++;

        if (pChinese >= OLED_CHN_CHAR_WIDTH)
        {
            pChinese = 0;
            if (FontSize == OLED_8X8_FULL)
            {
                fontArray = (const void *)OLED_CF8x8;
            }
            else if (FontSize == OLED_12X12_FULL)
            {
                fontArray = (const void *)OLED_CF12x12;
            }
            else if (FontSize == OLED_16X16_FULL)
            {
                fontArray = (const void *)OLED_CF16x16;
            }
            else if (FontSize == OLED_20X20_FULL)
            {
                fontArray = (const void *)OLED_CF20x20;
            }

            if (FontSize == OLED_8X8_FULL)
            {
                for (pIndex = 0; strcmp(((const ChineseCell8x8_t *)fontArray)[pIndex].Index, "") != 0; pIndex++)
                {
                    if (strcmp(((const ChineseCell8x8_t *)fontArray)[pIndex].Index, SingleChinese) == 0)
                    {
                        break;
                    }
                }
                OLED_ShowImageArea(X + ((i + 1) / OLED_CHN_CHAR_WIDTH - 1) * OLED_8X8_FULL, Y, OLED_8X8_FULL, OLED_8X8_FULL, RangeX, RangeY, RangeWidth, RangeHeight, (const uint8_t code *)(((const ChineseCell8x8_t *)fontArray)[pIndex].Data));
            }
            else if (FontSize == OLED_12X12_FULL)
            {
                for (pIndex = 0; strcmp(((const ChineseCell12x12_t *)fontArray)[pIndex].Index, "") != 0; pIndex++)
                {
                    if (strcmp(((const ChineseCell12x12_t *)fontArray)[pIndex].Index, SingleChinese) == 0)
                    {
                        break;
                    }
                }
                OLED_ShowImageArea(X + ((i + 1) / OLED_CHN_CHAR_WIDTH - 1) * OLED_12X12_FULL, Y, OLED_12X12_FULL, OLED_12X12_FULL, RangeX, RangeY, RangeWidth, RangeHeight, (const uint8_t code *)(((const ChineseCell12x12_t *)fontArray)[pIndex].Data));
            }
            else if (FontSize == OLED_16X16_FULL)
            {
                for (pIndex = 0; strcmp(((const ChineseCell16x16_t *)fontArray)[pIndex].Index, "") != 0; pIndex++)
                {
                    if (strcmp(((const ChineseCell16x16_t *)fontArray)[pIndex].Index, SingleChinese) == 0)
                    {
                        break;
                    }
                }
                OLED_ShowImageArea(X + ((i + 1) / OLED_CHN_CHAR_WIDTH - 1) * OLED_16X16_FULL, Y, OLED_16X16_FULL, OLED_16X16_FULL, RangeX, RangeY, RangeWidth, RangeHeight, (const uint8_t code *)(((const ChineseCell16x16_t *)fontArray)[pIndex].Data));
            }
            else if (FontSize == OLED_20X20_FULL)
            {
                for (pIndex = 0; strcmp(((const ChineseCell20x20_t *)fontArray)[pIndex].Index, "") != 0; pIndex++)
                {
                    if (strcmp(((const ChineseCell20x20_t *)fontArray)[pIndex].Index, SingleChinese) == 0)
                    {
                        break;
                    }
                }
                OLED_ShowImageArea(X + ((i + 1) / OLED_CHN_CHAR_WIDTH - 1) * OLED_20X20_FULL, Y, OLED_20X20_FULL, OLED_20X20_FULL, RangeX, RangeY, RangeWidth, RangeHeight, (const uint8_t code *)(((const ChineseCell20x20_t *)fontArray)[pIndex].Data));
            }
        }
    }
}

/* Printf-style ASCII output clipped to a rectangular window. */
void OLED_PrintfArea(int16_t RangeX, int16_t RangeY, int16_t RangeWidth, int16_t RangeHeight, int16_t X, int16_t Y, uint8_t FontSize, char *format, ...)
{
    char String[MAX_STRING_LENGTH];
    va_list arg;
    va_start(arg, format);
    vsprintf(String, format, arg);
    va_end(arg);
    OLED_ShowStringArea(RangeX, RangeY, RangeWidth, RangeHeight, X, Y, String, FontSize);
}

/* Show a mixed Chinese/ASCII string clipped to a rectangular window. */
void OLED_ShowMixStringArea(int16_t RangeX, int16_t RangeY, int16_t RangeWidth, int16_t RangeHeight, int16_t X, int16_t Y, char *String, uint8_t ChineseFontSize, uint8_t ASCIIFontSize)
{
    uint8_t i;
    char Chinese[OLED_CHN_CHAR_WIDTH + 1];
    while (*String != '\0')
    {
        if (*String & 0x80)
        {
            for (i = 0; i < OLED_CHN_CHAR_WIDTH; i++)
            {
                Chinese[i] = *(String + i);
            }
            Chinese[OLED_CHN_CHAR_WIDTH] = '\0';
            OLED_ShowChineseArea(RangeX, RangeY, RangeWidth, RangeHeight, X, Y, Chinese, ChineseFontSize);
            X += ChineseFontSize;
            String += OLED_CHN_CHAR_WIDTH;
        }
        else
        {
            OLED_ShowCharArea(RangeX, RangeY, RangeWidth, RangeHeight, X, Y, *String, ASCIIFontSize);
            X += ASCIIFontSize;
            String++;
        }
    }
}

/* Printf-style mixed Chinese/ASCII output clipped to a window. */
void OLED_PrintfMixArea(int16_t RangeX, int16_t RangeY, int16_t RangeWidth, int16_t RangeHeight, int16_t X, int16_t Y, uint8_t ChineseFontSize, uint8_t ASCIIFontSize, char *format, ...)
{
    char String[MAX_STRING_LENGTH];
    va_list arg;
    va_start(arg, format);
    vsprintf(String, format, arg);
    va_end(arg);
    OLED_ShowMixStringArea(RangeX, RangeY, RangeWidth, RangeHeight, X, Y, String, ChineseFontSize, ASCIIFontSize);
}

/* Light one pixel at (X, Y). Pixels outside the screen are ignored. */
void OLED_DrawPoint(int16_t X, int16_t Y)
{
    if (X < 0 || Y < 0 || X > OLED_WIDTH - 1 || Y > OLED_HEIGHT - 1)
    {
        return;
    }
    OLED_DisplayBuf[Y / 8][X] |= (uint8_t)(0x01 << (Y % 8));
}

/* Read back one pixel: 1 when lit, 0 when dark or out of range. */
uint8_t OLED_GetPoint(uint8_t X, uint8_t Y)
{
    if (X > OLED_WIDTH - 1)
    {
        return 0;
    }
    if (Y > OLED_HEIGHT - 1)
    {
        return 0;
    }
    if (OLED_DisplayBuf[Y / 8][X] & (0x01 << (Y % 8)))
    {
        return 1;
    }
    return 0;
}

/* Draw a straight line with the Bresenham algorithm. */
void OLED_DrawLine(int16_t X0, int16_t Y0, int16_t X1, int16_t Y1)
{
    int16_t x;
    int16_t y;
    int16_t dx;
    int16_t dy;
    int16_t d;
    int16_t incrE;
    int16_t incrNE;
    int16_t temp;
    int16_t x0 = X0;
    int16_t y0 = Y0;
    int16_t x1 = X1;
    int16_t y1 = Y1;
    uint8_t yflag = 0;
    uint8_t xyflag = 0;

    if (y0 == y1)
    {
        if (x0 > x1)
        {
            temp = x0;
            x0 = x1;
            x1 = temp;
        }
        for (x = x0; x <= x1; x++)
        {
            OLED_DrawPoint(x, y0);
        }
    }
    else if (x0 == x1)
    {
        if (y0 > y1)
        {
            temp = y0;
            y0 = y1;
            y1 = temp;
        }
        for (y = y0; y <= y1; y++)
        {
            OLED_DrawPoint(x0, y);
        }
    }
    else
    {
        if (x0 > x1)
        {
            temp = x0;
            x0 = x1;
            x1 = temp;
            temp = y0;
            y0 = y1;
            y1 = temp;
        }
        if (y0 > y1)
        {
            y0 = -y0;
            y1 = -y1;
            yflag = 1;
        }
        if (y1 - y0 > x1 - x0)
        {
            temp = x0;
            x0 = y0;
            y0 = temp;
            temp = x1;
            x1 = y1;
            y1 = temp;
            xyflag = 1;
        }
        dx = x1 - x0;
        dy = y1 - y0;
        incrE = 2 * dy;
        incrNE = 2 * (dy - dx);
        d = 2 * dy - dx;
        x = x0;
        y = y0;

        if (yflag && xyflag)
        {
            OLED_DrawPoint(y, -x);
        }
        else if (yflag)
        {
            OLED_DrawPoint(x, -y);
        }
        else if (xyflag)
        {
            OLED_DrawPoint(y, x);
        }
        else
        {
            OLED_DrawPoint(x, y);
        }

        while (x < x1)
        {
            x++;
            if (d < 0)
            {
                d += incrE;
            }
            else
            {
                y++;
                d += incrNE;
            }
            if (yflag && xyflag)
            {
                OLED_DrawPoint(y, -x);
            }
            else if (yflag)
            {
                OLED_DrawPoint(x, -y);
            }
            else if (xyflag)
            {
                OLED_DrawPoint(y, x);
            }
            else
            {
                OLED_DrawPoint(x, y);
            }
        }
    }
}
/* Draw a rectangle. The border is drawn when IsFilled is            */
/* OLED_UNFILLED, the whole area is filled when OLED_FILLED.          */
void OLED_DrawRectangle(int16_t X, int16_t Y, int16_t Width, int16_t Height, uint8_t IsFilled)
{
    int16_t X_start;
    int16_t X_end;
    int16_t Y_start;
    int16_t Y_end;
    int16_t validWidth;
    int16_t validHeight;
    int16_t start_page;
    int16_t end_page;
    int16_t x;
    int16_t page;
    int16_t i;
    uint8_t start_mask;
    uint8_t end_mask;
    uint8_t mask;

    if (Width == 0 || Height == 0)
    {
        return;
    }

    X_start = X;
    X_end = X + Width - 1;
    Y_start = Y;
    Y_end = Y + Height - 1;

    if (X_start < 0)
    {
        X_start = 0;
    }
    if (X_end >= OLED_WIDTH)
    {
        X_end = OLED_WIDTH - 1;
    }
    if (Y_start < 0)
    {
        Y_start = 0;
    }
    if (Y_end >= OLED_HEIGHT)
    {
        Y_end = OLED_HEIGHT - 1;
    }

    validWidth = X_end - X_start + 1;
    validHeight = Y_end - Y_start + 1;
    if (validWidth <= 0 || validHeight <= 0)
    {
        return;
    }

    start_page = Y_start / 8;
    end_page = Y_end / 8;

    start_mask = (uint8_t)(0xFF << (Y_start % 8));
    end_mask = (uint8_t)(0xFF >> (7 - (Y_end % 8)));

    if (IsFilled)
    {
        for (x = X_start; x <= X_end; x++)
        {
            for (page = start_page; page <= end_page; page++)
            {
                mask = 0xFF;
                if (page == start_page)
                {
                    mask &= start_mask;
                }
                if (page == end_page)
                {
                    mask &= end_mask;
                }
                if (page >= 0 && page < OLED_HEIGHT / 8)
                {
                    OLED_DisplayBuf[page][x] |= mask;
                }
            }
        }
    }
    else
    {
        for (i = X; i <= X + Width - 1; i++)
        {
            OLED_DrawPoint(i, Y);
            OLED_DrawPoint(i, Y + Height - 1);
        }
        for (i = Y; i <= Y + Height - 1; i++)
        {
            OLED_DrawPoint(X, i);
            OLED_DrawPoint(X + Width - 1, i);
        }
    }
}

/* Draw a triangle. Only the three edges are drawn when IsFilled is   */
/* OLED_UNFILLED, the interior is scanned and filled when OLED_FILLED. */
void OLED_DrawTriangle(int16_t X0, int16_t Y0, int16_t X1, int16_t Y1, int16_t X2, int16_t Y2, uint8_t IsFilled)
{
    int16_t minx = X0;
    int16_t miny = Y0;
    int16_t maxx = X0;
    int16_t maxy = Y0;
    int16_t i;
    int16_t j;
    int16_t vx[3];
    int16_t vy[3];

    /* Plain assignments so the code stays strict C89 */
    vx[0] = X0;
    vx[1] = X1;
    vx[2] = X2;
    vy[0] = Y0;
    vy[1] = Y1;
    vy[2] = Y2;

    if (!IsFilled)
    {
        OLED_DrawLine(X0, Y0, X1, Y1);
        OLED_DrawLine(X0, Y0, X2, Y2);
        OLED_DrawLine(X1, Y1, X2, Y2);
    }
    else
    {
        if (X1 < minx)
        {
            minx = X1;
        }
        if (X2 < minx)
        {
            minx = X2;
        }
        if (Y1 < miny)
        {
            miny = Y1;
        }
        if (Y2 < miny)
        {
            miny = Y2;
        }
        if (X1 > maxx)
        {
            maxx = X1;
        }
        if (X2 > maxx)
        {
            maxx = X2;
        }
        if (Y1 > maxy)
        {
            maxy = Y1;
        }
        if (Y2 > maxy)
        {
            maxy = Y2;
        }
        for (i = minx; i <= maxx; i++)
        {
            for (j = miny; j <= maxy; j++)
            {
                if (OLED_pnpoly(3, vx, vy, i, j))
                {
                    OLED_DrawPoint(i, j);
                }
            }
        }
    }
}

/* Draw a circle with the Bresenham algorithm, hollow or filled. */
void OLED_DrawCircle(int16_t X, int16_t Y, int16_t Radius, uint8_t IsFilled)
{
    int16_t x;
    int16_t y;
    int16_t d;
    int16_t j;

    d = 1 - Radius;
    x = 0;
    y = Radius;

    OLED_DrawPoint(X + x, Y + y);
    OLED_DrawPoint(X - x, Y - y);
    OLED_DrawPoint(X + y, Y + x);
    OLED_DrawPoint(X - y, Y - x);

    if (IsFilled)
    {
        for (j = -y; j < y; j++)
        {
            OLED_DrawPoint(X, Y + j);
        }
    }

    while (x < y)
    {
        x++;
        if (d < 0)
        {
            d += 2 * x + 1;
        }
        else
        {
            y--;
            d += 2 * (x - y) + 1;
        }

        OLED_DrawPoint(X + x, Y + y);
        OLED_DrawPoint(X + y, Y + x);
        OLED_DrawPoint(X - x, Y - y);
        OLED_DrawPoint(X - y, Y - x);
        OLED_DrawPoint(X + x, Y - y);
        OLED_DrawPoint(X + y, Y - x);
        OLED_DrawPoint(X - x, Y + y);
        OLED_DrawPoint(X - y, Y + x);

        if (IsFilled)
        {
            for (j = -y; j < y; j++)
            {
                OLED_DrawPoint(X + x, Y + j);
                OLED_DrawPoint(X - x, Y + j);
            }
            for (j = -x; j < x; j++)
            {
                OLED_DrawPoint(X - y, Y + j);
                OLED_DrawPoint(X + y, Y + j);
            }
        }
    }
}

/* Draw an ellipse with semi-axes A (horizontal) and B (vertical). */
void OLED_DrawEllipse(int16_t X, int16_t Y, int16_t A, int16_t B, uint8_t IsFilled)
{
    int16_t x;
    int16_t y;
    int16_t j;
    int16_t a = A;
    int16_t b = B;
    float d1;
    float d2;

    x = 0;
    y = b;
    /* int is only 16 bits on C251, so widen the squares to 32 bits    */
    /* before converting to float (MDU32 accelerates 32-bit multiply)  */
    d1 = (float)((int32_t)b * b) + (float)a * (float)a * (0.5f - (float)b);

    if (IsFilled)
    {
        for (j = -y; j < y; j++)
        {
            OLED_DrawPoint(X, Y + j);
            OLED_DrawPoint(X, Y + j);
        }
    }

    OLED_DrawPoint(X + x, Y + y);
    OLED_DrawPoint(X - x, Y - y);
    OLED_DrawPoint(X - x, Y + y);
    OLED_DrawPoint(X + x, Y - y);

    while ((float)((int32_t)b * b * (x + 1)) < (float)a * (float)a * ((float)y - 0.5f))
    {
        if (d1 <= 0.0f)
        {
            d1 += (float)((int32_t)b * b * (2 * x + 3));
        }
        else
        {
            d1 += (float)((int32_t)b * b * (2 * x + 3) + (int32_t)a * a * (-2 * y + 2));
            y--;
        }
        x++;

        if (IsFilled)
        {
            for (j = -y; j < y; j++)
            {
                OLED_DrawPoint(X + x, Y + j);
                OLED_DrawPoint(X - x, Y + j);
            }
        }

        OLED_DrawPoint(X + x, Y + y);
        OLED_DrawPoint(X - x, Y - y);
        OLED_DrawPoint(X - x, Y + y);
        OLED_DrawPoint(X + x, Y - y);
    }

    d2 = (float)b * (float)b * ((float)x + 0.5f) * ((float)x + 0.5f)
        + (float)a * (float)a * (float)(y - 1) * (float)(y - 1)
        - (float)a * (float)a * (float)b * (float)b;

    while (y > 0)
    {
        if (d2 <= 0.0f)
        {
            d2 += (float)((int32_t)b * b * (2 * x + 2) + (int32_t)a * a * (-2 * y + 3));
            x++;
        }
        else
        {
            d2 += (float)((int32_t)a * a * (-2 * y + 3));
        }
        y--;

        if (IsFilled)
        {
            for (j = -y; j < y; j++)
            {
                OLED_DrawPoint(X + x, Y + j);
                OLED_DrawPoint(X - x, Y + j);
            }
        }

        OLED_DrawPoint(X + x, Y + y);
        OLED_DrawPoint(X - x, Y - y);
        OLED_DrawPoint(X - x, Y + y);
        OLED_DrawPoint(X + x, Y - y);
    }
}

/* Draw an arc of the given radius between StartAngle and EndAngle.    */
/* Angles are in degrees, -180 to 180, 0 points to the right.          */
void OLED_DrawArc(int16_t X, int16_t Y, int16_t Radius, int16_t StartAngle, int16_t EndAngle, uint8_t IsFilled)
{
    int16_t x;
    int16_t y;
    int16_t d;
    int16_t j;

    if (Radius <= 0)
    {
        return;
    }

    d = 1 - Radius;
    x = 0;
    y = Radius;

    if (OLED_IsInAngle(x, y, StartAngle, EndAngle))
    {
        OLED_DrawPoint(X + x, Y + y);
    }
    if (OLED_IsInAngle(-x, -y, StartAngle, EndAngle))
    {
        OLED_DrawPoint(X - x, Y - y);
    }
    if (OLED_IsInAngle(y, x, StartAngle, EndAngle))
    {
        OLED_DrawPoint(X + y, Y + x);
    }
    if (OLED_IsInAngle(-y, -x, StartAngle, EndAngle))
    {
        OLED_DrawPoint(X - y, Y - x);
    }

    if (IsFilled)
    {
        for (j = -y; j < y; j++)
        {
            if (OLED_IsInAngle(0, j, StartAngle, EndAngle))
            {
                OLED_DrawPoint(X, Y + j);
            }
        }
    }

    while (x < y)
    {
        x++;
        if (d < 0)
        {
            d += 2 * x + 1;
        }
        else
        {
            y--;
            d += 2 * (x - y) + 1;
        }

        if (OLED_IsInAngle(x, y, StartAngle, EndAngle))
        {
            OLED_DrawPoint(X + x, Y + y);
        }
        if (OLED_IsInAngle(y, x, StartAngle, EndAngle))
        {
            OLED_DrawPoint(X + y, Y + x);
        }
        if (OLED_IsInAngle(-x, -y, StartAngle, EndAngle))
        {
            OLED_DrawPoint(X - x, Y - y);
        }
        if (OLED_IsInAngle(-y, -x, StartAngle, EndAngle))
        {
            OLED_DrawPoint(X - y, Y - x);
        }
        if (OLED_IsInAngle(x, -y, StartAngle, EndAngle))
        {
            OLED_DrawPoint(X + x, Y - y);
        }
        if (OLED_IsInAngle(y, -x, StartAngle, EndAngle))
        {
            OLED_DrawPoint(X + y, Y - x);
        }
        if (OLED_IsInAngle(-x, y, StartAngle, EndAngle))
        {
            OLED_DrawPoint(X - x, Y + y);
        }
        if (OLED_IsInAngle(-y, x, StartAngle, EndAngle))
        {
            OLED_DrawPoint(X - y, Y + x);
        }

        if (IsFilled)
        {
            for (j = -y; j < y; j++)
            {
                if (OLED_IsInAngle(x, j, StartAngle, EndAngle))
                {
                    OLED_DrawPoint(X + x, Y + j);
                }
                if (OLED_IsInAngle(-x, j, StartAngle, EndAngle))
                {
                    OLED_DrawPoint(X - x, Y + j);
                }
            }
            for (j = -x; j < x; j++)
            {
                if (OLED_IsInAngle(-y, j, StartAngle, EndAngle))
                {
                    OLED_DrawPoint(X - y, Y + j);
                }
                if (OLED_IsInAngle(y, j, StartAngle, EndAngle))
                {
                    OLED_DrawPoint(X + y, Y + j);
                }
            }
        }
    }
}

/* Draw a rectangle with rounded corners of the given Radius. */
void OLED_DrawRoundedRectangle(int16_t X, int16_t Y, int16_t Width, int16_t Height, int16_t Radius, uint8_t IsFilled)
{
    if (Width == 0 || Height == 0)
    {
        return;
    }
    if (Radius > Width / 2 || Radius > Height / 2)
    {
        Radius = (Width < Height ? Width : Height) / 2;
    }
    if (Radius <= 0)
    {
        OLED_DrawRectangle(X, Y, Width, Height, IsFilled);
        return;
    }
    OLED_DrawArc(X + Radius, Y + Radius, Radius, 180, -90, IsFilled);
    OLED_DrawArc(X + Width - Radius - 1, Y + Radius, Radius, -90, 0, IsFilled);
    OLED_DrawArc(X + Radius, Y + Height - Radius - 1, Radius, 90, 180, IsFilled);
    OLED_DrawArc(X + Width - Radius - 1, Y + Height - Radius - 1, Radius, 0, 90, IsFilled);

    if (IsFilled)
    {
        OLED_DrawRectangle(X + Radius, Y, Width - 2 * Radius, Height, OLED_FILLED);
        OLED_DrawRectangle(X, Y + Radius, Width, Height - 2 * Radius, OLED_FILLED);
    }
    else
    {
        OLED_DrawLine(X + Radius, Y, X + Width - Radius - 1, Y);
        OLED_DrawLine(X + Radius, Y + Height - 1, X + Width - Radius - 1, Y + Height - 1);
        OLED_DrawLine(X, Y + Radius, X, Y + Height - Radius - 1);
        OLED_DrawLine(X + Width - 1, Y + Radius, X + Width - 1, Y + Height - Radius - 1);
    }
}

