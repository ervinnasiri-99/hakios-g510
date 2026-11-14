/*
 * Camera driver for the G510 (MT6765).
 * Main camera: S5K JN1 (Samsung 50MP, chip ID 0x38E1)
 * Front camera: GC5035 (GalaxyCore 5MP)
 * Uses MTKCAM ISP/AAA, CSI-2 MIPI, CAMSV_1 DMA for capture.
 * License: APSL 2.0
 */
#include <kern/clock.h>
#include <libkern/libkern.h>
#include <mach/mach_types.h>
#include <pexpert/pexpert.h>
#include <sys/systm.h>
#include <sys/redwood_input.h>
#include <sys/sysctl.h>

extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
extern void redwood_keep(const char *format, ...) __printflike(1, 2);
extern void delay(int usec);
extern void delay_for_interval(uint32_t interval, uint32_t scale_factor);

/* CSI-2 / MIPI registers */
#define CSI_PHYS                0x15000000u
#define CSI_TOP_CON             0x0000
#define CSI_TOP_STA             0x0004
#define CSI_TOP_ERR             0x0008
#define CSI_DPHY_CON            0x0010
#define CSI_DPHY_STA            0x0014
#define CSI_DPHY_ERR            0x0018

/* CAMSV (Camera Subsystem Video) registers */
#define CAMSV_PHYS              0x15010000u
#define CAMSV_TOP_CON           0x0000
#define CAMSV_TOP_STA           0x0004
#define CAMSV_INT_EN            0x0008
#define CAMSV_INT_STA           0x000C
#define CAMSV_DMA_CON           0x0010
#define CAMSV_DMA_STA           0x0014
#define CAMSV_DMA_BASE          0x0020
#define CAMSV_DMA_OFST          0x0024
#define CAMSV_DMA_XSIZE         0x0028
#define CAMSV_DMA_YSIZE         0x002C
#define CAMSV_DMA_STRIDE        0x0030
#define CAMSV_CROP_CON          0x0040
#define CAMSV_CROP_OFST         0x0044
#define CAMSV_CROP_SIZE         0x0048

/* CAMSV channels */
#define CAMSV_CH0               0  /* Main camera */
#define CAMSV_CH1               1  /* Sub camera */
#define CAMSV_CH2               2  /* Third camera */

/* ISP registers */
#define ISP_PHYS                0x15020000u
#define ISP_TOP_CON             0x0000
#define ISP_INT_EN              0x0004
#define ISP_INT_STA             0x0008
#define ISP_CQ_CON              0x0010
#define ISP_CQ_STA              0x0014
#define ISP_CQ_BASE             0x0018
#define ISP_CQ_OFST             0x001C
#define ISP_CQ_DONE             0x0020

/* CCI (Camera Control Interface) for I2C sensor control */
#define CCI_PHYS                0x15008000u
#define CCI_BASE                0x15008000u
#define CCI_CON                 0x0000
#define CCI_EN                  0x0004
#define CCI_CMD                 0x0008
#define CCI_ADDR                0x000C
#define CCI_WDATA               0x0010
#define CCI_RDATA               0x0014
#define CCI_STA                 0x0018
#define CCI_INT_EN              0x001C
#define CCI_INT_STA             0x0020

/* S5K JN1 (main camera) I2C address and registers */
#define S5K_JN1_I2C_ADDR        0x20  /* 7-bit */
#define S5K_JN1_CHIP_ID         0x0000
#define S5K_JN1_CHIP_ID_VAL     0x38E1
#define S5K_JN1_MODE_SELECT     0x0100
#define S5K_JN1_SOFTWARE_RESET  0x0103
#define S5K_JN1_GROUP_HOLD      0x0104
#define S5K_JN1_EXPOSURE_H      0x0202
#define S5K_JN1_EXPOSURE_L      0x0203
#define S5K_JN1_GAIN_H          0x0204
#define S5K_JN1_GAIN_L          0x0205
#define S5K_JN1_VBLANK_H        0x0206
#define S5K_JN1_VBLANK_L        0x0207
#define S5K_JN1_HBLANK_H        0x0208
#define S5K_JN1_HBLANK_L        0x0209
#define S5K_JN1_IMG_ORIENTATION 0x0101
#define S5K_JN1_TEST_PATTERN    0x0600

/* GC5035 (front camera) I2C address and registers */
#define GC5035_I2C_ADDR         0x37  /* 7-bit */
#define GC5035_CHIP_ID          0x0000
#define GC5035_CHIP_ID_VAL      0x5035
#define GC5035_MODE_SELECT      0x0100
#define GC5035_SOFTWARE_RESET   0x0103
#define GC5035_GROUP_HOLD       0x0104
#define GC5035_EXPOSURE_H       0x0202
#define GC5035_EXPOSURE_L       0x0203
#define GC5035_GAIN_H           0x0204
#define GC5035_GAIN_L           0x0205

