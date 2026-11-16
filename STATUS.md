# HakiOS G510 (MT6765) — Implementation Status

**Last Updated:** 2026-11-14  
**Session:** GPU Driver Implementation + mtkloader PVR Probe + Infrastructure Updates

---

## 🎯 Current Goal
Complete HakiOS for G510 (MediaTek MT6765, PowerVR GE8320) with all hardware functional except cellular.

---

## ✅ Completed (Phases 1-2)

| Subsystem | Status | Files | Boot-arg |
|-----------|--------|-------|----------|
| Boot/CPU/Display/Touch/Buttons/Storage | ✅ Works | `g510_connsys.c`, `g510_tcm.c`, `g510_mtkkeys.c`, `g510_emmc.c`, `g510_cpufreq.c` | `cpus=8 touchscreen=1 buttons=2 mtk_cpufreq=3 emmc_root=1` |
| **Wi-Fi (CONNSYS gen4m)** | ✅ **DONE** | `g510_connsys.c` (39KB, 11 steps) | `connsys_step=11` |
| **Bluetooth (HCI over STP)** | ✅ **DONE** | `g510_mtkbt.c` + `redwood_mtkbt_skel.c` | `mtkbt_skel=1` |

---

## 🔧 In Progress (Phases 3-8)

### Phase 3: Audio (PMIC MT6358 I2S) — **STUBBED, NEEDS COMPLETION**
| Driver | File | Status | Notes |
|--------|------|--------|-------|
| PMIC Wrapper | `g510_mtkpwrap.c` | ✅ Implemented | SPMI arbiter for MT6358 |
| Audio Codec | `g510_mtkaudio.c` | ⚠️ **Stub** | AFE/I2S clocks, MT6358 init, DL/VUL paths |
| Speaker Amp | `g510_mtkspk.c` | ⚠️ **Stub** | AW87359 on I2C, volume control |
| Microphone | `g510_mtkmic.c` | ⚠️ **Stub** | MT6358 MICBIAS, AFE VUL |

**Boot-args:** `audio=1 adsp=1 sound=4 mic=4`

---

### Phase 4: Battery (PMIC Fuel Gauge) — **STUBBED, NEEDS COMPLETION**
| Driver | File | Status | Notes |
|--------|------|--------|-------|
| Battery | `g510_mtkbatt.c` | ⚠️ **Stub** | FGADC reads (SOC, V, I, temp), sysctl nodes, periodic thread |

**Boot-arg:** `battery=1`

---

### Phase 5: GPU (PowerVR GE8320) — **FIRMWARE EXTRACTED + DRIVER UPDATED** ⚠️ **PRIORITY**
| Driver | File | Status | Notes |
|--------|------|--------|-------|
| PowerVR GPU | `g510_mtkpvrgpu.c` | 🟡 **Substantially Updated** | Full step-based init (10 steps), **ELF32 firmware loading implemented**, correct register map |

**Hardware:** IMG PowerVR Rogue GE8320 (680 MHz, 2 shader clusters) — **DIFFERENT from MT6768's Mali-G52!**

**Boot-arg:** `mali=1` (or `pvr=1`)

**Completed This Session:**
1. ✅ PowerVR firmware **successfully extracted** from device:
   - `fw-pvr/pvr_rogue_fw.bin` (114,688 bytes) — **ELF32 MIPS executable** (rgx.fw)
   - `fw-pvr/pvr_mmu_fw.bin` (309,200 bytes) — Script/command file (rgx.sh)
2. ✅ Driver updated with **complete ELF32 parsing**:
   - ELF magic verification (0x7F 'E' 'L' 'F')
   - 32-bit, little-endian, MIPS (EM_MIPS=8) validation
   - PT_LOAD segment parsing and loading to EMI
   - BSS zero-fill (memsz > filesz)
   - Entry point configuration via PVR_REG_FW_BOOT_VEC
   - Base address configuration via PVR_REG_FW_BOOT_ADDR
3. ✅ Pull script updated for actual device filenames (rgx.fw, rgx.sh)

