/*
 * CONNSYS (Wi-Fi/Bluetooth): power, firmware, the MCU patch (PDA), BTIF, STP/WMT, connsys_sw_init;
 * Bluetooth's function on, HCI over STP channel 0, the kernel control com.apple.uart.sk.bluetooth
 * and the answers bluetoothd needs changed.
 * License: APSL 2.0
 */
#include <kern/clock.h>
#include <libkern/libkern.h>
#include <mach/mach_types.h>
#include <pexpert/pexpert.h>
#include <sys/systm.h>
#include <sys/sysctl.h>
#include <kern/thread.h>
#include <kern/kalloc.h>
#include <kern/locks.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/proc.h>
#include <sys/ttycom.h>
#include <sys/termios.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <sys/kern_control.h>
#include <sys/kpi_mbuf.h>
#include <sys/sys_domain.h>
#include <uuid/uuid.h>
#include <miscfs/devfs/devfs.h>

extern kern_return_t kernel_thread_start_priority(thread_continue_t continuation, void *parameter, integer_t priority, thread_t *new_thread);

extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
extern vm_offset_t ml_io_map_unmappable(vm_offset_t phys_addr, vm_size_t size, uint32_t flags);
extern void ml_io_unmap(vm_offset_t addr, vm_size_t sz);
extern void redwood_keep(const char *format, ...) __printflike(1, 2);
extern void delay(int usec);
extern void delay_for_interval(uint32_t interval, uint32_t scale_factor);
extern boolean_t ml_set_interrupts_enabled(boolean_t enable);

#define SPM_PHYS                0x10006000u
#define SPM_TOP1_PWR_CTRL       0x32C
#define SPM_PWR_STATUS          0x180
#define SPM_PWR_STATUS_2ND      0x184
#define PWR_STATUS_CONN         (1u << 1)
#define PWR_STATUS_CONN_TOP2    (1u << 30)

#define MCU_TOP_MISC_OFF_PHYS   0x180B1000u
#define CONSYS_IP_VER           0x10
#define CONSYS_CONF_ID          0x1C
#define CONSYS_IP_VER_ID        0x10020501u

#define MCU_PHYS                0x18002000u
#define CONSYS_HW_ID            0x00
#define CONSYS_FW_ID            0x04

#define PWRAP_PHYS              0x1000d000u
#define PWRAP_WACS2_CMD         0xc20
#define PWRAP_WACS2_RDATA       0xc24
#define PWRAP_WACS2_VLDCLR      0xc28
#define PWRAP_FSM(r)            (((r) >> 16) & 7u)

#define BTIF_PHYS               0x1100c000u
#define BTIF_TX_DMA_PHYS        0x11000b80u
#define BTIF_TX_VFF_PHYS        0x51200000u
#define BTIF_TX_VFF_SIZE        8192u

#define CONN_HIF_ON_PHYS        0x18007000u
#define CONN_HIF_ON_CPUPCR      0x104

#define RGU_PHYS                0x10007000u
#define RGU_SWRST               0x18
#define RGU_CONNSYS_SW_RST      (1u << 12)
#define RGU_KEY                 (0x88u << 24)

#define INFRACFG_PHYS           0x10001000u
#define INFRACFG_IFR2_CLR       0x84
#define INFRACFG_IFR2_SET       0x80
#define INFRACFG_IFR_BTIF       (1u << 31)
#define INFRACFG_AP_DMA         (1u << 18)

#define INFRACFG_BUS_PROT1_CLR  0x2a4
#define INFRACFG_BUS_PROT2_CLR  0x2ac
#define INFRACFG_BUS_PROT1_SET  0x2a0
#define INFRACFG_BUS_PROT2_SET  0x2a8

#define SPM_KEY                 0x000
#define SPM_KEY_VAL             0x0b160001u

#define PINMUX_PHYS             0x10005000u
#define PINMUX_CONNSYS_0        0x3f0
#define PINMUX_CONNSYS_1        0x3e0

#define CONNSYS_EMI_PHYS        0x51000000u
#define CONNSYS_EMI_SIZE        0x00400000u
#define CONNSYS_EMI_COREDUMP_OFFSET 0x68000u
#define CONNSYS_EMI_FW_PHY_BASE 0xf0000000u

#define WBG_PHYS                0x180B3000u
#define WBG_AFE_PLL             0x34
#define WBG_AFE_PLL_WAK         0x80000u

#define TCM_PHYS                0x10005000u

static uint32_t
rd(vm_offset_t base, uint32_t offset)
{
	return *(volatile uint32_t *)(base + offset);
}

static void
wr(vm_offset_t base, uint32_t offset, uint32_t value)
{
	*(volatile uint32_t *)(base + offset) = value;
}

static uint32_t
connsys_ms(void)
{
	uint64_t ns = 0;
	absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
	return (uint32_t)(ns / NSEC_PER_MSEC);
}

static vm_offset_t connsys_pwrap;

static bool
connsys_pwrap_wait(uint32_t state)
{
	for (uint32_t spin = 0; spin < 100000; spin++) {
		if (PWRAP_FSM(rd(connsys_pwrap, PWRAP_WACS2_RDATA)) == state) {
			return true;
		}
	}
	return false;
}

static bool
connsys_pmic_read_raw(uint32_t reg, uint32_t *value)
{
	if (!connsys_pwrap_wait(0)) {
		return false;
	}
	*(volatile uint32_t *)(connsys_pwrap + PWRAP_WACS2_CMD) = (reg >> 1) << 16;
	if (!connsys_pwrap_wait(6)) {
		return false;
	}
	*value = rd(connsys_pwrap, PWRAP_WACS2_RDATA) & 0xffff;
	*(volatile uint32_t *)(connsys_pwrap + PWRAP_WACS2_VLDCLR) = 1;
	return true;
}

static bool
connsys_pmic_read(uint32_t reg, uint32_t *value)
{
	bool ok;

	redwood_pwrap_lock();
	ok = connsys_pmic_read_raw(reg, value);
	redwood_pwrap_unlock();
	return ok;
}

static void
connsys_power_report(void)
{
	static const struct { const char *name; uint32_t enable, analog; } ldo[] = {
		{ "VCN18", 0x1c58, 0x1e8c },
		{ "VCN28", 0x1d88, 0x1e14 },
		{ "VCN33 (BT)", 0x1d1c, 0x1e28 },
		{ "VCN33 (Wi-Fi)", 0x1d2a, 0x1e28 },
	};

	connsys_pwrap = ml_io_map(PWRAP_PHYS, 0x1000);
	for (unsigned i = 0; i < sizeof(ldo) / sizeof(ldo[0]); i++) {
		uint32_t enable = 0xdead, analog = 0xdead;
		bool read = connsys_pmic_read(ldo[i].enable, &enable) && connsys_pmic_read(ldo[i].analog, &analog);

		redwood_keep("connsys: step 2: %s: %s, enable register %#x = %#x, analog %#x = %#x", ldo[i].name,
		    read ? ((enable & 1) ? "ON" : "off") : "NOT READ", ldo[i].enable, enable, ldo[i].analog, analog);
	}
}

