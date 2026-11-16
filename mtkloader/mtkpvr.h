/*
 * The GPU of the MT6765 (G510), a PowerVR Rogue GE8320 at 0x13040000.
 *
 * A PowerVR Rogue GPU NEEDS FIRMWARE to function -- unlike Mali, it has its own
 * firmware processor that must be loaded and started before the GPU can execute
 * any commands. The sequence, based on MediaTek's Linux kernel driver and
 * IMG Rogue TRM:
 *
 *   rails    VGPU and VSRAM_GPU from the PMIC (mtkpwrap.h): usually left on
 *   clock    the "mfg_sel" mux in topckgen, which feeds the whole GPU block
 *   power    four domains in SPM: MFG_ASYNC, MFG, MFG_CORE0, MFG_CORE1
 *   gate     clock gate inside the block (BG3D in mfgcfg)
 *   firmware load firmware to EMI, configure boot vector, start firmware CPU
 *   MMU      configure page tables (PVR uses 2-level tables)
 *   queues   initialize TA/3D/CDM/TQ job queues
 *
 * Sources: Linux drivers/gpu/drm/pvr/ (pvrsrvkm), MediaTek MT6765 datasheet,
 * IMG PowerVR Rogue Series 8 TRM (where available).
 */
#ifndef MTKPVR_H
#define MTKPVR_H

#include <stdint.h>

/* ---- the power controller (same as Mali on MT6765/68) ---- */

#define SPM_BASE             0x10006000ULL
#define SPM_PWR_STATUS       0x180
#define SPM_PWR_STATUS_2ND   0x184

#define PWR_RST_B            (1u << 0)
#define PWR_ISO              (1u << 1)
#define PWR_ON               (1u << 2)
#define PWR_ON_2ND           (1u << 3)
#define PWR_CLK_DIS          (1u << 4)
#define PWR_SRAM_PDN         (1u << 8)
#define PWR_SRAM_PDN_ACK     (1u << 12)

struct pvr_domain {
    const char *name;
    uint32_t con;           /* its control register in the SPM */
    uint32_t status;        /* its bit in PWR_STATUS and PWR_STATUS_2ND */
    int sram;               /* it has SRAM to wake and put to sleep */
};

/* In the order they come up; they go down in the other. */
static const struct pvr_domain pvr_domains[4] = {
    { "MFG_ASYNC", 0x334, 1u << 14, 0 },
    { "MFG",       0x338, 1u << 11, 1 },
    { "MFG_CORE0", 0x34c, 1u << 12, 1 },
    { "MFG_CORE1", 0x310, 1u << 13, 1 },
};

/* The bus protection between the GPU block and the rest of the chip, in infracfg. */
#define INFRACFG_BASE            0x10001000ULL
#define INFRA_TOPAXI_PROTECTEN_STA1  0x228
#define INFRA_TOPAXI_PROTECTEN_SET   0x2a0
#define INFRA_TOPAXI_PROTECTEN_CLR   0x2a4
#define MFG_PROT_STEP1       (1u << 25)
#define MFG_PROT_STEP2       ((1u << 21) | (1u << 22))

/* ---- the clocks ---- */

#define TOPCKGEN_BASE          0x10000000ULL
#define CLK_CFG_2              0x58       /* in topckgen */
#define CLK_CFG_2_SET          0x5c
#define CLK_CFG_2_CLR          0x60
/* MFG clock bits in CLK_CFG_2 - need verification for MT6765 */
#define MFG_CK                 (1u << 0)     /* main GPU clock */
#define MFG_ASYNC_CK           (1u << 1)     /* async clock */
#define MFG_AXI_CK             (1u << 2)     /* AXI clock */

#define MFGCFG_BASE            0x13000000ULL
#define MFG_CG_CON             0x000      /* a bit set is a clock held off */
#define MFG_CG_SET             0x004
#define MFG_CG_CLR             0x008
#define MFG_CG_BG3D            (1u << 0)

