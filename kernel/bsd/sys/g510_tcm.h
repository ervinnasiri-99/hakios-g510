/*
 * The touch screen (Novatek NT36525, TCM protocol over SPI0).
 * License: APSL 2.0
 */
#ifndef _G510_TCM_H_
#define _G510_TCM_H_

#include <sys/types.h>

#define G510_TCM_SPI_BUS    0
#define G510_TCM_CS_PIN     17
#define G510_TCM_IRQ_PIN    1
#define G510_TCM_RESET_PIN  0

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

#define TCM_IRQ_PIN          1
#define TCM_RESET_PIN        0

#define TCM_SLOW_GAP_US      20
#define TCM_READ_CHUNK       1024
#define TCM_WRITE_CHUNK      32

void g510_tcm_init(void);

#endif /* _G510_TCM_H_ */