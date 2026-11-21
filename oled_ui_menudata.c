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
 *   AuxFunc, {Area X,Y,W,H}, DrawFrame, DrawPrefix, StartX, StartY,
 *   ScrW, ScrH, TileW, TileH, ActiveID, Slot, {Start X,Y}, IfInit.
 * MenuItem: text, callback, submenu, radio, icon, _LineSlip.
 */

float edata testfloatnum = 0.5f;
long edata testintnum = 1;
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

/* Plain window without text or data */
MenuWindow xdata NullWindow = {
    80, 28, 4.0f, WINDOW_ROUNDRECTANGLE,
    (char code *)0, 0, 0, 0,
    (float edata *)0, (int8_t edata *)0, (int16_t edata *)0, (int32_t edata *)0, (long edata *)0,
    0.0f, 0.0f, 0.0f, 0, 0, 0, 0.0f
};

/*
 * @brief Pop up the empty window demo.
 */
void EmptyWindow(void)
{
    OLED_UI_CreateWindow(&NullWindow);
}

/* Text only window */
MenuWindow xdata TextWindow = {
    75, 18, 4.0f, WINDOW_ROUNDRECTANGLE,
    (char code *)"Hello text", OLED_UI_FONT_12, 8, 2,
    (float edata *)0, (int8_t edata *)0, (int16_t edata *)0, (int32_t edata *)0, (long edata *)0,
    0.0f, 0.0f, 0.0f, 0, 0, 0, 0.0f
};

/*
 * @brief Pop up the text window demo.
 */
void ShowTextWindow(void)
{
    OLED_UI_CreateWindow(&TextWindow);
}

/* Float live data window */
MenuWindow xdata FloatDataWindow = {
    80, 28, 4.0f, WINDOW_ROUNDRECTANGLE,
    (char code *)"Float Demo", OLED_UI_FONT_12, 4, 3,
    &testfloatnum, (int8_t edata *)0, (int16_t edata *)0, (int32_t edata *)0, (long edata *)0,
    0.1f, -100.0f, 100.0f, 3, 4, 8, 0.0f
};

/*
 * @brief Pop up the float data window demo.
 */
void ShowFloatDataWindow(void)
{
    OLED_UI_CreateWindow(&FloatDataWindow);
}

/* Integer live data window */
MenuWindow xdata IntDataWindow = {
    80, 28, 4.0f, WINDOW_ROUNDRECTANGLE,
    (char code *)"Int Demo", OLED_UI_FONT_12, 4, 3,
    (float edata *)0, (int8_t edata *)0, (int16_t edata *)0, &testintnum, (long edata *)0,
    1.0f, -100.0f, 100.0f, 3, 4, 8, 0.0f
};

/*
 * @brief Pop up the integer data window demo.
 */
