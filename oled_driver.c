/* Hardware layer: SSD1306 128x64 IIC OLED on AI8051U.
 * Blocking hardware I2C master (register polling).
 * SCL = P24, SDA = P23, 400kHz. System clock is expected to be 40MHz HIRC
 * (see SysClock_Init40MHz_HIRC), so set_i2c_fosc(40000000L) is applied
 * before set_i2c_mode computes the baud divider.
 */

#include "oled_driver.h"
#include "oled_ui_driver.h"
#include "set_io.h"
#include "set_i2c.h"

/* Display RAM mirror and color mode flag */
unsigned char edata OLED_DisplayBuf[8][128];
unsigned char OLED_ColorMode = 1;

/* Last content actually clocked out to the panel, page by page. Lives in
 * xdata (about 1KB) because edata is nearly exhausted; kept byte-identical
 * to OLED_DisplayBuf right after each page transfer so OLED_Update can
 * skip pages whose pixels did not change. */
static unsigned char xdata OLEDShadowBuf[8][128];

/* 1 = next OLED_Update must push all 8 pages regardless of the diff.
 * Starts at 1 on purpose: SSD1306 GDDRAM holds random data after power up,
 * so the very first frame has to be written in full, otherwise pages that
 * happen to match the zeroed shadow would keep that garbage on screen. */
static unsigned char xdata OLEDForceFull = 1;

/* SSD1306 7-bit slave address shifted for write */
#define OLED_I2C_ADDR_W 0x78

/* I2C master command codes (low 4 bits of I2CMSCR, STC convention:
 * 0x01 START, 0x02 SEND 8 clocks, 0x03 read ACK 9th clock,
 * 0x04 RECV, 0x05 send ACK/NAK bit, 0x06 STOP) */
#define I2C_CMD_START 0x01
#define I2C_CMD_SEND  0x02
#define I2C_CMD_ACK   0x03
#define I2C_CMD_STOP  0x06

/* Wait for one master step to finish, then clear the done flag.
 * Bounded: give up after ~1ms at 40MHz so a dead bus cannot hang UI. */
