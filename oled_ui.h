#ifndef __OLED_UI_H
#define __OLED_UI_H

/* OLED_UI framework ported from STM32 to AI8051U (Keil C251, C89).
 * Source: OLED_UI for STM32F103 (SSD1306 128x64).
 * All animation math uses single-precision float (TFPU accelerated).
 * 32-bit multiply/divide is MDU accelerated automatically.
 * No 64-bit integers: former INT64 window data maps to 32-bit long range.
 */

#define OLED_UI

#ifdef OLED_UI

#include "AI8051U.H"
#include "DEF.H"
#include "oled.h"
#include "oled_driver.h"
#include "oled_ui_driver.h"
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

/* Frame buffer is defined in oled_driver.c, only referenced here */
extern uint8_t edata OLED_DisplayBuf[OLED_HEIGHT / 8][OLED_WIDTH];

/************************************************************/
/**************** UI configuration macros *******************/

/* Tiles menu: Y of first item from screen top */
#define TILES_STARTPOINT_Y          (6)

/* Tiles menu: scrollbar Y offset below tiles */
#define TILES_SCROLLBAR_Y           (4)

/* Tiles menu: width of the fixed thumb, one slot of the 9 item main menu.
 * The 9 slots span 114 px (0..114), thumb 14 px ends on the right edge. */
#define TILES_THUMB_WIDTH           (14)

/* Tiles menu: label distance from screen bottom */
#define TILES_BOTTOM_DISTANCE       (0)

/* List menu long-text auto scroll speed in pixels per frame.
 * Smaller is slower. Use fractions of 1 (eg 0.5). */
#define LINE_SLIP_SPEED             (0.5f)

/* Draw arrow head above tiles menu.
 * Disabled: the arrow sits dead center and does not tell the user that
 * more tiles wait off screen, the partial next tile plus the slider do. */
#define IF_DRAW_ARROR               (0)

/* Line prefix symbols */
#define FUNCTION_PREFIX             ("~")
#define SUBMENU_PREFIX              (">")
#define RADIO_PREFIX                ("*")
#define NONE_PREFIX                 ("-")

#define LINEPERFIX_DISTANCE         (2)

/* Wait for move animation to finish before scrolling long text */
#define IF_WAIT_ANIMATION_FINISH    (1)

/* Long press on Up/Down keys repeats movement */
#define IF_START_UP_AND_DOWN_LONG_PRESS (1)
#define PRESS_TIME                  (0.6f)
#define CONTINUE_PRESS_TIME         (2.0f)
#define PRESS_MOVE_SPEED            (5)
#define CONTINUE_PRESS_MOVE_SPEED   (1)

/* Gap between window text and adjustable data */
#define WINDOW_DATA_TEXT_DISTANCE   (4)

/* Fade out frame delay in ms */
#define FADEOUT_TIME                (40)

/* Decimal places shown for float window data */
#define FLOAT_POINT_LENGTH          (2)
/************************************************************/

/* Printf format for float window data, driven by FLOAT_POINT_LENGTH.
 * Avoids "%.*f" which Keil C251 printf does not support. */
#define OU_STR2(x) #x
#define OU_STR(x) OU_STR2(x)
#define OU_FLOAT_FMT ("%." OU_STR(FLOAT_POINT_LENGTH) "f")

/* Cursor styles */
#define REVERSE_RECTANGLE           (0)
#define REVERSE_ROUNDRECTANGLE      (1)
#define HOLLOW_RECTANGLE            (2)
#define HOLLOW_ROUNDRECTANGLE       (3)
#define REVERSE_BLOCK               (4)
#define NOT_SHOW                    (5)

/* UI font Heights */
#define OLED_UI_FONT_8              (8)
#define OLED_UI_FONT_12             (12)
#define OLED_UI_FONT_16             (16)

#define CHINESE                     (0)
#define ASCII                       (1)

/* Color modes */
#define DARKMODE                    (1)
#define LIGHTMODE                   (0)

/* Mutex flag values */
#define FLAGEND                     (0)
#define FLAGSTART                   (1)
#define ENTER_FLAGSTART             (1)
#define BACK_FLAGSTART              (2)

