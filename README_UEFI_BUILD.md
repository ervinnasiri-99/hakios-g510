# G510 UEFI Build Setup (MT6765 - Same SoC as A03s)

The A03s UEFI approach works on MT6765 and boots to SpringBoard. G510 uses the same SoC.

## Required Directory Structure

```
hakios-g510/
├── uefi/
│   ├── MT6765Pkg/           # <-- Our package (copied from A03s, adapted for G510)
│   │   ├── Devices/g510.dsc
│   │   ├── Include/Configuration/DeviceMemoryMap.h
│   │   ├── Include/Configuration/XnuPayload.h
│   │   └── Library/... (XnuLoaderImageLib, XnuPayloadLib, FrameBufferBltLib, FrameBufferSerialPortLib)
│   └── BootShim/            # <-- Copied from A03s (no changes needed)
├── scripts/
│   ├── build_uefi_g510.sh   # Build UEFI firmware
│   ├── make_boot_g510.sh    # Pack boot.img
│   ├── flash_g510.sh        # Flash via ADB/fastboot
│   ├── pack_boot_v2.py      # Pack Android boot.img v2
│   └── verify_boot_img.py   # Verify boot image
└── edk2-mt6765/             # <-- EDK2 + edk2-platforms source tree (NOT in repo)
    ├── edk2/
    ├── edk2-platforms/
    ├── MT6765Pkg/ -> ../uefi/MT6765Pkg
    └── BootShim/ -> ../uefi/BootShim
```

## One-Time Setup (Run on Linux host / WSL2 Ubuntu)

```bash
cd ~/hakios-g510

# 1. Install build dependencies
sudo apt-get update && sudo apt-get install -y \
    build-essential clang lld llvm iasl python3 git rsync \
    uuid-dev nasm acpica-tools

# 2. Install xpack AArch64 toolchain (optional - LLVM fallback works)
# Download from: https://github.com/xpack-dev-tools/aarch64-none-elf-gcc-xpack/releases
# Extract to ~/xpack-aarch64-none-elf-gcc-13.2.1-1.1

# 3. Clone EDK2 and edk2-platforms
git clone --recursive https://github.com/tianocore/edk2.git edk2-mt6765/edk2
git clone --recursive https://github.com/tianocore/edk2-platforms.git edk2-mt6765/edk2-platforms

# 4. Link our packages into EDK2 tree
cd edk2-mt6765
ln -sf ../../uefi/MT6765Pkg MT6765Pkg
ln -sf ../../uefi/BootShim BootShim

# 5. Build BaseTools
make -C edk2/BaseTools -j$(nproc)

# 6. Install LLVM/clang (if not using xpack GCC)
# LLVM 18+ required for Clang38 toolchain
sudo apt-get install -y clang-18 lld-18 llvm-18 llvm-18-dev
```

## Build UEFI Firmware

```bash
export G510_LLVM_BIN=/usr/lib/llvm-18/bin   # or wherever clang/lld are
export G510_IASL=/usr/bin/iasl              # or xpack toolchain/bin/iasl
# Optional: export G510_TOOLCHAIN=~/xpack-aarch64-none-elf-gcc-13.2.1-1.1

./scripts/build_uefi_g510.sh
# Outputs: out/MT6765PKG_UEFI.fd, out/BootShim.bin, out/UEFI (combined)
```

## Pack Boot Image (requires stock boot dump from phone)

```bash
# Phone in TWRP, ADB root:
./scripts/make_boot_g510.sh --dump

# Or use existing stock boot.img:
./scripts/make_boot_g510.sh --stock boot.img
```

## Flash

```bash
# TWRP/ADB:
IMG=out/boot-uefi-g510.img ./scripts/flash_g510.sh --adb

# Fastboot:
fastboot flash boot_a out/boot-uefi-g510.img
```

## Boot Arguments for G510 (from device.conf + A03s reference)

```
cpus=8 touchscreen=1 buttons=2 mtk_cpufreq=3 emmc_root=1
fbwidth=720 fbheight=1600 fbscale=2 fbfit=3
win_base=0xD0000000 win_size=0x2F000000
```

## G510 vs A03s Hardware Differences

| Component | A03s (SM-A037F) | G510 (GM 21 Plus) |
|-----------|----------------|-------------------|
| Framebuffer | 0x7C250000 | 0x7E250000 |
| Touchscreen | Chipone ICNL9911C (bit-banged SPI GPIO25-28) | Novatek NT36525 (SPI0) |
| Hardware Keys | On-screen Power/Vol | Keypad at 0x10010000 |
| Panel | 720x1600 | 720x1600 |
| SoC | MT6765 (Helio P35) | MT6765 (Helio G35) |
| DRAM | 4 GB | 4 GB |

## Next Steps for Kernel Integration

The UEFI firmware hands off to XNU via the XNUP payload. The XNU kernel needs:
1. Memory window at `0xD0000000` size `0x2F000000` (from DeviceMemoryMap.h HLOS 3)
2. Framebuffer driver for LK's fb_base at `0x7E250000`
3. Touch driver for Novatek NT36525 on SPI0
4. Keypad driver for keys at `0x10010000`
5. eMMC driver for `emmc_root=1` (system image on `xnusys` partition)
6. CONNSYS driver for Wi-Fi/BT (same MT6765 CONNSYS as A03s)
7. PowerVR GE8320 GPU driver (`pvr=1`)

The A03s kernel changes (documented in `ios-SM-A037F/kernel/README.md`) are a good reference for the MT6765-specific XNU modifications needed.
