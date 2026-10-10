/* Self-check for the transcribed GSP display-list interpreter and sparse fill.
 *
 * Every expectation in this file is written out from the disassembly of
 * `136070-2102.200r` - see docs/mc68010-boot-findings.md §10 - NOT captured from
 * the transcription's own output.  Where a count is large enough that spelling
 * every word out would be unreadable (the sparse fill, the 0xB9A table) the
 * expectation is built from a hand-written table of the ROM's own bytes with a
 * loop whose bounds come from the instruction sequence, plus literal anchors
 * that pin the loop against off-by-one slips.
 *
 * The fake port models `tms340x0_device::host_w`
 * (devices/cpu/tms34010/tms34010.cpp:1644-1692): HSTADRH/HSTADRL latch the
 * address, HOST_DATA writes the word at (HSTADRH << 16 | HSTADRL) & 0xFFFFFFF0
 * and then, if HSTCTL bit 0x0800 is set, adds 0x10 back into the latches.
 *
 * The stream pointer's LOW BIT is observable - $2262c branches on it - so every
 * test that runs the interpreter states the parity it is exercising and every
 * stream is laid out so the byte offsets, and therefore the parities, can be
 * checked by counting.
 */
#include "gsp_display_list.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL: %s\n", #condition); \
        failures++; \
    } \
} while (0)

/* ── Fake TMS34010 host port + flat 68010 RAM ──────────────────────────── */

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

/* The 0xB9A table pushes 805 FIFO words and 100 address events. */
#define FAKE_EVENT_CAPACITY 4096

typedef struct {
    uint16_t hstadrh;
    uint16_t hstadrl;
    uint16_t hstctl;

    fake_event_t events[FAKE_EVENT_CAPACITY];
    unsigned event_count;
    int overflow;

    unsigned fifo_count;
    uint32_t fifo_first_address;
    uint16_t fifo_first_value;
    uint32_t fifo_last_address;
    uint16_t fifo_last_value;

    /* Flat 64 KiB at 0x000000: covers the lookup tables at 0x00FF900A and
     * 0x00FF902A and the watchdog at 0x00608000. */
    uint8_t ram[0x10000];
} fake_port_t;

static void fake_reset(fake_port_t *port)
{
    memset(port, 0, sizeof(*port));
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
    port->fifo_count++;
    port->fifo_last_address = address;
    port->fifo_last_value = value;
    fake_record(port, FAKE_EVENT_FIFO, address, value);

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
    fake_record(port, FAKE_EVENT_POKE, address, value);
}

static void fake_poke_byte(void *ctx, uint32_t address, uint8_t value)
{
    fake_port_t *port = (fake_port_t *)ctx;
    port->ram[address & 0xFFFFu] = value;
    fake_record(port, FAKE_EVENT_POKE, address, (uint16_t)value);
}

static void fake_bind(fake_port_t *port, stunrun_gsp_display_list_t *io)
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
    io->poke_byte = fake_poke_byte;
}

/* ── Expected-event helpers ───────────────────────────────────────────── */

/* The destination array and counter are macro parameters - suffixed so they
 * cannot collide with the fake_event_t field names they are expanded inside -
 * so the same macros can fill a local table or append to one inside a helper. */
#define APPEND_ADDRESS(exp_, n_, addr_) do { \
    exp_[n_].kind = FAKE_EVENT_ADDRESS_HI; \
    exp_[n_].address = 0u; \
    exp_[n_].value = (uint16_t)((addr_) >> 16); n_++; \
    exp_[n_].kind = FAKE_EVENT_ADDRESS_LO; \
    exp_[n_].address = 0u; \
    exp_[n_].value = (uint16_t)((addr_) & 0xFFFFu); n_++; \
} while (0)

#define APPEND_FIFO(exp_, n_, addr_, word_) do { \
    exp_[n_].kind = FAKE_EVENT_FIFO; \
    exp_[n_].address = (addr_); \
    exp_[n_].value = (uint16_t)(word_); n_++; \
} while (0)

#define APPEND_CONTROL_OR(exp_, n_, mask_) do { \
    exp_[n_].kind = FAKE_EVENT_CONTROL_OR; \
    exp_[n_].address = 0u; \
    exp_[n_].value = (uint16_t)(mask_); n_++; \
} while (0)

#define APPEND_POKE(exp_, n_, addr_, word_) do { \
    exp_[n_].kind = FAKE_EVENT_POKE; \
    exp_[n_].address = (addr_); \
    exp_[n_].value = (uint16_t)(word_); n_++; \
} while (0)

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

/* The interpreter emits to HSTDAT and stores to 68010 RAM only: it must never
 * touch the address latches.  Asserted separately because a zero-length
 * address prefix is invisible in the event comparison above. */
static void expect_no_address_writes(const char *what, const fake_port_t *port)
{
    unsigned i;

    for (i = 0; i < port->event_count; i++) {
        if (port->events[i].kind == FAKE_EVENT_ADDRESS_HI ||
            port->events[i].kind == FAKE_EVENT_ADDRESS_LO ||
            port->events[i].kind == FAKE_EVENT_CONTROL_SET ||
            port->events[i].kind == FAKE_EVENT_CONTROL_OR) {
            fprintf(stderr, "FAIL %s: event %u writes the host port (%d)\n",
                    what, i, port->events[i].kind);
            failures++;
            return;
        }
    }
}

