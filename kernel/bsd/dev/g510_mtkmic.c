/*
 * Microphone driver for the G510 (MT6765, MT6357/MT6358 PMIC).
 * The MT6358 provides microphone bias and ADC input.
 * License: APSL 2.0
 */
#include <kern/clock.h>
#include <libkern/libkern.h>
#include <mach/mach_types.h>
#include <pexpert/pexpert.h>
#include <sys/systm.h>
#include <sys/redwood_input.h>
#include "g510_mtkpwrap.h"

extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
extern void redwood_keep(const char *format, ...) __printflike(1, 2);
extern void delay(int usec);

/* MT6358 Microphone registers (via PWRAP) */
#define MT6358_MICBIAS_CON0     (0x05 << 13 | 0x1100)  /* MICBIAS0 */
#define MT6358_MICBIAS_CON1     (0x05 << 13 | 0x1102)  /* MICBIAS1 */
#define MT6358_AUDEN_ANA_CON26  (0x05 << 13 | 0x11D2)  /* MIC enable */

/* AFE registers for capture */
#define AFE_PHYS                0x11220000u
#define AFE_VUL_BASE            0x0030
#define AFE_VUL_CUR             0x0034
#define AFE_VUL_END             0x0038
#define AFE_VUL_CTRL            0x003C

static vm_offset_t g510_afe;

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

/*
 * Enable microphone bias in MT6358.
 * Returns 0 on success, error code on failure.
 */
static int
g510_mtkmic_enable_bias(void)
{
	int err;

	/* Enable MICBIAS0 (1.8V) */
	err = g510_pwrap_write(MT6358_MICBIAS_CON0, 0x8001); /* Enable, 1.8V */
	if (err) return err;

	/* Enable MICBIAS1 (1.8V) */
	err = g510_pwrap_write(MT6358_MICBIAS_CON1, 0x8001); /* Enable, 1.8V */
	if (err) return err;

	/* Enable microphone analog path */
	err = g510_pwrap_write(MT6358_AUDEN_ANA_CON26, 0xFFFF);
	if (err) return err;

	delay(10000); /* Wait for bias to stabilize (10ms) */

	redwood_keep("g510_mtkmic: microphone bias enabled");
	return 0;
}

/*
 * Configure AFE VUL (Voice Uplink) for microphone capture.
 * Returns 0 on success, error code on failure.
 */
static int
g510_mtkmic_config_afe(void)
{
	/* Configure VUL base/end for circular buffer */
	/* Buffer at 0x11220000 + offset, size configurable */
	afe_write(AFE_VUL_BASE, 0x11220000 + 0x1000); /* Example buffer */
	afe_write(AFE_VUL_END, 0x11220000 + 0x5000);  /* Example end */

	/* Enable VUL path */
	afe_write(AFE_VUL_CTRL, 0x1);
	delay(10);

	redwood_keep("g510_mtkmic: AFE VUL configured");
	return 0;
}

/*
 * Main initialization function for microphone.
 * Called with boot-arg mic=1.
 */
void
g510_mtkmic_init(void)
{
	uint32_t step = 0;
	int err;

	if (!PE_parse_boot_argn("mic", &step, sizeof(step)) || step == 0) {
		return;
	}

	redwood_keep("g510_mtkmic: init started (step=%u)", step);

	/* Map AFE registers */
	g510_afe = ml_io_map(AFE_PHYS, 0x10000);
	if (g510_afe == 0) {
		redwood_keep("g510_mtkmic: failed to map AFE");
		return;
	}

	/* Enable microphone bias */
	err = g510_mtkmic_enable_bias();
	if (err) {
		redwood_keep("g510_mtkmic: bias enable failed (%d)", err);
		return;
	}

	/* Configure AFE for capture */
	err = g510_mtkmic_config_afe();
	if (err) {
		redwood_keep("g510_mtkmic: AFE config failed (%d)", err);
		return;
	}

	redwood_keep("g510_mtkmic: initialization complete");
}