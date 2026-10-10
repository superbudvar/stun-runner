/* Self-check for the transcribed GSP boot / fill / palette routines.
 *
 * The fake port below models `tms340x0_device::host_w` faithfully
 * (devices/cpu/tms34010/tms34010.cpp:1644-1692): HOST_ADDRESS_H/HSTADRH and
 * HOST_ADDRESS_L/HSTADRL latch the address, HOST_DATA writes the word at
 * (HSTADRH << 16 | HSTADRL) & 0xFFFFFFF0 and then, if HSTCTL bit 0x0800 is
 * set, adds 0x10 back into the address latches, and HOST_CONTROL sets/ORs
 * HSTCTL.  So every FIFO event a routine produces is recorded as a concrete
 * (GSP address, word) pair.
 *
 * Two recording modes exist because the biggest fills push 230400 words:
 *   - every FIFO write is logged until `fifo_log_limit` have been logged
 *     (later FIFO writes are accounted for but not stored);
 *   - all FIFO writes are additionally summarised as a count, the first and
 *     last (address, word), and the length of the trailing run of equal words.
 */
#include "gsp_boot.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL: %s\n", #condition); \
        failures++; \
    } \
} while (0)

/* ── Fake TMS34010 host port ──────────────────────────────────────────── */

enum fake_event_kind {
    FAKE_EVENT_ADDRESS_HI,
    FAKE_EVENT_ADDRESS_LO,
    FAKE_EVENT_CONTROL_SET,
    FAKE_EVENT_CONTROL_OR,
    FAKE_EVENT_FIFO,
    FAKE_EVENT_POKE
};

typedef struct {
    int kind;
    uint32_t address; /* FIFO: resolved GSP address. POKE: 68010 address. */
    uint16_t value;
} fake_event_t;

#define FAKE_EVENT_CAPACITY 4096

typedef struct {
    uint16_t hstadrh;
    uint16_t hstadrl;
    uint16_t hstctl;

    fake_event_t events[FAKE_EVENT_CAPACITY];
    unsigned event_count;
    int overflow;

    unsigned fifo_log_limit; /* 0 = never store FIFO events */

    unsigned fifo_count;
    uint32_t fifo_first_address;
    uint16_t fifo_first_value;
    uint32_t fifo_last_address;
    uint16_t fifo_last_value;
    unsigned fifo_tail_count;
    uint16_t fifo_tail_value;
    uint32_t fifo_tail_first_address;

    unsigned poke_count;

    /* Flat 64 KiB at 0x000000: covers the ROM image and every 68010 address the
     * transcription reads or writes (0x0060xxxx, 0x0080xx, 0x00FFxxxx). */
    uint8_t ram[0x10000];
} fake_port_t;

static void fake_reset(fake_port_t *port)
{
    memset(port, 0, sizeof(*port));
    port->fifo_log_limit = FAKE_EVENT_CAPACITY; /* log everything by default */
}

static void fake_record(fake_port_t *port, int kind, uint32_t address,
                        uint16_t value)
{
    if (port->event_count >= FAKE_EVENT_CAPACITY) {
        port->overflow = 1;
        return;
    }
    port->events[port->event_count].kind = kind;
    port->events[port->event_count].address = address;
    port->events[port->event_count].value = value;
    port->event_count++;
}

static void fake_set_host_address_hi(void *ctx, uint16_t value)
{
    fake_port_t *port = (fake_port_t *)ctx;
    port->hstadrh = value;
    fake_record(port, FAKE_EVENT_ADDRESS_HI, 0u, value);
}

static void fake_set_host_address_lo(void *ctx, uint16_t value)
{
    fake_port_t *port = (fake_port_t *)ctx;
    port->hstadrl = value;
    fake_record(port, FAKE_EVENT_ADDRESS_LO, 0u, value);
}

static void fake_set_host_control(void *ctx, uint16_t value)
{
    fake_port_t *port = (fake_port_t *)ctx;
    port->hstctl = value;
    fake_record(port, FAKE_EVENT_CONTROL_SET, 0u, value);
}

static void fake_or_host_control_mask(void *ctx, uint16_t mask)
{
    fake_port_t *port = (fake_port_t *)ctx;
    port->hstctl = (uint16_t)(port->hstctl | mask);
    fake_record(port, FAKE_EVENT_CONTROL_OR, 0u, mask);
}

static void fake_write_fifo_word(void *ctx, uint16_t value)
{
    fake_port_t *port = (fake_port_t *)ctx;
    uint32_t address = ((uint32_t)port->hstadrh << 16) | (uint32_t)port->hstadrl;

    /* TMS34010_WRMEM_WORD(addr & 0xfffffff0, data) */
    address &= 0xFFFFFFF0u;

    if (port->fifo_count == 0u) {
        port->fifo_first_address = address;
        port->fifo_first_value = value;
    }
    if (port->fifo_count != 0u && value == port->fifo_tail_value) {
        port->fifo_tail_count++;
    } else {
        port->fifo_tail_count = 1u;
        port->fifo_tail_value = value;
        port->fifo_tail_first_address = address;
    }
    port->fifo_count++;
    port->fifo_last_address = address;
    port->fifo_last_value = value;

    if (port->fifo_count <= port->fifo_log_limit) {
        fake_record(port, FAKE_EVENT_FIFO, address, value);
    }

    /* optional postincrement, written back into the address latches */
    if (port->hstctl & 0x0800u) {
        address += 0x10u;
        port->hstadrh = (uint16_t)(address >> 16);
        port->hstadrl = (uint16_t)(address & 0xFFFFu);
    }
}

static uint16_t fake_peek_word(void *ctx, uint32_t address)
{
    fake_port_t *port = (fake_port_t *)ctx;
    size_t offset = (size_t)(address & 0xFFFFu);
    return (uint16_t)(((uint16_t)port->ram[offset] << 8) |
                      (uint16_t)port->ram[offset + 1]);
}

static uint8_t fake_peek_byte(void *ctx, uint32_t address)
{
    fake_port_t *port = (fake_port_t *)ctx;
    return port->ram[address & 0xFFFFu];
}

static void fake_poke_word(void *ctx, uint32_t address, uint16_t value)
{
    fake_port_t *port = (fake_port_t *)ctx;
    size_t offset = (size_t)(address & 0xFFFFu);
    port->ram[offset] = (uint8_t)(value >> 8);
    port->ram[offset + 1] = (uint8_t)(value & 0xFFu);
    port->poke_count++;
    fake_record(port, FAKE_EVENT_POKE, address, value);
}

static void fake_bind(fake_port_t *port, stunrun_gsp_host_t *io)
{
    io->ctx = port;
    io->set_host_address_lo = fake_set_host_address_lo;
    io->set_host_address_hi = fake_set_host_address_hi;
    io->set_host_control = fake_set_host_control;
    io->or_host_control_mask = fake_or_host_control_mask;
    io->write_fifo_word = fake_write_fifo_word;
    io->peek_word = fake_peek_word;
    io->peek_byte = fake_peek_byte;
    io->poke_word = fake_poke_word;
}

