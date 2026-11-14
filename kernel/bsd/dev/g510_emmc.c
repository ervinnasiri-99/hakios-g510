/*
 * The eMMC's `xnusys` partition as a read-only block device and the root.
 * License: APSL 2.0
 */
#include <sys/types.h>
#include <sys/param.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/disk.h>
#include <sys/vnode.h>
#include <sys/malloc.h>
#include <kern/locks.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include "g510_emmc.h"

#define G510_EMMC_SECTOR_SIZE 512

static uint64_t g510_emmc_size;
static lck_grp_t *g510_emmc_lock_grp;
static lck_mtx_t *g510_emmc_lock;

static int
g510_emmc_open(dev_t dev, int flags, int fmt, struct proc *p)
{
	return 0;
}

static int
g510_emmc_close(dev_t dev, int flags, int fmt, struct proc *p)
{
	return 0;
}

static int
g510_emmc_strategy(struct buf *bp)
{
	uint64_t lba = bp->bl_bno;
	uint32_t nblk = (bp->b_bcount + G510_EMMC_SECTOR_SIZE - 1) / G510_EMMC_SECTOR_SIZE;

	if (bp->bl_bcount % G510_EMMC_SECTOR_SIZE != 0) {
		bp->b_error = EINVAL;
		biodone(bp);
		return 0;
	}

	if (lba + nblk > g510_emmc_size / G510_EMMC_SECTOR_SIZE) {
		bp->b_error = EINVAL;
		biodone(bp);
		return 0;
	}

	lck_mtx_lock(g510_emmc_lock);
	/* TODO: call through to mtkloader's msdc_read() via a trampoline or exported function */
	// int ok = msdc_read(lba, nblk, bp->b_data);
	// if (!ok) bp->b_error = EIO;
	lck_mtx_unlock(g510_emmc_lock);

	biodone(bp);
	return 0;
}

static int
g510_emmc_ioctl(dev_t dev, u_long cmd, caddr_t data, int flags, struct proc *p)
{
	return ENOTTY;
}

static struct cdevsw g510_emmc_cdevsw = {
	.d_open = g510_emmc_open,
	.d_close = g510_emmc_close,
	.d_strategy = g510_emmc_strategy,
	.d_ioctl = g510_emmc_ioctl,
	.d_type = D_DISK,
};

void
g510_emmc_init(void)
{
	g510_emmc_lock_grp = lck_grp_alloc_init("g510_emmc", LCK_GRP_ATTR_NULL);
	g510_emmc_lock = lck_mtx_alloc_init(g510_emmc_lock_grp, LCK_ATTR_NULL);

	/* TODO: query actual size from GPT partition table */
	g510_emmc_size = 4 * 1024 * 1024 * 1024ULL; /* 4 GB placeholder */

	dev_t dev = 0;
	cdevsw_add(-1, &g510_emmc_cdevsw, &dev);
	(void)dev; /* suppress unused warning if not used */
}