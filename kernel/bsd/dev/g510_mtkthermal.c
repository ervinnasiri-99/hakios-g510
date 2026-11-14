/*
 * Thermal driver for the G510 (MT6765).
 * Reads thermal sensors (APMIXED/SCP), implements thermal throttling.
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

#define APMIXED_PHYS            0x10200000u
#define APMIXED_THERM_CON0      0x0000
#define APMIXED_THERM_CON1      0x0004
#define APMIXED_THERM_CON2      0x0008
#define APMIXED_THERM_DATA      0x0010
#define APMIXED_THERM_INT_EN    0x0020
#define APMIXED_THERM_INT_STA   0x0024

#define TOPCKGEN_PHYS           0x10000000u
#define TOPCKGEN_THERM_CTRL     0x0080

#define SCP_PHYS                0x10006000u
#define SCP_THERM_DATA          0x0080

/* Thermal sensor IDs */
#define THERMAL_SENSOR_CPU      0
#define THERMAL_SENSOR_GPU      1
#define THERMAL_SENSOR_PMIC     2
#define THERMAL_SENSOR_SKIN     3
#define THERMAL_SENSOR_BATTERY  4

/* Thermal sysctl node */
SYSCTL_DECL(_kern_g510);
SYSCTL_NODE(_kern_g510, OID_AUTO, thermal,
    CTLFLAG_RD | CTLFLAG_LOCKED, 0, "G510 thermal zones");

static int32_t g510_thermal_cpu = 0;      /* CPU temp (0.1°C) */
static int32_t g510_thermal_gpu = 0;      /* GPU temp (0.1°C) */
static int32_t g510_thermal_pmic = 0;     /* PMIC temp (0.1°C) */
static int32_t g510_thermal_skin = 0;     /* Skin temp (0.1°C) */
static int32_t g510_thermal_battery = 0;  /* Battery temp (0.1°C) */
static int32_t g510_thermal_throttle = 0; /* Throttling active (1=yes) */
static int32_t g510_thermal_stop_temp = 1050; /* Emergency stop at 105.0°C */

SYSCTL_INT(_kern_g510_thermal, OID_AUTO, cpu, CTLFLAG_RD | CTLFLAG_LOCKED,
    &g510_thermal_cpu, 0, "CPU temperature (0.1°C)");
SYSCTL_INT(_kern_g510_thermal, OID_AUTO, gpu, CTLFLAG_RD | CTLFLAG_LOCKED,
    &g510_thermal_gpu, 0, "GPU temperature (0.1°C)");
SYSCTL_INT(_kern_g510_thermal, OID_AUTO, pmic, CTLFLAG_RD | CTLFLAG_LOCKED,
    &g510_thermal_pmic, 0, "PMIC temperature (0.1°C)");
SYSCTL_INT(_kern_g510_thermal, OID_AUTO, skin, CTLFLAG_RD | CTLFLAG_LOCKED,
    &g510_thermal_skin, 0, "Skin temperature (0.1°C)");
SYSCTL_INT(_kern_g510_thermal, OID_AUTO, battery, CTLFLAG_RD | CTLFLAG_LOCKED,
    &g510_thermal_battery, 0, "Battery temperature (0.1°C)");
SYSCTL_INT(_kern_g510_thermal, OID_AUTO, throttle, CTLFLAG_RD | CTLFLAG_LOCKED,
    &g510_thermal_throttle, 0, "Throttling active (1=yes)");
SYSCTL_INT(_kern_g510_thermal, OID_AUTO, stop_temp, CTLFLAG_RD | CTLFLAG_LOCKED,
    &g510_thermal_stop_temp, 0, "Emergency stop temperature (0.1°C)");

static vm_offset_t g510_apmixed;
static vm_offset_t g510_topckgen;
static vm_offset_t g510_scp;

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
scp_read(uint32_t offset)
{
	return *(volatile uint32_t *)(g510_scp + offset);
}

static void
scp_write(uint32_t offset, uint32_t value)
{
	*(volatile uint32_t *)(g510_scp + offset) = value;
}

/*
 * Read thermal sensor from APMIXEDSYS.
 * Returns temperature in 0.1°C, or -1 on error.
 */
static int32_t
g510_thermal_read_apmixed(uint32_t sensor_id)
{
	/* Configure thermal controller for sensor */
	apmixed_write(APMIXED_THERM_CON0, (sensor_id << 16) | 0x1); /* Select sensor, enable */
	delay(100); /* Wait for ADC conversion */

	/* Read temperature data */
	uint32_t data = apmixed_read(APMIXED_THERM_DATA);
	if (data & 0x80000000) { /* Valid bit */
		return (int32_t)(data & 0x7FFFFFFF); /* Temperature in 0.1°C */
	}
	return -1;
}

/*
 * Read thermal sensor from SCP (for skin/battery).
 * Returns temperature in 0.1°C, or -1 on error.
 */