/* Event index of the FIRST FIFO write whose resolved GSP address equals
 * `address`, or event_count if there is no such write.  Auto-increment means
 * only occurrence 0 lands on `address` itself; later ones walk forward by
 * 0x10, so use this to anchor a run and then index forward. */
static unsigned fake_fifo_index(const fake_port_t *port, uint32_t address)
{
    unsigned i;

    for (i = 0; i < port->event_count; i++) {
        if (port->events[i].kind == FAKE_EVENT_FIFO &&
            port->events[i].address == address) {
            return i;
        }
    }
    return port->event_count;
}

/* ── Expected-value tables, written from the disassembly ──────────────── */

/* palette_lo at entry e: block = e / 32, i = e % 32, step = (8 * i) & 0xff.
 * The ROM's step pairs are (0,0),(0,0),(0,8),(0,8),(8,0),(8,0),(8,8),(8,8),
 * so red steps 0,0,0,0,8,8,8,8 and green 0,0,8,8,0,0,8,8. */
static uint16_t expected_palette_lo(unsigned entry)
{
    unsigned block = entry / 32u;
    unsigned index = entry % 32u;
    unsigned step = (8u * index) & 0xFFu;

    switch (block) {
    case 0: case 1: return 0x0000u;
    case 2: case 3: return (uint16_t)step;
    case 4: case 5: return (uint16_t)(step << 8);
    default: return (uint16_t)((step << 8) | step);
    }
}

/* palette_hi at entry e: the ROM's blue steps are 0,8,0,8,0,8,0,8, so the
 * blocks that step are 1,3,5,7 - which do NOT line up with the red/green
 * blocks above.  Blue is non-zero only on entries 32-63, 96-127, 160-191 and
 * 224-255; the resulting "cube" is skewed, not an aligned RGB cube. */
static uint16_t expected_palette_hi(unsigned entry)
{
    unsigned block = entry / 32u;
    unsigned index = entry % 32u;
    unsigned step = (8u * index) & 0xFFu;

    if (block == 1u || block == 3u || block == 5u || block == 7u) {
        return (uint16_t)step;
    }
    return 0x0000u;
}

/* ── Expected-event helpers ───────────────────────────────────────────── */

/* Parameter names are suffixed so they cannot collide with the fake_event_t
 * field names they are expanded inside. */
#define APPEND_ADDRESS(addr_) do { \
    expected[n].kind = FAKE_EVENT_ADDRESS_HI; \
    expected[n].address = 0u; \
    expected[n].value = (uint16_t)((addr_) >> 16); n++; \
    expected[n].kind = FAKE_EVENT_ADDRESS_LO; \
    expected[n].address = 0u; \
    expected[n].value = (uint16_t)((addr_) & 0xFFFFu); n++; \
} while (0)

#define APPEND_FIFO(addr_, word_) do { \
    expected[n].kind = FAKE_EVENT_FIFO; \
    expected[n].address = (addr_); \
    expected[n].value = (uint16_t)(word_); n++; \
} while (0)

#define APPEND_CONTROL_OR(mask_) do { \
    expected[n].kind = FAKE_EVENT_CONTROL_OR; \
    expected[n].address = 0u; \
    expected[n].value = (uint16_t)(mask_); n++; \
} while (0)

#define APPEND_POKE(addr_, word_) do { \
    expected[n].kind = FAKE_EVENT_POKE; \
    expected[n].address = (addr_); \
    expected[n].value = (uint16_t)(word_); n++; \
} while (0)

/* Compare the recorded log against an expected log, reporting the first
 * mismatch with full context. */
static void expect_events(const char *what, const fake_port_t *port,
                          const fake_event_t *expected, unsigned count)
{
    unsigned i;

    CHECK(!port->overflow);
    if (port->event_count != count) {
        fprintf(stderr, "FAIL %s: %u events recorded, %u expected\n", what,
                port->event_count, count);
        failures++;
        return;
    }
    for (i = 0; i < count; i++) {
        if (port->events[i].kind != expected[i].kind ||
            port->events[i].address != expected[i].address ||
            port->events[i].value != expected[i].value) {
            fprintf(stderr,
                    "FAIL %s: event %u: got (kind=%d, addr=0x%08X, "
                    "value=0x%04X), want (kind=%d, addr=0x%08X, "
                    "value=0x%04X)\n",
                    what, i, port->events[i].kind, port->events[i].address,
                    port->events[i].value, expected[i].kind,
                    expected[i].address, expected[i].value);
            failures++;
            return;
        }
    }
}

/* ── 0x570: the palette walk ──────────────────────────────────────────── */

#define WALK_PALETTE_EVENTS 545