static unsigned char I2C_Wait(void)
{
    unsigned long t;
    /* Sample the quadrature encoder once per I2C step. The bus needs about
     * 22us per byte at 400kHz and this routine runs once per SEND step and
     * once per ACK step, so fast rotation is captured at roughly 40kHz
     * instead of one sample per main loop frame. The call sits BEFORE the
     * spin on purpose: the hardware is already clocking the byte while the
     * pins are read, so the sample costs no extra bus time.
     * Safe context: Encoder_FastPoll only reads P10/P11 and updates
     * file-local statics, Encoder_Get is gated by enc_en (still 0 while
     * OLED_Init runs ahead of Encoder_Init in main), and no interrupt
     * handler anywhere calls I2C_Wait / OLED_WriteData / OLED_WriteCommand,
     * so this can never re-enter itself. */
    Encoder_FastPoll();
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

/* Issue one master command, keeping the interrupt enable bit untouched */
static void I2C_Cmd(unsigned char c)
{
    I2CMSCR = (unsigned char)((I2CMSCR & 0x80) | c);
}

/* Send one byte then clock in the ACK bit; returns 0 on ACK, 1 on
 * timeout or NACK. NOTE: SEND(0x02) shifts 8 clocks only, the 9th ACK
 * clock needs the separate 0x03 step before MSACKI is valid
 * (0 = ACK, slave pulled SDA low; 1 = NACK, SDA stayed high). */
static unsigned char I2C_SendByte(unsigned char b)
{
    I2CTXD = b;
    I2C_Cmd(I2C_CMD_SEND);
    if (I2C_Wait() != 0)
    {
        return 1;
    }
    I2C_Cmd(I2C_CMD_ACK);
    if (I2C_Wait() != 0)
    {
        return 1;
    }
    if ((I2CMSST & 0x02) != 0)
    {
        return 1;
    }
    return 0;
}

/* Configure I2C pins and baud rate once, then switch to polling mode */
static void OLED_I2C_Init(void)
{
    unsigned long i;
    /* I2C pins as open-drain with internal pull-up */
    set_io_mode(od_mode, Pin24, Pin23, Pin_End);
    set_io_mode(en_pur, Pin24, Pin23, Pin_End);
    /* Baud divider is derived from this clock, so report 40MHz first */
    set_i2c_fosc(40000000L);
    set_i2c_mode(I2c0, "400khz", I2c_P24_3, I2c_Master, I2c_End);
    /* Disable master interrupt, use blocking polling instead */
    I2CMSCR &= 0x7F;
    /* Short power-on settle delay */
    i = 60000UL;
    while (i > 0)
    {
        i--;
        _nop_();
    }
}

/* Clear the local display RAM mirror */
static void OLED_ClearBuf(void)
{
    unsigned char p;
    unsigned char c;
    p = 0;
    while (p < 8)
    {
        c = 0;
        while (c < 128)
        {
            OLED_DisplayBuf[p][c] = 0x00;
            c++;
        }
        p++;
    }
}

void OLED_WriteCommand(unsigned char cmd)
{
    I2C_Cmd(I2C_CMD_START);
    if (I2C_Wait() != 0)
    {
        I2C_Cmd(I2C_CMD_STOP);
        I2C_Wait();
        return;
    }
    if (I2C_SendByte(OLED_I2C_ADDR_W) != 0)
    {
        I2C_Cmd(I2C_CMD_STOP);
        I2C_Wait();
        return;
    }
    if (I2C_SendByte(0x00) != 0)
    {
        I2C_Cmd(I2C_CMD_STOP);
        I2C_Wait();
        return;
    }
    if (I2C_SendByte(cmd) != 0)
    {
        I2C_Cmd(I2C_CMD_STOP);
        I2C_Wait();
        return;
    }
    I2C_Cmd(I2C_CMD_STOP);
    I2C_Wait();
}

void OLED_WriteData(unsigned char edata *d, unsigned int n)
{
    unsigned int i;
    unsigned char v;
    I2C_Cmd(I2C_CMD_START);
    if (I2C_Wait() != 0)
    {
        I2C_Cmd(I2C_CMD_STOP);
        I2C_Wait();
        return;
    }
    if (I2C_SendByte(OLED_I2C_ADDR_W) != 0)
    {
        I2C_Cmd(I2C_CMD_STOP);
        I2C_Wait();
        return;
    }
    if (I2C_SendByte(0x40) != 0)
    {
        I2C_Cmd(I2C_CMD_STOP);
        I2C_Wait();
        return;
    }
    i = 0;
    while (i < n)
    {
        if (OLED_ColorMode != 0)
        {
            v = d[i];
        }
        else
        {
            v = (unsigned char)(~d[i]);
        }
        if (I2C_SendByte(v) != 0)
        {
            break;
        }
        i++;
    }
    I2C_Cmd(I2C_CMD_STOP);
    I2C_Wait();
}

void OLED_SetColorMode(unsigned char m)
{
    /* Inversion is applied on the wire, not in the buffer, so a mode flip
     * leaves OLED_DisplayBuf untouched and the page diff would see nothing
     * to send (the dithering ring banner redraws identical text every
     * phase). Force one full refresh whenever the mode really changes. */
    if (m != OLED_ColorMode)
    {
        OLED_ColorMode = m;
        OLEDForceFull = 1;
    }
}

void OLED_SetCursor(unsigned char page, unsigned char x)
{
    OLED_WriteCommand((unsigned char)(0xB0 | page));
    OLED_WriteCommand((unsigned char)(0x10 | ((x & 0xF0) >> 4)));
    OLED_WriteCommand((unsigned char)(0x00 | (x & 0x0F)));
}

void OLED_Init(void)
{
    static unsigned char code init_tab[] = {
        0xAE, 0xD5, 0xF0, 0xA8, 0x3F, 0xD3, 0x00, 0x40,
        0xA1, 0xC8, 0xDA, 0x12, 0x81, 0xDF, 0xD9, 0xF1,
        0xDB, 0x30, 0xA4, 0xA6, 0x8D, 0x14, 0xAF
    };
    unsigned char i;
    OLED_I2C_Init();
    i = 0;
    while (i < (unsigned char)sizeof(init_tab))
    {
        OLED_WriteCommand(init_tab[i]);
        i++;
    }
    OLED_ClearBuf();
    /* Panel GDDRAM is undefined at power on: the first frame must be pushed
     * in full, never diffed against the all-zero shadow. */
    OLEDForceFull = 1;
    OLED_Update();
}

/* Compare one page of the mirror against what the panel already holds.
 * Returns 1 when any byte differs, i.e. when the page must be re-sent. */
static unsigned char OLED_PageChanged(unsigned char p)
{
    unsigned char i;
    i = 0;
    while (i < 128)
    {
        if (OLED_DisplayBuf[p][i] != OLEDShadowBuf[p][i])
        {
            return 1;
        }
        i++;
    }
    return 0;
}

/* Copy one page of the mirror into the shadow after a successful transfer.
 * Hand written loop: avoids pulling in a memcpy helper. */
static void OLED_PageSync(unsigned char p)
{
    unsigned char i;
    i = 0;
    while (i < 128)
    {
        OLEDShadowBuf[p][i] = OLED_DisplayBuf[p][i];
        i++;
    }
}

void OLED_Update(void)
{
    unsigned char j;
    j = 0;
    while (j < 8)
    {
        if (OLEDForceFull != 0 || OLED_PageChanged(j) != 0)
        {
            OLED_SetCursor(j, 0);
            OLED_WriteData(OLED_DisplayBuf[j], 128);
            OLED_PageSync(j);
        }
        j++;
    }
    OLEDForceFull = 0;
}

void OLED_UpdateArea(unsigned char X, unsigned char Y, unsigned char W, unsigned char H)
{
    unsigned char j;
    unsigned char page0;
    unsigned char page1;
    if (X > 127)
    {
        return;
    }
    if (Y > 63)
    {
        return;
    }
    if ((unsigned int)X + (unsigned int)W > 128)
    {
        W = (unsigned char)(128 - X);
    }
    if ((unsigned int)Y + (unsigned int)H > 64)
    {
        H = (unsigned char)(64 - Y);
    }
    page0 = (unsigned char)(Y / 8);
    page1 = (unsigned char)(((unsigned int)Y + (unsigned int)H - 1) / 8 + 1);
    j = page0;
    while (j < page1)
    {
        OLED_SetCursor(j, X);
        OLED_WriteData(&OLED_DisplayBuf[j][X], W);
        j++;
    }
}

void OLED_Brightness(int b)
{
    if (b > 255)
    {
        b = 255;
    }
    if (b < 0)
    {
        b = 0;
    }
    OLED_WriteCommand(0x81);
    OLED_WriteCommand((unsigned char)b);
}