/* Ordered list of FIFO words, ignoring where they landed. */
static void expect_fifo_words(const char *what, const fake_port_t *port,
                              const uint16_t *words, unsigned count)
{
    unsigned i;
    unsigned seen = 0;

    for (i = 0; i < port->event_count; i++) {
        if (port->events[i].kind != FAKE_EVENT_FIFO) {
            continue;
        }
        if (seen >= count) {
            fprintf(stderr, "FAIL %s: more than %u FIFO words\n", what, count);
            failures++;
            return;
        }
        if (port->events[i].value != words[seen]) {
            fprintf(stderr, "FAIL %s: FIFO word %u is 0x%04X, want 0x%04X\n",
                    what, seen, port->events[i].value, words[seen]);
            failures++;
            return;
        }
        seen++;
    }
    if (seen != count) {
        fprintf(stderr, "FAIL %s: %u FIFO words, %u expected\n", what, seen,
                count);
        failures++;
    }
}

/* ── The lookup table the streams load ────────────────────────────────── */

/* Stream bytes 1..16 below.  $22690 reads the byte at $FF902B + 2*index, i.e.
 * lookup-table byte index/2, so table byte T[i] comes out of the FIFO as the
 * 16-bit word $00T[i] whenever the opcode selects index 2*i. */
static const uint8_t table_A[16] = {
    0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7,
    0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF
};

/* 0x225B0: 16 iterations (dbra seed $000F), each consuming one stream byte and
 * storing it twice - as a BYTE at $FF900A + 2i and as a WORD at $FF902A + 2i. */
static unsigned expect_loader_pokes(fake_event_t *expected, unsigned n,
                                   const uint8_t *bytes)
{
    unsigned i;

    for (i = 0; i < 16u; i++) {
        APPEND_POKE(expected, n, STUNRUN_GSP_DL_TABLE_LO + 2u * i, bytes[i]);
        APPEND_POKE(expected, n, STUNRUN_GSP_DL_TABLE_HI + 2u * i, bytes[i]);
    }
    return n;
}

/* 0x22608: `clr.w (A3)` fires once per opcode, and $2260a reads the opcode AFTER
 * it - so the kick is INTERLEAVED with that opcode's words, not sitting in front
 * of them.  The caller passes the slice of `words` that opcode produces. */
static unsigned append_opcode(fake_event_t *expected, unsigned n,
                              uint32_t fifo_address, const uint16_t *words,
                              unsigned count)
{
    unsigned i;

    APPEND_POKE(expected, n, STUNRUN_GSP_DL_WATCHDOG, 0u);
    for (i = 0; i < count; i++) {
        APPEND_FIFO(expected, n, fifo_address, words[i]);
    }
    return n;
}

/* ── 0x225E4: the all-opcode stream ───────────────────────────────────── */

/* Byte offsets are load-bearing: $2262c branches on the parity of the pointer,
 * and the pointer the branch tests is the offset just PAST the opcode byte.
 *
 *   0        0x00     $22600 entry byte - a nonzero one would end the list
 *   1-16     A0..AF   the lookup table, loaded by $225b0
 *   17       0x45     table run: D0 = $45 & $3F = 5 iterations.  D2 is clear,
 *                     so $2267c indexes on the operand's LOW nibble ($22690).
 *   18-22    03 04 05 06 07   -> T[3] T[4] T[5] T[6] T[7]
 *   23       0x08     copy 8 units.  The pointer before the opcode byte is 23
 *                     (odd), so after it is 24 (even) and $22632 falls through
 *                     to the EVEN half at $22656, which emits whole words.
 *                     `lsr.b` gives 4 and the `subq.w` gives 3, so `dbra` runs
 *                     4 times over exactly the eight bytes below.
 *   24-31    4 big-endian words
 *   32       0xC2     table fill: D0 = 2.  $22702 never looks at the parity.
 *   33       0x37     LOW nibble 7 -> T[7], emitted twice ($2273e, $22730)
 *   34       0x84     fill: D0 = 4, so `lsr.w #1,D0` gives 2 iterations and the
 *                     count's low bit is 0, leaving `subx.w D2,D2` at zero
 *   35       0x99     the fill word
 *   36       0xC0     terminator ($22704 `beq $225ce`, D0 = $C0 & $3F = 0)
 */
static const uint8_t stream_all[] = {
    0x00,                                       /*  0: entry byte          */
    0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, /*  1-7:  table            */
    0xA7, 0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, /*  8-15: table            */
    0xAE, 0xAF,                                 /* 16:    table            */
    0x45,                                       /* 17: table run x5        */
    0x03, 0x04, 0x05, 0x06, 0x07,               /* 18-22: operands         */
    0x08,                                       /* 23: copy 8 units        */
    0x11, 0x22, 0x33, 0x44,                     /* 24-27: two words        */
    0x55, 0x66, 0x77, 0x88,                     /* 28-31: two more         */
    0xC2,                                       /* 32: table fill x2       */
    0x37,                                       /* 33: operand             */
    0x84,                                       /* 34: fill x2             */
    0x99,                                       /* 35: operand             */
    0xC0                                        /* 36: terminator          */
};

#define STREAM_ALL_WORDS 13
#define STREAM_ALL_BYTES 37
#define STREAM_ALL_OPCODES 5
/* 16 table bytes, each stored twice, plus one watchdog kick per opcode. */
#define STREAM_ALL_STORES (32u + STREAM_ALL_OPCODES)