static void test_walk_palette(void)
{
    static fake_event_t expected[WALK_PALETTE_EVENTS];
    fake_port_t port;
    stunrun_gsp_host_t io;
    unsigned n = 0;
    unsigned i;

    /* The expected log, built straight from the disassembly at 0x570. */

    /* ori.w #$1800,$c00008.l */
    APPEND_CONTROL_OR(STUNRUN_GSP_HOSTCTL_AUTO_INCREMENT);

    /* move.l #$f4800020 / clr.w $c0000c / move.l #$f4800030 / clr.w $c0000c */
    APPEND_ADDRESS(STUNRUN_GSP_CONTROL_HI_SHIFTEREG_20);
    APPEND_FIFO(STUNRUN_GSP_CONTROL_HI_SHIFTEREG_20, 0x0000);
    APPEND_ADDRESS(STUNRUN_GSP_CONTROL_HI_SHIFTEREG_30);
    APPEND_FIFO(STUNRUN_GSP_CONTROL_HI_SHIFTEREG_30, 0x0000);

    /* move.l #$f5000000 / 8 x bsr $4f6 -> 256 words at 0xF5000000 + 16*i */
    APPEND_ADDRESS(STUNRUN_GSP_PALETTE_LO);
    for (i = 0; i < 256u; i++) {
        APPEND_FIFO(STUNRUN_GSP_PALETTE_LO + 16u * i, expected_palette_lo(i));
    }

    /* move.l #$f5800000 / 8 x bsr $518 -> 256 words at 0xF5800000 + 16*i */
    APPEND_ADDRESS(STUNRUN_GSP_PALETTE_HI);
    for (i = 0; i < 256u; i++) {
        APPEND_FIFO(STUNRUN_GSP_PALETTE_HI + 16u * i, expected_palette_hi(i));
    }

    /* bsr $530 - two address+word pairs, then five address latches that carry
     * no data word. */
    APPEND_ADDRESS(STUNRUN_GSP_CONTROL_HI_SHIFTEREG_70);
    APPEND_FIFO(STUNRUN_GSP_CONTROL_HI_SHIFTEREG_70, 0x0000);
    APPEND_ADDRESS(STUNRUN_GSP_CONTROL_HI_SHIFTEREG_F0);
    APPEND_FIFO(STUNRUN_GSP_CONTROL_HI_SHIFTEREG_F0, 0x0000);
    APPEND_ADDRESS(STUNRUN_GSP_DRAW_PARAM_0);
    APPEND_ADDRESS(STUNRUN_GSP_DRAW_PARAM_1);
    APPEND_ADDRESS(STUNRUN_GSP_DRAW_PARAM_2);
    APPEND_ADDRESS(STUNRUN_GSP_DRAW_PARAM_3);
    APPEND_ADDRESS(STUNRUN_GSP_DRAW_PARAM_4);

    /* bsr $468 */
    APPEND_ADDRESS(STUNRUN_GSP_CONTROL_HI_SHIFTEREG_10);
    APPEND_FIFO(STUNRUN_GSP_CONTROL_HI_SHIFTEREG_10, 0x0000);
    APPEND_ADDRESS(STUNRUN_GSP_CONTROL_HI_SHIFTEREG_90);
    APPEND_FIFO(STUNRUN_GSP_CONTROL_HI_SHIFTEREG_90, 0xFFFF);

    CHECK(n == WALK_PALETTE_EVENTS);

    fake_reset(&port);
    fake_bind(&port, &io);
    /* 0x570 opens with ori.w #$1800, so it enables auto-increment itself. */
    stunrun_gsp_boot_walk_palette(&io);
    expect_events("walk_palette", &port, expected, n);

    /* Anchor the ramp tables against literals so the table above can be checked
     * by hand against the disassembly. */
    CHECK(expected_palette_lo(0) == 0x0000u);   /* block 0: steps (0,0)   */
    CHECK(expected_palette_lo(31) == 0x0000u);  /* block 0, i = 31        */
    CHECK(expected_palette_lo(63) == 0x0000u);  /* block 1: steps (0,0)   */
    CHECK(expected_palette_lo(64) == 0x0000u);  /* block 2: steps (0,8)   */
    CHECK(expected_palette_lo(95) == 0x00F8u);  /* block 2, i = 31: 8*31  */
    CHECK(expected_palette_lo(127) == 0x00F8u); /* block 3: steps (0,8)   */
    CHECK(expected_palette_lo(128) == 0x0000u); /* block 4: steps (8,0)   */
    CHECK(expected_palette_lo(159) == 0xF800u); /* block 4, i = 31        */
    CHECK(expected_palette_lo(191) == 0xF800u); /* block 5: steps (8,0)   */
    CHECK(expected_palette_lo(192) == 0x0000u); /* block 6: steps (8,8)   */
    CHECK(expected_palette_lo(223) == 0xF8F8u); /* block 6, i = 31        */
    CHECK(expected_palette_lo(255) == 0xF8F8u); /* block 7: steps (8,8)   */

    /* Anchors for the blue ramp: step 0 on blocks 0,2,4,6 (all zero), step 8
     * on blocks 1,3,5,7. */
    CHECK(expected_palette_hi(0) == 0x0000u);   /* block 0: blue step 0    */
    CHECK(expected_palette_hi(31) == 0x0000u);  /* block 0, i = 31        */
    CHECK(expected_palette_hi(32) == 0x0000u);  /* block 1, i = 0         */
    CHECK(expected_palette_hi(63) == 0x00F8u);  /* block 1, i = 31: 8*31  */
    CHECK(expected_palette_hi(64) == 0x0000u);  /* block 2: blue step 0    */
    CHECK(expected_palette_hi(95) == 0x0000u);  /* blue does not step     */
    CHECK(expected_palette_hi(96) == 0x0000u);  /* block 3, i = 0         */
    CHECK(expected_palette_hi(127) == 0x00F8u); /* block 3, i = 31        */
    CHECK(expected_palette_hi(160) == 0x0000u); /* block 5, i = 0         */
    CHECK(expected_palette_hi(191) == 0x00F8u); /* block 5, i = 31        */
    CHECK(expected_palette_hi(224) == 0x0000u); /* block 7, i = 0         */
    CHECK(expected_palette_hi(255) == 0x00F8u); /* block 7, i = 31        */
}

/* ── 0x468 and 0x7C2 ──────────────────────────────────────────────────── */

static void test_clear_display_control(void)
{
    fake_event_t expected[6];
    fake_port_t port;
    stunrun_gsp_host_t io;
    unsigned n = 0;

    APPEND_ADDRESS(STUNRUN_GSP_CONTROL_HI_SHIFTEREG_10);
    APPEND_FIFO(STUNRUN_GSP_CONTROL_HI_SHIFTEREG_10, 0x0000);
    APPEND_ADDRESS(STUNRUN_GSP_CONTROL_HI_SHIFTEREG_90);
    APPEND_FIFO(STUNRUN_GSP_CONTROL_HI_SHIFTEREG_90, 0xFFFF);

    fake_reset(&port);
    fake_bind(&port, &io);
    port.hstctl = STUNRUN_GSP_HOSTCTL_AUTO_INCREMENT;
    stunrun_gsp_boot_clear_display_control(&io);
    expect_events("clear_display_control", &port, expected, n);
}

static void test_program_reset_flag(void)
{
    fake_event_t expected[3];
    fake_port_t port;
    stunrun_gsp_host_t io;
    unsigned n = 0;

    APPEND_ADDRESS(STUNRUN_GSP_UNMAPPED_RESET_FLAG);
    APPEND_FIFO(STUNRUN_GSP_UNMAPPED_RESET_FLAG, 0x0001);

    fake_reset(&port);
    fake_bind(&port, &io);
    port.hstctl = STUNRUN_GSP_HOSTCTL_AUTO_INCREMENT;
    stunrun_gsp_boot_program_reset_flag(&io);
    expect_events("program_reset_flag", &port, expected, n);
}

/* ── 0x48E ────────────────────────────────────────────────────────────── */

/* 1 OR + 3 + 3 + 2 latch + 256 + 2 latch + 256 + 3 + 3 */
#define CONFIGURE_PALETTE_EVENTS 529

static void test_configure_display_and_clear_palette(void)
{
    static fake_event_t expected[CONFIGURE_PALETTE_EVENTS];
    fake_port_t port;
    stunrun_gsp_host_t io;
    unsigned n = 0;
    unsigned i;

    APPEND_CONTROL_OR(STUNRUN_GSP_HOSTCTL_AUTO_INCREMENT);
    APPEND_ADDRESS(STUNRUN_GSP_CONTROL_HI_SHIFTEREG_20);
    APPEND_FIFO(STUNRUN_GSP_CONTROL_HI_SHIFTEREG_20, 0x0000);
    APPEND_ADDRESS(STUNRUN_GSP_CONTROL_HI_SHIFTEREG_30);
    APPEND_FIFO(STUNRUN_GSP_CONTROL_HI_SHIFTEREG_30, 0x0000);

    /* palette_lo ramp: addi.w #$101 - both bytes step together. */
    APPEND_ADDRESS(STUNRUN_GSP_PALETTE_LO);
    for (i = 0; i < 256u; i++) {
        APPEND_FIFO(STUNRUN_GSP_PALETTE_LO + 16u * i, (uint16_t)(i * 0x0101u));
    }

    /* palette_hi ramp: addq.w #1 - blue only. */
    APPEND_ADDRESS(STUNRUN_GSP_PALETTE_HI);
    for (i = 0; i < 256u; i++) {
        APPEND_FIFO(STUNRUN_GSP_PALETTE_HI + 16u * i, (uint16_t)i);
    }

    APPEND_ADDRESS(STUNRUN_GSP_CONTROL_HI_SHIFTEREG_10);
    APPEND_FIFO(STUNRUN_GSP_CONTROL_HI_SHIFTEREG_10, 0x0000);
    APPEND_ADDRESS(STUNRUN_GSP_CONTROL_HI_SHIFTEREG_90);
    APPEND_FIFO(STUNRUN_GSP_CONTROL_HI_SHIFTEREG_90, 0xFFFF);

    CHECK(n == CONFIGURE_PALETTE_EVENTS);

    fake_reset(&port);
    fake_bind(&port, &io);
    stunrun_gsp_boot_configure_display_and_clear_palette(&io);
    expect_events("configure_display_and_clear_palette", &port, expected, n);
}

