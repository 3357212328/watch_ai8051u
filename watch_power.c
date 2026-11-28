/* Power latch, HOME/long-press handling, flashlight, battery gauge and
 * the settings record.
 *
 * Flash note: the image is ~58.6 KB of the 64 KB code space, so the
 * record lives in the last 512 B sector (0xFE00), as far from code as
 * the part allows. One record is 15 bytes, but a save always erases
 * the whole sector, which is why it only runs on shutdown and on a
 * low battery, never per keystroke. The optional W25Q mirror is
 * written too when the chip answers.
 */

#include "watch_power.h"
#include "set_io.h"
#include "set_timer.h"
#include "oled.h"
#include "oled_ui.h"
#include "oled_ui_driver.h"
#include "watch_time.h"
#include "watch_face.h"
#include "watch_rtc.h"
#include "watch_flash.h"
#include "watch_dino.h"
#include "watch_plane.h"

/* EEPROM record layout (see Watch_PowerSave).
 * Lives in the LAST 512-byte sector of the 64 KB user flash: the image
 * is ~58.6 KB, so the top sector is the furthest possible from code
 * and cannot be clobbered by growth. A code reflash wipes it, which is
 * what the optional W25Q mirror is for. */
#define PWR_EEPROM_ADDR     0xFE00U
#define PWR_REC_LEN         15
#define PWR_REC_MAGIC       0x5A
#define PWR_REC_VERSION     0x01

/* Long press = 75 ticks of 20 ms */
#define PWR_LONG_TICKS      75
/* One minute of 20 ms ticks */
#define PWR_MINUTE_TICKS    3000

/* Battery thresholds, mV */
#define PWR_LOW_MV          3400U
#define PWR_LOW_LIMIT       3

/* Set to 1 once the 100k/100k divider on P1.2 is fitted behind the
 * 3.3 V LDO; the internal-reference method then no longer sees the
 * cell because VCC is regulated. */
#ifndef WATCH_VBAT_DIVIDER
#define WATCH_VBAT_DIVIDER  0
#endif

/* Radio button state, edited by the menu framework */
uint8_t edata Watch_TorchFlag = 0;

static uint8_t xdata PwBackTicks;
static uint8_t xdata PwAsked;
static uint8_t xdata PwTorchPin;
static uint16_t xdata PwMinuteTicks;
static uint8_t xdata PwLowCount;
static uint8_t xdata PwRtcChecked;
static uint8_t xdata PwIapValid;
static uint8_t xdata PwRec[PWR_REC_LEN];
static unsigned int xdata PwMv;

/* ------------------------------------------------------------------ */
/* Flash (IAP)                                                         */
/* ------------------------------------------------------------------ */

static void Pw_IapDelay(unsigned int loops)
{
    while (loops != 0U)
    {
        _nop_();
        loops--;
    }
}

static void Pw_IapEraseSector(unsigned int addr)
{
    uint8_t eaSave;

    eaSave = (uint8_t)EA;
    EA = 0;
    IAP_CONTR = 0x80;
    IAP_CMD = 0x03;                 /* sector erase */
    IAP_ADDRH = (uint8_t)(addr >> 8);
    IAP_ADDRL = (uint8_t)(addr & 0x00FFU);
    IAP_ADDRE = 0;
    IAP_TRIG = 0x5A;
    IAP_TRIG = 0xA5;
    /* Sector erase takes up to ~20 ms; the CPU parks here because the
     * IAP address/data registers must not change while it runs. */
    Delay_ms(25);
    IAP_CONTR = 0;
    EA = eaSave;
}

static void Pw_IapWriteByte(unsigned int addr, uint8_t value)
{
    uint8_t eaSave;

    eaSave = (uint8_t)EA;
    EA = 0;
    IAP_CONTR = 0x80;
    IAP_CMD = 0x02;                 /* byte program */
    IAP_ADDRH = (uint8_t)(addr >> 8);
    IAP_ADDRL = (uint8_t)(addr & 0x00FFU);
    IAP_ADDRE = 0;
    IAP_DATA = value;
    IAP_TRIG = 0x5A;
    IAP_TRIG = 0xA5;
    Delay_ms(2);                    /* byte program: ~50 us, be safe */
    IAP_CONTR = 0;
    EA = eaSave;
}

static uint8_t Pw_IapReadByte(unsigned int addr)
{
    uint8_t eaSave;
    uint8_t value;

    eaSave = (uint8_t)EA;
    EA = 0;
    IAP_CONTR = 0x80;
    IAP_CMD = 0x01;                 /* byte read */
    IAP_ADDRH = (uint8_t)(addr >> 8);
    IAP_ADDRL = (uint8_t)(addr & 0x00FFU);
    IAP_ADDRE = 0;
    IAP_TRIG = 0x5A;
    IAP_TRIG = 0xA5;
    Pw_IapDelay(40);
    value = IAP_DATA;    IAP_CONTR = 0;
    EA = eaSave;
    return value;
}

