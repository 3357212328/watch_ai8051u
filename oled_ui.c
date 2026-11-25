#include "oled_ui.h"
#include "usb_ctrl.h"

/* OLED_UI core ported from STM32 to AI8051U (Keil C251, C89).
 * Upstream project: bdth-7777777 OLED_UI on GitHub.
 * Logic parity with the STM32 version. Float math is single
 * precision (TFPU accelerated), 32-bit mul/div is MDU accelerated.
 */

/* Debounced key states, 1 means released */
OLED_Key edata OLED_UI_Key = {1, 1, 1, 1};
OLED_Key edata OLED_UI_LastKey = {1, 1, 1, 1};
/* Currently shown page and popup window */
MenuPage * edata CurrentMenuPage = NULL;
MenuWindow * edata CurrentWindow = NULL;
/* Enter mutex: FLAGSTART means a callback is pending */
MutexFlag edata KeyEnterFlag = FLAGEND;
/* Fade mutex: FLAGSTART means a page transition runs */
MutexFlag edata FadeOutFlag = FLAGEND;
/* Window mutex */
MutexFlag edata WindowFlag = FLAGEND;
/* Display mode, 1 is dark mode, 0 is light mode */
unsigned char edata ColorMode = DARKMODE;
/* Screen brightness */
int16_t edata OLED_UI_Brightness = 100;
/* Popup window sustain timer */
OLED_UI_WindowSustainCounter edata OLED_SustainCounter = {0, 0};
/* Encoder steps collected in the 20 ms tick, applied in main loop */
int16_t edata WindowProbDeltaData = 0;
/* External app hooks (watch face integration), see oled_ui.h */
void (*OLED_UI_BackHook)(void) = NULL;
void (*OLED_UI_IdleHook)(void) = NULL;
unsigned int OLED_UI_IdleTimeout = 0;
/* Silent ticks since the last input, drives IdleHook */
static unsigned int edata OLED_UI_IdleTicks = 0;

/********************************************************************************/
/******* Animated controls bound to the current page, all in fast RAM ***********/

/* Cursor highlight */
OLED_ChangeArea xdata OLED_UI_Cursor;
/* Menu border frame */
OLED_ChangeArea xdata OLED_UI_MenuFrame;
/* Popup window frame */
OLED_ChangeArea xdata OLED_UI_Window;
/* Scrollbar fill length */
OLED_ChangeDistance xdata OLED_UI_ScrollBarHeight;
/* Window progress bar fill length */
OLED_ChangeDistance xdata OLED_UI_ProbWidth;
/* Page origin for items */
OLED_ChangePoint xdata OLED_UI_PageStartPoint;
/* Item line spacing */
OLED_ChangeDistance xdata OLED_UI_LineStep;

/*
 * @brief Absolute value of a float (stays single precision).
 */
float OLEDUI_AbsF(float v)
{
    if (v < 0.0f)
    {
        return -v;
    }
    return v;
}

/*
 * @brief Ceiling of a float returned as int16 (stays single precision).
 */
int16_t OLEDUI_CeilF(float v)
{
    int16_t t = (int16_t)v;
    if (v > (float)t)
    {
        t++;
    }
    return t;
}

/*
 * @brief Map a UI font height to a real font width.
 * @param fontsize UI font height (8/12/16/20)
 * @param style CHINESE for full width fonts, ASCII for half width fonts
 */
OLED_Font GetOLED_Font(OLED_Font fontsize, unsigned char style)
{
    OLED_Font ChineseFont;
    OLED_Font ASCIIFont;
    switch (fontsize)
    {
        case OLED_UI_FONT_8:
            ChineseFont = OLED_8X8_FULL;
            ASCIIFont = OLED_6X8_HALF;
            break;
        case OLED_UI_FONT_12:
            ChineseFont = OLED_12X12_FULL;
            ASCIIFont = OLED_7X12_HALF;
            break;
        case OLED_UI_FONT_16:
            ChineseFont = OLED_16X16_FULL;
            ASCIIFont = OLED_8X16_HALF;
            break;
        default:
            ChineseFont = OLED_12X12_FULL;
            ASCIIFont = OLED_7X12_HALF;
            break;
    }
    if (style == CHINESE)
    {
        return ChineseFont;
    }
    else
    {
        return ASCIIFont;
    }
}

/*
 * @brief Invert the given rectangle with the requested frame style.
 */
void ReverseCoordinate(int16_t X, int16_t Y, int16_t Width, int16_t Height, uint8_t Style)
{
    switch (Style)
    {
        case REVERSE_RECTANGLE:
            OLED_ReverseArea(X, Y, Width, Height);
            break;
        case REVERSE_ROUNDRECTANGLE:
            if (Width >= 3 && Height >= 3)
            {
                OLED_ReverseArea(X, Y, 1, 1);
                OLED_ReverseArea(X + Width - 1, Y, 1, 1);
                OLED_ReverseArea(X, Y + Height - 1, 1, 1);
                OLED_ReverseArea(X + Width - 1, Y + Height - 1, 1, 1);
            }
            OLED_ReverseArea(X, Y, Width, Height);
            break;
        case HOLLOW_RECTANGLE:
            OLED_ReverseArea(X, Y, Width, Height);
            OLED_ReverseArea(X + 1, Y + 1, Width - 2, Height - 2);
            break;
        case HOLLOW_ROUNDRECTANGLE:
            OLED_ReverseArea(X, Y, Width, Height);
            OLED_ReverseArea(X + 1, Y + 1, Width - 2, Height - 2);
            if (Width >= 3)
            {
                OLED_ReverseArea(X, Y, 1, 1);
                OLED_ReverseArea(X + Width - 1, Y, 1, 1);
            }
            if (Height >= 3)
            {
                OLED_ReverseArea(X, Y + Height - 1, 1, 1);
                OLED_ReverseArea(X + Width - 1, Y + Height - 1, 1, 1);
            }
            break;
        case REVERSE_BLOCK:
            OLED_ReverseArea(X, Y + 1, GetOLED_Font(CurrentMenuPage->General_FontSize, ASCII), Height);
            break;
        case NOT_SHOW:
            break;
        default:
            OLED_ReverseArea(X, Y, Width, Height);
            break;
    }
}

/* Fade dither cells, level 1 keeps all pixels, level 5 clears all */
static const uint8_t code OLEDUI_FadePatterns[5][2][2] =
{
    {{0, 0}, {0, 0}},
    {{1, 0}, {0, 0}},
    {{1, 0}, {0, 1}},
    {{1, 0}, {1, 1}},
    {{1, 1}, {1, 1}}
};

/*
 * @brief Apply one dither fade level to a rectangle (clipped to screen).
 * @param fadeLevel 1..5, larger means more pixels cleared
 */
void OLED_UI_FadeOut_Masking(int16_t x0, int16_t y0, int16_t width, int16_t height, int8_t fadeLevel)
{
    int16_t xEnd;
    int16_t yEnd;
    int16_t x;
    int16_t y;
    int16_t pageIdx;
    int16_t bitPos;
    int16_t gridX;
    int16_t gridY;
    uint8_t pixelMask;

    /* Clip to screen */
    if (x0 < 0)
    {
        width += x0;
        x0 = 0;
    }
    if (y0 < 0)
    {
        height += y0;
        y0 = 0;
    }
    if (x0 + width > OLED_WIDTH)
    {
        width = OLED_WIDTH - x0;
    }
    if (y0 + height > OLED_HEIGHT)
    {
        height = OLED_HEIGHT - y0;
    }
    if (width <= 0 || height <= 0)
    {
        return;
    }

    if (fadeLevel < 1 || fadeLevel > 5)
    {
        return;
    }

    xEnd = x0 + width;
    yEnd = y0 + height;

    for (y = y0; y < yEnd; y++)
    {
        pageIdx = y / 8;
        bitPos = y % 8;
        pixelMask = (uint8_t)(1u << (uint8_t)bitPos);

        for (x = x0; x < xEnd; x++)
        {
            gridX = (x - x0) % 2;
            gridY = (y - y0) % 2;
            if (OLEDUI_FadePatterns[fadeLevel - 1][gridY][gridX])
            {
                OLED_DisplayBuf[pageIdx][x] &= (uint8_t)(~pixelMask);
            }
        }
    }
}

/*
 * @brief Fade out the current menu area in 5 steps.
 */
void OLED_UI_FadeoutCurrentArea(int16_t x, int16_t y, int16_t width, int16_t height)
{
    uint8_t i;
    if (CurrentMenuPage->General_MovingSpeed <= 0.0f)
    {
        OLED_Clear();
        return;
    }
    for (i = 1; i <= 5; i++)
    {
        OLED_UI_FadeOut_Masking(x, y, width, height, (int8_t)i);
        OLED_Update();
        Delay_ms(FADEOUT_TIME);
    }
}

/*
 * @brief Fade out the whole screen in 5 steps.
 */
void OLED_UI_FadeoutAllArea(void)
{
    uint8_t i;
    if (CurrentMenuPage->General_MovingSpeed <= 0.0f)
    {
        OLED_Clear();
        return;
    }
    for (i = 1; i <= 5; i++)
    {
        OLED_UI_FadeOut_Masking(0, 0, OLED_WIDTH, OLED_HEIGHT, (int8_t)i);
        OLED_Update();
        Delay_ms(FADEOUT_TIME);
    }
}

/*
 * @brief Count menu items, the list ends at the NULL text sentinel.
 */
MenuID GetMenuItemNum(MenuItem *items)
{
    MenuID num = 0;
    while (items[num].General_item_text != NULL)
    {
        num += 1;
    }
    return num;
}

/*
 * @brief Move one float toward its target (linear or PID spring curve).
 */
