/*
 * PMIC wrapper (PWRAP) for the G510 (MT6765).
 * The PWRAP is the SPMI arbiter that gives access to the PMIC (MT6357/MT6358)
 * for audio codec, battery gauge, vibrator, and other PMIC functions.
 * License: APSL 2.0
 */
#include <kern/clock.h>
#include <libkern/libkern.h>
#include <mach/mach_types.h>
#include <pexpert/pexpert.h>
#include <sys/systm.h>
#include <sys/redwood_input.h>

extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
extern void redwood_keep(const char *format, ...) __printflike(1, 2);
extern void delay(int usec);

#define PWRAP_PHYS              0x1000D000u
#define PWRAP_BASE              0x1000D000u
#define PWRAP_DEW_BASE          0x1000E000u
#define PWRAP_DEW_CRC_BASE      0x1000E100u
#define PWRAP_DEW_CRC_VAL_BASE  0x1000E180u

/* PWRAP registers */
#define PWRAP_WACS2_RDATA       0x0004
#define PWRAP_WACS2_VLDCLR      0x0008
#define PWRAP_WACS2_CMD         0x000C
#define PWRAP_WACS2_RDATA_W1S   0x0010
#define PWRAP_WACS2_CMD_W1S     0x0014
#define PWRAP_WACS2_CMD_W1C     0x0018
#define PWRAP_WACS2_WDATA       0x001C
#define PWRAP_WACS2_STATE       0x0020
#define PWRAP_WACS2_CIPHER      0x0024
#define PWRAP_WACS2_CIPHER_SWR  0x0028
#define PWRAP_WACS2_CIPHER_RDD  0x002C
#define PWRAP_HARB_INIT         0x0080
#define PWRAP_HARB_HPRIO        0x0084
#define PWRAP_HARB_IPRIO        0x0088
#define PWRAP_HARB_INIT1        0x008C
#define PWRAP_HARB_INIT2        0x0090
#define PWRAP_MAN_EN            0x00A0
#define PWRAP_MAN_CMD           0x00A4
#define PWRAP_MAN_RDATA         0x00A8
#define PWRAP_MAN_VLDCLR        0x00AC
#define PWRAP_DIO_EN            0x00B0
#define PWRAP_RDDMY             0x00B4
#define PWRAP_CIPHER_KEY_SEL    0x00C0
#define PWRAP_CIPHER_IV_SEL     0x00C4
#define PWRAP_CIPHER_EN         0x00C8
#define PWRAP_CIPHER_RDY        0x00CC
#define PWRAP_CIPHER_MODE       0x00D0
#define PWRAP_CIPHER_SWRST      0x00D4
#define PWRAP_SIDLY             0x00E0
#define PWRAP_RDDMY_NO          0x00E4
#define PWRAP_DEW_READ_TEST     0x00F0
#define PWRAP_DEW_WRITE_TEST    0x00F4
#define PWRAP_DEW_CRC_EN        0x00F8
#define PWRAP_DEW_CRC_VAL       0x00FC
#define PWRAP_DEW_CRC           0x0100
#define PWRAP_DEW_EVENT         0x0104
#define PWRAP_DEW_EVENT_RAW     0x0108
#define PWRAP_DEW_EVENT_EN      0x010C
#define PWRAP_DEW_READ_TEST2    0x0110
#define PWRAP_DEW_WRITE_TEST2   0x0114
#define PWRAP_DEW_CRC_EN2       0x0118
#define PWRAP_DEW_CRC_VAL2      0x011C
#define PWRAP_DEW_CRC2          0x0120
#define PWRAP_DEW_EVENT2        0x0124
#define PWRAP_DEW_EVENT_RAW2    0x0128
#define PWRAP_DEW_EVENT_EN2     0x012C
#define PWRAP_DEW_DIO_EN        0x0130
#define PWRAP_DEW_MODE          0x0134
#define PWRAP_DEW_RDDMY         0x0138
#define PWRAP_DEW_MAN_EN        0x013C
#define PWRAP_DEW_MAN_CMD       0x0140
#define PWRAP_DEW_MAN_RDATA     0x0144
#define PWRAP_DEW_MAN_VLDCLR    0x0148
#define PWRAP_DEW_CIPHER_KEY    0x0150
#define PWRAP_DEW_CIPHER_IV     0x0154
#define PWRAP_DEW_CIPHER_EN     0x0158
#define PWRAP_DEW_CIPHER_RDY    0x015C
#define PWRAP_DEW_CIPHER_MODE   0x0160
#define PWRAP_DEW_CIPHER_SWRST  0x0164
#define PWRAP_DEW_SIDLY         0x0168
#define PWRAP_DEW_RDDMY_NO      0x016C

/* WACS2_CMD bits */
#define WACS2_WRITE             (1u << 31)
#define WACS2_READ              (0u << 31)
#define WACS2_ADR_SHIFT         16
#define WACS2_ADR_MASK          (0x3FFF << 16)
#define WACS2_WDATA_MASK        0xFFFF

