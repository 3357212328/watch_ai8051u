#include "oled_ui.h"
#include "watch_face.h"
#include "watch_clock.h"
#include "watch_icons.h"

/* Menu content: windows, pages and items (all positional C89 initializers).
 * Field order follows the structs in oled_ui.h.
 * MenuWindow: W, H, ContinueTime, WinType, Text, FontSize, SideDist,
 *   TopDist, ProbFloat, ProbI8, ProbI16, ProbI32, ProbI64(long*),
 *   DataStep, Min, Max, BottomDist, SideDist, LineH, _LineSlip.
 * MenuPage: Type, Speed, Cursor, Move, FontSize, Parent, Items, LineSpace,
 *   AuxFunc, {Area X,Y,W,H}, DrawFrame, StartX, StartY,
 *   ScrW, ScrH, TileW, TileH, ActiveID, Slot, {Start X,Y}, IfInit.
 * MenuItem: text, callback, submenu, radio, icon, _LineSlip.
 */

#define SPEED 10

/* Brightness adjust window, binds the live brightness value */
MenuWindow xdata SetBrightnessWindow = {
    80, 28, 4.0f, WINDOW_ROUNDRECTANGLE,
    (char code *)"Brightness", OLED_UI_FONT_12, 4, 3,
    (float edata *)0, (int8_t edata *)0, &OLED_UI_Brightness, (int32_t edata *)0, (long edata *)0,
    5.0f, 5.0f, 255.0f, 3, 4, 8, 0.0f
};

/*
 * @brief Pop up the brightness adjust window.
 */
void BrightnessWindow(void)
{
    OLED_UI_CreateWindow(&SetBrightnessWindow);
}

/* Time Set edit windows, bind the staging values in watch_face.c.
 * Every Show*Window first calls Watch_TimeSet_Touch(field) so the field
 * being edited starts from the live RTC value, not from the power on
 * snapshot. field: 0 Year, 1 Month, 2 Day, 3 Hour, 4 Minute, 5 Second. */
MenuWindow xdata WinTimeHour = {
    80, 28, 6.0f, WINDOW_ROUNDRECTANGLE,
    (char code *)"Hour", OLED_UI_FONT_12, 4, 3,
    (float edata *)0, (int8_t edata *)0, &Watch_StageHour, (int32_t edata *)0, (long edata *)0,
    1.0f, 0.0f, 23.0f, 3, 4, 8, 0.0f
};

MenuWindow xdata WinTimeMinute = {
    80, 28, 6.0f, WINDOW_ROUNDRECTANGLE,
    (char code *)"Minute", OLED_UI_FONT_12, 4, 3,
    (float edata *)0, (int8_t edata *)0, &Watch_StageMinute, (int32_t edata *)0, (long edata *)0,
    1.0f, 0.0f, 59.0f, 3, 4, 8, 0.0f
};

MenuWindow xdata WinTimeSecond = {
    80, 28, 6.0f, WINDOW_ROUNDRECTANGLE,
    (char code *)"Second", OLED_UI_FONT_12, 4, 3,
    (float edata *)0, (int8_t edata *)0, &Watch_StageSecond, (int32_t edata *)0, (long edata *)0,
    1.0f, 0.0f, 59.0f, 3, 4, 8, 0.0f
};

MenuWindow xdata WinTimeYear = {
    80, 28, 6.0f, WINDOW_ROUNDRECTANGLE,
    (char code *)"Year", OLED_UI_FONT_12, 4, 3,
    (float edata *)0, (int8_t edata *)0, &Watch_StageYear, (int32_t edata *)0, (long edata *)0,
    1.0f, 2000.0f, 2099.0f, 3, 4, 8, 0.0f
};