static bool
connsys_pmic_write_raw(uint32_t reg, uint32_t value)
{
	if (!connsys_pwrap_wait(0)) {
		return false;
	}
	*(volatile uint32_t *)(connsys_pwrap + PWRAP_WACS2_CMD) = (1u << 31) | (reg >> 1) << 16 | (value & 0xffff);
	return connsys_pwrap_wait(0);
}

static bool
connsys_pmic_write(uint32_t reg, uint32_t value)
{
	bool ok;

	redwood_pwrap_lock();
	ok = connsys_pmic_write_raw(reg, value);
	redwood_pwrap_unlock();
	return ok;
}

static bool
connsys_pmic_update(uint32_t reg, uint32_t mask, uint32_t value)
{
	uint32_t now;

	return connsys_pmic_read(reg, &now) && connsys_pmic_write(reg, (now & ~mask) | (value & mask));
}

static void
connsys_xo_wcn_on(void)
{
	uint32_t cw00 = 0, cw01 = 0, after = 0, wanted = 1;
	bool known;

	(void)PE_parse_boot_argn("connsys_xo", &wanted, sizeof(wanted));
	if (!connsys_pmic_read(0x788, &cw00) || !connsys_pmic_read(0x78e, &cw01)) {
		redwood_keep("connsys: step 3: XO_WCN: DCXO_CW00/CW01 not read (the wrapper did not answer)");
		return;
	}
	known = (cw00 & ~0x20u) == (0x4e1du & ~0x20u);
	if (wanted != 0 && known && (cw00 & 0x20u) == 0) {
		(void)connsys_pmic_update(0x788, 0x20u, 0x20u);
		delay_for_interval(2, NSEC_PER_MSEC);
	}
	(void)connsys_pmic_read(0x788, &after);
	redwood_keep("connsys: step 3: XO_WCN: DCXO_CW00 %#x CW01 %#x (%s) -> CW00 %#x, XO_EXTBUF2_EN_M %u%s", cw00, cw01,
	    known ? "Android's boot value" : "NOT Android's 0x4e1d: not written", after, (after >> 5) & 1,
	    wanted == 0 ? " (connsys_xo=0)" : "");
}

static void
connsys_supplies_on(void)
{
	bool ok = connsys_pmic_write(0x1c5a, 1) &&
	    connsys_pmic_update(0x1c58, 1, 1) &&
	    connsys_pmic_write(0x1d1e, 1) &&
	    connsys_pmic_update(0x1e28, 0x300, 0x100) &&
	    connsys_pmic_update(0x1d1c, 1, 1) &&
	    connsys_pmic_update(0x1d2a, 1, 1) &&
	    connsys_pmic_update(0x1d88, 1, 1);

	delay(1000);
	redwood_keep("connsys: step 3: VCN18, VCN33 (3.3 V) and VCN28 turned on: %s", ok ? "written" : "THE WRAPPER DID NOT ANSWER");
	connsys_power_report();
	connsys_xo_wcn_on();
}

static bool
connsys_wait(vm_offset_t base, uint32_t offset, uint32_t bit, const char *what)
{
	for (unsigned n = 0; n < 10000; n++) {
		if (rd(base, offset) & bit) {
			return true;
		}
		delay(10);
	}
	redwood_keep("connsys: step 4 [%u ms]: %s: no ack after 100 ms (%#x = %#x)", connsys_ms(), what, offset, rd(base, offset));
	return false;
}

static void
connsys_power_on(vm_offset_t spm)
{
	vm_offset_t pinmux = ml_io_map(PINMUX_PHYS, 0x1000), rgu = ml_io_map(RGU_PHYS, 0x1000);
	vm_offset_t infra = ml_io_map(INFRACFG_PHYS, 0x1000), misc = ml_io_map(MCU_TOP_MISC_OFF_PHYS, 0x1000);
	uint32_t version = 0;

#define W(base, offset, value) (*(volatile uint32_t *)((base) + (offset)) = (value))
	W(pinmux, PINMUX_CONNSYS_0, (rd(pinmux, PINMUX_CONNSYS_0) & 0xffffff88u) | 0x11u);
	W(pinmux, PINMUX_CONNSYS_1, (rd(pinmux, PINMUX_CONNSYS_1) & 0x8888888fu) | 0x11111110u);
	delay(150);
	W(rgu, RGU_SWRST, rd(rgu, RGU_SWRST) | RGU_CONNSYS_SW_RST | RGU_KEY);
	W(spm, SPM_KEY, (rd(spm, SPM_KEY) & 0xffffu) | SPM_KEY_VAL);
	W(spm, SPM_TOP1_PWR_CTRL, rd(spm, SPM_TOP1_PWR_CTRL) | (1u << 2));
	if (!connsys_wait(spm, SPM_PWR_STATUS, 1u << 1, "on-domain power")) {
		return;
	}
	W(spm, SPM_TOP1_PWR_CTRL, rd(spm, SPM_TOP1_PWR_CTRL) | (1u << 3));
	if (!connsys_wait(spm, SPM_PWR_STATUS_2ND, 1u << 1, "on-domain power, second part")) {
		return;
	}
	W(spm, SPM_TOP1_PWR_CTRL, rd(spm, SPM_TOP1_PWR_CTRL) & ~(1u << 4));
	W(spm, SPM_TOP1_PWR_CTRL, rd(spm, SPM_TOP1_PWR_CTRL) & ~(1u << 1));
	W(spm, SPM_TOP1_PWR_CTRL, rd(spm, SPM_TOP1_PWR_CTRL) | (1u << 0));
	(void)connsys_wait(spm, SPM_PWR_STATUS, 1u << 30, "off-domain power");
	(void)connsys_wait(spm, SPM_PWR_STATUS_2ND, 1u << 30, "off-domain power, second part");
	W(rgu, RGU_SWRST, (rd(rgu, RGU_SWRST) & ~(1u << 9)) | RGU_KEY);
	W(infra, INFRACFG_BUS_PROT1_CLR, (1u << 14) | (1u << 16));
	W(infra, INFRACFG_BUS_PROT1_CLR, 1u << 13);
	W(infra, INFRACFG_BUS_PROT2_CLR, 1u << 18);
	delay(10);
	{
		uint32_t spmx = 1;

		(void)PE_parse_boot_argn("connsys_spmx", &spmx, sizeof(spmx));
		if (spmx != 0) {
			W(spm, 0x6f8, 0x5);
			W(spm, 0x6fc, 0x50505);
		}
	}
#undef W
	delay(10);
	redwood_keep("connsys: step 4 [%u ms]: powered: TOP1_PWR_CTRL %#x, PWR_STATUS %#x/%#x, bus protection %#x (%#x) and %#x (%#x)",
	    connsys_ms(), rd(spm, SPM_TOP1_PWR_CTRL), rd(spm, SPM_PWR_STATUS), rd(spm, SPM_PWR_STATUS_2ND), rd(infra, 0x220), rd(infra, 0x228),
	    rd(infra, 0x250), rd(infra, 0x258));
	{
		uint32_t both = rd(spm, SPM_PWR_STATUS) & rd(spm, SPM_PWR_STATUS_2ND);

		if (!(both & (1u << 1)) || !(both & ((1u << 30) | (1u << 31)))) {
			redwood_keep("connsys: step 4 [%u ms]: the off-domain is not powered (%#x): its registers are not read", connsys_ms(), both);
			return;
		}
		redwood_keep("connsys: step 4 [%u ms]: CONN_HIF_ON CPUPCR %#x", connsys_ms(), rd(ml_io_map(CONN_HIF_ON_PHYS, 0x1000), CONN_HIF_ON_CPUPCR));
	}
	for (unsigned n = 0; n < 10 && version != CONSYS_IP_VER_ID; n++) {
		version = rd(misc, CONSYS_IP_VER);
		delay(20000);
	}
	redwood_keep("connsys: step 4 [%u ms]: IP version %#x: %s", connsys_ms(), version,
	    version == CONSYS_IP_VER_ID ? "CONNSYS LIVES (MT6765's)" : "not the expected 0x10020501");
}

