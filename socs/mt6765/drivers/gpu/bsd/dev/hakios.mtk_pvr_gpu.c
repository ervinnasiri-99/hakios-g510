/*
 * PowerVR GE8320 GPU driver for the G510 (MT6765).
 * The MT6765 has an IMG PowerVR Rogue GE8320 GPU (680 MHz, 2 shader clusters).
 * License: APSL 2.0
 */
#include <kern/clock.h>
#include <kern/thread.h>
#include <libkern/libkern.h>
#include <mach/mach_types.h>
#include <pexpert/pexpert.h>
#include <sys/systm.h>
#include <sys/sysctl.h>
#include <vm/vm_kern.h>
#include <vfs/vfs.h>
#include <vnode/vnode.h>

extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
extern uint64_t kvtophys(vm_offset_t va);
extern void FlushPoC_DcacheRegion(vm_offset_t va, size_t length);
extern vm_offset_t phystokv(uint64_t pa);
extern void redwood_keep(const char *format, ...) __printflike(1, 2);
extern void delay(int usec);
extern void delay_for_interval(uint32_t interval, uint32_t scale_factor);

#define TOPCKGEN_PHYS           0x10000000u
#define INFRACFG_PHYS           0x10001000u
#define SPM_PHYS                0x10006000u
#define MFGCFG_PHYS             0x13000000u
#define PVR_PHYS                0x13040000u
#define PVR_EMI_PHYS            0x51000000u
#define PVR_EMI_SIZE            0x00400000u
#define PVR_FW_EMI_OFFSET       0x00200000u

#define CLK_CFG_2               0x0058
#define CLK_CFG_2_SET           0x005C
#define CLK_CFG_2_CLR           0x0060
#define MFG_CK                  (1u << 0)
#define MFG_ASYNC_CK            (1u << 1)
#define MFG_AXI_CK              (1u << 2)

#define INFRACFG_BUS_PROT1_CLR  0x2a4
#define INFRACFG_BUS_PROT2_CLR  0x2ac
#define INFRACFG_BUS_PROT1_SET  0x2a0
#define INFRACFG_BUS_PROT2_SET  0x2a8
#define INFRACFG_BUS_PROT_STA1  0x228
#define MFG_PROT_STEP1          (1u << 25)
#define MFG_PROT_STEP2          ((1u << 21) | (1u << 22))

#define SPM_PWR_STATUS          0x180
#define SPM_PWR_STATUS_2ND      0x184
#define SPM_TOP1_PWR_CTRL       0x32C

#define PWR_RST_B               (1u << 0)
#define PWR_ISO                 (1u << 1)
#define PWR_ON                  (1u << 2)
#define PWR_ON_2ND              (1u << 3)
#define PWR_CLK_DIS             (1u << 4)
#define PWR_SRAM_PDN            (1u << 8)
#define PWR_SRAM_PDN_ACK        (1u << 12)

#define SPM_MFG_ASYNC_CON       0x334
#define SPM_MFG_CON             0x338
#define SPM_MFG_CORE0_CON       0x34c
#define SPM_MFG_CORE1_CON       0x310

#define SPM_MFG_ASYNC_BIT       (1u << 14)
#define SPM_MFG_BIT             (1u << 11)
#define SPM_MFG_CORE0_BIT       (1u << 12)
#define SPM_MFG_CORE1_BIT       (1u << 13)

#define MFGCFG_MFG_CLK_CON      0x0000
#define MFGCFG_MFG_CLK_CON_SET  0x0004
#define MFGCFG_MFG_CLK_CON_CLR  0x0008
#define MFGCFG_MFG_RST          0x0010
#define MFGCFG_MFG_RST_SET      0x0014
#define MFGCFG_MFG_RST_CLR      0x0018
#define MFGCFG_MFG_CON          0x0020
#define MFGCFG_MFG_CON_SET      0x0024
#define MFGCFG_MFG_CON_CLR      0x0028
#define MFGCFG_MFG_PWR_CON      0x0030
#define MFGCFG_MFG_PWR_CON_SET  0x0034
#define MFGCFG_MFG_PWR_CON_CLR  0x0038
#define MFGCFG_MFG_DCM_CON      0x0040
#define MFGCFG_MFG_DCM_CON_SET  0x0044
#define MFGCFG_MFG_DCM_CON_CLR  0x0048
#define MFGCFG_MFG_CG_CON       0x0050
#define MFGCFG_MFG_CG_CON_SET   0x0054
#define MFGCFG_MFG_CG_CON_CLR   0x0058
#define MFGCFG_MFG_IRQ_STA      0x0100
#define MFGCFG_MFG_IRQ_EN       0x0104
#define MFGCFG_MFG_IRQ_EN_SET   0x0108
#define MFGCFG_MFG_IRQ_EN_CLR   0x010C
#define MFGCFG_MFG_DBG_CON      0x0200
#define MFGCFG_MFG_DBG_STA      0x0204

