/* ram_march.h — STUN Runner's factory RAM march test, transcribed to C.
 *
 * Source of truth: STUN Runner MC68010 ROM, disassembly taken with
 * STUNRUN_DISASM (see docs/mc68010-boot-findings.md).
 *
 *   0x5B6E  driver: D6 = pass counter, D0 = 0, D4 = fault flag,
 *                   A6 = 0x5B7C (the return trampoline), then `bra $59d8`
 *   0x59D8  fill prologue: store D0 at every word from A1 to A2
 *   0x59E6  march C, addresses ASCEND  (A1 -> A2), mask 0x8000 -> 0x0001
 *   0x5A32  march C, addresses DESCEND (A2-2 -> A1), mask 0x0001 -> 0x8000
 *   0x5A7E  march D, addresses ASCEND  (also re-writes the background pattern)
 *   0x5AD4  march D, addresses DESCEND (also re-writes the background pattern)
 *   0x5B2A  inverted read-back test
 *   0x5E82  fault reporter: FIFO $9F9F twice, HSTADR += 0x20, jmp (A4) FATAL
 *   0x5E64  done reporter:  FIFO $5F5F twice, HSTADR += 0x20, jmp (A6)
 *   0x5E48  pass marker:   FIFO $3F3F twice, HSTADR -= 0x20, jmp (A6)
 *   0x5E9E  phase-2 marker: FIFO $FFFF twice, HSTADR += 0x20, jmp (A6)
 *
 * A1 = 0xFFFF8000 (first RAM word) and A2 = 0xFFFFFFFE (last RAM word), so the
 * test covers the full 0x80000 bytes the 68010 sees at harddriv.cpp:615.  That
 * sizing matters: before it was fixed our RAM aliased every 32 KiB.
 *
 * The kernel signature is (A1, A2, D5 = pattern, D6 = pass counter).  The ROM
 * enters the C/D kernels at 0x59EA as well as 0x59E6 to switch D5 from 0x0000
 * to 0xFFFF, so the caller supplies the pattern.
 *
 * Any single mismatch sets D4 and branches to 0x5E82, which is terminal: the
 * real ROM jumps to A4 and never returns.  This transcription mirrors that by
 * returning immediately with faulted set.
 */
#ifndef STUNRUN_RAM_MARCH_H
#define STUNRUN_RAM_MARCH_H

#include <stdint.h>

/* Palette words the ROM pushes to the GSP FIFO while the test runs.  These are
 * the machine's own pass/fail report; they are what the diagnostics screen
 * displays, so they double as the test oracle. */
#define STUNRUN_MARCH_MARK_PASS   0x3F3Fu /* 0x5E48 — a variant passed */
#define STUNRUN_MARCH_MARK_DONE   0x5F5Fu /* 0x5E64 — every variant passed */
#define STUNRUN_MARCH_MARK_FAULT  0x9F9Fu /* 0x5E82 — a fault was found */
#define STUNRUN_MARCH_MARK_PHASE2 0xFFFFu /* 0x5E9E — second pattern phase */

/* HSTADR nudge the ROM applies alongside each marker. */
#define STUNRUN_MARCH_HSTADR_DELTA 0x20

typedef enum {
    /* 0x59E6 — addresses ascend from A1, mask shifts right 0x8000 -> 0x0001.
     * The watchdog is kicked *after* the compare, at 0x5A0A. */
    STUNRUN_MARCH_C_ASCENDING = 0,
    /* 0x5A32 — addresses descend from A2-2, mask shifts left 0x0001 -> 0x8000.
     * The watchdog is kicked *before* the compare, at 0x5A46. */
    STUNRUN_MARCH_C_DESCENDING,
    /* 0x5A7E — as C_ASCENDING plus `move.w D5,(A0)` after each write. */
    STUNRUN_MARCH_D_ASCENDING,
    /* 0x5AD4 — as C_DESCENDING plus `move.w D5,(A0)` after each write. */
    STUNRUN_MARCH_D_DESCENDING,
    /* 0x5B2A — eori.w #$ffff / verify zero / ori.w #$ffff / verify 0xFFFF. */
    STUNRUN_MARCH_INVERTED
} stunrun_march_variant_t;

#define STUNRUN_MARCH_VARIANT_COUNT 5

/* Bus seam.  The kernels only touch word RAM, the watchdog at 0x608000, and
 * the GSP FIFO at 0xC0000C, so that is all this needs. */
typedef struct stunrun_march_io {
    void *ctx;
    void     (*poke_w)(void *ctx, uint32_t addr, uint16_t value);
    uint16_t (*peek_w)(void *ctx, uint32_t addr);
    void     (*watchdog)(void *ctx);        /* clr.w $608000 */
    void     (*fifo_w)(void *ctx, uint16_t value); /* move.w D0,$c0000c.l */
    void     (*hstadr_add)(void *ctx, int32_t delta); /* subi/addi.l #$20 */
} stunrun_march_io_t;

typedef struct {
    uint32_t passes;     /* D6 */
    uint32_t fault_addr; /* 0 when the run was clean */
    int      faulted;    /* D4 */
    uint32_t watchdog_kicks;
} stunrun_march_result_t;

/* Run one kernel.  base is A1, limit is A2 (the last word address).  `out`
 * may be NULL. */
void stunrun_march_run(const stunrun_march_io_t *io,
                       stunrun_march_variant_t variant,
                       uint32_t base,
                       uint32_t limit,
                       uint16_t pattern,
                       stunrun_march_result_t *out);

/* 0x5E82: report a fault.  Terminal, exactly like `jmp (A4)`. */
void stunrun_march_report_fault(const stunrun_march_io_t *io);

/* 0x5E48 / 0x5E64 / 0x5E9E: the non-terminal reporters. */
void stunrun_march_report_pass(const stunrun_march_io_t *io);
void stunrun_march_report_done(const stunrun_march_io_t *io);
void stunrun_march_report_phase2(const stunrun_march_io_t *io);

/* 0x59D8: fill every word in [base, limit] with `value`.  Returns the next
 * address past the end, matching the ROM's exit A0. */
uint32_t stunrun_march_fill(const stunrun_march_io_t *io, uint32_t base,
                            uint32_t limit, uint16_t value);

#endif /* STUNRUN_RAM_MARCH_H */