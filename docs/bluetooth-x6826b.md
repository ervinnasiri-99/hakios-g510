# Bluetooth on the G510 (MT6765, MT6631): from controller to `bluetoothd` iOS 16.5

Paths `userland/...`, `runkit/...` and kernel files `g510_connsys.c`, `g510_btsk.c` here — from the common repository (hakios-xnu-kernel, branch `mtk`); drivers for this phone live in `../kernel/`. Names of foreign devices from the air removed.

Runs 601–711 (8–9 November 2026). Controller MT6631 (in CONNSYS, like Wi-Fi: `wifi-x6826b.md`) powered on by kernel; real `bluetoothd` from iOS 16.5 speaks HCI over its native transport and **reaches `PoweredOn`**: app via CoreBluetooth sees `state 5`, scan reaches radio, real LE advertisements from nearby devices arrive in `bluetoothd` and **reach the app**: `bt_test` (unfiltered scan) shows six devices in ten seconds, one with name (run 711).

## Path from radio to app

```
CoreBluetooth in app ──XPC── bluetoothd (iOS 16.5, real)
   bluetoothd ──ioctl/getsockopt── kern_control "com.apple.uart.sk.bluetooth"        g510_connsys.c (bt_ctl_*)
   bluetoothd ──os_channel_create── Skywalk kernel pipe "g510_bt"                 g510_btsk.c
   kernel: H4 packets ⇄ STP, channel 0 (BT_TASK_INDX) ⇄ BTIF ⇄ CONNSYS ⇄ MT6631        g510_connsys.c (bt_bridge)
```

- **Enable** (`connsys_bt=1`, step 20 in `g510_connsys.c`): after WMT (steps 1–11) `WMT_FUNC_CTRL` `01 06 02 00 00 01` (type 0 — BT, 1 — on), reply `02 06 01 00 00`. Before reply firmware sends its event opcodes 0x16 — they are not ack. Then raw HCI in STP channel 0 frames (`connsys_stp_task`). Controller: HCI 5.2, manufacturer 0x0046 (MediaTek), default address `00:00:46:76:63:01`, ACL 1021×696, LE 251×8 (run 602).
- **Nodes and transport** (`connsys_bt=2`): `/dev/cu.bluetooth`, `/dev/uart.bluetooth`, `/dev/btwake`, `/dev/btreset`, `/dev/btpoweroff` (read/written, terminal ioctls accepted — that was not enough: `bluetoothd` transport is different), kernel control socket `com.apple.uart.sk.bluetooth` (`CTL_FLAG_REG_SOCK_STREAM`: it connects SOCK_STREAM) and Skywalk kernel pipe per `bsd/skywalk/nexus/kpipe/nx_kpipe_loopback.c`: `getsockopt 0x40105412` returns channel UUID, `os_channel_create` opens it, user's TX ring — H4 packets for controller, RX ring fills `sync_rx` from controller's queue, one packet per slot.
- **Transport `H4BC` — only working one for `bluetoothd`** (run 612): driver `H4` in its table not found.

## What was needed from the environment