/* ── 0x4F6 / 0x518 ramps ──────────────────────────────────────────────── */

static void test_ramps(void)
{
    fake_port_t port;
    stunrun_gsp_host_t io;
    unsigned i;

    /* 0x4F6: (high << 8) | low, high stepped by high_step, low by low_step. */
    fake_reset(&port);
    fake_bind(&port, &io);
    port.hstctl = STUNRUN_GSP_HOSTCTL_AUTO_INCREMENT;
    stunrun_gsp_latch_address(&io, STUNRUN_GSP_PALETTE_LO);
    stunrun_gsp_boot_write_ramp_pair(&io, 8, 4);
    CHECK(port.fifo_count == 32u);
    CHECK(port.fifo_first_address == STUNRUN_GSP_PALETTE_LO);
    CHECK(port.fifo_last_address == STUNRUN_GSP_PALETTE_LO + 31u * 16u);
    for (i = 0; i < 32u; i++) {
        CHECK(port.events[2u + i].address == STUNRUN_GSP_PALETTE_LO + 16u * i);
        CHECK(port.events[2u + i].value ==
              (uint16_t)(((8u * i) << 8) | (uint16_t)(4u * i)));
    }

    /* 0x518: a single step, one byte in the low half. */
    fake_reset(&port);
    fake_bind(&port, &io);
    port.hstctl = STUNRUN_GSP_HOSTCTL_AUTO_INCREMENT;
    stunrun_gsp_latch_address(&io, STUNRUN_GSP_PALETTE_HI);
    stunrun_gsp_boot_write_ramp_single(&io, 8);
    CHECK(port.fifo_count == 32u);
    CHECK(port.fifo_last_address == STUNRUN_GSP_PALETTE_HI + 31u * 16u);
    for (i = 0; i < 32u; i++) {
        CHECK(port.events[2u + i].value == (uint16_t)(8u * i));
    }

    /* One (8,8) pair block is exactly the last block of the palette walk. */
    fake_reset(&port);
    fake_bind(&port, &io);
    port.hstctl = STUNRUN_GSP_HOSTCTL_AUTO_INCREMENT;
    stunrun_gsp_latch_address(&io, STUNRUN_GSP_PALETTE_LO);
    stunrun_gsp_boot_write_ramp_pair(&io, 8, 8);
    for (i = 0; i < 32u; i++) {
        CHECK(port.events[2u + i].value == expected_palette_lo(192u + i));
    }
}

/* ── 0x530 ────────────────────────────────────────────────────────────── */

static void test_latch_draw_parameters(void)
{
    fake_event_t expected[16];
    fake_port_t port;
    stunrun_gsp_host_t io;
    unsigned n = 0;

    APPEND_ADDRESS(STUNRUN_GSP_CONTROL_HI_SHIFTEREG_70);
    APPEND_FIFO(STUNRUN_GSP_CONTROL_HI_SHIFTEREG_70, 0x0000);
    APPEND_ADDRESS(STUNRUN_GSP_CONTROL_HI_SHIFTEREG_F0);
    APPEND_FIFO(STUNRUN_GSP_CONTROL_HI_SHIFTEREG_F0, 0x0000);
    APPEND_ADDRESS(STUNRUN_GSP_DRAW_PARAM_0);
    APPEND_ADDRESS(STUNRUN_GSP_DRAW_PARAM_1);
    APPEND_ADDRESS(STUNRUN_GSP_DRAW_PARAM_2);
    APPEND_ADDRESS(STUNRUN_GSP_DRAW_PARAM_3);
    APPEND_ADDRESS(STUNRUN_GSP_DRAW_PARAM_4);

    fake_reset(&port);
    fake_bind(&port, &io);
    port.hstctl = STUNRUN_GSP_HOSTCTL_AUTO_INCREMENT;
    stunrun_gsp_boot_latch_draw_parameters(&io);
    expect_events("latch_draw_parameters", &port, expected, n);

    /* The five trailing latches carry no data word, so no auto-increment happens:
     * the routine returns with $02000000 still latched. */
    CHECK(port.fifo_count == 2u);
    CHECK(port.hstadrh == 0x0200u);
    CHECK(port.hstadrl == 0x0000u);
}

/* ── 0x7B0 ────────────────────────────────────────────────────────────── */

static void test_write_control_lo_word(void)
{
    fake_event_t expected[3];
    fake_port_t port;
    stunrun_gsp_host_t io;
    unsigned n = 0;

    APPEND_ADDRESS(STUNRUN_GSP_CONTROL_LO);
    APPEND_FIFO(STUNRUN_GSP_CONTROL_LO, 0xBEEFu);

    fake_reset(&port);
    fake_bind(&port, &io);
    /* Deliberately NOT pre-setting auto-increment: 0x7B0 must not touch
     * HSTCTL, so enabling it is the caller's job. */
    stunrun_gsp_boot_write_control_lo_word(&io, 0xBEEFu);
    expect_events("write_control_lo_word", &port, expected, n);
    CHECK(port.hstctl == 0u);
}

/* ── 0x636 / 0x67A / 0x6AC / 0x6EE / 0x756 fills ───────────────────────── */