static char connsys_said[1000] = "not run";
SYSCTL_DECL(_kern_redwood);
SYSCTL_STRING(_kern_redwood, OID_AUTO, connsys, CTLFLAG_RD | CTLFLAG_LOCKED, connsys_said, sizeof(connsys_said), "");

static uint8_t *
connsys_file(const char *name, uint32_t *size)
{
	char path[96];
	vfs_context_t context = vfs_context_kernel();
	vnode_t vnode = NULLVP;
	struct vnode_attr attributes;
	uint8_t *data;
	int left = 0;

	snprintf(path, sizeof(path), "/fw/connsys/%s", name);
	if (vnode_lookup(path, 0, &vnode, context) != 0) {
		return NULL;
	}
	VATTR_INIT(&attributes);
	VATTR_WANTED(&attributes, va_data_size);
	if (vnode_getattr(vnode, &attributes, context) != 0 || attributes.va_data_size < 48 ||
	    attributes.va_data_size > CONNSYS_EMI_SIZE) {
		vnode_put(vnode);
		return NULL;
	}
	data = kalloc_data(attributes.va_data_size, Z_WAITOK);
	if (data != NULL && (vn_rdwr(UIO_READ, vnode, (caddr_t)data, (int)attributes.va_data_size, 0, UIO_SYSSPACE, 0,
	    vfs_context_ucred(context), &left, vfs_context_proc(context)) != 0 || left != 0)) {
		kfree_data(data, attributes.va_data_size);
		data = NULL;
	}
	vnode_put(vnode);
	*size = (uint32_t)attributes.va_data_size;
	return data;
}

static vm_offset_t connsys_btif;
static vm_offset_t connsys_dma_tx;
static vm_offset_t connsys_dma_vff;
static uint32_t connsys_dma_wpt;
static uint32_t connsys_dma_wrap;
static uint32_t connsys_dma = 1;

static void
connsys_btif_dma_init(void)
{
	vm_offset_t infra = ml_io_map(INFRACFG_PHYS, 0x1000);

	*(volatile uint32_t *)(infra + 0x8c) = INFRACFG_AP_DMA;
	delay(10);
	if (connsys_dma_tx == 0) {
		connsys_dma_tx = ml_io_map(BTIF_TX_DMA_PHYS, 0x1000);
	}
	if (connsys_dma_vff == 0) {
		connsys_dma_vff = ml_io_map(BTIF_TX_VFF_PHYS, BTIF_TX_VFF_SIZE);
	}
	*(volatile uint32_t *)(connsys_dma_tx + 0x0c) |= 1;
	for (unsigned i = 0; i < 100 && (*(volatile uint32_t *)(connsys_dma_tx + 0x08) & 1); i++) {
		delay(10);
	}
	if (*(volatile uint32_t *)(connsys_dma_tx + 0x08) & 1) {
		*(volatile uint32_t *)(connsys_dma_tx + 0x0c) |= 2;
		delay(10);
		*(volatile uint32_t *)(connsys_dma_tx + 0x0c) &= ~2;
	}
	*(volatile uint32_t *)(connsys_dma_tx + 0x1c) = BTIF_TX_VFF_PHYS;
	*(volatile uint32_t *)(connsys_dma_tx + 0x54) = 0;
	*(volatile uint32_t *)(connsys_dma_tx + 0x24) = BTIF_TX_VFF_SIZE;
	*(volatile uint32_t *)(connsys_dma_tx + 0x2c) = 0;
	*(volatile uint32_t *)(connsys_dma_tx + 0x30) = 0;
	connsys_dma_wpt = 0;
	connsys_dma_wrap = 0;
	*(volatile uint32_t *)(connsys_dma_tx + 0x28) = BTIF_TX_VFF_SIZE - 7;
	*(volatile uint32_t *)(connsys_dma_tx + 0x00) = 0;
	*(volatile uint32_t *)(connsys_dma_tx + 0x04) = 0;
	redwood_keep("connsys: step 6 [%u ms]: AP DMA TX ready: EN %#x, WPT %#x, RPT %#x, LEFT %u",
	    connsys_ms(), rd(connsys_dma_tx, 0x08), rd(connsys_dma_tx, 0x2c), rd(connsys_dma_tx, 0x30), rd(connsys_dma_tx, 0x40));
}

static void
connsys_dma_copy(vm_offset_t dst, const uint8_t *src, unsigned len)
{
	volatile uint8_t *d = (volatile uint8_t *)dst;

	for (unsigned i = 0; i < len; i++) {
		d[i] = src[i];
	}
}

static void
connsys_btif_dma_write(const uint8_t *bytes, unsigned length)
{
	unsigned at = 0, stuck = 0;

	for (unsigned t = 0; t < 50000 && (rd(connsys_dma_tx, 0x14) & 1); t++) {
		delay(10);
	}
	while (at < length) {
		unsigned chunk = length - at;
		unsigned left = rd(connsys_dma_tx, 0x40);

		if (left == 0) {
			unsigned t;
			for (t = 0; t < 50000 && rd(connsys_dma_tx, 0x40) == 0; t++) {
				delay(10);
			}
			if (t >= 50000) {
				stuck = 1;
				break;
			}
			left = rd(connsys_dma_tx, 0x40);
		}
		if (chunk > left) {
			chunk = left;
		}
		if (connsys_dma_wpt + chunk >= BTIF_TX_VFF_SIZE) {
			unsigned tail = BTIF_TX_VFF_SIZE - connsys_dma_wpt;
			connsys_dma_copy(connsys_dma_vff + connsys_dma_wpt, bytes + at, tail);
			connsys_dma_copy(connsys_dma_vff, bytes + at + tail, chunk - tail);
			connsys_dma_wpt = connsys_dma_wpt + chunk - BTIF_TX_VFF_SIZE;
			connsys_dma_wrap ^= 0x10000;
		} else {
			connsys_dma_copy(connsys_dma_vff + connsys_dma_wpt, bytes + at, chunk);
			connsys_dma_wpt += chunk;
		}
		at += chunk;
		__asm__ volatile ("dsb sy" ::: "memory");

		*(volatile uint32_t *)(connsys_dma_tx + 0x08) |= 1;
		*(volatile uint32_t *)(connsys_dma_tx + 0x2c) = connsys_dma_wpt | connsys_dma_wrap;

		uint32_t valid = rd(connsys_dma_tx, 0x3c);
		if (valid > 0 && valid < 8) {
			*(volatile uint32_t *)(connsys_dma_tx + 0x14) |= 1;
		}
	}
	for (unsigned t = 0; !stuck && t < 50000 && rd(connsys_dma_tx, 0x3c) != 0; t++) {
		uint32_t valid = rd(connsys_dma_tx, 0x3c);
		if (valid > 0 && valid < 8) {
			*(volatile uint32_t *)(connsys_dma_tx + 0x14) |= 1;
		}
		delay(10);
	}
	for (unsigned t = 0; !stuck && t < 50000 && !(rd(connsys_btif, 0x14) & (1u << 6)); t++) {
		delay(10);
	}
	if (stuck) {
		redwood_keep("connsys: BTIF DMA TX timeout at %u/%u (LSR %#x, DMA_EN %#x, VALID %u, LEFT %u)",
		    at, length, rd(connsys_btif, 0x14), rd(connsys_dma_tx, 0x08),
		    rd(connsys_dma_tx, 0x3c), rd(connsys_dma_tx, 0x40));
	}
}