static void test_stream_all_opcodes_even(void)
{
    /* 13 FIFO words, in order:
     *   $22690      x5  the table run's LOW-nibble lookups
     *   $2265e      x4  the copied words
     *   $2273e/$22730 x2  the table fill's single LOW-nibble lookup, twice
     *   $226f8      x2  the fill word
     */
    static const uint16_t expected_words[STREAM_ALL_WORDS] = {
        0x00A3u, 0x00A4u, 0x00A5u, 0x00A6u, 0x00A7u,
        0x1122u, 0x3344u, 0x5566u, 0x7788u,
        0x00A7u, 0x00A7u,
        0x0099u, 0x0099u
    };
    fake_event_t expected[64];
    fake_port_t port;
    stunrun_gsp_display_list_t io;
    stunrun_gsp_display_list_result_t result;
    /* How many words each of the five opcodes produces, from the instruction
     * sequence: 5 (table run), 4 (even-half copy of 8 bytes), 2 (table fill),
     * 2 (fill), 0 (the terminator). */
    static const unsigned split[STREAM_ALL_OPCODES + 1] = {
        0u, 5u, 9u, 11u, 13u, 13u
    };
    unsigned n = 0u;
    unsigned i;

    /* No auto-increment: the caller has not enabled HSTCTL $0800, so every
     * word lands on the single latched address $00000000. */
    n = expect_loader_pokes(expected, n, table_A);
    for (i = 0; i < STREAM_ALL_OPCODES; i++) {
        n = append_opcode(expected, n, 0x00000000u, &expected_words[split[i]],
                          split[i + 1u] - split[i]);
    }
    CHECK(n == STREAM_ALL_STORES + STREAM_ALL_WORDS);

    fake_reset(&port);
    fake_bind(&port, &io);
    CHECK((((uintptr_t)stream_all) & 1u) == 0u); /* the parity this pins */
    stunrun_gsp_display_list_run(&io, stream_all, &result);

    expect_events("all-opcodes", &port, expected, n);
    expect_no_address_writes("all-opcodes", &port);
    expect_fifo_words("all-opcodes", &port, expected_words, STREAM_ALL_WORDS);
    CHECK(port.fifo_count == STREAM_ALL_WORDS);
    CHECK(port.fifo_first_value == 0x00A3u);
    CHECK(port.fifo_last_value == 0x0099u);
    CHECK(result.words_emitted == STREAM_ALL_WORDS);
    CHECK(result.stream_end == STREAM_ALL_BYTES);
    CHECK(result.end == STUNRUN_GSP_DL_END_RESET);

    /* The lookup table the loader left behind: both copies, and the even half
     * of each word in the second copy never written. */
    CHECK(port.ram[0x900A] == 0xA0u && port.ram[0x900B] == 0x00u);
    CHECK(port.ram[0x902A] == 0x00u && port.ram[0x902B] == 0xA0u);
    CHECK(port.ram[0x9038] == 0x00u && port.ram[0x9039] == 0xA7u);
    CHECK(port.ram[0x9048] == 0x00u && port.ram[0x9049] == 0xAFu);
}

/* The same stream, with the caller's latched address and the $0800
 * post-increment the way 0x570 leaves it.  The interpreter does not latch
 * anything, so every word must land at the latched address plus 16*i. */
static void test_post_increment_address_walk(void)
{
    fake_port_t port;
    stunrun_gsp_display_list_t io;
    stunrun_gsp_display_list_result_t result;
    unsigned i;
    unsigned seen = 0;

    fake_reset(&port);
    fake_bind(&port, &io);
    port.hstadrh = 0x0200u;
    port.hstadrl = 0x1230u;
    port.hstctl = 0x0800u;

    stunrun_gsp_display_list_run(&io, stream_all, &result);

    CHECK(port.fifo_count == STREAM_ALL_WORDS);
    CHECK(port.fifo_first_address == 0x02001230u);
    /* Over EVERY event, not just the first fifo_count: the 32 loader stores and
     * the five watchdog kicks share the log with the words. */
    for (i = 0; i < port.event_count; i++) {
        /* the resolved address is masked to $10, then post-incremented */
        if (port.events[i].kind != FAKE_EVENT_FIFO) {
            continue;
        }
        if (port.events[i].address != 0x02001230u + 16u * seen) {
            fprintf(stderr,
                    "FAIL post-increment: word %u landed at 0x%08X, "
                    "want 0x%08X\n", seen, port.events[i].address,
                    0x02001230u + 16u * seen);
            failures++;
            break;
        }
        seen++;
    }
    CHECK(seen == STREAM_ALL_WORDS);
    /* The last word left the latches one step further on: 13 words x $10. */
    CHECK(port.hstadrl == 0x1300u);
    expect_no_address_writes("post-increment", &port);
}

/* ── 0x22602: a nonzero first byte ends the list ───────────────────────── */

static void test_stream_immediate_terminate(void)
{
    /* Any nonzero first byte: $22602 `bne $225ce`.  Nothing is loaded, no
     * watchdog is kicked, no word is emitted, and the byte is consumed. */
    static const uint8_t stream[] = { 0x7F, 0x45, 0x03 };
    fake_port_t port;
    stunrun_gsp_display_list_t io;
    stunrun_gsp_display_list_result_t result;

    fake_reset(&port);
    fake_bind(&port, &io);
    stunrun_gsp_display_list_run(&io, stream, &result);

    CHECK(port.event_count == 0u);
    CHECK(port.fifo_count == 0u);
    CHECK(result.words_emitted == 0u);
    CHECK(result.stream_end == 1u);
    CHECK(result.end == STUNRUN_GSP_DL_END_RESET);
}

