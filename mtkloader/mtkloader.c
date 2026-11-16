/*
 * mtkloader: boots XNU on the General Mobile GM 21 Plus (G510, MT6765) from boot_a, and brings Android
 * back afterwards with XNU's log in pstore.
 *
 * The boot image's kernel (tools/chainboot.py) is this program, then Android's own kernel Image
 * at CHAIN_OFFSET, then the XNU payload ("XNUP": kernel, ramdisk). Each boot, by the marker:
 *
 *   none     RUN: load XNU, give it LK's framebuffer, arm the watchdog, start it.
 *   RUNNING  XNU was cut short by the watchdog: copy its log from DRAM into ramoops, start
 *            Android.
 *   DONE     XNU came back to us (panic, early exception, orderly end) and its log was saved
 *            then: start Android.
 *
 * The log ends up in /sys/fs/pstore/console-ramoops-0 under Android. See mtkdbg.h for the
 * screen and ramoops output.
 *
 * Display: the panel is video mode and keeps scanning OVL0 layer 0 (LK's framebuffer,
 * 0x7e250000, pitch 2944 = 736 pixels). LK enables per-pixel alpha on that layer, and XNU's
 * console writes 0x00rrggbb pixels: fully transparent. So per-pixel alpha is switched off, and
 * XNU's color table gets alpha 0xff besides.
 */
#include <stdint.h>
#include <stddef.h>
#include "mtkdbg.h"
#include "msdc.h"
#include "mtkspi.h"
#include "tcm.h"
#include "../xnuloader/identity.h"

/* XNU's view of memory: one contiguous range that nothing else (LK, Android, TEE, ATF, the
 * framebuffer) uses; 0xc0000000-0xfe000000 is free DRAM on this board. */
#define XNU_PHYS_BASE       0xc0000000ULL
#define XNU_MEM_SIZE        0xfe000000ULL       /* up to dramc-rk1 at 0x1bffff000; room for the system image */
/* Where the system image is put, clear of the kernel, as xnuloader does it (RESIDENT_RD_OFFSET). */
#define SYSTEM_PA           (XNU_PHYS_BASE + 0x08000000ULL)
#define SYSTEM_PART         "xnusys"
/* 0: all of it. Smaller is for trying the path out: packfs will not mount a half image. */
#ifndef SYSTEM_MAX_MB
#define SYSTEM_MAX_MB       0
#endif
/* boot_args.virtBase, as xnuloader/loader.c has it: the kernel's text is 48 MB above it. */
#define KERNEL_VIRT_BASE    0xfffffff004000000ULL
#define XNU_SLIDE           (XNU_PHYS_BASE - KERNEL_VIRT_BASE)
#define PAGE_SIZE_MAX       0x4000ULL

/* Seconds XNU may run before the watchdog resets into Android; 0: run until reset by hand. */
#ifndef XNU_WDT_SECONDS
#define XNU_WDT_SECONDS     0
#endif
/* Seconds after which the second core takes a snapshot of XNU and resets; 0: no watcher. */
#ifndef XNU_WATCH_SECONDS
#define XNU_WATCH_SECONDS   0
#endif
/*
 * How many processors the device tree offers the kernel. It takes them from the head of
 * core_order below, so what is left of that list is nobody's but ours -- the watcher takes the
 * next one. Leaving all eight in the tree and trusting "cpus=N" to stop the kernel sooner is a
 * guess about its parsing; this is not. XNU_EXTRA_ARGS should carry the same number as "cpus=".
 *
 * A watcher on a core the kernel wants is worse than none: PSCI CPU_ON answers "already on", the
 * kernel counts a processor that never checks in, and every rendezvous between cores waits for
 * it -- which is how a 15 minute run stayed on the Apple logo.
 */
#ifndef XNU_CORES
#define XNU_CORES           7
#endif
_Static_assert(XNU_CORES >= 1 && XNU_CORES <= 7, "one core at least, and one left for the watcher");

/*
 * The processors in the order the kernel is offered them: the one this code runs on first (LK
 * leaves us on core 0), then the three other Cortex-A53s at ~2.3GHz, then the three other Cortex-A53s
 * at ~1.8GHz. The 8th core is reserved for the watcher. So "cpus=7" uses 7 cores for XNU.
 */
