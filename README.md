# hakios-g510

English · [Русский](README.ru.md)

HakiOS for the **General Mobile GM 21 Plus (G510)**: MediaTek Helio G35 (**MT6765**), PowerVR GE8320 GPU, eMMC.
iOS 16.5's own user space (launchd → backboardd → SpringBoard → apps) on Apple's open XNU kernel
(xnu-8796.121.2), with this phone's own loader and drivers.

Nothing on the phone is replaced for good: `mtkloader` sits in `boot_a` in front of Android's own kernel,
starts XNU once when Android asks for it, and the phone comes back to Android after every run. The iOS
system image lives in a partition of its own (`xnusys`, carved from the end of `userdata`) and the kernel
reads it off the eMMC as it needs it.

## What works

| | | |
| --- | --- | --- |
| boot | works | mtkloader (`mtk/loader/` of the common tree): MMU off, eMMC through MSDC0, device tree for XNU; 8 cores |
| CPU | works | big cores clocked up (`mtk_cpufreq=3`) |
| storage | works, read only | the system image read off the eMMC as needed (`hakios.mtk_emmc.c`, `emmc_root=1`): ~2.5 GB of 4 for iOS |
| display | works | 720×1600, iOS at scale 2 |
| touch | works | Novatek NT36525 over SPI0 (`hakios.tcm.c`; the loader sends its firmware) |
| buttons | works | power, volume (`hakios.mtk_keys.c`) |
| GPU | missing | PowerVR GE8320 — fb-only for now (fbscale=2 fbfit=3) |
| sensors | missing | accelerometer through the SCP sensor hub |
| Wi-Fi | partial | CONNSYS up, WMT channel over BTIF/STP; no network yet (connsys_step=8) |
| Bluetooth | partial | bluetoothd of iOS 16.5 on the MT6631 (HCI over STP): powered on, BLE scan reaches apps, Settings lists devices; no confirmed pairing, profiles or audio yet |
| audio, camera, haptics, cellular | missing | |

## What is here

- `mtkloader/` — the bare-metal loader (MIT): chains in front of Android's kernel in `boot_a`, brings up
  what XNU cannot (touch firmware over SPI, PMIC, clocks), reads the eMMC, builds XNU's device tree and boot
  arguments.
- `kernel/` — this phone's drivers in XNU (APSL 2.0, see `kernel/README.md`).
- `tools/` — running it: `xnu-cycle.sh` (one run: build, flash `boot_a`, start XNU, fetch the log),
  `boot-xnu.sh`, `chainboot.py`, `system-flash.sh` (the system image into `xnusys`), `gpt-carve.py`,
  probes (`gpu-probe.sh`, `touch-probe.sh`, `msdc-test.sh`), log readers (`fbshot.py`, `frames.py`,
  `cache-name.py`, `rwm-recipes.py`).
- `configs/` — `g510_defconfig` and `g510.config`: the kernel's configuration for this phone in the common tree.
- `pull-firmware.sh` — the phone's firmware out of **your** phone into `fw-*/` (needs root on Android).
- `docs/` — how each part was found and what each run showed.
- `device.conf` — the phone and the state of each component.

The common tree's branch `mtk` ([hakios-xnu-kernel](https://github.com/oosexgang/hakios-xnu-kernel), branch `mtk`) has
the same loader and scripts (`mtk/loader/`, `mtk/tools/`) and these drivers under `hakios` names (`mtk/mt6765/`), and builds and
runs them with `make`.

## Not here, by design

No file of iOS, no firmware of the phone, no screenshot (LEGAL.md). The loader's build makes two headers and git ignores both:
`touchfw.h` from your phone's `hdl_firmware.img` (`pull-firmware.sh`), and `font8x16.h`, the loader's text font, rasterized on
your Mac.

## Running

In the common tree, the branch `mtk` (what it needs:
- a General Mobile GM 21 Plus (G510) with an unlocked bootloader, rooted Android (Magisk), its stock
  `boot_a` image saved (`backup/boot_a_magisk.img` at the top of the tree);
- a Mac, your own Darwin userland 16.5 IPSW (for `xnuloader/identity.h`).

```sh
git clone -b mtk https://github.com/oosexgang/hakios-xnu-kernel.git hakios && cd hakios
tools/hakios-device.py sync g510      # this repository, into devices/g510
export HAKIOS_SERIAL=...                # your phone, as `adb devices` shows it
devices/g510/pull-firmware.sh         # fw-touch/, fw-connsys/
xnuloader/mkidentity.sh your.ipsw
make g510_defconfig && make kernel    # hakios-kernel: mtk_emmc mtk_connsys mtk_keys tcm mtk_scp
make phone SECS=300 RAMDISK=...         # boot_a written, XNU once, Android back, the log in logs/
```

The user space that runs on this phone (Mesa's panfrost, the Metal driver) still reads the project's former names
(`kern.redwood.*`, `RedwoodMali`) and is not yet in the common tree; `RAMDISK=` and the system image in `xnusys` are its.

With this repository's own `tools/`, in the layout of the tree it was written in (`mtkloader/` and `tools/` at its top, next
to `xnuloader/`, `xnu-deps/` and `userland/`):

```sh
./pull-firmware.sh                   # fw-touch/, fw-connsys/ next to this repository's mtkloader/
XNU_SECS=300 XNU_ARGS="cpus=8 touchscreen=1 buttons=2 mtk_cpufreq=3 emmc_root=1" \
    tools/xnu-cycle.sh                # one run; the log lands in logs/
```

## Licenses

`kernel/` is APSL 2.0 (`LICENSE.APSL`), as code built into XNU; everything else is MIT (`LICENSE`).
See `NOTICE` and `LEGAL.md`.# Trigger CI

test

Trigger build
test
Trigger build
Trigger build
trigger
trigger
