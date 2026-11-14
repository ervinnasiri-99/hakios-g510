# Wi-Fi on the G510 (MT6765): what gets ported, what to write from scratch

Plan, not a finished description: as of this writing there is not a single line of the MT6765 Wi-Fi driver in the XNU tree. Here is where the line is drawn, what from the Qualcomm Wi-Fi branch (`main`) drops in unchanged, what has to be written, and what needs to be known before the first line of code.

## 1. What comes from the Qualcomm branch unchanged

In the main branch Wi-Fi is split in two halves, and the boundary between them does not know which radio sits underneath.

Top (up to iOS) — common to any phone:

| file | what it does |
| --- | --- |
| `bsd/dev/redwood_apple80211.c` | answers iOS's Apple80211 service (scan, networks, join) |
| `bsd/dev/redwood_sta.c`, `bsd/sys/redwood_sta.h` | one thread that performs each call into the radio, one at a time; list of what the last scan heard; join a network from file on startup |
| `bsd/sys/redwood_net.h` (+ implementation) | frames: XNU's network interface, "link up/down" |
| `wifi/lx/src/lx_wpa.c` | WPA2 handshake with the password; verified on the host (`tools/wpa-test/run.sh`) |

Bottom in `main` — Linux's ath11k (WCN6750), built on FreeBSD's LinuxKPI headers (`wifi/lx/`,
`bsd/dev/redwood_lx.c`, `tools/mk-ath11k.sh`), and our station on top of it (`wifi/lx/src/lx_80211.c`). This **does not port**: different radio.

## 2. What the new radio driver must supply

Exactly what the upper station expects — nothing more:

```c
/* bsd/sys/g510_sta.h */
struct g510_sta_ops {
	int  (*power)(bool on);                                   /* power radio on/off */
	int  (*scan)(const struct g510_sta_scan *what);
	void (*heard)(void (*each)(const struct g510_sta_bss *, void *), void *);
	int  (*join)(const struct g510_sta_join *what);           /* open or WPA2 with password */
	int  (*leave)(void);
	void (*state)(struct g510_sta_state *state);              /* not asleep, from any thread */
};
g510_sta_attach(&ops);           /* when radio is ready */
g510_sta_lost(error);            /* network went away on its own */

/* bsd/sys/g510_net.h */
struct g510_net_link { bool (*transmit)(mbuf_t, size_t); unsigned (*poll)(void); ... };
g510_net_attach(&link); g510_net_input(frame, length); g510_net_link_state(up);
```

The MediaTek firmware does scanning, join and keys itself (gen4m — a "full MAC" driver — `mtk_cfg80211_scan`, `mtk_cfg80211_connect`, `mtk_cfg80211_add_key` hand the work to firmware), so neither `lx_80211.c` station nor `lx_wpa.c` handshake are needed here: `scan` and `join` are commands to the firmware.

## 3. G510 hardware

- Wi-Fi and Bluetooth (and GPS, FM) — **CONNSYS** subsystem inside MT6765 with its own MCU and radio **MT6631**. On Android brought up by modules `wmt_drv.ko` (power, firmware load, common channel), `wmt_chrdev_wifi.ko` and `wlan_drv_gen4m.ko` (Wi-Fi), in `/vendor/lib/modules`, with firmware from `/vendor/firmware` (per `GEMINI.md`; **verify** exact list on phone).
- No mainline Linux driver for CONNSYS (mt76 is for stand-alone MediaTek chips, not the ones integrated in SoC); only vendor GPL sources in device kernels for MT6765. **Verify** if Transsion released G510 kernel sources; if not — take another MT6765 phone's kernel with same CONNSYS.
- From the G510 device tree (merged.dts, 27 Oct 2026): `consys@18002000`, `compatible = "mediatek,mt6765-consys"`, registers indexed by `enum CONSYS_BASE_ADDRESS_INDEX` (`mtk_wcn_consys_hw.h`):

| index | block | address | size |
| --- | --- | --- | --- |
| 0 | MCU (CONNSYS CPU config; `CONSYS_IP_VER` at offset 0x10) | 0x18002000 | 0x1000 |
| 1 | TOP_RGU (reset) | 0x10007000 | 0x100 |
| 2 | INFRACFG_AO | 0x10001000 | 0x1000 |
| 3 | SPM (power) | 0x10006000 | 0x1000 |
| 4 | MCU_CONN_HIF_ON | 0x18007000 | 0x1000 |
| 5 | MCU_TOP_MISC_OFF | 0x180b1000 | 0x1000 |
| 6 | MCU_CFG_ON | 0x180a3000 | 0x1000 |
| 7 | MCU_CIRQ | 0x180a5000 | 0x800 |
| 8 | MCU_TOP_MISC_ON | 0x180c1000 | 0x1000 |
| 9 | MCU_CONN_HIF_PDMA | 0x18004000 | 0x1000 |