/* PWRAP MAN_EN bits */
#define MAN_EN                  (1u << 0)

/* PWRAP HARB_INIT bits */
#define HARB_INIT_SPI           (1u << 0)
#define HARB_INIT_SPI_CK        (1u << 1)
#define HARB_INIT_SPI_OP        (1u << 2)

/* PMIC (MT6357/MT6358) register addresses (via SPMI through PWRAP) */
#define MT6358_SLAVE_ID         0x05
#define MT6358_BASE             (MT6358_SLAVE_ID << 13)

/* MT6358 Audio codec registers */
#define MT6358_AUDEN_ANA_CON0   (MT6358_BASE + 0x11A0)
#define MT6358_AUDEN_ANA_CON1   (MT6358_BASE + 0x11A2)
#define MT6358_AUDEN_ANA_CON2   (MT6358_BASE + 0x11A4)
#define MT6358_AUDEN_ANA_CON3   (MT6358_BASE + 0x11A6)
#define MT6358_AUDEN_ANA_CON4   (MT6358_BASE + 0x11A8)
#define MT6358_AUDEN_ANA_CON5   (MT6358_BASE + 0x11AA)
#define MT6358_AUDEN_ANA_CON6   (MT6358_BASE + 0x11AC)
#define MT6358_AUDEN_ANA_CON7   (MT6358_BASE + 0x11AE)
#define MT6358_AUDEN_ANA_CON8   (MT6358_BASE + 0x11B0)
#define MT6358_AUDEN_ANA_CON9   (MT6358_BASE + 0x11B2)
#define MT6358_AUDEN_ANA_CON10  (MT6358_BASE + 0x11B4)
#define MT6358_AUDEN_ANA_CON11  (MT6358_BASE + 0x11B6)
#define MT6358_AUDEN_ANA_CON12  (MT6358_BASE + 0x11B8)
#define MT6358_AUDEN_ANA_CON13  (MT6358_BASE + 0x11BA)
#define MT6358_AUDEN_ANA_CON14  (MT6358_BASE + 0x11BC)
#define MT6358_AUDEN_ANA_CON15  (MT6358_BASE + 0x11BE)
#define MT6358_AUDEN_ANA_CON16  (MT6358_BASE + 0x11C0)
#define MT6358_AUDEN_ANA_CON17  (MT6358_BASE + 0x11C2)
#define MT6358_AUDEN_ANA_CON18  (MT6358_BASE + 0x11C4)
#define MT6358_AUDEN_ANA_CON19  (MT6358_BASE + 0x11C6)
#define MT6358_AUDEN_ANA_CON20  (MT6358_BASE + 0x11C8)
#define MT6358_AUDEN_ANA_CON21  (MT6358_BASE + 0x11CA)
#define MT6358_AUDEN_ANA_CON22  (MT6358_BASE + 0x11CC)
#define MT6358_AUDEN_ANA_CON23  (MT6358_BASE + 0x11CE)
#define MT6358_AUDEN_ANA_CON24  (MT6358_BASE + 0x11D0)
#define MT6358_AUDEN_ANA_CON25  (MT6358_BASE + 0x11D2)

/* MT6358 Battery gauge registers */
#define MT6358_FGADC_CON0       (MT6358_BASE + 0x1E00)
#define MT6358_FGADC_CON1       (MT6358_BASE + 0x1E02)
#define MT6358_FGADC_CON2       (MT6358_BASE + 0x1E04)
#define MT6358_FGADC_CON3       (MT6358_BASE + 0x1E06)
#define MT6358_FGADC_CON4       (MT6358_BASE + 0x1E08)
#define MT6358_FGADC_CON5       (MT6358_BASE + 0x1E0A)
#define MT6358_FGADC_CON6       (MT6358_BASE + 0x1E0C)
#define MT6358_FGADC_CON7       (MT6358_BASE + 0x1E0E)
#define MT6358_FGADC_CON8       (MT6358_BASE + 0x1E10)
#define MT6358_FGADC_CON9       (MT6358_BASE + 0x1E12)
#define MT6358_FGADC_CON10      (MT6358_BASE + 0x1E14)
#define MT6358_FGADC_CON11      (MT6358_BASE + 0x1E16)
#define MT6358_FGADC_CON12      (MT6358_BASE + 0x1E18)
#define MT6358_FGADC_CON13      (MT6358_BASE + 0x1E1A)
#define MT6358_FGADC_CON14      (MT6358_BASE + 0x1E1C)
#define MT6358_FGADC_CON15      (MT6358_BASE + 0x1E1E)
#define MT6358_BATON_HT_EN      (MT6358_BASE + 0x1E20)
#define MT6358_BATON_LT_EN      (MT6358_BASE + 0x1E22)
#define MT6358_BATON_HT_THD     (MT6358_BASE + 0x1E24)
#define MT6358_BATON_LT_THD     (MT6358_BASE + 0x1E26)

