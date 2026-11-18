#ifndef __SYS_CLOCK_H
#define __SYS_CLOCK_H

#include "AI8051U.H"
#include "DEF.H"

/* Switch system clock to 40MHz internal HIRC, no divider */
void SysClock_Init40MHz_HIRC(void);

/* Start the PLL and feed TFPU at 120MHz overclock (108MHz limit waived) */
void TFPU_Init120MHz(void);

#endif