/* ── Odd copy counts: SR-12, the parity flip ──────────────────────────── */

/* Three pending-flush and parity transitions in one stream.  $2266a reads a
 * WORD and advances ONE byte, so the leftover also leaves the pointer ODD;
 * $22622 (the other flush site) reads a byte it has NOT consumed yet, which
 * moves the pointer on BEFORE $2262c tests its parity; $22650 parks a byte
 * shifted a LONG way left, leaving D3.w = $0000.  None of the three ever emits
 * the parked word, so the observable difference is where the stream ends up.
 *
 * Pointer parity == byte offset here because the stream is asserted even (see
 * the CHECK below), so the offsets below can be counted rather than simulated.
 *
 *   0        0x00     entry byte
 *   1-16     A0..AF   table
 *   17       0x03     copy 3; pointer after the opcode is 18 (EVEN) -> $22656
 *   18,19    11 22    -> 0x1122                                     word 0
 *   20       9B       leftover: `move.w (A0),D3` = $9B43, `addq.l #1,A0`
 *                     -> pointer 21 (ODD), D2 = $FFFF, and $9B43 is dead
 *   21       43       table run x3 with D2 SET -> $226a8, which indexes on the
 *                     HIGH nibble ($226b2) and never clears D2
 *   22-24    91 22 33 -> T[9] T[2] T[3]                              words 1-3
 *   25       0x02     copy 2 with D2 SET -> $22622 flushes a FRESH stream byte
 *   26       0x77        emitted as 0x0077; D2 cleared; D0 = 1
 *                     pointer is now 27 (ODD) - the flush byte moved it
 *   27       55       count 1 cannot fill a pair, so this is the odd leftover:
 *                     `move.b` + `lsl.l #8` -> D3.w = $0000, D2 = $FFFF
 *   28       0x03     copy 3 with D2 SET -> flush again
 *   29       0x66        emitted as 0x0066; D2 cleared; D0 = 2
 *                     pointer is now 30 (EVEN) -> $22656
 *   30,31    77 88    -> 0x7788; an even count parks nothing     word 6
 *   32       0x03     copy 3, D2 clear; pointer after the opcode is 33 (ODD)
 *                     -> $22634, which consumes byte pairs high byte first -
 *                     but the `lsl.w #8` at $2263e is dead (a byte move into D3
 *                     clears the high half), so only 0x5B reaches the FIFO
 *   33,34    5A 5B    -> 0x005B                                     word 7
 *   35       6C       odd leftover, parked as $0000; D2 = $FFFF
 *   36       0xC0     terminator
 */
static const uint8_t stream_parity[] = {
    0x00,
    0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6,
    0xA7, 0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD,
    0xAE, 0xAF,
    0x03, 0x11, 0x22, 0x9B,
    0x43, 0x91, 0x22, 0x33,
    0x02, 0x77, 0x55,
    0x03, 0x66, 0x77, 0x88,
    0x03, 0x5A, 0x5B, 0x6C,
    0xC0
};

#define STREAM_PARITY_WORDS 8
#define STREAM_PARITY_BYTES 37

static void test_stream_odd_counts_even_pointer(void)
{
    static const uint16_t expected_words[STREAM_PARITY_WORDS] = {
        0x1122u,                         /* $2265e, even half, 3 -> 1 word     */
        0x00A9u, 0x00A2u, 0x00A3u,       /* $226b2, D2 set -> HIGH nibble      */
        0x0077u,                         /* $22622, the pending flush reads 77 */
        0x0066u,                         /* $22622 again, pointer now even     */
        0x7788u,                         /* $2265e, even half, 2 -> 1 word     */
        0x005Bu                          /* $22642, odd half; $2263e is dead  */
    };
    fake_port_t port;
    stunrun_gsp_display_list_t io;
    stunrun_gsp_display_list_result_t result;

    fake_reset(&port);
    fake_bind(&port, &io);
    CHECK((((uintptr_t)stream_parity) & 1u) == 0u);
    stunrun_gsp_display_list_run(&io, stream_parity, &result);

    expect_fifo_words("parity-even", &port, expected_words,
                      STREAM_PARITY_WORDS);
    CHECK(port.fifo_count == STREAM_PARITY_WORDS);
    CHECK(result.words_emitted == STREAM_PARITY_WORDS);
    CHECK(result.stream_end == STREAM_PARITY_BYTES);
    CHECK(result.end == STUNRUN_GSP_DL_END_RESET);
    expect_no_address_writes("parity-even", &port);

    /* The three leftovers the ROM parks are never emitted: $9B43 is read into
     * D3 at $2266a and left there, and the two $22650 leftovers both leave
     * D3.w = $0000.  None of the three appears in the log. */
    {
        unsigned i;

        for (i = 0; i < port.event_count; i++) {
            CHECK(port.events[i].kind != FAKE_EVENT_FIFO ||
                  port.events[i].value != 0x9B43u);
            CHECK(port.events[i].kind != FAKE_EVENT_FIFO ||
                  port.events[i].value != 0x0055u);
            CHECK(port.events[i].kind != FAKE_EVENT_FIFO ||
                  port.events[i].value != 0x006Cu);
        }
    }
}

/* ── 0x00 in mid-stream reloads the table ─────────────────────────────── */