static void
connsys_btif_init(void)
{
	static bool asked;

	if (!asked) {
		asked = true;
		(void)PE_parse_boot_argn("connsys_dma", &connsys_dma, sizeof(connsys_dma));
	}
	vm_offset_t infra = ml_io_map(INFRACFG_PHYS, 0x1000);

	*(volatile uint32_t *)(infra + 0x84) = INFRACFG_IFR_BTIF;
	delay(10);
	connsys_btif = ml_io_map(BTIF_PHYS, 0x1000);
	*(volatile uint32_t *)(connsys_btif + 0x0c) = 0;
	*(volatile uint32_t *)(connsys_btif + 0x6c) |= 1;
	*(volatile uint32_t *)(connsys_btif + 0x60) = 0x18;
	*(volatile uint32_t *)(connsys_btif + 0x08) = 1u << 1;
	*(volatile uint32_t *)(connsys_btif + 0x08) = 0;
	*(volatile uint32_t *)(connsys_btif + 0x08) = 1u << 2;
	*(volatile uint32_t *)(connsys_btif + 0x08) = 0;
	if (connsys_dma) {
		connsys_btif_dma_init();
		*(volatile uint32_t *)(connsys_btif + 0x4c) = (1u << 2) | (1u << 1);
	} else {
		*(volatile uint32_t *)(connsys_btif + 0x4c) = 1u << 2;
	}
	*(volatile uint32_t *)(connsys_btif + 0x04) = 0;
	redwood_keep("connsys: step 6 [%u ms]: BTIF on (%s): clock status %#x, LSR %#x",
	    connsys_ms(), connsys_dma ? "DMA TX" : "PIO", rd(infra, 0x90), rd(connsys_btif, 0x14));
}

static void
connsys_build_time(const uint8_t *file, uint32_t offset)
{
	static vm_offset_t times;
	static uint32_t wanted = 1, asked;
	uint8_t text[16];

	if (!asked) {
		asked = 1;
		(void)PE_parse_boot_argn("connsys_times", &wanted, sizeof(wanted));
	}
	if (wanted == 0) {
		return;
	}
	if (times == 0) {
		times = ml_io_map(CONNSYS_EMI_PHYS + 0x68000u, 0x1000);
	}
	memcpy(text, file, 15);
	text[15] = 0;
	for (unsigned i = 0; i < 16; i += 4) {
		uint32_t word;

		memcpy(&word, text + i, 4);
		*(volatile uint32_t *)(times + (offset - 0x68000u) + i) = word;
	}
	__asm__ volatile ("dsb sy" ::: "memory");
}

static void
connsys_btif_listen(const char *what)
{
	char line[200];
	size_t used = 0;
	unsigned got = 0;

	if (connsys_btif == 0) {
		return;
	}
	used = (size_t)snprintf(line, sizeof(line), "%s:", what);
	for (unsigned t = 0; t < 1000 && got < 48; t++) {
		while ((rd(connsys_btif, 0x14) & 1) && got < 48) {
			used += (size_t)snprintf(line + used, sizeof(line) - used, " %02x", rd(connsys_btif, 0x0) & 0xff);
			got++;
		}
		delay(1000);
	}
	if (got == 0) {
		snprintf(line + used, sizeof(line) - used, " nothing (LSR %#x)", rd(connsys_btif, 0x14));
	}
	redwood_keep("connsys: [%u ms] %s", connsys_ms(), line);
	strlcat(connsys_said, " | ", sizeof(connsys_said));
	strlcat(connsys_said, line, sizeof(connsys_said));
}

static bool connsys_stp_full;
static uint8_t connsys_stp_seq, connsys_stp_ack, connsys_stp_expect;
static unsigned connsys_stp_task = 4;
static vm_offset_t connsys_hif;
static vm_offset_t connsys_mcu;
static vm_offset_t connsys_rgu;
static vm_offset_t connsys_wbg;
static uint8_t *connsys_patch_file;
static uint32_t connsys_patch_size;
static bool connsys_chip_id_done;
static unsigned connsys_doing = 7;

static bool
connsys_mcu_restart(void)
{
	uint32_t version = 0;

	if (connsys_rgu == 0 || connsys_mcu == 0) {
		return false;
	}
	redwood_keep("connsys: [%u ms] MCU restart via RGU reset...", connsys_ms());
	*(volatile uint32_t *)(connsys_rgu + RGU_SWRST) = (rd(connsys_rgu, RGU_SWRST) | RGU_CONNSYS_SW_RST) | RGU_KEY;
	delay_for_interval(10, NSEC_PER_MSEC);
	if (connsys_wbg != 0) {
		*(volatile uint32_t *)(connsys_wbg + WBG_AFE_PLL) = (rd(connsys_wbg, WBG_AFE_PLL) & 0xffe7ffffu) | WBG_AFE_PLL_WAK;
		__asm__ volatile ("dsb sy" ::: "memory");
	}
	*(volatile uint32_t *)(connsys_rgu + RGU_SWRST) = (rd(connsys_rgu, RGU_SWRST) & ~RGU_CONNSYS_SW_RST) | RGU_KEY;
	for (unsigned n = 0; n < 50 && version != 0x1D1E; n++) {
		delay_for_interval(20, NSEC_PER_MSEC);
		version = rd(connsys_mcu, 0x600);
	}
	for (unsigned n = 0; n < 100 && (connsys_hif != 0 && rd(connsys_hif, CONN_HIF_ON_CPUPCR) != 0xb04); n++) {
		delay_for_interval(10, NSEC_PER_MSEC);
	}
	delay_for_interval(20, NSEC_PER_MSEC);
	if (connsys_dma && connsys_dma_tx != 0) {
		*(volatile uint32_t *)(connsys_dma_tx + 0x0c) |= 1;
		for (unsigned i = 0; i < 100 && (*(volatile uint32_t *)(connsys_dma_tx + 0x08) & 1); i++) {
			delay(10);
		}
		connsys_dma_wpt = 0;
		connsys_dma_wrap = 0;
		*(volatile uint32_t *)(connsys_dma_tx + 0x2c) = 0;
		*(volatile uint32_t *)(connsys_dma_tx + 0x30) = 0;
	}
	connsys_btif_init();
	redwood_keep("connsys: [%u ms] MCU restarted: 0x600=%#x, CPUPCR=%#x",
	    connsys_ms(), rd(connsys_mcu, 0x600), connsys_hif != 0 ? rd(connsys_hif, CONN_HIF_ON_CPUPCR) : 0);
	return version == 0x1D1E;
}