void ShowIntDataWindow(void)
{
    OLED_UI_CreateWindow(&IntDataWindow);
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
 *        so Start! always arms the countdown from the staging values.
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

/* Device page side picture */
void AboutThisDeviceAuxFunc(void)
{
    LogoMove.TargetPoint.X = 0.0f;
    LogoMove.TargetPoint.Y = -80.0f;
    ChangePoint(&LogoMove);
    OLED_ShowImageArea(OLEDUI_CeilF(LogoMove.CurrentPoint.X), OLEDUI_CeilF(LogoMove.CurrentPoint.Y), 32, 64, 0, 0, 128, 128, OLED_UI_SettingsLogo);
    if (OLEDUI_AbsF(OLED_UI_PageStartPoint.CurrentPoint.X - OLED_UI_PageStartPoint.TargetPoint.X) < 4.0f)
    {
        LogoTextMove.TargetPoint.X = 102.0f;
        LogoTextMove.TargetPoint.Y = 0.0f;
    }
    ChangePoint(&LogoTextMove);
    OLED_ShowImageArea(OLEDUI_CeilF(LogoTextMove.CurrentPoint.X), OLEDUI_CeilF(LogoTextMove.CurrentPoint.Y), 26, 64, 0, 0, 128, 128, OLED_UI_LOGOTEXT64);
}

/* About page side picture */
void AboutOLED_UIAuxFunc(void)
{
    LogoMove.TargetPoint.X = 0.0f;
    LogoMove.TargetPoint.Y = -80.0f;
    ChangePoint(&LogoMove);
    OLED_ShowImageArea(OLEDUI_CeilF(LogoMove.CurrentPoint.X), OLEDUI_CeilF(LogoMove.CurrentPoint.Y), 32, 64, 0, 0, 128, 128, OLED_UI_SettingsLogo);
    if (OLEDUI_AbsF(OLED_UI_PageStartPoint.CurrentPoint.X - OLED_UI_PageStartPoint.TargetPoint.X) < 4.0f)
    {
        WelcomeTextMove.TargetPoint.X = 110.0f;
        WelcomeTextMove.TargetPoint.Y = 0.0f;
    }
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
    {(char code *)"More...", NULL, &MoreMenuPage, (unsigned char edata *)0, Image_more, 0.0f},
    {(char code *)0, NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f}
};

/* Settings list menu */
MenuItem xdata SettingsMenuItems[] = {
    {(char code *)"Brightness", BrightnessWindow, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Dark Mode", NULL, NULL, &ColorMode, (const uint8_t code *)0, 0.0f},
    {(char code *)"Show FPS", NULL, NULL, &OLED_UI_ShowFpsFlag, (const uint8_t code *)0, 0.0f},
    {(char code *)"About Device", NULL, &AboutThisDeviceMenuPage, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"About OLED UI", NULL, &AboutOLED_UIMenuPage, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Time Set", NULL, &TimeSetMenuPage, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Thanks for watching!", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"[Back]", OLED_UI_Back, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)0, NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f}
};

MenuItem xdata AboutThisDeviceMenuItems[] = {
    {(char code *)"-[MCU:]", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)" AI8051U", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)" 8051 C251 Core", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)" MDU+TFPU Inside", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"-[Screen:]", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)" SSD1306 128x64 OLED", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"-[Link:]", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)" I2C OLED", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"[Back]", OLED_UI_Back, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)0, NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f}
};

MenuItem xdata AboutOLED_UIMenuItems[] = {
    {(char code *)"-[Author:]", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)" bdth-7777777", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"-[Address:]", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)" github.com/bdth-7777777/OLED_UI", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"[Back]", OLED_UI_Back, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)0, NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f}
};

