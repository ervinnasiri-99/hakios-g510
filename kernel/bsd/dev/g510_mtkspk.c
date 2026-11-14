/*
 * Speaker amplifier driver for the G510 (MT6765).
 * The AW87359 is a stereo speaker amplifier on I2C (per oppo6765_defconfig).
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

/* I2C controller for speaker amplifier */
#define I2C0_PHYS               0x11007000u
#define I2C_BASE                0x11007000u

/* I2C registers */
#define I2C_DATA_PORT           0x0000
#define I2C_SLAVE_ADDR          0x0004
#define I2C_INTR_MASK           0x0008
#define I2C_INTR_STAT           0x000C
#define I2C_CONTROL             0x0010
#define I2C_TRANSFER_LEN        0x0014
#define I2C_TRANSAC_LEN         0x0018
#define I2C_DELAY_LEN           0x001C
#define I2C_TIMING              0x0020
#define I2C_START               0x0024
#define I2C_EXT_CONF            0x0028
#define I2C_FIFO_STAT           0x0030
#define I2C_FIFO_THRESH         0x0034
#define I2C_FIFO_ADDR_CLR       0x0038
#define I2C_IO_CONFIG           0x003C

/* I2C CONTROL bits */
#define I2C_CTRL_ACKERR_DET_EN  (1u << 0)
#define I2C_CTRL_CLK_EXT_EN     (1u << 1)
#define I2C_CTRL_DIR_CHANGE     (1u << 2)
#define I2C_CTRL_ACKERR_DET     (1u << 3)
#define I2C_CTRL_TRANSFER_LEN_CHANGE (1u << 4)
#define I2C_CTRL_DMA_EN         (1u << 5)
#define I2C_CTRL_SCLK_STRETCH   (1u << 6)
#define I2C_CTRL_ACKERR_DET_CLR (1u << 7)

/* AW87359 I2C address (7-bit) */
#define AW87359_I2C_ADDR        0x34

/* AW87359 registers */
#define AW87359_RESET           0x00
#define AW87359_SYSCTRL         0x01
#define AW87359_SYSST           0x02
#define AW87359_CH1VOL          0x03
#define AW87359_CH2VOL          0x04
#define AW87359_CH12VOL         0x05
#define AW87359_LIMCTRL         0x06
#define AW87359_LIMCTRL2        0x07
#define AW87359_BOOSTCTRL       0x08
#define AW87359_BOOSTCTRL2      0x09
#define AW87359_BATCTRL         0x0A
#define AW87359_BATCTRL2        0x0B
#define AW87359_BATCTRL3        0x0C
#define AW87359_TEMPCTRL        0x0D
#define AW87359_TEMPCTRL2       0x0E
#define AW87359_INTMASK         0x0F
#define AW87359_INTSTAT         0x10
#define AW87359_INTRAW          0x11
#define AW87359_CH1BOOST        0x12
#define AW87359_CH2BOOST        0x13
#define AW87359_DACCTRL         0x14
#define AW87359_DACCTRL2        0x15
#define AW87359_MISC            0x16

/* AW87359 SYSCTRL bits */
#define AW87359_SYSCTRL_AMP_EN  (1u << 0)
#define AW87359_SYSCTRL_BOOST_EN (1u << 1)
#define AW87359_SYSCTRL_DAC_EN  (1u << 2)
#define AW87359_SYSCTRL_SHDN    (1u << 7)

/* AW87359 SYSST bits */
#define AW87359_SYSST_OCP       (1u << 0)
#define AW87359_SYSST_OTP       (1u << 1)
#define AW87359_SYSST_UVP       (1u << 2)
#define AW87359_SYSST_BATLOW    (1u << 3)
#define AW87359_SYSST_BATHIGH   (1u << 4)

static vm_offset_t g510_i2c;

static uint32_t
i2c_read(uint32_t offset)
{
	return *(volatile uint32_t *)(g510_i2c + offset);
}

static void
i2c_write(uint32_t offset, uint32_t value)
{
	*(volatile uint32_t *)(g510_i2c + offset) = value;
}