/* Two tables, so a mid-stream reload is observable: the second table run must
 * read the SECOND table's bytes.
 *
 *   0        0x00     entry byte
 *   1-16     A0..AF   table 1
 *   17       0x00     opcode $00 -> $2261c `beq $22604` -> $225b0 again
 *   18-33    B0..BF   table 2
 *   34       0x42     table run x2, D2 clear -> LOW nibble
 *   35,36    0x02 0x04 -> T2[2] T2[4] = 0xB2 0xB4
 *   37       0xC0     terminator
 */
static const uint8_t stream_reload[] = {
    0x00,
    0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6,
    0xA7, 0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD,
    0xAE, 0xAF,
    0x00,
    0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6,
    0xB7, 0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD,
    0xBE, 0xBF,
    0x42, 0x02, 0x04,
    0xC0
};

static const uint8_t table_B[16] = {
    0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7,
    0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF
};

static void test_reload_opcode_00(void)
{
    static const uint16_t expected_words[2] = { 0x00B2u, 0x00B4u };
    fake_port_t port;
    stunrun_gsp_display_list_t io;
    stunrun_gsp_display_list_result_t result;
    unsigned i;

    fake_reset(&port);
    fake_bind(&port, &io);
    stunrun_gsp_display_list_run(&io, stream_reload, &result);

    expect_fifo_words("reload", &port, expected_words, 2u);
    CHECK(port.fifo_count == 2u);
    CHECK(result.stream_end == 38u);
    CHECK(result.end == STUNRUN_GSP_DL_END_RESET);

    /* 0x225b0 ran twice, so both tables were stored; the SECOND one is what
     * survives at $FF902B (and at $FF900A).  Every entry, not a spot check. */
    for (i = 0; i < 16u; i++) {
        CHECK(port.ram[0x900A + 2u * i] == table_B[i]);
        CHECK(port.ram[0x902B + 2u * i] == table_B[i]);
        CHECK(port.ram[0x902A + 2u * i] == 0x00u);
    }
}

/* ── Terminators ──────────────────────────────────────────────────────── */

/* 0x80 -> $226d4 `beq $225ce`; 0xC0 -> $22704 `beq $225ce`; 0x40 ->
 * $22674 `bmi $225d4`, the epilogue WITHOUT the jsr.  All three consume their
 * opcode byte and nothing else, so the pointer ends at 18: the entry byte, the
 * sixteen table bytes, and the opcode. */
static void test_terminators(void)
{
    static const uint8_t stream_80[] = {
        0x00, 0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6,
        0xA7, 0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF,
        0x80
    };
    static const uint8_t stream_c0[] = {
        0x00, 0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6,
        0xA7, 0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF,
        0xC0
    };
    /* 0x40 terminates before any operand byte is read: $22672 `subq.w #1,D0`
     * takes $0000 to $FFFF and the very next instruction branches. */
    static const uint8_t stream_40[] = {
        0x00, 0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6,
        0xA7, 0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF,
        0x40, 0xFF, 0xFF
    };
    fake_port_t port;
    stunrun_gsp_display_list_t io;
    stunrun_gsp_display_list_result_t result;

    fake_reset(&port);
    fake_bind(&port, &io);
    stunrun_gsp_display_list_run(&io, stream_80, &result);
    CHECK(port.fifo_count == 0u);
    CHECK(result.words_emitted == 0u);
    CHECK(result.stream_end == 18u);
    CHECK(result.end == STUNRUN_GSP_DL_END_RESET);

    fake_reset(&port);
    fake_bind(&port, &io);
    stunrun_gsp_display_list_run(&io, stream_c0, &result);
    CHECK(port.fifo_count == 0u);
    CHECK(result.stream_end == 18u);
    CHECK(result.end == STUNRUN_GSP_DL_END_RESET);

    fake_reset(&port);
    fake_bind(&port, &io);
    stunrun_gsp_display_list_run(&io, stream_40, &result);
    CHECK(port.fifo_count == 0u);
    CHECK(result.stream_end == 18u); /* the two 0xFF operands stay unread */
    CHECK(result.end == STUNRUN_GSP_DL_END_OPCODE_40);

    /* Each of the three kicked the watchdog exactly once (32 loader stores + 1). */
    CHECK(port.event_count == 33u);
}

/* ── 0x0BC4: the sparse fill ──────────────────────────────────────────── */

/* The ROM's own record table, read out of the ROM at 0xB9A:
 *
 *   000b9a: 00 04 | 00 10 00 04 00 09 05 05 | 00 10 00 06 00 09 0a 0a
 *   000baa:         00 10 00 08 00 09 10 10 | 00 10 00 0a 00 09 64 64
 *   000bba:         00 10 00 0c 00 09 ff ff
 *
 * $0004 is the count seed, so `dbra D5,$bce` runs five passes - and the five
 * 8-byte records end at 0xBC4, which is the first byte of the routine.  Each
 * record is [row_term, tile, columns, control] as the ROM addresses it.
 */
static const uint8_t rom_table_0xb9a[2 + 5 * 8] = {
    0x00, 0x04,
    0x00, 0x10, 0x00, 0x04, 0x00, 0x09, 0x05, 0x05,
    0x00, 0x10, 0x00, 0x06, 0x00, 0x09, 0x0A, 0x0A,
    0x00, 0x10, 0x00, 0x08, 0x00, 0x09, 0x10, 0x10,
    0x00, 0x10, 0x00, 0x0A, 0x00, 0x09, 0x64, 0x64,
    0x00, 0x10, 0x00, 0x0C, 0x00, 0x09, 0xFF, 0xFF
};