static void test_fill_2bpp_vram_ffff(void)
{
    fake_port_t port;
    stunrun_gsp_host_t io;

    fake_reset(&port);
    fake_bind(&port, &io);
    port.fifo_log_limit = 0u; /* 24577 FIFO writes: summarise instead */
    port.hstctl = STUNRUN_GSP_HOSTCTL_AUTO_INCREMENT;

    stunrun_gsp_boot_fill_2bpp_vram_ffff(&io, 0x0002u);

    /* One control_lo word, then 96 x 256 fill words. */
    CHECK(port.fifo_count == 1u + 96u * 256u);
    CHECK(port.fifo_first_address == STUNRUN_GSP_CONTROL_LO);
    CHECK(port.fifo_first_value == 0x0002u);
    CHECK(port.fifo_last_address == STUNRUN_GSP_2BPP_VRAM_BASE + 16u * 24575u);
    CHECK(port.fifo_last_value == 0xFFFFu);
    CHECK(port.fifo_tail_count == 96u * 256u);
    CHECK(port.fifo_tail_first_address == STUNRUN_GSP_2BPP_VRAM_BASE);
    CHECK(port.fifo_tail_value == 0xFFFFu);
    /* 96 watchdog kicks and no other 68010 writes. */
    CHECK(port.poke_count == 96u);
    CHECK(port.event_count == 1u + 2u + 2u + 96u);
    CHECK(port.events[0].kind == FAKE_EVENT_CONTROL_OR);
    CHECK(port.events[0].value == STUNRUN_GSP_HOSTCTL_AUTO_INCREMENT);
    CHECK(port.events[1].kind == FAKE_EVENT_ADDRESS_HI);
    CHECK(port.events[1].value == 0xF400u);
    CHECK(port.events[2].kind == FAKE_EVENT_ADDRESS_LO);
    CHECK(port.events[3].kind == FAKE_EVENT_ADDRESS_HI);
    CHECK(port.events[3].value == 0x0200u);
    CHECK(port.events[4].kind == FAKE_EVENT_ADDRESS_LO);
    CHECK(port.events[4].value == 0x0000u);
    CHECK(port.events[5].kind == FAKE_EVENT_POKE);
    CHECK(port.events[5].address == STUNRUN_MAIN_WATCHDOG);
}

static void test_fill_main_vram(void)
{
    fake_port_t port;
    stunrun_gsp_host_t io;

    fake_reset(&port);
    fake_bind(&port, &io);
    port.fifo_log_limit = 0u;
    /* 0x67A opens with `ori.w #$1800`, so auto-increment is enabled by the
     * routine itself; HSTCTL starts as the 0xA000 that 0x3F8 wrote. */
    port.hstctl = STUNRUN_GSP_HOSTCTL_BOOT_INIT;

    stunrun_gsp_boot_fill_main_vram(&io, 0x1234u);

    CHECK(port.fifo_count == 900u * 256u);
    CHECK(port.fifo_first_address == STUNRUN_GSP_MAIN_VRAM_BASE);
    CHECK(port.fifo_first_value == 0x1234u);
    CHECK(port.fifo_last_address ==
          STUNRUN_GSP_MAIN_VRAM_BASE + 16u * (900u * 256u - 1u));
    CHECK(port.fifo_tail_count == 900u * 256u);
    CHECK(port.fifo_tail_value == 0x1234u);
    CHECK(port.poke_count == 900u);
    /* 0xA000 | 0x1800 = 0xB800 - the ROM ORs, it does not set. */
    CHECK(port.hstctl ==
          (uint16_t)(STUNRUN_GSP_HOSTCTL_BOOT_INIT |
                     STUNRUN_GSP_HOSTCTL_AUTO_INCREMENT));
}

static void test_fill_2bpp_vram_from_d1(void)
{
    fake_port_t port;
    stunrun_gsp_host_t io;

    fake_reset(&port);
    fake_bind(&port, &io);
    port.fifo_log_limit = 0u;
    port.hstctl = STUNRUN_GSP_HOSTCTL_AUTO_INCREMENT;

    stunrun_gsp_boot_fill_2bpp_vram_from_d1(&io, 0x0020u, 0xABCDu);

    CHECK(port.fifo_count == 1u + 96u * 256u);
    CHECK(port.fifo_first_address == STUNRUN_GSP_CONTROL_LO);
    CHECK(port.fifo_first_value == 0x0020u);
    /* move.w D1,D0 - the fill word is the caller's D1, NOT the control seed. */
    CHECK(port.fifo_tail_first_address == STUNRUN_GSP_2BPP_VRAM_BASE);
    CHECK(port.fifo_tail_value == 0xABCDu);
    CHECK(port.fifo_tail_count == 96u * 256u);
    CHECK(port.poke_count == 96u);
}

static void test_load_palette_from_ram(void)
{
    fake_port_t port;
    stunrun_gsp_host_t io;
    unsigned first_blue;
    unsigned first_rg;
    unsigned last_rg;

    fake_reset(&port);
    fake_bind(&port, &io);
    port.hstctl = STUNRUN_GSP_HOSTCTL_AUTO_INCREMENT;

    /* $8018 = 0x1234, the word at $801B = 0x5678, $801C = 0x7890.
     * `move.b $801b.w,D1` fetches the WORD at $801B and keeps its low byte
     * 0x78 - which is also the low byte of the blue word at $801C. */
    port.ram[0x8018] = 0x12u;
    port.ram[0x8019] = 0x34u;
    port.ram[0x801A] = 0x56u;
    port.ram[0x801B] = 0x78u;
    port.ram[0x801C] = 0x78u;
    port.ram[0x801D] = 0x90u;

    stunrun_gsp_boot_load_palette_from_ram(&io);

    /* Two flat 256-word loops plus the two shiftreg clears, no watchdog kicks. */
    CHECK(port.poke_count == 0u);
    CHECK(port.fifo_count == 2u + 256u + 256u);
    first_blue = fake_fifo_index(&port, STUNRUN_GSP_PALETTE_HI);
    first_rg = fake_fifo_index(&port, STUNRUN_GSP_PALETTE_LO);
    last_rg = first_rg + 255u;
    /* palette_hi is written FIRST, then palette_lo. */
    CHECK(first_blue < first_rg);
    CHECK(port.events[first_blue].value == 0x7890u);
    CHECK(port.events[first_blue + 255u].value == 0x7890u);
    CHECK(port.events[first_blue + 255u].address ==
          STUNRUN_GSP_PALETTE_HI + 16u * 255u);
    /* (word at $8018 << 8) | low byte of the word at $801B */
    CHECK(port.events[first_rg].value == (uint16_t)(0x3400u | 0x78u));
    CHECK(port.events[last_rg].value == (uint16_t)(0x3400u | 0x78u));
    CHECK(port.events[last_rg].address == STUNRUN_GSP_PALETTE_LO + 16u * 255u);

    /* Prove the byte operand is the LOW BYTE of the word at $801B, not an
     * independent byte at $801B: changing only the high half of $801B must not
     * change what is written. */
    fake_reset(&port);
    fake_bind(&port, &io);
    port.hstctl = STUNRUN_GSP_HOSTCTL_AUTO_INCREMENT;
    port.ram[0x8018] = 0x12u;
    port.ram[0x8019] = 0x34u;
    port.ram[0x801A] = 0xFFu;
    port.ram[0x801B] = 0x78u;
    port.ram[0x801C] = 0x78u;
    port.ram[0x801D] = 0x90u;
    stunrun_gsp_boot_load_palette_from_ram(&io);
    first_rg = fake_fifo_index(&port, STUNRUN_GSP_PALETTE_LO);
    CHECK(port.events[first_rg].value == (uint16_t)(0x3400u | 0x78u));
}

