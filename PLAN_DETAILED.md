# HakiOS G510 (MT6765) — Detailed Implementation Plan

## 🎯 Goal
A General Mobile GM 21 Plus (G510) running iOS 16.5 on XNU with Wi-Fi, Bluetooth (pairing + audio), audio, battery reporting, GPU acceleration, sensors, camera, and thermal management — all hardware features functional except cellular.

## 🧨 Now
- Boot, display, touch, buttons, CPU (8 cores clocked), eMMC root: **WORK**
- Wi-Fi: CONNSYS partially up (CONNSYS step 8, WMT channel over BTIF/STP) — **no network yet**
- Bluetooth: bluetoothd powered on, BLE scan reaches apps, Settings lists devices — **no confirmed pairing, profiles, or audio**
- GPU: PowerVR GE8320 — **fb-only, no acceleration**
- Audio, battery, haptics, camera, torch, cellular, thermal, sensors: **MISSING**

## 🛠️ Steps

### Phase 1: Wi-Fi (CONNSYS gen4m) — COMPLETED ✅
- **Files**: `kernel/bsd/dev/g510_connsys.c` (39KB, 11 steps)
- **Boot-arg**: `connsys_step=11`
- **Verify**: Settings Wi-Fi shows networks, joins WPA2, link up, traffic passes

### Phase 2: Bluetooth (HCI over STP, pairing + audio) — COMPLETED ✅
- **Files**: `kernel/bsd/dev/g510_mtkbt.c` (15KB), `g510_mtkbt.h`
- **Boot-arg**: `mtkbt_skel=1`
- **Verify**: Confirmed pairing, A2DP/HFP/HID profiles, audio routes to/from Bluetooth device

### Phase 3: Audio (PMIC MT6358 I2S) — TODO
**Hardware**: MT6358 PMIC (per oppo6765_defconfig: MT6357/MT6358)
- Audio codec registers via PMIC wrapper (SPMI)
- I2S clock routing, AFE/ADDA/DL/UL paths
- Speaker amplifiers (AW87359 per oppo6765_defconfig)
- Microphone input

**Files to create**:
- `kernel/bsd/dev/g510_mtkpwrap.c` — PMIC wrapper (shared with Wi-Fi/BT/keys)
- `kernel/bsd/dev/g510_mtkaudio.c` — Audio codec driver (MT6358)
- `kernel/bsd/dev/g510_mtkspk.c` — Speaker amplifier (AW87359)
- `kernel/bsd/dev/g510_mtkmic.c` — Microphone driver
- `kernel/bsd/sys/g510_mtkaudio.h` — Header

**Drivers to enable in g510.config**:
```
CONFIG_HAKIOS_MTK_PWRAP=y
CONFIG_HAKIOS_MTK_AUDIO=y
CONFIG_HAKIOS_MTK_SPK=y
CONFIG_HAKIOS_MTK_MIC=y
```

**Boot-args**: `audio=1 adsp=1 sound=4 mic=4`

**Verify**: System sounds, music playback, microphone input, volume keys work

### Phase 4: Battery (PMIC fuel gauge) — TODO
**Hardware**: MT6358 fuel gauge (MT6357/MT6358 PMIC)
- Read SOC, voltage, current, temp, health via PMIC wrapper
- Publish via `IOPowerSource`/`IOBattery` (Darwin's battery UI)

**Files to create**:
- `kernel/bsd/dev/g510_mtkbatt.c` — Battery driver (expand from stub)
- `kernel/bsd/sys/g510_mtkbatt.h` — Header

**Drivers to enable in g510.config**:
```
CONFIG_HAKIOS_MTK_BATT=y
```

**Boot-args**: `battery=1`

**Verify**: Settings → Battery shows percentage, charging status, time remaining

### Phase 5: GPU (PowerVR GE8320) — TODO
**Hardware**: IMG PowerVR Rogue GE8320 (680 MHz, 2 shader clusters)
- Different from MT6768's Mali-G52!
- Need: Firmware load, MMU, job submission, sync objects
- Userspace: Mesa panfrost or direct PowerVR userspace

**Files to create**:
- `kernel/bsd/dev/g510_mtkpvrgpu.c` — PowerVR GPU driver
- `kernel/bsd/sys/g510_mtkpvrgpu.h` — Header
- Userspace: PowerVR firmware from device

**Drivers to enable in g510.config**:
```
CONFIG_HAKIOS_MTK_PVRGPU=y
```

**Boot-args**: `mali=1` (or `pvr=1` for PowerVR)

**Verify**: 60 fps UI, OpenGL ES apps run, Metal driver loads (Metal-on-Vulkan or direct)

### Phase 6: Sensors (SCP hub) — TODO
**Hardware**: SCP sensor hub (accelerometer, gyro, proximity)
- SCP communication via IPI/mailbox
- Accelerometer, gyro, proximity via SCP
- Publish via IOKit `IOHIDDevice`

**Files to create/expand**:
- `kernel/bsd/dev/g510_mtk_scp.c` — SCP hub driver (expand from stub)
- `kernel/bsd/sys/g510_mtk_scp.h` — Header

**Drivers to enable in g510.config**:
```
CONFIG_HAKIOS_MTK_SCP=y
```

**Boot-args**: `sensors=1`

**Verify**: Auto-rotate, shake-to-undo, proximity during calls

### Phase 7: Camera (MTKCAM) — TODO
**Hardware**: S5K JN1 main camera, GC5035 front (per oppo6765_defconfig)
- MTKCAM ISP/AAA, CSI-2 MIPI
- V4L2-style buffer queue, CCI/I2C sensor control
- CAMSV_1 DMA for capture

**Files to create/expand**:
- `kernel/bsd/dev/g510_mtkcam.c` — Camera driver (expand from stub)
- `kernel/bsd/sys/g510_mtkcam.h` — Header
- Sensor tables (from device firmware)

**Drivers to enable in g510.config**:
```
CONFIG_HAKIOS_MTK_CAM=y
CONFIG_HAKIOS_MTK_TORCH=y
```

**Boot-args**: `camera=1 torch=1`

**Verify**: Camera app opens, preview, capture, video record

### Phase 8: Thermal (MTK thermal zones) — TODO
**Hardware**: MT6765 thermal sensors (APMIXED/SCP)
- Read thermal sensors
- Implement `IOPlatformThermalProfile`
- CPU/GPU throttling under load

**Files to create**:
- `kernel/bsd/dev/g510_mtkthermal.c` — Thermal driver
- `kernel/bsd/sys/g510_mtkthermal.h` — Header

**Drivers to enable in g510.config**:
```
CONFIG_HAKIOS_MTK_THERMAL=y
```

**Boot-args**: `thermal=1 thermal_stop=105`

**Verify**: Thermal zones in ioreg, throttling under load, no overheat shutdown

## 🔄 Full Verification
**Boot-args**: `XNU_ARGS="cpus=8 touchscreen=1 buttons=2 mtk_cpufreq=3 emmc_root=1 connsys_step=11 bluetooth=2 audio=1 battery=1 mali=1 sensors=1 camera=1 thermal=1"`
- Log shows all subsystems up
- SpringBoard at 60 fps
- Wi-Fi connected
- Bluetooth paired + audio
- Battery % shown
- Camera works
- Thermal stable

## 🚫 Not now
Cellular (modem bring-up, QMI/RIL, CarrierBundle) · Torch (LED driver) · Haptics (PMIC vibrator) · DRM/FairPlay (needs Apple keys) · iCloud/App Store (Apple services) · 32-bit app support (XNU is 64-bit only)
