/*
 * Bluetooth transport for the G510 (MT6765, MT6631 in CONNSYS).
 * Reuses BTIF from hakios.mtk_connsys.c for H4 over STP channel 0.
 * License: APSL 2.0
 */
#include <kern/clock.h>
#include <libkern/libkern.h>
#include <mach/mach_types.h>
#include <pexpert/pexpert.h>
#include <sys/systm.h>
#include <sys/sysctl.h>
#include <kern/thread.h>

extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
extern void redwood_keep(const char *format, ...) __printflike(1, 2);
extern void delay(int usec);
extern void delay_for_interval(uint32_t interval, uint32_t scale_factor);
extern boolean_t ml_set_interrupts_enabled(boolean_t enable);

#define BTIF_PHYS               0x1100c000u
#define BTIF_TX_DMA_PHYS        0x11000b80u
#define BTIF_TX_VFF_PHYS        0x51200000u
#define BTIF_TX_VFF_SIZE        8192u

#define CONN_HIF_ON_PHYS        0x18007000u
#define CONN_HIF_ON_CPUPCR      0x104

#define RGU_PHYS                0x10007000u
#define RGU_SWRST               0x18
#define RGU_CONNSYS_SW_RST      (1u << 12)
#define RGU_KEY                 (0x88u << 24)

#define INFRACFG_PHYS           0x10001000u
#define INFRACFG_IFR2_CLR       0x84
#define INFRACFG_IFR2_SET       0x80
#define INFRACFG_IFR_BTIF       (1u << 31)
#define INFRACFG_AP_DMA         (1u << 18)

static vm_offset_t g510_btif;
static vm_offset_t g510_dma_tx;
static vm_offset_t g510_dma_vff;
static uint32_t g510_dma_wpt;
static uint32_t g510_dma_wrap;
static uint32_t g510_dma = 1;
static vm_offset_t g510_hif;
static vm_offset_t g510_mcu;
static vm_offset_t g510_rgu;
static vm_offset_t g510_wbg;

static uint32_t
g510_bt_ms(void)
{
    uint64_t ns = 0;
    absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
    return (uint32_t)(ns / NSEC_PER_MSEC);
}

static uint32_t
rd(vm_offset_t base, uint32_t offset)
{
    return *(volatile uint32_t *)(base + offset);
}

static void
wr(vm_offset_t base, uint32_t offset, uint32_t value)
{
    *(volatile uint32_t *)(base + offset) = value;
}

static void
g510_btif_dma_init(void)
{
    vm_offset_t infra = ml_io_map(INFRACFG_PHYS, 0x1000);

    *(volatile uint32_t *)(infra + 0x8c) = INFRACFG_AP_DMA;
    delay(10);
    if (g510_dma_tx == 0) {
        g510_dma_tx = ml_io_map(BTIF_TX_DMA_PHYS, 0x1000);
    }
    if (g510_dma_vff == 0) {
        g510_dma_vff = ml_io_map(BTIF_TX_VFF_PHYS, BTIF_TX_VFF_SIZE);
    }
    *(volatile uint32_t *)(g510_dma_tx + 0x0c) |= 1;
    for (unsigned i = 0; i < 100 && (*(volatile uint32_t *)(g510_dma_tx + 0x08) & 1); i++) {
        delay(10);
    }
    if (*(volatile uint32_t *)(g510_dma_tx + 0x08) & 1) {
        *(volatile uint32_t *)(g510_dma_tx + 0x0c) |= 2;
        delay(10);
        *(volatile uint32_t *)(g510_dma_tx + 0x0c) &= ~2;
    }
    *(volatile uint32_t *)(g510_dma_tx + 0x1c) = BTIF_TX_VFF_PHYS;
    *(volatile uint32_t *)(g510_dma_tx + 0x54) = 0;
    *(volatile uint32_t *)(g510_dma_tx + 0x24) = BTIF_TX_VFF_SIZE;
    *(volatile uint32_t *)(g510_dma_tx + 0x2c) = 0;
    *(volatile uint32_t *)(g510_dma_tx + 0x30) = 0;
    g510_dma_wpt = 0;
    g510_dma_wrap = 0;
    *(volatile uint32_t *)(g510_dma_tx + 0x28) = BTIF_TX_VFF_SIZE - 7;
    *(volatile uint32_t *)(g510_dma_tx + 0x00) = 0;
    *(volatile uint32_t *)(g510_dma_tx + 0x04) = 0;
    redwood_keep("g510_mtkbt: BTIF DMA TX ready: EN %#x, WPT %#x, RPT %#x, LEFT %u",
        g510_bt_ms(), rd(g510_dma_tx, 0x08), rd(g510_dma_tx, 0x2c), rd(g510_dma_tx, 0x30), rd(g510_dma_tx, 0x40));
}