SYSCTL_DECL(_kern_g510);
SYSCTL_NODE(_kern_g510, OID_AUTO, camera,
    CTLFLAG_RD | CTLFLAG_LOCKED, 0, "G510 camera subsystem");

static int32_t g510_camera_main_ready = 0;
static int32_t g510_camera_front_ready = 0;

SYSCTL_INT(_kern_g510_camera, OID_AUTO, main_ready, CTLFLAG_RD | CTLFLAG_LOCKED,
    &g510_camera_main_ready, 0, "Main camera ready (1=yes)");
SYSCTL_INT(_kern_g510_camera, OID_AUTO, front_ready, CTLFLAG_RD | CTLFLAG_LOCKED,
    &g510_camera_front_ready, 0, "Front camera ready (1=yes)");

static vm_offset_t g510_csi;
static vm_offset_t g510_camsv;
static vm_offset_t g510_isp;
static vm_offset_t g510_cci;

static uint32_t
csi_read(uint32_t offset)
{
	return *(volatile uint32_t *)(g510_csi + offset);
}

static void
csi_write(uint32_t offset, uint32_t value)
{
	*(volatile uint32_t *)(g510_csi + offset) = value;
}

static uint32_t
camsv_read(uint32_t offset)
{
	return *(volatile uint32_t *)(g510_camsv + offset);
}

static void
camsv_write(uint32_t offset, uint32_t value)
{
	*(volatile uint32_t *)(g510_camsv + offset) = value;
}

static uint32_t
isp_read(uint32_t offset)
{
	return *(volatile uint32_t *)(g510_isp + offset);
}

static void
isp_write(uint32_t offset, uint32_t value)
{
	*(volatile uint32_t *)(g510_isp + offset) = value;
}

static uint32_t
cci_read(uint32_t offset)
{
	return *(volatile uint32_t *)(g510_cci + offset);
}

static void
cci_write(uint32_t offset, uint32_t value)
{
	*(volatile uint32_t *)(g510_cci + offset) = value;
}

/*
 * Wait for CCI transaction to complete.
 * Returns 0 on success, ETIMEDOUT on timeout.
 */
static int
cci_wait_done(void)
{
	for (int i = 0; i < 10000; i++) {
		uint32_t sta = cci_read(CCI_STA);
		if (sta & 0x1) { /* Transaction done */
			cci_write(CCI_STA, 0x1); /* Clear */
			return 0;
		}
		if (sta & 0x2) { /* ACK error */
			cci_write(CCI_STA, 0x2); /* Clear */
			return EIO;
		}
		delay(10);
	}
	return ETIMEDOUT;
}

/*
 * Write a register to camera sensor via CCI (I2C).
 * Returns 0 on success, error code on failure.
 */
static int
cci_write_reg(uint8_t i2c_addr, uint16_t reg, uint8_t value)
{
	cci_write(CCI_ADDR, ((uint32_t)i2c_addr << 24) | (reg & 0xFFFF));
	cci_write(CCI_WDATA, value);
	cci_write(CCI_CMD, 0x1); /* Write command */
	cci_write(CCI_EN, 0x1);

	int err = cci_wait_done();
	if (err) {
		redwood_keep("g510_mtkcam: CCI write failed (addr=0x%02X, reg=0x%04X, err=%d)",
		    i2c_addr, reg, err);
		return err;
	}

	return 0;
}

/*
 * Read a register from camera sensor via CCI (I2C).
 * Returns 0 on success, error code on failure.
 */
static int
cci_read_reg(uint8_t i2c_addr, uint16_t reg, uint8_t *out_value)
{
	cci_write(CCI_ADDR, ((uint32_t)i2c_addr << 24) | (reg & 0xFFFF));
	cci_write(CCI_CMD, 0x2); /* Read command */
	cci_write(CCI_EN, 0x1);

	int err = cci_wait_done();
	if (err) {
		redwood_keep("g510_mtkcam: CCI read failed (addr=0x%02X, reg=0x%04X, err=%d)",
		    i2c_addr, reg, err);
		return err;
	}

	*out_value = (uint8_t)(cci_read(CCI_RDATA) & 0xFF);
	return 0;
}

/*
 * Initialize S5K JN1 main camera sensor.
 * Returns 0 on success, error code on failure.
 */