static uint16_t
connsys_crc16(const uint8_t *bytes, unsigned length)
{
	uint16_t crc = 0;

	for (unsigned i = 0; i < length; i++) {
		crc ^= bytes[i];
		for (int bit = 0; bit < 8; bit++) {
			crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ 0xA001) : (uint16_t)(crc >> 1);
		}
	}
	return crc;
}

static int
connsys_btif_byte(unsigned wait_ms)
{
	for (unsigned t = 0; t <= wait_ms * 10; t++) {
		if (rd(connsys_btif, 0x14) & 1) {
			return (int)(rd(connsys_btif, 0x0) & 0xff);
		}
		delay(100);
	}
	return -1;
}

static void
connsys_stp_send(const uint8_t *payload, unsigned length)
{
	uint8_t frame[4 + 1100 + 2];
	uint16_t crc = connsys_stp_full ? connsys_crc16(payload, length) : 0;

	if (length > 1100) {
		return;
	}

	frame[0] = (uint8_t)(0x80 | (connsys_stp_full ? ((connsys_stp_seq << 3) | connsys_stp_ack) : 0));
	frame[1] = (uint8_t)((connsys_stp_task << 4) | ((length >> 8) & 0xf));
	frame[2] = (uint8_t)length;
	frame[3] = connsys_stp_full ? (uint8_t)(frame[0] + frame[1] + frame[2]) : 0;
	memcpy(frame + 4, payload, length);
	frame[4 + length] = (uint8_t)crc;
	frame[5 + length] = (uint8_t)(crc >> 8);
	connsys_btif_write(frame, 6 + length);
	if (connsys_stp_full) {
		connsys_stp_seq = (connsys_stp_seq + 1) & 7;
	}
}

static int
connsys_stp_receive(uint8_t *payload, unsigned room, char *heard, size_t heard_room, unsigned first_ms)
{
	size_t used = strlen(heard);

	for (;;) {
		int b0 = connsys_btif_byte(first_ms), b1, b2, b3;
		unsigned length;

		if (b0 < 0) {
			return -1;
		}
		if (b0 == 0x55 || !(b0 & 0x80) || (b0 & 0x40)) {
			used += (size_t)snprintf(heard + used, heard_room - used, " ?%02x", b0);
			continue;
		}
		if ((b1 = connsys_btif_byte(50)) < 0 || (b2 = connsys_btif_byte(50)) < 0 || (b3 = connsys_btif_byte(50)) < 0) {
			return -1;
		}
		length = ((unsigned)(b1 & 0xf) << 8) | (unsigned)b2;
		if (length == 0) {
			used += (size_t)snprintf(heard + used, heard_room - used, " [ack %u]", b0 & 7);
			continue;
		}
		for (unsigned i = 0; i < length + 2; i++) {
			int b = connsys_btif_byte(50);

			if (b < 0) {
				return -1;
			}
			if (i < length && i < room) {
				payload[i] = (uint8_t)b;
			}
		}
		if (connsys_stp_full) {
			const uint8_t seq = (uint8_t)((b0 >> 3) & 7);
			const bool again = seq != connsys_stp_expect;
			uint8_t ack[4];

			if (!again) {
				connsys_stp_ack = seq;
				connsys_stp_expect = (uint8_t)((seq + 1) & 7);
			}
			ack[0] = (uint8_t)(0x80 | connsys_stp_ack);
			ack[1] = 0;
			ack[2] = 0;
			ack[3] = ack[0];
			connsys_btif_write(ack, 4);
			if (again) {
				used += (size_t)snprintf(heard + used, heard_room - used, " [seq %u again, %u bytes]", seq, length);
				continue;
			}
		}
		if (((b1 >> 4) & 7) != connsys_stp_task) {
			used += (size_t)snprintf(heard + used, heard_room - used, " [type %u, %u bytes]", (b1 >> 4) & 7, length);
			continue;
		}
		return (int)(length < room ? length : room);
	}
}

static void
connsys_btif_write(const uint8_t *bytes, unsigned length)
{
	if (connsys_dma && connsys_dma_tx != 0) {
		connsys_btif_dma_write(bytes, length);
		return;
	}
	static uint32_t quiet = 0, asked;
	boolean_t was = TRUE;

	if (!asked) {
		asked = 1;
		(void)PE_parse_boot_argn("connsys_irqs_off", &quiet, sizeof(quiet));
	}
	if (quiet) {
		was = ml_set_interrupts_enabled(FALSE);
	}
	unsigned at = 0, stuck = 0;

	while (at < length) {
		unsigned t;
		for (t = 0; t < 50000 && !(rd(connsys_btif, 0x14) & (1u << 6)); t++) {
			delay(10);
		}
		if (t >= 50000) {
			stuck = 1;
			break;
		}
		for (unsigned n = 0; n < 16 && at < length; n++, at++) {
			*(volatile uint8_t *)(connsys_btif + 0x0) = bytes[at];
		}
	}
	for (unsigned t = 0; !stuck && t < 50000 && !(rd(connsys_btif, 0x14) & (1u << 6)); t++) {
		delay(10);
	}
	if (quiet) {
		(void)ml_set_interrupts_enabled(was);
	}
	if (stuck) {
		redwood_keep("connsys: BTIF write timeout at %u/%u (LSR %#x)", at, length, rd(connsys_btif, 0x14));
	}
}

static void
connsys_btif_wake(void)
{
	*(volatile uint32_t *)(connsys_btif + 0x64) &= ~1u;
	delay(150);
	*(volatile uint32_t *)(connsys_btif + 0x64) |= 1u;
	delay_for_interval(20, NSEC_PER_MSEC);
}