/* Fullscreen menu area helper */
#define FULLSCREEN 1, 1, OLED_WIDTH - 2, OLED_HEIGHT - 2

/* Enter/back event results */
#define OLED_UI_BACK                (0)
#define OLED_UI_ENTER               (1)

/* Menu types */
#define MENU_TYPE_LIST              (0)
#define MENU_TYPE_TILES             (1)

/* Animation curve types */
#define UNLINEAR                    (0)
#define PID_CURVE                   (1)

/* Window frame types */
#define WINDOW_RECTANGLE            (0)
#define WINDOW_ROUNDRECTANGLE       (1)

/* Window data styles.
 * NOTE: WINDOW_DATA_STYLE_INT64 is kept for API compatibility but on
 * Keil C251 (no 64-bit type) it addresses a 32-bit long, same as INT32. */
#define WINDOW_DATA_STYLE_NONE      (0)
#define WINDOW_DATA_STYLE_FLOAT     (1)
#define WINDOW_DATA_STYLE_INT8      (2)
#define WINDOW_DATA_STYLE_INT16     (3)
#define WINDOW_DATA_STYLE_INT32     (4)
#define WINDOW_DATA_STYLE_INT64     (5)

/*************************** data types ****************************/

/* Menu kind (list or tiles) */
typedef int8_t MenuStyle;

/* Menu item index */
typedef int16_t MenuID;

/* Menu animation speed */
typedef float MenuMovingSpeed;

/* Menu font height */
typedef int8_t OLED_Font;

/* Mutex flag, only FLAGSTART or FLAGEND */
typedef int8_t MutexFlag;

/* Safe ID delta (moves cursor) and unsafe ID delta (window adjust) */
typedef struct MenuID_Type {
    MenuID Safe;
    MenuID Unsafe;
} MenuID_Type;

/* Float point */
typedef struct OLED_Point {
    float X;
    float Y;
} OLED_Point;

/* Rectangular area */
typedef struct OLED_Area {
    float X;
    float Y;
    float Width;
    float Height;
} OLED_Area;

/* Key states, 1 = released, 0 = pressed */
typedef struct OLED_Key {
    uint8_t Enter;
    uint8_t Back;
    uint8_t Up;
    uint8_t Down;
} OLED_Key;

/* Animated area (current value chases target value) */
typedef struct OLED_ChangeArea {
    OLED_Area CurrentArea;
    OLED_Area TargetArea;
    OLED_Area Error;
    OLED_Area LastError;
    OLED_Area Integral;
    OLED_Area Derivative;
} OLED_ChangeArea;

/* Animated point */
typedef struct OLED_ChangePoint {
    OLED_Point CurrentPoint;
    OLED_Point TargetPoint;
    OLED_Point Error;
    OLED_Point LastError;
    OLED_Point Integral;
    OLED_Point Derivative;
} OLED_ChangePoint;

/* Animated scalar distance.
 * CurrentDistance chases TargetDistance, PID terms shape the curve. */
typedef struct OLED_ChangeDistance {
    float CurrentDistance;
    float TargetDistance;
    float Error;
    float LastError;
    float Integral;
    float Derivative;
} OLED_ChangeDistance;

/* Frame rate counter */
typedef struct OLED_UI_Counter {
    int16_t count;
    int16_t value;
    int16_t step;
} OLED_UI_Counter;

/* Window sustain timer */
typedef struct OLED_UI_WindowSustainCounter {
    int16_t count;
    unsigned char SustainFlag;
} OLED_UI_WindowSustainCounter;

/* Popup window descriptor.
 * Point exactly one Prob_Data_* pointer at live data, or none.
 * Prob_Data_Int_64 is a 32-bit long on C251 (no 64-bit type).
 * All Prob_Data_* bind edata RAM ONLY: on 80251/C251 an edata pointer
 * cannot encode an xdata address, so binding xdata here would silently
 * truncate at runtime with no compiler diagnostic. Keep live data edata. */
