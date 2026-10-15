/* GSP handoff interrupt handler, ROM $0002143C (MC68010 level-3 autovector).
 *
 * Ported to C deliberately: this is the routine that acknowledges the GSP's
 * interrupt request and flags the 68010's display-list waiter, so owning it in
 * C makes its effects observable and adjustable without editing ROM. The
 * default shell still runs the original 68k path; see the shell's
 * STUNRUN_GSP_HANDOFF_C opt-in.
 *
 * See docs/mc68010-boot-findings.md section 48 for the transcription, and
 * section 40.1 for why this address is the only writer of the wait flag.
 */
#ifndef STUNRUN_GSP_HANDOFF_IRQ_H
#define STUNRUN_GSP_HANDOFF_IRQ_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* $FFDB48 after the handler: $FFFF, not merely non-zero. The waiter at $02FC82
 * tests non-zero, but the value matters to anything reading the flag back. */
#define STUNRUN_GSP_HANDOFF_WAIT_FLAG 0xFFFFu

/* Board addresses the ROM touches. Exposed so a test can assert the same
 * constants the disassembly uses rather than re-typing them. */
#define STUNRUN_GSP_HANDOFF_ADDR_WAIT_FLAG   0x00FFDB48u
#define STUNRUN_GSP_HANDOFF_ADDR_TICK_BASE  0x00801400u
#define STUNRUN_GSP_HANDOFF_ADDR_TICK_SAMP  0x00FFDB36u
#define STUNRUN_GSP_HANDOFF_ADDR_ELAPSED    0x00FF9BAAu
#define STUNRUN_GSP_HANDOFF_ADDR_HSTCTL     0x00C00008u

/* The ROM's ANDI mask, verbatim from `02 79 ff 7f` at $021446.
 *
 * $FF7F is 1111_1111_0111_1111: it clears EXACTLY ONE BIT, bit 7, and
 * preserves all fifteen others -- including bit 15 (HLT) and bit 11 (the
 * HSTDAT auto-increment).
 *
 * This matters and was got wrong twice: sections 45 and 47.7 of the findings
 * both claimed this instruction "preserves bits 11:0" (bit 7 is inside 11:0,
 * so it does not) and that it "releases the GSP" by clearing HLT. It does
 * neither. Bit 7 is HSTCTLL.INTOUT, the GSP-to-host interrupt request, so the
 * instruction ACKNOWLEDGES the interrupt that just fired: the host arm of
 * MAME's HSTCTLL write is clear-only (`newreg &= data | ~0x0080`,
 * tms34010.cpp:1676), so writing bit 7 clear drops INTOUT and therefore drops
 * the GSP's INT line. See findings 48.2 for why "releases the GSP" is void. */
#define STUNRUN_GSP_HANDOFF_HSTCTL_MASK     0xFF7Fu
#define STUNRUN_GSP_HANDOFF_HSTCTL_INTOUT   0x0080u  /* HSTCTLL bit 7: the ack */
#define STUNRUN_GSP_HANDOFF_HSTCTL_HLT      0x8000u  /* PRESERVED by the mask */

/* One invocation's machine state. */
typedef struct stunrun_gsp_handoff_state {
    /* IN: GSP host HSTCTL before the ANDI. Bit 7 is INTOUT, the request being
     * acknowledged; bit 11 is the HSTDAT auto-increment; bit 15 is HLT, which
     * the mask preserves whether it is set or clear. */
    uint16_t hstctl;

    /* IN: 68010 tick counter ($8014.w, the word at offset 0x14 of the $8000
     * page) and the previous sample ($FFDB36). Both are free-running, so the
     * subtraction must wrap rather than saturate. */
    uint32_t tick_counter;
    uint32_t tick_last_sample;

    /* IN/OUT: D0 as the handler leaves it. The ROM saves and restores it, so
     * this is deliberately unchanged by the handler. */
    uint32_t interrupted_d0;

    /* OUT: set by the handler. */
    uint16_t wait_flag;          /* $FFDB48 */
    uint16_t hstctl_after;       /* HSTCTL after ANDI.W #$FF7F */
    uint32_t elapsed_ticks;      /* SUB.L result */
    uint32_t elapsed_ticks_store;/* $FF9BAA */
} stunrun_gsp_handoff_state_t;

/* Execute one GSP handoff interrupt. Field names map 1:1 to the instructions at
 * $02143C-$021460; see the .c file for the verbatim listing. */
void stunrun_gsp_handoff_irq(stunrun_gsp_handoff_state_t *state);

#ifdef __cplusplus
}
#endif

#endif /* STUNRUN_GSP_HANDOFF_IRQ_H */