static void
connsys_btif_dump_state(const char *tag)
{
	uint32_t lsr, iir, ier, fakelcr, sleep_en, dma_en, tri_lvl, wak, handshake;
	uint32_t cpupcr = 0, mcu600 = 0, mcu16c = 0;
	vm_offset_t mcu = ml_io_map(MCU_PHYS, 0x1000);

	if (connsys_btif != 0) {
		lsr = rd(connsys_btif, 0x14);
		iir = rd(connsys_btif, 0x08);
		ier = rd(connsys_btif, 0x04);
		fakelcr = rd(connsys_btif, 0x0c);
		sleep_en = rd(connsys_btif, 0x48);
		dma_en = rd(connsys_btif, 0x4c);
		tri_lvl = rd(connsys_btif, 0x60);
		wak = rd(connsys_btif, 0x64);
		handshake = rd(connsys_btif, 0x6c);
	} else {
		lsr = iir = ier = fakelcr = sleep_en = dma_en = tri_lvl = wak = handshake = 0xdead;
	}
	if (connsys_hif != 0) {
		cpupcr = rd(connsys_hif, CONN_HIF_ON_CPUPCR);
	}
	if (mcu != 0) {
		mcu600 = rd(mcu, 0x600);
		mcu16c = rd(mcu, 0x16c);
	}

	redwood_keep("connsys: BTIF [%s] [%u ms]: LSR %#x (DR=%u OE=%u PE=%u FE=%u THRE=%u TEMT=%u) IIR %#x IER %#x DMA_EN %#x SLEEP %#x TRI %#x WAK %#x HSHK %#x FLCR %#x | MCU pc %#x 600=%#x 16c=%#x",
	    tag, connsys_ms(), lsr, lsr & 1, (lsr >> 1) & 1, (lsr >> 2) & 1, (lsr >> 3) & 1, (lsr >> 5) & 1, (lsr >> 6) & 1,
	    iir, ier, dma_en, sleep_en, tri_lvl, wak, handshake, fakelcr, cpupcr, mcu600, mcu16c);
}

static bool connsys_skip_foreign;
static unsigned connsys_answer_ms;

static bool
connsys_wmt(const char *what, const uint8_t *command, unsigned length, const uint8_t *expected, unsigned expected_length)
{
	char line[220];
	uint8_t answer[32];
	int got = -1;
	size_t used;
	static bool woken;
	const unsigned wait_ms = connsys_stp_full ? (connsys_answer_ms != 0 ? connsys_answer_ms : 1000) : 3000;

	used = (size_t)snprintf(line, sizeof(line), "%s: (MCU pc %#x)", what, connsys_hif != 0 ? rd(connsys_hif, CONN_HIF_ON_CPUPCR) : 0);
	static uint32_t wake = 2;
	static bool asked;
	if (!asked) {
		asked = true;
		(void)PE_parse_boot_argn("connsys_wake", &wake, sizeof(wake));
	}
	for (unsigned try = 0; try < (connsys_stp_full ? 1 : 3); try++) {
		if (try > 0) {
			connsys_btif_dump_state("before retry resend");
			if (!connsys_stp_full && !connsys_chip_id_done) {
				connsys_mcu_restart();
			}
		}
		if (wake == 2 || (wake == 1 && (!woken || try > 0))) {
			connsys_btif_wake();
			woken = true;
		}
		connsys_stp_send(command, length);
		got = connsys_stp_receive(answer, sizeof(answer), line, sizeof(line), wait_ms);
		for (unsigned more = 0; more < 4 && got == 6 && memcmp(answer, "\x02\x03\x02\x00\x00", 5) == 0 &&
		    !((unsigned)got >= expected_length && memcmp(answer, expected, expected_length) == 0); more++) {
			used = strlen(line);
			snprintf(line + used, sizeof(line) - used, " [event %02x]", answer[5]);
			got = connsys_stp_receive(answer, sizeof(answer), line, sizeof(line), wait_ms);
		}
		for (unsigned more = 0; more < 8 && connsys_skip_foreign && connsys_stp_task == 4 && got >= 5 && answer[0] == 0x02 &&
		    expected_length >= 2 && answer[1] != expected[1]; more++) {
			used = strlen(line);
			snprintf(line + used, sizeof(line) - used, " [event %02x, %d bytes]", answer[1], got);
			got = connsys_stp_receive(answer, sizeof(answer), line, sizeof(line), wait_ms);
		}
		if (got >= 0) {
			break;
		}
		used = strlen(line);
		snprintf(line + used, sizeof(line) - used, " . (pc %#x)", connsys_hif != 0 ? rd(connsys_hif, CONN_HIF_ON_CPUPCR) : 0);
	}
	used = strlen(line);
	if (got < 0) {
		snprintf(line + used, sizeof(line) - used, " no answer (LSR %#x)", rd(connsys_btif, 0x14));
	}
	for (int i = 0; i < got; i++) {
		used += (size_t)snprintf(line + used, sizeof(line) - used, " %02x", answer[i]);
	}
	if (got >= 0) {
		snprintf(line + used, sizeof(line) - used, (unsigned)got >= expected_length && memcmp(answer, expected, expected_length) == 0 ?
		    " (as expected)" : " (NOT as expected)");
	}
	redwood_keep("connsys: step %u [%u ms]: %s", connsys_doing, connsys_ms(), line);
	strlcat(connsys_said, " | ", sizeof(connsys_said));
	strlcat(connsys_said, line, sizeof(connsys_said));
	return got >= 0 && (unsigned)got >= expected_length && memcmp(answer, expected, expected_length) == 0;
}

static bool
connsys_pda_patch(void)
{
	static const uint8_t pda_done[] = { 0x02, 0x01, 0x01, 0x00, 0x00 };
	static const uint8_t reset[] = { 0x01, 0x07, 0x01, 0x00, 0x04 };
	static const uint8_t reset_done[] = { 0x02, 0x07, 0x01, 0x00, 0x00 };
	uint8_t pda[21] = {
		0x01, 0x01, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2f, 0x02, 0x02, 0x00,
	};
	uint8_t head[12] = { 0 };
	uint32_t size = connsys_patch_size, body, pieces = 0;
	uint8_t *file = connsys_patch_file;
	bool free_file = false;
	bool ok;

	connsys_doing = 9;
	if (file == NULL) {
		file = connsys_file("soc1_0_patch_mcu_1_1_hdr.bin", &size);
		free_file = true;
	}
	if (file == NULL || size <= 28 + 2) {
		redwood_keep("connsys: step 9 [%u ms]: /fw/connsys/soc1_0_patch_mcu_1_1_hdr.bin: not read", connsys_ms());
		if (file != NULL && free_file) {
			kfree_data(file, size);
		}
		return false;
	}
	body = size - 28 - 2;
	{
		uint32_t target = 0, override = 0;

		memcpy(&target, file + 24, 4);
		target &= ~0xffu;
		if (PE_parse_boot_argn("connsys_pda_addr", &override, sizeof(override)) && override != 0) {
			target = override;
		}
		memcpy(&pda[5], &target, 4);
	}
	for (unsigned i = 0; i < 4; i++) {
		pda[9 + i] = (uint8_t)(body >> (8 * i));
		head[i] = (uint8_t)((body + sizeof(head)) >> (8 * i));
	}
	redwood_keep("connsys: step 9 [%u ms]: patch %.16s (%.4s, versions %02x%02x/%02x%02x): %u bytes, %u pieces of 1000, the last %u; "
	    "to %02x %02x %02x %02x", connsys_ms(), (const char *)file, (const char *)file + 16, file[20], file[21], file[22], file[23], body,
	    (body + 999) / 1000, body - (body - 1) / 1000 * 1000, pda[5], pda[6], pda[7], pda[8]);
	if (connsys_step < 10) {
		if (free_file) {
			kfree_data(file, size);
		}
		return false;
	}
	connsys_doing = 10;
	connsys_firmware_said("before the patch");
	{
		uint32_t low = 1, before = rd(connsys_btif, 0x64);

		(void)PE_parse_boot_argn("connsys_wakelow", &low, sizeof(low));
		if (low != 0) {
			*(volatile uint32_t *)(connsys_btif + 0x64) = before & ~1u;
			delay_for_interval(5, NSEC_PER_MSEC);
		}
		redwood_keep("connsys: step 10 [%u ms]: BTIF_WAK %#x -> %#x before the patch", connsys_ms(), before,
		    rd(connsys_btif, 0x64));
	}
	ok = connsys_wmt("PDA configuration", pda, sizeof(pda), pda_done, sizeof(pda_done));
	if (ok) {
		char pcs[400];
		size_t used = 0;

		pcs[0] = 0;
		connsys_btif_write(head, sizeof(head));
		for (uint32_t at = 0; at < body; at += 1000, pieces++) {
			connsys_btif_write(file + 28 + 2 + at, body - at < 1000 ? body - at : 1000);
			used += (size_t)snprintf(pcs + used, sizeof(pcs) - used, " %x", rd(connsys_hif, CONN_HIF_ON_CPUPCR));
		}
		for (unsigned t = 0; t < 50000 && !(rd(connsys_btif, 0x14) & (1u << 6)); t++) {
			delay(10);
		}
		used += (size_t)snprintf(pcs + used, sizeof(pcs) - used, " | after:");
		for (unsigned n = 0; n < 10 && used < sizeof(pcs); n++) {
			delay_for_interval(5, NSEC_PER_MSEC);
			used += (size_t)snprintf(pcs + used, sizeof(pcs) - used, " %x", rd(connsys_hif, CONN_HIF_ON_CPUPCR));
		}
		redwood_keep("connsys: step 10 [%u ms]: %u pieces of the patch sent, LSR %#x", connsys_ms(), pieces, rd(connsys_btif, 0x14));
		redwood_keep("connsys: step 10: MCU pc per piece:%s", pcs);
		connsys_firmware_said("after the patch");
		redwood_keep("connsys: step 10: TOPRGU+0x18 %#x (CONNSYS CPU reset by the AP: %s)", rd(connsys_rgu, RGU_SWRST),
		    (rd(connsys_rgu, RGU_SWRST) & RGU_CONNSYS_SW_RST) ? "HELD" : "not held");
		connsys_build_time(file, 0x68330u);
		delay_for_interval(50, NSEC_PER_MSEC);
		connsys_btif_dump_state("before WMT reset");
		ok = connsys_wmt("WMT reset", reset, sizeof(reset), reset_done, sizeof(reset_done));
	}
	if (free_file) {
		kfree_data(file, size);
	}
	return ok;
}

