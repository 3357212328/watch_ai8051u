/* Optional W25Q SPI NOR mirror for the settings record.
 *
 * SPI0 is driven straight from the registers (blocking, polled): the
 * generic set_spi module is DMA/interrupt based and pulls in sscanf,
 * which is far more than a 15-byte record needs.
 *
 * Mode: SSIG=1 (CS is the plain P5.2 GPIO, P1.4 stays free) forces
 * CPHA=1; with CPOL=1 that is SPI mode 3, which W25Q supports, so
 * SPCTL = 0xDC (SSIG + SPEN + MSTR + CPOL + CPHA, clk/4).
 * The manual is explicit that CPHA=0 requires SSIG=0, so the earlier
 * SSIG=1/CPHA=0 value left the shifter without a clock.
 *
 * Every wait is bounded: a missing or miswired chip can never hang the
 * boot path, it just fails the probe and is ignored.
 */

#include "watch_flash.h"
#include "set_io.h"
#include "oled_ui_driver.h"

#define FLASH_CS        P52
#define FLASH_PIN_CS    Pin52

#define W25_CMD_WRITE_ENABLE    0x06
#define W25_CMD_READ_STATUS     0x05
#define W25_CMD_READ_DATA       0x03
#define W25_CMD_PAGE_PROGRAM    0x02
#define W25_CMD_SECTOR_ERASE    0x20
#define W25_CMD_JEDEC_ID        0x9F

static uint8_t xdata FlashOk;

/* One full-duplex byte at clk/4 (10 MHz at 40 MHz core). The guard
 * caps the wait at ~50 us: a dead SPI bus must not stop the boot. */
static uint8_t Flash_Xfer(uint8_t out)
{
    uint16_t guard;

    SPDAT = out;
    guard = 0;
    while (SPIF == 0)
    {
        guard++;
        if (guard > 2000U)
        {
            break;
        }
    }
    SPSTAT = 0xC0;              /* write 1 to clear SPIF and WCOL */
    return SPDAT;
}

/* Poll the status register until WIP clears. 2 ms steps, 600 ms cap,
 * which covers the 400 ms worst-case sector erase. */
static uint8_t Flash_WaitBusy(void)
{
    unsigned int guard;

    FLASH_CS = 0;
    (void)Flash_Xfer(W25_CMD_READ_STATUS);
    guard = 0;
    while ((Flash_Xfer(0xFF) & 0x01U) != 0U)
    {
        FLASH_CS = 1;
        Delay_ms(2);
        FLASH_CS = 0;
        (void)Flash_Xfer(W25_CMD_READ_STATUS);
        guard++;
        if (guard > 300U)
        {
            FLASH_CS = 1;
            return 1;
        }
    }
    FLASH_CS = 1;
    return 0;
}

static void Flash_SendAddr(uint32_t addr)
{
    (void)Flash_Xfer((uint8_t)(addr >> 16));
    (void)Flash_Xfer((uint8_t)(addr >> 8));
    (void)Flash_Xfer((uint8_t)addr);
}

void Watch_FlashInit(void)
{
    uint8_t id0;
    uint8_t id2;

    EAXFR = 1;                  /* 32-bit mode: enable XFR access */
    SPI_S0 = 0;                 /* P1.4..P1.7 pin group */
    SPI_S1 = 0;

    SPCTL = 0xDC;               /* SSIG, SPEN, MSTR, mode 3, clk/4 */
    SPSTAT = 0xC0;

    set_io_mode(pp_mode, Pin15, Pin17, Pin_End);    /* MOSI, SCLK */
    set_io_mode(hz_mode, Pin16, Pin_End);           /* MISO      */
    set_io_mode(pp_mode, FLASH_PIN_CS, Pin_End);
    FLASH_CS = 1;

    FlashOk = 0;
    FLASH_CS = 0;
    (void)Flash_Xfer(W25_CMD_JEDEC_ID);
    id0 = Flash_Xfer(0xFF);
    (void)Flash_Xfer(0xFF);     /* memory type: not used */
    id2 = Flash_Xfer(0xFF);
    FLASH_CS = 1;

    /* Only a real manufacturer code counts: with a dead bus the port
     * would read back 0x00/0xFF/0x9F (the command byte), none of which
     * is in the list, so a missing chip can never look present. */
    if ((id0 == 0xEFU || id0 == 0xC8U || id0 == 0x20U ||
         id0 == 0x9DU || id0 == 0x1CU || id0 == 0x0BU) &&
        id2 >= 0x15U && id2 <= 0x20U)
    {
        FlashOk = 1;
    }
}

uint8_t Watch_FlashPresent(void)
{
    return FlashOk;
}

uint8_t Watch_FlashRead(uint32_t addr, uint8_t *buf, uint16_t len)
{
    uint16_t i;

    if (FlashOk == 0)
    {
        return 1;
    }
    FLASH_CS = 0;
    (void)Flash_Xfer(W25_CMD_READ_DATA);
    Flash_SendAddr(addr);
    for (i = 0; i < len; i++)
    {
        buf[i] = Flash_Xfer(0xFF);
    }
    FLASH_CS = 1;
    return 0;
}

uint8_t Watch_FlashWrite(uint32_t addr, const uint8_t *buf, uint16_t len)
{
    uint16_t done;
    uint16_t chunk;
    uint16_t room;
    uint16_t i;

    if (FlashOk == 0)
    {
        return 1;
    }
    done = 0;
    while (done < len)
    {
        /* A page program cannot cross a 256-byte boundary */
        room = (uint16_t)(256U - (uint16_t)((addr + done) & 0x00FFU));
        chunk = (uint16_t)(len - done);
        if (chunk > room)
        {
            chunk = room;
        }

        FLASH_CS = 0;
        (void)Flash_Xfer(W25_CMD_WRITE_ENABLE);
        FLASH_CS = 1;

        FLASH_CS = 0;
        (void)Flash_Xfer(W25_CMD_PAGE_PROGRAM);
        Flash_SendAddr(addr + done);
        for (i = 0; i < chunk; i++)
        {
            (void)Flash_Xfer(buf[done + i]);
        }
        FLASH_CS = 1;

        if (Flash_WaitBusy() != 0)
        {
            return 1;
        }
        done = (uint16_t)(done + chunk);
    }
    return 0;
}

uint8_t Watch_FlashEraseSector(uint32_t addr)
{
    if (FlashOk == 0)
    {
        return 1;
    }
    FLASH_CS = 0;
    (void)Flash_Xfer(W25_CMD_WRITE_ENABLE);
    FLASH_CS = 1;

    FLASH_CS = 0;
    (void)Flash_Xfer(W25_CMD_SECTOR_ERASE);
    Flash_SendAddr(addr);
    FLASH_CS = 1;

    return Flash_WaitBusy();
}