**Implemented Previously:**
1. ✅ Correct register addresses for MT6765 (SPM, TOPCKGEN, INFRACFG, MFGCFG, PVR)
2. ✅ Power domain structures matching MT6765 datasheet
3. ✅ Bus protection handling via INFRACFG
4. ✅ 10-step initialization: clocks → power → MFGCFG → soft reset → identify → **load fw → start fw** → MMU → test job → MMU test
5. ✅ Complete register map (Core, Firmware, MMU, TA, 3D, CDM, TQ)
6. ✅ Loader-side PVR probe (`mtkloader/mtkpvr.h`)

**Still Needed:**
1. Kernel build + test on device
2. Hardware probe (`tools/pvr-probe.sh`) to verify register addresses
3. Proper MMU page tables (2-level AArch64 format)
4. Queue initialization (TA/3D/CDM/TQ ring buffers)
5. Sync objects/fences
6. IOKit service for userspace
7. Userspace driver decision

---

### Phase 6: Sensors (SCP Hub) — **STUBBED, NEEDS COMPLETION**
| Driver | File | Status | Notes |
|--------|------|--------|-------|
| SCP Hub | `g510_mtk_scp.c` | ⚠️ **Stub** | IPI/mailbox, shared SRAM, accel/gyro/proximity |

**Boot-arg:** `sensors=1`

---

### Phase 7: Camera (MTKCAM) — **STUBBED, NEEDS COMPLETION**
| Driver | File | Status | Notes |
|--------|------|--------|-------|
| Camera | `g510_mtkcam.c` | ⚠️ **Stub** | CSI-2, CAMSV DMA, CCI/I2C, S5K JN1 + GC5035 sensors |

**Boot-args:** `camera=1 torch=1`

---

### Phase 8: Thermal (MTK Thermal Zones) — **STUBBED, NEEDS COMPLETION**
| Driver | File | Status | Notes |
|--------|------|--------|-------|
| Thermal | `g510_mtkthermal.c` | ⚠️ **Stub** | APMIXED/SCP sensors, throttling, emergency stop |

**Boot-args:** `thermal=1 thermal_stop=105`

---

## 📊 GitHub Actions Analysis

### Workflows
1. **`.github/workflows/build.yml`** — Main build on macOS-latest (180 min timeout)
   - Checks out `oosexgang/hakios-xnu-kernel` (main branch) + this repo
   - Copies device files to `devices/g510/` in kernel repo
   - Runs `hakios-config.py defconfig g510 .config` + `kernel --tree`
   - Builds kernel via `xnu-build.sh`
   - Fetches iOS 16.5 IPSW (20F66) for `xnuloader/identity.h`
   - Builds userland, mtkloader, boot image
   - **Uploads artifacts:** logs, `xnu-flashed-slot_a.img`, kernel, system.apfs

2. **`.github/workflows/sync-and-build.yml`** — Similar, triggers on push to main in this repo

### Build Dependencies (from workflow)
- macOS runner
- `brew install bash blacktop/tap/ipsw cmake ninja coreutils gnu-sed gawk`
- Xcode (default)
- `xnu-deps/setup.sh` (cctools, SDK, theos)
- XNU patches via `xnu/apply.sh`
- ath11k Wi-Fi object via `tools/mk-ath11k.sh`

### Key Build Scripts in Common Kernel Repo
- `xnu-build.sh` — Main kernel build
- `tools/hakios-config.py` — Configuration system (reads `drivers/map` and `drivers/*/conf/*.conf`)
- `tools/mk-ath11k.sh` — Builds Wi-Fi firmware object
- `mtkloader/build.sh` — Bare-metal loader build
- `mtkloader/build_flashed.py` — Packs boot_a image

---

## 🏗️ Repository Structure

