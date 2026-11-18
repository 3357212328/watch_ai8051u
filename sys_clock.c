/* System clock and TFPU clock setup for AI8051U.
 * Assumes EAXFR access for extended registers.
 */

#include "sys_clock.h"
#include "set_timer.h"
#include "set_i2c.h"

void SysClock_Init40MHz_HIRC(void)
{
    EAXFR = 1;
    WTST = 0;
    CKCON = 0;
    /* Drop divider first so the band switch never overclocks the core */
    CLKDIV = 0x04;
    /* Load factory 40MHz trims, then select the 44M HIRC band */
    IRTRIM = CHIPID18;
    VRTRIM = CHIPID24;
    IRCBAND |= 0x03;
    /* Run at full 40MHz with no division */
    CLKDIV = 0x00;
    /* Keep library baud and tick math on the same 40MHz base */
    set_timer_fosc(40000000L);
    set_i2c_fosc(40000000L);
}

void TFPU_Init120MHz(void)
{
    unsigned int i;
    EAXFR = 1;
    /* Clear CKMS (96M PLL path), then select the 144M PLL output */
    CLKSEL &= 0x7F;
    CLKSEL |= 0x80;
    /* PLL input divider /4 for the 40MHz HIRC source */
    USBCLK &= 0x9F;
    USBCLK |= 0x40;
    /* Start the PLL */
    USBCLK |= 0x80;
    /* Wait well over 50us for the PLL to lock at 40MHz */
    i = 2000;
    while (i > 0)
    {
        i--;
        _nop_();
        _nop_();
    }
    /* No HSCLK division, route PLL to the high speed clock */
    HSCLKDIV = 0;
    CLKSEL |= 0x40;
    /* TFPU divider 1 on the max PLL tap: nominal 120MHz target, 108MHz
     * limit waived per user confirmation; verify actual rate by scope. */
    TFPU_CLKDIV = 1;
}
