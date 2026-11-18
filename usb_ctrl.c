/* USB-CDC console driving the OLED_UI input path.
 * Transport: STC USB-CDC virtual serial (ai_usb.h + stc_usb_cdc_32g.LIB).
 * Host bytes arrive in UsbOutBuffer[0..OutNumber-1] with bUsbOutReady set;
 * usb_OUT_done() re-arms the endpoint. Pulses staged here last exactly
 * one 20ms UI tick, so the existing press/release edge logic just works.
 *
 * ---------------- RTC time sync (STC-ISP / AI-ISP "Set RTC") ----------------
 * The STC download tools can push the PC clock straight into the on-chip
 * RTC over this same CDC port. Usage constraints for the host tool:
 *   1. Leave the user-defined command header EMPTY in the tool. The frame
 *      is then parsed as a plain binary burst, no ASCII header prefix.
 *   2. The frame is pure binary HEX, NOT BCD:
 *      [century][year][month][day][weekday][hour][min][sec][subsec][ms_hi][ms_lo]
 *      century = value 20 as raw byte (0x14 or 0x20, both accepted as the
 *      sync candidate), year 0..99 means 20YY, weekday 0..6 = Sun..Sat,
 *      subsec 0..127 in 1/128 s units, millisecond big-endian 0..999.
 *      I.e. 1 + 8 single-byte fields + 2 millisecond bytes = 11 bytes
 *      (indices 0..10) collected after the leading century byte.
 *   3. Frame bytes never reach the console decoder (a subsecond/millisecond
 *      byte such as 0x65 = 'e' would otherwise trigger a phantom key
 *      press). Normal keypress ASCII ('e','b','w','s','+','-') never
 *      matches the 0x14/0x20 sync byte, so typing is unaffected.
 *   4. The frame must contain a weekday byte consistent with the date
 *      (checked with Zeller's congruence, via WatchTime_Weekday).
 *   5. If the tool reports an error / the clock did not move, just click
 *      "Set RTC" again - a partial or corrupted burst is dropped whole.
 * The parser keeps state across USB packets, so a frame split over two
 * OUT transfers is still reassembled.
 */

#include "usb_ctrl.h"
#include "ai_usb.h"
#include "watch_time.h"

/* Staged virtual presses, cleared on take (consumed once per 20 ms
 * tick - not hot, parked in xdata per edata policy) */
static unsigned char xdata uc_enter;
static unsigned char xdata uc_back;
static unsigned char xdata uc_up;
static unsigned char xdata uc_down;
/* Staged encoder detents, cleared on take */
static short xdata uc_enc;

/* ------- STC-ISP / AI-ISP "Set RTC" time-sync incremental parser -------
 * rtc_stage counts frame bytes collected: 0 = hunting for the century
 * sync byte, 1..11 = position inside the binary frame. State survives
 * across USB packets so frames split over OUT transfers reassemble; any
 * out-of-range byte resets the machine and re-judges that byte as a new
 * sync candidate. Frame bytes are consumed, never forwarded to
 * USBCtrl_Decode (parked in xdata per edata policy). */
static unsigned char xdata rtc_stage;
static unsigned char xdata rtc_buf[11];

/* Final step: copy the validated frame into the on-chip RTC. Parameters
 * are already range-checked, WatchTime_Set clamps/validates once more
 * and runs the SETRTC reload. Milliseconds were checked (<=999) but not
 * stored: the RTC reload zeroes the subsecond counter itself (existing
 * WatchTime_Set semantics, INISSEC = 0). */
static void USBCtrl_RTCCommit(void)
{
    WatchTime t;

    t.Year   = rtc_buf[1];
    t.Month  = rtc_buf[2];
    t.Day    = rtc_buf[3];
    t.Hour   = rtc_buf[5];
    t.Minute = rtc_buf[6];
    t.Second = rtc_buf[7];
    t.SubSec = rtc_buf[8];
    WatchTime_Set(&t);
    /* Keil C251 printf: %bu = unsigned char argument, zero padded. */
    printf_usb("Time set: 20%02bu-%02bu-%02bu %02bu:%02bu:%02bu\r\n",
               t.Year, t.Month, t.Day, t.Hour, t.Minute, t.Second);
}

/* Feed one host byte into the RTC frame parser.
 * Returns 1 if the byte was consumed by the parser (never forward it to
 * the console decoder), 0 if it is free for normal key decoding. */