static void
g510_dma_copy(vm_offset_t dst, const uint8_t *src, unsigned len)
{
    volatile uint8_t *d = (volatile uint8_t *)dst;

    for (unsigned i = 0; i < len; i++) {
        d[i] = src[i];
    }
}

static void
g510_btif_dma_write(const uint8_t *bytes, unsigned length)
{
    unsigned at = 0, stuck = 0;

    for (unsigned t = 0; t < 50000 && (rd(g510_dma_tx, 0x14) & 1); t++) {
        delay(10);
    }
    while (at < length) {
        unsigned chunk = length - at;
        unsigned left = rd(g510_dma_tx, 0x40);

        if (left == 0) {
            unsigned t;
            for (t = 0; t < 50000 && rd(g510_dma_tx, 0x40) == 0; t++) {
                delay(10);
            }
            if (t >= 50000) {
                stuck = 1;
                break;
            }
            left = rd(g510_dma_tx, 0x40);
        }
        if (chunk > left) {
            chunk = left;
        }
        if (g510_dma_wpt + chunk >= BTIF_TX_VFF_SIZE) {
            unsigned tail = BTIF_TX_VFF_SIZE - g510_dma_wpt;
            g510_dma_copy(g510_dma_vff + g510_dma_wpt, bytes + at, tail);
            g510_dma_copy(g510_dma_vff, bytes + at + tail, chunk - tail);
            g510_dma_wpt = g510_dma_wpt + chunk - BTIF_TX_VFF_SIZE;
            g510_dma_wrap ^= 0x10000;
        } else {
            g510_dma_copy(g510_dma_vff + g510_dma_wpt, bytes + at, chunk);
            g510_dma_wpt += chunk;
        }
        at += chunk;
        __asm__ volatile ("dsb sy" ::: "memory");

        *(volatile uint32_t *)(g510_dma_tx + 0x08) |= 1;
        *(volatile uint32_t *)(g510_dma_tx + 0x2c) = g510_dma_wpt | g510_dma_wrap;

        uint32_t valid = rd(g510_dma_tx, 0x3c);
        if (valid > 0 && valid < 8) {
            *(volatile uint32_t *)(g510_dma_tx + 0x14) |= 1;
        }
    }
    for (unsigned t = 0; !stuck && t < 50000 && rd(g510_dma_tx, 0x3c) != 0; t++) {
        uint32_t valid = rd(g510_dma_tx, 0x3c);
        if (valid > 0 && valid < 8) {
            *(volatile uint32_t *)(g510_dma_tx + 0x14) |= 1;
        }
        delay(10);
    }
    for (unsigned t = 0; !stuck && t < 50000 && !(rd(g510_btif, 0x14) & (1u << 6)); t++) {
        delay(10);
    }
    if (stuck) {
        redwood_keep("g510_mtkbt: BTIF DMA TX timeout at %u/%u (LSR %#x, DMA_EN %#x, VALID %u, LEFT %u)",
            at, length, rd(g510_btif, 0x14), rd(g510_dma_tx, 0x08),
            rd(g510_dma_tx, 0x3c), rd(g510_dma_tx, 0x40));
    }
}

