/*
 * Audio codec driver for the G510 (MT6765, MT6357/MT6358 PMIC).
 * The MT6358 is the PMIC with integrated audio codec (I2S, AFE, ADDA).
 * License: APSL 2.0
 */
#include <kern/clock.h>
#include <libkern/libkern.h>
#include <mach/mach_types.h>
#include <pexpert/pexpert.h>
#include <sys/systm.h>
#include <sys/redwood_input.h>
#include "g510_mtkpwrap.h"
#include "g510_mtkaudio.h"

extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
extern void redwood_keep(const char *format, ...) __printflike(1, 2);
extern void delay(int usec);
extern void delay_for_interval(uint32_t interval, uint32_t scale_factor);

#define AFE_PHYS                0x11220000u
#define AFE_BASE                0x11220000u

/* AFE registers (Audio Front End) */
#define AFE_DL1_BASE            0x0000
#define AFE_DL1_CUR             0x0004
#define AFE_DL1_END             0x0008
#define AFE_DL1_CTRL            0x000C
#define AFE_DL2_BASE            0x0010
#define AFE_DL2_CUR             0x0014
#define AFE_DL2_END             0x0018
#define AFE_DL2_CTRL            0x001C
#define AFE_DL3_BASE            0x0020
#define AFE_DL3_CUR             0x0024
#define AFE_DL3_END             0x0028
#define AFE_DL3_CTRL            0x002C
#define AFE_VUL_BASE            0x0030
#define AFE_VUL_CUR             0x0034
#define AFE_VUL_END             0x0038
#define AFE_VUL_CTRL            0x003C
#define AFE_VUL2_BASE           0x0040
#define AFE_VUL2_CUR            0x0044
#define AFE_VUL2_END            0x0048
#define AFE_VUL2_CTRL           0x004C
#define AFE_DAI_BASE            0x0050
#define AFE_DAI_CUR             0x0054
#define AFE_DAI_END             0x0058
#define AFE_DAI_CTRL            0x005C
#define AFE_DAI2_BASE           0x0060
#define AFE_DAI2_CUR            0x0064
#define AFE_DAI2_END            0x0068
#define AFE_DAI2_CTRL           0x006C
#define AFE_AWB_BASE            0x0070
#define AFE_AWB_CUR             0x0074
#define AFE_AWB_END             0x0078
#define AFE_AWB_CTRL            0x007C
#define AFE_VUL3_BASE           0x0080
#define AFE_VUL3_CUR            0x0084
#define AFE_VUL3_END            0x0088
#define AFE_VUL3_CTRL           0x008C
#define AFE_DL4_BASE            0x0090
#define AFE_DL4_CUR             0x0094
#define AFE_DL4_END             0x0098
#define AFE_DL4_CTRL            0x009C
#define AFE_DL5_BASE            0x00A0
#define AFE_DL5_CUR             0x00A4
#define AFE_DL5_END             0x00A8
#define AFE_DL5_CTRL            0x00AC
#define AFE_DL6_BASE            0x00B0
#define AFE_DL6_CUR             0x00B4
#define AFE_DL6_END             0x00B8
#define AFE_DL6_CTRL            0x00BC
#define AFE_I2S_CON             0x0100
#define AFE_I2S2_CON            0x0104
#define AFE_I2S3_CON            0x0108
#define AFE_I2S4_CON            0x010C
#define AFE_CONN_BASE           0x0200
#define AFE_CONN_END            0x0204
#define AFE_CONN_CTRL           0x0208
#define AFE_IRQ_MCU_EN          0x0300
#define AFE_IRQ_MCU_CLR         0x0304
#define AFE_IRQ_MCU_CNT         0x0308
#define AFE_IRQ_MCU_MON         0x030C
#define AFE_IRQ1_MCU_EN         0x0310
#define AFE_IRQ1_MCU_CLR        0x0314
#define AFE_IRQ1_MCU_CNT        0x0318
#define AFE_IRQ1_MCU_MON        0x031C
#define AFE_IRQ2_MCU_EN         0x0320
#define AFE_IRQ2_MCU_CLR        0x0324
#define AFE_IRQ2_MCU_CNT        0x0328
#define AFE_IRQ2_MCU_MON        0x032C
#define AFE_IRQ3_MCU_EN         0x0330
#define AFE_IRQ3_MCU_CLR        0x0334
#define AFE_IRQ3_MCU_CNT        0x0338
#define AFE_IRQ3_MCU_MON        0x033C
#define AFE_IRQ4_MCU_EN         0x0340
#define AFE_IRQ4_MCU_CLR        0x0344
#define AFE_IRQ4_MCU_CNT        0x0348
#define AFE_IRQ4_MCU_MON        0x034C
#define AFE_IRQ5_MCU_EN         0x0350
#define AFE_IRQ5_MCU_CLR        0x0354
#define AFE_IRQ5_MCU_CNT        0x0358
#define AFE_IRQ5_MCU_MON        0x035C
#define AFE_IRQ6_MCU_EN         0x0360
#define AFE_IRQ6_MCU_CLR        0x0364
#define AFE_IRQ6_MCU_CNT        0x0368
#define AFE_IRQ6_MCU_MON        0x036C
#define AFE_IRQ7_MCU_EN         0x0370
#define AFE_IRQ7_MCU_CLR        0x0374
#define AFE_IRQ7_MCU_CNT        0x0378
#define AFE_IRQ7_MCU_MON        0x037C
#define AFE_IRQ8_MCU_EN         0x0380
#define AFE_IRQ8_MCU_CLR        0x0384
#define AFE_IRQ8_MCU_CNT        0x0388
#define AFE_IRQ8_MCU_MON        0x038C