void ChangeFloatNum(float *CurrentNum, float *TargetNum, float *ErrorNum, float *LastErrorNum, float *IntegralNum, float *DerivativeNum)
{
    float Kp;
    float Ki;
    float Kd;
    if (CurrentMenuPage->General_MoveStyle == UNLINEAR)
    {
        if (*CurrentNum == *TargetNum)
        {
            return;
        }
        /* Zero speed snaps to the target */
        if (CurrentMenuPage->General_MovingSpeed <= 0.0f)
        {
            *ErrorNum = 0.0f;
            *LastErrorNum = 0.0f;
            *CurrentNum = *TargetNum;
            return;
        }
        *LastErrorNum = *ErrorNum;
        *ErrorNum = *TargetNum - *CurrentNum;
        *CurrentNum += 0.02f * CurrentMenuPage->General_MovingSpeed * (*ErrorNum);
        /* Close enough counts as arrived */
        if (OLEDUI_AbsF(*CurrentNum - *TargetNum) < CurrentMenuPage->General_MovingSpeed / 20.0f)
        {
            *ErrorNum = 0.0f;
            *LastErrorNum = 0.0f;
            *CurrentNum = *TargetNum;
            return;
        }
    }
    if (CurrentMenuPage->General_MoveStyle == PID_CURVE)
    {
        /* Zero speed snaps to the target and clears the PID state */
        if (CurrentMenuPage->General_MovingSpeed <= 0.0f)
        {
            *ErrorNum = 0.0f;
            *LastErrorNum = 0.0f;
            *DerivativeNum = 0.0f;
            *IntegralNum = 0.0f;
            *CurrentNum = *TargetNum;
            return;
        }
        Kp = 0.02f * CurrentMenuPage->General_MovingSpeed;
        Ki = 0.005f * CurrentMenuPage->General_MovingSpeed;
        Kd = 0.002f;

        *LastErrorNum = *ErrorNum;
        *ErrorNum = *TargetNum - *CurrentNum;
        *IntegralNum += *ErrorNum;
        /* Tick period is 0.1 s for the derivative term */
        *DerivativeNum = (*ErrorNum - *LastErrorNum) / 0.1f;
        *CurrentNum += Kp * (*ErrorNum) + Ki * (*IntegralNum) + Kd * (*DerivativeNum);
        /* Close enough counts as arrived */
        if (OLEDUI_AbsF(*TargetNum - *CurrentNum) < 0.5f)
        {
            *ErrorNum = 0.0f;
            *LastErrorNum = 0.0f;
            *DerivativeNum = 0.0f;
            *CurrentNum = *TargetNum;
            return;
        }
    }
}

/*
 * @brief Animate one scalar distance toward its target.
 */
void ChangeDistance(OLED_ChangeDistance *distance)
{
    ChangeFloatNum(&distance->CurrentDistance, &distance->TargetDistance, &distance->Error, &distance->LastError, &distance->Integral, &distance->Derivative);
}

/*
 * @brief Animate one point toward its target.
 */
void ChangePoint(OLED_ChangePoint *point)
{
    ChangeFloatNum(&point->CurrentPoint.X, &point->TargetPoint.X, &point->Error.X, &point->LastError.X, &point->Integral.X, &point->Derivative.X);
    ChangeFloatNum(&point->CurrentPoint.Y, &point->TargetPoint.Y, &point->Error.Y, &point->LastError.Y, &point->Integral.Y, &point->Derivative.Y);
}

/*
 * @brief Animate one area toward its target.
 */
void ChangeArea(OLED_ChangeArea *area)
{
    ChangeFloatNum(&area->CurrentArea.X, &area->TargetArea.X, &area->Error.X, &area->LastError.X, &area->Integral.X, &area->Derivative.X);
    ChangeFloatNum(&area->CurrentArea.Y, &area->TargetArea.Y, &area->Error.Y, &area->LastError.Y, &area->Integral.Y, &area->Derivative.Y);
    ChangeFloatNum(&area->CurrentArea.Width, &area->TargetArea.Width, &area->Error.Width, &area->LastError.Width, &area->Integral.Width, &area->Derivative.Width);
    ChangeFloatNum(&area->CurrentArea.Height, &area->TargetArea.Height, &area->Error.Height, &area->LastError.Height, &area->Integral.Height, &area->Derivative.Height);
}

/*
 * @brief Init animation state when entering a page (slide in effect).
 */
void CurrentMenuPageInit(void)
{
    if (CurrentMenuPage->General_MenuType == MENU_TYPE_LIST)
    {
        /* Start one screen to the right, slide in */
        OLED_UI_PageStartPoint.CurrentPoint.X = OLED_UI_MenuFrame.CurrentArea.X + CurrentMenuPage->List_StartPointX + OLED_WIDTH;
        OLED_UI_PageStartPoint.CurrentPoint.Y = OLED_UI_MenuFrame.CurrentArea.Y + CurrentMenuPage->List_StartPointY;
        OLED_UI_PageStartPoint.TargetPoint.X = CurrentMenuPage->List_MenuArea.X + CurrentMenuPage->List_StartPointX;
        OLED_UI_PageStartPoint.TargetPoint.Y = CurrentMenuPage->List_MenuArea.Y + CurrentMenuPage->List_StartPointY;
        OLED_UI_MenuFrame.TargetArea.X = CurrentMenuPage->List_MenuArea.X;
        OLED_UI_MenuFrame.TargetArea.Y = CurrentMenuPage->List_MenuArea.Y;
        OLED_UI_MenuFrame.TargetArea.Width = CurrentMenuPage->List_MenuArea.Width;
        OLED_UI_MenuFrame.TargetArea.Height = CurrentMenuPage->List_MenuArea.Height;

        /* Start flat so lines unfold */
        OLED_UI_LineStep.CurrentDistance = -3.0f;
        OLED_UI_LineStep.TargetDistance = CurrentMenuPage->General_LineSpace;
        CurrentMenuPage->_ActiveMenuID = 0;
        CurrentMenuPage->_Slot = 0;
    }
    else if (CurrentMenuPage->General_MenuType == MENU_TYPE_TILES)
    {
        /* Keep the remembered tile instead of snapping back to item 0:
         * the static initialiser already starts it at 0 for the very
         * first entry, and every later entry reopens on the tile the
         * user actually left (e.g. Calendar after leaving Calendar).
         * The strip start is shifted by that index so the remembered
         * tile lands in the centre slot, exactly where a manual move
         * would have put it. */
        OLED_UI_PageStartPoint.TargetPoint.X =
            (float)(CurrentMenuPage->Tiles_ScreenWidth / 2 - CurrentMenuPage->Tiles_TileWidth / 2)
            - (float)CurrentMenuPage->_ActiveMenuID *
              (float)(CurrentMenuPage->Tiles_TileWidth + CurrentMenuPage->General_LineSpace);
        OLED_UI_PageStartPoint.TargetPoint.Y = TILES_STARTPOINT_Y;
        /* Start off screen so tiles fly in */
        OLED_UI_PageStartPoint.CurrentPoint.X = -50.0f;
        OLED_UI_PageStartPoint.CurrentPoint.Y = -(float)CurrentMenuPage->Tiles_TileWidth;
        OLED_UI_LineStep.CurrentDistance = 1.0f;
        OLED_UI_LineStep.TargetDistance = CurrentMenuPage->General_LineSpace;
    }
    /* Restart long text scroll from the beginning */
    SetLineSplitZero();
}

/*
 * @brief Restore animation state when going back to a visited page.
 */
void CurrentMenuPageBackUp(void)
{
    if (CurrentMenuPage->General_MenuType == MENU_TYPE_LIST)
    {
        /* Start one screen to the left, slide in */
        OLED_UI_PageStartPoint.CurrentPoint.X = OLED_UI_MenuFrame.CurrentArea.X + CurrentMenuPage->List_StartPointX - OLED_WIDTH;
        OLED_UI_PageStartPoint.CurrentPoint.Y = CurrentMenuPage->_StartPoint.Y;
        OLED_UI_PageStartPoint.TargetPoint.X = CurrentMenuPage->_StartPoint.X;
        OLED_UI_PageStartPoint.TargetPoint.Y = CurrentMenuPage->_StartPoint.Y;
        OLED_UI_LineStep.CurrentDistance = CurrentMenuPage->General_LineSpace;
        OLED_UI_LineStep.TargetDistance = CurrentMenuPage->General_LineSpace;
    }
    else if (CurrentMenuPage->General_MenuType == MENU_TYPE_TILES)
    {
        OLED_UI_PageStartPoint.CurrentPoint.X = CurrentMenuPage->_StartPoint.X + CurrentMenuPage->Tiles_TileWidth;
        OLED_UI_PageStartPoint.CurrentPoint.Y = -(float)CurrentMenuPage->Tiles_TileHeight - 1.0f;
        OLED_UI_PageStartPoint.TargetPoint.X = CurrentMenuPage->_StartPoint.X;
        OLED_UI_PageStartPoint.TargetPoint.Y = CurrentMenuPage->_StartPoint.Y;
        OLED_UI_LineStep.CurrentDistance = CurrentMenuPage->General_LineSpace;
        OLED_UI_LineStep.TargetDistance = CurrentMenuPage->General_LineSpace;
    }
    SetLineSplitZero();
}

/*
 * @brief Init OLED, timer, keys, encoder and show the first page.
 */
void OLED_UI_Init(MenuPage *Page)
{
    OLED_Init();
    Timer_Init();
    Key_Init();
    Encoder_Init();

    CurrentMenuPage = Page;
    CurrentMenuPageInit();
}

/*
 * @brief Enter a page without the Enter-key fade. Used when an
 *        external app (watch face) hands control back to the menu.
 */
void OLED_UI_OpenMenu(MenuPage *Page)
{
    CurrentMenuPage = Page;
    CurrentMenuPageInit();
    OLED_UI_IdleTicks = 0;
    /* Re-entry stale-key fix: park the key snapshot with the live pin
     * state. A key held while the menu opens no longer emits a stale
     * release edge; released keys emit nothing on re-entry. */
    OLED_UI_Key.Enter = Key_GetEnterStatus();
    OLED_UI_Key.Back = Key_GetBackStatus();
    OLED_UI_Key.Up = Key_GetUpStatus();
    OLED_UI_Key.Down = Key_GetDownStatus();
    OLED_UI_LastKey = OLED_UI_Key;
}

/*
 * @brief 1 when no callback is pending (safe to handle input).
 */
unsigned char GetEnterFlag(void)
{
    if (KeyEnterFlag == FLAGEND)
    {
        return 1;
    }
    else
    {
        return 0;
    }
}