/* The ROM addresses every record field as a WORD - `move.w (A0),D1`,
 * `move.w (2,A0),D0`, `move.w (4,A0),D1`, `move.w (6,A0),$c0000c.l` - so the
 * byte arrays above are read in pairs here. */
static unsigned rom_record_word(unsigned record, unsigned field)
{
    const uint8_t *p = &rom_table_0xb9a[2 + record * 8 + field];

    return ((unsigned)p[0] << 8) | (unsigned)p[1];
}

/* 0x2418: D0 = $02000000 + (tile & $FF) * $2800 + (row_term & $FF) * 16. */
static uint32_t rom_base_address(unsigned record)
{
    unsigned row_term = rom_record_word(record, 0);
    unsigned tile = rom_record_word(record, 2);

    return 0x02000000u + (tile & 0xFFu) * 0x2800u + (row_term & 0xFFu) * 16u;
}

/* One record: 10 address latches (1 control + 1 base + 8 row re-latches) and
 * 1 + 8 * 2 * (columns + 1) FIFO words.  `dbra D2` runs columns + 1 times, and
 * each pass writes the pair at $0C06/$0C08. */
static unsigned append_sparse_fill_record(fake_event_t *expected, unsigned n,
                                           unsigned record)
{
    unsigned columns = rom_record_word(record, 4);
    uint16_t control = (uint16_t)rom_record_word(record, 6);
    uint32_t base = rom_base_address(record);
    unsigned row;
    unsigned i;

    /* 00000bc4: ori.w #$1800,$c00008.l */
    if (record == 0u) {
        APPEND_CONTROL_OR(expected, n, STUNRUN_GSP_DL_HOSTCTL_AUTO_INCREMENT);
    }
    /* 00000bda: move.l #$f4000000,$c00002.l */
    APPEND_ADDRESS(expected, n, STUNRUN_GSP_DL_CONTROL_LO);
    /* 00000be4: move.w (6,A0),$c0000c.l */
    APPEND_FIFO(expected, n, STUNRUN_GSP_DL_CONTROL_LO, control);
    /* 00000bfa: move.l D0,$c00002.l */
    APPEND_ADDRESS(expected, n, base);

    /* 00000c06/$0c08 x (columns + 1), eight times, with `dbra D4`.
     * Each FIFO write post-increments by $10, so the pair lands on consecutive
     * words; `addi.l #$400,D0` then re-latches a whole row further on. */
    for (row = 0; row < 8u; row++) {
        for (i = 0; i < (columns + 1u) * 2u; i++) {
            APPEND_FIFO(expected, n, base + 0x400u * row + 0x10u * i, 0xFFFFu);
        }
        /* 00000c14: move.l D0,$c00002.l - including the last one, whose value
         * is immediately discarded by $0c22. */
        APPEND_ADDRESS(expected, n, base + 0x400u * (row + 1u));
    }
    return n;
}

#define ROM_TABLE_RECORDS 5
#define ROM_TABLE_LATCHES 50 /* 10 per record: 1 control + 1 base + 8 rows   */
#define ROM_TABLE_ROWS 8
#define ROM_TABLE_FIFO_WORDS 805 /* 1 + 8 * 2 * 10 per record                */
/* Per record: 5 events for the control latch pair + its word + the base latch
 * pair, then eight rows of (columns + 1) * 2 = 20 words and one re-latch. */
#define ROM_TABLE_EVENTS_PER_RECORD (5u + ROM_TABLE_ROWS * (20u + 2u))

static void test_sparse_fill_rom_table(void)
{
    fake_event_t expected[1024];
    fake_port_t port;
    stunrun_gsp_display_list_t io;
    unsigned n = 0u;
    unsigned record;
    unsigned i;

    for (record = 0; record < ROM_TABLE_RECORDS; record++) {
        n = append_sparse_fill_record(expected, n, record);
    }
    CHECK(n == 1u + ROM_TABLE_RECORDS * ROM_TABLE_EVENTS_PER_RECORD);

    /* Literal anchors for $2418, so the base addresses can be checked by hand
     * against the table above. */
    CHECK(rom_base_address(0) == 0x0200A100u);
    CHECK(rom_base_address(1) == 0x0200F100u);
    CHECK(rom_base_address(2) == 0x02014100u);
    CHECK(rom_base_address(3) == 0x02019100u);
    CHECK(rom_base_address(4) == 0x0201E100u);

    fake_reset(&port);
    fake_bind(&port, &io);
    stunrun_gsp_display_list_sparse_fill(&io, rom_table_0xb9a);

    expect_events("sparse-fill-0xb9a", &port, expected, n);
    CHECK(port.fifo_count == ROM_TABLE_FIFO_WORDS);
    CHECK(port.fifo_first_address == STUNRUN_GSP_DL_CONTROL_LO);
    CHECK(port.fifo_first_value == 0x0505u);
    CHECK(port.fifo_last_value == 0xFFFFu);
    /* the final row of the final record: base + 0x400 * 7 + 0x10 * 19 */
    CHECK(port.fifo_last_address == 0x0201E100u + 0x400u * 7u + 0x10u * 19u);
    CHECK(port.hstctl & 0x0800u);

    /* The five control words, in order: record + 6, pushed to $F4000000. */
    {
        static const uint16_t controls[ROM_TABLE_RECORDS] = {
            0x0505u, 0x0A0Au, 0x1010u, 0x6464u, 0xFFFFu
        };
        unsigned seen = 0;

        for (i = 0; i < port.fifo_count; i++) {
            if (port.events[i].kind != FAKE_EVENT_FIFO ||
                port.events[i].address != STUNRUN_GSP_DL_CONTROL_LO) {
                continue;
            }
            CHECK(seen < ROM_TABLE_RECORDS);
            if (seen < ROM_TABLE_RECORDS) {
                CHECK(port.events[i].value == controls[seen]);
                seen++;
            }
        }
        CHECK(seen == ROM_TABLE_RECORDS);
    }

    /* The address latches: $0800 - 1 control latch per record, i.e. 50. */
    {
        unsigned lo_latches = 0;

        for (i = 0; i < port.event_count; i++) {
            if (port.events[i].kind == FAKE_EVENT_ADDRESS_LO) {
                lo_latches++;
            }
        }
        CHECK(lo_latches == ROM_TABLE_LATCHES);
    }
}