static unsigned char USBCtrl_RTCFeed(unsigned char c)
{
    unsigned char ok;
    unsigned int ms;

    ok = 0;
    if (rtc_stage == 0)
    {
        /* Sync hunt: a century byte (0x14 or 0x20 raw) opens a frame */
        if (c == 0x14 || c == 0x20)
        {
            rtc_buf[0] = c;
            rtc_stage = 1;
            ok = 1;
        }
    }
    else
    {
        /* Incremental range check by incoming frame position */
        switch (rtc_stage)
        {
        case 1: /* year 0..99 */
            ok = (c <= 99);
            break;
        case 2: /* month 1..12 */
            ok = ((c >= 1) && (c <= 12));
            break;
        case 3: /* day 1..31 */
            ok = ((c >= 1) && (c <= 31));
            break;
        case 4: /* weekday 0..6 */
            ok = (c <= 6);
            break;
        case 5: /* hour 0..23 */
            ok = (c <= 23);
            break;
        case 6: /* minute 0..59 */
            ok = (c <= 59);
            break;
        case 7: /* second 0..59 */
            ok = (c <= 59);
            break;
        case 8: /* subsec 0..127 */
            ok = (c <= 127);
            break;
        case 9: /* millisecond hi, bound-checked with lo at the end */
        case 10: /* millisecond lo */
            ok = 1;
            break;
        default:
            ok = 0;
            break;
        }
        if (ok)
        {
            rtc_buf[rtc_stage] = c;
            rtc_stage = (unsigned char)(rtc_stage + 1);
            if (rtc_stage >= 11)
            {
                /* Frame complete: joint ms bound + weekday/date
                 * consistency check; discard the whole frame on a
                 * mismatch, either way the machine is rearmed. */
                ms  = (unsigned int)rtc_buf[9] * 256U;
                ms += (unsigned int)rtc_buf[10];
                if (ms <= 999 &&
                    rtc_buf[4] == WatchTime_Weekday(rtc_buf[1], rtc_buf[2], rtc_buf[3]))
                {
                    USBCtrl_RTCCommit();
                }
                rtc_stage = 0;
            }
        }
        else
        {
            /* Out of range: drop the half frame, then re-judge this
             * byte as a fresh sync candidate before decoding it. */
            rtc_stage = 0;
            if (c == 0x14 || c == 0x20)
            {
                rtc_buf[0] = c;
                rtc_stage = 1;
                ok = 1;
            }
        }
    }
    return ok;
}

static void USBCtrl_Decode(unsigned char c)
{
    if (c >= 'A' && c <= 'Z')
    {
        c = (unsigned char)(c + 32);
    }
    switch (c)
    {
    case 'e':
        uc_enter = 1;
        break;
    case 'b':
        uc_back = 1;
        break;
    case 'w':
        uc_up = 1;
        break;
    case 's':
        uc_down = 1;
        break;
    case '+':
        uc_enc++;
        break;
    case '-':
        uc_enc--;
        break;
    case 'h':
    case '?':
        printf_usb("OLED_UI keys: e/b/w/s +/-(enc) h=help\r\n");
        break;
    default:
        break;
    }
}

void USBCtrl_Init(void)
{
    uc_enter = 0;
    uc_back = 0;
    uc_up = 0;
    uc_down = 0;
    uc_enc = 0;
    usb_init();
    printf_usb("AI8051U OLED_UI USB console ready\r\n");
    printf_usb("e=enter b=back w=up s=down +=enc+ -=enc- h=help\r\n");
}

void USBCtrl_Poll(void)
{
    int i;
    int n;
    unsigned char c;
    if (bUsbOutReady == 0)
    {
        return;
    }
    if (DeviceState != DEVSTATE_CONFIGURED)
    {
        usb_OUT_done();
        return;
    }
    n = OutNumber;
    if (n < 0)
    {
        n = 0;
    }
    if (n > 64)
    {
        n = 64;
    }
    for (i = 0; i < n; i++)
    {
        c = UsbOutBuffer[i];
        /* RTC frame parser first; frame bytes must not leak into the
         * console decoder (see header comment). */
        if (USBCtrl_RTCFeed(c) == 0)
        {
            USBCtrl_Decode(c);
        }
    }
    usb_OUT_done();
}

unsigned char USBCtrl_TakeEnter(void)
{
    unsigned char v;
    v = uc_enter;
    uc_enter = 0;
    return v;
}

unsigned char USBCtrl_TakeBack(void)
{
    unsigned char v;
    v = uc_back;
    uc_back = 0;
    return v;
}

unsigned char USBCtrl_TakeUp(void)
{
    unsigned char v;
    v = uc_up;
    uc_up = 0;
    return v;
}

unsigned char USBCtrl_TakeDown(void)
{
    unsigned char v;
    v = uc_down;
    uc_down = 0;
    return v;
}

short USBCtrl_TakeEncoder(void)
{
    short v;
    v = uc_enc;
    uc_enc = 0;
    return v;
}