| What | How found | What done |
| --- | --- | --- |
| `No bluetooth on this device.` (603) | disasm `FindLocalMacAddress`: address from `BT_DEVICE_ADDRESS` (zeroed), from IORegistry node `bluetooth`/`marconi-bt` (nodes don't become services, 606) or from boot-arg | boot-arg `wlan.debug.generate-mac=1`: `bluetoothd` makes random address itself (607) |
| `bluetoothd` crashes on identity root | keychain without `keybagd`/`securityd`/`aks_keys=1` | image `rc.launchd.metalaks`: `aks_restore`, `keybagd --init`, then Metal; launchd jobs `keybagd`, `securityd` (608) |
| `BlueTool timed out running boot script` | BlueTool picks Broadcom firmware by `kMGQWifiVendor`, no `.hcd` files in image | service `com.apple.BlueTool` inside real BlueTool process (`libg510`, `bluetool_serves`): `result 0`, `manufacturer`, `firmware` (611). Separate binary not allowed: `launchd` panics on non-platform exec (610) |
| `Local address` and command mask | controller doesn't set LE bits in `Read_Local_Supported_Commands` (octets 24–28) | kernel adds Core 5.2 bits (616, 617) |
| `Write_Default_Erroneous_Data_Reporting` (0x0c5b) | controller: Unknown command, `bluetoothd` treats as failure | reply as completed (619) |
| vendor Apple/Broadcom commands (OGF 0x3f) | `Unknown command` on each | `Command Complete`, status 0 (628) |
| `bluetoothd` restarts every ~17 s | `_exit(1)` in `bluetoothd+0x90238`, reason `reason=3602` (`VSC stall detected`) — vendor command hung, needs data in reply | see below |

### Which vendor command asks what (by names from `Sending: BD_VSC_…` vs wires, run 659+)

Kernel (`bt_quirks`) rewrites controller's "Unknown command" to `Command Complete`:

- by status — all except listed: `0xfce9` (BD_VSC_LE_META_ENA_FEATURES), `0xfce2` with subcmds `0x0b` (CHANGE_LE_SETTINGS), `0x1c` (SET_MIN_AFH_LE_CHANNELS), `0x26` (SET_SAR_LOCATION), `0xfd47` (ENABLE_EXTENDED_DUPLICATE_FILTER), `0xfd4e`. **Data on 0xfce9 breaks queue** (650–658: next command doesn't leave);
- data byte after status — `0xfd48` (`bcm_vsc.c:642 NumReadBytesAvail >= 1`);
- 16 zeros — `0xfce2` subcmd `0x15` (**BD_VSC_OLYMPIC_ANTENNA_PREF_B**: six bytes `BT_VSCAntPrefParams_t` after header `15 00 00 00`; its reply — byte stream) and `0x36` (BT_VSC_OLYMPIC_SET_RETAIN_LE_SCAN_DUPLICATES_ON_START);
- 64 bytes (first `0x51`) — `0xfd1b 0x51` (**BD_VSC_FACTORY_CAL_READ_TABLE**, read table).

After this queue reaches end: `Power state change`, `managerState 5`, `Update Dynamic Store with state On`, `CBMsgIdStateUpdated` to all sessions, app gets `state 5` (run 670).

### How it was found (tools left in `libg510`)

`bluetoothd` disappears from console with process, so inside it (`userland/src/inject.c`, program name `bluetoothd`, only in it):
- intercept `_os_log_impl`/`_os_log_error_impl`/`_os_log_fault_impl` writes its state messages (before stack start — doesn't write, after enable — only scan) into `kern.g510.host_keep` buffer which kernel appends to log end: «`bt[pid] Sending: …`», «`completed with status …`», «`Bluetooth error - restarting {reason, context}`»;
- intercept `_exit` (where `bluetoothd` exits: `+0x90238` and `+0x902a8`) returns from its restart functions, as their epilog, if reason is VSC stall (`reason=3602`); `bluetoothd` stays alive, doesn't restart;
- `bt_test` (`userland/src/bt_test.m`, rights `kTCCServiceBluetoothAlways`): central manager state and scan.

## What still needs the environment

| What | How found | What to do |
| --- | --- | --- |
| `bluetoothd` identity root | keychain without `keybagd`/`securityd`/`aks_keys=1` | image `rc.launchd.metalaks`: `aks_restore`, `keybagd --init`, then Metal; launchd jobs `keybagd`, `securityd` (608) |
| `BlueTool` boot script | BlueTool picks Broadcom firmware by `kMGQWifiVendor`, no `.hcd` files in image | service `com.apple.BlueTool` inside real BlueTool process (`libg510`, `bluetool_serves`): `result 0`, `manufacturer`, `firmware` (611). Separate binary not allowed: `launchd` panics on non-platform exec (610) |
| `Local address` and command mask | controller doesn't set LE bits in `Read_Local_Supported_Commands` (octets 24–28) | kernel adds Core 5.2 bits (616, 617) |
| `Write_Default_Erroneous_Data_Reporting` (0x0c5b) | controller: Unknown command, `bluetoothd` treats as failure | reply as completed (619) |
| vendor Apple/Broadcom commands (OGF 0x3f) | `Unknown command` on each | `Command Complete`, status 0 (628) |
| `bluetoothd` restarts every ~17 s | `_exit(1)` in `bluetoothd+0x90238`, reason `reason=3602` (`VSC stall detected`) — vendor command hung, needs data in reply | see below |

### Which vendor command asks what (by names from `Sending: BD_VSC_…` vs wires, run 659+)

Kernel (`bt_quirks`) rewrites controller's "Unknown command" to `Command Complete`:

- by status — all except listed: `0xfce9` (BD_VSC_LE_META_ENA_FEATURES), `0xfce2` with subcmds `0x0b` (CHANGE_LE_SETTINGS), `0x1c` (SET_MIN_AFH_LE_CHANNELS), `0x26` (SET_SAR_LOCATION), `0xfd47` (ENABLE_EXTENDED_DUPLICATE_FILTER), `0xfd4e`. **Data on 0xfce9 breaks queue** (650–658: next command doesn't leave);
- data byte after status — `0xfd48` (`bcm_vsc.c:642 NumReadBytesAvail >= 1`);
- 16 zeros — `0xfce2` subcmd `0x15` (**BD_VSC_OLYMPIC_ANTENNA_PREF_B**: six bytes `BT_VSCAntPrefParams_t` after header `15 00 00 00`; its reply — byte stream) and `0x36` (BT_VSC_OLYMPIC_SET_RETAIN_LE_SCAN_DUPLICATES_ON_START);
- 64 bytes (first `0x51`) — `0xfd1b 0x51` (**BD_VSC_FACTORY_CAL_READ_TABLE**, read table).

After this queue reaches end: `Power state change`, `managerState 5`, `Update Dynamic Store with state On`, `CBMsgIdStateUpdated` to all sessions, app gets `state 5` (run 670).

### How it was found (tools left in `libg510`)

`bluetoothd` disappears from console with process, so inside it (`userland/src/inject.c`, program name `bluetoothd`, only in it):
- intercept `_os_log_impl`/`_os_log_error_impl`/`_os_log_fault_impl` writes its state messages (before stack start — doesn't write, after enable — only scan) into `kern.g510.host_keep` buffer which kernel appends to log end: «`bt[pid] Sending: …`», «`completed with status …`», «`Bluetooth error - restarting {reason, context}`»;
- intercept `_exit` (where `bluetoothd` exits: `+0x90238` and `+0x902a8`) returns from its restart functions, as their epilog, if reason is VSC stall (`reason=3602`); `bluetoothd` stays alive, doesn't restart;
- `bt_test` (`userland/src/bt_test.m`, rights `kTCCServiceBluetoothAlways`): central manager state and scan.

## Image and build

- System partition (32 MB) was full: `libOSMesa.8.dylib` (11 MB) moved to system image (`/usr/lib/g510/libOSMesa.8.dylib`), in boot image — symlink (`OSMESA_EXT=1` in `runkit/mk.sh`, `userland/build.sh` puts copy in system image; image without `OSMESA_EXT` stays as is). System partition `xnusys` reflashed once (`tools/system-flash.sh`, 8 Nov); image 30.4 MB of 32.
- Local (`runkit/` outside git) run: `runkit/bt-run.sh` (Metal + Wi-Fi + keychain + camera driver + Bluetooth). Variables: `RUN`, `SECS`, `BTLOG="com.apple.bluetoothd"` (verbose os_log `bluetoothd`; without — silent), `OSMESA_EXT=1`,