/* A single record, spelled out, so the $0BF4 nest's shape is legible without
 * walking the 805-word log above.
 *
 *   count seed $0000 -> `dbra D5` runs once
 *   row_term $0010, tile $0004, columns $0001 -> 2 pairs per row
 *   control $0505
 *   base = $02000000 + 4 * $2800 + $10 * 16 = $0200A100
 *
 * So: 1 control latch + 1 control word + 1 base latch + 8 rows x 4 words, with a
 * re-latch after every row (8 of them, the last one dead).  10 latches, 33
 * words.  Note $0BF4 calls NOTHING - the whole nest is straight-line code.
 */
static const uint8_t sparse_fill_one[] = {
    0x00, 0x00,
    0x00, 0x10, 0x00, 0x04, 0x00, 0x01, 0x05, 0x05
};

#define SPARSE_FILL_ONE_LATCHES 10
#define SPARSE_FILL_ONE_WORDS 33

static void test_sparse_fill_single_record(void)
{
    fake_event_t expected[64];
    fake_port_t port;
    stunrun_gsp_display_list_t io;
    unsigned n = 0u;
    unsigned row;

    APPEND_CONTROL_OR(expected, n, STUNRUN_GSP_DL_HOSTCTL_AUTO_INCREMENT);
    APPEND_ADDRESS(expected, n, STUNRUN_GSP_DL_CONTROL_LO);
    APPEND_FIFO(expected, n, STUNRUN_GSP_DL_CONTROL_LO, 0x0505u);
    APPEND_ADDRESS(expected, n, 0x0200A100u);
    for (row = 0; row < 8u; row++) {
        unsigned i;

        for (i = 0; i < 4u; i++) {
            APPEND_FIFO(expected, n, 0x0200A100u + 0x400u * row + 0x10u * i, 0xFFFFu);
        }
        APPEND_ADDRESS(expected, n, 0x0200A100u + 0x400u * (row + 1u));
    }
    CHECK(n == 1u + SPARSE_FILL_ONE_LATCHES * 2u + SPARSE_FILL_ONE_WORDS);

    fake_reset(&port);
    fake_bind(&port, &io);
    stunrun_gsp_display_list_sparse_fill(&io, sparse_fill_one);
    expect_events("sparse-fill-one", &port, expected, n);

    /* The eight row addresses, spelled out: the $0400 stride is the whole
     * point of the shared `dbra D4` target. */
    CHECK(expected[4].kind == FAKE_EVENT_ADDRESS_HI);
    CHECK(expected[4].value == 0x0200u);
    CHECK(expected[5].kind == FAKE_EVENT_ADDRESS_LO);
    CHECK(expected[5].value == 0xA100u);
    /* $0c14 relatches eight times, so the last one is $0200A100 + $0400 * 8.
     * Its value is thrown away by $0c22, but the port still saw it. */
    CHECK(port.hstadrl == 0xC100u);

    /* 33 words: the one control word plus 32 0xFFFF writes. */
    CHECK(port.fifo_count == SPARSE_FILL_ONE_WORDS);
    CHECK(port.fifo_first_value == 0x0505u);
    CHECK(port.fifo_last_value == 0xFFFFu);
    CHECK(port.fifo_last_address == 0x0200A100u + 0x400u * 7u + 0x30u);
}

/* ── 0x0C46: the $20-stride fill ──────────────────────────────────────── */

static void test_fill_stride_0x20(void)
{
    fake_event_t expected[3 * 32 + 8];
    fake_port_t port;
    stunrun_gsp_display_list_t io;
    unsigned n = 0u;
    unsigned i;

    /* 00000c48: move.w #$20,D4 -> 32 passes; each latches D3 and pushes D2,
     * then `addi.l #$20,D3`.  The $0800 post-increment from the caller also
     * steps the latches by $10, so the two together leave the writes on a $20
     * grid with one word occupied.  The loop is `subq.w #1,D4`/`bne`, NOT
     * `dbra`, so the count is the literal 32 and there is no 33rd write at
     * base + $400. */
    for (i = 0; i < 32u; i++) {
        APPEND_ADDRESS(expected, n, 0x02004000u + 0x20u * i);
        APPEND_FIFO(expected, n, 0x02004000u + 0x20u * i, 0x1234u);
    }
    CHECK(n == 96u);

    fake_reset(&port);
    fake_bind(&port, &io);
    port.hstctl = 0x0800u;
    stunrun_gsp_display_list_fill_stride_0x20(&io, 0x02004000u, 0x1234u);
    expect_events("fill-stride", &port, expected, n);
    CHECK(port.fifo_count == 32u);
    CHECK(port.fifo_last_address == 0x02004000u + 0x20u * 31u);
}

