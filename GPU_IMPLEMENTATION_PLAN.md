# PowerVR GE8320 GPU Driver Implementation Plan

**Target:** General Mobile GM 21 Plus (G510) — MediaTek MT6765 (Helio G35)
**GPU:** IMG PowerVR Rogue GE8320 (680 MHz, 2 shader clusters)
**Current State:** Stub implementation in `kernel/bsd/dev/g510_mtkpvrgpu.c`

---

## 🔴 Critical Difference: PowerVR vs Mali

| Aspect | Mali-G52 (MT6768) | PowerVR GE8320 (MT6765/G510) |
|--------|-------------------|-------------------------------|
| Firmware | **None needed** | **Required** (PVR firmware) |
| Architecture | Bifrost | Rogue (Series 8) |
| Register Base | 0x13040000 | 0x13040000 (same MFGCFG block) |
| Job Submission | Job Manager slots | TA/3D/CDM/TQ queues |
| MMU | Panfrost-style | PVR MMU (different) |
| Sync | Implicit fences | Explicit sync objects |

---

## 📋 Implementation Phases

### Phase 5A: PowerVR Firmware Loading (CRITICAL - Blocking)
PowerVR GPUs **require firmware** to be loaded before they can execute any commands. Unlike Mali, the GPU is non-functional without firmware.

**Firmware Files Needed (from device):**
- `pvr_rogue_fw.bin` — Main firmware
- `pvr_mmu_fw.bin` — MMU firmware (if separate)
- Located in `/fw/pvr/` or similar on device

**Implementation in `g510_mtkpvrgpu.c`:**
```c
// Add firmware loading function (similar to connsys_firmware_load)
static int g510_mtkpvrgpu_load_firmware(void) {
    // 1. Read firmware from /fw/pvr/*.bin via vnode
    // 2. Copy to EMI/physical memory (PVR_FW_BASE)
    // 3. Configure firmware boot address
    // 4. Start firmware processor
    // 5. Wait for firmware ready signal
}
```

**Register addresses (need verification from MediaTek docs):**
- PVR firmware boot vector: `PVR_REG_FW_BOOT` (likely 0x1000+)
- PVR firmware status: `PVR_REG_FW_STATUS`

---

### Phase 5B: Power Domain & Clock Bring-up (PARTIAL - DONE)
**Already implemented in stub:**
- Clock enable in TOPCKGEN (CLK_CFG_2)
- SPM power domains: MFG_ASYNC, MFG, MFG_CORE0, MFG_CORE1
- INFRACFG bus protection release
- MFGCFG clock gates and power control

**Needs verification against MT6765 datasheet:**
- Correct SPM domain register offsets for MT6765 (may differ from MT6768)
- PVR-specific clock muxes in TOPCKGEN

---

### Phase 5C: PowerVR Register Identification (NEEDS VERIFICATION)
**Current stub uses assumed register map:**
```c
#define PVR_REG_IDENT       0x0000
#define PVR_REG_STATUS      0x0004
#define PVR_REG_CTRL        0x0008
#define PVR_REG_SOFT_RESET  0x000C
#define PVR_REG_IRQ_RAW     0x0010
#define PVR_REG_IRQ_CLR     0x0014
#define PVR_REG_IRQ_EN      0x0018
#define PVR_REG_MMU_CTRL    0x0100
#define PVR_REG_MMU_PT_BASE 0x0104
#define PVR_REG_MMU_PT_SIZE 0x0108
#define PVR_REG_MMU_FLUSH   0x010C
#define PVR_REG_TA_BASE     0x0200
#define PVR_REG_TA_CTRL     0x0204
#define PVR_REG_TA_KICK     0x0208
#define PVR_REG_3D_BASE     0x0300
#define PVR_REG_3D_CTRL     0x0304
#define PVR_REG_3D_KICK     0x0308
#define PVR_REG_CDM_BASE    0x0400
#define PVR_REG_CDM_CTRL    0x0404
#define PVR_REG_CDM_KICK    0x0408
#define PVR_REG_TQ_BASE     0x0500
#define PVR_REG_TQ_CTRL     0x0504
#define PVR_REG_TQ_KICK     0x0508
```

**Action Required:** Verify against IMG PowerVR Rogue Series 8 TRM (Technical Reference Manual) or Linux `pvrsrvkm` driver.

---

### Phase 5D: Firmware Initialization Sequence
After firmware load, the GPU needs explicit initialization:

1. **Soft Reset** — Reset all GPU blocks (TA, 3D, CDM, TQ)
2. **Firmware Boot** — Write boot vector, start firmware CPU
3. **Wait for Firmware Ready** — Poll `PVR_REG_FW_STATUS` for ready bit
4. **MMU Initialization** — Set up page tables (see Phase 5E)
5. **Queue Initialization** — Configure TA/3D/CDM/TQ queues
6. **IRQ Setup** — Enable interrupts for job completion

---

