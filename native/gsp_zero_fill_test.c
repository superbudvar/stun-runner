/* Tests for the MC68010 zero-fill slice at ROM $548E-$54C0.
 *
 * The load-bearing assertion is a NEGATIVE one, and it is the reason this
 * routine has a slice at all: all 230,400 data writes are issued to $C0000C,
 * and $C0000C does not decode under the pinned driver, so the routine cannot
 * alter VRAM.  That is checked here as a fact about the recorded bus stream --
 * every cycle is classified through the slice's own published decode predicate
 * and the tally is asserted -- rather than as an absence, because "the test saw
 * no VRAM write" is indistinguishable from "the test could not have seen one".
 *
 * Note what the tally is NOT: `live_writes` is 2, not 0.  The `move.l
 * #$ff800000,$c00002.l` at $5496 lands, as HSTADRL := $FF80 and HSTCTL := $0000.
 * What is zero is writes reaching HSTDAT, and that is the claim that means the
 * routine cannot move a byte in VRAM.
 *
 * The test deliberately does NOT read the loop counts out of the header.  It
 * asserts the ROM's literals -- 900 passes, 256 words per pass, 230,400 data
 * writes, $1800, $FF800000, a 256-cycle watchdog cadence -- transcribed from the
 * bytes at $548E-$54C0 of
 *
 *   roms/stunrun/stunrun_maincpu.bin
 *   sha256 29c911a21d1abe780419cd3a924080756490f2d008776995dd48c7b2bedc5b6c
 *
 * and only then checks the header's copy against them.  A fixture placed at
 * whatever the header says moves with a wrong constant and the test still
 * passes, which is the trap findings section 15.6 describes; that is why the
 * equality check on the constants exists and why it is not the only check.
 * It is also what lets the fault injection work: mutating the loop seeds in a
 * scratch copy of gsp_zero_fill.c must make this test FAIL, and it does --
 * see the end of the sibling findings section for the four mutants and the
 * diagnostics each one produced.
 *
 * No ROM is needed to build or run this test.
 */
#include "gsp_zero_fill.h"

#include <stdio.h>
#include <string.h>