/*
 * @brief 1 when no fade transition runs (safe to handle input).
 */
unsigned char GetFadeoutFlag(void)
{
    if (FadeOutFlag == FLAGEND)
    {
        return 1;
    }
    else
    {
        return 0;
    }
}

/*
 * @brief Format a string and measure its pixel width.
 *        Bytes with the top bit set count as full width Chinese
 *        characters (2 bytes each), others as half width ASCII.
 */
int16_t CalcStringWidth(int16_t ChineseFont, int16_t ASCIIFont, const char *format, ...)
{
    int16_t StringLength = 0;
    char xdata String[MAX_STRING_LENGTH];
    char *ptr;
    va_list args;
    va_start(args, format);
    vsprintf(String, format, args);
    va_end(args);
    /* Bounds check: never walk past the buffer */
    String[MAX_STRING_LENGTH - 1] = 0;
    if (strlen(String) >= (unsigned int)(MAX_STRING_LENGTH - 1))
    {
        String[MAX_STRING_LENGTH - 1] = 0;
    }

    ptr = String;
    while (*ptr != 0)
    {
        if ((unsigned char)*ptr & 0x80)
        {
            StringLength += ChineseFont;
            ptr += 2;
        }
        else
        {
            StringLength += ASCIIFont;
            ptr++;
        }
    }

    return StringLength;
}

/*
 * @brief Tell which live data pointer a window uses.
 */
int8_t GetWindowProbDataStyle(MenuWindow *window)
{
    if (window == NULL)
    {
        return WINDOW_DATA_STYLE_NONE;
    }
    if (window->Prob_Data_Int_8 != NULL)
    {
        return WINDOW_DATA_STYLE_INT8;
    }
    else if (window->Prob_Data_Int_16 != NULL)
    {
        return WINDOW_DATA_STYLE_INT16;
    }
    else if (window->Prob_Data_Int_32 != NULL)
    {
        return WINDOW_DATA_STYLE_INT32;
    }
    else if (window->Prob_Data_Int_64 != NULL)
    {
        return WINDOW_DATA_STYLE_INT64;
    }
    else if (window->Prob_Data_Float != NULL)
    {
        return WINDOW_DATA_STYLE_FLOAT;
    }
    else
    {
        return WINDOW_DATA_STYLE_NONE;
    }
}

/*
 * @brief Draw the popup window with text, live data and progress bar.
 */
void OLED_DrawWindow(void)
{
    OLED_Font ChineseFont;
    OLED_Font ASCIIFont;
    int16_t MaxLength;
    int16_t DataLength;
    int8_t DataStyle;
    int16_t WindowTextStringLength;

    /* Park the hidden window off screen */
    if (OLED_SustainCounter.SustainFlag == 0)
    {
        OLED_UI_Window.TargetArea.Height = 30.0f;
        OLED_UI_Window.TargetArea.Width = 60.0f;
        OLED_UI_Window.TargetArea.X = (OLED_WIDTH - 60) / 2;
        OLED_UI_Window.TargetArea.Y = -40.0f;
    }
    if (CurrentWindow == NULL)
    {
        return;
    }
    ChineseFont = GetOLED_Font(CurrentWindow->Text_FontSize, CHINESE);
    ASCIIFont = GetOLED_Font(CurrentWindow->Text_FontSize, ASCII);

    /* Draw only while the window is on screen */
    if (OLED_SustainCounter.SustainFlag == 1 || (OLED_UI_Window.CurrentArea.Height != 30.0f && OLED_UI_Window.CurrentArea.Width != 60.0f && OLED_UI_Window.CurrentArea.Y != -40.0f))
    {
        if (CurrentWindow->General_WindowType == WINDOW_RECTANGLE)
        {
            OLED_DrawRectangle(OLEDUI_CeilF(OLED_UI_Window.CurrentArea.X) - 1, OLEDUI_CeilF(OLED_UI_Window.CurrentArea.Y) - 1, OLEDUI_CeilF(OLED_UI_Window.CurrentArea.Width) + 2, OLEDUI_CeilF(OLED_UI_Window.CurrentArea.Height) + 2, OLED_UNFILLED);
            OLED_ClearArea(OLEDUI_CeilF(OLED_UI_Window.CurrentArea.X), OLEDUI_CeilF(OLED_UI_Window.CurrentArea.Y), OLEDUI_CeilF(OLED_UI_Window.CurrentArea.Width), OLEDUI_CeilF(OLED_UI_Window.CurrentArea.Height));
        }
        else
        {
            OLED_DrawRoundedRectangle(OLEDUI_CeilF(OLED_UI_Window.CurrentArea.X) - 1, OLEDUI_CeilF(OLED_UI_Window.CurrentArea.Y) - 1, OLEDUI_CeilF(OLED_UI_Window.CurrentArea.Width) + 2, OLEDUI_CeilF(OLED_UI_Window.CurrentArea.Height) + 2, 2, OLED_UNFILLED);
            OLED_ClearArea(OLEDUI_CeilF(OLED_UI_Window.CurrentArea.X), OLEDUI_CeilF(OLED_UI_Window.CurrentArea.Y), OLEDUI_CeilF(OLED_UI_Window.CurrentArea.Width), OLEDUI_CeilF(OLED_UI_Window.CurrentArea.Height));
        }
        /* Text layout:
         * title starts at window X + side gap, window top + top gap;
         * data value is right aligned at window right - side gap. */
        MaxLength = OLEDUI_CeilF(OLED_UI_Window.CurrentArea.Width) - 2 * CurrentWindow->Text_FontSideDistance;
        DataLength = 0;
        DataStyle = GetWindowProbDataStyle(CurrentWindow);
        /* Show live data when a data pointer is bound */
        if (DataStyle != WINDOW_DATA_STYLE_NONE)
        {
            if (DataStyle == WINDOW_DATA_STYLE_FLOAT)
            {
                MaxLength = OLEDUI_CeilF(OLED_UI_Window.CurrentArea.Width) - 2 * CurrentWindow->Text_FontSideDistance - CalcStringWidth(ChineseFont, ASCIIFont, OU_FLOAT_FMT, *CurrentWindow->Prob_Data_Float) - WINDOW_DATA_TEXT_DISTANCE;
                DataLength = CalcStringWidth(ChineseFont, ASCIIFont, OU_FLOAT_FMT, *CurrentWindow->Prob_Data_Float);
                OLED_PrintfArea(
                    OLEDUI_CeilF(OLED_UI_Window.CurrentArea.X),
                    OLEDUI_CeilF(OLED_UI_Window.CurrentArea.Y),
                    OLEDUI_CeilF(OLED_UI_Window.CurrentArea.Width),
                    OLEDUI_CeilF(OLED_UI_Window.CurrentArea.Height),
                    OLEDUI_CeilF(OLED_UI_Window.CurrentArea.X) + CurrentWindow->General_Width - 1 - CurrentWindow->Text_FontSideDistance - DataLength,
                    OLEDUI_CeilF(OLED_UI_Window.CurrentArea.Y) + CurrentWindow->Text_FontTopDistance,
                    ASCIIFont,
                    OU_FLOAT_FMT, *CurrentWindow->Prob_Data_Float);
            }
            else
            {
                switch (DataStyle)
                {
                    case WINDOW_DATA_STYLE_INT8:
                        MaxLength = OLEDUI_CeilF(OLED_UI_Window.CurrentArea.Width) - 2 * CurrentWindow->Text_FontSideDistance - CalcStringWidth(ChineseFont, ASCIIFont, "%d", *CurrentWindow->Prob_Data_Int_8) - WINDOW_DATA_TEXT_DISTANCE;
                        DataLength = CalcStringWidth(ChineseFont, ASCIIFont, "%d", *CurrentWindow->Prob_Data_Int_8);
                        OLED_PrintfArea(
                            OLEDUI_CeilF(OLED_UI_Window.CurrentArea.X),
                            OLEDUI_CeilF(OLED_UI_Window.CurrentArea.Y),
                            OLEDUI_CeilF(OLED_UI_Window.CurrentArea.Width),
                            OLEDUI_CeilF(OLED_UI_Window.CurrentArea.Height),
                            OLEDUI_CeilF(OLED_UI_Window.CurrentArea.X) + CurrentWindow->General_Width - 1 - CurrentWindow->Text_FontSideDistance - DataLength,
                            OLEDUI_CeilF(OLED_UI_Window.CurrentArea.Y) + CurrentWindow->Text_FontTopDistance,
                            ASCIIFont,
                            "%d", *CurrentWindow->Prob_Data_Int_8);
                        break;
                    case WINDOW_DATA_STYLE_INT16:
                        MaxLength = OLEDUI_CeilF(OLED_UI_Window.CurrentArea.Width) - 2 * CurrentWindow->Text_FontSideDistance - CalcStringWidth(ChineseFont, ASCIIFont, "%d", *CurrentWindow->Prob_Data_Int_16) - WINDOW_DATA_TEXT_DISTANCE;
                        DataLength = CalcStringWidth(ChineseFont, ASCIIFont, "%d", *CurrentWindow->Prob_Data_Int_16);
                        OLED_PrintfArea(
                            OLEDUI_CeilF(OLED_UI_Window.CurrentArea.X),
                            OLEDUI_CeilF(OLED_UI_Window.CurrentArea.Y),
                            OLEDUI_CeilF(OLED_UI_Window.CurrentArea.Width),
                            OLEDUI_CeilF(OLED_UI_Window.CurrentArea.Height),
                            OLEDUI_CeilF(OLED_UI_Window.CurrentArea.X) + CurrentWindow->General_Width - 1 - CurrentWindow->Text_FontSideDistance - DataLength,
                            OLEDUI_CeilF(OLED_UI_Window.CurrentArea.Y) + CurrentWindow->Text_FontTopDistance,
                            ASCIIFont,
                            "%d", *CurrentWindow->Prob_Data_Int_16);
                        break;
                    case WINDOW_DATA_STYLE_INT32:
                        MaxLength = OLEDUI_CeilF(OLED_UI_Window.CurrentArea.Width) - 2 * CurrentWindow->Text_FontSideDistance - CalcStringWidth(ChineseFont, ASCIIFont, "%ld", *CurrentWindow->Prob_Data_Int_32) - WINDOW_DATA_TEXT_DISTANCE;
                        DataLength = CalcStringWidth(ChineseFont, ASCIIFont, "%ld", *CurrentWindow->Prob_Data_Int_32);
                        OLED_PrintfArea(
                            OLEDUI_CeilF(OLED_UI_Window.CurrentArea.X),
                            OLEDUI_CeilF(OLED_UI_Window.CurrentArea.Y),
                            OLEDUI_CeilF(OLED_UI_Window.CurrentArea.Width),
                            OLEDUI_CeilF(OLED_UI_Window.CurrentArea.Height),
                            OLEDUI_CeilF(OLED_UI_Window.CurrentArea.X) + CurrentWindow->General_Width - 1 - CurrentWindow->Text_FontSideDistance - DataLength,
                            OLEDUI_CeilF(OLED_UI_Window.CurrentArea.Y) + CurrentWindow->Text_FontTopDistance,
                            ASCIIFont,
                            "%ld", *CurrentWindow->Prob_Data_Int_32);
                        break;
                    case WINDOW_DATA_STYLE_INT64:
                        /* 32-bit long on C251, same range as INT32 */
                        MaxLength = OLEDUI_CeilF(OLED_UI_Window.CurrentArea.Width) - 2 * CurrentWindow->Text_FontSideDistance - CalcStringWidth(ChineseFont, ASCIIFont, "%ld", *CurrentWindow->Prob_Data_Int_64) - WINDOW_DATA_TEXT_DISTANCE;
                        DataLength = CalcStringWidth(ChineseFont, ASCIIFont, "%ld", *CurrentWindow->Prob_Data_Int_64);
                        OLED_PrintfArea(
                            OLEDUI_CeilF(OLED_UI_Window.CurrentArea.X),
                            OLEDUI_CeilF(OLED_UI_Window.CurrentArea.Y),
                            OLEDUI_CeilF(OLED_UI_Window.CurrentArea.Width),
                            OLEDUI_CeilF(OLED_UI_Window.CurrentArea.Height),
                            OLEDUI_CeilF(OLED_UI_Window.CurrentArea.X) + CurrentWindow->General_Width - 1 - CurrentWindow->Text_FontSideDistance - DataLength,
                            OLEDUI_CeilF(OLED_UI_Window.CurrentArea.Y) + CurrentWindow->Text_FontTopDistance,
                            ASCIIFont,
                            "%ld", *CurrentWindow->Prob_Data_Int_64);
                        break;
                    default:
                        break;
                }
            }

            /* Progress bar frame */
            OLED_DrawRoundedRectangle(OLEDUI_CeilF(OLED_UI_Window.CurrentArea.X) + CurrentWindow->Prob_SideDistance,
                OLEDUI_CeilF(OLED_UI_Window.CurrentArea.Y) + OLEDUI_CeilF(OLED_UI_Window.CurrentArea.Height) - CurrentWindow->Prob_LineHeight - CurrentWindow->Prob_BottomDistance,
                OLEDUI_CeilF(OLED_UI_Window.CurrentArea.Width) - 2 * CurrentWindow->Prob_SideDistance, CurrentWindow->Prob_LineHeight, 2, OLED_UNFILLED);
            /* Progress bar fill, clamped to the frame */
            OLED_DrawRectangle(OLEDUI_CeilF(OLED_UI_Window.CurrentArea.X) + CurrentWindow->Prob_SideDistance + 2,
                OLEDUI_CeilF(OLED_UI_Window.CurrentArea.Y) + OLEDUI_CeilF(OLED_UI_Window.CurrentArea.Height) - CurrentWindow->Prob_LineHeight - CurrentWindow->Prob_BottomDistance + 2,
                OLEDUI_CeilF(OLED_UI_ProbWidth.CurrentDistance) >= OLEDUI_CeilF(OLED_UI_Window.CurrentArea.Width) - 2 * CurrentWindow->Prob_SideDistance - 4 ? OLEDUI_CeilF(OLED_UI_Window.CurrentArea.Width) - 2 * CurrentWindow->Prob_SideDistance - 4 : OLEDUI_CeilF(OLED_UI_ProbWidth.CurrentDistance), CurrentWindow->Prob_LineHeight - 4, OLED_FILLED);
        }
        if (CurrentWindow->Text_String != NULL)
        {
            WindowTextStringLength = CalcStringWidth(ChineseFont, ASCIIFont, "%s", CurrentWindow->Text_String);
            /* Scroll the title when it is wider than the free space */
            if (WindowTextStringLength > MaxLength)
            {
#if IF_WAIT_ANIMATION_FINISH
                if (OLED_UI_Window.CurrentArea.X == OLED_UI_Window.TargetArea.X &&
                    OLED_UI_Window.CurrentArea.Y == OLED_UI_Window.TargetArea.Y &&
                    OLED_UI_Window.CurrentArea.Width == OLED_UI_Window.TargetArea.Width &&
                    OLED_UI_Window.CurrentArea.Height == OLED_UI_Window.TargetArea.Height)
                {
#endif
                    CurrentWindow->_LineSlip -= LINE_SLIP_SPEED;
#if IF_WAIT_ANIMATION_FINISH
                }
#endif
            }
            if (CurrentWindow->_LineSlip < -WindowTextStringLength)
            {
                CurrentWindow->_LineSlip = MaxLength + 1;
            }

            OLED_PrintfArea(
                OLEDUI_CeilF(OLED_UI_Window.CurrentArea.X) + CurrentWindow->Text_FontSideDistance,
                OLEDUI_CeilF(OLED_UI_Window.CurrentArea.Y) + CurrentWindow->Text_FontTopDistance,
                MaxLength,
                OLEDUI_CeilF(OLED_UI_Window.CurrentArea.Height),
                OLEDUI_CeilF(OLED_UI_Window.CurrentArea.X) + CurrentWindow->Text_FontSideDistance + OLEDUI_CeilF(CurrentWindow->_LineSlip),
                OLEDUI_CeilF(OLED_UI_Window.CurrentArea.Y) + CurrentWindow->Text_FontTopDistance,
                ASCIIFont, "%s", CurrentWindow->Text_String);
        }
    }
    else
    {
        /* Window has left the screen, release it */
        OLED_UI_ProbWidth.CurrentDistance = 0.0f;
        OLED_UI_ProbWidth.Derivative = 0.0f;
        OLED_UI_ProbWidth.Error = 0.0f;
        OLED_UI_ProbWidth.Integral = 0.0f;
        OLED_UI_ProbWidth.LastError = 0.0f;
        OLED_UI_ProbWidth.TargetDistance = 0.0f;
        CurrentWindow = NULL;
    }
}