/* ------------------------------------------------------------------ */
/* Battery                                                             */
/* ------------------------------------------------------------------ */

/* One blocking ADC conversion on channel ch (0..15). Returns 0 when
 * the conversion did not finish, so a stuck ADC cannot report a fake
 * full battery. */
static unsigned int Pw_AdcOnce(uint8_t ch)
{
    unsigned int value;
    uint16_t guard;

    ADC_CONTR = (uint8_t)(0x80U | (ch & 0x0FU));   /* power on, select */
    ADCCFG = 0x2F;                                 /* right aligned, slow */
    ADCTIM = 0x3F;                                 /* longest sample window */
    Pw_IapDelay(200);                              /* let the ADC settle */
    ADC_CONTR |= 0x40;                             /* start */
    guard = 0;
    while ((ADC_CONTR & 0x20U) == 0U)
    {
        guard++;
        if (guard > 4000U)
        {
            ADC_CONTR &= 0x7F;                     /* power down */
            return 0;
        }
    }
    value = ((unsigned int)ADC_RES << 8) | (unsigned int)ADC_RESL;
    ADC_CONTR = 0x00;                              /* stop, flag off, power down */
    return value;
}

unsigned int Watch_PowerBatteryMv(void)
{
    unsigned int adc;
    unsigned int mv;

    adc = Pw_AdcOnce(15);                          /* internal 1.19 V */
    if (adc < 200U || adc > 4000U)
    {
        return 0;
    }
    mv = (unsigned int)((1190UL * 4096UL) / (unsigned long)adc);
#if WATCH_VBAT_DIVIDER
    /* Divider mode: VCC is regulated, so the reference channel would
     * only report 3.3 V. Read the divider instead. */
    adc = Pw_AdcOnce(2);                           /* ADC2 = P1.2 */
    if (adc == 0U)
    {
        return 0;
    }
    mv = (unsigned int)((3300UL * (unsigned long)adc * 2UL) / 4096UL);
#endif
    return mv;
}

/* ------------------------------------------------------------------ */
/* Settings record                                                     */
/* ------------------------------------------------------------------ */

unsigned int Watch_PowerBatteryMvCached(void)
{
    return PwMv;
}

void Watch_PowerSave(void)
{
    WatchTime t;
    uint8_t i;
    uint8_t sum;

    WatchTime_Read(&t);
    PwRec[0] = PWR_REC_MAGIC;
    PwRec[1] = PWR_REC_VERSION;
    PwRec[2] = 0;                       /* checksum, filled below */
    PwRec[3] = t.Year;
    PwRec[4] = t.Month;
    PwRec[5] = t.Day;
    PwRec[6] = t.Hour;
    PwRec[7] = t.Minute;
    PwRec[8] = t.Second;
    if (OLED_UI_Brightness < 1)
    {
        PwRec[9] = 1;
    }
    else if (OLED_UI_Brightness > 100)
    {
        PwRec[9] = 100;
    }
    else
    {
        PwRec[9] = (uint8_t)OLED_UI_Brightness;
    }
    PwRec[10] = (uint8_t)(ColorMode != 0);
    PwRec[11] = (uint8_t)(Watch_Dino_GetHighScore() & 0x00FFU);
    PwRec[12] = (uint8_t)(Watch_Dino_GetHighScore() >> 8);
    PwRec[13] = Watch_Plane_GetHighScore();
    PwRec[14] = (uint8_t)(Watch_FaceStyle != 0);
    sum = 0;
    for (i = 3; i < PWR_REC_LEN; i++)
    {
        sum = (uint8_t)(sum + PwRec[i]);
    }
    PwRec[2] = sum;

    Pw_IapEraseSector(PWR_EEPROM_ADDR);
    for (i = 0; i < PWR_REC_LEN; i++)
    {
        Pw_IapWriteByte((unsigned int)(PWR_EEPROM_ADDR + i), PwRec[i]);
    }

    /* Mirror to the optional external NOR. Its erase takes ~50 ms and
     * the on-chip copy is already committed, so a missing or failed
     * chip cannot lose the record. */
    if (Watch_FlashPresent() != 0)
    {
        if (Watch_FlashEraseSector(0UL) == 0)
        {
            (void)Watch_FlashWrite(0UL, PwRec, PWR_REC_LEN);
        }
    }
}

