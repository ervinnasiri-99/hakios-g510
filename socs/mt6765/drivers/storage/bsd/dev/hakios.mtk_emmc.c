/*
 * The eMMC's `xnusys` partition as a read-only block device and the root for MT6765.
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

#define MTK_EMMC_SECTOR_SIZE 512

static uint64_t mtk_emmc_size;
static lck_grp_t *mtk_emmc_lock_grp;
static lck_mtx_t *mtk_emmc_lock;

static int
mtk_emmc_open(dev_t dev, int flags, int fmt, struct proc *p)
{
	return 0;
}

static int
mtk_emmc_close(dev_t dev, int flags, int fmt, struct proc *p)
{
	return 0;
}

static int
mtk_emmc_strategy(struct buf *bp)
{
	uint64_t lba = bp->bl_bno;
	uint32_t nblk = (bp->b_bcount + MTK_EMMC_SECTOR_SIZE - 1) / MTK_EMMC_SECTOR_SIZE;

	if (bp->bl_bcount % MTK_EMMC_SECTOR_SIZE != 0) {
		bp->b_error = EINVAL;
		biodone(bp);
		return 0;
	}

	if (lba + nblk > mtk_emmc_size / MTK_EMMC_SECTOR_SIZE) {
		bp->b_error = EINVAL;
		biodone(bp);
		return 0;
	}

	lck_mtx_lock(mtk_emmc_lock);
	/* TODO: call through to mtkloader's msdc_read() via a trampoline or exported function */
	// int ok = msdc_read(lba, nblk, bp->b_data);
	// if (!ok) bp->b_error = EIO;
	lck_mtx_unlock(mtk_emmc_lock);

	biodone(bp);
	return 0;
}

static int
mtk_emmc_ioctl(dev_t dev, u_long cmd, caddr_t data, int flags, struct proc *p)
{
	return ENOTTY;
}

static struct cdevsw mtk_emmc_cdevsw = {
	.d_open = mtk_emmc_open,
	.d_close = mtk_emmc_close,
	.d_strategy = mtk_emmc_strategy,
	.d_ioctl = mtk_emmc_ioctl,
	.d_type = D_DISK,
};

void
hakios_mtk_emmc_init(void)
{
	mtk_emmc_lock_grp = lck_grp_alloc_init("mtk_emmc", LCK_GRP_ATTR_NULL);
	mtk_emmc_lock = lck_mtx_alloc_init(mtk_emmc_lock_grp, LCK_ATTR_NULL);

	/* TODO: query actual size from GPT partition table */
	mtk_emmc_size = 4 * 1024 * 1024 * 1024ULL; /* 4 GB placeholder */

	dev_t dev = 0;
	cdevsw_add(-1, &mtk_emmc_cdevsw, &dev);
	(void)dev; /* suppress unused warning if not used */
}