/*
 * @brief Move list items down by one row (cursor goes up).
 */
void MenuItemsMoveUp(void)
{
    OLED_UI_PageStartPoint.TargetPoint.Y -= (OLED_UI_LineStep.TargetDistance + CurrentMenuPage->General_FontSize);
}

/*
 * @brief Move list items up by one row (cursor goes down).
 */
void MenuItemsMoveDown(void)
{
    OLED_UI_PageStartPoint.TargetPoint.Y += (OLED_UI_LineStep.TargetDistance + CurrentMenuPage->General_FontSize);
}

/*
 * @brief Move tiles right by one tile (cursor goes left).
 */
void MenuItemsMoveLeft(void)
{
    OLED_UI_PageStartPoint.TargetPoint.X -= (OLED_UI_LineStep.TargetDistance + CurrentMenuPage->Tiles_TileWidth);
}

/*
 * @brief Move tiles left by one tile (cursor goes right).
 */
void MenuItemsMoveRight(void)
{
    OLED_UI_PageStartPoint.TargetPoint.X += (OLED_UI_LineStep.TargetDistance + CurrentMenuPage->Tiles_TileWidth);
}

/*
 * @brief Count how many list rows fit in the menu area.
 */
int16_t GetCurrentMenuPageMaxSlotNum(void)
{
    return (int16_t)(CurrentMenuPage->List_MenuArea.Height - CurrentMenuPage->List_StartPointY + OLED_UI_LineStep.TargetDistance - 1) / (OLED_UI_LineStep.TargetDistance + CurrentMenuPage->General_FontSize);
}

/*
 * @brief Aim the cursor highlight at the active item.
 */
void SetTargetCursor(void)
{
    float cursorW;
    float frameW;
    if (CurrentMenuPage->General_MenuType == MENU_TYPE_LIST)
    {
        OLED_UI_Cursor.TargetArea.X = OLED_UI_PageStartPoint.TargetPoint.X - 1.0f;
        OLED_UI_Cursor.TargetArea.Y = OLED_UI_PageStartPoint.TargetPoint.Y +
            CurrentMenuPage->_ActiveMenuID * (CurrentMenuPage->General_LineSpace + CurrentMenuPage->General_FontSize) - 1.0f;
        OLED_UI_Cursor.TargetArea.Height = CurrentMenuPage->General_FontSize + 2;
        cursorW = (float)CalcStringWidth(
            GetOLED_Font(CurrentMenuPage->General_FontSize, CHINESE), GetOLED_Font(CurrentMenuPage->General_FontSize, ASCII), "%s", CurrentMenuPage->General_MenuItems[CurrentMenuPage->_ActiveMenuID].General_item_text) + 2.0f;
        frameW = OLED_UI_MenuFrame.CurrentArea.Width + OLED_UI_MenuFrame.CurrentArea.X - OLED_UI_PageStartPoint.CurrentPoint.X - 6.0f;
        /* The radio box owns the right edge: keep the highlight off it */
        if (CurrentMenuPage->General_MenuItems[CurrentMenuPage->_ActiveMenuID].List_BoolRadioBox != NULL)
        {
            frameW -= (float)RADIO_BOX_RESERVE;
        }
        OLED_UI_Cursor.TargetArea.Width = cursorW < frameW ? cursorW : frameW;
    }
    if (CurrentMenuPage->General_MenuType == MENU_TYPE_TILES)
    {
        OLED_UI_Cursor.TargetArea.X = CurrentMenuPage->Tiles_ScreenWidth / 2 - CalcStringWidth(GetOLED_Font(CurrentMenuPage->General_FontSize, CHINESE), GetOLED_Font(CurrentMenuPage->General_FontSize, ASCII), "%s", CurrentMenuPage->General_MenuItems[CurrentMenuPage->_ActiveMenuID].General_item_text) / 2 - 1;
        OLED_UI_Cursor.TargetArea.Y = CurrentMenuPage->Tiles_ScreenHeight - CurrentMenuPage->General_FontSize - TILES_BOTTOM_DISTANCE - 1;
        OLED_UI_Cursor.TargetArea.Height = CurrentMenuPage->General_FontSize + 2;
        OLED_UI_Cursor.TargetArea.Width = CalcStringWidth(GetOLED_Font(CurrentMenuPage->General_FontSize, CHINESE), GetOLED_Font(CurrentMenuPage->General_FontSize, ASCII), "%s", CurrentMenuPage->General_MenuItems[CurrentMenuPage->_ActiveMenuID].General_item_text) + 2;
        /* The label band is a readout pinned under the name it shows, not
         * a highlight that travels: snapping it keeps the chip exactly on
         * the label and stops it sweeping in from the corner on entry. */
        OLED_UI_Cursor.CurrentArea = OLED_UI_Cursor.TargetArea;
    }
}

