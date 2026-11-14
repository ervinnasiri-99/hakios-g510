/*
 * SCP sensor hub driver for the G510 (MT6765).
 * The SCP (Sensor Control Processor) manages accelerometer, gyroscope, and proximity.
 * Communication via IPI/mailbox and shared memory.
 * License: APSL 2.0
 */
#include <kern/clock.h>
#include <libkern/libkern.h>
#include <mach/mach_types.h>
#include <pexpert/pexpert.h>
#include <sys/systm.h>
#include <sys/redwood_input.h>
#include <sys/sysctl.h>
#include <sys/kern_control.h>

extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
extern void redwood_keep(const char *format, ...) __printflike(1, 2);
extern void delay(int usec);
extern void delay_for_interval(uint32_t interval, uint32_t scale_factor);
extern kern_return_t kernel_thread_start(void (*func)(void *), void *arg, thread_t *new_thread);

#define SCP_PHYS                0x10006000u  /* Same as SPM? Check MT6765 datasheet */
#define SCP_IPI_PHYS            0x10006200u  /* SCP IPI registers */
#define SCP_SRAM_PHYS           0x10006800u  /* SCP shared SRAM */

#define INFRACFG_PHYS           0x10001000u
#define INFRACFG_SCP_CTRL       0x0840

/* SCP IPI registers */
#define SCP_IPI_SEND            0x0000
#define SCP_IPI_STA             0x0004
#define SCP_IPI_ACK             0x0008
#define SCP_IPI_MASK            0x000C
#define SCP_IPI_SEND_COM        0x0010
#define SCP_IPI_STA_COM         0x0014
#define SCP_IPI_ACK_COM         0x0018
#define SCP_IPI_MASK_COM        0x001C

/* IPI IDs for sensor communication */
#define IPI_SENSOR_CMD          0
#define IPI_SENSOR_DATA         1
#define IPI_SENSOR_EVENT        2

/* Sensor types */
#define SENSOR_ACCELEROMETER    1
#define SENSOR_GYROSCOPE        2
#define SENSOR_PROXIMITY        3
#define SENSOR_LIGHT            4
#define SENSOR_MAGNETOMETER     5

/* Sensor commands */
#define SENSOR_CMD_ENABLE       0x01
#define SENSOR_CMD_DISABLE      0x02
#define SENSOR_CMD_SET_DELAY    0x03
#define SENSOR_CMD_GET_DATA     0x04
#define SENSOR_CMD_CALIBRATE    0x05

SYSCTL_DECL(_kern_g510);
SYSCTL_NODE(_kern_g510, OID_AUTO, scp,
    CTLFLAG_RD | CTLFLAG_LOCKED, 0, "G510 SCP sensor hub");

static int32_t g510_accel_x = 0, g510_accel_y = 0, g510_accel_z = 0;
static int32_t g510_gyro_x = 0, g510_gyro_y = 0, g510_gyro_z = 0;
static int32_t g510_proximity = 0;
static int32_t g510_light = 0;

SYSCTL_INT(_kern_g510_scp, OID_AUTO, accel_x, CTLFLAG_RD | CTLFLAG_LOCKED,
    &g510_accel_x, 0, "Accelerometer X (mg)");
SYSCTL_INT(_kern_g510_scp, OID_AUTO, accel_y, CTLFLAG_RD | CTLFLAG_LOCKED,
    &g510_accel_y, 0, "Accelerometer Y (mg)");
SYSCTL_INT(_kern_g510_scp, OID_AUTO, accel_z, CTLFLAG_RD | CTLFLAG_LOCKED,
    &g510_accel_z, 0, "Accelerometer Z (mg)");
SYSCTL_INT(_kern_g510_scp, OID_AUTO, gyro_x, CTLFLAG_RD | CTLFLAG_LOCKED,
    &g510_gyro_x, 0, "Gyroscope X (mdps)");
SYSCTL_INT(_kern_g510_scp, OID_AUTO, gyro_y, CTLFLAG_RD | CTLFLAG_LOCKED,
    &g510_gyro_y, 0, "Gyroscope Y (mdps)");
SYSCTL_INT(_kern_g510_scp, OID_AUTO, gyro_z, CTLFLAG_RD | CTLFLAG_LOCKED,
    &g510_gyro_z, 0, "Gyroscope Z (mdps)");
SYSCTL_INT(_kern_g510_scp, OID_AUTO, proximity, CTLFLAG_RD | CTLFLAG_LOCKED,
    &g510_proximity, 0, "Proximity (cm)");
SYSCTL_INT(_kern_g510_scp, OID_AUTO, light, CTLFLAG_RD | CTLFLAG_LOCKED,
    &g510_light, 0, "Ambient light (lux)");

static vm_offset_t g510_scp_ipi;
static vm_offset_t g510_scp_sram;
static vm_offset_t g510_infracfg;

static uint32_t
ipi_read(uint32_t offset)
{
	return *(volatile uint32_t *)(g510_scp_ipi + offset);
}

static void
ipi_write(uint32_t offset, uint32_t value)
{
	*(volatile uint32_t *)(g510_scp_ipi + offset) = value;
}

static uint32_t
infracfg_read(uint32_t offset)
{
	return *(volatile uint32_t *)(g510_infracfg + offset);
}

static void
infracfg_write(uint32_t offset, uint32_t value)
{
	*(volatile uint32_t *)(g510_infracfg + offset) = value;
}

/*
 * Send command to SCP via IPI.
 * Returns 0 on success, error code on failure.
 */