/* ── 0x225B0 on its own ───────────────────────────────────────────────── */

static void test_fill_stride_blocks(void)
{
    fake_event_t expected[15 * 32 * 3];
    unsigned n = 0u;
    unsigned block, column, enabled;
    /* Literal SUBQ/BNE seed 15, NOT a DBRA seed. */
    for (block = 0; block < 15u; block++) {
        for (column = 0; column < 32u; column++) {
            uint32_t address = 0x02000000u + block * 0x4000u + column * 0x20u;
            APPEND_ADDRESS(expected, n, address);
            APPEND_FIFO(expected, n, address, 1u);
        }
    }
    CHECK(n == 1440u);
    for (enabled = 0; enabled < 2u; enabled++) {
        fake_port_t port;
        stunrun_gsp_display_list_t io;
        fake_reset(&port);
        fake_bind(&port, &io);
        port.hstctl = (uint16_t)(0xa000u | (enabled ? 0x0800u : 0u));
        stunrun_gsp_display_list_fill_stride_blocks(&io);
        expect_events("fill-stride-blocks", &port, expected, n);
        CHECK(port.fifo_count == 480u);
        CHECK(port.fifo_first_address == 0x02000000u);
        CHECK(port.fifo_last_address == 0x020383e0u);
        CHECK(port.hstadrh == 0x0203u);
        CHECK(port.hstadrl == (enabled ? 0x83f0u : 0x83e0u));
        CHECK(port.hstctl == (uint16_t)(0xa000u | (enabled ? 0x0800u : 0u)));
    }
}

static void test_load_tables(void)
{
    fake_event_t expected[40];
    fake_port_t port;
    stunrun_gsp_display_list_t io;
    uint32_t consumed = 0u;
    unsigned n = 0u;

    fake_reset(&port);
    fake_bind(&port, &io);
    n = expect_loader_pokes(expected, n, table_A);
    CHECK(n == 32u);

    stunrun_gsp_display_list_load_tables(&io, table_A, &consumed);
    expect_events("load-tables", &port, expected, n);
    CHECK(consumed == 16u);

    /* Both copies of byte i, and the even half of the second copy left at 0. */
    CHECK(port.ram[STUNRUN_GSP_DL_TABLE_LO & 0xFFFFu] == 0xA0u);
    CHECK(port.ram[(STUNRUN_GSP_DL_TABLE_LO & 0xFFFFu) + 1u] == 0x00u);
    CHECK(port.ram[(STUNRUN_GSP_DL_TABLE_LO & 0xFFFFu) + 30u] == 0xAFu);
    CHECK(port.ram[(STUNRUN_GSP_DL_TABLE_HI & 0xFFFFu)] == 0x00u);
    CHECK(port.ram[(STUNRUN_GSP_DL_TABLE_HI & 0xFFFFu) + 1u] == 0xA0u);
    CHECK(port.ram[(STUNRUN_GSP_DL_TABLE_HI & 0xFFFFu) + 31u] == 0xAFu);

    /* And what $22690 reads back: the byte at $FF902B + 2*index. */
    CHECK(fake_peek_byte(&port, STUNRUN_GSP_DL_TABLE_HI + 1u) == 0xA0u);
    CHECK(fake_peek_byte(&port, STUNRUN_GSP_DL_TABLE_HI + 1u + 18u) == 0xA9u);
    CHECK(fake_peek_byte(&port, STUNRUN_GSP_DL_TABLE_HI + 1u + 30u) == 0xAFu);
}

/* ── NULL safety ──────────────────────────────────────────────────────── */

static void test_null_safety(void)
{
    fake_port_t port;
    stunrun_gsp_display_list_t io;
    stunrun_gsp_display_list_result_t result;
    uint32_t consumed = 1u;

    fake_reset(&port);
    fake_bind(&port, &io);

    stunrun_gsp_display_list_run(NULL, stream_all, &result);
    stunrun_gsp_display_list_run(&io, NULL, &result);
    stunrun_gsp_display_list_run(&io, stream_all, NULL);
    stunrun_gsp_display_list_load_tables(NULL, table_A, &consumed);
    stunrun_gsp_display_list_load_tables(&io, NULL, &consumed);
    CHECK(consumed == 0u);
    stunrun_gsp_display_list_sparse_fill(NULL, sparse_fill_one);
    stunrun_gsp_display_list_sparse_fill(&io, NULL);
    stunrun_gsp_display_list_fill_stride_0x20(NULL, 0u, 0u);
    stunrun_gsp_display_list_fill_stride_blocks(NULL);
    CHECK(port.event_count == 0u);
}

int main(void)
{
    test_stream_all_opcodes_even();
    test_post_increment_address_walk();
    test_stream_immediate_terminate();
    test_stream_odd_counts_even_pointer();
    test_reload_opcode_00();
    test_terminators();
    test_sparse_fill_rom_table();
    test_sparse_fill_single_record();
    test_fill_stride_0x20();
    test_fill_stride_blocks();
    test_load_tables();
    test_null_safety();

    if (failures == 0)
        puts("gsp-display-list-slice: PASS");
    return failures != 0;
}