/*
 * @brief Aim the menu frame at the current page area.
 */
void SetTargetMenuFrame(void)
{
    OLED_UI_MenuFrame.TargetArea.X = CurrentMenuPage->List_MenuArea.X;
    OLED_UI_MenuFrame.TargetArea.Y = CurrentMenuPage->List_MenuArea.Y;
    OLED_UI_MenuFrame.TargetArea.Width = CurrentMenuPage->List_MenuArea.Width;
    OLED_UI_MenuFrame.TargetArea.Height = CurrentMenuPage->List_MenuArea.Height;
}

/*
 * @brief Reset long text scroll offsets of the current page.
 */
void SetLineSplitZero(void)
{
    MenuPage *page = CurrentMenuPage;
    MenuID i;
    for (i = 0; i < GetMenuItemNum(page->General_MenuItems); i++)
    {
        page->General_MenuItems[i]._LineSlip = 0.0f;
    }
}

/*
 * @brief Aim the scrollbar fill at the active item ratio.
 */
void SetTargetScrollBarHeight(void)
{
    if (CurrentMenuPage->General_MenuType == MENU_TYPE_TILES)
    {
        OLED_UI_ScrollBarHeight.TargetDistance = (float)(CurrentMenuPage->Tiles_ScreenWidth * (CurrentMenuPage->_ActiveMenuID + 1) / GetMenuItemNum(CurrentMenuPage->General_MenuItems));
    }
}

/*
 * @brief Aim the window progress bar at the live data ratio.
 */
void SetTargetProbWidth(void)
{
    MenuWindow *window;
    int8_t DataStyle;
    window = CurrentWindow;
    DataStyle = GetWindowProbDataStyle(window);

    if (DataStyle != WINDOW_DATA_STYLE_NONE)
    {
        if (DataStyle == WINDOW_DATA_STYLE_FLOAT)
        {
            OLED_UI_ProbWidth.TargetDistance = ((*window->Prob_Data_Float - window->Prob_MinData) / ((float)window->Prob_MaxData - window->Prob_MinData)) * (OLED_UI_Window.CurrentArea.Width - 2 * window->Prob_SideDistance - 4);
        }
        else
        {
            switch (DataStyle)
            {
                case WINDOW_DATA_STYLE_INT8:
                    OLED_UI_ProbWidth.TargetDistance = ((*CurrentWindow->Prob_Data_Int_8 - window->Prob_MinData) / ((float)window->Prob_MaxData - window->Prob_MinData)) * (OLED_UI_Window.CurrentArea.Width - 2 * window->Prob_SideDistance - 4);
                    break;
                case WINDOW_DATA_STYLE_INT16:
                    OLED_UI_ProbWidth.TargetDistance = ((*CurrentWindow->Prob_Data_Int_16 - window->Prob_MinData) / ((float)window->Prob_MaxData - window->Prob_MinData)) * (OLED_UI_Window.CurrentArea.Width - 2 * window->Prob_SideDistance - 4);
                    break;
                case WINDOW_DATA_STYLE_INT32:
                    OLED_UI_ProbWidth.TargetDistance = ((*CurrentWindow->Prob_Data_Int_32 - window->Prob_MinData) / ((float)window->Prob_MaxData - window->Prob_MinData)) * (OLED_UI_Window.CurrentArea.Width - 2 * window->Prob_SideDistance - 4);
                    break;
                case WINDOW_DATA_STYLE_INT64:
                    /* 32-bit long on C251, same range as INT32 */
                    OLED_UI_ProbWidth.TargetDistance = ((*CurrentWindow->Prob_Data_Int_64 - window->Prob_MinData) / ((float)window->Prob_MaxData - window->Prob_MinData)) * (OLED_UI_Window.CurrentArea.Width - 2 * window->Prob_SideDistance - 4);
                    break;
                default:
                    break;
            }
        }
    }
    else
    {
        return;
    }
}

/*
 * @brief Print all visible items of the current page.
 */