static void
connsys_firmware_said(const char *when)
{
	vm_offset_t ctl = ml_io_map_unmappable(CONNSYS_EMI_PHYS + CONNSYS_EMI_COREDUMP_OFFSET, 0x4000, 0x7);
	char text[170];

	if (ctl == 0) {
		return;
	}
#define C(o) rd(ctl, (o))
	redwood_keep("connsys: %s: EMI ctl state %#x sync %#x/%#x %#x/%#x/%#x/%#x print %#x+%#x@%#x int %#x dump_end %#x dbglog %#x "
	    "pages %#x sleep %#x assert %#x", when, C(0x0), C(0x4), C(0x8), C(0xc), C(0x10), C(0x14), C(0x18), C(0x1c), C(0x20), C(0x24),
	    C(0x28), C(0x2c), C(0x40), C(0x44), C(0x4c), C(0x100));
	for (unsigned part = 0; part < 3; part++) {
		unsigned n = 0;

		for (unsigned i = 0; i < 160; i++) {
			uint8_t c = *(volatile uint8_t *)(ctl + 0x400 + part * 160 + i);

			text[n++] = c >= 0x20 && c < 0x7f ? (char)c : (c == 0 ? '_' : '.');
		}
		text[n] = 0;
		redwood_keep("connsys: %s: trace %u: %s", when, part, text);
	}
	uint32_t start = C(0x1c), length = C(0x20);
#undef C
	ml_io_unmap(ctl, 0x4000);
	if (start >= CONNSYS_EMI_FW_PHY_BASE && start < CONNSYS_EMI_FW_PHY_BASE + CONNSYS_EMI_SIZE && length != 0 && length <= 0x10000u) {
		uint32_t at = start - CONNSYS_EMI_FW_PHY_BASE, page = at & ~0x3fffu;
		vm_offset_t buf = ml_io_map_unmappable(CONNSYS_EMI_PHYS + page, 0x8000, 0x7);

		if (buf != 0) {
			for (unsigned part = 0; part < 3; part++) {
				unsigned n = 0;

				for (unsigned i = 0; i < 160 && (at - page) + part * 160 + i < 0x8000; i++) {
					uint8_t c = *(volatile uint8_t *)(buf + (at - page) + part * 160 + i);

					text[n++] = c >= 0x20 && c < 0x7f ? (char)c : (c == 0 ? '_' : '.');
				}
				text[n] = 0;
				redwood_keep("connsys: %s: print %u: %s", when, part, text);
			}
			ml_io_unmap(buf, 0x8000);
		}
	}
}

static void
connsys_firmware_load(void)
{
	static const uint8_t ram_mcu_done[] = { 0x02, 0x03, 0x01, 0x00, 0x00 };
	static const uint8_t ram_wifi_done[] = { 0x02, 0x03, 0x01, 0x00, 0x00 };
	static const uint8_t ram_bt_done[] = { 0x02, 0x03, 0x01, 0x00, 0x00 };
	uint8_t *ram_mcu = connsys_file("soc1_0_ram_mcu_1_1_hdr.bin", &connsys_patch_size);
	uint8_t *ram_wifi = connsys_file("soc1_0_ram_wifi_1_1_hdr.bin", &connsys_patch_size);
	uint8_t *ram_bt = connsys_file("soc1_0_ram_bt_1_1_hdr.bin", &connsys_patch_size);
	uint8_t *wifi_ram_code = connsys_file("WIFI_RAM_CODE_soc1_0_1_1.bin", &connsys_patch_size);
	vm_offset_t emi = ml_io_map(CONNSYS_EMI_PHYS, CONNSYS_EMI_SIZE);

	if (ram_mcu == NULL || ram_wifi == NULL || ram_bt == NULL || wifi_ram_code == NULL || emi == 0) {
		redwood_keep("connsys: step 5 [%u ms]: firmware files not read", connsys_ms());
		if (ram_mcu) kfree_data(ram_mcu, connsys_patch_size);
		if (ram_wifi) kfree_data(ram_wifi, connsys_patch_size);
		if (ram_bt) kfree_data(ram_bt, connsys_patch_size);
		if (wifi_ram_code) kfree_data(wifi_ram_code, connsys_patch_size);
		return;
	}

	/* MCU RAM code at 0x00000000 (EMI+0) */
	memcpy((void *)emi, ram_mcu + 48, connsys_patch_size - 48);
	/* MCU ROM patch at 0x001c0000 */
	memcpy((void *)(emi + 0x1c000), ram_mcu + 48 + (connsys_patch_size - 48), 0x10000);
	/* BT RAM at 0x00800000 */
	memcpy((void *)(emi + 0x80000), ram_bt + 48, connsys_patch_size - 48);
	/* Wi-Fi RAM at 0x01400000 */
	memcpy((void *)(emi + 0x140000), ram_wifi + 48, connsys_patch_size - 48);
	/* Wi-Fi RAM code at 0x01fe0000 */
	memcpy((void *)(emi + 0x1fe000), wifi_ram_code, connsys_patch_size);
	__asm__ volatile ("dsb sy" ::: "memory");

	/* EMI remapping: INFRA+0x380 = EMI base, INFRA+0x388 = peripheral window */
	vm_offset_t infra = ml_io_map(INFRACFG_PHYS, 0x1000);
	*(volatile uint32_t *)(infra + 0x380) = CONNSYS_EMI_PHYS >> 24;
	*(volatile uint32_t *)(infra + 0x388) = 0x00200000;

	/* Let CPU go: RGU+0x18 clear bit 12 */
	*(volatile uint32_t *)(connsys_rgu + RGU_SWRST) = (rd(connsys_rgu, RGU_SWRST) & ~RGU_CONNSYS_SW_RST) | RGU_KEY;
	delay_for_interval(50, NSEC_PER_MSEC);

	/* Poll MCU+0x600 for 0x1D1E */
	for (unsigned n = 0; n < 500 && rd(connsys_mcu, 0x600) != 0x1D1E; n++) {
		delay_for_interval(20, NSEC_PER_MSEC);
	}
	redwood_keep("connsys: step 5 [%u ms]: firmware loaded, MCU 0x600=%#x", connsys_ms(), rd(connsys_mcu, 0x600));

	if (ram_mcu) kfree_data(ram_mcu, connsys_patch_size);
	if (ram_wifi) kfree_data(ram_wifi, connsys_patch_size);
	if (ram_bt) kfree_data(ram_bt, connsys_patch_size);
	if (wifi_ram_code) kfree_data(wifi_ram_code, connsys_patch_size);
}