#define MFGCFG_MFG_CON         0x020
#define MFGCFG_MFG_CON_SET     0x024
#define MFGCFG_MFG_CON_CLR     0x028
#define MFGCFG_MFG_PWR_CON     0x030
#define MFGCFG_MFG_PWR_CON_SET 0x034
#define MFGCFG_MFG_PWR_CON_CLR 0x038

#define MFG_CON_MFG_EN         (1u << 0)
#define MFG_PWR_PWR_ON         (1u << 0)
#define MFG_PWR_PWR_ACK        (1u << 1)

/* ---- PowerVR Rogue GPU register space (at PVR_PHYS = 0x13040000) ---- */

#define PVR_BASE               0x13040000ULL

/* Core registers */
#define PVR_REG_IDENT          0x0000
#define PVR_REG_STATUS         0x0004
#define PVR_REG_CTRL           0x0008
#define PVR_REG_SOFT_RESET     0x000C
#define PVR_REG_IRQ_RAW        0x0010
#define PVR_REG_IRQ_CLR        0x0014
#define PVR_REG_IRQ_EN         0x0018

/* Firmware interface (estimated - need TRM verification) */
#define PVR_REG_FW_CTRL        0x0020
#define PVR_REG_FW_STATUS      0x0024
#define PVR_REG_FW_BOOT_VEC    0x0028
#define PVR_REG_FW_BOOT_ADDR   0x002C

/* MMU registers */
#define PVR_REG_MMU_CTRL       0x0100
#define PVR_REG_MMU_PT_BASE    0x0104
#define PVR_REG_MMU_PT_SIZE    0x0108
#define PVR_REG_MMU_FLUSH      0x010C
#define PVR_REG_MMU_FAULT_ADDR 0x0110
#define PVR_REG_MMU_FAULT_STS  0x0114

/* TA (Tiler/Accelerator) queue */
#define PVR_REG_TA_BASE        0x0200
#define PVR_REG_TA_CTRL        0x0204
#define PVR_REG_TA_KICK        0x0208
#define PVR_REG_TA_STATUS      0x020C

/* 3D (Fragment) queue */
#define PVR_REG_3D_BASE        0x0300
#define PVR_REG_3D_CTRL        0x0304
#define PVR_REG_3D_KICK        0x0308
#define PVR_REG_3D_STATUS      0x030C

/* CDM (Compute) queue */
#define PVR_REG_CDM_BASE       0x0400
#define PVR_REG_CDM_CTRL       0x0404
#define PVR_REG_CDM_KICK       0x0408
#define PVR_REG_CDM_STATUS     0x040C

/* TQ (Transfer/2D) queue */
#define PVR_REG_TQ_BASE        0x0500
#define PVR_REG_TQ_CTRL        0x0504
#define PVR_REG_TQ_KICK        0x0508
#define PVR_REG_TQ_STATUS      0x050C

/* PVR_REG_CTRL bits */
#define PVR_CTRL_EN            (1u << 0)
#define PVR_CTRL_SOFT_RST      (1u << 1)

/* PVR_REG_SOFT_RESET bits */
#define PVR_SRST_TA            (1u << 0)
#define PVR_SRST_3D            (1u << 1)
#define PVR_SRST_CDM           (1u << 2)
#define PVR_SRST_TQ            (1u << 3)

/* PVR_REG_FW_CTRL bits */
#define PVR_FW_CTRL_START      (1u << 0)
#define PVR_FW_CTRL_HALT       (1u << 1)

/* PVR_REG_FW_STATUS bits */
#define PVR_FW_STATUS_READY    (1u << 0)
#define PVR_FW_STATUS_FAULT    (1u << 1)

/* Queue control bits */
#define QUEUE_CTRL_EN          (1u << 0)
#define QUEUE_CTRL_PRI_SHIFT   4

