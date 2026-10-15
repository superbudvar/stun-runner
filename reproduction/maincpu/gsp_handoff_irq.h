/* GSP handoff interrupt handler, ROM $0002143C (MC68010 level-3 autovector).
 *
 * Ported to C deliberately: this is the routine that releases the 68010's
 * display-list waiter AND the GSP itself, so owning it in C makes its effects
 * observable and adjustable without editing ROM. The default shell still runs
 * the original 68k path; see native/gsp_handoff_irq.c and the shell's
 * STUNRUN_GSP_HANDOFF_C opt-in.
 *
 * See docs/mc68010-boot-findings.md section 45 for the transcription, and
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
#define STUNRUN_GSP_HANDOFF_HSTCTL_HLT      0x8000u

/* One invocation's machine state. */
typedef struct stunrun_gsp_handoff_state {
    /* IN: GSP host HSTCTL before the ANDI. Bit 15 is HLT (held set while the
     * 68010 is uploading); bit 11 is the HSTDAT auto-increment. */
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
    uint16_t hstctl_after;       /* HSTCTL after the ANDI */
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