static void test_stride_fill_2bpp_vram(void)
{
    static fake_event_t expected[4 + 384 * 5];
    fake_port_t port;
    stunrun_gsp_host_t io;
    unsigned n = 0;
    unsigned i;

    /* Two $ffff words to control_lo, then the strided loop: each pass latches
     * a running address, kicks the watchdog, delays, and writes two words. */
    APPEND_ADDRESS(STUNRUN_GSP_CONTROL_LO);
    APPEND_FIFO(STUNRUN_GSP_CONTROL_LO, 0xFFFF);
    APPEND_FIFO(STUNRUN_GSP_CONTROL_LO + 0x10u, 0xFFFF);

    for (i = 0; i < 384u; i++) {
        uint32_t address = 0x02000200u + 0x400u * i;
        APPEND_ADDRESS(address);
        APPEND_POKE(STUNRUN_MAIN_WATCHDOG, 0x0000);
        APPEND_FIFO(address, 0xFFFF);
        APPEND_FIFO(address + 0x10u, 0xFFFF);
    }

    fake_reset(&port);
    fake_bind(&port, &io);
    port.hstctl = STUNRUN_GSP_HOSTCTL_AUTO_INCREMENT;
    stunrun_gsp_boot_stride_fill_2bpp_vram(&io);
    expect_events("stride_fill_2bpp_vram", &port, expected, n);

    CHECK(port.fifo_count == 2u + 384u * 2u);
    CHECK(port.poke_count == 384u);
    CHECK(port.fifo_last_address == 0x0205FE10u);
    CHECK(port.hstctl == STUNRUN_GSP_HOSTCTL_AUTO_INCREMENT);
}

/* ── 0x34A / 0x37C / 0x3BE ────────────────────────────────────────────── */

static void rom_put_word(uint8_t *rom, uint32_t address, uint16_t value)
{
    rom[address] = (uint8_t)(value >> 8);
    rom[address + 1] = (uint8_t)(value & 0xFFu);
}

static void rom_put_long(uint8_t *rom, uint32_t address, uint32_t value)
{
    rom[address] = (uint8_t)(value >> 24);
    rom[address + 1] = (uint8_t)((value >> 16) & 0xFFu);
    rom[address + 2] = (uint8_t)((value >> 8) & 0xFFu);
    rom[address + 3] = (uint8_t)(value & 0xFFu);
}

/* ROM image with a known record table, packed-bitfield table and flag byte. */
static void build_test_rom(uint8_t *rom)
{
    /* Record table at 0x2B8, seed 0x0001 at 0x2E8 -> two records. */
    rom_put_long(rom, 0x02B8u, 0xF5000000u);
    rom_put_word(rom, 0x02BCu, 0x1234u);
    rom_put_long(rom, 0x02BEu, 0xF5800000u);
    rom_put_word(rom, 0x02C2u, 0x5678u);
    rom_put_word(rom, 0x02E8u, 0x0001u);
    /* tst.b $2f9.l == 0 -> the display window word is 0xFFFF. */
    rom[0x02F9] = 0x00u;

    /* Packed bitfield entries at 0x32C + index * 10:
     *   +0 source address, +4 term seed, +6 destination address. */
    rom_put_long(rom, 0x032Cu, 0x00FF8000u);
    rom_put_word(rom, 0x0330u, 0x0003u); /* 4 terms */
    rom_put_long(rom, 0x0332u, 0xF4000000u);

    rom_put_long(rom, 0x0336u, 0x00FF8100u);
    rom_put_word(rom, 0x033Au, 0x0000u); /* 1 term */
    rom_put_long(rom, 0x033Cu, 0xF4800020u);

    rom_put_long(rom, 0x0340u, 0x00FF8200u);
    rom_put_word(rom, 0x0344u, 0x0001u); /* 2 terms */
    rom_put_long(rom, 0x0346u, 0x02000000u);
}

/* The three packed-bitfield source arrays the test ROM points at. */
static void seed_packed_sources(fake_port_t *port)
{
    /* entry 0: four (value, mask, shift) triples
     *   (0x12,0x0F,1) -> (0x12 & 0x0F) << 1 = 0x0004
     *   (0xAB,0x0F,2) -> (0xAB & 0x0F) << 2 = 0x002C
     *   (0xF0,0xFF,0) -> 0x00F0
     *   (0xFF,0xFF,9) -> a 68010 WORD shift keeps bits 15..9 of the result:
     *                   0xFF << 9 is 0x1FE00, truncated to 0xFE00
     * ORed together: 0xFEFC */
    port->ram[0x8000] = 0x12u; port->ram[0x8001] = 0x0Fu; port->ram[0x8002] = 0x01u;
    port->ram[0x8003] = 0xABu; port->ram[0x8004] = 0x0Fu; port->ram[0x8005] = 0x02u;
    port->ram[0x8006] = 0xF0u; port->ram[0x8007] = 0xFFu; port->ram[0x8008] = 0x00u;
    port->ram[0x8009] = 0xFFu; port->ram[0x800A] = 0xFFu; port->ram[0x800B] = 0x09u;
    /* entry 1: one term, (0x7F,0x3C,3) -> (0x7F & 0x3C) << 3 = 0x1E0 */
    port->ram[0x8100] = 0x7Fu; port->ram[0x8101] = 0x3Cu; port->ram[0x8102] = 0x03u;
    /* entry 2: two terms, (0x01,0x01,4) and (0x80,0x80,7) -> 0x4010 */
    port->ram[0x8200] = 0x01u; port->ram[0x8201] = 0x01u; port->ram[0x8202] = 0x04u;
    port->ram[0x8203] = 0x80u; port->ram[0x8204] = 0x80u; port->ram[0x8205] = 0x07u;
}

static void test_upload_record_table(void)
{
    uint8_t rom[0x10000];
    fake_port_t port;
    stunrun_gsp_host_t io;

    memset(rom, 0, sizeof(rom));
    build_test_rom(rom);

    fake_reset(&port);
    fake_bind(&port, &io);
    port.hstctl = STUNRUN_GSP_HOSTCTL_AUTO_INCREMENT;
    stunrun_gsp_boot_upload_record_table(&io, rom);

    CHECK(port.fifo_count == 2u); /* seed 0x0001 -> seed + 1 records */
    CHECK(port.events[0].kind == FAKE_EVENT_ADDRESS_HI);
    CHECK(port.events[0].value == 0xF500u);
    CHECK(port.events[2].kind == FAKE_EVENT_FIFO);
    CHECK(port.events[2].address == 0xF5000000u);
    CHECK(port.events[2].value == 0x1234u);
    CHECK(port.events[3].value == 0xF580u);
    CHECK(port.events[5].address == 0xF5800000u);
    CHECK(port.events[5].value == 0x5678u);

    /* A zero seed still emits exactly one record: dbra counts seed + 1. */
    rom_put_word(rom, 0x02E8u, 0x0000u);
    fake_reset(&port);
    fake_bind(&port, &io);
    port.hstctl = STUNRUN_GSP_HOSTCTL_AUTO_INCREMENT;
    stunrun_gsp_boot_upload_record_table(&io, rom);
    CHECK(port.fifo_count == 1u);
}