static void
g510_btif_init(void)
{
    static bool asked;

    if (!asked) {
        asked = true;
        (void)PE_parse_boot_argn("g510_bt_dma", &g510_dma, sizeof(g510_dma));
    }
    vm_offset_t infra = ml_io_map(INFRACFG_PHYS, 0x1000);

    *(volatile uint32_t *)(infra + 0x84) = INFRACFG_IFR_BTIF;
    delay(10);
    g510_btif = ml_io_map(BTIF_PHYS, 0x1000);
    *(volatile uint32_t *)(g510_btif + 0x0c) = 0;
    *(volatile uint32_t *)(g510_btif + 0x6c) |= 1;
    *(volatile uint32_t *)(g510_btif + 0x60) = 0x18;
    *(volatile uint32_t *)(g510_btif + 0x08) = 1u << 1;
    *(volatile uint32_t *)(g510_btif + 0x08) = 0;
    *(volatile uint32_t *)(g510_btif + 0x08) = 1u << 2;
    *(volatile uint32_t *)(g510_btif + 0x08) = 0;
    if (g510_dma) {
        g510_btif_dma_init();
        *(volatile uint32_t *)(g510_btif + 0x4c) = (1u << 2) | (1u << 1);
    } else {
        *(volatile uint32_t *)(g510_btif + 0x4c) = 1u << 2;
    }
    *(volatile uint32_t *)(g510_btif + 0x04) = 0;
    redwood_keep("g510_mtkbt: BTIF on (%s): clock status %#x, LSR %#x",
        g510_bt_ms(), g510_dma ? "DMA TX" : "PIO", rd(infra, 0x90), rd(g510_btif, 0x14));
}

static void
g510_btif_write(const uint8_t *bytes, unsigned length)
{
    if (g510_dma && g510_dma_tx != 0) {
        g510_btif_dma_write(bytes, length);
        return;
    }
    static uint32_t quiet = 0, asked;
    boolean_t was = TRUE;

    if (!asked) {
        asked = 1;
        (void)PE_parse_boot_argn("g510_bt_irqs_off", &quiet, sizeof(quiet));
    }
    if (quiet) {
        was = ml_set_interrupts_enabled(FALSE);
    }
    unsigned at = 0, stuck = 0;

    while (at < length) {
        unsigned t;
        for (t = 0; t < 50000 && !(rd(g510_btif, 0x14) & (1u << 6)); t++) {
            delay(10);
        }
        if (t >= 50000) {
            stuck = 1;
            break;
        }
        for (unsigned n = 0; n < 16 && at < length; n++, at++) {
            *(volatile uint8_t *)(g510_btif + 0x0) = bytes[at];
        }
    }
    for (unsigned t = 0; !stuck && t < 50000 && !(rd(g510_btif, 0x14) & (1u << 6)); t++) {
        delay(10);
    }
    if (quiet) {
        (void)ml_set_interrupts_enabled(was);
    }
    if (stuck) {
        redwood_keep("g510_mtkbt: BTIF write timeout at %u/%u (LSR %#x)", at, length, rd(g510_btif, 0x14));
    }
}

static void
g510_btif_wake(void)
{
    *(volatile uint32_t *)(g510_btif + 0x64) &= ~1u;
    delay(150);
    *(volatile uint32_t *)(g510_btif + 0x64) |= 1u;
    delay_for_interval(20, NSEC_PER_MSEC);
}

static uint16_t
g510_crc16(const uint8_t *bytes, unsigned length)
{
    uint16_t crc = 0;

    for (unsigned i = 0; i < length; i++) {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ 0xA001) : (uint16_t)(crc >> 1);
        }
    }
    return crc;
}

static void
g510_stp_send(const uint8_t *payload, unsigned length, bool full_mode, uint8_t *seq, uint8_t *ack, uint8_t *expect)
{
    uint8_t frame[4 + 1100 + 2];
    uint16_t crc = full_mode ? g510_crc16(payload, length) : 0;

    if (length > 1100) {
        return;
    }

    frame[0] = (uint8_t)(0x80 | (full_mode ? ((*seq << 3) | *ack) : 0));
    frame[1] = (uint8_t)((0 << 4) | ((length >> 8) & 0xf));
    frame[2] = (uint8_t)length;
    frame[3] = full_mode ? (uint8_t)(frame[0] + frame[1] + frame[2]) : 0;
    memcpy(frame + 4, payload, length);
    frame[4 + length] = (uint8_t)crc;
    frame[5 + length] = (uint8_t)(crc >> 8);
    g510_btif_write(frame, 6 + length);
    if (full_mode) {
        *seq = (*seq + 1) & 7;
    }
}