MenuWindow xdata WinTimeMonth = {
    80, 28, 6.0f, WINDOW_ROUNDRECTANGLE,
    (char code *)"Month", OLED_UI_FONT_12, 4, 3,
    (float edata *)0, (int8_t edata *)0, &Watch_StageMonth, (int32_t edata *)0, (long edata *)0,
    1.0f, 1.0f, 12.0f, 3, 4, 8, 0.0f
};

MenuWindow xdata WinTimeDay = {
    80, 28, 6.0f, WINDOW_ROUNDRECTANGLE,
    (char code *)"Day", OLED_UI_FONT_12, 4, 3,
    (float edata *)0, (int8_t edata *)0, &Watch_StageDay, (int32_t edata *)0, (long edata *)0,
    1.0f, 1.0f, 31.0f, 3, 4, 8, 0.0f
};

/* Time Set done popup, text only. Width 96: "Time saved!" at 11x7 px
 * = 77 px does not fit the 74 px usable width at 90, so widen to 96
 * (8 px side distance gives 80 px; centered x 16..112 fits the 128 screen). */
MenuWindow xdata TimeSetDoneWindow = {
    96, 28, 3.0f, WINDOW_ROUNDRECTANGLE,
    (char code *)"Time saved!", OLED_UI_FONT_12, 8, 3,
    (float edata *)0, (int8_t edata *)0, (int16_t edata *)0, (int32_t edata *)0, (long edata *)0,
    0.0f, 0.0f, 0.0f, 0, 0, 0, 0.0f
};

/*
 * @brief Pop up the hour edit window.
 */
void ShowTimeHourWindow(void)
{
    Watch_TimeSet_Touch(3);
    OLED_UI_CreateWindow(&WinTimeHour);
}

/*
 * @brief Pop up the minute edit window.
 */
void ShowTimeMinuteWindow(void)
{
    Watch_TimeSet_Touch(4);
    OLED_UI_CreateWindow(&WinTimeMinute);
}

/*
 * @brief Pop up the second edit window.
 */
void ShowTimeSecondWindow(void)
{
    Watch_TimeSet_Touch(5);
    OLED_UI_CreateWindow(&WinTimeSecond);
}

/*
 * @brief Pop up the year edit window.
 */
void ShowTimeYearWindow(void)
{
    Watch_TimeSet_Touch(0);
    OLED_UI_CreateWindow(&WinTimeYear);
}

/*
 * @brief Pop up the month edit window.
 */
void ShowTimeMonthWindow(void)
{
    Watch_TimeSet_Touch(1);
    OLED_UI_CreateWindow(&WinTimeMonth);
}

/*
 * @brief Pop up the day edit window.
 */
void ShowTimeDayWindow(void)
{
    Watch_TimeSet_Touch(2);
    OLED_UI_CreateWindow(&WinTimeDay);
}

/*
 * @brief Save the staged time and pop up the done window.
 */
void TimeSetSave(void)
{
    Watch_TimeSet_Save();
    OLED_UI_CreateWindow(&TimeSetDoneWindow);
}

/* Alarm clock edit windows, bind the staging values in watch_clock.c */
MenuWindow xdata WinAlarmHour = {
    80, 28, 6.0f, WINDOW_ROUNDRECTANGLE,
    (char code *)"Hour", OLED_UI_FONT_12, 4, 3,
    (float edata *)0, (int8_t edata *)0, &Watch_StageAlarmHour, (int32_t edata *)0, (long edata *)0,
    1.0f, 0.0f, 23.0f, 3, 4, 8, 0.0f
};

MenuWindow xdata WinAlarmMinute = {
    80, 28, 6.0f, WINDOW_ROUNDRECTANGLE,
    (char code *)"Minute", OLED_UI_FONT_12, 4, 3,
    (float edata *)0, (int8_t edata *)0, &Watch_StageAlarmMinute, (int32_t edata *)0, (long edata *)0,
    1.0f, 0.0f, 59.0f, 3, 4, 8, 0.0f
};

