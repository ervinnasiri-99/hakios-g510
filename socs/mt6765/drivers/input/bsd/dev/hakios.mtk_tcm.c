/*
 * Touch screen (Novatek NT36525, TCM protocol over SPI0) for MT6765.
 * License: APSL 2.0
 */
#include <sys/types.h>
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/proc.h>
#include <dev/evdev/input.h>

#define MTK_TCM_SPI_BUS    0
#define MTK_TCM_CS_PIN     17
#define MTK_TCM_IRQ_PIN    1
#define MTK_TCM_RESET_PIN  0

#define TCM_MARKER           0xa5
#define TCM_PADDING          0x5a
#define TCM_STATUS_OK        0x01
#define TCM_STATUS_CONTINUED 0x03

#define TCM_CMD_CONTINUE_WRITE   0x01
#define TCM_CMD_IDENTIFY         0x02
#define TCM_CMD_ENABLE_REPORT    0x05
#define TCM_CMD_GET_APP_INFO     0x20
#define TCM_CMD_GET_REPORT_CONF  0x25
#define TCM_CMD_ROMBOOT_DOWNLOAD 0x45
#define TCM_ROMBOOT_UNIT         16
#define TCM_CMD_ROMBOOT_RUN      0x42

#define TCM_REPORT_IDENTIFY  0x10
#define TCM_REPORT_TOUCH     0x11

#define TCM_MODE_APPLICATION 0x01
#define TCM_MODE_HOSTDOWNLOAD 0x02
#define TCM_MODE_ROMBOOT     0x04

#define TCM_SLOW_GAP_US      20
#define TCM_READ_CHUNK       1024
#define TCM_WRITE_CHUNK      32

static int mtk_tcm_spi_stalls;

static int
mtk_tcm_xfer(const uint8_t *tx, uint8_t *rx, uint32_t len)
{
    return 1;
}

static int
mtk_tcm_irq(void)
{
    return 0;
}

static int
mtk_tcm_wait_irq(uint32_t ms)
{
    for (uint32_t i = 0; i < ms; i++) {
        if (mtk_tcm_irq()) {
            return 1;
        }
        delay(1);
    }
    return 0;
}

static int
mtk_tcm_write(uint8_t cmd, const uint8_t *payload, uint32_t len, uint32_t unit,
    uint32_t *steps)
{
    return 1;
}

static int
mtk_tcm_read(uint8_t *code, uint8_t *buf, uint32_t max, uint32_t *got)
{
    return 1;
}

static void
mtk_tcm_load_firmware(uint8_t *message)
{
}

static void
mtk_tcm_run_firmware(void)
{
}

static void
mtk_tcm_reset(void)
{
}

void
hakios_mtk_tcm_init(void)
{
}