/* 1 when the record in PwRec passes magic + version + checksum */
static uint8_t Pw_RecordValid(void)
{
    uint8_t i;
    uint8_t sum;

    if (PwRec[0] != PWR_REC_MAGIC || PwRec[1] != PWR_REC_VERSION)
    {
        return 0;
    }
    sum = 0;
    for (i = 3; i < PWR_REC_LEN; i++)
    {
        sum = (uint8_t)(sum + PwRec[i]);
    }
    return (uint8_t)(sum == PwRec[2]);
}

/* Apply a record that already sits in PwRec */
static void Pw_ApplyRecord(void)
{
    if (PwRec[9] >= 1 && PwRec[9] <= 100)
    {
        OLED_UI_Brightness = (int16_t)PwRec[9];
    }
    ColorMode = (uint8_t)(PwRec[10] != 0);
    Watch_Dino_SetHighScore((unsigned int)PwRec[11] | ((unsigned int)PwRec[12] << 8));
    Watch_Plane_SetHighScore(PwRec[13]);
    Watch_FaceStyle = (uint8_t)(PwRec[14] != 0);
}

void Watch_PowerRestore(void)
{
    uint8_t i;

    /* On-chip sector only: the W25Q probe is deliberately deferred to
     * the first 20 ms tick so the boot path never touches SPI. If this
     * copy is missing (fresh download) the first tick loads the flash
     * mirror instead. */
    for (i = 0; i < PWR_REC_LEN; i++)
    {
        PwRec[i] = Pw_IapReadByte((unsigned int)(PWR_EEPROM_ADDR + i));
    }
    if (Pw_RecordValid() == 0)
    {
        return;
    }
    PwIapValid = 1;
    Pw_ApplyRecord();
}

/* ------------------------------------------------------------------ */
/* Time: external RTC first, saved snapshot as the fallback            */
/* ------------------------------------------------------------------ */

/* 1 when a is strictly later than b (second resolution) */
static uint8_t Pw_TimeAfter(const WatchTime *a, const WatchTime *b)
{
    if (a->Year != b->Year)     { return (uint8_t)(a->Year > b->Year); }
    if (a->Month != b->Month)   { return (uint8_t)(a->Month > b->Month); }
    if (a->Day != b->Day)       { return (uint8_t)(a->Day > b->Day); }
    if (a->Hour != b->Hour)     { return (uint8_t)(a->Hour > b->Hour); }
    if (a->Minute != b->Minute) { return (uint8_t)(a->Minute > b->Minute); }
    return (uint8_t)(a->Second > b->Second);
}

static void Pw_TimeSync(void)
{
    WatchTime t;
    WatchTime cur;

    if (Watch_RTC_Valid() != 0)
    {
        if (Watch_RTC_Read(&t) == 0)
        {
            WatchTime_Set(&t);
        }
        return;
    }
    /* No external RTC: the on-chip one restarted at the compile-time
     * default. Put back the snapshot if it is ahead of that, which is
     * always better than jumping back to 2026-01-01. */
    if (Pw_RecordValid() == 0)
    {
        return;
    }
    t.Year = PwRec[3];
    t.Month = PwRec[4];
    t.Day = PwRec[5];
    t.Hour = PwRec[6];
    t.Minute = PwRec[7];
    t.Second = PwRec[8];
    t.SubSec = 0;
    if (t.Month < 1 || t.Month > 12 || t.Day < 1 ||
        t.Day > WatchTime_DaysInMonth(t.Year, t.Month) ||
        t.Hour > 23 || t.Minute > 59 || t.Second > 59)
    {
        return;
    }
    WatchTime_Read(&cur);
    /* Only when strictly newer than the on-chip default, so a stale
     * record can never move the clock backwards. */
    if (Pw_TimeAfter(&t, &cur) == 0)
    {
        return;
    }
    WatchTime_Set(&t);
}

/* Re-anchor the drifting on-chip RTC to the external one once a minute */
static void Pw_RtcResync(void)
{
    WatchTime ext;
    WatchTime cur;
    int16_t diff;

    if (Watch_RTC_Type() == WATCH_RTC_NONE)
    {
        return;
    }
    if (Watch_RTC_Read(&ext) != 0)
    {
        return;
    }
    WatchTime_Read(&cur);
    diff = (int16_t)ext.Second - (int16_t)cur.Second;
    if (diff > 1 || diff < -1)
    {
        WatchTime_Set(&ext);
    }
}

/* ------------------------------------------------------------------ */
/* Torch                                                               */
/* ------------------------------------------------------------------ */

void Watch_TorchSet(uint8_t on)
{
    Watch_TorchFlag = (uint8_t)(on != 0);
    P51 = (uint8_t)(Watch_TorchFlag != 0);
    PwTorchPin = Watch_TorchFlag;
}

