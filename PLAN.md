# HakiOS G510 (MT6765) — Near-Fully-Working iOS Implementation Plan

## 🎯 Goal
A General Mobile GM 21 Plus (G510) running iOS 16.5 on XNU with Wi-Fi, Bluetooth (pairing + audio), audio, battery reporting, GPU acceleration, sensors, camera, and thermal management — all hardware features functional except cellular.

## 🧨 Now
- Boot, display, touch, buttons, CPU (8 cores clocked), eMMC root: **WORK**
- Wi-Fi: CONNSYS partially up (CONNSYS step 8, WMT channel over BTIF/STP) — **no network yet**
- Bluetooth: bluetoothd powered on, BLE scan reaches apps, Settings lists devices — **no confirmed pairing, profiles, or audio**
- GPU: PowerVR GE8320 — **fb-only, no acceleration**
- Audio, battery, haptics, camera, torch, cellular, thermal, sensors: **MISSING**

## 🧠 Idea
Complete each hardware subsystem in dependency order using the established HakiOS pattern: loader brings up firmware/regulators → kernel driver implements device-tree-driven init → iOS userspace daemons (wifid, bluetoothd, etc.) work unmodified. Each phase adds one boot-arg and one kernel driver file, verified by log output and iOS UI.

## 🛠️ Steps

- 1️⃣ **Phase 1: Wi-Fi (CONNSYS gen4m)** → Implement CONNSYS power-on sequence, firmware load (ROM patch + RAM), WMT/STP/BTIF channels, gen4m station ops (scan/join/keys to firmware), netif attach; boot-arg `connsys_step=11` → verify: Settings Wi-Fi shows networks, joins WPA2, link up, traffic passes
- 2️⃣ **Phase 2: Bluetooth (HCI over STP, pairing + audio)** → Implement STP full mode, HCI channel 0, BT function-on, WMT chip-id, connsys_sw_init (PDA patch), kernel control `com.apple.uart.sk.bluetooth` with answers bluetoothd expects; boot-arg `bluetooth=2` → verify: confirmed pairing, A2DP/HFP/HID profiles, audio routes to/from Bluetooth device
- 3️⃣ **Phase 3: Audio (PMIC MT6358 I2S)** → Implement MT6358 codec registers via PMIC wrapper, I2S clock routing, AFE/ADDA/DL/UL paths, `redwood_sound.c`/`redwood_audio.c` pattern → boot-arg `audio=1` → verify: system sounds, music playback, microphone input, volume keys work
- 4️⃣ **Phase 4: Battery (PMIC fuel gauge)** → Read MT6358 gauge registers (SOC, voltage, current, temp, health) via PMIC wrapper, publish via `IOPowerSource`/`IOBattery` → boot-arg `battery=1` → verify: Settings → Battery shows percentage, charging status, time remaining
- 5️⃣ **Phase 5: GPU (PowerVR GE8320)** → Implement PVR driver: firmware load, MMU, job submission, sync objects, Mesa panfrost in userspace → boot-arg `mali=1` → verify: 60 fps UI, OpenGL ES apps run, Metal driver loads (Metal-on-Vulkan or direct)
- 6️⃣ **Phase 6: Sensors (SCP hub)** → Implement SCP communication, accelerometer/gyro/proximity via SCP sensor hub, publish via IOKit `IOHIDDevice` → boot-arg `sensors=1` → verify: auto-rotate, shake-to-undo, proximity during calls
- 7️⃣ **Phase 7: Camera (MTKCAM)** → Implement MTKCAM ISP/AAA, CSI-2 MIPI, V4L2-style buffer queue, `redwood_camera.c` pattern → boot-arg `camera=1` → verify: Camera app opens, preview, capture, video record
- 8️⃣ **Phase 8: Thermal (MTK thermal zones)** → Read MT6765 thermal sensors (APMIXED/SCP), implement `IOPlatformThermalProfile`, CPU/GPU throttling → boot-arg `thermal=1` → verify: thermal zones in ioreg, throttling under load, no overheat shutdown
- 9️⃣ **Verify** → Full run with all boot-args: `XNU_ARGS="cpus=8 touchscreen=1 buttons=2 mtk_cpufreq=3 emmc_root=1 connsys_step=11 bluetooth=2 audio=1 battery=1 mali=1 sensors=1 camera=1 thermal=1"` → log shows all subsystems up, SpringBoard at 60 fps, Wi-Fi connected, Bluetooth paired + audio, battery %, camera works, thermal stable

## 🚫 Not now
Cellular (modem bring-up, QMI/RIL, CarrierBundle) · Torch (LED driver) · Haptics (PMIC vibrator) · DRM/FairPlay (needs Apple keys) · iCloud/App Store (Apple services) · 32-bit app support (XNU is 64-bit only)