/*
 * Wait for I2C transaction to complete.
 * Returns 0 on success, ETIMEDOUT on timeout.
 */
static int
i2c_wait_done(void)
{
	for (int i = 0; i < 10000; i++) {
		uint32_t stat = i2c_read(I2C_INTR_STAT);
		if (stat & 0x1) { /* Transfer complete */
			i2c_write(I2C_INTR_STAT, 0x1); /* Clear */
			return 0;
		}
		if (stat & 0x2) { /* ACK error */
			i2c_write(I2C_INTR_STAT, 0x2); /* Clear */
			return EIO;
		}
		delay(10);
	}
	return ETIMEDOUT;
}

/*
 * Write a register to AW87359 via I2C.
 * Returns 0 on success, error code on failure.
 */
static int
aw87359_write(uint8_t reg, uint8_t value)
{
	if (g510_i2c == 0) {
		return ENXIO;
	}

	/* Set slave address */
	i2c_write(I2C_SLAVE_ADDR, AW87359_I2C_ADDR << 1);

	/* Write register address */
	i2c_write(I2C_DATA_PORT, reg);
	i2c_write(I2C_TRANSFER_LEN, 1);
	i2c_write(I2C_TRANSAC_LEN, 2); /* Reg + data */
	i2c_write(I2C_CONTROL, I2C_CTRL_DIR_CHANGE);
	i2c_write(I2C_START, 1);

	int err = i2c_wait_done();
	if (err) {
		redwood_keep("g510_mtkspk: AW87359 write reg addr failed (%d)", err);
		return err;
	}

	/* Write register value */
	i2c_write(I2C_DATA_PORT, value);
	i2c_write(I2C_TRANSFER_LEN, 1);
	i2c_write(I2C_TRANSAC_LEN, 1);
	i2c_write(I2C_CONTROL, 0);
	i2c_write(I2C_START, 1);

	err = i2c_wait_done();
	if (err) {
		redwood_keep("g510_mtkspk: AW87359 write reg value failed (%d)", err);
		return err;
	}

	return 0;
}

/*
 * Read a register from AW87359 via I2C.
 * Returns 0 on success, error code on failure.
 */
static int
aw87359_read(uint8_t reg, uint8_t *out_value)
{
	if (g510_i2c == 0) {
		return ENXIO;
	}

	/* Set slave address */
	i2c_write(I2C_SLAVE_ADDR, AW87359_I2C_ADDR << 1);

	/* Write register address */
	i2c_write(I2C_DATA_PORT, reg);
	i2c_write(I2C_TRANSFER_LEN, 1);
	i2c_write(I2C_TRANSAC_LEN, 2); /* Reg + data */
	i2c_write(I2C_CONTROL, I2C_CTRL_DIR_CHANGE);
	i2c_write(I2C_START, 1);

	int err = i2c_wait_done();
	if (err) {
		redwood_keep("g510_mtkspk: AW87359 read reg addr failed (%d)", err);
		return err;
	}

	/* Read register value */
	i2c_write(I2C_DATA_PORT, 0);
	i2c_write(I2C_TRANSFER_LEN, 1);
	i2c_write(I2C_TRANSAC_LEN, 1);
	i2c_write(I2C_CONTROL, 0);
	i2c_write(I2C_START, 1);

	err = i2c_wait_done();
	if (err) {
		redwood_keep("g510_mtkspk: AW87359 read reg value failed (%d)", err);
		return err;
	}

	*out_value = (uint8_t)(i2c_read(I2C_DATA_PORT) & 0xFF);
	return 0;
}

/*
 * Initialize AW87359 speaker amplifier.
 * Returns 0 on success, error code on failure.
 */