/* ---- EMI for firmware ---- */
#define PVR_EMI_PHYS           0x51000000ULL  /* Same as CONNSYS EMI? Need verify */
#define PVR_EMI_SIZE           0x00400000ULL
#define PVR_FW_EMI_OFFSET      0x00200000ULL  /* Where firmware is loaded in EMI */

/* ---- waiting, with a limit on it ---- */

/* Until (register & mask) == want, for at most `ms`. 1 if it got there. */
static int pvr_wait(uint64_t reg, uint32_t mask, uint32_t want, uint32_t ms)
{
    uint64_t end = dbg_ticks() + dbg_freq() / 1000 * ms;

    do {
        if ((rd32(reg) & mask) == want) {
            return 1;
        }
    } while (dbg_ticks() < end);
    return 0;
}

static void pvr_set(uint64_t reg, uint32_t bits)
{
    wr32(reg, rd32(reg) | bits);
}

static void pvr_clear(uint64_t reg, uint32_t bits)
{
    wr32(reg, rd32(reg) & ~bits);
}

/* ---- one domain up, or down ---- */

static int pvr_domain_on(const struct pvr_domain *d, int *stage)
{
    uint64_t con = SPM_BASE + d->con;

    *stage = 1;
    pvr_set(con, PWR_ON);
    pvr_set(con, PWR_ON_2ND);
    if (!pvr_wait(SPM_BASE + SPM_PWR_STATUS, d->status, d->status, 20) ||
        !pvr_wait(SPM_BASE + SPM_PWR_STATUS_2ND, d->status, d->status, 20)) {
        return 0;
    }
    *stage = 2;
    pvr_clear(con, PWR_CLK_DIS);
    pvr_clear(con, PWR_ISO);
    pvr_set(con, PWR_RST_B);
    if (d->sram) {
        *stage = 3;
        pvr_clear(con, PWR_SRAM_PDN);
        if (!pvr_wait(con, PWR_SRAM_PDN_ACK, 0, 20)) {
            return 0;
        }
    }
    *stage = 0;
    return 1;
}

static int pvr_domain_off(const struct pvr_domain *d)
{
    uint64_t con = SPM_BASE + d->con;
    int answered = 1;

    if (d->sram) {
        pvr_set(con, PWR_SRAM_PDN);
        answered = pvr_wait(con, PWR_SRAM_PDN_ACK, PWR_SRAM_PDN_ACK, 20);
    }
    pvr_set(con, PWR_ISO);
    pvr_set(con, PWR_CLK_DIS);
    pvr_clear(con, PWR_RST_B);
    pvr_clear(con, PWR_ON);
    pvr_clear(con, PWR_ON_2ND);
    return pvr_wait(SPM_BASE + SPM_PWR_STATUS, d->status, 0, 20) &&
           pvr_wait(SPM_BASE + SPM_PWR_STATUS_2ND, d->status, 0, 20) && answered;
}

/* The GPU block's side of the bus: protection off once MFG has power, on again before it goes. */
static void pvr_bus_open(void)
{
    wr32(INFRACFG_BASE + INFRA_TOPAXI_PROTECTEN_CLR, MFG_PROT_STEP2);
    wr32(INFRACFG_BASE + INFRA_TOPAXI_PROTECTEN_CLR, MFG_PROT_STEP1);
    __asm__ volatile("dsb sy");
}

static int pvr_bus_close(void)
{
    wr32(INFRACFG_BASE + INFRA_TOPAXI_PROTECTEN_SET, MFG_PROT_STEP1);
    if (!pvr_wait(INFRACFG_BASE + INFRA_TOPAXI_PROTECTEN_STA1, MFG_PROT_STEP1, MFG_PROT_STEP1, 20)) {
        return 0;
    }
    wr32(INFRACFG_BASE + INFRA_TOPAXI_PROTECTEN_SET, MFG_PROT_STEP2);
    return pvr_wait(INFRACFG_BASE + INFRA_TOPAXI_PROTECTEN_STA1, MFG_PROT_STEP2, MFG_PROT_STEP2, 20);
}

