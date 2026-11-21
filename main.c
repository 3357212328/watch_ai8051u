/* main.c - AI8051U watch demo (SSD1306 128x64, hardware I2C) */
#pragma maxargs (200)

#include "AI8051U.H"
#include "DEF.H"
#include "sys_clock.h"
#include "oled.h"
#include "oled_driver.h"
#include "oled_ui_driver.h"
#include "oled_ui.h"
#include "usb_ctrl.h"
#include "set_timer.h"
#include "watch_face.h"
#include "watch_time.h"

/* Watch app on the OLED_UI framework:
 * Boot into the digital watch face (on-chip RTC, internal IRC32K).
 * Any key/encoder/USB virtual key opens the OLED_UI menu. The main
 * menu gets a "Watch Face" tile, Settings gets "Time Set" (on-chip
 * RTC editing), More gets "Stopwatch". Back on the main menu or 30 s
 * of inactivity parks on the watch face again.
 * Keys: P32 Enter, P33 Back, P36 Up, P37 Down (active low, pull-up).
 * Encoder: P10(A) / P11(B) software quadrature.
 * OLED: SCL P24, SDA P23, 400kHz hardware I2C, 0.96 inch SSD1306.
 * USB-CDC console: e=enter b=back w=up s=down +=enc+ -=enc- h=help.
 * CPU: 40MHz HIRC. TFPU: 120MHz overclock (108MHz limit waived).
  */

void main(void)
{
    SysClock_Init40MHz_HIRC();
    TFPU_Init120MHz();
    USBCtrl_Init();
    EA = 1;
    /* Ports first, app state second. Watch_Init() parks its release-edge
     * trackers with the live pin state; reading P3.2/P1.0/P1.1 before
     * Key_Init/Encoder_Init enables the pull-ups samples a floating
     * high-Z pin. If that reads low, the first 20 ms tick sees a
     * "released" key and opens the menu straight from boot.
     * EAXFR stays on: set_io_mode() reaches PxPU through the XSFR
     * window, and WatchTime_Init() is no longer the one arming it. */
    EAXFR = 1;
    Key_Init();
    Encoder_Init();
    Watch_Init();
    /* Same hardware bring-up as OLED_UI_Init, without page select:
     * the watch face is the boot mode, the menu opens on demand. */
    OLED_Init();
    Timer_Init();

    while (1)
    {
        /* Fast encoder sampling (debounced accumulation between 20ms ticks) */
        Encoder_FastPoll();
        /* Drain USB-CDC RX into virtual key/encoder pulses */
        USBCtrl_Poll();
        if (get_timer_state(Timer11) != 0)
        {
            Watch_Tick();
            /* Follow the PC clock while a USB host is present, and apply
             * the manual drift trim when running standalone. */
            WatchTime_Discipline();
            WatchTime_TrimTick();
        }
        Watch_Render();
    }
}
