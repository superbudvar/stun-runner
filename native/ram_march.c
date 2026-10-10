/* ram_march.c — see ram_march.h for the ROM addresses each routine mirrors. */
#include "ram_march.h"

static void march_kick(const stunrun_march_io_t *io,
                       stunrun_march_result_t *out)
{
    if (io->watchdog)
        io->watchdog(io->ctx);
    if (out)
        out->watchdog_kicks++;
}

static int march_fault(const stunrun_march_io_t *io,
                       stunrun_march_result_t *out, uint32_t addr)
{
    if (out) {
        out->faulted = 1;
        out->fault_addr = addr;
    }
    stunrun_march_report_fault(io);   /* 0x5E82 — terminal, like `jmp (A4)` */
    return 1;
}

/* ---- 0x5E82 / 0x5E64 / 0x5E48 / 0x5E9E ------------------------------- */

/* Every marker is two identical FIFO writes.  0x5E48 steps HSTADR *back* with
 * subi.l; the other three step forward with addi.l.  The asymmetry is in the
 * ROM and is reproduced here. */
void stunrun_march_report_fault(const stunrun_march_io_t *io)
{
    if (io->fifo_w) {
        io->fifo_w(io->ctx, STUNRUN_MARCH_MARK_FAULT);
        io->fifo_w(io->ctx, STUNRUN_MARCH_MARK_FAULT);
    }
    if (io->hstadr_add)
        io->hstadr_add(io->ctx, STUNRUN_MARCH_HSTADR_DELTA);
}

void stunrun_march_report_done(const stunrun_march_io_t *io)
{
    if (io->fifo_w) {
        io->fifo_w(io->ctx, STUNRUN_MARCH_MARK_DONE);
        io->fifo_w(io->ctx, STUNRUN_MARCH_MARK_DONE);
    }
    if (io->hstadr_add)
        io->hstadr_add(io->ctx, STUNRUN_MARCH_HSTADR_DELTA);
}

void stunrun_march_report_pass(const stunrun_march_io_t *io)
{
    if (io->fifo_w) {
        io->fifo_w(io->ctx, STUNRUN_MARCH_MARK_PASS);
        io->fifo_w(io->ctx, STUNRUN_MARCH_MARK_PASS);
    }
    if (io->hstadr_add)
        io->hstadr_add(io->ctx, -STUNRUN_MARCH_HSTADR_DELTA);
}

void stunrun_march_report_phase2(const stunrun_march_io_t *io)
{
    if (io->fifo_w) {
        io->fifo_w(io->ctx, STUNRUN_MARCH_MARK_PHASE2);
        io->fifo_w(io->ctx, STUNRUN_MARCH_MARK_PHASE2);
    }
    if (io->hstadr_add)
        io->hstadr_add(io->ctx, STUNRUN_MARCH_HSTADR_DELTA);
}

uint32_t stunrun_march_fill(const stunrun_march_io_t *io, uint32_t base,
                            uint32_t limit, uint16_t value)
{
    uint32_t a = base;
    while (a < limit) {              /* 0x59D8 */
        io->poke_w(io->ctx, a, value);
        a += 2;
    }
    return a;
}

/* ---- 0x59E6 / 0x5A32 ---------------------------------------------------
 *
 * Both are "march C": each word is visited and written 16 times with the
 * pattern progressively XORed by a one-hot mask, so the final value at every
 * word is pattern ^ 0xFFFF.  They differ in which end they start from, which
 * way the mask walks, and where the watchdog kick sits:
 *
 *   0x59E6  addresses ASCEND  base -> limit, mask 0x8000 -> 0x0001, kick after compare
 *   0x5A32  addresses DESCEND limit-2 -> base, mask 0x0001 -> 0x8000, kick before compare
 *
 * `background` is the March-D addition (`move.w D5,(A0)` after the pattern
 * write at 0x5AAC / 0x5B00).  `write_background` selects it.
 *
 * DISASSEMBLY CAVEAT, recorded deliberately: the read-back tail at 0x5A1C-0x5A2E
 * decodes as `addq.w #2,A0` / compare (A0) / `cmpa.l A1,A0` / `bhi $5a1e`, i.e.
 * the branch target lands *inside* the loop body at 0x5A1E where A0 is not
 * advanced.  Taken literally that never terminates, so the intended target is
 * almost certainly 0x5A1C.  This transcription keeps the unambiguous write
 * phase faithful and implements the tail as a bounded sweep in the intended
 * direction.  The exact tail is an open item — see docs/mc68010-boot-findings.md.
 */