static const uint8_t core_order[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
#define WATCH_CORE          core_order[XNU_CORES]
/* Checkpoint for XNU to return at (osfmk/arm/arm_init.c LOADER_CHECKPOINT), 0 = none. */
#ifndef XNU_CHECKPOINT
#define XNU_CHECKPOINT      0
#endif
/* loader_return_after: XNU comes back by itself after that long, with its log (loader_return.c) */
#ifndef XNU_RETURN_AFTER
#define XNU_RETURN_AFTER    "0"
#endif
/* 1: always start XNU (tools/xnu-cycle.sh); 0: Android unless asked once (tools/boot-xnu.sh). */
/* 1: look at the touch screen's SPI and report, then go back to Android (no XNU). */
#ifndef TOUCH_PROBE
#define TOUCH_PROBE         0
#endif
/* 1: wake the GPU and ask it what it is, if Android asked for it (tools/gpu-probe.sh); then Android. */
#ifndef GPU_PROBE
#define GPU_PROBE           0
#endif
/* 1: wake the PowerVR GPU and ask it what it is (tools/pvr-probe.sh); then Android. */
#ifndef PVR_PROBE
#define PVR_PROBE           0
#endif
/* 1: read the camera's pins, rails and I2C controller and report, if Android asked for it (tools/cam-probe.sh); then Android. */
#ifndef CAM_PROBE
#define CAM_PROBE           0
#endif
/* 1: read the eMMC and report, then go back to Android (no XNU). */
#ifndef MSDC_TEST
#define MSDC_TEST           0
#endif
/* 1: check at EL1 that the timer interrupts arrive at all (diagnostics, leaves GIC state). */
#ifndef TIMER_PROBE
#define TIMER_PROBE         0
#endif
#ifndef MENU_DEFAULT_XNU
#define MENU_DEFAULT_XNU    0
#endif
/*
 * 1: the kernel's text console on the screen (-v). It is a slow way to run -- scrolling reads
 * the whole framebuffer back -- and iOS cannot draw while it is on, so a real boot goes without
 * it and the log comes back through the loader instead (v_display = 1 says "graphics mode").
 */
#ifndef XNU_VERBOSE
#define XNU_VERBOSE         0
#endif
/* More for the kernel's command line: cores, the clock (epoch=), whatever a run wants to try. */
/* The serial number iOS is told (the device tree's serial-number): a placeholder, not any real device's. */
#ifndef XNU_SERIAL
#define XNU_SERIAL          "HAKIOS0000000000"
#endif
#ifndef XNU_EXTRA_ARGS
#define XNU_EXTRA_ARGS      "cpus=7 touchscreen=1 buttons=2 mtk_cpufreq=3 emmc_root=1"
#endif
/*
 * 1: the system image stays on the eMMC and the kernel reads it as it needs it (emmc_root=1,
 * bsd/dev/redwood_emmc.c), the default; 0: it is read into DRAM here as md0 -- 2.4 GB of the kernel's 4 GB,
 * wired from the start, and three and a half minutes of every boot (run 320).
 */
#ifndef XNU_ROOT_EMMC
#define XNU_ROOT_EMMC       1
#endif
#if XNU_ROOT_EMMC
#define XNU_ROOT_ARG        "rd=md0 emmc_root=1"     /* (rd=md0 for libignition's "ramdisk" boot) */
#else
#define XNU_ROOT_ARG        "rd=md0"
#endif
#if XNU_VERBOSE
#define XNU_BOOT_ARGS       "debug=0x8 serial=3 " XNU_ROOT_ARG " -v " XNU_EXTRA_ARGS " loader_return_after=" XNU_RETURN_AFTER
#else
#define XNU_BOOT_ARGS       "debug=0x8 serial=3 " XNU_ROOT_ARG " " XNU_EXTRA_ARGS " loader_return_after=" XNU_RETURN_AFTER
#endif

#define CHAIN_OFFSET        0x40000u
#define KERNEL_PA           0x40080000ULL
#define CHAIN_STUB_PA       0x4a000000ULL

/* Run state, kept in DRAM across the watchdog reset (three copies: a bit may flip). */
#define MARKER_PA           0x80000000ULL
#define MARK_RUNNING        0x4e52584eu /* "NXRN" */
#define MARK_DONE           0x4e44584eu /* "NXDN" */

#define MH_MAGIC_64         0xfeedfacfu
#define LC_SEGMENT_64       0x19
#define LC_SYMTAB           0x2
#define LC_UNIXTHREAD       0x5
#define PAYLOAD_MAGIC       0x50554e58u /* "XNUP" */

struct boot_video {
	uint64_t v_baseAddr, v_display, v_rowBytes, v_width, v_height, v_depth;
};

#define BOOT_LINE_LENGTH 608

/* pexpert/pexpert/arm64/boot.h: virtBase comes first (start.s reads BA_VIRT_BASE, BA_PHYS_BASE). */
struct boot_args {
	uint16_t revision;
	uint16_t version;
	uint64_t virtBase;
	uint64_t physBase;
	uint64_t memSize;
	uint64_t topOfKernelData;
	struct boot_video video;
	uint32_t machineType;
	uint64_t deviceTreeP;
	uint32_t deviceTreeLength;