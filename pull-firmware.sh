#!/bin/bash
# The General Mobile GM 21 Plus (G510) firmware, out of YOUR phone's Android, into fw-*/ (at the top of the HakiOS tree, or next to this repository's mtkloader/)
# (git ignores them: they are the vendor's, and nothing of the vendor's is in the repository).
#
#   fw-touch/novatek_ts_fw.bin   the touch controller's (Novatek NT36525 on SPI0): the loader carries it
#                               (mtkloader/tcm.h; mtkloader/build.sh makes touchfw.h out of it)
#   fw-connsys/                 CONNSYS's, for Wi-Fi and Bluetooth (docs/wifi-x6826b.md)
#
# Android keeps them where the shell user cannot read (SELinux), so this needs root on the phone
# (Magisk's su): they are copied to a temporary folder, pulled, and the folder is removed. Read only:
# nothing on the phone is changed.
set -e
S=${HAKIOS_SERIAL:?set HAKIOS_SERIAL to the serial of your phone, as adb devices shows it}
# into the top of the HakiOS tree this repository is synced into (devices/g510/), where mtk/loader looks; else (or with a
# directory given) next to this repository's own mtkloader/
HERE="$(cd "$(dirname "$0")" && pwd)"
if [ -z "${1:-}" ] && [ -d "$HERE/../../mtk/loader" ]; then OUT="$(cd "$HERE/../.." && pwd)"; else OUT="${1:-$HERE}"; fi
TMP=/data/local/tmp/hakios-fw
CONNSYS=(
    soc1_0_patch_mcu_1_1_hdr.bin
    soc1_0_ram_mcu_1_1_hdr.bin
    soc1_0_ram_wifi_1_1_hdr.bin
    soc1_0_ram_bt_1_1_hdr.bin
    WIFI_RAM_CODE_soc1_0_1_1.bin
    WMT_SOC.cfg
    wifi.cfg
    BT_FW.cfg
)

adb -s "$S" shell "su -c 'rm -rf $TMP; mkdir -p $TMP; \
    for f in ${CONNSYS[@]}; do cp /vendor/firmware/\$f $TMP/; done; \
    cp \$(find /vendor/firmware /vendor/etc -name novatek_ts_fw.bin 2>/dev/null | head -1) $TMP/; \
    chmod 644 $TMP/*'"
mkdir -p "$OUT/fw-touch" "$OUT/fw-connsys"
adb -s "$S" pull "$TMP/novatek_ts_fw.bin" "$OUT/fw-touch/" >/dev/null
for f in "${CONNSYS[@]}"; do adb -s "$S" pull "$TMP/$f" "$OUT/fw-connsys/" >/dev/null; done
adb -s "$S" shell "su -c 'rm -rf $TMP'"
ls -l "$OUT/fw-touch" "$OUT/fw-connsys"