static int
g510_btif_byte(unsigned wait_ms)
{
    for (unsigned t = 0; t <= wait_ms * 10; t++) {
        if (rd(g510_btif, 0x14) & 1) {
            return (int)(rd(g510_btif, 0x0) & 0xff);
        }
        delay(100);
    }
    return -1;
}

static int
g510_stp_receive(uint8_t *payload, unsigned room, unsigned wait_ms, bool full_mode, uint8_t *seq, uint8_t *ack, uint8_t *expect)
{
    for (;;) {
        int b0 = g510_btif_byte(wait_ms), b1, b2, b3;
        unsigned length;

        if (b0 < 0) {
            return -1;
        }
        if (b0 == 0x55 || !(b0 & 0x80) || (b0 & 0x40)) {
            continue;
        }
        if ((b1 = g510_btif_byte(50)) < 0 || (b2 = g510_btif_byte(50)) < 0 || (b3 = g510_btif_byte(50)) < 0) {
            return -1;
        }
        length = ((unsigned)(b1 & 0xf) << 8) | (unsigned)b2;
        if (length == 0) {
            if (full_mode) {
                *ack = b0 & 7;
            }
            continue;
        }
        for (unsigned i = 0; i < length + 2; i++) {
            int b = g510_btif_byte(50);

            if (b < 0) {
                return -1;
            }
            if (i < length && i < room) {
                payload[i] = (uint8_t)b;
            }
        }
        if (full_mode) {
            const uint8_t s = (uint8_t)((b0 >> 3) & 7);
            const bool again = s != *expect;
            uint8_t ack[4];

            if (!again) {
                *ack = s;
                *expect = (uint8_t)((s + 1) & 7);
            }
            ack[0] = (uint8_t)(0x80 | *ack);
            ack[1] = 0;
            ack[2] = 0;
            ack[3] = ack[0];
            g510_btif_write(ack, 4);
            if (again) {
                continue;
            }
        }
        if (((b1 >> 4) & 7) != 0) {
            continue;
        }
        return (int)(length < room ? length : room);
    }
}

static bool
g510_wmt(const char *what, const uint8_t *command, unsigned length, const uint8_t *expected, unsigned expected_length)
{
    char line[220];
    uint8_t answer[32];
    int got = -1;
    size_t used;
    static bool woken;
    const unsigned wait_ms = 2000;

    used = (size_t)snprintf(line, sizeof(line), "%s: (MCU pc %#x)", what, g510_hif != 0 ? rd(g510_hif, CONN_HIF_ON_CPUPCR) : 0);
    static uint32_t wake = 2;
    static bool asked;
    if (!asked) {
        asked = true;
        (void)PE_parse_boot_argn("g510_bt_wake", &wake, sizeof(wake));
    }
    for (unsigned try = 0; try < 3; try++) {
        if (try > 0) {
            if (!woken) {
                g510_btif_wake();
                woken = true;
            }
        }
        if (wake == 2 || (wake == 1 && (!woken || try > 0))) {
            g510_btif_wake();
            woken = true;
        }
        g510_stp_send(command, length, false, NULL, NULL, NULL);
        got = g510_stp_receive(answer, sizeof(answer), wait_ms, false, NULL, NULL, NULL);
        for (unsigned more = 0; more < 4 && got == 6 && memcmp(answer, "\x02\x03\x02\x00\x00", 5) == 0 &&
            !((unsigned)got >= expected_length && memcmp(answer, expected, expected_length) == 0); more++) {
            used = strlen(line);
            snprintf(line + used, sizeof(line) - used, " [event %02x]", answer[5]);
            got = g510_stp_receive(answer, sizeof(answer), wait_ms, false, NULL, NULL, NULL);
        }
        if (got >= 0) {
            break;
        }
        used = strlen(line);
        snprintf(line + used, sizeof(line) - used, " . (pc %#x)", g510_hif != 0 ? rd(g510_hif, CONN_HIF_ON_CPUPCR) : 0);
    }
    used = strlen(line);
    if (got < 0) {
        snprintf(line + used, sizeof(line) - used, " no answer (LSR %#x)", rd(g510_btif, 0x14));
    }
    for (int i = 0; i < got; i++) {
        used += (size_t)snprintf(line + used, sizeof(line) - used, " %02x", answer[i]);
    }
    if (got >= 0) {
        snprintf(line + used, sizeof(line) - used, (unsigned)got >= expected_length && memcmp(answer, expected, expected_length) == 0 ?
            " (as expected)" : " (NOT as expected)");
    }
    redwood_keep("g510_mtkbt: %s", line);
    return got >= 0 && (unsigned)got >= expected_length && memcmp(answer, expected, expected_length) == 0;
}