/* Countdown edit windows, bind the staging values in watch_clock.c */
MenuWindow xdata WinCDownMin = {
    80, 28, 6.0f, WINDOW_ROUNDRECTANGLE,
    (char code *)"Minute", OLED_UI_FONT_12, 4, 3,
    (float edata *)0, (int8_t edata *)0, &Watch_StageCountMin, (int32_t edata *)0, (long edata *)0,
    1.0f, 0.0f, 99.0f, 3, 4, 8, 0.0f
};

MenuWindow xdata WinCDownSec = {
    80, 28, 6.0f, WINDOW_ROUNDRECTANGLE,
    (char code *)"Second", OLED_UI_FONT_12, 4, 3,
    (float edata *)0, (int8_t edata *)0, &Watch_StageCountSec, (int32_t edata *)0, (long edata *)0,
    1.0f, 0.0f, 59.0f, 3, 4, 8, 0.0f
};

/* Zero-countdown guard popup, text only. "SET TIME!" = 9 * 7 px = 63,
 * fits the same 64 px usable width. */
MenuWindow xdata SetTimeWindow = {
    80, 28, 3.0f, WINDOW_ROUNDRECTANGLE,
    (char code *)"SET TIME!", OLED_UI_FONT_12, 8, 3,
    (float edata *)0, (int8_t edata *)0, (int16_t edata *)0, (int32_t edata *)0, (long edata *)0,
    0.0f, 0.0f, 0.0f, 0, 0, 0, 0.0f
};

/*
 * @brief Pop up the alarm hour edit window.
 */
void ShowAlarmHourWindow(void)
{
    OLED_UI_CreateWindow(&WinAlarmHour);
}

/*
 * @brief Pop up the alarm minute edit window.
 */
void ShowAlarmMinuteWindow(void)
{
    OLED_UI_CreateWindow(&WinAlarmMinute);
}

/*
 * @brief Pop up the countdown minute edit window.
 */
void ShowCDownMinWindow(void)
{
    OLED_UI_CreateWindow(&WinCDownMin);
}

/*
 * @brief Pop up the countdown second edit window.
 */
void ShowCDownSecWindow(void)
{
    OLED_UI_CreateWindow(&WinCDownSec);
}

/*
 * @brief Countdown Start item: with a zero staging run time the start
 *        is blocked with the SET TIME! popup, otherwise the countdown
 *        app opens. The reset drops a carried over remaining time first,
 *        so Start always arms the countdown from the staging values.
 */
void CountDownStartMenuCM(void)
{
    if ((Watch_StageCountMin <= 0) && (Watch_StageCountSec <= 0))
    {
        OLED_UI_CreateWindow(&SetTimeWindow);
    }
    else
    {
        Watch_Countdown_Reset();
        Watch_OpenCountdown();
    }
}

/* Logo animation anchors for the auxiliary draw functions */
OLED_ChangePoint LogoMove;
OLED_ChangePoint LogoTextMove;
OLED_ChangePoint WelcomeTextMove;

/* Settings page side picture: logo slides in once the page settles */
void SettingAuxFunc(void)
{
    if (OLEDUI_AbsF(OLED_UI_PageStartPoint.CurrentPoint.X - OLED_UI_PageStartPoint.TargetPoint.X) < 4.0f)
    {
        LogoMove.TargetPoint.X = 0.0f;
        LogoMove.TargetPoint.Y = 0.0f;
    }
    LogoTextMove.TargetPoint.X = 129.0f;
    LogoTextMove.TargetPoint.Y = 0.0f;
    WelcomeTextMove.TargetPoint.X = 128.0f;
    WelcomeTextMove.TargetPoint.Y = 0.0f;
    ChangePoint(&LogoMove);
    OLED_ShowImageArea(OLEDUI_CeilF(LogoMove.CurrentPoint.X), OLEDUI_CeilF(LogoMove.CurrentPoint.Y), 32, 64, 0, 0, 128, 128, OLED_UI_SettingsLogo);
    ChangePoint(&LogoTextMove);
    OLED_ShowImageArea(OLEDUI_CeilF(LogoTextMove.CurrentPoint.X), OLEDUI_CeilF(LogoTextMove.CurrentPoint.Y), 26, 64, 0, 0, 128, 128, OLED_UI_LOGOTEXT64);
    ChangePoint(&WelcomeTextMove);
    OLED_ShowImageArea(OLEDUI_CeilF(WelcomeTextMove.CurrentPoint.X), OLEDUI_CeilF(WelcomeTextMove.CurrentPoint.Y), 16, 64, 0, 0, 128, 128, OLED_UI_LOGOGithub);
}