static int32_t
g510_thermal_read_scp(uint32_t sensor_id)
{
	/* SCP writes thermal data to shared memory periodically */
	volatile uint32_t *scp_therm = (volatile uint32_t *)(g510_scp + SCP_THERM_DATA);
	return (int32_t)scp_therm[sensor_id];
}

/*
 * Apply thermal throttling based on temperature.
 */
static void
g510_thermal_apply_throttling(void)
{
	int32_t max_temp = g510_thermal_cpu;
	if (g510_thermal_gpu > max_temp) max_temp = g510_thermal_gpu;
	if (g510_thermal_pmic > max_temp) max_temp = g510_thermal_pmic;
	if (g510_thermal_skin > max_temp) max_temp = g510_thermal_skin;

	/* Throttle at 85°C */
	if (max_temp >= 850) {
		g510_thermal_throttle = 1;
		/* Request CPU frequency reduction */
		/* This would call into cpufreq driver */
		redwood_keep("g510_thermal: throttling active (max temp=%d°C)", max_temp / 10);
	} else {
		g510_thermal_throttle = 0;
	}

	/* Emergency stop at configured temperature */
	if (max_temp >= g510_thermal_stop_temp) {
		redwood_keep("g510_thermal: EMERGENCY STOP (temp=%d°C, limit=%d°C)",
		    max_temp / 10, g510_thermal_stop_temp / 10);
		/* Request handover to loader */
		/* kern_reboot(RB_HALT) or similar */
	}
}

/*
 * Thermal monitoring thread - periodically reads sensors and applies throttling.
 */
static void
g510_thermal_monitor_thread(void *arg)
{
#pragma unused(arg)
	while (1) {
		/* Read APMIXED sensors */
		g510_thermal_cpu = g510_thermal_read_apmixed(THERMAL_SENSOR_CPU);
		g510_thermal_gpu = g510_thermal_read_apmixed(THERMAL_SENSOR_GPU);
		g510_thermal_pmic = g510_thermal_read_apmixed(THERMAL_SENSOR_PMIC);

		/* Read SCP sensors */
		g510_thermal_skin = g510_thermal_read_scp(THERMAL_SENSOR_SKIN);
		g510_thermal_battery = g510_thermal_read_scp(THERMAL_SENSOR_BATTERY);

		/* Apply throttling */
		g510_thermal_apply_throttling();

		redwood_keep("g510_thermal: CPU=%d°C, GPU=%d°C, PMIC=%d°C, Skin=%d°C, Batt=%d°C, Throttle=%d",
		    g510_thermal_cpu / 10, g510_thermal_gpu / 10, g510_thermal_pmic / 10,
		    g510_thermal_skin / 10, g510_thermal_battery / 10, g510_thermal_throttle);

		delay_for_interval(5, NSEC_PER_SEC); /* Check every 5 seconds */
	}
}

/*
 * Main initialization function for thermal driver.
 * Called with boot-arg thermal=1.
 */
void
g510_mtkthermal_init(void)
{
	uint32_t step = 0;
	thread_t thread;
	int err;

	if (!PE_parse_boot_argn("thermal", &step, sizeof(step)) || step == 0) {
		return;
	}

	/* Parse thermal_stop boot-arg */
	PE_parse_boot_argn("thermal_stop", &g510_thermal_stop_temp, sizeof(g510_thermal_stop_temp));

	redwood_keep("g510_thermal: init started (step=%u, stop=%d°C)", step, g510_thermal_stop_temp / 10);

	/* Map hardware registers */
	g510_apmixed = ml_io_map(APMIXED_PHYS, 0x10000);
	g510_topckgen = ml_io_map(TOPCKGEN_PHYS, 0x1000);
	g510_scp = ml_io_map(SCP_PHYS, 0x10000);

	if (g510_apmixed == 0 || g510_topckgen == 0 || g510_scp == 0) {
		redwood_keep("g510_thermal: failed to map registers");
		return;
	}

	/* Enable thermal controller in APMIXEDSYS */
	apmixed_write(APMIXED_THERM_CON0, 0x1); /* Enable thermal ADC */
	apmixed_write(APMIXED_THERM_CON1, 0xFFFF); /* Enable all sensors */
	apmixed_write(APMIXED_THERM_INT_EN, 0xF); /* Enable interrupts for thresholds */
	delay(10);

	/* Enable thermal clock in TOPCKGEN */
	topckgen_write(TOPCKGEN_THERM_CTRL, 0x1);
	delay(10);

	/* Start thermal monitoring thread */
	err = kernel_thread_start(g510_thermal_monitor_thread, NULL, &thread);
	if (err) {
		redwood_keep("g510_thermal: failed to start monitor thread (%d)", err);
		return;
	}
	thread_deallocate(thread);

	redwood_keep("g510_thermal: initialization complete");
}