int
g510_mtkspk_init(void)
{
	uint8_t sysst;
	int err;

	if (g510_i2c != 0) {
		return 0; /* already initialized */
	}

	g510_i2c = ml_io_map(I2C0_PHYS, 0x1000);
	if (g510_i2c == 0) {
		redwood_keep("g510_mtkspk: failed to map I2C0");
		return ENXIO;
	}

	/* Configure I2C timing (100kHz) */
	i2c_write(I2C_TIMING, 0x40); /* Standard mode */
	i2c_write(I2C_DELAY_LEN, 0x10);
	delay(10);

	/* Read device status */
	err = aw87359_read(AW87359_SYSST, &sysst);
	if (err) {
		redwood_keep("g510_mtkspk: AW87359 SYSST read failed (%d)", err);
		return err;
	}
	redwood_keep("g510_mtkspk: AW87359 SYSST = 0x%02X", sysst);

	/* Check for faults */
	if (sysst & (AW87359_SYSST_OCP | AW87359_SYSST_OTP | AW87359_SYSST_UVP)) {
		redwood_keep("g510_mtkspk: AW87359 fault detected (SYSST=0x%02X)", sysst);
	}

	/* Reset device */
	err = aw87359_write(AW87359_RESET, 0x01);
	if (err) {
		redwood_keep("g510_mtkspk: AW87359 reset failed (%d)", err);
		return err;
	}
	delay(1000); /* Wait for reset */

	/* Configure boost converter */
	err = aw87359_write(AW87359_BOOSTCTRL, 0x1C); /* 8.5V boost */
	if (err) return err;
	err = aw87359_write(AW87359_BOOSTCTRL2, 0x00);
	if (err) return err;

	/* Configure battery monitoring */
	err = aw87359_write(AW87359_BATCTRL, 0x80); /* Enable battery monitor */
	if (err) return err;
	err = aw87359_write(AW87359_BATCTRL2, 0x28); /* 3.0V UVLO */
	if (err) return err;
	err = aw87359_write(AW87359_BATCTRL3, 0x00);
	if (err) return err;

	/* Configure thermal protection */
	err = aw87359_write(AW87359_TEMPCTRL, 0x40); /* 135C OTP */
	if (err) return err;
	err = aw87359_write(AW87359_TEMPCTRL2, 0x00);
	if (err) return err;

	/* Configure limiter */
	err = aw87359_write(AW87359_LIMCTRL, 0x8F); /* Enable limiter, 1.5x gain */
	if (err) return err;
	err = aw87359_write(AW87359_LIMCTRL2, 0x00);
	if (err) return err;

	/* Set default volume (muted initially) */
	err = aw87359_write(AW87359_CH1VOL, 0x00);
	if (err) return err;
	err = aw87359_write(AW87359_CH2VOL, 0x00);
	if (err) return err;

	/* Enable amplifier */
	err = aw87359_write(AW87359_SYSCTRL, AW87359_SYSCTRL_AMP_EN |
	                                         AW87359_SYSCTRL_BOOST_EN |
	                                         AW87359_SYSCTRL_DAC_EN);
	if (err) return err;
	delay(10000); /* Wait for boost to stabilize */

	/* Enable DAC */
	err = aw87359_write(AW87359_DACCTRL, 0x01);
	if (err) return err;

	redwood_keep("g510_mtkspk: AW87359 initialized");
	return 0;
}

/*
 * Set speaker volume (0-255).
 * Returns 0 on success, error code on failure.
 */
int
g510_mtkspk_set_volume(uint8_t volume)
{
	int err;

	err = aw87359_write(AW87359_CH1VOL, volume);
	if (err) return err;
	err = aw87359_write(AW87359_CH2VOL, volume);
	if (err) return err;

	return 0;
}

/*
 * Enable/disable speaker output.
 * Returns 0 on success, error code on failure.
 */
int
g510_mtkspk_enable(bool enable)
{
	uint8_t sysctrl;

	int err = aw87359_read(AW87359_SYSCTRL, &sysctrl);
	if (err) return err;

	if (enable) {
		sysctrl |= AW87359_SYSCTRL_AMP_EN | AW87359_SYSCTRL_DAC_EN;
	} else {
		sysctrl &= ~(AW87359_SYSCTRL_AMP_EN | AW87359_SYSCTRL_DAC_EN);
	}

	return aw87359_write(AW87359_SYSCTRL, sysctrl);
}

/*
 * Get speaker fault status.
 * Returns 0 on success, error code on failure.
 */
int
g510_mtkspk_get_status(uint8_t *out_status)
{
	return aw87359_read(AW87359_SYSST, out_status);
}