static void test_write_packed_bitfield(void)
{
    uint8_t rom[0x10000];
    fake_port_t port;
    stunrun_gsp_host_t io;
    unsigned word;

    memset(rom, 0, sizeof(rom));
    build_test_rom(rom);

    for (word = 0; word < 3u; word++) {
        fake_reset(&port);
        fake_bind(&port, &io);
        seed_packed_sources(&port);
        port.hstctl = STUNRUN_GSP_HOSTCTL_AUTO_INCREMENT;
        stunrun_gsp_boot_write_packed_bitfield(&io, rom, (uint16_t)word);
        CHECK(port.fifo_count == 1u);
        if (word == 0u) {
            CHECK(port.events[2].address == 0xF4000000u);
            CHECK(port.events[2].value == 0xFEFCu);
        } else if (word == 1u) {
            CHECK(port.events[2].address == 0xF4800020u);
            CHECK(port.events[2].value == 0x01E0u);
        } else {
            CHECK(port.events[2].address == 0x02000000u);
            CHECK(port.events[2].value == 0x4010u);
        }
    }
}

static void test_program_display_window(void)
{
    uint8_t rom[0x10000];
    fake_port_t port;
    stunrun_gsp_host_t io;

    memset(rom, 0, sizeof(rom));
    build_test_rom(rom);

    /* Flag byte 0 -> the NOT is taken -> 0xFFFF, then & $FFFC | $3. */
    fake_reset(&port);
    fake_bind(&port, &io);
    port.hstctl = STUNRUN_GSP_HOSTCTL_AUTO_INCREMENT;
    stunrun_gsp_boot_program_display_window(&io, rom);
    CHECK(port.fifo_count == 2u);
    CHECK(port.events[0].kind == FAKE_EVENT_ADDRESS_HI);
    CHECK(port.events[0].value == 0xC000u);
    CHECK(port.events[1].kind == FAKE_EVENT_ADDRESS_LO);
    CHECK(port.events[1].value == 0x0090u);
    CHECK(port.events[2].kind == FAKE_EVENT_FIFO);
    CHECK(port.events[2].address == 0xC0000090u);
    CHECK(port.events[2].value == 0xFFFFu);
    CHECK(port.events[3].value == 0xF480u);
    CHECK(port.events[4].value == 0x0000u);
    CHECK(port.events[5].address == 0xF4800000u);
    CHECK(port.events[5].value == 0x0000u);

    /* Any non-zero flag byte skips the NOT, so the word is 0x0003. */
    rom[0x02F9] = 0x01u;
    fake_reset(&port);
    fake_bind(&port, &io);
    port.hstctl = STUNRUN_GSP_HOSTCTL_AUTO_INCREMENT;
    stunrun_gsp_boot_program_display_window(&io, rom);
    CHECK(port.events[2].value == 0x0003u);
}

/* ── 0x3F8: boot init ─────────────────────────────────────────────────── */

static void test_boot_init(void)
{
    uint8_t rom[0x10000];
    fake_port_t port;
    stunrun_gsp_host_t io;

    memset(rom, 0, sizeof(rom));
    build_test_rom(rom);

    fake_reset(&port);
    fake_bind(&port, &io);
    seed_packed_sources(&port);
    /* Log every latch/poke and the first 700 FIFO writes; the 230400-word main
     * VRAM fill that closes the routine is summarised instead. */
    port.fifo_log_limit = 700u;

    stunrun_gsp_boot_init(&io, rom);
    CHECK(!port.overflow);

    /* 0: clr.w $60400c.l, 1: clr.w $6041c.l, 2: move.w #$a000,$c00008.l.
     * The control write is a SET, not an OR - the only one in the boot path. */
    CHECK(port.events[0].kind == FAKE_EVENT_POKE);
    CHECK(port.events[0].address == STUNRUN_MAIN_ZP_LATCH_A);
    CHECK(port.events[1].address == STUNRUN_MAIN_ZP_LATCH_B);
    CHECK(port.events[2].kind == FAKE_EVENT_CONTROL_SET);
    CHECK(port.events[2].value == STUNRUN_GSP_HOSTCTL_BOOT_INIT);

    /* 3-5: bsr $37c with D0 = 2. */
    CHECK(port.events[3].kind == FAKE_EVENT_ADDRESS_HI);
    CHECK(port.events[3].value == 0x0200u);
    CHECK(port.events[5].kind == FAKE_EVENT_FIFO);
    CHECK(port.events[5].address == 0x02000000u);
    CHECK(port.events[5].value == 0x4010u);

    /* 6-11: bsr $34a, two records. */
    CHECK(port.events[6].value == 0xF500u);
    CHECK(port.events[8].address == 0xF5000000u);
    CHECK(port.events[8].value == 0x1234u);
    CHECK(port.events[9].value == 0xF580u);
    CHECK(port.events[11].address == 0xF5800000u);
    CHECK(port.events[11].value == 0x5678u);

    /* 12-14: bsr $7c2. */
    CHECK(port.events[12].value == 0xC000u);
    CHECK(port.events[13].value == 0x0150u);
    CHECK(port.events[14].address == 0xC0000150u);
    CHECK(port.events[14].value == 0x0001u);

    /* 15-20: bsr $3be. */
    CHECK(port.events[15].value == 0xC000u);
    CHECK(port.events[16].value == 0x0090u);
    CHECK(port.events[17].address == 0xC0000090u);
    CHECK(port.events[17].value == 0xFFFFu);
    CHECK(port.events[18].value == 0xF480u);
    CHECK(port.events[19].value == 0x0000u);
    CHECK(port.events[20].address == 0xF4800000u);
    CHECK(port.events[20].value == 0x0000u);

    /* 21-23: bsr $37c with D0 = 1. */
    CHECK(port.events[21].value == 0xF480u);
    CHECK(port.events[22].value == 0x0020u);
    CHECK(port.events[23].address == 0xF4800020u);
    CHECK(port.events[23].value == 0x01E0u);

    /* 24-26: bsr $37c with D0 = 0. */
    CHECK(port.events[24].value == 0xF400u);
    CHECK(port.events[26].address == 0xF4000000u);
    CHECK(port.events[26].value == 0xFEFCu);

    /* 27: bsr $48e opens with ori.w #$1800. */
    CHECK(port.events[27].kind == FAKE_EVENT_CONTROL_OR);
    CHECK(port.events[27].value == STUNRUN_GSP_HOSTCTL_AUTO_INCREMENT);

    /* 28-33: the two shiftreg clears inside 0x48e. */
    CHECK(port.events[28].value == 0xF480u);
    CHECK(port.events[29].value == 0x0020u);
    CHECK(port.events[30].address == 0xF4800020u);
    CHECK(port.events[31].value == 0xF480u);
    CHECK(port.events[32].value == 0x0030u);
    CHECK(port.events[33].address == 0xF4800030u);

    /* 34-35: the palette_lo latch, then 256 grey-ramp words. */
    CHECK(port.events[34].value == 0xF500u);
    CHECK(port.events[36].address == STUNRUN_GSP_PALETTE_LO);
    CHECK(port.events[36].value == 0x0000u);
    CHECK(port.events[291].address == STUNRUN_GSP_PALETTE_LO + 16u * 255u);
    CHECK(port.events[291].value == 0xFFFFu);

    /* 292-293: the palette_hi latch, then 256 blue words. */
    CHECK(port.events[292].value == 0xF580u);
    CHECK(port.events[294].address == STUNRUN_GSP_PALETTE_HI);
    CHECK(port.events[294].value == 0x0000u);
    CHECK(port.events[549].address == STUNRUN_GSP_PALETTE_HI + 16u * 255u);
    CHECK(port.events[549].value == 0x00FFu);

    /* 550-561: the 0x468 sequence twice - once at the end of 0x48E, then once
     * inlined by 0x3F8.  Each is ADDR_HI/ADDR_LO/FIFO. */
    CHECK(port.events[550].value == 0xF480u);
    CHECK(port.events[551].value == 0x0010u);
    CHECK(port.events[552].value == 0x0000u);
    CHECK(port.events[553].value == 0xF480u);
    CHECK(port.events[554].value == 0x0090u);
    CHECK(port.events[555].value == 0xFFFFu);
    CHECK(port.events[556].value == 0xF480u);
    CHECK(port.events[557].value == 0x0010u);
    CHECK(port.events[558].value == 0x0000u);
    CHECK(port.events[559].value == 0xF480u);
    CHECK(port.events[560].value == 0x0090u);
    CHECK(port.events[561].value == 0xFFFFu);

    /* 562: 0x67A's `ori.w #$1800` (HSTCTL becomes 0xA000 | 0x1800 = 0xB800).
     * 563-564: the main VRAM latch, then the 230400-word fill. */
    CHECK(port.events[562].kind == FAKE_EVENT_CONTROL_OR);
    CHECK(port.events[562].value == STUNRUN_GSP_HOSTCTL_AUTO_INCREMENT);
    CHECK(port.hstctl == 0xB800u);
    CHECK(port.events[563].kind == FAKE_EVENT_ADDRESS_HI);
    CHECK(port.events[563].value == 0xFF80u);
    CHECK(port.events[564].kind == FAKE_EVENT_ADDRESS_LO);
    CHECK(port.events[564].value == 0x0000u);

    /* Total FIFO words: 1 (bitfield) + 2 (records) + 1 (reset flag)
     * + 2 (display window) + 1 + 1 (bitfields) + 516 (0x48E: 2 shiftreg
     * + 256 + 256 + 2) + 2 (the 0x468 0x3F8 inlines)
     * + 230400 (main VRAM fill). */
    CHECK(port.fifo_count == 8u + 516u + 2u + 900u * 256u);
    CHECK(port.fifo_tail_count == 900u * 256u);
    CHECK(port.fifo_tail_value == 0x0000u);
    CHECK(port.fifo_tail_first_address == STUNRUN_GSP_MAIN_VRAM_BASE);
    /* Two ZP latch clears plus 900 watchdog kicks. */
    CHECK(port.poke_count == 2u + 900u);
}