/* AFE I2S_CON bits */
#define I2S_CON_I2S_EN          (1u << 0)
#define I2S_CON_I2S_MASTER      (1u << 1)
#define I2S_CON_I2S_FMT_I2S     (0u << 2)
#define I2S_CON_I2S_FMT_LJ      (1u << 2)
#define I2S_CON_I2S_FMT_RJ      (2u << 2)
#define I2S_CON_I2S_FMT_DSP     (3u << 2)
#define I2S_CON_I2S_WLEN_16     (0u << 4)
#define I2S_CON_I2S_WLEN_32     (1u << 4)
#define I2S_CON_I2S_WLEN_24     (2u << 4)
#define I2S_CON_I2S_WLEN_8      (3u << 4)
#define I2S_CON_I2S_CH_STEREO   (0u << 6)
#define I2S_CON_I2S_CH_MONO     (1u << 6)
#define I2S_CON_I2S_SLAVE       (0u << 7)
#define I2S_CON_I2S_MSB         (0u << 8)
#define I2S_CON_I2S_LSB         (1u << 8)

/* AFE DL1_CTRL bits */
#define DL_CTRL_ON              (1u << 0)
#define DL_CTRL_LOOPBACK        (1u << 1)

/* TopCKGEN for clock control */
#define TOPCKGEN_PHYS           0x10000000u
#define CLK_CFG_0               0x0000
#define CLK_CFG_1               0x0004
#define CLK_CFG_2               0x0008
#define CLK_CFG_3               0x000C
#define CLK_CFG_4               0x0010
#define CLK_CFG_5               0x0014
#define CLK_CFG_6               0x0018
#define CLK_CFG_7               0x001C
#define CLK_CFG_UPDATE          0x0020
#define CLK_CFG_0_SET           0x0024
#define CLK_CFG_0_CLR           0x0028
#define CLK_CFG_1_SET           0x002C
#define CLK_CFG_1_CLR           0x0030
#define CLK_CFG_2_SET           0x0034
#define CLK_CFG_2_CLR           0x0038
#define CLK_CFG_3_SET           0x003C
#define CLK_CFG_3_CLR           0x0040
#define CLK_CFG_4_SET           0x0044
#define CLK_CFG_4_CLR           0x0048
#define CLK_CFG_5_SET           0x004C
#define CLK_CFG_5_CLR           0x0050
#define CLK_CFG_6_SET           0x0054
#define CLK_CFG_6_CLR           0x0058
#define CLK_CFG_7_SET           0x005C
#define CLK_CFG_7_CLR           0x0060

/* APMIXEDSYS for audio clocks */
#define APMIXED_PHYS            0x10200000u
#define APMIXED_AUD_TOP_CK_CON0 0x0000
#define APMIXED_AUD_TOP_CK_CON1 0x0004
#define APMIXED_AUD_TOP_CK_CON2 0x0008
#define APMIXED_AUD_TOP_CK_CON3 0x000C
#define APMIXED_AUD_TOP_CK_CON4 0x0010
#define APMIXED_AUD_TOP_CK_CON5 0x0014
#define APMIXED_AUD_TOP_CK_CON6 0x0018
#define APMIXED_AUD_TOP_CK_CON7 0x001C

/* APMIXED AUD_TOP_CK_CON bits */
#define AUD_TOP_CK_AUD_EN       (1u << 0)
#define AUD_TOP_CK_AUD_1_DIV    (1u << 1)
#define AUD_TOP_CK_AUD_2_DIV    (1u << 2)
#define AUD_TOP_CK_AUD_ENGEN    (1u << 3)

static vm_offset_t g510_afe;
static vm_offset_t g510_topckgen;
static vm_offset_t g510_apmixed;

static uint32_t
afe_read(uint32_t offset)
{
	return *(volatile uint32_t *)(g510_afe + offset);
}