### Phase 5E: MMU & Page Tables (CRITICAL FOR USERSPACE)
PowerVR Rogue uses a **two-level page table** (different from Mali's three-level):

| Level | Bits | Entries |
|-------|------|---------|
| Level 1 (PD) | 39:22 | 256 |
| Level 2 (PT) | 21:12 | 1024 |

**Page Table Entry Format (PVR):**
```
Bits 55:12  - Physical page address (44 bits)
Bits 11:10  - Reserved
Bit 9       - Access Flag (AF)
Bits 8:7    - Shareability (Inner Shareable = 0b11)
Bits 6:5    - AP[1:0] (RW = 0b01)
Bits 4:2    - AttrIndx (memory attribute)
Bits 1:0    - Type (0b11 = Page, 0b11 = Table for L1)
```

**Transcfg Register:** Configures page table walk (VA bits, page size, walk cacheability)

---

### Phase 5F: Job Submission (TA/3D/CDM/TQ)
PowerVR has **4 independent queues**:

| Queue | Purpose | Registers |
|-------|---------|-----------|
| **TA** (Tiler/Accelerator) | Vertex processing, tiling | `PVR_REG_TA_*` |
| **3D** (Fragment) | Pixel processing | `PVR_REG_3D_*` |
| **CDM** (Compute) | Compute shaders | `PVR_REG_CDM_*` |
| **TQ** (Transfer) | 2D blits, copies | `PVR_REG_TQ_*` |

**Job Structure (per queue):**
- Base address of job queue (ring buffer)
- Control register (enable, priority)
- Kick register (doorbell)
- Status register (job head/tail, errors)

**Sync Objects:** Each job can reference sync objects (fences) for dependency tracking.

---

### Phase 5G: Userspace Interface (IOKit Service)
Need to expose GPU to userspace via IOKit:

1. **Device:** `/dev/hakios-gpu` (or similar)
2. **Operations:**
   - Buffer allocation (GEM-like)
   - Buffer mapping (GPU VA ↔ CPU VA)
   - Job submission (ioctl)
   - Fence/sync creation & wait
   - Device info query (GPU ID, cores, features)

**Reference:** `redwood_mali.c` IOKit integration (`RedwoodMali.cpp` in userspace)

---

### Phase 5H: Mesa Panfrost vs Direct PVR Userspace
**Decision needed:**

| Option | Pros | Cons |
|--------|------|------|
| **Mesa Panfrost** | Reuses existing Gallium driver, OpenGL ES/Vulkan | Panfrost is for Mali, not PowerVR! |
| **Direct PVR Userspace** | Native PowerVR support | Need to port/implement PVR userspace (pvr_dri, libpvr) |
| **Custom Gallium** | Full control | Massive effort |

**Reality Check:** Panfrost **does not support PowerVR**. The hakios-metal driver is for Adreno (Qualcomm). For PowerVR, we need either:
- **IMG PowerVR Linux userspace** (proprietary, hard to get)
- **Custom minimal userspace** for our needs (compute + basic 2D/3D)

**Recommendation:** Start with **minimal custom userspace** (compute kernels via CDM, 2D via TQ), defer full 3D pipeline.

---

## 📝 Immediate Next Steps (Tonight)

### 1. Add Firmware Loading to `g510_mtkpvrgpu.c`
```c
// New function: g510_mtkpvrgpu_load_firmware()
// Reads from /fw/pvr/, copies to EMI, starts firmware
```

### 2. Verify Register Map
- Cross-reference with Linux `drivers/gpu/drm/pvr/` (if exists)
- Check IMG Rogue Series 8 TRM (if available)
- Use GPU probe to discover actual register layout

### 3. Add GPU Probe to mtkloader
Extend `tools/gpu-probe.sh` and `mtkloader/mtkpvr.h` (new file) to:
- Power up GPU domains
- Load firmware
- Read GPU_ID register
- Run test job

### 4. Update `drivers/gpu/conf/gpu.conf` (already correct)
### 5. Update `kernel/drivers/map` (already correct)

---

## 🔧 Required Hardware Info (Need from Device)

Run on phone (via mtkloader GPU probe or Android):
1. **PVR register base** — Confirm `0x13040000`
2. **GPU_ID value** — Expected GE8320: `0x8320xxxx`
3. **Firmware location** — Where Android stores PVR firmware
4. **Firmware load address** — EMI offset for firmware
5. **IRQ numbers** — GPU interrupt lines

---

## 📚 References

- **Linux PVR Driver:** `drivers/gpu/drm/pvr/` (kernel 6.x+)
- **IMG Rogue Series 8 TRM** — Register definitions
- **MediaTek MT6765 Datasheet** — SPM/TOPCKGEN/MFGCFG registers
- **redwood_mali.c** — Working Mali driver pattern to follow
- **hakios-metal** — Userspace driver pattern (but for Adreno)

---

## ⚠️ Blockers

1. **No PowerVR firmware in repo** — Must pull from device via `pull-firmware.sh` extension
2. **No TRM access** — Register map is guessed; need actual documentation
3. **Userspace gap** — No Mesa driver for PowerVR; custom userspace needed

---

## 🎯 Success Criteria

| Step | Verification |
|------|--------------|
| Firmware loads | `redwood_keep: PVR firmware loaded, FW status = READY` |
| GPU identified | `redwood_keep: GPU_ID = 0x8320xxxx (GE8320)` |
| MMU works | `redwood_keep: MMU initialized, page tables active` |
| Test job passes | `redwood_keep: test job passed (wrote 0xCAFEBABE)` |
| Userspace works | `hakios-metal-probe` or custom test runs compute kernel |

---

*Document created: 2026-11-14*  
*Next update: After firmware loading implementation*