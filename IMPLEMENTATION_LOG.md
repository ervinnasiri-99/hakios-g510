# HakiOS G510 Implementation Log

**Date:** 2026-11-15  
**Session:** PowerVR Firmware Extraction + Driver ELF Support

---

## 🎯 Summary of Changes

### 1. PowerVR Firmware Successfully Extracted from Device ✅

**Firmware Files Pulled:**
| Source (on device) | Destination (local) | Size | Type |
|-------------------|---------------------|------|------|
| `/vendor/firmware/rgx.fw` | `fw-pvr/pvr_rogue_fw.bin` | 114,688 bytes | ELF32 MIPS executable |
| `/vendor/firmware/rgx.sh` | `fw-pvr/pvr_mmu_fw.bin` | 309,200 bytes | Script/command file |

**Firmware Analysis:**
- `pvr_rogue_fw.bin`: **ELF 32-bit LSB executable, MIPS, MIPS32 rel2 version 1 (SYSV), statically linked, stripped**
- Entry point: `0xBFc00100` (from ELF header)
- Multiple PT_LOAD segments with vaddr/paddr mapping
- This is the actual PowerVR Rogue firmware for the MIPS firmware processor

### 2. Driver Updated for ELF32 Firmware Loading (`kernel/bsd/dev/g510_mtkpvrgpu.c`)

**Major Changes:**
- Added complete ELF32 header structures (`elf32_ehdr`, `elf32_phdr`)
- Implemented `g510_mtkpvrgpu_load_firmware()` to:
  1. Read firmware file via vnode from `/fw/pvr/pvr_rogue_fw.bin`
  2. Verify ELF magic (0x7F 'E' 'L' 'F')
  3. Verify 32-bit, little-endian, MIPS architecture (EM_MIPS = 8)
  4. Parse program headers for PT_LOAD segments
  5. Copy each LOAD segment to EMI at correct physical/virtual addresses
  6. Zero-fill BSS sections (where memsz > filesz)
  7. Configure firmware entry point via `PVR_REG_FW_BOOT_VEC`
  8. Configure firmware base address via `PVR_REG_FW_BOOT_ADDR`

**ELF Segments Expected (from firmware analysis):**
- Entry point: `0xBFc00100`
- Multiple PT_LOAD segments with varying vaddr/paddr
- Firmware will be loaded to EMI at `0x51200000` (PVR_FW_EMI_OFFSET)

### 3. Pull Script Updated (`pull-firmware.sh`)

- Added PVR_SRC array with actual device filenames (`rgx.fw`, `rgx.sh`)
- Renames to driver-expected names during pull (`pvr_rogue_fw.bin`, `pvr_mmu_fw.bin`)

### 4. Loader Stub Updated (`mtkloader/mtkpvr.h`)

- Added ELF32 structures for future loader-side implementation
- Kept legacy firmware header for backwards compatibility

---

## 🔴 Current Status

| Component | Status |
|-----------|--------|
| Firmware files | ✅ **Extracted and verified** |
| Driver ELF parsing | ✅ **Implemented** |
| Pull script | ✅ **Updated and tested** |
| Kernel build | ⏳ **Not yet tested** |
| Hardware probe | ⏳ **Not yet run** |
| Firmware loading on device | ⏳ **Needs kernel build + test** |

---

## 🎯 Next Steps (Priority Order)

### Immediate (Next Session):
1. **Build kernel** with updated driver:
   ```bash
   cd D:\xperiasp\hakios-g510
   XNU_SECS=300 XNU_ARGS="cpus=8 touchscreen=1 buttons=2 mtk_cpufreq=3 emmc_root=1 connsys_step=11 mtkbt_skel=1 audio=1 adsp=1 sound=4 mic=4 mali=1 sensors=1 camera=1 thermal=1 thermal_stop=105" \
       tools/xnu-cycle.sh
   ```

2. **Run PVR probe** to verify register addresses:
   ```bash
   ./tools/pvr-probe.sh
   ```

3. **Check kernel log** for firmware loading output:
   - `g510_mtkpvrgpu: loading firmware...`
   - `g510_mtkpvrgpu: ELF32 MIPS firmware, entry=0xXXXXXXXX, phoff=..., phnum=...`
   - `g510_mtkpvrgpu: loaded segment X: vaddr=0xXXXXXXXX paddr=0xXXXXXXXX filesz=... memsz=...`
   - `g510_mtkpvrgpu: firmware loaded to EMI (total XXXX bytes, entry=0xXXXXXXXX)`

### Then:
1. **Verify register addresses** from probe output
2. **Implement proper MMU page tables** (2-level AArch64)
3. **Initialize TA/3D/CDM/TQ queues**
4. **Add sync objects/fences**

---

## 📝 Notes

- The firmware is a **MIPS ELF32 executable** — this matches the PowerVR Rogue architecture which uses a MIPS-based firmware processor
- The `rgx.sh` file (309KB) appears to be a script/command sequence — may need investigation for MMU firmware or initialization commands
- EMI firmware load address: `0x51200000` (PVR_EMI_PHYS + PVR_FW_EMI_OFFSET)
- Boot arg for GPU: `mali=1` (legacy) or `pvr=1` (preferred)
- Firmware path in kernel: `/fw/pvr/pvr_rogue_fw.bin` (on xnusys partition)

---

## 🔗 Files Modified This Session

```
D:\xperiasp\hakios-g510\
├── kernel/bsd/dev/g510_mtkpvrgpu.c      # ELF32 firmware loading support
├── pull-firmware.sh                      # Updated for actual firmware filenames
├── mtkloader/mtkpvr.h                    # ELF32 structures for loader
├── fw-pvr/pvr_rogue_fw.bin              # NEW: Extracted firmware (114KB)
├── fw-pvr/pvr_mmu_fw.bin                # NEW: Extracted firmware script (309KB)
├── IMPLEMENTATION_LOG.md                # This file
└── STATUS.md                            # Updated project status
```

---

*End of log — 2026-11-15 11:15*