/* ELF32 header for MIPS firmware */
#define ELF32_MAGIC             0x464C457Fu  /* 0x7F 'E' 'L' 'F' */
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

/* Legacy firmware header (for backwards compat) */
#define PVR_FW_MAGIC            0x52565042u  /* "PVRB" */
struct pvr_fw_header {
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    uint32_t boot_vector;
    uint32_t reserved[4];
};

/* Load firmware file from /fw/pvr/ on eMMC (via vnode in kernel, or raw read in loader) */
/* In loader context, we read raw from eMMC partition */

static int pvr_load_firmware_to_emi(void)
{
    out("PVR: loading firmware from eMMC...\n");

    /* Map EMI for firmware loading */
    uint64_t emi = PVR_EMI_PHYS;  /* Physical address, accessed via ml_io_map equivalent */

    /* Read firmware file - we'll use the same approach as connsys firmware */
    /* For now, we'll copy from the known firmware locations */
    out("PVR: firmware loading via eMMC not yet fully implemented in loader\n");
    out("PVR: use kernel-side firmware loading for now\n");
    return 0;  /* Not fully implemented yet - kernel side handles it */
}

/* ---- GPU probe ---- */

static const char pvr_probe_flag[] = "DARWIN-REDWOOD:PVR-PROBE";