/* ── Shared helper ────────────────────────────────────────────────────── */

static void test_push_word_block(void)
{
    fake_port_t port;
    stunrun_gsp_host_t io;

    /* 96 x 256 words with a watchdog kick per pass. */
    fake_reset(&port);
    fake_bind(&port, &io);
    port.fifo_log_limit = 0u;
    port.hstctl = STUNRUN_GSP_HOSTCTL_AUTO_INCREMENT;
    stunrun_gsp_push_word_block(&io, 0x02000000u, 0x1234u, 0x005Fu, 0x00FFu, 1);
    CHECK(port.fifo_count == 96u * 256u);
    CHECK(port.poke_count == 96u);
    CHECK(port.events[0].kind == FAKE_EVENT_ADDRESS_HI);
    CHECK(port.events[0].value == 0x0200u);
    CHECK(port.events[1].kind == FAKE_EVENT_ADDRESS_LO);

    /* Seeds count down from their own value, so seed 0 still runs once. */
    fake_reset(&port);
    fake_bind(&port, &io);
    port.hstctl = STUNRUN_GSP_HOSTCTL_AUTO_INCREMENT;
    stunrun_gsp_push_word_block(&io, 0x02000000u, 0x1234u, 0u, 0u, 0);
    CHECK(port.fifo_count == 1u);
    CHECK(port.poke_count == 0u);

    /* Without auto-increment every word lands on the same address. */
    fake_reset(&port);
    fake_bind(&port, &io);
    stunrun_gsp_push_word_block(&io, 0x02000000u, 0x1234u, 1u, 1u, 0);
    CHECK(port.fifo_count == 4u);
    CHECK(port.events[3].address == 0x02000000u);
}

/* ── NULL safety ──────────────────────────────────────────────────────── */

static void test_null_safety(void)
{
    fake_port_t port;
    stunrun_gsp_host_t io;

    fake_reset(&port);
    fake_bind(&port, &io);

    stunrun_gsp_latch_address(NULL, 0u);
    stunrun_gsp_push_word_block(NULL, 0u, 0u, 1u, 1u, 1);
    stunrun_gsp_boot_upload_record_table(NULL, NULL);
    stunrun_gsp_boot_bus_delay();
    stunrun_gsp_boot_write_packed_bitfield(NULL, NULL, 0u);
    stunrun_gsp_boot_program_display_window(NULL, NULL);
    stunrun_gsp_boot_init(NULL, NULL);
    stunrun_gsp_boot_clear_display_control(NULL);
    stunrun_gsp_boot_configure_display_and_clear_palette(NULL);
    stunrun_gsp_boot_write_ramp_pair(NULL, 0u, 0u);
    stunrun_gsp_boot_write_ramp_single(NULL, 0u);
    stunrun_gsp_boot_latch_draw_parameters(NULL);
    stunrun_gsp_boot_walk_palette(NULL);
    stunrun_gsp_boot_fill_2bpp_vram_ffff(NULL, 0u);
    stunrun_gsp_boot_fill_main_vram(NULL, 0u);
    stunrun_gsp_boot_fill_2bpp_vram_from_d1(NULL, 0u, 0u);
    stunrun_gsp_boot_load_palette_from_ram(NULL);
    stunrun_gsp_boot_stride_fill_2bpp_vram(NULL);
    stunrun_gsp_boot_short_delay();
    stunrun_gsp_boot_write_control_lo_word(NULL, 0u);
    stunrun_gsp_boot_program_reset_flag(NULL);
    CHECK(port.event_count == 0u);
}

int main(void)
{
    test_push_word_block();
    test_upload_record_table();
    test_write_packed_bitfield();
    test_program_display_window();
    test_clear_display_control();
    test_configure_display_and_clear_palette();
    test_ramps();
    test_latch_draw_parameters();
    test_walk_palette();
    test_fill_2bpp_vram_ffff();
    test_fill_main_vram();
    test_fill_2bpp_vram_from_d1();
    test_load_palette_from_ram();
    test_stride_fill_2bpp_vram();
    test_write_control_lo_word();
    test_program_reset_flag();
    test_boot_init();
    test_null_safety();

    if (failures == 0)
        puts("gsp-boot-slice: PASS");
    return failures != 0;
}