Interrupts (GIC SPI): 0x11C (284), 0x4E (78), 0x11F (287). `wifi@18000000` (`mediatek,wifi`): reg 0x18000000, 1 MB, IRQ SPI 0x11D (285). Reserved memory: `consys-reserve-memory` 4 MB and `wifi-reserve-memory` (shared-dma-pool) 3 MB, both anywhere in 0x40000000–0xC0000000 — Linux places them, so XNU must allocate them itself. PMIC regulator: `mt6358regulator/ldo_vcn33_wifi`.

Still to find out:

1. **Clocks and regulators**: which clocks and PMIC rails (VCN18, VCN28, VCN33) `mt6765.c` enables (`consys_hw_vcn18_ctrl` etc.) — from sources and MT6358 (`mtkloader/mtkpwrap.h` already reads it).
2. **Firmware files and load order**. On phone (27 Oct, `ls` from adb): `soc1_0_patch_mcu_1_1_hdr.bin`, `soc1_0_ram_mcu_1_1_hdr.bin`, `soc1_0_ram_wifi_1_1_hdr.bin`, `soc1_0_ram_bt_1_1_hdr.bin`, `WIFI_RAM_CODE_soc1_0_1_1.bin`, `WMT_SOC.cfg`, `wifi.cfg`, `BT_FW.cfg`, modules `wmt_drv.ko`, `wmt_chrdev_wifi.ko`, `wlan_drv_gen4m.ko`, `bt_drv_connac1x.ko`, `connfem.ko`. Pull via `pull-firmware.sh` and verify exact names.

## 4. What is known and what still needs finding

From sources (27 Oct):

- **CONNSYS power-on** (`mt6765.c`): `consys_hw_power_ctrl` (power domain in SPM), `consys_clock_buffer_ctrl`, `consys_ahb_clock_ctrl`, `consys_hw_reset_bit_set`, `consys_emi_mpu_set_region_protection` (EMI region for firmware), `polling_consys_chipid` (sign that subsystem woke), `consys_acr_reg_setting`, `consys_afe_reg_setting`, then `consys_dl_rom_patch`. Addresses: `CONN_HIF_ON` 0x18007000, AFE 0x180B3000, clocks 0x180C1200, EMI at CPU 0x80068000 (firmware at 0xF0068000), pins 0x10005000 and 0x10420000. Part comes from device tree (`consys_read_reg_from_dts`, `consys_clk_get_from_dts`, `consys_pmic_get_from_dts`) — pull from phone's DTB.
- **Firmware**: ROM patch with `_soc1_0` suffix (`mt66xx_patch_hdr.bin`), chip "Talbot SoC 1.0" with MT6631. Exact filenames from `/vendor/firmware` on phone.
- **Wi-Fi**: gen4m, AXI bus; "full MAC" driver — `mtk_cfg80211_scan`, `mtk_cfg80211_connect`, `mtk_cfg80211_add_key` hand work to firmware.

Still need to find:

1. **Clocks and regulators**: which clocks and PMIC pins (VCN18, VCN28, VCN33) `mt6765.c` toggles (`consys_hw_vcn18_ctrl` and neighbours) — from sources and MT6358 (`mtkloader/mtkpwrap.h` already reads it).
2. **Firmware files and load order**. On phone (27 Oct, `ls` from adb) they are: `soc1_0_patch_mcu_1_1_hdr.bin`, `soc1_0_ram_mcu_1_1_hdr.bin`, `soc1_0_ram_wifi_1_1_hdr.bin`, `soc1_0_ram_bt_1_1_hdr.bin`, `WIFI_RAM_CODE_soc1_0_1_1.bin`, `WMT_SOC.cfg`, `wifi.cfg`, `BT_FW.cfg`, modules `wmt_drv.ko`, `wmt_chrdev_wifi.ko`, `wlan_drv_gen4m.ko`, `bt_drv_connac1x.ko`, `connfem.ko`. Pull via `pull-firmware.sh`.
3. **CONNSYS source tree**: Transsion kernel for G510 not public; use MiCode/MTK_kernel_modules `merlin-r-oss` commit `ffe6bc5` (6 July 2021, 48 MB, Xiaomi's Redmi 9 / POCO M2 = MT6768, same CONNSYS) — downloaded 27 Oct to `xnu-deps/build/connsys-src/` (in `.gitignore`). Contains:
   - `common/common_main/platform/mt6768.c`, `include/mt6768.h` — WMT for MT6768: power, clocks, EMI, ROM patch load
   - `conninfra/` — common CONNSYS infrastructure
   - `wlan/core/gen4m/` — Wi-Fi driver; `os/linux/plat/mt6768/plat_priv.c`, AXI bus `os/linux/hif/axi`
   - `bt/mt66xx/` — Bluetooth for this family

## 5. What is ported and what is new

From `main` branch (Qualcomm) **without changes**:

| file | what it does |
| --- | --- |
| `bsd/dev/redwood_apple80211.c` | answers iOS Wi-Fi service requests (scan, networks, join) |
| `bsd/dev/redwood_sta.c`, `bsd/sys/redwood_sta.h` | single thread that serializes every call into the radio; list of last scan's results; join network from file at boot |
| `bsd/sys/redwood_net.h` (+ impl) | frames: XNU network interface, "link up/down" |
| `wifi/lx/src/lx_wpa.c` | WPA2 handshake with password; checked on host (`tools/wpa-test/run.sh`) |

From `main` **not portable** — ath11k from Linux (WCN6750), built on FreeBSD LinuxKPI headers (`wifi/lx/`, `bsd/dev/redwood_lx.c`, `tools/mk-ath11k.sh`), and our station over it (`wifi/lx/src/lx_80211.c`). **Different hardware.**

## 6. What the new radio driver must provide

Exactly what the upper station expects — nothing more:

```c
/* bsd/sys/g510_sta.h */
struct g510_sta_ops {
	int  (*power)(bool on);                                   /* power radio on/off */
	int  (*scan)(const struct g510_sta_scan *what);
	void (*heard)(void (*each)(const struct g510_sta_bss *, void *), void *);
	int  (*join)(const struct g510_sta_join *what);           /* open or WPA2 with password */
	int  (*leave)(void);
	void (*state)(struct g510_sta_state *state);              /* not asleep, from any thread */
};
g510_sta_attach(&ops);           /* when radio is ready */
g510_sta_lost(error);            /* network vanished on its own */

/* bsd/sys/g510_net.h */
struct g510_net_link { bool (*transmit)(mbuf_t, size_t); unsigned (*poll)(void); ... };
g510_net_attach(&link); g510_net_input(frame, length); g510_net_link_state(up);
```

MediaTek firmware does scan, join, keys itself (gen4m — "full MAC" driver — `mtk_cfg80211_scan`, `mtk_cfg80211_connect`, `mtk_cfg80211_add_key` hand work to firmware), so neither `lx_80211.c` station nor `lx_wpa.c` handshake are needed here: `scan` and `join` are commands to firmware.

## 3. G510 hardware

- Wi-Fi and Bluetooth (and GPS, FM) — CONNSYS subsystem inside MT6765 with its own MCU and radio **MT6631**. On Android brought up by `wmt_drv.ko` (power, firmware load, common channel), `wmt_chrdev_wifi.ko` and `wlan_drv_gen4m.ko` (Wi-Fi), in `/vendor/lib/modules`, with firmware from `/vendor/firmware` (per `GEMINI.md`; **verify** exact list on phone).
- No mainline Linux driver for CONNSYS (mt76 is for stand-alone MediaTek chips, not SoC-integrated); only vendor GPL sources in device kernels for MT6765. **Verify** if Transsion released G510 kernel; if not — take another MT6765 phone's kernel with same CONNSYS.
- From G510 device tree (merged.dts, 27 Oct 2026): `consys@18002000`, `compatible = "mediatek,mt6765-consys"`, register blocks indexed by `enum CONSYS_BASE_ADDRESS_INDEX` (`mtk_wcn_consys_hw.h`):

| index | block | address | size |
| --- | --- | --- | --- |
| 0 | MCU (CONNSYS CPU config; `CONSYS_IP_VER` at offset 0x10) | 0x18002000 | 0x1000 |
| 1 | TOP_RGU (reset) | 0x10007000 | 0x100 |
| 2 | INFRACFG_AO | 0x10001000 | 0x1000 |
| 3 | SPM (power) | 0x10006000 | 0x1000 |
| 4 | MCU_CONN_HIF_ON | 0x18007000 | 0x1000 |
| 5 | MCU_TOP_MISC_OFF | 0x180b1000 | 0x1000 |
| 6 | MCU_CFG_ON | 0x180a3000 | 0x1000 |
| 7 | MCU_CIRQ | 0x180a5000 | 0x800 |
| 8 | MCU_TOP_MISC_ON | 0x180c1000 | 0x1000 |
| 9 | MCU_CONN_HIF_PDMA | 0x18004000 | 0x1000 |

Interrupts (GIC SPI): 0x11C (284), 0x4E (78), 0x11F (287). `wifi@18000000` (`mediatek,wifi`): reg 0x18000000, 1 MB, IRQ SPI 0x11D (285). Reserved memory: `consys-reserve-memory` 4 MB and `wifi-reserve-memory` (shared-dma-pool) 3 MB, both anywhere in 0x40000000–0xC0000000 — Linux picks, so XNU must allocate itself. PMIC regulator: `mt6358regulator/ldo_vcn33_wifi`.

Still need to find out:

1. **Clocks and regulators**: which clocks and PMIC rails (VCN18, VCN28, VCN33) `mt6765.c` enables (`consys_hw_vcn18_ctrl` and neighbours) — from sources and MT6358 (`mtkloader/mtkpwrap.h` already reads it).
2. **Firmware files and load order**. On phone (27 Oct, `ls` from adb): `soc1_0_patch_mcu_1_1_hdr.bin`, `soc1_0_ram_mcu_1_1_hdr.bin`, `soc1_0_ram_wifi_1_1_hdr.bin`, `soc1_0_ram_bt_1_1_hdr.bin`, `WIFI_RAM_CODE_soc1_0_1_1.bin`, `WMT_SOC.cfg`, `wifi.cfg`, `BT_FW.cfg`, modules `wmt_drv.ko`, `wmt_chrdev_wifi.ko`, `wlan_drv_gen4m.ko`, `bt_drv_connac1x.ko`, `connfem.ko`. Pull via `pull-firmware.sh`.

## 4. What is known and what still needs finding

From sources (27 Oct):

- **CONNSYS power-on** (`mt6765.c`): `consys_hw_power_ctrl` (SPM power domain), `consys_clock_buffer_ctrl`, `consys_ahb_clock_ctrl`, `consys_hw_reset_bit_set`, `consys_emi_mpu_set_region_protection` (EMI region for firmware), `polling_consys_chipid` (sign subsystem alive), `consys_acr_reg_setting`, `consys_afe_reg_setting`, then `consys_dl_rom_patch`. Addresses: `CONN_HIF_ON` 0x18007000, AFE 0x180B3000, clocks 0x180C1200, EMI at CPU 0x80068000 (firmware at 0xF0068000), pins 0x10005000 and 0x10420000. Part from device tree (`consys_read_reg_from_dts`, `consys_clk_get_from_dts`, `consys_pmic_get_from_dts`) — extract from phone's DTB.
- **Firmware**: ROM patch with `_soc1_0` suffix (`mt66xx_patch_hdr.bin`), chip "Talbot SoC 1.0" with MT6631. Exact filenames from `/vendor/firmware` on phone.
- **Wi-Fi**: gen4m, AXI bus; "full MAC" driver — `mtk_cfg80211_scan`, `mtk_cfg80211_connect`, `mtk_cfg80211_add_key` delegate to firmware.

Still need to find:

1. **Clocks and regulators**: which clocks and PMIC rails (VCN18, VCN28, VCN33) `mt6765.c` enables (`consys_hw_vcn18_ctrl` and neighbours) — from sources and MT6358 (`mtkloader/mtkpwrap.h` already reads it).
2. **Firmware files and load order**. On phone (27 Oct, `ls` from adb): `soc1_0_patch_mcu_1_1_hdr.bin`, `soc1_0_ram_mcu_1_1_hdr.bin`, `soc1_0_ram_wifi_1_1_hdr.bin`, `soc1_0_ram_bt_1_1_hdr.bin`, `WIFI_RAM_CODE_soc1_0_1_1.bin`, `WMT_SOC.cfg`, `wifi.cfg`, `BT_FW.cfg`, modules `wmt_drv.ko`, `wmt_chrdev_wifi.ko`, `wlan_drv_gen4m.ko`, `bt_drv_connac1x.ko`, `connfem.ko`. Pull via `pull-firmware.sh`.

## 5. Sources (downloaded 27 Oct 2026)

`xnu-deps/build/connsys-src/` (in `.gitignore`): `connectivity/` from `merlin-r-oss` branch of [MiCode/MTK_kernel_modules](https://github.com/MiCode/MTK_kernel_modules) — MediaTek kernel modules Xiaomi released for Redmi 9 / POCO M2 (Helio G80/G85, MT6768 platform, like X6826B), commit `ffe6bc5` from 6 July 2021, 48 MB, license in repo unspecified. Contains:

| path | what |
| --- | --- |
| `common/common_main/platform/mt6768.c`, `include/mt6768.h` | WMT for MT6768: power, clocks, EMI, ROM patch load |
| `conninfra/` | common CONNSYS infrastructure |
| `wlan/core/gen4m/` | Wi-Fi driver; `os/linux/plat/mt6768/plat_priv.c`, AXI bus `os/linux/hif/axi` |
| `bt/mt66xx/` | Bluetooth for this family |

## 6. What is ported and what is new

From `main` branch (Qualcomm) **unchanged**:

| file | what it does |
| --- | --- |
| `bsd/dev/redwood_apple80211.c` | answers iOS Wi-Fi service Apple80211 requests (scan, networks, join) |
| `bsd/dev/redwood_sta.c`, `bsd/sys/redwood_sta.h` | single thread, serializes every radio call, one at a time; list of what last scan heard; join network from file at boot |
| `bsd/sys/redwood_net.h` (+ impl) | frames: XNU network interface, "link up/down" |
| `wifi/lx/src/lx_wpa.c` | WPA2 handshake with password; verified on host (`tools/wpa-test/run.sh`) |

Bottom in `main` — ath11k from Linux (WCN6750), built on FreeBSD LinuxKPI headers (`wifi/lx/`, `bsd/dev/redwood_lx.c`, `tools/mk-ath11k.sh`), and our station on top (`wifi/lx/src/lx_80211.c`). This **does not port**: different hardware.

## 7. What the new radio driver must provide

Exactly what the upper station expects — nothing more:

```c
/* bsd/sys/g510_sta.h */
struct g510_sta_ops {
	int  (*power)(bool on);                                   /* power radio on/off */
	int  (*scan)(const struct g510_sta_scan *what);
	void (*heard)(void (*each)(const struct g510_sta_bss *, void *), void *);
	int  (*join)(const struct g510_sta_join *what);           /* open network or WPA2 with password */
	int  (*leave)(void);
	void (*state)(struct g510_sta_state *state);              /* not asleep, from any thread */
};
g510_sta_attach(&ops);           /* when radio is ready */
g510_sta_lost(error);            /* network went away on its own */

/* bsd/sys/g510_net.h */
struct g510_net_link { bool (*transmit)(mbuf_t, size_t); unsigned (*poll)(void); ... };
g510_net_attach(&link); g510_net_input(frame, length); g510_net_link_state(up);
```

MediaTek firmware does scan, join, keys itself (gen4m — "full MAC" driver — `mtk_cfg80211_scan`, `mtk_cfg80211_connect`, `mtk_cfg80211_add_key` hand work to firmware), so neither `lx_80211.c` station nor `lx_wpa.c` handshake are needed here: `scan` and `join` are commands to firmware.

## 8. What has been ported and what is new

| The same on every phone (already written) | Yours to find and write |
| --- | --- |
| The kernel: Apple's xnu-8796.121.2 + `xnu/redwood.patch` + `xnu/files/`, built as `DEVELOPMENT_ARM64_BCM2837` | The loader's board constants (the table below) |
| GICv3 (`osfmk/arm64/gic_v3_irq.c`), PSCI cores (`psci_smp.c`), the return to the loader (`loader_return.c`), the platform expert (`iokit/Kernel/GenericARMPE.cpp`) | The order of the processors and which are the big ones |
| The image format and its packer (`tools/chainboot.py`), the run cycle (`tools/xnu-cycle.sh`, `tools/boot-xnu.sh`) | The storage driver if yours is not an eMMC on MSDC0 left ready by LK |
| The iOS side: `ios165/fetch.sh`, `userland/build.sh`, the injected libraries | The touch controller, the GPU's power sequence, the keys, the radios, the camera |
| The log's road back through ramoops (`mtkloader/mtkdbg.h`) | The addresses of the ramoops zones and of LK's framebuffer |

A phone with another SoC of the same family keeps most of the left column and all of the method. A phone that is not MediaTek keeps the kernel and the iOS side only: `mtkloader` assumes LK, MSDC, TOPRGU and OVL.