static void pvr_probe(void)
{
    uint16_t hwcid = 0, vgpu_con = 0, vgpu_dbg = 0, vgpu_sel = 0, vsram_con = 0, vsram_dbg = 0, vsram_sel = 0;
    int we_lit_vgpu = 0, we_lit_vsram = 0, up = 0, stage = 0;
    uint32_t status, status2, clk_cfg2;

    out("== mtkloader: PowerVR GE8320 probe ==\n");

    /* ---- the rails ---- */
    out("PMIC: ");
    if (!pwrap_read(MT6358_HWCID, &hwcid)) {
        out("the wrapper does not answer (WACS2_EN ");
        out_hex(rd32(PWRAP_BASE + PWRAP_WACS2_EN));
        out(")\n");
        return;
    }
    out("chip ");
    out_hex(hwcid);
    out("\n  ");
    pwrap_read(MT6358_BUCK_VGPU_CON0, &vgpu_con);
    pwrap_read(MT6358_BUCK_VGPU_DBG1, &vgpu_dbg);
    pwrap_read(MT6358_BUCK_VGPU_ELR0, &vgpu_sel);
    pwrap_read(MT6358_LDO_VSRAM_GPU_CON0, &vsram_con);
    pwrap_read(MT6358_LDO_VSRAM_GPU_DBG1, &vsram_dbg);
    pwrap_read(MT6358_LDO_VSRAM_CON3, &vsram_sel);

    auto say_mv = [](const char *name, int on, uint16_t selector) {
        out(name);
        out(on ? " on at " : " off, set for ");
        out_dec(mt6358_millivolts(selector));
        out(" mV");
    };

    say_mv("VGPU", vgpu_dbg & 1, vgpu_sel);
    out(", ");
    say_mv("VSRAM_GPU", vsram_dbg & 1, vsram_sel);
    out("\n");

    if (!(vsram_dbg & 1) || !(vgpu_dbg & 1)) {
        uint32_t mv_gpu = mt6358_millivolts(vgpu_sel), mv_sram = mt6358_millivolts(vsram_sel);

        if (mv_gpu < 575 || mv_gpu > 900 || mv_sram < 750 || mv_sram > 1000) {
            out("  a rail is off and set for a voltage I will not switch on: stopping\n");
            return;
        }
        if (!(vsram_dbg & 1)) {
            out("  switching VSRAM_GPU on\n");
            pwrap_write(MT6358_LDO_VSRAM_GPU_CON0, vsram_con | 1);
            we_lit_vsram = 1;
        }
        if (!(vgpu_dbg & 1)) {
            out("  switching VGPU on\n");
            pwrap_write(MT6358_BUCK_VGPU_CON0, vgpu_con | 1);
            we_lit_vgpu = 1;
        }
        dbg_delay_ms(2);
        pwrap_read(MT6358_BUCK_VGPU_DBG1, &vgpu_dbg);
        pwrap_read(MT6358_LDO_VSRAM_GPU_DBG1, &vsram_dbg);
        out("  now VGPU ");
        out(vgpu_dbg & 1 ? "on" : "STILL OFF");
        out(", VSRAM_GPU ");
        out(vsram_dbg & 1 ? "on" : "STILL OFF");
        out("\n");
        if (!(vgpu_dbg & 1) || !(vsram_dbg & 1)) {
            goto rails_off;
        }
    }

    /* ---- what the preloader left ---- */
    status = rd32(SPM_BASE + SPM_PWR_STATUS);
    status2 = rd32(SPM_BASE + SPM_PWR_STATUS_2ND);
    clk_cfg2 = rd32(TOPCKGEN_BASE + CLK_CFG_2);
    out("SPM power status ");
    out_hex(status);
    out(" / ");
    out_hex(status2);
    out(", mfg clock cfg2 ");
    out_hex(clk_cfg2);
    out(", bus protection ");
    out_hex(rd32(INFRACFG_BASE + INFRA_TOPAXI_PROTECTEN_STA1));
    out("\n");
    for (int i = 0; i < 4; i++) {
        if (status & pvr_domains[i].status) {
            out("  ");
            out(pvr_domains[i].name);
            out(" is on already; leaving the GPU alone\n");
            goto rails_off;
        }
    }

    /* ---- the clock, then the power ---- */
    out("mfg clocks enable\n");
    wr32(TOPCKGEN_BASE + CLK_CFG_2_SET, MFG_CK | MFG_ASYNC_CK | MFG_AXI_CK);
    __asm__ volatile("dsb sy");
    dbg_delay_us(10);

    for (up = 0; up < 4; up++) {
        const struct pvr_domain *d = &pvr_domains[up];

        out("  powering ");
        out(d->name);
        out(": ");
        if (!pvr_domain_on(d, &stage)) {
            out("stalled at step ");
            out_dec((uint32_t)stage);
            out(", its control ");
            out_hex(rd32(SPM_BASE + d->con));
            out(", status ");
            out_hex(rd32(SPM_BASE + SPM_PWR_STATUS));
            out("\n");
            pvr_domain_off(d);
            break;
        }
        out("on\n");
        if (up == 1) {
            out("  opening the bus to the GPU block\n");
            pvr_bus_open();
        }
    }
    if (up < 4) {
        goto power_off;
    }

    /* ---- inside the block ---- */
    out("mfgcfg clock gates ");
    out_hex(rd32(MFGCFG_BASE + MFG_CG_CON));
    wr32(MFGCFG_BASE + MFG_CG_CLR, MFG_CG_BG3D);
    __asm__ volatile("dsb sy");
    out(" -> ");
    out_hex(rd32(MFGCFG_BASE + MFG_CG_CON));
    out("\n");

    /* Enable MFG in MFGCFG */
    wr32(MFGCFG_BASE + MFGCFG_MFG_PWR_CON_SET, MFG_PWR_PWR_ON);
    dbg_delay_ms(10);
    for (int i = 0; i < 100; i++) {
        if (rd32(MFGCFG_BASE + MFGCFG_MFG_PWR_CON) & MFG_PWR_PWR_ACK) break;
        dbg_delay_us(100);
    }
    wr32(MFGCFG_BASE + MFGCFG_MFG_CON_SET, MFG_CON_MFG_EN);
    dbg_delay_us(10);

    /* ---- PowerVR GPU registers ---- */
    uint64_t pvr = PVR_BASE;

    {
        uint32_t id = rd32(pvr + PVR_REG_IDENT);
        out("PVR_REG_IDENT: ");
        out_hex(id);
        out("\n");

        /* Check for GE8320 */
        if ((id & 0xFFFF0000) == 0x83200000) {
            out("PowerVR GE8320 detected (Series 8)\n");
        } else if ((id & 0xFFFF0000) == 0x85400000) {
            out("PowerVR GE8540 detected (Series 8)\n");
        } else {
            out("Unknown PowerVR GPU, continuing...\n");
        }
    }

    /* Soft reset all GPU blocks */
    out("Soft reset: ");
    wr32(pvr + PVR_REG_SOFT_RESET, PVR_SRST_TA | PVR_SRST_3D | PVR_SRST_CDM | PVR_SRST_TQ);
    dbg_delay_ms(10);
    out(pvr_wait(pvr + PVR_REG_SOFT_RESET, PVR_SRST_TA | PVR_SRST_3D | PVR_SRST_CDM | PVR_SRST_TQ, 0, 100) ? "done\n" : "NO ANSWER\n");

    /* ---- Firmware loading ---- */
    out("Firmware loading: NOT YET IMPLEMENTED\n");
    out("Need to load PVR firmware to EMI and start firmware CPU\n");
    /* pvr_load_firmware_to_emi(); */

    /* ---- MMU initialization (identity for test) ---- */
    out("MMU identity mapping test: ");
    wr32(pvr + PVR_REG_MMU_CTRL, 0x1);  /* Enable MMU in identity mode (estimated) */
    dbg_delay_us(10);
    out("done (stub)\n");

    /* ---- Test job on TQ (2D) ---- */
    out("Test TQ job: ");
    {
        volatile uint32_t *target = (volatile uint32_t *)0x50001000ULL;
        *target = 0;
        __asm__ volatile("dsb sy");

        wr32(pvr + PVR_REG_TQ_BASE, 0x50001000u);
        wr32(pvr + PVR_REG_TQ_CTRL, QUEUE_CTRL_EN);
        wr32(pvr + PVR_REG_TQ_KICK, 1);

        int done = 0;
        for (int i = 0; i < 10000; i++) {
            if (rd32(pvr + PVR_REG_TQ_STATUS) & 0x2) {
                done = 1;
                break;
            }
            dbg_delay_us(10);
        }
        out(done ? "completed, " : "TIMEOUT, ");
        out_hex(*target);
        out(*target == 0xCAFEBABE ? " <- GPU wrote value\n" : " (not written)\n");
    }

    /* ---- Power down ----
    out("Powering down GPU...\n");
    wr32(MFGCFG_BASE + MFGCFG_MFG_CON_CLR, MFG_CON_MFG_EN);
    wr32(MFGCFG_BASE + MFGCFG_MFG_PWR_CON_CLR, MFG_PWR_PWR_ON);

    power_off:
    while (up-- > 0) {
        const struct pvr_domain *d = &pvr_domains[up];
        if (up == 1 && !pvr_bus_close()) {
            out("  bus protection did not close; leaving MFG up\n");
            break;
        }
        out("  powering ");
        out(d->name);
        out(pvr_domain_off(d) ? " off\n" : " off: NO ANSWER\n");
    }
    wr32(TOPCKGEN_BASE + CLK_CFG_2_CLR, MFG_CK | MFG_ASYNC_CK | MFG_AXI_CK);
    __asm__ volatile("dsb sy"); */

rails_off:
    if (we_lit_vgpu) {
        pwrap_write(MT6358_BUCK_VGPU_CON0, vgpu_con & ~1u);
    }
    if (we_lit_vsram) {
        pwrap_write(MT6358_LDO_VSRAM_GPU_CON0, vsram_con & ~1u);
    }
    out("PowerVR probe done\n");
}

#endif /* MTKPVR_H */