static int
g510_mtkcam_init_s5kjn1(void)
{
	uint8_t chip_id_h, chip_id_l;
	int err;

	redwood_keep("g510_mtkcam: initializing S5K JN1 main camera");

	/* Software reset */
	err = cci_write_reg(S5K_JN1_I2C_ADDR, S5K_JN1_SOFTWARE_RESET, 0x01);
	if (err) return err;
	delay(10000); /* 10ms */

	/* Read chip ID */
	err = cci_read_reg(S5K_JN1_I2C_ADDR, S5K_JN1_CHIP_ID, &chip_id_h);
	if (err) return err;
	err = cci_read_reg(S5K_JN1_I2C_ADDR, S5K_JN1_CHIP_ID + 1, &chip_id_l);
	if (err) return err;

	uint16_t chip_id = (chip_id_h << 8) | chip_id_l;
	if (chip_id != S5K_JN1_CHIP_ID_VAL) {
		redwood_keep("g510_mtkcam: S5K JN1 chip ID mismatch (expected 0x%04X, got 0x%04X)",
		    S5K_JN1_CHIP_ID_VAL, chip_id);
		return ENXIO;
	}

	redwood_keep("g510_mtkcam: S5K JN1 detected (chip ID 0x%04X)", chip_id);

	/* Configure sensor for preview mode (example: 1632x1224 @ 30fps) */
	/* This would require full register sequence from sensor datasheet */
	/* For now, just enable streaming */
	err = cci_write_reg(S5K_JN1_I2C_ADDR, S5K_JN1_MODE_SELECT, 0x01);
	if (err) return err;

	/* Set default exposure and gain */
	err = cci_write_reg(S5K_JN1_I2C_ADDR, S5K_JN1_EXPOSURE_H, 0x04);
	if (err) return err;
	err = cci_write_reg(S5K_JN1_I2C_ADDR, S5K_JN1_EXPOSURE_L, 0x00);
	if (err) return err;
	err = cci_write_reg(S5K_JN1_I2C_ADDR, S5K_JN1_GAIN_H, 0x00);
	if (err) return err;
	err = cci_write_reg(S5K_JN1_I2C_ADDR, S5K_JN1_GAIN_L, 0x10);
	if (err) return err;

	/* Release group hold to apply settings */
	err = cci_write_reg(S5K_JN1_I2C_ADDR, S5K_JN1_GROUP_HOLD, 0x01);
	if (err) return err;
	err = cci_write_reg(S5K_JN1_I2C_ADDR, S5K_JN1_GROUP_HOLD, 0x00);
	if (err) return err;

	delay(10000);

	redwood_keep("g510_mtkcam: S5K JN1 initialized");
	g510_camera_main_ready = 1;
	return 0;
}

/*
 * Initialize GC5035 front camera sensor.
 * Returns 0 on success, error code on failure.
 */
static int
g510_mtkcam_init_gc5035(void)
{
	uint8_t chip_id_h, chip_id_l;
	int err;

	redwood_keep("g510_mtkcam: initializing GC5035 front camera");

	/* Software reset */
	err = cci_write_reg(GC5035_I2C_ADDR, GC5035_SOFTWARE_RESET, 0x01);
	if (err) return err;
	delay(10000); /* 10ms */

	/* Read chip ID */
	err = cci_read_reg(GC5035_I2C_ADDR, GC5035_CHIP_ID, &chip_id_h);
	if (err) return err;
	err = cci_read_reg(GC5035_I2C_ADDR, GC5035_CHIP_ID + 1, &chip_id_l);
	if (err) return err;

	uint16_t chip_id = (chip_id_h << 8) | chip_id_l;
	if (chip_id != GC5035_CHIP_ID_VAL) {
		redwood_keep("g510_mtkcam: GC5035 chip ID mismatch (expected 0x%04X, got 0x%04X)",
		    GC5035_CHIP_ID_VAL, chip_id);
		return ENXIO;
	}

	redwood_keep("g510_mtkcam: GC5035 detected (chip ID 0x%04X)", chip_id);

	/* Enable streaming */
	err = cci_write_reg(GC5035_I2C_ADDR, GC5035_MODE_SELECT, 0x01);
	if (err) return err;

	/* Set default exposure and gain */
	err = cci_write_reg(GC5035_I2C_ADDR, GC5035_EXPOSURE_H, 0x04);
	if (err) return err;
	err = cci_write_reg(GC5035_I2C_ADDR, GC5035_EXPOSURE_L, 0x00);
	if (err) return err;
	err = cci_write_reg(GC5035_I2C_ADDR, GC5035_GAIN_H, 0x00);
	if (err) return err;
	err = cci_write_reg(GC5035_I2C_ADDR, GC5035_GAIN_L, 0x10);
	if (err) return err;

	/* Release group hold */
	err = cci_write_reg(GC5035_I2C_ADDR, GC5035_GROUP_HOLD, 0x01);
	if (err) return err;
	err = cci_write_reg(GC5035_I2C_ADDR, GC5035_GROUP_HOLD, 0x00);
	if (err) return err;

	delay(10000);

	redwood_keep("g510_mtkcam: GC5035 initialized");
	g510_camera_front_ready = 1;
	return 0;
}