typedef struct MenuWindow {
    int16_t General_Width;
    int16_t General_Height;
    float General_ContinueTime;
    uint8_t General_WindowType;

    char code *Text_String;
    OLED_Font Text_FontSize;
    int16_t Text_FontSideDistance;
    int16_t Text_FontTopDistance;

    float edata *Prob_Data_Float;
    int8_t edata *Prob_Data_Int_8;
    int16_t edata *Prob_Data_Int_16;
    int32_t edata *Prob_Data_Int_32;
    long edata *Prob_Data_Int_64;
    float Prob_DataStep;
    float Prob_MinData;
    float Prob_MaxData;
    int16_t Prob_BottomDistance;
    int16_t Prob_SideDistance;
    int16_t Prob_LineHeight;

    float _LineSlip;
} MenuWindow;

/* Menu page descriptor.
 * For LIST pages fill List_* fields, Tiles_* are 0.
 * For TILES pages fill Tiles_* fields, List_* are 0. */
typedef struct MenuPage {
    MenuStyle General_MenuType;
    MenuMovingSpeed General_MovingSpeed;
    uint8_t General_CursorStyle;
    uint8_t General_MoveStyle;
    OLED_Font General_FontSize;
    struct MenuPage *General_ParentMenuPage;
    struct MenuItem *General_MenuItems;
    int16_t General_LineSpace;
    void (*General_ShowAuxiliaryFunction)(void);

    OLED_Area List_MenuArea;
    unsigned char List_IfDrawFrame;
    unsigned char List_IfDrawLinePerfix;
    int16_t List_StartPointX;
    int16_t List_StartPointY;

    int16_t Tiles_ScreenWidth;
    int16_t Tiles_ScreenHeight;
    int16_t Tiles_TileWidth;
    int16_t Tiles_TileHeight;

    MenuID _ActiveMenuID;
    MenuID _Slot;
    OLED_Point _StartPoint;
    unsigned char _IfInit;
} MenuPage;

/* Single menu item.
 * Radio items point List_BoolRadioBox at a live flag.
 * Tile items point Tiles_Icon at a code flash bitmap, list items use NULL. */
typedef struct MenuItem {
    char code *General_item_text;
    void (*General_callback)(void);
    MenuPage *General_SubMenuPage;

    unsigned char edata *List_BoolRadioBox;
    const uint8_t code *Tiles_Icon;

    float _LineSlip;
} MenuItem;

/**************** speed-critical globals (edata fast RAM) ****************/
extern OLED_ChangeArea xdata OLED_UI_Cursor;
extern OLED_ChangeArea xdata OLED_UI_MenuFrame;
extern OLED_ChangeArea xdata OLED_UI_Window;
extern OLED_ChangeDistance xdata OLED_UI_ScrollBarHeight;
extern OLED_ChangeDistance xdata OLED_UI_ProbWidth;
extern OLED_ChangePoint xdata OLED_UI_PageStartPoint;
extern OLED_ChangeDistance xdata OLED_UI_LineStep;
extern OLED_Key edata OLED_UI_Key;
extern OLED_Key edata OLED_UI_LastKey;
extern MenuPage * edata CurrentMenuPage;
extern MenuWindow * edata CurrentWindow;
extern MutexFlag edata KeyEnterFlag;
extern MutexFlag edata FadeOutFlag;
extern MutexFlag edata WindowFlag;
extern unsigned char edata ColorMode;
extern int16_t edata OLED_UI_Brightness;
extern OLED_UI_WindowSustainCounter edata OLED_SustainCounter;
extern int16_t edata WindowProbDeltaData;

/**************** menu data (defined in oled_ui_menudata.c) ****************/
extern MenuItem xdata MainMenuItems[];
extern MenuItem xdata SettingsMenuItems[];
extern MenuItem xdata MoreMenuItems[];
extern MenuPage xdata MainMenuPage;
extern MenuPage xdata SettingsMenuPage;
extern MenuPage xdata MoreMenuPage;
extern MenuItem xdata TimeSetMenuItems[];
extern MenuPage xdata TimeSetMenuPage;
extern MenuItem xdata AlarmMenuItems[];
extern MenuPage xdata AlarmMenuPage;
extern MenuItem xdata CountdownMenuItems[];
extern MenuPage xdata CountdownMenuPage;