void PrintMenuElements(void)
{
    /* Snapshot page and animation state so the loop is reentry safe.
     * Snapshots live in xdata: they are far too big for DATA overlay. */
    MenuPage *page;
    OLED_ChangePoint xdata TempTargetPoint;
    OLED_ChangeArea xdata TempTargetArea;
    MenuID num;
    OLED_Font ChineseFont;
    OLED_Font ASCIIFont;
    OLED_Point CursorPoint;
    float TilesStartY;
    int16_t RadioBoxReserve;
    int16_t RadioBoxX;
    int16_t RadioBoxY;
    int16_t StringLength;
    int16_t ScrollBarHeight;
    int16_t ScrollBarX;
    MenuID i;

    page = CurrentMenuPage;
    TempTargetPoint = OLED_UI_PageStartPoint;
    TempTargetArea = OLED_UI_MenuFrame;
    num = GetMenuItemNum(page->General_MenuItems);
    ChineseFont = GetOLED_Font(page->General_FontSize, CHINESE);
    ASCIIFont = GetOLED_Font(page->General_FontSize, ASCII);
    CursorPoint = TempTargetPoint.CurrentPoint;

    if (page->General_MenuType == MENU_TYPE_LIST)
    {
        /* Draw the menu frame slightly larger so it never covers text */
        if (page->List_IfDrawFrame == 1)
        {
            OLED_DrawRectangle(OLEDUI_CeilF(TempTargetArea.CurrentArea.X) - 1, OLEDUI_CeilF(TempTargetArea.CurrentArea.Y) - 1, OLEDUI_CeilF(TempTargetArea.CurrentArea.Width) + 2, OLEDUI_CeilF(TempTargetArea.CurrentArea.Height) + 2, OLED_UNFILLED);
        }

        for (i = 0; i < num; i++)
        {
            /* Skip rows fully outside the screen */
            if (CursorPoint.Y + ChineseFont < 0 || CursorPoint.Y > OLED_HEIGHT)
            {
                CursorPoint.Y += (page->General_FontSize + OLED_UI_LineStep.CurrentDistance);
                continue;
            }

            StringLength = CalcStringWidth(ChineseFont, ASCIIFont, "%s", page->General_MenuItems[i].General_item_text);

            /* Radio row: a real checkbox on the right edge shows the state.
             * The row keeps that slot free of text. */
            if (page->General_MenuItems[i].List_BoolRadioBox != NULL)
            {
                RadioBoxReserve = RADIO_BOX_RESERVE;
                RadioBoxX = OLEDUI_CeilF(TempTargetArea.CurrentArea.X) + OLEDUI_CeilF(TempTargetArea.CurrentArea.Width) - RADIO_BOX_GAP - RADIO_BOX_SIZE;
                RadioBoxY = OLEDUI_CeilF(CursorPoint.Y) + (page->General_FontSize - RADIO_BOX_SIZE) / 2;
                OLED_DrawRectangle(RadioBoxX, RadioBoxY, RADIO_BOX_SIZE, RADIO_BOX_SIZE, OLED_UNFILLED);
                if (*page->General_MenuItems[i].List_BoolRadioBox != 0)
                {
                    OLED_DrawRectangle(RadioBoxX + 2, RadioBoxY + 2, RADIO_BOX_SIZE - 4, RADIO_BOX_SIZE - 4, OLED_FILLED);
                }
            }
            else
            {
                RadioBoxReserve = 0;
            }

            /* Scroll text that overflows the row */
            if (StringLength > (TempTargetArea.TargetArea.X + TempTargetArea.TargetArea.Width - 6 - 2) - TempTargetPoint.TargetPoint.X - RadioBoxReserve)
            {
#if IF_WAIT_ANIMATION_FINISH
                if (TempTargetArea.TargetArea.Width == TempTargetArea.CurrentArea.Width &&
                    TempTargetArea.TargetArea.X == TempTargetArea.CurrentArea.X &&
                    TempTargetArea.TargetArea.Height == TempTargetArea.CurrentArea.Height &&
                    TempTargetArea.TargetArea.Y == TempTargetArea.CurrentArea.Y)
                {
#endif
                    page->General_MenuItems[i]._LineSlip -= LINE_SLIP_SPEED;
#if IF_WAIT_ANIMATION_FINISH
                }
#endif
                /* Wrap around once fully scrolled out */
                if (page->General_MenuItems[i]._LineSlip < -StringLength)
                {
                    page->General_MenuItems[i]._LineSlip = (TempTargetArea.TargetArea.X + TempTargetArea.TargetArea.Width - 6) - CursorPoint.X;
                }
            }

            OLED_PrintfArea(OLEDUI_CeilF(TempTargetArea.CurrentArea.X) + CurrentMenuPage->List_StartPointX,
                OLEDUI_CeilF(TempTargetArea.CurrentArea.Y),
                OLEDUI_CeilF(TempTargetArea.CurrentArea.Width) - 6 - CurrentMenuPage->List_StartPointX - 2 - RadioBoxReserve,
                OLEDUI_CeilF(TempTargetArea.CurrentArea.Height),
                OLEDUI_CeilF(CursorPoint.X + page->General_MenuItems[i]._LineSlip),
                OLEDUI_CeilF(CursorPoint.Y),
                ASCIIFont, "%s", page->General_MenuItems[i].General_item_text);

            CursorPoint.Y += (page->General_FontSize + OLED_UI_LineStep.CurrentDistance);
        }
    }
    if (page->General_MenuType == MENU_TYPE_TILES)
    {
        /* The strip only slides horizontally, so the row Y is constant for
         * the whole loop and marks where the cursor ring goes. */
        TilesStartY = CursorPoint.Y;

        for (i = 0; i < num; i++)
        {
            /* Skip tiles fully outside the screen */
            if (CursorPoint.X + page->Tiles_TileWidth < 0 || CursorPoint.X > OLED_WIDTH)
            {
                CursorPoint.X += (page->Tiles_TileWidth + OLED_UI_LineStep.CurrentDistance);
                continue;
            }

            OLED_ShowImageArea(OLEDUI_CeilF(CursorPoint.X), OLEDUI_CeilF(CursorPoint.Y), page->Tiles_TileWidth, page->Tiles_TileHeight, 0, 0, page->Tiles_ScreenWidth, page->Tiles_ScreenHeight, page->General_MenuItems[i].Tiles_Icon == NULL ? UnKnown : page->General_MenuItems[i].Tiles_Icon);
            CursorPoint.X += (page->Tiles_TileWidth + OLED_UI_LineStep.CurrentDistance);
        }

        /* Selection cursor, part 1: a 1 px inverse video ring around the
         * centre slot, rounded like the list row highlight. Inverse video
         * keeps it visible over the white tile backgrounds and over the
         * black gap alike. The active tile always settles on this slot, so
         * a ring fixed here reads as the selection while the strip slides
         * underneath. Painted here, not through OLED_UI_Cursor, because the
         * generic cursor target only knows the settled position and the
         * ring has to ride the sliding row Y to stay on the tile.
         * Part 2 is the label band, drawn by the framework cursor. */
        ReverseCoordinate(
            page->Tiles_ScreenWidth / 2 - page->Tiles_TileWidth / 2 - TILES_CURSOR_GAP,
            OLEDUI_CeilF(TilesStartY) - TILES_CURSOR_GAP,
            page->Tiles_TileWidth + 2 * TILES_CURSOR_GAP,
            page->Tiles_TileHeight + 2 * TILES_CURSOR_GAP,
            HOLLOW_ROUNDRECTANGLE);
        /* Arrow head above the tiles, off by default (see IF_DRAW_ARROR).
         * The preprocessor guard drops the draw call entirely, a plain
         * constant test would leave dead code behind. */
#if IF_DRAW_ARROR
        OLED_ShowImageArea(page->Tiles_ScreenWidth / 2 - 3, 0, 6, 5, 0, 0, page->Tiles_ScreenWidth, page->Tiles_ScreenHeight, Arrow);
#endif
        StringLength = CalcStringWidth(ChineseFont, ASCIIFont, "%s", page->General_MenuItems[page->_ActiveMenuID].General_item_text);
        /* Scroll the active label when wider than the screen */
        if (StringLength > page->Tiles_ScreenWidth)
        {
#if IF_WAIT_ANIMATION_FINISH
            if (OLED_UI_PageStartPoint.CurrentPoint.X == OLED_UI_PageStartPoint.TargetPoint.X &&
                OLED_UI_PageStartPoint.CurrentPoint.Y == OLED_UI_PageStartPoint.TargetPoint.Y)
            {
#endif
                page->General_MenuItems[page->_ActiveMenuID]._LineSlip -= LINE_SLIP_SPEED;
#if IF_WAIT_ANIMATION_FINISH
            }
#endif
        }
        else
        {
            SetLineSplitZero();
        }
        if (page->General_MenuItems[page->_ActiveMenuID]._LineSlip < -StringLength)
        {
            page->General_MenuItems[page->_ActiveMenuID]._LineSlip = page->Tiles_ScreenWidth + 1;
        }

        OLED_PrintfArea(0, 0, page->Tiles_ScreenWidth, page->Tiles_ScreenHeight,
            StringLength > page->Tiles_ScreenWidth ? OLEDUI_CeilF(page->General_MenuItems[page->_ActiveMenuID]._LineSlip) : page->Tiles_ScreenWidth / 2 - CalcStringWidth(ChineseFont, ASCIIFont, "%s", page->General_MenuItems[page->_ActiveMenuID].General_item_text) / 2 + OLEDUI_CeilF(page->General_MenuItems[page->_ActiveMenuID]._LineSlip),
            page->Tiles_ScreenHeight - page->General_FontSize - TILES_BOTTOM_DISTANCE,
            ASCIIFont,
            "%s", page->General_MenuItems[page->_ActiveMenuID].General_item_text);
        /* Below the tiles: a thin track with a solid cursor block sitting
         * on it. The block is one slot wide (14 px) and its x position is
         * driven by _ActiveMenuID, so it reads as the selection cursor
         * instead of a filling progress bar: item 0 parks it on the left
         * edge, item 8 puts its right edge on the screen edge (114 + 14).
         * The track is drawn first and the block over it, so the block
         * really is the cursor, not a filled segment of the bar. */
        ScrollBarHeight = (page->Tiles_ScreenHeight >= 128 ? 5 : 4);
        ScrollBarX = (int16_t)((114UL * (unsigned long)page->_ActiveMenuID) / 8UL);
        OLED_DrawLine(0,
            TILES_STARTPOINT_Y + page->Tiles_TileHeight + TILES_SCROLLBAR_Y + ScrollBarHeight / 2,
            page->Tiles_ScreenWidth - 1,
            TILES_STARTPOINT_Y + page->Tiles_TileHeight + TILES_SCROLLBAR_Y + ScrollBarHeight / 2);
        OLED_DrawRectangle(ScrollBarX,
            TILES_STARTPOINT_Y + page->Tiles_TileHeight + TILES_SCROLLBAR_Y,
            TILES_THUMB_WIDTH, ScrollBarHeight, OLED_FILLED);
    }
    if (page->General_ShowAuxiliaryFunction != NULL)
    {
        page->General_ShowAuxiliaryFunction();
    }
}

/*
 * @brief Sample keys and encoder, report the active item movement.
 * @return Safe delta moves the cursor, Unsafe delta adjusts window data.
 */
MenuID_Type OLED_KeyAndEncoderRecord(void)
{
    static unsigned char IfUpTapAndHold = 0;
    static unsigned char IfDownTapAndHold = 0;
#if IF_START_UP_AND_DOWN_LONG_PRESS
    static int16_t UpPressCounter = 0;
    static int16_t UpPressedCounter = 0;
    static int16_t DownPressCounter = 0;
    static int16_t DownPressedCounter = 0;
#endif
    int16_t MaxID;
    MenuID_Type IncreaseID;
    int16_t ActiveMenuID;
    int16_t LastActiveID;

    /* Shift key states */
    OLED_UI_LastKey.Up = OLED_UI_Key.Up;
    OLED_UI_LastKey.Down = OLED_UI_Key.Down;
    OLED_UI_LastKey.Enter = OLED_UI_Key.Enter;
    OLED_UI_LastKey.Back = OLED_UI_Key.Back;

    /* Sample current key states, 0 means pressed */
    OLED_UI_Key.Enter = Key_GetEnterStatus();
    OLED_UI_Key.Back = Key_GetBackStatus();
    OLED_UI_Key.Up = Key_GetUpStatus();
    OLED_UI_Key.Down = Key_GetDownStatus();
    /* UART console virtual presses, each lasts exactly one tick */
    if (USBCtrl_TakeEnter() != 0)
    {
        OLED_UI_Key.Enter = 0;
    }
    if (USBCtrl_TakeBack() != 0)
    {
        OLED_UI_Key.Back = 0;
    }
    if (USBCtrl_TakeUp() != 0)
    {
        OLED_UI_Key.Up = 0;
    }
    if (USBCtrl_TakeDown() != 0)
    {
        OLED_UI_Key.Down = 0;
    }
#if IF_START_UP_AND_DOWN_LONG_PRESS

    /* Up held: after PRESS_TIME start auto repeat */
    if (OLED_UI_LastKey.Up == 0 && OLED_UI_Key.Up == 0)
    {
        UpPressCounter++;
        if (UpPressCounter >= (float)PRESS_TIME * 50)
        {
            UpPressedCounter++;
            if (UpPressedCounter >= (UpPressCounter >= (PRESS_TIME + CONTINUE_PRESS_TIME) * 50 ? CONTINUE_PRESS_MOVE_SPEED : PRESS_MOVE_SPEED))
            {
                IfUpTapAndHold = 1;
                UpPressedCounter = 0;
                IfUpTapAndHold = 1;
            }
            else
            {
                IfUpTapAndHold = 0;
            }
        }
    }
    else
    {
        UpPressCounter = 0;
        UpPressedCounter = 0;
        IfUpTapAndHold = 0;
    }
    /* Down held: after PRESS_TIME start auto repeat */
    if (OLED_UI_LastKey.Down == 0 && OLED_UI_Key.Down == 0)
    {
        DownPressCounter++;
        if (DownPressCounter >= (float)PRESS_TIME * 50)
        {
            DownPressedCounter++;
            if (DownPressedCounter >= (DownPressCounter >= (PRESS_TIME + CONTINUE_PRESS_TIME) * 50 ? CONTINUE_PRESS_MOVE_SPEED : PRESS_MOVE_SPEED))
            {
                IfDownTapAndHold = 1;
                DownPressedCounter = 0;
                IfDownTapAndHold = 1;
            }
            else
            {
                IfDownTapAndHold = 0;
            }
        }
    }
    else
    {
        DownPressCounter = 0;
        DownPressedCounter = 0;
        IfDownTapAndHold = 0;
    }

#endif
    if (OLED_UI_Key.Up == OLED_UI_Key.Down)
    {
        IfUpTapAndHold = 0;
        IfDownTapAndHold = 0;
    }

    MaxID = GetMenuItemNum(CurrentMenuPage->General_MenuItems);
    IncreaseID.Safe = 0;
    IncreaseID.Unsafe = 0;
    ActiveMenuID = CurrentMenuPage->_ActiveMenuID;
    LastActiveID = ActiveMenuID;
    /* Encoder steps (hardware + UART console) move the selection */
    ActiveMenuID += Encoder_Get() + USBCtrl_TakeEncoder();

    /* Released Up key (or hold repeat) moves up */
    if ((OLED_UI_Key.Up != OLED_UI_LastKey.Up && OLED_UI_Key.Up == 1) || IfUpTapAndHold)
    {
        ActiveMenuID--;
    }

    /* Released Down key (or hold repeat) moves down */
    if ((OLED_UI_Key.Down != OLED_UI_LastKey.Down && OLED_UI_Key.Down == 1) || IfDownTapAndHold)
    {
        ActiveMenuID++;
    }

    IncreaseID.Unsafe = ActiveMenuID - LastActiveID;
    /* Clamp to the item range */
    if (ActiveMenuID >= MaxID - 1)
    {
        ActiveMenuID = MaxID - 1;
    }
    if (ActiveMenuID <= 0)
    {
        ActiveMenuID = 0;
    }

    IncreaseID.Safe = ActiveMenuID - LastActiveID;
    return IncreaseID;
}