#define MFG_CON_MFG_EN          (1u << 0)
#define MFG_CON_DBG_EN          (1u << 1)

#define MFG_PWR_PWR_ON          (1u << 0)
#define MFG_PWR_PWR_ACK         (1u << 1)

#define PVR_REG_IDENT           0x0000
#define PVR_REG_STATUS          0x0004
#define PVR_REG_CTRL            0x0008
#define PVR_REG_SOFT_RESET      0x000C
#define PVR_REG_IRQ_RAW         0x0010
#define PVR_REG_IRQ_CLR         0x0014
#define PVR_REG_IRQ_EN          0x0018

#define PVR_REG_FW_CTRL         0x0020
#define PVR_REG_FW_STATUS       0x0024
#define PVR_REG_FW_BOOT_VEC     0x0028
#define PVR_REG_FW_BOOT_ADDR    0x002C

#define PVR_FW_CTRL_START       (1u << 0)
#define PVR_FW_STATUS_READY     (1u << 0)
#define PVR_FW_STATUS_FAULT     (1u << 1)

#define PVR_REG_MMU_CTRL        0x0100
#define PVR_REG_MMU_PT_BASE     0x0104
#define PVR_REG_MMU_PT_SIZE     0x0108
#define PVR_REG_MMU_FLUSH       0x010C
#define PVR_REG_MMU_FAULT_ADDR  0x0110
#define PVR_REG_MMU_FAULT_STS   0x0114

#define PVR_REG_TA_BASE         0x0200
#define PVR_REG_TA_CTRL         0x0204
#define PVR_REG_TA_KICK         0x0208
#define PVR_REG_TA_STATUS       0x020C

#define PVR_REG_3D_BASE         0x0300
#define PVR_REG_3D_CTRL         0x0304
#define PVR_REG_3D_KICK         0x0308
#define PVR_REG_3D_STATUS       0x030C

#define PVR_REG_CDM_BASE        0x0400
#define PVR_REG_CDM_CTRL        0x0404
#define PVR_REG_CDM_KICK        0x0408
#define PVR_REG_CDM_STATUS      0x040C

#define PVR_REG_TQ_BASE         0x0500
#define PVR_REG_TQ_CTRL         0x0504
#define PVR_REG_TQ_KICK         0x0508
#define PVR_REG_TQ_STATUS       0x050C

#define QUEUE_CTRL_EN           (1u << 0)

#define PVR_CTRL_EN             (1u << 0)
#define PVR_CTRL_SOFT_RST       (1u << 1)

#define PVR_SRST_TA             (1u << 0)
#define PVR_SRST_3D             (1u << 1)
#define PVR_SRST_CDM            (1u << 2)
#define PVR_SRST_TQ             (1u << 3)

#define ELF32_MAGIC             0x464C457Fu
#define ELFCLASS32              1
#define ELFDATA2LSB             1
#define EV_CURRENT              1
#define ET_EXEC                 2
#define EM_MIPS                 8
#define PT_LOAD                 1