#define CHECK(x)                                                             \
    do {                                                                     \
        if (!(x)) {                                                          \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__,          \
                    __LINE__, #x);                                           \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static int check_eq(unsigned long long got, unsigned long long want,
                    const char *what, int line)
{
    if (got != want) {
        fprintf(stderr, "%s:%d: %s = %llu (0x%llX), expected %llu (0x%llX)\n",
                __FILE__, line, what, got, got, want, want);
        return 0;
    }
    return 1;
}

#define CHECK_EQ(got, want)                                                  \
    do {                                                                     \
        if (!check_eq((unsigned long long)(got), (unsigned long long)(want),  \
                      #got, __LINE__))                                       \
            return 1;                                                        \
    } while (0)

/* ── ROM literals, transcribed from the bytes ─────────────────────────── */

#define ROM_CONTROL 0x00C00008u     /* $548E ori.w #$1800,$c00008.l */
#define ROM_ADDRESS_HIGH 0x00C00002u /* $5496 move.l #$ff800000,$c00002.l */
#define ROM_ADDRESS_LOW 0x00C00004u  /* ... its second word */
#define ROM_DATA 0x00C0000Cu         /* $54AC move.w D0,$c0000c.l */
#define ROM_WATCHDOG 0x00608000u     /* $54B6 clr.w $608000.l */
#define ROM_CONTROL_OR 0x1800u
#define ROM_GSP_BASE 0xFF800000u
#define ROM_SEED 0x0000u
#define ROM_PASS_SEED 0x0383u /* $54A4 move.w #$383,D2 */
#define ROM_WORD_SEED 0x00FFu /* $54A8 move.w #$ff,D1  */
#define ROM_PASSES 0x0384u   /* `dbra` = seed + 1 */
#define ROM_WORDS_PER_PASS 0x0100u
#define ROM_DATA_WRITES 0x00038400u /* 900 * 256 = 230400 */
#define ROM_CONTINUATION 0x00005082u /* $507A lea ($6,PC),A0 */

#define ROM_READS 1u
#define ROM_WRITES (1u + 2u + ROM_DATA_WRITES + ROM_PASSES) /* 231303 */

/* The stream, indexed from 0.  Cycle 0 is the `ori.w` read half; cycles 1..3
 * are the control store and the two address words; after that the body is 900
 * identical groups of 256 data writes then one watchdog. */
#define GROUP_FIRST 4u
#define GROUP_STRIDE (ROM_WORDS_PER_PASS + 1u)          /* 257 */
#define TOTAL_CYCLES (GROUP_FIRST + GROUP_STRIDE * ROM_PASSES)
#define WATCHDOG_K(k) (GROUP_FIRST + GROUP_STRIDE * (k) + ROM_WORDS_PER_PASS)

/* ── Stream recorder ──────────────────────────────────────────────────── */

typedef struct cycle {
    uint32_t address;
    uint16_t value;
    uint8_t is_read;
} cycle_t;

/* 231,304 cycles x 12 bytes is 2.8 MB, so this is file scope: it would not fit
 * on a default stack. */
static cycle_t g_cycles[TOTAL_CYCLES];
static unsigned long g_cycle_count;
static unsigned long g_overflow;

static void record(uint32_t address, uint16_t value, uint8_t is_read)
{
    if (g_cycle_count >= (unsigned long)TOTAL_CYCLES) {
        g_overflow++;
        return;
    }
    g_cycles[g_cycle_count].address = address;
    g_cycles[g_cycle_count].value = value;
    g_cycles[g_cycle_count].is_read = is_read;
    g_cycle_count++;
}

static void on_read(void *ctx, uint32_t address, uint16_t value)
{
    (void)ctx;
    record(address, value, 1u);
}

static void on_write(void *ctx, uint32_t address, uint16_t value)
{
    (void)ctx;
    record(address, value, 0u);
}

static int expect_cycle(unsigned long index, uint8_t is_read, uint32_t address,
                        uint16_t value)
{
    const cycle_t *c;

    if (index >= g_cycle_count) {
        fprintf(stderr, "cycle %lu: never reached (stream has %lu cycles)\n",
                index, g_cycle_count);
        return 0;
    }
    c = &g_cycles[index];
    if (c->is_read != is_read || c->address != address || c->value != value) {
        fprintf(stderr,
                "cycle %lu = %s $%08lX = $%04X, expected %s $%08lX = $%04X\n",
                index, c->is_read ? "read " : "write",
                (unsigned long)c->address, (unsigned)c->value,
                is_read ? "read " : "write", (unsigned long)address,
                (unsigned)value);
        return 0;
    }
    return 1;
}

/* ── The decode predicate, on its own ─────────────────────────────────── */

/* The decode is the whole finding, so it is pinned directly and not only
 * through the routine. */
static int test_decode(void)
{
    struct {
        uint32_t address;
        int decode;
        const char *name;
    } cases[] = {
        {0x00C00000u, STUNRUN_HOSTPORT_HSTADRH, "HSTADRH"},
        {0x00C00002u, STUNRUN_HOSTPORT_HSTADRL, "HSTADRL"},
        {0x00C00004u, STUNRUN_HOSTPORT_HSTCTL, "HSTCTL"},
        {0x00C00006u, STUNRUN_HOSTPORT_HSTDAT, "HSTDAT"},
        /* $C00008 and $C0000C: the two this routine writes, both dropped. */
        {0x00C00008u, STUNRUN_HOSTPORT_DISCARDED, "discarded"},
        {0x00C0000Cu, STUNRUN_HOSTPORT_DISCARDED, "discarded"},
        /* The rest of the 16 KB window, sampled at the far end too. */
        {0x00C0000Au, STUNRUN_HOSTPORT_DISCARDED, "discarded"},
        {0x00C03FFEu, STUNRUN_HOSTPORT_DISCARDED, "discarded"},
        /* An odd word offset cannot occur on a 16-bit bus, because the
         * handler is reached through a word-wide delegate that shifts the
         * offset right by one.  Classified anyway so the predicate is total. */
        {0x00C00001u, STUNRUN_HOSTPORT_DISCARDED, "discarded"},
        /* Outside the window.  $608000 is the watchdog, and it is NOT a
         * decode: "dropped by the driver" and "not the driver's port" are
         * different answers and must not be conflated. */
        {0x00608000u, STUNRUN_HOSTPORT_OUTSIDE, "outside"},
        {0x00BFFFFFu, STUNRUN_HOSTPORT_OUTSIDE, "outside"},
        {0x00C04000u, STUNRUN_HOSTPORT_OUTSIDE, "outside"},
    };
    size_t i;

    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); i++) {
        int got = (int)stunrun_gsp_zero_fill_classify(cases[i].address);
        const char *name = stunrun_gsp_zero_fill_decode_name(got);
        if (got != cases[i].decode || strcmp(name, cases[i].name) != 0) {
            fprintf(stderr, "classify($%08lX) = %s (%d), expected %s (%d)\n",
                    (unsigned long)cases[i].address, name, got, cases[i].name,
                    cases[i].decode);
            return 1;
        }
    }
    return 0;
}

/* ── The header's constants, audited against the ROM ─────────────────── */

/* Compares against literals, never against the other constants: two wrong
 * constants that agree with each other must not pass. */
static int test_constants(void)
{
    CHECK_EQ(STUNRUN_GSP_ZERO_FILL_CONTROL, ROM_CONTROL);
    CHECK_EQ(STUNRUN_GSP_ZERO_FILL_ADDRESS_HIGH, ROM_ADDRESS_HIGH);
    CHECK_EQ(STUNRUN_GSP_ZERO_FILL_ADDRESS_LOW, ROM_ADDRESS_LOW);
    CHECK_EQ(STUNRUN_GSP_ZERO_FILL_DATA, ROM_DATA);
    CHECK_EQ(STUNRUN_GSP_ZERO_FILL_WATCHDOG, ROM_WATCHDOG);
    CHECK_EQ(STUNRUN_GSP_ZERO_FILL_CONTROL_OR, ROM_CONTROL_OR);
    CHECK_EQ(STUNRUN_GSP_ZERO_FILL_GSP_BASE, ROM_GSP_BASE);
    CHECK_EQ(STUNRUN_GSP_ZERO_FILL_SEED, ROM_SEED);
    CHECK_EQ(STUNRUN_GSP_ZERO_FILL_PASS_SEED, ROM_PASS_SEED);
    CHECK_EQ(STUNRUN_GSP_ZERO_FILL_WORD_SEED, ROM_WORD_SEED);
    CHECK_EQ(STUNRUN_GSP_ZERO_FILL_PASSES, ROM_PASSES);
    CHECK_EQ(STUNRUN_GSP_ZERO_FILL_WORDS_PER_PASS, ROM_WORDS_PER_PASS);
    CHECK_EQ(STUNRUN_GSP_ZERO_FILL_DATA_WRITES, ROM_DATA_WRITES);
    CHECK_EQ(STUNRUN_GSP_ZERO_FILL_PC, 0x0000548Eu);
    CHECK_EQ(STUNRUN_GSP_ZERO_FILL_PC_END, 0x000054C2u);
    CHECK_EQ(STUNRUN_GSP_ZERO_FILL_CALLER_BRA, 0x0000507Eu);
    CHECK_EQ(STUNRUN_GSP_ZERO_FILL_BOOT_CONTINUATION, ROM_CONTINUATION);
    CHECK_EQ(STUNRUN_GSP_HOST_WINDOW_BASE, 0x00C00000u);
    CHECK_EQ(STUNRUN_GSP_HOST_WINDOW_END, 0x00C04000u);
    /* The off-by-one is the arithmetic a reader is most likely to get wrong,
     * so it is spelled out rather than left to the derivation. */
    CHECK_EQ(ROM_DATA_WRITES, (unsigned long)(ROM_PASSES * ROM_WORDS_PER_PASS));
    CHECK_EQ(ROM_DATA_WRITES, 230400u);
    CHECK_EQ(ROM_WRITES, 231303u);
    return 0;
}

/* ── The routine ──────────────────────────────────────────────────────── */

static int test_routine(void)
{
    stunrun_gsp_zero_fill_port_t port;
    stunrun_gsp_zero_fill_result_t r;
    unsigned long live = 0u, discarded = 0u, outside = 0u, live_data = 0u;
    unsigned long reads = 0u, writes = 0u;
    unsigned long data_writes = 0u, watchdogs = 0u;
    unsigned long control_reads = 0u, control_writes = 0u, address_writes = 0u;
    unsigned long control_store = 0u, data_store = 0u;
    unsigned long i;

    memset(&port, 0, sizeof(port));
    port.bus_read = on_read;
    port.bus_write = on_write;
    port.hstctl = 0u;

    g_cycle_count = 0u;
    g_overflow = 0u;
    memset(g_cycles, 0, sizeof(g_cycles));

    CHECK(stunrun_gsp_zero_fill(ROM_CONTINUATION, &port, &r) == 1);

    /* ── The arithmetic the loop would otherwise have to be trusted for ── */
    CHECK_EQ(r.data_writes, ROM_DATA_WRITES);
    CHECK_EQ(r.data_writes, 230400u);
    CHECK_EQ(r.watchdog_writes, ROM_PASSES);
    CHECK_EQ(r.watchdog_writes, 900u);
    CHECK_EQ(r.writes, ROM_WRITES);
    CHECK_EQ(r.writes, 231303u);
    CHECK_EQ(r.reads, ROM_READS);
    CHECK_EQ(r.control_reads, 1u);
    CHECK_EQ(r.control_writes, 1u);
    CHECK_EQ(r.address_writes, 2u);
    CHECK_EQ(r.continuation, ROM_CONTINUATION);
    CHECK_EQ(r.continuation, 0x00005082u);
    CHECK_EQ(r.start_address, ROM_GSP_BASE);
    CHECK_EQ(r.data_address, ROM_GSP_BASE);
    CHECK_EQ(r.data_value, ROM_SEED);
    CHECK_EQ(r.control_before, 0u);
    CHECK_EQ(r.control_after, ROM_CONTROL_OR);
    /* The census must account for every cycle, with nothing left over. */
    CHECK_EQ(r.live_writes + r.discarded_writes + r.outside_writes,
             (unsigned long)r.writes);

    /* ── The stream itself ── */
    CHECK_EQ(g_overflow, 0u);
    CHECK_EQ(g_cycle_count, (unsigned long)(ROM_READS + ROM_WRITES));
    CHECK_EQ(g_cycle_count, 231304u);

    /* The prologue, cycle for cycle, including the read half of the `ori.w`:
     * the read has to come FIRST or the control word is a plain store and the
     * routine is not what the ROM says. */
    CHECK(expect_cycle(0u, 1u, ROM_CONTROL, 0x0000u));
    CHECK(expect_cycle(1u, 0u, ROM_CONTROL, ROM_CONTROL_OR));
    /* `move.l` on a 16-bit bus: most significant word at the lower address. */
    CHECK(expect_cycle(2u, 0u, ROM_ADDRESS_HIGH, 0xFF80u));
    CHECK(expect_cycle(3u, 0u, ROM_ADDRESS_LOW, 0x0000u));

    /* The first group's head and its boundary: 256 data writes, then the
     * watchdog.  This is what pins the watchdog cadence -- kicked after every
     * 256 words, not once per pass and not once at the end. */
    CHECK(expect_cycle(GROUP_FIRST, 0u, ROM_DATA, ROM_SEED));
    CHECK(expect_cycle(GROUP_FIRST + 1u, 0u, ROM_DATA, ROM_SEED));
    CHECK(expect_cycle(WATCHDOG_K(0u) - 1u, 0u, ROM_DATA, ROM_SEED));
    CHECK(expect_cycle(WATCHDOG_K(0u), 0u, ROM_WATCHDOG, 0x0000u));
    CHECK(expect_cycle(WATCHDOG_K(0u) + 1u, 0u, ROM_DATA, ROM_SEED));
    CHECK(expect_cycle(WATCHDOG_K(1u), 0u, ROM_WATCHDOG, 0x0000u));
    CHECK(expect_cycle(WATCHDOG_K(2u) - 1u, 0u, ROM_DATA, ROM_SEED));
    CHECK(expect_cycle(WATCHDOG_K(2u), 0u, ROM_WATCHDOG, 0x0000u));

    /* The last group, so a loop that stops one iteration early is caught. */
    CHECK(expect_cycle(WATCHDOG_K(ROM_PASSES - 1u) - 1u, 0u, ROM_DATA,
                       ROM_SEED));
    CHECK(expect_cycle(WATCHDOG_K(ROM_PASSES - 1u), 0u, ROM_WATCHDOG, 0x0000u));

    /* Exhaustive: EVERY cycle in the stream, not a sample.  Each watchdog must
     * sit exactly where the loop nesting puts it, with exactly 256 data writes
     * in front of it, and no other address may appear anywhere. */
    for (i = 0u; i < g_cycle_count; i++) {
        const cycle_t *c = &g_cycles[i];

        if (c->is_read) {
            reads++;
            /* The one and only read is the `ori.w` at $548E. */
            if (i != 0u || c->address != ROM_CONTROL) {
                fprintf(stderr,
                        "cycle %lu: unexpected read of $%08lX\n", i,
                        (unsigned long)c->address);
                return 1;
            }
            control_reads++;
            continue;
        }

        writes++;
        switch (c->address) {
        case ROM_CONTROL:
            control_writes++;
            control_store++;
            if (i != 1u) {
                fprintf(stderr,
                        "cycle %lu: HSTCTL store outside the prologue (i=1)\n",
                        i);
                return 1;
            }
            break;
        case ROM_ADDRESS_HIGH:
        case ROM_ADDRESS_LOW:
            address_writes++;
            if (i > 3u) {
                fprintf(stderr, "cycle %lu: late address latch\n", i);
                return 1;
            }
            break;
        case ROM_DATA:
            data_writes++;
            data_store++;
            if (c->value != ROM_SEED) {
                fprintf(stderr,
                        "cycle %lu: HSTDAT = $%04X, expected $%04X\n", i,
                        (unsigned)c->value, (unsigned)ROM_SEED);
                return 1;
            }
            break;
        case ROM_WATCHDOG:
            watchdogs++;
            if (c->value != 0u) {
                fprintf(stderr, "cycle %lu: watchdog value $%04X, expected 0\n",
                        i, (unsigned)c->value);
                return 1;
            }
            /* THE CADENCE.  900 watchdogs, each after exactly 256 data
             * writes, each at a distinct stride-257 boundary. */
            if (i != (unsigned long)WATCHDOG_K(watchdogs - 1u)) {
                fprintf(stderr,
                        "cycle %lu: watchdog #%lu out of position "
                        "(expected cycle %lu)\n",
                        i, watchdogs, (unsigned long)WATCHDOG_K(watchdogs - 1u));
                return 1;
            }
            break;
        default:
            fprintf(stderr, "cycle %lu: unexpected write to $%08lX\n", i,
                    (unsigned long)c->address);
            return 1;
        }
    }

    CHECK_EQ(reads, ROM_READS);
    CHECK_EQ(writes, ROM_WRITES);
    CHECK_EQ(control_reads, 1u);
    CHECK_EQ(control_writes, 1u);
    CHECK_EQ(control_store, 1u);
    CHECK_EQ(address_writes, 2u);
    CHECK_EQ(data_writes, ROM_DATA_WRITES);
    CHECK_EQ(data_store, ROM_DATA_WRITES);
    CHECK_EQ(watchdogs, ROM_PASSES);
    CHECK_EQ(watchdogs, 900u);

    /* ── THE NO-OP ──
     *
     * Re-derived here, from the recorded stream, through the slice's published
     * decode predicate -- not read back out of `r`, which would only prove the
     * slice agrees with itself. */
    for (i = 0u; i < g_cycle_count; i++) {
        int decode;

        if (g_cycles[i].is_read)
            continue;
        decode = (int)stunrun_gsp_zero_fill_classify(g_cycles[i].address);
        if (decode == (int)STUNRUN_HOSTPORT_OUTSIDE)
            outside++;
        else if (decode == (int)STUNRUN_HOSTPORT_DISCARDED)
            discarded++;
        else {
            live++;
            if (decode == (int)STUNRUN_HOSTPORT_HSTDAT)
                live_data++;
        }
    }

    /* Two cycles reach the GSP: the address pair at $C00002/$C00004.  Not
     * zero, and asserting zero here would be asserting something the pinned
     * decode refutes. */
    CHECK_EQ(live, 2u);
    /* Every one of the 230,400 data writes plus the one control store is
     * dropped. */
    CHECK_EQ(discarded, (unsigned long)(ROM_DATA_WRITES + 1u));
    CHECK_EQ(discarded, 230401u);
    /* The watchdog is outside the window; that is not the same as discarded. */
    CHECK_EQ(outside, ROM_PASSES);
    CHECK_EQ(outside, 900u);
    /* THE CLAIM.  Nothing reached HSTDAT, so nothing reached GSP memory, so
     * nothing in the 4 MiB share can have been altered by this routine. */
    CHECK_EQ(live_data, 0u);
    CHECK_EQ(r.live_data_writes, 0u);

    /* And the slice's own census agrees with the independent recount. */
    CHECK_EQ(r.live_writes, live);
    CHECK_EQ(r.discarded_writes, discarded);
    CHECK_EQ(r.outside_writes, outside);
    CHECK_EQ(r.live_data_writes, live_data);

    return 0;
}

/* ── The continuation is passed through, not hard-coded ──────────────── */

static int test_continuation_passthrough(void)
{
    stunrun_gsp_zero_fill_port_t port;
    stunrun_gsp_zero_fill_result_t r;

    memset(&port, 0, sizeof(port));
    g_cycle_count = 0u;
    g_overflow = 0u;

    /* A continuation the ROM never uses, so a slice that ignored the argument
     * and returned the boot chain's $5082 would be caught. */
    CHECK(stunrun_gsp_zero_fill(0x00012345u, &port, &r) == 1);
    CHECK_EQ(r.continuation, 0x00012345u);

    CHECK(stunrun_gsp_zero_fill(0x00000000u, &port, &r) == 1);
    CHECK_EQ(r.continuation, 0x00000000u);

    /* No observer attached: the routine must still run and still count.  A
     * seam that only works when wired up would hide the no-op. */
    memset(&port, 0, sizeof(port));
    CHECK(stunrun_gsp_zero_fill(ROM_CONTINUATION, &port, &r) == 1);
    CHECK_EQ(r.data_writes, ROM_DATA_WRITES);
    CHECK_EQ(r.live_data_writes, 0u);

    /* Unusable arguments are refused rather than reported as a clean run. */
    CHECK(stunrun_gsp_zero_fill(ROM_CONTINUATION, NULL, &r) == 0);
    CHECK(stunrun_gsp_zero_fill(ROM_CONTINUATION, &port, NULL) == 0);

    return 0;
}

/* ── The `ori.w` really is a read-modify-write ───────────────────────── */

static int test_control_read_modify_write(void)
{
    stunrun_gsp_zero_fill_port_t port;
    stunrun_gsp_zero_fill_result_t r;

    memset(&port, 0, sizeof(port));
    port.bus_read = on_read;
    port.bus_write = on_write;
    /* An ARBITRARY caller preset, chosen only so that the OR is observable:
     * 0xA000 has 0x0800 clear, so a preset that low leaves INCW unset even
     * after the OR, which is the interesting case.  This is NOT a measured
     * MAME value and nothing here claims it is -- what the ROM leaves in the
     * live HSTCTL when the boot path runs is still unresolved (findings
     * section 31.4), precisely because the ROM's own HSTCTL store at $C00008
     * is discarded. */
    port.hstctl = 0xA000u;

    g_cycle_count = 0u;
    g_overflow = 0u;
    memset(g_cycles, 0, sizeof(g_cycles));

    CHECK(stunrun_gsp_zero_fill(ROM_CONTINUATION, &port, &r) == 1);

    /* Read first, then the OR-ed store.  A slice that modelled `ori.w` as a
     * plain store would put 0x1800 in cycle 0 and emit no read at all, and
     * `expect_cycle(0u, 1u, ...)` is what catches that. */
    CHECK(expect_cycle(0u, 1u, ROM_CONTROL, 0xA000u));
    CHECK(expect_cycle(1u, 0u, ROM_CONTROL, 0xB800u));
    CHECK_EQ(r.control_before, 0xA000u);
    CHECK_EQ(r.control_after, 0xB800u);
    CHECK_EQ(port.hstctl, 0xB800u);

    /* Still a no-op on VRAM, whatever HSTCTL held. */
    CHECK_EQ(r.live_data_writes, 0u);
    CHECK_EQ(r.discarded_writes, (unsigned long)(ROM_DATA_WRITES + 1u));

    return 0;
}

int main(void)
{
    if (test_constants() != 0)
        return 1;
    if (test_decode() != 0)
        return 1;
    if (test_routine() != 0)
        return 1;
    if (test_continuation_passthrough() != 0)
        return 1;
    if (test_control_read_modify_write() != 0)
        return 1;
    puts("gsp zero-fill slice: all checks passed");
    return 0;
}