static int
g510_scp_send_cmd(uint32_t sensor_type, uint32_t cmd, uint32_t param)
{
	uint32_t ipi_data = (sensor_type << 24) | (cmd << 16) | (param & 0xFFFF);

	/* Wait for IPI to be ready */
	for (int i = 0; i < 1000; i++) {
		if (!(ipi_read(SCP_IPI_STA) & (1u << IPI_SENSOR_CMD))) {
			break;
		}
		delay(10);
	}

	/* Send command */
	ipi_write(SCP_IPI_SEND + IPI_SENSOR_CMD * 4, ipi_data);
	delay(10);

	return 0;
}

/*
 * Wait for response from SCP.
 * Returns 0 on success, error code on failure.
 */
static int
g510_scp_wait_response(uint32_t *out_data)
{
	for (int i = 0; i < 10000; i++) {
		if (ipi_read(SCP_IPI_STA) & (1u << IPI_SENSOR_DATA)) {
			*out_data = ipi_read(SCP_IPI_STA_COM + IPI_SENSOR_DATA * 4);
			/* Acknowledge */
			ipi_write(SCP_IPI_ACK + IPI_SENSOR_DATA * 4, 1);
			return 0;
		}
		delay(10);
	}
	return ETIMEDOUT;
}

/*
 * Enable a sensor on the SCP.
 * Returns 0 on success, error code on failure.
 */
static int
g510_scp_enable_sensor(uint32_t sensor_type)
{
	uint32_t response;
	int err;

	err = g510_scp_send_cmd(sensor_type, SENSOR_CMD_ENABLE, 0);
	if (err) return err;

	err = g510_scp_wait_response(&response);
	if (err) return err;

	redwood_keep("g510_scp: sensor %u enabled, response=0x%08X", sensor_type, response);
	return 0;
}

/*
 * Set sensor sampling delay.
 * Returns 0 on success, error code on failure.
 */
static int
g510_scp_set_delay(uint32_t sensor_type, uint32_t delay_ms)
{
	return g510_scp_send_cmd(sensor_type, SENSOR_CMD_SET_DELAY, delay_ms);
}

/*
 * Read sensor data from SCP shared memory.
 * The SCP writes sensor data to shared SRAM periodically.
 */
static void
g510_scp_read_sensor_data(void)
{
	volatile uint32_t *sram = (volatile uint32_t *)g510_scp_sram;

	/* Read accelerometer data (example layout) */
	g510_accel_x = (int32_t)sram[0];
	g510_accel_y = (int32_t)sram[1];
	g510_accel_z = (int32_t)sram[2];

	/* Read gyroscope data */
	g510_gyro_x = (int32_t)sram[4];
	g510_gyro_y = (int32_t)sram[5];
	g510_gyro_z = (int32_t)sram[6];

	/* Read proximity */
	g510_proximity = (int32_t)sram[8];

	/* Read ambient light */
	g510_light = (int32_t)sram[9];
}

/*
 * SCP sensor update thread - periodically reads sensor data.
 */
static void
g510_scp_update_thread(void *arg)
{
#pragma unused(arg)
	while (1) {
		g510_scp_read_sensor_data();
		delay_for_interval(20, NSEC_PER_MSEC); /* 50Hz update */
	}
}

/*
 * Main initialization function for SCP sensor hub.
 * Called with boot-arg sensors=1.
 */
void
g510_mtk_scp_init(void)
{
	uint32_t step = 0;
	thread_t thread;
	int err;

	if (!PE_parse_boot_argn("sensors", &step, sizeof(step)) || step == 0) {
		return;
	}

	redwood_keep("g510_scp: init started (step=%u)", step);

	/* Map hardware registers */
	g510_infracfg = ml_io_map(INFRACFG_PHYS, 0x10000);
	g510_scp_ipi = ml_io_map(SCP_IPI_PHYS, 0x1000);
	g510_scp_sram = ml_io_map(SCP_SRAM_PHYS, 0x10000);

	if (g510_infracfg == 0 || g510_scp_ipi == 0 || g510_scp_sram == 0) {
		redwood_keep("g510_scp: failed to map registers");
		return;
	}

	/* Enable SCP clock in INFRACFG */
	infracfg_write(INFRACFG_SCP_CTRL, 1);
	delay(100);

	/* Enable accelerometer */
	err = g510_scp_enable_sensor(SENSOR_ACCELEROMETER);
	if (err) {
		redwood_keep("g510_scp: accelerometer enable failed (%d)", err);
	}

	/* Enable gyroscope */
	err = g510_scp_enable_sensor(SENSOR_GYROSCOPE);
	if (err) {
		redwood_keep("g510_scp: gyroscope enable failed (%d)", err);
	}

	/* Enable proximity */
	err = g510_scp_enable_sensor(SENSOR_PROXIMITY);
	if (err) {
		redwood_keep("g510_scp: proximity enable failed (%d)", err);
	}

	/* Set default delays (20ms = 50Hz) */
	g510_scp_set_delay(SENSOR_ACCELEROMETER, 20);
	g510_scp_set_delay(SENSOR_GYROSCOPE, 20);
	g510_scp_set_delay(SENSOR_PROXIMITY, 100);

	/* Start periodic update thread */
	err = kernel_thread_start(g510_scp_update_thread, NULL, &thread);
	if (err) {
		redwood_keep("g510_scp: failed to start update thread (%d)", err);
		return;
	}
	thread_deallocate(thread);

	redwood_keep("g510_scp: initialization complete");
}