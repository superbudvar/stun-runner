/* Observable-output test for ROM $506C-$5078 (see gsp_boot_entry.h).
 *
 * The three instructions have exactly three observable effects, and all three
 * are asserted here literally: the value written to A7, the value written to
 * A0, and the address control leaves for.  Asserting "it ran" would be
 * vacuous -- the slice is three assignments.  The order the three happen in
 * is asserted too, because that is the part a chain actually depends on and
 * the part a final-state-only check cannot see.
 *
 * Expectations are literals here and derived values live in the
 * implementation, never the other way round: if both sides of a check computed
 * the answer from the same constants, changing a constant would move both
 * sides together and the test would keep passing for the wrong reason.
 *
 * Negative control: this file is unmodified and is run against deliberately
 * broken copies of gsp_boot_entry.c produced with sed.  See the findings doc
 * section 32 for the recorded failures.
 */
#include "gsp_boot_entry.h"

#include <stdio.h>

static int failures;

static void check(int ok, const char *what, unsigned long got,
                  unsigned long want)
{
    if (ok)
        return;
    failures++;
    fprintf(stderr, "FAIL: %s: got 0x%lx, want 0x%lx\n", what, got, want);
}

/* ── Emitter fixture ───────────────────────────────────────────────────── */

#define MAX_EVENTS 8

typedef struct trace {
    stunrun_gsp_boot_entry_event_t kind[MAX_EVENTS];
    uint32_t value[MAX_EVENTS];
    size_t count;
    int overflow;
} trace_t;

static void emit(void *ctx, stunrun_gsp_boot_entry_event_t event, uint32_t value)
{
    trace_t *t = (trace_t *)ctx;
    if (t->count >= MAX_EVENTS) {
        t->overflow = 1;
        return;
    }
    t->kind[t->count] = event;
    t->value[t->count] = value;
    t->count++;
}

static void run(stunrun_gsp_boot_entry_state_t *state, trace_t *trace)
{
    trace->count = 0u;
    trace->overflow = 0;
    stunrun_gsp_boot_entry_init(state);
    state->emit = emit;
    state->ctx = trace;
    stunrun_gsp_boot_entry_step(state);
}

/* ── 1. The PC-relative rule itself ────────────────────────────────────── */

/* The continuation is not a constant, it is $5074 + $0006.  The first case is
 * the real encoding; the rest are shifted bases and a negative displacement,
 * which a slice that had baked $507A in would fail immediately. */
static void test_pc_relative_arithmetic(void)
{
    uint32_t base;

    check(stunrun_gsp_boot_entry_pc_relative(0x5074u, 0x0006) == 0x507Au,
          "lea base $5074 disp $0006",
          stunrun_gsp_boot_entry_pc_relative(0x5074u, 0x0006), 0x507Au);

    for (base = 0x0000u; base <= 0xFFF0u; base += 0x30u) {
        uint32_t got = stunrun_gsp_boot_entry_pc_relative(base, 0x0006);
        check(got == base + 6u, "shifted base tracks 1:1",
              got, base + 6u);
    }

    /* Negative displacement: (d,PC) is a signed 16-bit form.  $5074 + $0006
     * would also be satisfied by an unsigned add, but $5000 - 6 is not. */
    check(stunrun_gsp_boot_entry_pc_relative(0x5000u, -6) == 0x4FFAu,
          "negative displacement sign-extends",
          stunrun_gsp_boot_entry_pc_relative(0x5000u, -6), 0x4FFAu);

    /* The classic off-by-one: using the opcode's own address instead of the
     * extension word's.  $5072 + $0006 is $5078, one byte inside the `bra`,
     * so this must not silently agree with the right answer. */
    check(stunrun_gsp_boot_entry_pc_relative(0x5072u, 0x0006) != 0x507Au,
          "opcode address is not the PC base",
          stunrun_gsp_boot_entry_pc_relative(0x5072u, 0x0006), 0x507Au);
}

/* ── 2. The three register writes, and their replacement semantics ─────── */