MenuItem xdata MoreMenuItems[] = {
    {(char code *)"[Back]", OLED_UI_Back, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Stopwatch", Watch_OpenStopwatch, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Font8 demo", NULL, &Font8MenuPage, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Font12 demo", NULL, &Font12MenuPage, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Font16 demo", NULL, &Font16MenuPage, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Font20 demo", NULL, &Font20MenuPage, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Long text demo", NULL, &LongMenuPage, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Spring demo", NULL, &SpringMenuPage, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Long list demo", NULL, &LongListMenuPage, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Small area demo", NULL, &SmallAreaMenuPage, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Empty window demo", EmptyWindow, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Text window demo", ShowTextWindow, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Float window demo", ShowFloatDataWindow, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Int window demo", ShowIntDataWindow, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)0, NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f}
};

MenuItem xdata Font8MenuItems[] = {
    {(char code *)"[Back]", OLED_UI_Back, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Hello World", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"English Text", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"1234567890", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"ABCDEFGHIJKLMNOPQRSTUVWXYZ", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"abcdefghijklmnopqrstuvwxyz", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)",.[]!@#$+-/^&*()", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Sample Text", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"English Text", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"1234567890", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"ABCDEFGHIJKLMNOPQRSTUVWXYZ", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"abcdefghijklmnopqrstuvwxyz", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)",.[]!@#$+-/^&*()", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)0, NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f}
};

MenuItem xdata Font12MenuItems[] = {
    {(char code *)"[Back]", OLED_UI_Back, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Hello World", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"English Text", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"1234567890", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"ABCDEFGHIJKLMNOPQRSTUVWXYZ", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"abcdefghijklmnopqrstuvwxyz", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)",.[]!@#$+-/^&*()", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Sample Text", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"English Text", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"1234567890", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"ABCDEFGHIJKLMNOPQRSTUVWXYZ", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"abcdefghijklmnopqrstuvwxyz", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)",.[]!@#$+-/^&*()", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)0, NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f}
};

MenuItem xdata Font16MenuItems[] = {
    {(char code *)"[Back]", OLED_UI_Back, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Hello World", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"English Text", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"1234567890", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"ABCDEFGHIJKLMNOPQRSTUVWXYZ", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"abcdefghijklmnopqrstuvwxyz", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)",.[]!@#$+-/^&*()", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Sample Text", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"English Text", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"1234567890", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"ABCDEFGHIJKLMNOPQRSTUVWXYZ", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"abcdefghijklmnopqrstuvwxyz", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)",.[]!@#$+-/^&*()", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)0, NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f}
};

MenuItem xdata Font20MenuItems[] = {
    {(char code *)"[Back]", OLED_UI_Back, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Hello World", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"English Text", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"1234567890", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"ABCDEFGHIJKLMNOPQRSTUVWXYZ", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"abcdefghijklmnopqrstuvwxyz", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)",.[]!@#$+-/^&*()", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Sample Text", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"English Text", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"1234567890", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"ABCDEFGHIJKLMNOPQRSTUVWXYZ", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"abcdefghijklmnopqrstuvwxyz", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)",.[]!@#$+-/^&*()", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)0, NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f}
};

MenuItem xdata LongMenuItems[] = {
    {(char code *)"[Back]", OLED_UI_Back, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Very Very Very Long Sample Text", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Very Very Very Long English Text", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"1 2 3 4 5 6 7 8 9 10 11 12 13 14 15", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)0, NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f}
};

MenuItem xdata SpringMenuItems[] = {
    {(char code *)"[Back]", OLED_UI_Back, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Hello World", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"English Text", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"1234567890", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"ABCDEFGHIJKLMNOPQRSTUVWXYZ", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"abcdefghijklmnopqrstuvwxyz", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)",.[]!@#$+-/^&*()", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Sample Text", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"English Text", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"1234567890", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"ABCDEFGHIJKLMNOPQRSTUVWXYZ", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"abcdefghijklmnopqrstuvwxyz", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)",.[]!@#$+-/^&*()", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)0, NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f}
};

MenuItem xdata LongListMenuItems[] = {
    {(char code *)"[Back]", OLED_UI_Back, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item1", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item2", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item3", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item4", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item5", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item6", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item7", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item8", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item9", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item10", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item11", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item12", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item13", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item14", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item15", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item16", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item17", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item18", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item19", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item20", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item21", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item22", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item23", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item24", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item25", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item26", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item27", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item28", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item29", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item30", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item31", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item32", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item33", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item34", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item35", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item36", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item37", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item38", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item39", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item40", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item41", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item42", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item43", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item44", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item45", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item46", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item47", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item48", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item49", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item50", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item51", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item52", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item53", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item54", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item55", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item56", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item57", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item58", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item59", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item60", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item61", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item62", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item63", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item64", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item65", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item66", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item67", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item68", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item69", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item70", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item71", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item72", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item73", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item74", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item75", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item76", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item77", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item78", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item79", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item80", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item81", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item82", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item83", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item84", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item85", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item86", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item87", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item88", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item89", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item90", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item91", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item92", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item93", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item94", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item95", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item96", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item97", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item98", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item99", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item100", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item101", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item102", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item103", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item104", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item105", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item106", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item107", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item108", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item109", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item110", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item111", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item112", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item113", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item114", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item115", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item116", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item117", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item118", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item119", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item120", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item121", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item122", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item123", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item124", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item125", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item126", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item127", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item128", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Item129", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)0, NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f}
};

MenuItem xdata SmallAreaMenuItems[] = {
    {(char code *)"[Back]", OLED_UI_Back, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Hello World", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"English Text", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"1234567890", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"ABCDEFGHIJKLMNOPQRSTUVWXYZ", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"abcdefghijklmnopqrstuvwxyz", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)",.[]!@#$+-/^&*()", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Sample Text", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"English Text", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"1234567890", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"ABCDEFGHIJKLMNOPQRSTUVWXYZ", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"abcdefghijklmnopqrstuvwxyz", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)",.[]!@#$+-/^&*()", NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)0, NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f}
};

/* Main tiles menu.
 * LineSpace 3: 32 px tile + 3 px gap = 35 px step, so the third tile
 * keeps a visible slice on screen as a hint that the strip goes on. */
MenuPage xdata MainMenuPage = {
    MENU_TYPE_TILES, SPEED, NOT_SHOW, UNLINEAR, OLED_UI_FONT_16,
    NULL, MainMenuItems, 3, MainAuxFunc,
    {0.0f, 0.0f, 0.0f, 0.0f}, 0, 0, 0, 0,
    128, 64, 32, 32,
    0, 0, {0.0f, 0.0f}, 0
};

MenuPage xdata SettingsMenuPage = {
    MENU_TYPE_LIST, SPEED, REVERSE_ROUNDRECTANGLE, UNLINEAR, OLED_UI_FONT_12,
    &MainMenuPage, SettingsMenuItems, 4, SettingAuxFunc,
    {32.0f, 0.0f, 95.0f, 64.0f}, 0, 1, 4, 2,
    0, 0, 0, 0,
    0, 0, {0.0f, 0.0f}, 0
};

MenuPage xdata AboutThisDeviceMenuPage = {
    MENU_TYPE_LIST, SPEED, REVERSE_BLOCK, UNLINEAR, OLED_UI_FONT_12,
    &SettingsMenuPage, AboutThisDeviceMenuItems, 4, AboutThisDeviceAuxFunc,
    {0.0f, 0.0f, 100.0f, 64.0f}, 0, 0, 4, 2,
    0, 0, 0, 0,
    0, 0, {0.0f, 0.0f}, 0
};

MenuPage xdata AboutOLED_UIMenuPage = {
    MENU_TYPE_LIST, SPEED, REVERSE_BLOCK, UNLINEAR, OLED_UI_FONT_12,
    &SettingsMenuPage, AboutOLED_UIMenuItems, 4, AboutOLED_UIAuxFunc,
    {0.0f, 0.0f, 105.0f, 64.0f}, 0, 0, 4, 2,
    0, 0, 0, 0,
    0, 0, {0.0f, 0.0f}, 0
};

MenuPage xdata MoreMenuPage = {
    MENU_TYPE_LIST, SPEED, REVERSE_ROUNDRECTANGLE, UNLINEAR, OLED_UI_FONT_12,
    &MainMenuPage, MoreMenuItems, 4, NULL,
    {1.0f, 1.0f, 128.0f, 64.0f}, 0, 1, 4, 2,
    0, 0, 0, 0,
    0, 0, {0.0f, 0.0f}, 0
};

MenuPage xdata Font8MenuPage = {
    MENU_TYPE_LIST, SPEED, REVERSE_ROUNDRECTANGLE, UNLINEAR, OLED_UI_FONT_8,
    &MoreMenuPage, Font8MenuItems, 5, NULL,
    {0.0f, 0.0f, 128.0f, 64.0f}, 0, 1, 4, 2,
    0, 0, 0, 0,
    0, 0, {0.0f, 0.0f}, 0
};

MenuPage xdata Font12MenuPage = {
    MENU_TYPE_LIST, SPEED, REVERSE_ROUNDRECTANGLE, UNLINEAR, OLED_UI_FONT_12,
    &MoreMenuPage, Font12MenuItems, 4, NULL,
    {0.0f, 0.0f, 128.0f, 64.0f}, 0, 1, 4, 2,
    0, 0, 0, 0,
    0, 0, {0.0f, 0.0f}, 0
};

MenuPage xdata Font16MenuPage = {
    MENU_TYPE_LIST, SPEED, REVERSE_ROUNDRECTANGLE, UNLINEAR, OLED_UI_FONT_16,
    &MoreMenuPage, Font16MenuItems, 5, NULL,
    {1.0f, 1.0f, 126.0f, 62.0f}, 0, 1, 4, 2,
    0, 0, 0, 0,
    0, 0, {0.0f, 0.0f}, 0
};

MenuPage xdata Font20MenuPage = {
    MENU_TYPE_LIST, SPEED, REVERSE_ROUNDRECTANGLE, UNLINEAR, OLED_UI_FONT_20,
    &MoreMenuPage, Font20MenuItems, 10, NULL,
    {0.0f, 0.0f, 128.0f, 64.0f}, 0, 1, 4, 2,
    0, 0, 0, 0,
    0, 0, {0.0f, 0.0f}, 0
};

MenuPage xdata LongMenuPage = {
    MENU_TYPE_LIST, SPEED, REVERSE_ROUNDRECTANGLE, UNLINEAR, OLED_UI_FONT_12,
    &MoreMenuPage, LongMenuItems, 4, NULL,
    {0.0f, 0.0f, 128.0f, 64.0f}, 0, 1, 4, 2,
    0, 0, 0, 0,
    0, 0, {0.0f, 0.0f}, 0
};

MenuPage xdata SpringMenuPage = {
    MENU_TYPE_LIST, SPEED, REVERSE_ROUNDRECTANGLE, PID_CURVE, OLED_UI_FONT_12,
    &MoreMenuPage, SpringMenuItems, 4, NULL,
    {0.0f, 0.0f, 128.0f, 64.0f}, 0, 1, 4, 2,
    0, 0, 0, 0,
    0, 0, {0.0f, 0.0f}, 0
};

MenuPage xdata LongListMenuPage = {
    MENU_TYPE_LIST, SPEED, REVERSE_ROUNDRECTANGLE, UNLINEAR, OLED_UI_FONT_12,
    &MoreMenuPage, LongListMenuItems, 4, NULL,
    {0.0f, 0.0f, 128.0f, 64.0f}, 0, 1, 4, 2,
    0, 0, 0, 0,
    0, 0, {0.0f, 0.0f}, 0
};

MenuPage xdata SmallAreaMenuPage = {
    MENU_TYPE_LIST, SPEED, REVERSE_ROUNDRECTANGLE, UNLINEAR, OLED_UI_FONT_12,
    &MoreMenuPage, SmallAreaMenuItems, 6, NULL,
    {10.0f, 10.0f, 60.0f, 36.0f}, 1, 1, 4, 2,
    0, 0, 0, 0,
    0, 0, {0.0f, 0.0f}, 0
};

/* Time Set list menu */
MenuItem xdata TimeSetMenuItems[] = {
    {(char code *)"[Back]", OLED_UI_Back, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
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
    {32.0f, 0.0f, 95.0f, 64.0f}, 0, 1, 4, 2,
    0, 0, 0, 0,
    0, 0, {0.0f, 0.0f}, 0
};

/* Alarm clock list menu (submenu of Main) */
MenuItem xdata AlarmMenuItems[] = {
    {(char code *)"[Back]", OLED_UI_Back, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
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
    {32.0f, 0.0f, 95.0f, 64.0f}, 0, 1, 4, 2,
    0, 0, {0.0f, 0.0f}, 0
};

/* Countdown list menu (submenu of Main) */
MenuItem xdata CountdownMenuItems[] = {
    {(char code *)"[Back]", OLED_UI_Back, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Minute", ShowCDownMinWindow, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Second", ShowCDownSecWindow, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)"Start!", CountDownStartMenuCM, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f},
    {(char code *)0, NULL, NULL, (unsigned char edata *)0, (const uint8_t code *)0, 0.0f}
};

MenuPage xdata CountdownMenuPage = {
    MENU_TYPE_LIST, SPEED, REVERSE_ROUNDRECTANGLE, UNLINEAR, OLED_UI_FONT_12,
    &MainMenuPage, CountdownMenuItems, 4, NULL,
    {32.0f, 0.0f, 95.0f, 64.0f}, 0, 1, 4, 2,
    0, 0, {0.0f, 0.0f}, 0
};