uint8_t Watch_TorchIsOn(void)
{
    return Watch_TorchFlag;
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */

void Watch_PowerInit(void)
{
    /* Latch first: everything else can wait, losing the rail cannot. */
    set_io_mode(pp_mode, Pin26, Pin_End);
    P26 = 1;

    /* Torch line parked low, push-pull so the NMOS gate is defined. */
    set_io_mode(pp_mode, Pin51, Pin_End);
    P51 = 0;
    PwTorchPin = 0;
    Watch_TorchFlag = 0;

    /* IAP timing: IAP_TPS is the CPU frequency in MHz. The system clock
     * is fixed at 40 MHz HIRC, so this is exact and needs no measuring. */
    IAP_TPS = 40;

    PwBackTicks = 0;
    PwAsked = 0;
    PwMinuteTicks = 0;
    PwLowCount = 0;
    PwRtcChecked = 0;
    PwIapValid = 0;

    /* No SPI here: Watch_FlashInit runs on the first 20 ms tick, after
     * the panel is already showing the face. A missing or miswired
     * flash chip therefore cannot keep the watch dark. */
    Watch_PowerRestore();
}

uint8_t Watch_PowerTick(void)
{
    uint8_t back;
    uint8_t on;
    unsigned int mv;

    /* First tick after boot: the panel is already up, so the SPI probe
     * and the external RTC can run without any chance of a dark boot. */
    if (PwRtcChecked == 0)
    {
        PwRtcChecked = 1;
        Watch_FlashInit();
        /* Only when the on-chip copy is gone (fresh download): the NOR
         * mirror then restores the settings and the high scores. */
        if (PwIapValid == 0 && Watch_FlashPresent() != 0)
        {
            if (Watch_FlashRead(0UL, PwRec, PWR_REC_LEN) == 0 &&
                Pw_RecordValid() != 0)
            {
                Pw_ApplyRecord();
            }
        }
        Pw_TimeSync();
        PwMv = Watch_PowerBatteryMv();
    }

    /* The menu edits the flag directly; follow it here. */
    on = (uint8_t)(Watch_TorchFlag != 0);
    if (on != PwTorchPin)
    {
        PwTorchPin = on;
        P51 = on;
    }

    /* Long press on BACK: one shot per press, 1.5 s. */
    back = (uint8_t)(Key_GetBackStatus() == 0);
    if (back != 0)
    {
        if (PwBackTicks < 200)
        {
            PwBackTicks++;
        }
        if (PwBackTicks == PWR_LONG_TICKS && PwAsked == 0)
        {
            PwAsked = 1;
            return 1;
        }
    }
    else
    {
        PwBackTicks = 0;
        PwAsked = 0;
    }

    /* Once a minute: RTC anchor + battery check */
    PwMinuteTicks++;
    if (PwMinuteTicks >= PWR_MINUTE_TICKS)
    {
        PwMinuteTicks = 0;
        Pw_RtcResync();
        mv = Watch_PowerBatteryMv();
        PwMv = mv;
        if (mv != 0 && mv < PWR_LOW_MV)
        {
            PwLowCount++;
            if (PwLowCount >= PWR_LOW_LIMIT)
            {
                Watch_PowerOff();
            }
        }
        else
        {
            PwLowCount = 0;
        }
    }
    return 0;
}

void Watch_PowerOff(void)
{
    unsigned int wait;

    Watch_PowerSave();
    Watch_TorchSet(0);

    OLED_Clear();
    OLED_ShowString(28, 26, (char code *)"POWER OFF", OLED_8X16_HALF);
    OLED_Update();

    /* Release the latch. While BACK is still down the button itself
     * holds the P-MOS gate low, so the rail drops on release, not here. */
    P26 = 0;
    wait = 0;
    while (Key_GetBackStatus() == 0)
    {
        wait++;
        Pw_IapDelay(200);
        if (wait > 10000U)
        {
            break;                 /* button stuck: do not hang forever */
        }
    }

    /* Still alive means USB (or a board without the latch) is powering
     * the MCU. Blank the panel and park; a BACK press reboots into the
     * firmware, which is the only way to "turn on" while the cable is
     * attached. On battery the rail is already collapsing here. */
    OLED_Clear();
    OLED_Update();
    while (1)
    {
        if (Key_GetBackStatus() == 0)
        {
            Delay_ms(30);              /* debounce */
            if (Key_GetBackStatus() == 0)
            {
                IAP_CONTR = 0x20;      /* SWRST: reboot from user code */
                while (1)
                {
                    ;
                }
            }
        }
    }
}
