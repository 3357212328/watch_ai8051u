/* External battery-backed RTC (DS3231 / SD3078) on I2C0.
 *
 * Blocking hardware I2C master, same register-level protocol as
 * oled_driver.c (START / SEND 8 clocks / read ACK / RECV / send ACK or
 * NAK / STOP), because the OLED already owns I2C0 and both drivers
 * must speak the identical dialect. Never call this from an interrupt:
 * it spins on I2CMSST for up to ~1 ms per byte.
 *
 * Nothing here runs until Watch_RTC_Probe() is called, so a board
 * without the chip keeps working exactly as before.
 */

#include "watch_rtc.h"
#include "set_io.h"
#include "set_i2c.h"

/* 7-bit slave addresses */
#define RTC_ADDR_DS3231     0x68
#define RTC_ADDR_SD3078     0x32

/* I2C master command codes (low 4 bits of I2CMSCR) */
#define RTC_CMD_START       0x01
#define RTC_CMD_SEND        0x02
#define RTC_CMD_ACK         0x03
#define RTC_CMD_RECV        0x04
#define RTC_CMD_MACK        0x05
#define RTC_CMD_STOP        0x06

/* DS3231 registers we touch beyond the time block */
#define RTC_REG_STATUS      0x0F

/* Cached probe result and one-shot pin setup flag */
static uint8_t xdata rtc_type;
static uint8_t xdata rtc_pins;

/* Wait for one master step to finish and clear the done flag.
 * Bounded so a dead bus cannot hang the UI. */
static uint8_t RTC_Wait(void)
{
    unsigned long t;
    t = 10000UL;
    while ((I2CMSST & 0x40) == 0)
    {
        if (t == 0UL)
        {
            return 1;
        }
        t--;
    }
    I2CMSST &= 0xBF;
    return 0;
}

static void RTC_Cmd(uint8_t c)
{
    I2CMSCR = (uint8_t)((I2CMSCR & 0x80) | c);
}

/* Send one byte plus the 9th ACK clock. 0 = ACKed, 1 = timeout/NACK. */
static uint8_t RTC_SendByte(uint8_t b)
{
    I2CTXD = b;
    RTC_Cmd(RTC_CMD_SEND);
    if (RTC_Wait() != 0)
    {
        return 1;
    }
    RTC_Cmd(RTC_CMD_ACK);
    if (RTC_Wait() != 0)
    {
        return 1;
    }
    if ((I2CMSST & 0x02) != 0)
    {
        return 1;
    }
    return 0;
}

/* Receive one byte. ack = 1 keeps the transfer alive (more bytes
 * follow), ack = 0 sends NAK so the slave releases SDA. */
static uint8_t RTC_RecvByte(uint8_t *b, uint8_t ack)
{
    RTC_Cmd(RTC_CMD_RECV);
    if (RTC_Wait() != 0)
    {
        return 1;
    }
    *b = I2CRXD;
    /* MSACKO is bit0: 0 = ACK, 1 = NAK */
    if (ack != 0)
    {
        I2CMSST &= 0xFE;
    }
    else
    {
        I2CMSST |= 0x01;
    }
    RTC_Cmd(RTC_CMD_MACK);
    if (RTC_Wait() != 0)
    {
        return 1;
    }
    return 0;
}

static void RTC_Start(void)
{
    RTC_Cmd(RTC_CMD_START);
    RTC_Wait();
}

static void RTC_Stop(void)
{
    RTC_Cmd(RTC_CMD_STOP);
    RTC_Wait();
}

/* Bus and pins are already up (OLED_Init did it); we only make sure
 * the pull-ups are on, once, and that the master is in polling mode. */
static void RTC_BusInit(void)
{
    if (rtc_pins != 0)
    {
        return;
    }
    set_io_mode(od_mode, Pin24, Pin23, Pin_End);
    set_io_mode(en_pur, Pin24, Pin23, Pin_End);
    set_i2c_fosc(40000000L);
    set_i2c_mode(I2c0, "400khz", I2c_P24_3, I2c_Master, I2c_End);
    I2CMSCR &= 0x7F;
    rtc_pins = 1;
}

/* 0 when the device at addr acknowledges its own address byte */
static uint8_t RTC_Ping(uint8_t addr7)
{
    uint8_t err;
    RTC_Start();
    err = RTC_SendByte((uint8_t)(addr7 << 1));
    RTC_Stop();
    return err;
}

static uint8_t RTC_BcdToBin(uint8_t v)
{
    return (uint8_t)(((v >> 4) * 10) + (v & 0x0F));
}

static uint8_t RTC_BinToBcd(uint8_t v)
{
    return (uint8_t)(((v / 10) << 4) | (v % 10));
}

uint8_t Watch_RTC_Probe(void)
{
    RTC_BusInit();
    if (rtc_type != WATCH_RTC_NONE)
    {
        return rtc_type;
    }
    if (RTC_Ping(RTC_ADDR_DS3231) == 0)
    {
        rtc_type = WATCH_RTC_DS3231;
        return rtc_type;
    }
    if (RTC_Ping(RTC_ADDR_SD3078) == 0)
    {
        rtc_type = WATCH_RTC_SD3078;
        return rtc_type;
    }
    return WATCH_RTC_NONE;
}

uint8_t Watch_RTC_Type(void)
{
    return rtc_type;
}