/*
 * Configure CSI-2 receiver for camera input.
 * Returns 0 on success, error code on failure.
 */
static int
g510_mtkcam_config_csi(void)
{
	/* Enable CSI-2 lanes and configure data types */
	/* Lane 0: clock, Lane 1-2: data (for 2-lane config) */
	csi_write(CSI_DPHY_CON, 0x7); /* Enable clock + 2 data lanes */
	delay(10);

	/* Configure for RAW10 (S5K JN1) or RAW8 (GC5035) */
	csi_write(CSI_TOP_CON, 0x1); /* Enable CSI */
	delay(10);

	redwood_keep("g510_mtkcam: CSI-2 configured");
	return 0;
}

/*
 * Configure CAMSV for capture DMA.
 * Returns 0 on success, error code on failure.
 */
static int
g510_mtkcam_config_camsv(void)
{
	/* Configure CAMSV channel 0 for main camera */
	camsv_write(CAMSV_DMA_CON + CAMSV_CH0 * 0x100, 0x1); /* Enable DMA */
	camsv_write(CAMSV_DMA_BASE + CAMSV_CH0 * 0x100, 0); /* Base addr (set at capture time) */
	camsv_write(CAMSV_DMA_XSIZE + CAMSV_CH0 * 0x100, 1632); /* Width */
	camsv_write(CAMSV_DMA_YSIZE + CAMSV_CH0 * 0x100, 1224); /* Height */
	camsv_write(CAMSV_DMA_STRIDE + CAMSV_CH0 * 0x100, 1632 * 2); /* Stride (RAW10 = 2 bytes/pixel) */

	/* Configure CAMSV channel 1 for front camera */
	camsv_write(CAMSV_DMA_CON + CAMSV_CH1 * 0x100, 0x1);
	camsv_write(CAMSV_DMA_BASE + CAMSV_CH1 * 0x100, 0);
	camsv_write(CAMSV_DMA_XSIZE + CAMSV_CH1 * 0x100, 2592); /* Width */
	camsv_write(CAMSV_DMA_YSIZE + CAMSV_CH1 * 0x100, 1944); /* Height */
	camsv_write(CAMSV_DMA_STRIDE + CAMSV_CH1 * 0x100, 2592 * 2);

	camsv_write(CAMSV_TOP_CON, 0x1); /* Enable CAMSV */
	delay(10);

	redwood_keep("g510_mtkcam: CAMSV configured");
	return 0;
}

/*
 * Main initialization function for camera driver.
 * Called with boot-arg camera=1.
 */
void
g510_mtkcam_init(void)
{
	uint32_t step = 0;
	int err;

	if (!PE_parse_boot_argn("camera", &step, sizeof(step)) || step == 0) {
		return;
	}

	redwood_keep("g510_mtkcam: init started (step=%u)", step);

	/* Map hardware registers */
	g510_csi = ml_io_map(CSI_PHYS, 0x10000);
	g510_camsv = ml_io_map(CAMSV_PHYS, 0x10000);
	g510_isp = ml_io_map(ISP_PHYS, 0x10000);
	g510_cci = ml_io_map(CCI_PHYS, 0x10000);

	if (g510_csi == 0 || g510_camsv == 0 || g510_isp == 0 || g510_cci == 0) {
		redwood_keep("g510_mtkcam: failed to map registers");
		return;
	}

	/* Configure CSI-2 */
	err = g510_mtkcam_config_csi();
	if (err) {
		redwood_keep("g510_mtkcam: CSI config failed (%d)", err);
		return;
	}

	/* Configure CAMSV */
	err = g510_mtkcam_config_camsv();
	if (err) {
		redwood_keep("g510_mtkcam: CAMSV config failed (%d)", err);
		return;
	}

	/* Enable CCI */
	cci_write(CCI_EN, 0x1);
	cci_write(CCI_CON, 0x1); /* Standard mode, 100kHz */
	delay(10);

	/* Initialize main camera (S5K JN1) */
	err = g510_mtkcam_init_s5kjn1();
	if (err) {
		redwood_keep("g510_mtkcam: S5K JN1 init failed (%d)", err);
	} else {
		redwood_keep("g510_mtkcam: main camera ready");
	}

	/* Initialize front camera (GC5035) */
	err = g510_mtkcam_init_gc5035();
	if (err) {
		redwood_keep("g510_mtkcam: GC5035 init failed (%d)", err);
	} else {
		redwood_keep("g510_mtkcam: front camera ready");
	}

	redwood_keep("g510_mtkcam: initialization complete (main=%d, front=%d)",
	    g510_camera_main_ready, g510_camera_front_ready);
}