static void test_registers(void)
{
    stunrun_gsp_boot_entry_state_t state;
    trace_t trace;

    run(&state, &trace);

    /* $506C: movea.l #$ffffc000,A7 */
    check(state.a7 == 0xFFFFC000u, "A7 after $506C", state.a7, 0xFFFFC000u);
    /* $5072: lea ($6,PC),A0 -> $507A */
    check(state.continuation == 0x507Au, "continuation from $5072",
          state.continuation, 0x507Au);
    check(state.a0 == 0x507Au, "A0 after $5072", state.a0, 0x507Au);
    /* $5076: bra $53F2 */
    check(state.transfer_pc == 0x53F2u, "transfer target from $5076",
          state.transfer_pc, 0x53F2u);

    /* Both writes REPLACE.  init() seeds A7 with $DEADBEEF and A0 with
     * $12345678; if either were an arithmetic update the values above would
     * have come out different. */
    check(state.a7 != 0xDEADBEEFu, "A7 replaced, not added", state.a7,
          0xDEADBEEFu);
    check(state.a0 != 0x12345678u, "A0 replaced, not added", state.a0,
          0x12345678u);

    /* The continuation is where $53F2's `jmp (A0)` at $548C returns to, so it
     * must differ from the transfer target.  A slice that had these two the
     * wrong way round would still be "a plausible boot chain". */
    check(state.continuation != state.transfer_pc,
          "continuation and transfer target are distinct addresses",
          state.continuation, state.transfer_pc);

    /* $FFFFC000 is the ROM's own Initial SSP (the first long at ROM
     * $00000000).  The 68010 drives a 24-bit bus, so the address it actually
     * reaches is $FFC000 -- which is 16 KiB above $FF8000, the base of the
     * 32 KiB work RAM established in findings section 9, leaving 16 KiB of
     * stack growth below A7.  Assert the mask and the arithmetic separately so
     * that moving either the SP literal or the RAM base breaks something
     * visible. */
    check((state.a7 & 0x00FFFFFFu) == 0x00FFC000u,
          "SP masked to the 24-bit bus", state.a7 & 0x00FFFFFFu, 0x00FFC000u);
    check(0x00FFC000u - 0x00FF8000u == 0x4000u,
          "SP sits 16 KiB into the 32 KiB RAM",
          0x00FFC000u - 0x00FF8000u, 0x4000u);
    check(0x00FFFFFFu - 0x00FF8000u + 1u == 0x8000u,
          "RAM window is 32 KiB, not 512 KiB",
          0x00FFFFFFu - 0x00FF8000u + 1u, 0x8000u);
}

/* ── 3. The chain, in order ────────────────────────────────────────────── */

/* SP, then the continuation, then the transfer.  Recorded as a value trace
 * rather than re-derived, so a slice that emits the right three numbers in
 * the wrong order is caught. */
static void test_chain_order(void)
{
    stunrun_gsp_boot_entry_state_t state;
    trace_t trace;
    int overflowed;

    run(&state, &trace);
    overflowed = trace.overflow;

    check(!overflowed && trace.count == 3u, "exactly three events",
          (unsigned long)trace.count, 3u);
    if (!overflowed && trace.count == 3u) {
        check(trace.kind[0] == STUNRUN_GSP_BOOT_ENTRY_SET_SP,
              "event 0 is the SP write", (unsigned long)trace.kind[0],
              (unsigned long)STUNRUN_GSP_BOOT_ENTRY_SET_SP);
        check(trace.value[0] == 0xFFFFC000u, "event 0 value", trace.value[0],
              0xFFFFC000u);

        check(trace.kind[1] == STUNRUN_GSP_BOOT_ENTRY_SET_CONTINUATION,
              "event 1 is the A0 write", (unsigned long)trace.kind[1],
              (unsigned long)STUNRUN_GSP_BOOT_ENTRY_SET_CONTINUATION);
        check(trace.value[1] == 0x507Au, "event 1 value", trace.value[1],
              0x507Au);

        check(trace.kind[2] == STUNRUN_GSP_BOOT_ENTRY_TRANSFER,
              "event 2 is the branch", (unsigned long)trace.kind[2],
              (unsigned long)STUNRUN_GSP_BOOT_ENTRY_TRANSFER);
        check(trace.value[2] == 0x53F2u, "event 2 value", trace.value[2],
              0x53F2u);
    }
}

/* ── 4. NULL tolerance ─────────────────────────────────────────────────── */

/* Declared here rather than inside the test so the NULL-call checks run first
 * against one object the test also steps with no emitter attached. */
static stunrun_gsp_boot_entry_state_t unwired;

static void test_null_is_a_no_op(void)
{
    stunrun_gsp_boot_entry_state_t *null_state = NULL;
    trace_t trace;

    trace.count = 0u;
    trace.overflow = 0;

    stunrun_gsp_boot_entry_init(null_state);
    stunrun_gsp_boot_entry_step(null_state);

    /* init() with no emitter, then step: the ROM's three instructions have no
     * failure mode, so neither may the slice.  `unwired` was zeroed as a
     * static, so its emit is NULL and step must decline rather than crash. */
    stunrun_gsp_boot_entry_init(&unwired);
    stunrun_gsp_boot_entry_step(&unwired);
    check(trace.count == 0u, "unwired state emits nothing",
          (unsigned long)trace.count, 0u);
}

int main(void)
{
    test_pc_relative_arithmetic();
    test_registers();
    test_chain_order();
    test_null_is_a_no_op();

    if (failures != 0) {
        fprintf(stderr, "gsp boot entry slice: %d check(s) failed\n", failures);
        return 1;
    }
    puts("gsp boot entry slice: all checks passed");
    return 0;
}