/* Main page: park all logos off screen */
void MainAuxFunc(void)
{
    LogoMove.TargetPoint.X = -200.0f;
    LogoMove.TargetPoint.Y = 0.0f;
    LogoMove.CurrentPoint.X = -200.0f;
    LogoMove.CurrentPoint.Y = 0.0f;

    LogoTextMove.TargetPoint.X = 129.0f;
    LogoTextMove.TargetPoint.Y = 0.0f;
    LogoTextMove.CurrentPoint.X = 129.0f;
    LogoTextMove.CurrentPoint.Y = 0.0f;

    WelcomeTextMove.TargetPoint.X = 128.0f;
    WelcomeTextMove.TargetPoint.Y = 0.0f;
    WelcomeTextMove.CurrentPoint.X = 128.0f;
    WelcomeTextMove.CurrentPoint.Y = 0.0f;
}

/* Main tiles menu */
MenuItem xdata MainMenuItems[] = {
    {(char code *)"Watch Face", Watch_OpenFace, NULL, (unsigned char edata *)0, Image_watch, 0.0f},
    {(char code *)"Settings", NULL, &SettingsMenuPage, (unsigned char edata *)0, Image_setings, 0.0f},
    {(char code *)"Calendar", Watch_OpenCalendar, NULL, (unsigned char edata *)0, Image_calendar, 0.0f},
    {(char code *)"Alarm", NULL, &AlarmMenuPage, (unsigned char edata *)0, Image_alarm, 0.0f},
    {(char code *)"Count Down", NULL, &CountdownMenuPage, (unsigned char edata *)0, Image_cdown, 0.0f},
    {(char code *)"Up Timer", Watch_OpenUptimer, NULL, (unsigned char edata *)0, Image_uptime, 0.0f},
    {(char code *)"Dino Game", Watch_OpenDino, NULL, (unsigned char edata *)0, Image_dino, 0.0f},
    {(char code *)"Plane War", Watch_OpenPlane, NULL, (unsigned char edata *)0, Image_plane, 0.0f},
    {(char code *)"More", NULL, &MoreMenuPage, (unsigned char edata *)0, Image_more, 0.0f},
    {(char code *)0, NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f}
};

