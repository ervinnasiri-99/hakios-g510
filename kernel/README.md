# kernel/

This phone's drivers in Apple's XNU (xnu-8796.121.2), as files of the kernel tree (the paths below are
the tree's). Licensed under the Apple Public Source License 2.0 (`../LICENSE.APSL`), as code built
into XNU.

| file | what | boot-arg |
| --- | --- | --- |
| `bsd/dev/g510_emmc.c`, `bsd/sys/g510_emmc.h` | the eMMC's `xnusys` partition as a read-only block device and the root | `emmc_root=1` |
| `bsd/dev/g510_tcm.c` | the touch screen (Novatek NT36525 on SPI0) | `touchscreen=1` |
| `bsd/dev/g510_mtkkeys.c` | the buttons; the big cores' clock | `buttons=2`, `mtk_cpufreq=` |
| `bsd/dev/g510_connsys.c` | CONNSYS (Wi-Fi/Bluetooth): power, firmware, the MCU patch (PDA), BTIF, STP/WMT, `connsys_sw_init` | `connsys_step=1..11` |
| `bsd/dev/g510_mtkwifi.c` | the Wi-Fi itself (gen4m CONNAC on AXI): firmware, scan, WPA2 join, `en0`; the network is listed and joined in Settings | `connsys_step=12..17` |
| `bsd/dev/g510_mtkbt.c`, `bsd/sys/g510_mtkbt.h` | Bluetooth transport (HCI over STP ch0 via BTIF): `mtkbt_platform` for the skeleton; function on/off, H4 send/receive | `mtkbt_skel=1` |
| `bsd/dev/g510_mtkbatt.c` | the battery: `kern.g510.battery_*`, and HakiosBattery, the IOPMPowerSource Darwin reads the charge from | `mtkbatt=1` |
| `bsd/dev/g510_mtkkeys.c` | the power and volume keys, read from the PMIC through the SPMI arbiter | `buttons=2`, `mtk_cpufreq=3` |
| `bsd/dev/g510_cpufreq.c` | the clock of the cores the kernel runs on (cpufreq=1), and the memory bus's votes (ddr=) | `mtk_cpufreq=` |
| `bsd/dev/g510_mtk_scp.c` | the SCP sensor hub: accelerometer, `kern.g510.accel` | `scp_step=7` |
| `bsd/dev/g510_mtkbatt.c` | the battery: `kern.g510.battery_*`, and HakiosBattery, the IOPMPowerSource Darwin reads the charge from | `mtkbatt=1` |

They are not a kernel on their own: they hook into the common kernel (its `bsd/conf/files`, the input
device `/dev/hakios-touch`, the display driver, `IOFindBSDRoot` for `emmc_root=1`), which lives in the
common repository. Until they are merged there they keep the project's former names (`redwood_*`,
`kern.redwood.*`); the merge renames them to `hakios_*`.

They are merged into the common repository's branch `mtk` as `mtk/mt6765/drivers/` (`hakios.*`, `kern.hakios.*`), where
`make g510_defconfig && make kernel` builds them; these copies are kept as they were.

`bsd/dev/g510_mtkwifi.c` shares the PMIC wrapper with `g510_mtkkeys.c` and `g510_connsys.c` through `g510_pwrap_lock()`, which lives in the common
tree's `redwood_input.c`: `patches/redwood_pwrap_lock.patch` is that change.