static void
afe_write(uint32_t offset, uint32_t value)
{
	*(volatile uint32_t *)(g510_afe + offset) = value;
}

static uint32_t
topckgen_read(uint32_t offset)
{
	return *(volatile uint32_t *)(g510_topckgen + offset);
}

static void
topckgen_write(uint32_t offset, uint32_t value)
{
	*(volatile uint32_t *)(g510_topckgen + offset) = value;
}

static uint32_t
apmixed_read(uint32_t offset)
{
	return *(volatile uint32_t *)(g510_apmixed + offset);
}

static void
apmixed_write(uint32_t offset, uint32_t value)
{
	*(volatile uint32_t *)(g510_apmixed + offset) = value;
}

/*
 * Enable audio clocks in TOPCKGEN and APMIXEDSYS.
 * Returns 0 on success, error code on failure.
 */
static int
g510_mtkaudio_enable_clocks(void)
{
	/* Enable audio clocks in TOPCKGEN */
	topckgen_write(CLK_CFG_0_SET, (1u << 20)); /* AUD_INTBUS */
	topckgen_write(CLK_CFG_1_SET, (1u << 0) | (1u << 1)); /* AUD_1, AUD_2 */
	topckgen_write(CLK_CFG_1_SET, (1u << 2) | (1u << 3)); /* AUD_ENGEN1, AUD_ENGEN2 */
	delay(10);

	/* Enable audio clocks in APMIXEDSYS */
	apmixed_write(APMIXED_AUD_TOP_CK_CON0, AUD_TOP_CK_AUD_EN | AUD_TOP_CK_AUD_ENGEN);
	apmixed_write(APMIXED_AUD_TOP_CK_CON1, AUD_TOP_CK_AUD_EN | AUD_TOP_CK_AUD_ENGEN);
	delay(10);

	redwood_keep("g510_mtkaudio: clocks enabled");
	return 0;
}

/*
 * Configure AFE I2S interface for MT6358 codec.
 * Returns 0 on success, error code on failure.
 */
static int
g510_mtkaudio_config_i2s(void)
{
	/* Configure I2S0 (primary) for MT6358: 48kHz, 16-bit, stereo, master */
	uint32_t i2s_con = I2S_CON_I2S_EN |
	                   I2S_CON_I2S_MASTER |
	                   I2S_CON_I2S_FMT_I2S |
	                   I2S_CON_I2S_WLEN_16 |
	                   I2S_CON_I2S_CH_STEREO;
	afe_write(AFE_I2S_CON, i2s_con);
	delay(10);

	/* Configure I2S1 (secondary) if needed */
	afe_write(AFE_I2S2_CON, 0);
	afe_write(AFE_I2S3_CON, 0);
	afe_write(AFE_I2S4_CON, 0);
	delay(10);

	redwood_keep("g510_mtkaudio: I2S configured (con=0x%08X)", i2s_con);
	return 0;
}

/*
 * Initialize MT6358 audio codec via PWRAP.
 * Returns 0 on success, error code on failure.
 */