/********************************************************************************/
/* Small float helpers (avoid 64-bit FP lib calls on C251) */
float OLEDUI_AbsF(float v);
int16_t OLEDUI_CeilF(float v);

/* Drawing helpers */
OLED_Font GetOLED_Font(OLED_Font fontsize, unsigned char style);
void ReverseCoordinate(int16_t X, int16_t Y, int16_t Width, int16_t Height, uint8_t Style);
void OLED_UI_FadeOut_Masking(int16_t x0, int16_t y0, int16_t width, int16_t height, int8_t fadeLevel);
void OLED_UI_FadeoutCurrentArea(int16_t x, int16_t y, int16_t width, int16_t height);
void OLED_UI_FadeoutAllArea(void);

/* Animation core */
MenuID GetMenuItemNum(MenuItem *items);
void ChangeFloatNum(float *CurrentNum, float *TargetNum, float *ErrorNum, float *LastErrorNum, float *IntegralNum, float *DerivativeNum);
void ChangeDistance(OLED_ChangeDistance *distance);
void ChangePoint(OLED_ChangePoint *point);
void ChangeArea(OLED_ChangeArea *area);

/* Page setup */
void CurrentMenuPageInit(void);
void CurrentMenuPageBackUp(void);
void OLED_UI_Init(MenuPage *Page);

/* Flags */
unsigned char GetEnterFlag(void);
unsigned char GetFadeoutFlag(void);
void SetEnterFlag(void);
void ResetEnterFlag(void);
void SetFadeOutFlag(MutexFlag action);
void ResetFadeOutFlag(void);

/* Measure and window data */
int16_t CalcStringWidth(int16_t ChineseFont, int16_t ASCIIFont, const char *format, ...);
int8_t GetWindowProbDataStyle(MenuWindow *window);
void OLED_DrawWindow(void);

/* Cursor and frame targets */
void MenuItemsMoveUp(void);
void MenuItemsMoveDown(void);
void MenuItemsMoveLeft(void);
void MenuItemsMoveRight(void);
int16_t GetCurrentMenuPageMaxSlotNum(void);
void SetTargetCursor(void);
void SetTargetMenuFrame(void);
void SetLineSplitZero(void);
void SetTargetScrollBarHeight(void);
void SetTargetProbWidth(void);

/* Render */
void DrawLinePermix(MenuPage *page, MenuID i, OLED_Point *CursorPoint, OLED_Font ASCIIFont);
void PrintMenuElements(void);

/* Input */
MenuID_Type OLED_KeyAndEncoderRecord(void);
void EnterEventMenuItem(void);
void BackEventMenuItem(void);
void OLED_UI_Back(void);
void RunCurrentCallBackFunction(void);
void SetCursorZero(void);

/* Window control */
void OLED_UI_CreateWindow(MenuWindow *window);
void RunFadeOut(void);
void OLED_WindowDataDispose(void);

/* Main flow: call MainLoop every frame, InterruptHandler every 20 ms */
void MoveMenuElements(void);
void OLED_UI_MainLoop(void);
void OLED_UI_InterruptHandler(void);

/* External app integration (watch face).
 * BackHook fires when Back is pressed on a page without a parent
 * (the main tiles menu), IdleHook after IdleTimeout silent ticks.
 * Hooks are only called from InterruptHandler while the UI is safe;
 * they must not call fade functions themselves, just request a mode
 * change. IdleTimeout counts 20 ms ticks, 0 disables the watchdog. */
extern void (*OLED_UI_BackHook)(void);
extern void (*OLED_UI_IdleHook)(void);
extern unsigned int OLED_UI_IdleTimeout;

/* Enter a page without the normal Enter-key fade, for use when an
 * external app (watch face) switches back to the menu. */
void OLED_UI_OpenMenu(MenuPage *Page);

#endif

#endif