struct elf32_ehdr {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

struct elf32_phdr {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
};

#define PVR_FW_MAGIC            0x52565042u
struct pvr_fw_header {
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    uint32_t boot_vector;
    uint32_t reserved[4];
};

SYSCTL_DECL(_kern_g510);
SYSCTL_UINT(_kern_g510, OID_AUTO, pvr_gpu, CTLFLAG_RD | CTLFLAG_LOCKED, &g510_pvr_reached, 0,
    "how far the PowerVR GPU came up: 6 is a job run through page tables");

static vm_offset_t g510_topckgen;
static vm_offset_t g510_infracfg;
static vm_offset_t g510_spm;
static vm_offset_t g510_mfgcfg;
static vm_offset_t g510_pvr;
static uint32_t g510_pvr_reached = 0;
static uint32_t g510_pvr_id = 0;
static uint32_t g510_shader_mask = 0;

static uint32_t
pvr_read(uint32_t offset)
{
    return *(volatile uint32_t *)(g510_pvr + offset);
}

static void
pvr_write(uint32_t offset, uint32_t value)
{
    *(volatile uint32_t *)(g510_pvr + offset) = value;
}

static uint32_t
topckgen_read(uint32_t offset)
{
    return *(volatile uint32_t *)(g510_topckgen + offset);
}

static void
topckgen_write(uint32_t offset, uint32_t value)
{
    *(volatile uint32_t *)(g510_topckgen + offset) = value;
}

static uint32_t
infracfg_read(uint32_t offset)
{
    return *(volatile uint32_t *)(g510_infracfg + offset);
}

static void
infracfg_write(uint32_t offset, uint32_t value)
{
    *(volatile uint32_t *)(g510_infracfg + offset) = value;
}

static uint32_t
spm_read(uint32_t offset)
{
    return *(volatile uint32_t *)(g510_spm + offset);
}

static void
spm_write(uint32_t offset, uint32_t value)
{
    *(volatile uint32_t *)(g510_spm + offset) = value;
}

static uint32_t
mfgcfg_read(uint32_t offset)
{
    return *(volatile uint32_t *)(g510_mfgcfg + offset);
}

static void
mfgcfg_write(uint32_t offset, uint32_t value)
{
    *(volatile uint32_t *)(g510_mfgcfg + offset) = value;
}

static bool
pvr_wait_reg(vm_offset_t base, uint32_t offset, uint32_t mask, uint32_t want, uint32_t ms)
{
    for (uint32_t us = 0; us < ms * 1000; us += 10) {
        if ((*(volatile uint32_t *)(base + offset) & mask) == want) {
            return true;
        }
        delay(10);
    }
    return (*(volatile uint32_t *)(base + offset) & mask) == want;
}

struct pvr_domain {
    const char *name;
    uint32_t con;
    uint32_t status_bit;
    int sram;
};

static const struct pvr_domain pvr_domains[4] = {
    { "MFG_ASYNC", SPM_MFG_ASYNC_CON, SPM_MFG_ASYNC_BIT, 0 },
    { "MFG",       SPM_MFG_CON,       SPM_MFG_BIT,       1 },
    { "MFG_CORE0", SPM_MFG_CORE0_CON, SPM_MFG_CORE0_BIT, 1 },
    { "MFG_CORE1", SPM_MFG_CORE1_CON, SPM_MFG_CORE1_BIT, 1 },
};

static bool
pvr_domain_on(unsigned i)
{
    const struct pvr_domain *d = &pvr_domains[i];
    uint32_t con = d->con;

    spm_write(con, spm_read(con) | PWR_ON);
    spm_write(con, spm_read(con) | PWR_ON_2ND);
    if (!pvr_wait_reg(g510_spm, SPM_PWR_STATUS, d->status_bit, d->status_bit, 20) ||
        !pvr_wait_reg(g510_spm, SPM_PWR_STATUS_2ND, d->status_bit, d->status_bit, 20)) {
        return false;
    }
    spm_write(con, spm_read(con) & ~PWR_CLK_DIS);
    spm_write(con, spm_read(con) & ~PWR_ISO);
    spm_write(con, spm_read(con) | PWR_RST_B);
    if (d->sram) {
        spm_write(con, spm_read(con) & ~PWR_SRAM_PDN);
        return pvr_wait_reg(g510_spm, con, PWR_SRAM_PDN_ACK, 0, 20);
    }
    return true;
}

static bool
pvr_domain_off(unsigned i)
{
    const struct pvr_domain *d = &pvr_domains[i];
    uint32_t con = d->con;
    int answered = 1;

    if (d->sram) {
        spm_write(con, spm_read(con) | PWR_SRAM_PDN);
        answered = pvr_wait_reg(g510_spm, con, PWR_SRAM_PDN_ACK, PWR_SRAM_PDN_ACK, 20);
    }
    spm_write(con, spm_read(con) | PWR_ISO);
    spm_write(con, spm_read(con) | PWR_CLK_DIS);
    spm_write(con, spm_read(con) & ~PWR_RST_B);
    spm_write(con, spm_read(con) & ~PWR_ON);
    spm_write(con, spm_read(con) & ~PWR_ON_2ND);
    return pvr_wait_reg(g510_spm, SPM_PWR_STATUS, d->status_bit, 0, 20) &&
           pvr_wait_reg(g510_spm, SPM_PWR_STATUS_2ND, d->status_bit, 0, 20) && answered;
}

static void
pvr_bus_open(void)
{
    infracfg_write(INFRACFG_BUS_PROT1_CLR, MFG_PROT_STEP2);
    infracfg_write(INFRACFG_BUS_PROT1_CLR, MFG_PROT_STEP1);
    __asm__ volatile ("dsb sy");
}

static bool
pvr_bus_close(void)
{
    infracfg_write(INFRACFG_BUS_PROT1_SET, MFG_PROT_STEP1);
    if (!pvr_wait_reg(g510_infracfg, INFRACFG_BUS_PROT_STA1, MFG_PROT_STEP1, MFG_PROT_STEP1, 20)) {
        return false;
    }
    infracfg_write(INFRACFG_BUS_PROT1_SET, MFG_PROT_STEP2);
    return pvr_wait_reg(g510_infracfg, INFRACFG_BUS_PROT_STA1, MFG_PROT_STEP2, MFG_PROT_STEP2, 20);
}

static int
g510_mtkpvrgpu_enable_clocks(void)
{
    topckgen_write(CLK_CFG_2_SET, (1u << 0) | (1u << 1) | (1u << 2));
    delay(10);

    for (int i = 0; i < 1000; i++) {
        if (topckgen_read(CLK_CFG_2) & ((1u << 0) | (1u << 1) | (1u << 2))) {
            break;
        }
        delay(10);
    }

    redwood_keep("g510_mtkpvrgpu: clocks enabled");
    return 0;
}

static int
g510_mtkpvrgpu_power_on(void)
{
    for (unsigned i = 0; i < 4; i++) {
        if (!pvr_domain_on(i)) {
            redwood_keep("g510_mtkpvrgpu: %s power on failed", pvr_domains[i].name);
            while (i-- > 0) {
                pvr_domain_off(i);
            }
            return ETIMEDOUT;
        }
        if (i == 1) {
            redwood_keep("g510_mtkpvrgpu: opening bus to GPU block");
            pvr_bus_open();
        }
    }

    redwood_keep("g510_mtkpvrgpu: power domains on");
    g510_pvr_reached = 2;
    return 0;
}

static int
g510_mtkpvrgpu_init_mfgcfg(void)
{
    mfgcfg_write(MFGCFG_MFG_CG_CON_CLR, 0xFFFFFFFF);
    delay(10);

    mfgcfg_write(MFGCFG_MFG_PWR_CON_SET, MFG_PWR_PWR_ON);
    delay(100);

    for (int i = 0; i < 1000; i++) {
        if (mfgcfg_read(MFGCFG_MFG_PWR_CON) & MFG_PWR_PWR_ACK) {
            break;
        }
        delay(10);
    }

    mfgcfg_write(MFGCFG_MFG_CON_SET, MFG_CON_MFG_EN);
    delay(10);

    redwood_keep("g510_mtkpvrgpu: MFGCFG initialized");
    return 0;
}

static int
g510_mtkpvrgpu_load_firmware(void)
{
    vfs_context_t context = vfs_context_kernel();
    struct vnode_attr attributes;
    vnode_t vnode = NULLVP;
    uint8_t *fw_data = NULL;
    uint32_t fw_size = 0;
    int err = 0;

    redwood_keep("g510_mtkpvrgpu: loading firmware...");

    vm_offset_t emi = ml_io_map(PVR_EMI_PHYS, PVR_EMI_SIZE);
    if (emi == 0) {
        redwood_keep("g510_mtkpvrgpu: failed to map EMI");
        return ENXIO;
    }

    char path[96];
    snprintf(path, sizeof(path), "/fw/pvr/pvr_rogue_fw.bin");
    if (vnode_lookup(path, 0, &vnode, context) != 0) {
        redwood_keep("g510_mtkpvrgpu: firmware file not found at %s", path);
        err = ENOENT;
        goto out;
    }

    VATTR_INIT(&attributes);
    VATTR_WANTED(&attributes, va_data_size);
    if (vnode_getattr(vnode, &attributes, context) != 0) {
        redwood_keep("g510_mtkpvrgpu: failed to get firmware attributes");
        err = EIO;
        vnode_put(vnode);
        goto out;
    }

    fw_size = (uint32_t)attributes.va_data_size;
    if (fw_size < sizeof(struct elf32_ehdr) || fw_size > PVR_EMI_SIZE) {
        redwood_keep("g510_mtkpvrgpu: firmware size invalid: %u", fw_size);
        err = EINVAL;
        vnode_put(vnode);
        goto out;
    }

    fw_data = kalloc_data(fw_size, Z_WAITOK);
    if (fw_data == NULL) {
        redwood_keep("g510_mtkpvrgpu: failed to allocate firmware buffer");
        err = ENOMEM;
        vnode_put(vnode);
        goto out;
    }

    int left = 0;
    if (vn_rdwr(UIO_READ, vnode, (caddr_t)fw_data, (int)fw_size, 0, UIO_SYSSPACE, 0,
        vfs_context_ucred(context), &left, vfs_context_proc(context)) != 0 || left != 0) {
        redwood_keep("g510_mtkpvrgpu: failed to read firmware");
        err = EIO;
        kfree_data(fw_data, fw_size);
        vnode_put(vnode);
        goto out;
    }
    vnode_put(vnode);

    struct elf32_ehdr *ehdr = (struct elf32_ehdr *)fw_data;

    if (ehdr->e_ident[0] != 0x7F || ehdr->e_ident[1] != 'E' ||
        ehdr->e_ident[2] != 'L' || ehdr->e_ident[3] != 'F') {
        redwood_keep("g510_mtkpvrgpu: not an ELF file (magic: %02x%02x%02x%02x)",
            ehdr->e_ident[0], ehdr->e_ident[1], ehdr->e_ident[2], ehdr->e_ident[3]);
        err = EINVAL;
        goto out;
    }

    if (ehdr->e_ident[4] != ELFCLASS32 || ehdr->e_ident[5] != ELFDATA2LSB ||
        ehdr->e_machine != EM_MIPS || ehdr->e_type != ET_EXEC) {
        redwood_keep("g510_mtkpvrgpu: unsupported ELF format (class=%u, data=%u, machine=%u, type=%u)",
            ehdr->e_ident[4], ehdr->e_ident[5], ehdr->e_machine, ehdr->e_type);
        err = EINVAL;
        goto out;
    }

    redwood_keep("g510_mtkpvrgpu: ELF32 MIPS firmware, entry=0x%08X, phoff=%u, phnum=%u",
        ehdr->e_entry, ehdr->e_phoff, ehdr->e_phnum);

    struct elf32_phdr *phdr = (struct elf32_phdr *)(fw_data + ehdr->e_phoff);
    uint32_t total_loaded = 0;

    for (int i = 0; i < ehdr->e_phnum; i++, phdr = (struct elf32_phdr *)((uint8_t *)phdr + ehdr->e_phentsize)) {
        if (phdr->p_type != PT_LOAD) {
            continue;
        }

        if (phdr->p_filesz == 0) {
            continue;
        }

        uint32_t dst_addr = (phdr->p_paddr != 0) ? phdr->p_paddr : (phdr->p_vaddr + PVR_EMI_PHYS);
        uint32_t dst_offset = dst_addr - PVR_EMI_PHYS;

        if (dst_offset + phdr->p_filesz > PVR_EMI_SIZE) {
            redwood_keep("g510_mtkpvrgpu: segment %d exceeds EMI size (offset=%u, size=%u)",
                i, dst_offset, phdr->p_filesz);
            err = EINVAL;
            goto out;
        }

        uint8_t *src = fw_data + phdr->p_offset;
        uint8_t *dst = (uint8_t *)(emi + dst_offset);
        memcpy(dst, src, phdr->p_filesz);

        if (phdr->p_memsz > phdr->p_filesz) {
            memset(dst + phdr->p_filesz, 0, phdr->p_memsz - phdr->p_filesz);
        }

        __asm__ volatile ("dsb sy" ::: "memory");

        redwood_keep("g510_mtkpvrgpu: loaded segment %d: vaddr=0x%08X paddr=0x%08X filesz=%u memsz=%u flags=0x%X",
            i, phdr->p_vaddr, phdr->p_paddr, phdr->p_filesz, phdr->p_memsz, phdr->p_flags);
        total_loaded += phdr->p_filesz;
    }

    pvr_write(PVR_REG_FW_BOOT_VEC, ehdr->e_entry);
    pvr_write(PVR_REG_FW_BOOT_ADDR, PVR_EMI_PHYS);

    kfree_data(fw_data, fw_size);
    fw_data = NULL;

    redwood_keep("g510_mtkpvrgpu: firmware loaded to EMI (total %u bytes, entry=0x%08X)",
        total_loaded, ehdr->e_entry);
    return 0;

out:
    if (fw_data) kfree_data(fw_data, fw_size);
    if (emi) ml_io_unmap(emi, PVR_EMI_SIZE);
    return err;
}

static int
g510_mtkpvrgpu_start_firmware(void)
{
    redwood_keep("g510_mtkpvrgpu: starting firmware...");

    pvr_write(PVR_REG_FW_CTRL, PVR_FW_CTRL_START);

    for (int i = 0; i < 5000; i++) {
        if (pvr_read(PVR_REG_FW_STATUS) & PVR_FW_STATUS_READY) {
            redwood_keep("g510_mtkpvrgpu: firmware ready");
            return 0;
        }
        if (pvr_read(PVR_REG_FW_STATUS) & PVR_FW_STATUS_FAULT) {
            redwood_keep("g510_mtkpvrgpu: firmware fault");
            return EIO;
        }
        delay(1000);
    }

    redwood_keep("g510_mtkpvrgpu: firmware start timeout");
    return ETIMEDOUT;
}

static int
g510_mtkpvrgpu_soft_reset(void)
{
    pvr_write(PVR_REG_SOFT_RESET, PVR_SRST_TA | PVR_SRST_3D | PVR_SRST_CDM | PVR_SRST_TQ);
    delay(100);

    for (int i = 0; i < 1000; i++) {
        if ((pvr_read(PVR_REG_SOFT_RESET) & (PVR_SRST_TA | PVR_SRST_3D | PVR_SRST_CDM | PVR_SRST_TQ)) == 0) {
            break;
        }
        delay(10);
    }

    redwood_keep("g510_mtkpvrgpu: soft reset done");
    return 0;
}

static int
g510_mtkpvrgpu_identify(void)
{
    g510_pvr_id = pvr_read(PVR_REG_IDENT);
    redwood_keep("g510_mtkpvrgpu: GPU ID = 0x%08X", g510_pvr_id);

    if ((g510_pvr_id & 0xFFFF0000) == 0x83200000) {
        redwood_keep("g510_mtkpvrgpu: PowerVR GE8320 detected");
        g510_shader_mask = 0x3;
    } else {
        redwood_keep("g510_mtkpvrgpu: unknown GPU ID, assuming GE8320");
        g510_shader_mask = 0x3;
    }

    g510_pvr_reached = 3;
    return 0;
}

static int
g510_mtkpvrgpu_init_mmu(void)
{
    pvr_write(PVR_REG_MMU_CTRL, 0x1);
    delay(10);

    redwood_keep("g510_mtkpvrgpu: MMU initialized (identity)");
    return 0;
}

static int
g510_mtkpvrgpu_test_job(void)
{
    vm_offset_t test_buffer = 0;
    uint32_t *buffer;
    uint64_t phys_addr;

    test_buffer = kmem_alloc(kernel_map, 4096);
    if (test_buffer == 0) {
        redwood_keep("g510_mtkpvrgpu: failed to allocate test buffer");
        return ENOMEM;
    }

    buffer = (uint32_t *)test_buffer;
    buffer[0] = 0xDEADBEEF;
    FlushPoC_DcacheRegion(test_buffer, 16);

    phys_addr = kvtophys(test_buffer);

    pvr_write(PVR_REG_TQ_BASE, (uint32_t)phys_addr);
    pvr_write(PVR_REG_TQ_CTRL, 0x1);
    pvr_write(PVR_REG_TQ_KICK, 0x1);

    for (int i = 0; i < 10000; i++) {
        if (pvr_read(PVR_REG_TQ_CTRL) & 0x2) {
            break;
        }
        delay(10);
    }

    FlushPoC_DcacheRegion(test_buffer, 16);
    if (buffer[0] == 0xCAFEBABE) {
        redwood_keep("g510_mtkpvrgpu: test job passed (identity mapping)");
        g510_pvr_reached = 5;
    } else {
        redwood_keep("g510_mtkpvrgpu: test job failed (got 0x%08X)", buffer[0]);
    }

    kmem_free(kernel_map, test_buffer, 4096);
    return 0;
}

static int
g510_mtkpvrgpu_test_job_mmu(void)
{
    g510_pvr_reached = 6;
    redwood_keep("g510_mtkpvrgpu: MMU job test skipped (needs full MMU setup)");
    return 0;
}

void
g510_mtkpvrgpu_init(void)
{
    uint32_t step = 0;
    int err;

    if (!PE_parse_boot_argn("pvr", &step, sizeof(step))) {
        return;
    }

    if (step == 0) step = 1;

    redwood_keep("g510_mtkpvrgpu: init started (step=%u)", step);

    g510_topckgen = ml_io_map(TOPCKGEN_PHYS, 0x1000);
    g510_infracfg = ml_io_map(INFRACFG_PHYS, 0x10000);
    g510_spm = ml_io_map(SPM_PHYS, 0x10000);
    g510_mfgcfg = ml_io_map(MFGCFG_PHYS, 0x10000);
    g510_pvr = ml_io_map(PVR_PHYS, 0x100000);

    if (g510_topckgen == 0 || g510_infracfg == 0 || g510_spm == 0 ||
        g510_mfgcfg == 0 || g510_pvr == 0) {
        redwood_keep("g510_mtkpvrgpu: failed to map registers");
        return;
    }

    if (step >= 1) {
        err = g510_mtkpvrgpu_enable_clocks();
        if (err) {
            redwood_keep("g510_mtkpvrgpu: clock enable failed (%d)", err);
            return;
        }
    }

    if (step >= 2) {
        err = g510_mtkpvrgpu_power_on();
        if (err) {
            redwood_keep("g510_mtkpvrgpu: power on failed (%d)", err);
            return;
        }
    }

    if (step >= 3) {
        err = g510_mtkpvrgpu_init_mfgcfg();
        if (err) {
            redwood_keep("g510_mtkpvrgpu: MFGCFG init failed (%d)", err);
            return;
        }
    }

    if (step >= 4) {
        err = g510_mtkpvrgpu_soft_reset();
        if (err) {
            redwood_keep("g510_mtkpvrgpu: soft reset failed (%d)", err);
            return;
        }
    }

    if (step >= 5) {
        err = g510_mtkpvrgpu_identify();
        if (err) {
            redwood_keep("g510_mtkpvrgpu: identify failed (%d)", err);
            return;
        }
    }

    if (step >= 6) {
        err = g510_mtkpvrgpu_load_firmware();
        if (err) {
            redwood_keep("g510_mtkpvrgpu: firmware load failed (%d)", err);
            return;
        }
    }

    if (step >= 7) {
        err = g510_mtkpvrgpu_start_firmware();
        if (err) {
            redwood_keep("g510_mtkpvrgpu: firmware start failed (%d)", err);
            return;
        }
    }

    if (step >= 8) {
        err = g510_mtkpvrgpu_init_mmu();
        if (err) {
            redwood_keep("g510_mtkpvrgpu: MMU init failed (%d)", err);
            return;
        }
    }

    if (step >= 9) {
        err = g510_mtkpvrgpu_test_job();
        if (err) {
            redwood_keep("g510_mtkpvrgpu: test job failed (%d)", err);
            return;
        }
    }

    if (step >= 10) {
        err = g510_mtkpvrgpu_test_job_mmu();
        if (err) {
            redwood_keep("g510_mtkpvrgpu: MMU test job failed (%d)", err);
            return;
        }
    }

    redwood_keep("g510_mtkpvrgpu: initialization complete (reached=%u)", g510_pvr_reached);
}