static bool
connsys_chip_id(void)
{
	static const uint8_t chip_id_cmd[] = { 0x01, 0x04, 0x01, 0x00, 0x08, 0x00, 0x00, 0x00, 0x08 };
	static const uint8_t chip_id_done[] = { 0x02, 0x04, 0x01, 0x00, 0x00 };

	connsys_doing = 7;
	connsys_chip_id_done = connsys_wmt("chip ID", chip_id_cmd, sizeof(chip_id_cmd), chip_id_done, sizeof(chip_id_done));
	return connsys_chip_id_done;
}

static void
connsys_sw_init(void)
{
	static const uint8_t init_table_1[] = { 0x01, 0x01, 0x01, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00 };
	static const uint8_t init_table_1_done[] = { 0x02, 0x01, 0x01, 0x00, 0x00 };
	static const uint8_t init_table_2[] = { 0x01, 0x02, 0x01, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00 };
	static const uint8_t init_table_2_done[] = { 0x02, 0x02, 0x01, 0x00, 0x00 };
	static const uint8_t init_table_3[] = { 0x01, 0x03, 0x01, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00 };
	static const uint8_t init_table_3_done[] = { 0x02, 0x03, 0x01, 0x00, 0x00 };
	static const uint8_t init_table_4[] = { 0x01, 0x04, 0x01, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00 };
	static const uint8_t init_table_4_done[] = { 0x02, 0x04, 0x01, 0x00, 0x00 };
	static const uint8_t init_table_5[] = { 0x01, 0x05, 0x01, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00 };
	static const uint8_t init_table_5_done[] = { 0x02, 0x05, 0x01, 0x00, 0x00 };

	connsys_doing = 11;
	connsys_wmt("init table 1", init_table_1, sizeof(init_table_1), init_table_1_done, sizeof(init_table_1_done));
	connsys_wmt("init table 2", init_table_2, sizeof(init_table_2), init_table_2_done, sizeof(init_table_2_done));
	connsys_wmt("init table 3", init_table_3, sizeof(init_table_3), init_table_3_done, sizeof(init_table_3_done));
	connsys_wmt("init table 4", init_table_4, sizeof(init_table_4), init_table_4_done, sizeof(init_table_4_done));
	connsys_stp_full = true;
	connsys_stp_seq = 0;
	connsys_stp_ack = 7;
	connsys_stp_expect = 0;
	connsys_wmt("init table 5 (full mode)", init_table_5, sizeof(init_table_5), init_table_5_done, sizeof(init_table_5_done));
	redwood_keep("connsys: step 11 [%u ms]: connsys_sw_init done, STP full mode", connsys_ms());
}

static void
connsys_step_thread(__unused void *parameter, __unused wait_result_t result)
{
	vm_offset_t spm = ml_io_map(SPM_PHYS, 0x1000);

	connsys_rgu = ml_io_map(RGU_PHYS, 0x1000);
	connsys_mcu = ml_io_map(MCU_PHYS, 0x1000);
	connsys_hif = ml_io_map(CONN_HIF_ON_PHYS, 0x1000);
	connsys_wbg = ml_io_map(WBG_PHYS, 0x1000);

	(void)PE_parse_boot_argn("connsys_step", &connsys_step, sizeof(connsys_step));
	if (connsys_step < 1) {
		redwood_keep("connsys: step 0: no step requested");
		return;
	}

	redwood_keep("connsys: step 1 [%u ms]: IP version %#x, configuration %#x, hardware %#x, firmware %#x",
	    connsys_ms(), rd(ml_io_map(MCU_TOP_MISC_OFF_PHYS, 0x1000), CONSYS_IP_VER),
	    rd(ml_io_map(MCU_TOP_MISC_OFF_PHYS, 0x1000), CONSYS_CONF_ID),
	    rd(ml_io_map(MCU_PHYS, 0x1000), CONSYS_HW_ID),
	    rd(ml_io_map(MCU_PHYS, 0x1000), CONSYS_FW_ID));

	if (connsys_step >= 2) {
		connsys_power_report();
	}
	if (connsys_step >= 3) {
		connsys_supplies_on();
	}
	if (connsys_step >= 4) {
		connsys_power_on(spm);
	}
	if (connsys_step >= 5) {
		connsys_firmware_load();
	}
	if (connsys_step >= 6) {
		connsys_btif_init();
	}
	if (connsys_step >= 7) {
		connsys_chip_id();
	}
	if (connsys_step >= 8) {
		connsys_stp_full = true;
		connsys_stp_seq = 0;
		connsys_stp_ack = 7;
		connsys_stp_expect = 0;
	}
	if (connsys_step >= 9) {
		connsys_pda_patch();
	}
	if (connsys_step >= 10) {
		/* step 10 is part of step 9 */
	}
	if (connsys_step >= 11) {
		connsys_sw_init();
	}
}

void
hakios_mtk_connsys_init(void)
{
	thread_t thread;
	uint32_t step = 0;

	if (!PE_parse_boot_argn("connsys_step", &step, sizeof(step)) || step == 0) {
		return;
	}
	if (kernel_thread_start_priority(connsys_step_thread, NULL, BASEPRI_DEFAULT, &thread) == KERN_SUCCESS) {
		thread_deallocate(thread);
	} else {
		printf("connsys: cannot start the thread\n");
	}
}