/*
 * @brief Lock the enter mutex and freeze the encoder for the callback.
 */
void SetEnterFlag(void)
{
    KeyEnterFlag = FLAGSTART;
    Encoder_Disable();
}

/*
 * @brief Release the enter mutex.
 */
void ResetEnterFlag(void)
{
    KeyEnterFlag = FLAGEND;
}

/*
 * @brief Lock the fade mutex and freeze the encoder for the transition.
 */
void SetFadeOutFlag(MutexFlag action)
{
    FadeOutFlag = action;
    Encoder_Disable();
}

/*
 * @brief Release the fade mutex.
 */
void ResetFadeOutFlag(void)
{
    FadeOutFlag = FLAGEND;
}

/*
 * @brief Handle the Enter key on the active item:
 *        callback only -> run callback;
 *        submenu only -> fade into submenu.
 */
void EnterEventMenuItem(void)
{
    if (CurrentMenuPage->General_MenuItems[CurrentMenuPage->_ActiveMenuID].General_callback != NULL && CurrentMenuPage->General_MenuItems[CurrentMenuPage->_ActiveMenuID].General_SubMenuPage == NULL)
    {
        SetEnterFlag();
    }
    if (CurrentMenuPage->General_MenuItems[CurrentMenuPage->_ActiveMenuID].General_SubMenuPage != NULL
        && CurrentMenuPage->General_MenuItems[CurrentMenuPage->_ActiveMenuID].General_callback == NULL)
    {
        SetFadeOutFlag(ENTER_FLAGSTART);
    }
}

/*
 * @brief Handle the Back key: fade back to the parent page.
 */
void BackEventMenuItem(void)
{
    if (CurrentMenuPage->General_ParentMenuPage != NULL)
    {
        SetFadeOutFlag(BACK_FLAGSTART);
    }
    else if (OLED_UI_BackHook != NULL)
    {
        /* Back on the parentless page: let an external app react */
        OLED_UI_BackHook();
    }
}

/*
 * @brief Same as pressing the Back key.
 */
void OLED_UI_Back(void)
{
    BackEventMenuItem();
}

/*
 * @brief Run the pending item callback, then release the mutex.
 */
void RunCurrentCallBackFunction(void)
{
    if (KeyEnterFlag == FLAGSTART)
    {
        CurrentMenuPage->General_MenuItems[CurrentMenuPage->_ActiveMenuID].General_callback();
        ResetEnterFlag();
        Encoder_Enable();
    }
}

/*
 * @brief Hide the cursor highlight.
 */
void SetCursorZero(void)
{
    OLED_UI_Cursor.TargetArea.X = 0.0f;
    OLED_UI_Cursor.TargetArea.Y = 0.0f;
    OLED_UI_Cursor.TargetArea.Width = 0.0f;
    OLED_UI_Cursor.TargetArea.Height = 0.0f;
    OLED_UI_Cursor.CurrentArea.X = 0.0f;
    OLED_UI_Cursor.CurrentArea.Y = 0.0f;
    OLED_UI_Cursor.CurrentArea.Width = 0.0f;
    OLED_UI_Cursor.CurrentArea.Height = 0.0f;
}

/*
 * @brief Pop up a window centered on the screen.
 */
void OLED_UI_CreateWindow(MenuWindow *window)
{
    /* Fresh window restarts the progress bar and title scroll */
    if (CurrentWindow == NULL)
    {
        OLED_UI_ProbWidth.CurrentDistance = 0.0f;
        window->_LineSlip = 0.0f;
    }
    OLED_SustainCounter.SustainFlag = 1;
    OLED_SustainCounter.count = 0;

    OLED_UI_Window.TargetArea.Width = window->General_Width;
    OLED_UI_Window.TargetArea.Height = window->General_Height;
    OLED_UI_Window.TargetArea.X = (OLED_WIDTH - window->General_Width) / 2;
    OLED_UI_Window.TargetArea.Y = (OLED_HEIGHT - window->General_Height) / 2;

    CurrentWindow = window;
}

/*
 * @brief Run a pending page transition (enter submenu or back).
 */
void RunFadeOut(void)
{
    /* FLAGEND means no transition is pending.
     * ENTER_FLAGSTART: Enter pressed on a submenu item.
     * BACK_FLAGSTART: Back pressed with a parent page present. */
    if (FadeOutFlag != FLAGEND)
    {
        if (CurrentMenuPage->General_MenuType == MENU_TYPE_LIST)
        {
            if (FadeOutFlag == ENTER_FLAGSTART)
            {
                CurrentMenuPage->_IfInit = 1;
                /* Fade only the menu area when staying in list style */
                if (CurrentMenuPage->General_MenuItems[CurrentMenuPage->_ActiveMenuID].General_SubMenuPage->General_MenuType == MENU_TYPE_LIST)
                {
                    OLED_UI_FadeoutCurrentArea(CurrentMenuPage->List_MenuArea.X, CurrentMenuPage->List_MenuArea.Y, CurrentMenuPage->List_MenuArea.Width - 5, CurrentMenuPage->List_MenuArea.Height - 2);
                }
                else
                {
                    OLED_UI_FadeoutAllArea();
                    OLED_UI_ScrollBarHeight.CurrentDistance = 0.0f;
                }

                /* Save position for the way back */
                CurrentMenuPage->_StartPoint.X = OLED_UI_PageStartPoint.TargetPoint.X;
                CurrentMenuPage->_StartPoint.Y = OLED_UI_PageStartPoint.TargetPoint.Y;

                CurrentMenuPage = CurrentMenuPage->General_MenuItems[CurrentMenuPage->_ActiveMenuID].General_SubMenuPage;
                CurrentMenuPageInit();
            }
            if (FadeOutFlag == BACK_FLAGSTART)
            {
                if (CurrentMenuPage->General_ParentMenuPage->General_MenuType == MENU_TYPE_LIST)
                {
                    OLED_UI_FadeoutCurrentArea(CurrentMenuPage->List_MenuArea.X, CurrentMenuPage->List_MenuArea.Y, CurrentMenuPage->List_MenuArea.Width - 5, CurrentMenuPage->List_MenuArea.Height - 2);
                }
                else
                {
                    OLED_UI_FadeoutAllArea();
                    OLED_UI_ScrollBarHeight.CurrentDistance = 0.0f;
                }
                CurrentMenuPage = CurrentMenuPage->General_ParentMenuPage;
                if (CurrentMenuPage->_IfInit == 0)
                {
                    CurrentMenuPageInit();
                }
                else
                {
                    CurrentMenuPageBackUp();
                }
            }

            ResetFadeOutFlag();
            SetCursorZero();
        }
        else if (CurrentMenuPage->General_MenuType == MENU_TYPE_TILES)
        {
            OLED_UI_FadeoutAllArea();
            if (FadeOutFlag == ENTER_FLAGSTART)
            {
                CurrentMenuPage->_IfInit = 1;
                CurrentMenuPage->_StartPoint.X = OLED_UI_PageStartPoint.TargetPoint.X;
                CurrentMenuPage->_StartPoint.Y = OLED_UI_PageStartPoint.TargetPoint.Y;
                CurrentMenuPage = CurrentMenuPage->General_MenuItems[CurrentMenuPage->_ActiveMenuID].General_SubMenuPage;
                CurrentMenuPageInit();
            }
            if (FadeOutFlag == BACK_FLAGSTART)
            {
                CurrentMenuPage = CurrentMenuPage->General_ParentMenuPage;
                if (CurrentMenuPage->_IfInit == 0)
                {
                    CurrentMenuPageInit();
                }
                else
                {
                    CurrentMenuPageBackUp();
                }
            }
            OLED_UI_ScrollBarHeight.CurrentDistance = 0.0f;
            SetCursorZero();
            ResetFadeOutFlag();
        }
        Encoder_Enable();
    }
}

/*
 * @brief Age the popup window and apply encoder steps to live data.
 *        Runs in the main loop (data writes must not run in the ISR).
 */