static int
g510_power_on(void)
{
    redwood_keep("g510_mtkbt: power_on: CONNSYS already up, waiting for MCU");
    for (unsigned n = 0; n < 500 && rd(g510_mcu, 0x600) != 0x1D1E; n++) {
        delay_for_interval(20, NSEC_PER_MSEC);
    }
    redwood_keep("g510_mtkbt: power_on: MCU 0x600=%#x", rd(g510_mcu, 0x600));
    return (rd(g510_mcu, 0x600) == 0x1D1E) ? 0 : ETIMEDOUT;
}

static int
g510_function_on(void)
{
    static const uint8_t func_on[] = { 0x01, 0x06, 0x02, 0x00, 0x00, 0x01 };
    static const uint8_t func_on_done[] = { 0x02, 0x06, 0x01, 0x00, 0x00 };

    redwood_keep("g510_mtkbt: function_on: sending WMT_FUNC_CTRL (BT on)");
    if (!g510_wmt("WMT_FUNC_CTRL BT on", func_on, sizeof(func_on), func_on_done, sizeof(func_on_done))) {
        return EIO;
    }
    redwood_keep("g510_mtkbt: function_on: done");
    return 0;
}

static void
g510_function_off(void)
{
    static const uint8_t func_off[] = { 0x01, 0x06, 0x02, 0x00, 0x00, 0x00 };
    static const uint8_t func_off_done[] = { 0x02, 0x06, 0x01, 0x00, 0x00 };

    redwood_keep("g510_mtkbt: function_off: sending WMT_FUNC_CTRL (BT off)");
    g510_wmt("WMT_FUNC_CTRL BT off", func_off, sizeof(func_off), func_off_done, sizeof(func_off_done));
}

static int
g510_send(const uint8_t *h4, unsigned length)
{
    static uint8_t seq = 0, ack = 7, expect = 0;

    g510_btif_wake();
    g510_stp_send(h4, length, true, &seq, &ack, &expect);
    return 0;
}

static int
g510_receive(uint8_t *h4, unsigned room, unsigned wait_ms)
{
    static uint8_t seq = 0, ack = 7, expect = 0;
    int got;

    got = g510_stp_receive(h4, room, wait_ms, true, &seq, &ack, &expect);
    return got >= 0 ? got : -1;
}

static void
g510_wake(void)
{
    g510_btif_wake();
}

static int
g510_wifi_address(uint8_t out[6])
{
    return ENXIO;
}

static const struct mtkbt_platform mtkbt_platform = {
    .name = "mt6765 / g510 / mt6631",
    .power_on = g510_power_on,
    .function_on = g510_function_on,
    .function_off = g510_function_off,
    .send = g510_send,
    .receive = g510_receive,
    .wake = g510_wake,
    .wifi_address = g510_wifi_address,
};

void
hakios_mtk_bt_init(void)
{
    extern void redwood_mtkbt_skel_init(void);
    redwood_mtkbt_skel_init();
}