uint8_t Watch_RTC_ReadRegs(uint8_t reg, uint8_t *buf, uint8_t len)
{
    uint8_t addr7;
    uint8_t i;
    if (len == 0 || len > 8)
    {
        return 1;
    }
    addr7 = (uint8_t)(rtc_type == WATCH_RTC_DS3231 ? RTC_ADDR_DS3231 : RTC_ADDR_SD3078);
    if (rtc_type == WATCH_RTC_NONE)
    {
        return 1;
    }
    RTC_Start();
    if (RTC_SendByte((uint8_t)(addr7 << 1)) != 0)
    {
        RTC_Stop();
        return 1;
    }
    if (RTC_SendByte(reg) != 0)
    {
        RTC_Stop();
        return 1;
    }
    RTC_Start();
    if (RTC_SendByte((uint8_t)((addr7 << 1) | 0x01)) != 0)
    {
        RTC_Stop();
        return 1;
    }
    for (i = 0; i < len; i++)
    {
        if (RTC_RecvByte(&buf[i], (uint8_t)((i + 1) < len)) != 0)
        {
            RTC_Stop();
            return 1;
        }
    }
    RTC_Stop();
    return 0;
}

uint8_t Watch_RTC_WriteRegs(uint8_t reg, const uint8_t *buf, uint8_t len)
{
    uint8_t addr7;
    uint8_t i;
    if (len == 0 || len > 8)
    {
        return 1;
    }
    if (rtc_type == WATCH_RTC_NONE)
    {
        return 1;
    }
    addr7 = (uint8_t)(rtc_type == WATCH_RTC_DS3231 ? RTC_ADDR_DS3231 : RTC_ADDR_SD3078);
    RTC_Start();
    if (RTC_SendByte((uint8_t)(addr7 << 1)) != 0)
    {
        RTC_Stop();
        return 1;
    }
    if (RTC_SendByte(reg) != 0)
    {
        RTC_Stop();
        return 1;
    }
    for (i = 0; i < len; i++)
    {
        if (RTC_SendByte(buf[i]) != 0)
        {
            RTC_Stop();
            return 1;
        }
    }
    RTC_Stop();
    return 0;
}

uint8_t Watch_RTC_Read(WatchTime *t)
{
    uint8_t raw[7];
    uint8_t sec;
    uint8_t min;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint8_t year;
    if (Watch_RTC_Probe() == WATCH_RTC_NONE)
    {
        return 1;
    }
    if (Watch_RTC_ReadRegs(0x00, raw, 7) != 0)
    {
        return 1;
    }
    /* Bit7 of the seconds register is the oscillator-stop / CH flag on
     * DS3231 and must be masked before BCD conversion. */
    sec = RTC_BcdToBin((uint8_t)(raw[0] & 0x7F));
    min = RTC_BcdToBin((uint8_t)(raw[1] & 0x7F));
    hour = raw[2];
    if ((hour & 0x40) != 0)
    {
        /* 12 h mode: bit5 is PM */
        hour = RTC_BcdToBin((uint8_t)(hour & 0x1F));
        if ((raw[2] & 0x20) != 0)
        {
            if (hour < 12)
            {
                hour = (uint8_t)(hour + 12);
            }
        }
        else if (hour == 12)
        {
            hour = 0;
        }
    }
    else
    {
        hour = RTC_BcdToBin((uint8_t)(hour & 0x3F));
    }
    day = RTC_BcdToBin((uint8_t)(raw[4] & 0x3F));
    month = RTC_BcdToBin((uint8_t)(raw[5] & 0x1F));
    year = RTC_BcdToBin(raw[6]);
    if (sec > 59 || min > 59 || hour > 23 || month < 1 || month > 12 ||
        day < 1 || day > WatchTime_DaysInMonth(year, month) || year > 99)
    {
        return 1;
    }
    t->Year = year;
    t->Month = month;
    t->Day = day;
    t->Hour = hour;
    t->Minute = min;
    t->Second = sec;
    t->SubSec = 0;
    return 0;
}

uint8_t Watch_RTC_Valid(void)
{
    WatchTime t;
    if (Watch_RTC_Probe() == WATCH_RTC_NONE)
    {
        return 0;
    }
    return (uint8_t)(Watch_RTC_Read(&t) == 0);
}

uint8_t Watch_RTC_Write(const WatchTime *t)
{
    uint8_t raw[7];
    if (Watch_RTC_Probe() == WATCH_RTC_NONE)
    {
        return 1;
    }
    raw[0] = RTC_BinToBcd((uint8_t)(t->Second % 60));
    raw[1] = RTC_BinToBcd((uint8_t)(t->Minute % 60));
    raw[2] = RTC_BinToBcd((uint8_t)(t->Hour % 24));      /* bit6 = 0: 24 h */
    raw[3] = (uint8_t)(WatchTime_Weekday(t->Year, t->Month, t->Day) + 1);
    raw[4] = RTC_BinToBcd((uint8_t)(t->Day % 32));
    raw[5] = RTC_BinToBcd((uint8_t)(t->Month % 13));
    raw[6] = RTC_BinToBcd((uint8_t)(t->Year % 100));
    if (Watch_RTC_WriteRegs(0x00, raw, 7) != 0)
    {
        return 1;
    }
    if (rtc_type == WATCH_RTC_DS3231)
    {
        /* Clear OSF so the next probe does not report "time lost". */
        raw[0] = 0x00;
        Watch_RTC_WriteRegs(RTC_REG_STATUS, raw, 1);
    }
    return 0;
}
