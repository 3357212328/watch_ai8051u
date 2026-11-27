/* Hardware layer: keys, software encoder, tick timer and delays for OLED_UI.
 * Keys: P32 Enter, P33 Back, P36 Up, P37 Down, all active low with pull-up.
 * Encoder: software quadrature on P10(A) / P11(B) with pull-up, polled decode.
 */

#include "oled_ui_driver.h"
#include "set_io.h"
#include "set_timer.h"

/* Quadrature accumulator in quarter steps, kept between Encoder_Get calls */
static long edata enc_acc;
/* Last accepted AB state, previous raw sample (2-poll debounce), enable flag */
static unsigned char edata enc_last;
static unsigned char edata enc_deb;
static unsigned char edata enc_en;

void Key_Init(void)
{
    set_io_mode(pu_mode, Pin32, Pin33, Pin36, Pin37, Pin35, Pin_End);
    set_io_mode(en_pur, Pin32, Pin33, Pin36, Pin37, Pin35, Pin_End);
}

unsigned char Key_GetEnterStatus(void)
{
    return (unsigned char)P32;
}

unsigned char Key_GetBackStatus(void)
{
    return (unsigned char)P33;
}

unsigned char Key_GetUpStatus(void)
{
    return (unsigned char)P36;
}

unsigned char Key_GetDownStatus(void)
{
    return (unsigned char)P37;
}

unsigned char Key_GetHomeStatus(void)
{
    return (unsigned char)P35;
}

void Timer_Init(void)
{
    /* 20ms tick on 16-bit auto-reload Timer11 */
    set_timer_mode(Timer11, "20ms", Timer_End);
}

void Encoder_Init(void)
{
    set_io_mode(pu_mode, Pin10, Pin11, Pin_End);
    set_io_mode(en_pur, Pin10, Pin11, Pin_End);
    enc_acc = 0;
    enc_last = 0;
    if (P10)
    {
        enc_last |= 0x02;
    }
    if (P11)
    {
        enc_last |= 0x01;
    }
    enc_deb = enc_last;
    enc_en = 1;
}

void Encoder_Enable(void)
{
    /* Re-sync phase so enabling never injects a phantom step */
    enc_last = 0;
    if (P10)
    {
        enc_last |= 0x02;
    }
    if (P11)
    {
        enc_last |= 0x01;
    }
    enc_deb = enc_last;
    enc_en = 1;
}

void Encoder_Disable(void)
{
    enc_en = 0;
}

void Encoder_FastPoll(void)
{
    unsigned char cur;
    unsigned char idx;
    cur = 0;
    if (P10)
    {
        cur |= 0x02;
    }
    if (P11)
    {
        cur |= 0x01;
    }
    /* Accept only 2 consecutive equal samples (contact debounce) */
    if (cur == enc_deb)
    {
        if (enc_en != 0 && cur != enc_last)
        {
            /* Forward cycle 00->01->11->10->00 counts up, reverse counts down */
            idx = (unsigned char)((enc_last << 2) | cur);
            if (idx == 0x01 || idx == 0x07 || idx == 0x0E || idx == 0x08)
            {
                enc_acc++;
            }
            if (idx == 0x02 || idx == 0x0B || idx == 0x0D || idx == 0x04)
            {
                enc_acc--;
            }
        }
        enc_last = cur;
    }
    enc_deb = cur;
}

short Encoder_Get(void)
{
    short result;
    result = 0;
    if (enc_en != 0)
    {
        /* Same /4 post-division as the timer encoder version */
        result = (short)(enc_acc / 4);
        enc_acc = enc_acc % 4;
    }
    return result;
}

void Delay_us(unsigned long x)
{
    unsigned int i;
    while (x > 0)
    {
        x--;
        /* About 40 clocks per microsecond at 40MHz 1T */
        i = 8;
        while (i > 0)
        {
            i--;
            _nop_();
            _nop_();
            _nop_();
            _nop_();
        }
    }
}

void Delay_ms(unsigned long x)
{
    unsigned int i;
    while (x > 0)
    {
        x--;
        /* About 4000 loop iterations per millisecond at 40MHz 1T */
        i = 4000;
        while (i > 0)
        {
            i--;
            _nop_();
        }
    }
}

void Delay_s(unsigned long x)
{
    while (x > 0)
    {
        x--;
        Delay_ms(1000UL);
    }
}