/* MT6358 Vibrator registers */
#define MT6358_VIBR_CON0        (MT6358_BASE + 0x1F00)
#define MT6358_VIBR_CON1        (MT6358_BASE + 0x1F02)
#define MT6358_VIBR_CON2        (MT6358_BASE + 0x1F04)
#define MT6358_VIBR_CON3        (MT6358_BASE + 0x1F06)
#define MT6358_VIBR_CON4        (MT6358_BASE + 0x1F08)

static vm_offset_t g510_pwrap;

static uint32_t
pwrap_read(void *base, uint32_t offset)
{
	return *(volatile uint32_t *)((vm_offset_t)base + offset);
}

static void
pwrap_write(void *base, uint32_t offset, uint32_t value)
{
	*(volatile uint32_t *)((vm_offset_t)base + offset) = value;
}

/*
 * Wait for PWRAP WACS2 command to complete.
 * Returns 0 on success, ETIMEDOUT on timeout.
 */
static int
pwrap_wacs2_wait(vm_offset_t base)
{
	for (int i = 0; i < 1000; i++) {
		uint32_t state = pwrap_read(base, PWRAP_WACS2_STATE);
		if (!(state & 0x1)) {
			return 0;
		}
		delay(1);
	}
	return ETIMEDOUT;
}

/*
 * Read a register from the PMIC via PWRAP WACS2.
 * Returns 0 on success, error code on failure.
 */
int
g510_pwrap_read(uint32_t pmic_addr, uint16_t *out_val)
{
	if (g510_pwrap == 0) {
		return ENXIO;
	}

	uint32_t cmd = WACS2_READ | (pmic_addr << WACS2_ADR_SHIFT);
	pwrap_write(g510_pwrap, PWRAP_WACS2_CMD, cmd);

	int err = pwrap_wacs2_wait(g510_pwrap);
	if (err) {
		redwood_keep("g510_pwrap: read timeout at addr 0x%08X", pmic_addr);
		return err;
	}

	uint32_t rdata = pwrap_read(g510_pwrap, PWRAP_WACS2_RDATA);
	*out_val = (uint16_t)(rdata & 0xFFFF);

	/* Clear valid bit */
	pwrap_write(g510_pwrap, PWRAP_WACS2_VLDCLR, 1);

	return 0;
}

/*
 * Write a register to the PMIC via PWRAP WACS2.
 * Returns 0 on success, error code on failure.
 */
int
g510_pwrap_write(uint32_t pmic_addr, uint16_t value)
{
	if (g510_pwrap == 0) {
		return ENXIO;
	}

	uint32_t cmd = WACS2_WRITE | (pmic_addr << WACS2_ADR_SHIFT) | (value & 0xFFFF);
	pwrap_write(g510_pwrap, PWRAP_WACS2_CMD, cmd);

	int err = pwrap_wacs2_wait(g510_pwrap);
	if (err) {
		redwood_keep("g510_pwrap: write timeout at addr 0x%08X", pmic_addr);
		return err;
	}

	/* Clear valid bit */
	pwrap_write(g510_pwrap, PWRAP_WACS2_VLDCLR, 1);

	return 0;
}

/*
 * Initialize the PWRAP block.
 * Returns 0 on success, error code on failure.
 */
int
g510_pwrap_init(void)
{
	if (g510_pwrap != 0) {
		return 0; /* already initialized */
	}

	g510_pwrap = ml_io_map(PWRAP_PHYS, 0x1000);
	if (g510_pwrap == 0) {
		redwood_keep("g510_pwrap: failed to map PWRAP");
		return ENXIO;
	}

	/* Enable PWRAP HARB */
	pwrap_write(g510_pwrap, PWRAP_HARB_INIT, HARB_INIT_SPI | HARB_INIT_SPI_CK | HARB_INIT_SPI_OP);
	delay(10);

	/* Enable WACS2 */
	pwrap_write(g510_pwrap, PWRAP_MAN_EN, MAN_EN);
	delay(10);

	/* Initialize DEW (Dual-Edge Waveform) for better timing */
	pwrap_write(g510_pwrap, PWRAP_DEW_CRC_EN, 1);
	pwrap_write(g510_pwrap, PWRAP_DEW_CRC_EN2, 1);
	delay(10);

	redwood_keep("g510_pwrap: initialized at 0x%08X", PWRAP_PHYS);
	return 0;
}

/*
 * Read MT6358 chip ID to verify PMIC communication.
 * Returns 0 on success, error code on failure.
 */
int
g510_pwrap_read_chip_id(uint16_t *out_id)
{
	return g510_pwrap_read(MT6358_BASE + 0x0000, out_id); /* CHIP_ID register */
}