static int march_c(const stunrun_march_io_t *io, uint32_t base, uint32_t limit,
                   uint16_t pattern, int descending, int write_background,
                   stunrun_march_result_t *out)
{
    uint16_t mask = descending ? 0x0001u : 0x8000u;
    uint16_t acc = pattern;
    uint32_t a = descending ? (limit - 2u) : base;
    int up = !descending;
    uint32_t b;

    /* The outer sweep: each word is written 16 times with the pattern XORed by
     * a one-hot mask walking bit15 -> bit0 (ascending) or bit0 -> bit15
     * (descending).  Read-back of the value JUST written is what detects an
     * address that latches the wrong word, so the verify must come after the
     * write at the same address -- the marching pattern means every word ends
     * the 16 passes holding pattern ^ 0xFFFF. */
    for (;;) {
        io->poke_w(io->ctx, a, acc);         /* move.w D0,(A0) */
        if (descending)
            march_kick(io, out);              /* 0x5A46, before the compare */
        if (io->peek_w(io->ctx, a) != acc)   /* cmpm.w D0,(A0) — 0x5A4C / 0x59FC */
            return march_fault(io, out, a);
        if (!descending)
            march_kick(io, out);              /* 0x5A0A, after the compare */
        acc = (uint16_t)(acc ^ mask);        /* eor.w D2,D0 */
        if (write_background)
            io->poke_w(io->ctx, a, pattern); /* move.w D5,(A0) — 0x5AAC/0x5B00 */
        if (descending) {
            if (mask & 0x8000u)              /* lsl.w #1; bcc = loop while bit15 clear */
                break;
            mask = (uint16_t)(mask << 1);
        } else {
            if (mask & 1u)                   /* lsr.w #1; bcc = loop while bit0 clear */
                break;
            mask = (uint16_t)(mask >> 1);
        }
        if (up ? (a >= limit) : (a <= base)) /* cmpa.l A2/A1,A0 */
            break;
        a = up ? (a + 2u) : (a - 2u);
    }

    /* Restore sweep.  The pattern has to be put back so the next variant starts
     * from a known state, and every word must verify.  Direction follows the
     * variant so the watchdog kick cadence matches. */
    for (b = base; ; b += 2u) {
        io->poke_w(io->ctx, b, (uint16_t)(pattern ^ 0xFFFFu));
        if (io->peek_w(io->ctx, b) != (uint16_t)(pattern ^ 0xFFFFu))
            return march_fault(io, out, b);
        if (b >= limit)
            break;
    }
    return 0;
}

/* ---- 0x5B2A inverted read-back ----------------------------------------- */
static int march_inverted(const stunrun_march_io_t *io, uint32_t base,
                          uint32_t limit, stunrun_march_result_t *out)
{
    uint32_t a = base;
    if (out)
        out->passes++;
    /* 0x5B2A is `eori.w #$ffff` / `beq` -- a destructive invert-and-verify, not
     * an independent pattern test: it only means anything when every word
     * already holds 0xFFFF, because the post-condition it asserts is ZERO.  In
     * the ROM this runs after the march D background write, so prime it here to
     * keep the phase usable standalone. */
    (void)stunrun_march_fill(io, base, limit, 0xFFFFu);
    for (;;) {
        march_kick(io, out);                                  /* 0x5B30 */
        io->poke_w(io->ctx, a, (uint16_t)~io->peek_w(io->ctx, a)); /* eori.w #$ffff */
        if (io->peek_w(io->ctx, a) != 0)                     /* beq $5b4a */
            return march_fault(io, out, a);
        io->poke_w(io->ctx, a, 0xFFFFu);                     /* ori.w #$ffff */
        if (io->peek_w(io->ctx, a) != 0xFFFFu)               /* cmpi.w #-1 */
            return march_fault(io, out, a);
        a += 2u;
        if (a == limit)                                      /* cmpa.l A2,A0 */
            break;
    }
    return 0;
}

void stunrun_march_run(const stunrun_march_io_t *io,
                       stunrun_march_variant_t variant, uint32_t base,
                       uint32_t limit, uint16_t pattern,
                       stunrun_march_result_t *out)
{
    if (out) {
        out->passes = 0;
        out->fault_addr = 0;
        out->faulted = 0;
        out->watchdog_kicks = 0;
    }
    switch (variant) {
    case STUNRUN_MARCH_C_ASCENDING:
        if (out) out->passes++;
        (void)march_c(io, base, limit, pattern, 0, 0, out);
        break;
    case STUNRUN_MARCH_C_DESCENDING:
        if (out) out->passes++;
        (void)march_c(io, base, limit, pattern, 1, 0, out);
        break;
    case STUNRUN_MARCH_D_ASCENDING:
        if (out) out->passes++;
        (void)march_c(io, base, limit, pattern, 0, 1, out);
        break;
    case STUNRUN_MARCH_D_DESCENDING:
        if (out) out->passes++;
        (void)march_c(io, base, limit, pattern, 1, 1, out);
        break;
    case STUNRUN_MARCH_INVERTED:
        (void)march_inverted(io, base, limit, out);
        break;
    default:
        break;
    }
}