static int
g510_mtkaudio_init_codec(void)
{
	int err;

	/* Enable audio analog blocks in MT6358 */
	err = g510_pwrap_write(MT6358_AUDEN_ANA_CON0, 0xFFFF); /* Enable all analog */
	if (err) return err;
	err = g510_pwrap_write(MT6358_AUDEN_ANA_CON1, 0xFFFF);
	if (err) return err;
	err = g510_pwrap_write(MT6358_AUDEN_ANA_CON2, 0xFFFF);
	if (err) return err;
	err = g510_pwrap_write(MT6358_AUDEN_ANA_CON3, 0xFFFF);
	if (err) return err;
	err = g510_pwrap_write(MT6358_AUDEN_ANA_CON4, 0xFFFF);
	if (err) return err;
	err = g510_pwrap_write(MT6358_AUDEN_ANA_CON5, 0xFFFF);
	if (err) return err;
	err = g510_pwrap_write(MT6358_AUDEN_ANA_CON6, 0xFFFF);
	if (err) return err;
	err = g510_pwrap_write(MT6358_AUDEN_ANA_CON7, 0xFFFF);
	if (err) return err;
	err = g510_pwrap_write(MT6358_AUDEN_ANA_CON8, 0xFFFF);
	if (err) return err;
	err = g510_pwrap_write(MT6358_AUDEN_ANA_CON9, 0xFFFF);
	if (err) return err;
	err = g510_pwrap_write(MT6358_AUDEN_ANA_CON10, 0xFFFF);
	if (err) return err;
	err = g510_pwrap_write(MT6358_AUDEN_ANA_CON11, 0xFFFF);
	if (err) return err;
	err = g510_pwrap_write(MT6358_AUDEN_ANA_CON12, 0xFFFF);
	if (err) return err;
	err = g510_pwrap_write(MT6358_AUDEN_ANA_CON13, 0xFFFF);
	if (err) return err;
	err = g510_pwrap_write(MT6358_AUDEN_ANA_CON14, 0xFFFF);
	if (err) return err;
	err = g510_pwrap_write(MT6358_AUDEN_ANA_CON15, 0xFFFF);
	if (err) return err;
	err = g510_pwrap_write(MT6358_AUDEN_ANA_CON16, 0xFFFF);
	if (err) return err;
	err = g510_pwrap_write(MT6358_AUDEN_ANA_CON17, 0xFFFF);
	if (err) return err;
	err = g510_pwrap_write(MT6358_AUDEN_ANA_CON18, 0xFFFF);
	if (err) return err;
	err = g510_pwrap_write(MT6358_AUDEN_ANA_CON19, 0xFFFF);
	if (err) return err;
	err = g510_pwrap_write(MT6358_AUDEN_ANA_CON20, 0xFFFF);
	if (err) return err;
	err = g510_pwrap_write(MT6358_AUDEN_ANA_CON21, 0xFFFF);
	if (err) return err;
	err = g510_pwrap_write(MT6358_AUDEN_ANA_CON22, 0xFFFF);
	if (err) return err;
	err = g510_pwrap_write(MT6358_AUDEN_ANA_CON23, 0xFFFF);
	if (err) return err;
	err = g510_pwrap_write(MT6358_AUDEN_ANA_CON24, 0xFFFF);
	if (err) return err;
	err = g510_pwrap_write(MT6358_AUDEN_ANA_CON25, 0xFFFF);
	if (err) return err;

	delay(100); /* Wait for analog blocks to stabilize */

	redwood_keep("g510_mtkaudio: MT6358 codec initialized");
	return 0;
}

/*
 * Initialize AFE (Audio Front End) for playback and capture.
 * Returns 0 on success, error code on failure.
 */
static int
g510_mtkaudio_init_afe(void)
{
	/* Configure DL1 (playback) path */
	afe_write(AFE_DL1_CTRL, DL_CTRL_ON);
	delay(10);

	/* Configure VUL (capture) path */
	/* VUL uses I2S input from MT6358 */
	afe_write(AFE_VUL_CTRL, DL_CTRL_ON);
	delay(10);

	/* Set up AFE connections */
	afe_write(AFE_CONN_CTRL, 0x1); /* Enable AFE connections */
	delay(10);

	redwood_keep("g510_mtkaudio: AFE initialized");
	return 0;
}

/*
 * Main initialization function for MT6358 audio.
 * Called with boot-arg audio=1.
 */
void
g510_mtkaudio_init(void)
{
	uint32_t step = 0;
	uint16_t chip_id;
	int err;

	if (!PE_parse_boot_argn("audio", &step, sizeof(step)) || step == 0) {
		return;
	}

	redwood_keep("g510_mtkaudio: init started (step=%u)", step);

	/* Map hardware registers */
	g510_afe = ml_io_map(AFE_PHYS, 0x10000);
	g510_topckgen = ml_io_map(TOPCKGEN_PHYS, 0x1000);
	g510_apmixed = ml_io_map(APMIXED_PHYS, 0x10000);

	if (g510_afe == 0 || g510_topckgen == 0 || g510_apmixed == 0) {
		redwood_keep("g510_mtkaudio: failed to map registers");
		return;
	}

	/* Initialize PWRAP first */
	err = g510_pwrap_init();
	if (err) {
		redwood_keep("g510_mtkaudio: PWRAP init failed (%d)", err);
		return;
	}

	/* Verify PMIC communication */
	err = g510_pwrap_read_chip_id(&chip_id);
	if (err) {
		redwood_keep("g510_mtkaudio: PMIC chip ID read failed (%d)", err);
		return;
	}
	redwood_keep("g510_mtkaudio: PMIC chip ID = 0x%04X", chip_id);

	/* Enable clocks */
	err = g510_mtkaudio_enable_clocks();
	if (err) {
		redwood_keep("g510_mtkaudio: clock enable failed (%d)", err);
		return;
	}

	/* Initialize MT6358 codec */
	err = g510_mtkaudio_init_codec();
	if (err) {
		redwood_keep("g510_mtkaudio: codec init failed (%d)", err);
		return;
	}

	/* Configure I2S */
	err = g510_mtkaudio_config_i2s();
	if (err) {
		redwood_keep("g510_mtkaudio: I2S config failed (%d)", err);
		return;
	}

	/* Initialize AFE */
	err = g510_mtkaudio_init_afe();
	if (err) {
		redwood_keep("g510_mtkaudio: AFE init failed (%d)", err);
		return;
	}

	redwood_keep("g510_mtkaudio: initialization complete");
}