/* Settings list menu */
MenuItem xdata SettingsMenuItems[] = {
    {(char code *)"Brightness", BrightnessWindow, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Dark Mode", NULL, NULL, &ColorMode, (const uint8_t code *)0, 0.0f},
    {(char code *)"Time Set", NULL, &TimeSetMenuPage, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Back", OLED_UI_Back, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)0, NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f}
};

MenuItem xdata MoreMenuItems[] = {
    {(char code *)"Back", OLED_UI_Back, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Stopwatch", Watch_OpenStopwatch, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)0, NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f}
};









/* Main tiles menu.
 * LineSpace 3: 32 px tile + 3 px gap = 35 px step, so the third tile
 * keeps a visible slice on screen as a hint that the strip goes on.
 * Cursor: default REVERSE_ROUNDRECTANGLE inverts the active label band
 * (drawn by the framework), PrintMenuElements adds the focus ring. */
MenuPage xdata MainMenuPage = {
    MENU_TYPE_TILES, SPEED, REVERSE_ROUNDRECTANGLE, UNLINEAR, OLED_UI_FONT_16,
    NULL, MainMenuItems, 3, MainAuxFunc,
    {0.0f, 0.0f, 0.0f, 0.0f}, 0, 0, 0,
    128, 64, 32, 32,
    0, 0, {0.0f, 0.0f}, 0
};

MenuPage xdata SettingsMenuPage = {
    MENU_TYPE_LIST, SPEED, REVERSE_ROUNDRECTANGLE, UNLINEAR, OLED_UI_FONT_12,
    &MainMenuPage, SettingsMenuItems, 4, SettingAuxFunc,
    {32.0f, 0.0f, 95.0f, 64.0f}, 0, 4, 2,
    0, 0, 0, 0,
    0, 0, {0.0f, 0.0f}, 0
};

MenuPage xdata MoreMenuPage = {
    MENU_TYPE_LIST, SPEED, REVERSE_ROUNDRECTANGLE, UNLINEAR, OLED_UI_FONT_12,
    &MainMenuPage, MoreMenuItems, 4, NULL,
    {1.0f, 1.0f, 128.0f, 64.0f}, 0, 4, 2,
    0, 0, 0, 0,
    0, 0, {0.0f, 0.0f}, 0
};









/* Time Set list menu */
MenuItem xdata TimeSetMenuItems[] = {
    {(char code *)"Back", OLED_UI_Back, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Hour", ShowTimeHourWindow, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Minute", ShowTimeMinuteWindow, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Second", ShowTimeSecondWindow, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Year", ShowTimeYearWindow, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Month", ShowTimeMonthWindow, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Day", ShowTimeDayWindow, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Save Time", TimeSetSave, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)0, NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f}
};

MenuPage xdata TimeSetMenuPage = {
    MENU_TYPE_LIST, SPEED, REVERSE_ROUNDRECTANGLE, UNLINEAR, OLED_UI_FONT_12,
    &SettingsMenuPage, TimeSetMenuItems, 4, NULL,
    {32.0f, 0.0f, 95.0f, 64.0f}, 0, 4, 2,
    0, 0, 0, 0,
    0, 0, {0.0f, 0.0f}, 0
};

/* Alarm clock list menu (submenu of Main) */
MenuItem xdata AlarmMenuItems[] = {
    {(char code *)"Back", OLED_UI_Back, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Hour", ShowAlarmHourWindow, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Minute", ShowAlarmMinuteWindow, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    /* Radio item: the box on the right shows the live alarm state, the
     * framework flips the flag by itself, so no callback here. */
    {(char code *)"ON / OFF", NULL, NULL, &ClkAlarmEnabled, (const uint8_t code *)0, 0.0f},
    {(char code *)0, NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f}
};

MenuPage xdata AlarmMenuPage = {
    MENU_TYPE_LIST, SPEED, REVERSE_ROUNDRECTANGLE, UNLINEAR, OLED_UI_FONT_12,
    &MainMenuPage, AlarmMenuItems, 4, NULL,
    {32.0f, 0.0f, 95.0f, 64.0f}, 0, 4, 2,
    0, 0, {0.0f, 0.0f}, 0
};

/* Countdown list menu (submenu of Main) */
MenuItem xdata CountdownMenuItems[] = {
    {(char code *)"Back", OLED_UI_Back, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Minute", ShowCDownMinWindow, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Second", ShowCDownSecWindow, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Start", CountDownStartMenuCM, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)0, NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f}
};

MenuPage xdata CountdownMenuPage = {
    MENU_TYPE_LIST, SPEED, REVERSE_ROUNDRECTANGLE, UNLINEAR, OLED_UI_FONT_12,
    &MainMenuPage, CountdownMenuItems, 4, NULL,
    {32.0f, 0.0f, 95.0f, 64.0f}, 0, 4, 2,
    0, 0, {0.0f, 0.0f}, 0
};