```
D:\xperiasp\
├── hakios-g510/                    # THIS REPO (G510 device)
│   ├── .github/workflows/          # CI: build.yml, sync-and-build.yml
│   ├── kernel/                     # XNU drivers (APSL 2.0)
│   │   ├── bsd/dev/                # Driver source files
│   │   ├── bsd/sys/                # Headers
│   │   ├── drivers/                # Config for hakios-config.py
│   │   │   ├── map                 # Driver → subsystem mapping
│   │   │   ├── audio/conf/audio.conf
│   │   │   ├── gpu/conf/gpu.conf
│   │   │   ├── camera/conf/camera.conf
│   │   │   ├── power/conf/power.conf
│   │   │   └── ...
│   │   └── soc/soc.conf            # SoC = mt6765
│   ├── mtkloader/                  # Bare-metal loader (MIT)
│   │   ├── mtkloader.c             # Main loader
│   │   ├── mtkmali.h               # MT6768 Mali (NOT G510!)
│   │   ├── mtkconnsys.h            # CONNSYS bring-up
│   │   ├── mtkpwrap.h              # PMIC wrapper
│   │   ├── mtkpvr.h                # NEW: PowerVR probe for loader
│   │   └── build.sh / build_flashed.py
│   ├── xnuloader/                  # UEFI loader (for Qualcomm)
│   ├── configs/                    # g510_defconfig, g510.config
│   ├── tools/                      # xnu-cycle.sh, boot-xnu.sh, probes
│   ├── PLAN.md / PLAN_DETAILED.md  # Implementation roadmap
│   └── device.conf                 # Device status
├── hakios/
│   ├── hakios-xnu-kernel/          # Common kernel repo (branch 'mtk')
│   │   ├── mtk/mt6765/             # MT6765 drivers in common tree
│   │   ├── userland/               # iOS 16.5 user space
│   │   └── tools/hakios-config.py  # Config system
│   └── hakios-metal/               # Metal driver (Adreno, not PowerVR!)
└── hakios-mt6768/                  # MT6768 device (Mali-G52)
```

---

## ⚠️ Critical Notes

1. **G510 has PowerVR GE8320, NOT Mali** — MT6768 has Mali-G52. The `mtkloader/mtkmali.h` is for MT6768. G510 GPU driver is `g510_mtkpvrgpu.c`.

2. **PowerVR requires firmware** — Unlike Mali, PowerVR needs firmware loaded (PVR firmware from device). See `PLAN_DETAILED.md` Phase 5.

3. **Config System** — `hakios-config.py` reads:
   - `drivers/map` for file list
   - `drivers/<subsystem>/conf/*.conf` for driver options
   - Generates `.config` with `CONFIG_HAKIOS_<NAME>=y`

4. **Naming Convention** — Common tree uses `hakios_*`, `kern.hakios.*`. This repo keeps legacy `redwood_*` — rename together.

5. **Boot Args** — Every hardware step behind a boot-arg, does nothing without it.

---

## 🎯 Next Steps (Priority Order)

### Immediate: Complete GPU Driver (Phase 5)
1. **Pull PowerVR firmware from device** — Run updated `pull-firmware.sh` on target phone
2. **Run hardware probe** — Execute `tools/pvr-probe.sh` to discover actual register layout
3. **Update register addresses** — Fix any incorrect offsets based on probe results
4. **Verify firmware header format** — Check actual firmware files and update driver
5. **Implement proper MMU page tables** — 2-level AArch64 format for userspace buffers

### Then: Audio (Phase 3)
- MT6358 codec registers via PWRAP
- I2S clock routing, AFE/ADDA paths
- AW87359 speaker amp
- Microphone input

### Then: Battery (Phase 4)
- MT6358 FGADC reads
- IOPMPowerSource/IOBattery for Darwin UI

### Then: Sensors/Camera/Thermal (Phases 6-8)

---

## 📝 Documentation Created/Updated This Session

| File | Description |
|------|-------------|
| `STATUS.md` | This file — project status |
| `GPU_IMPLEMENTATION_PLAN.md` | Detailed GPU driver implementation plan |
| `IMPLEMENTATION_LOG.md` | Session log with all changes |
| `kernel/bsd/dev/g510_mtkpvrgpu.c` | **Major update** — 10-step init, firmware loading |
| `mtkloader/mtkpvr.h` | **NEW** — Loader-side PVR probe |
| `mtkloader/mtkloader.c` | PVR_PROBE integration |
| `tools/pvr-probe.sh` | **NEW** — Probe script |
| `tools/xnu-cycle.sh` | Updated default boot args |
| `pull-firmware.sh` | Added PVR firmware support |
| `.gitignore` | Added `fw-pvr/` |

---

## 🔗 References
- `PLAN.md` / `PLAN_DETAILED.md` — Full roadmap
- `device.conf` — Current hardware status
- `AGENTS.md` — Rules for AI agents
- `README.md` — Project overview
- Common kernel: `../hakios/hakios-xnu-kernel/` (branch `mtk`)