void OLED_WindowDataDispose(void)
{
    int8_t DataStyle;
    if (CurrentWindow != NULL)
    {
        if (OLED_SustainCounter.count >= (int16_t)(CurrentWindow->General_ContinueTime * 50))
        {
            OLED_SustainCounter.SustainFlag = 0;
            OLED_SustainCounter.count = 0;
        }
    }

    DataStyle = GetWindowProbDataStyle(CurrentWindow);
    if (DataStyle != WINDOW_DATA_STYLE_NONE)
    {
        if (DataStyle == WINDOW_DATA_STYLE_FLOAT)
        {
            *CurrentWindow->Prob_Data_Float += (WindowProbDeltaData * CurrentWindow->Prob_DataStep);
            if (*CurrentWindow->Prob_Data_Float <= CurrentWindow->Prob_MinData)
            {
                *CurrentWindow->Prob_Data_Float = CurrentWindow->Prob_MinData;
            }
            if (*CurrentWindow->Prob_Data_Float >= CurrentWindow->Prob_MaxData)
            {
                *CurrentWindow->Prob_Data_Float = CurrentWindow->Prob_MaxData;
            }
        }
        else
        {
            switch (DataStyle)
            {
                case WINDOW_DATA_STYLE_INT8:
                    *CurrentWindow->Prob_Data_Int_8 += (WindowProbDeltaData * CurrentWindow->Prob_DataStep);
                    if (*CurrentWindow->Prob_Data_Int_8 <= CurrentWindow->Prob_MinData)
                    {
                        *CurrentWindow->Prob_Data_Int_8 = (int8_t)CurrentWindow->Prob_MinData;
                    }
                    if (*CurrentWindow->Prob_Data_Int_8 >= CurrentWindow->Prob_MaxData)
                    {
                        *CurrentWindow->Prob_Data_Int_8 = (int8_t)CurrentWindow->Prob_MaxData;
                    }
                    break;
                case WINDOW_DATA_STYLE_INT16:
                    *CurrentWindow->Prob_Data_Int_16 += (WindowProbDeltaData * CurrentWindow->Prob_DataStep);
                    if (*CurrentWindow->Prob_Data_Int_16 <= CurrentWindow->Prob_MinData)
                    {
                        *CurrentWindow->Prob_Data_Int_16 = (int16_t)CurrentWindow->Prob_MinData;
                    }
                    if (*CurrentWindow->Prob_Data_Int_16 >= CurrentWindow->Prob_MaxData)
                    {
                        *CurrentWindow->Prob_Data_Int_16 = (int16_t)CurrentWindow->Prob_MaxData;
                    }
                    break;
                case WINDOW_DATA_STYLE_INT32:
                    *CurrentWindow->Prob_Data_Int_32 += (WindowProbDeltaData * CurrentWindow->Prob_DataStep);
                    if (*CurrentWindow->Prob_Data_Int_32 <= CurrentWindow->Prob_MinData)
                    {
                        *CurrentWindow->Prob_Data_Int_32 = (long)CurrentWindow->Prob_MinData;
                    }
                    if (*CurrentWindow->Prob_Data_Int_32 >= CurrentWindow->Prob_MaxData)
                    {
                        *CurrentWindow->Prob_Data_Int_32 = (long)CurrentWindow->Prob_MaxData;
                    }
                    break;
                case WINDOW_DATA_STYLE_INT64:
                    /* 32-bit long on C251, same range as INT32 */
                    *CurrentWindow->Prob_Data_Int_64 += (WindowProbDeltaData * CurrentWindow->Prob_DataStep);
                    if (*CurrentWindow->Prob_Data_Int_64 <= CurrentWindow->Prob_MinData)
                    {
                        *CurrentWindow->Prob_Data_Int_64 = (long)CurrentWindow->Prob_MinData;
                    }
                    if (*CurrentWindow->Prob_Data_Int_64 >= CurrentWindow->Prob_MaxData)
                    {
                        *CurrentWindow->Prob_Data_Int_64 = (long)CurrentWindow->Prob_MaxData;
                    }
                    break;
                default:
                    break;
            }
        }
    }
    WindowProbDeltaData = 0;
}

/*
 * @brief Advance all animations and render the current page.
 */
void MoveMenuElements(void)
{
    SetTargetCursor();
    SetTargetMenuFrame();
    SetTargetProbWidth();
    SetTargetScrollBarHeight();

    ChangePoint(&OLED_UI_PageStartPoint);
    ChangeDistance(&OLED_UI_LineStep);
    ChangeDistance(&OLED_UI_ScrollBarHeight);

    ChangeArea(&OLED_UI_MenuFrame);

    PrintMenuElements();

    ChangeArea(&OLED_UI_Cursor);
    ReverseCoordinate(OLEDUI_CeilF(OLED_UI_Cursor.CurrentArea.X), OLEDUI_CeilF(OLED_UI_Cursor.CurrentArea.Y), OLEDUI_CeilF(OLED_UI_Cursor.CurrentArea.Width), OLEDUI_CeilF(OLED_UI_Cursor.CurrentArea.Height), CurrentMenuPage->General_CursorStyle);
    OLED_SetColorMode(ColorMode);

    OLED_Brightness(OLED_UI_Brightness);

    ChangeDistance(&OLED_UI_ProbWidth);
    ChangeArea(&OLED_UI_Window);

    OLED_WindowDataDispose();

    OLED_DrawWindow();
}

/*
 * @brief UI main loop: transitions, render, callbacks, present.
 *        Call every frame from main().
 */
void OLED_UI_MainLoop(void)
{
    RunFadeOut();

    OLED_Clear();

    MoveMenuElements();

    RunCurrentCallBackFunction();

    OLED_Update();
}

/*
 * @brief UI tick handler. Call every 20 ms from the Timer11 ISR or
 *        from main loop polling. Only samples input and moves the
 *        selection; all rendering stays in the main loop.
 */
void OLED_UI_InterruptHandler(void)
{
    MenuWindow *window;
    MenuID_Type IncreaseID;
    int16_t i;
    unsigned char IdleActivity;
    window = CurrentWindow;
    IdleActivity = 0;
    /* Skip input while a callback or transition owns the UI */
    if (GetEnterFlag() && GetFadeoutFlag())
    {
        /* Safe delta moves the cursor, unsafe delta adjusts windows */
        IncreaseID = OLED_KeyAndEncoderRecord();

        /* While a window is up, encoder steps adjust live data */
        if (OLED_SustainCounter.SustainFlag == 1)
        {
            if (IncreaseID.Unsafe != 0)
            {
                OLED_SustainCounter.count = 0;
                IdleActivity = 1;
            }
            WindowProbDeltaData += IncreaseID.Unsafe;
            IncreaseID.Safe = 0;
        }
        if (IncreaseID.Safe != 0)
        {
            IdleActivity = 1;
        }

        /* Negative delta means repeated Up presses */
        if (IncreaseID.Safe < 0)
        {
            for (i = 0; i < -IncreaseID.Safe; i++)
            {
                if (CurrentMenuPage->General_MenuType == MENU_TYPE_LIST)
                {
                    /* Scrolled to top but not on the first item: move page */
                    if (CurrentMenuPage->_Slot == 0 && CurrentMenuPage->_ActiveMenuID != 0)
                    {
                        MenuItemsMoveDown();
                    }
                    /* Cursor not at the top slot: move slot up */
                    if (CurrentMenuPage->_Slot > 0)
                    {
                        CurrentMenuPage->_Slot--;
                    }
                    CurrentMenuPage->_ActiveMenuID--;
                }
                if (CurrentMenuPage->General_MenuType == MENU_TYPE_TILES)
                {
                    CurrentMenuPage->_ActiveMenuID--;
                    MenuItemsMoveRight();
                }
            }
        }
        if (IncreaseID.Safe > 0)
        {
            for (i = 0; i < IncreaseID.Safe; i++)
            {
                if (CurrentMenuPage->General_MenuType == MENU_TYPE_LIST)
                {
                    /* Scrolled to bottom and not on the last item: move page */
                    if (CurrentMenuPage->_Slot == GetCurrentMenuPageMaxSlotNum() - 1 && CurrentMenuPage->_ActiveMenuID != GetMenuItemNum(CurrentMenuPage->General_MenuItems))
                    {
                        MenuItemsMoveUp();
                    }
                    /* Cursor not at the bottom slot: move slot down */
                    if (CurrentMenuPage->_Slot < GetCurrentMenuPageMaxSlotNum() - 1)
                    {
                        CurrentMenuPage->_Slot++;
                    }
                    CurrentMenuPage->_ActiveMenuID++;
                }
                if (CurrentMenuPage->General_MenuType == MENU_TYPE_TILES)
                {
                    CurrentMenuPage->_ActiveMenuID++;
                    MenuItemsMoveLeft();
                }
            }
        }

        /* Released Back key: go back, or keep a live window longer */
        if (OLED_UI_Key.Back != OLED_UI_LastKey.Back && OLED_UI_Key.Back == 1)
        {
            IdleActivity = 1;
            if (OLED_SustainCounter.SustainFlag != 1)
            {
                BackEventMenuItem();
            }
            else
            {
                OLED_SustainCounter.count = (int16_t)(window->General_ContinueTime * 50);
            }
        }
        /* Released Enter key: confirm, toggle radio, or sustain window */
        if (OLED_UI_Key.Enter != OLED_UI_LastKey.Enter && OLED_UI_Key.Enter == 1)
        {
            IdleActivity = 1;
            if (OLED_SustainCounter.SustainFlag != 1)
            {
                EnterEventMenuItem();
                if (CurrentMenuPage->General_MenuItems[CurrentMenuPage->_ActiveMenuID].List_BoolRadioBox != NULL)
                {
                    *CurrentMenuPage->General_MenuItems[CurrentMenuPage->_ActiveMenuID].List_BoolRadioBox = !(*CurrentMenuPage->General_MenuItems[CurrentMenuPage->_ActiveMenuID].List_BoolRadioBox);
                }
            }
            else
            {
                OLED_SustainCounter.count = 0;
            }
        }
    }

    /* Age a live window */
    if (OLED_SustainCounter.SustainFlag == 1)
    {
        OLED_SustainCounter.count++;
    }

    /* Idle watchdog: reset on input, count silent ticks, fire hook */
    if (IdleActivity != 0)
    {
        OLED_UI_IdleTicks = 0;
    }
    else if (GetEnterFlag() && GetFadeoutFlag())
    {
        if (OLED_UI_IdleTicks < 0xFFFF)
        {
            OLED_UI_IdleTicks++;
        }
    }
    if (OLED_UI_IdleTimeout != 0U
        && CurrentWindow == NULL
        && GetEnterFlag() && GetFadeoutFlag()
        && OLED_UI_IdleTicks >= OLED_UI_IdleTimeout)
    {
        OLED_UI_IdleTicks = 0;
        if (OLED_UI_IdleHook != NULL)
        {
            OLED_UI_IdleHook();
        }
    }
}
