/*
 * The eMMC's `xnusys` partition as a read-only block device and the root.
 * License: APSL 2.0
 */
#ifndef _G510_EMMC_H_
#define _G510_EMMC_H_

#include <sys/types.h>

#define G510_EMMC_SECTOR_SIZE 512

void g510_emmc_init(void);

#endif /* _G510_EMMC_H_ */