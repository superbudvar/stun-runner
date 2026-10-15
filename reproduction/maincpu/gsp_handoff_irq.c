/* GSP handoff interrupt handler, ROM $0002143C-$00021460 (MC68010 level-3
 * autovector). Transcribed verbatim from the ROM; this is the one routine the
 * project deliberately owns in C so its effects are observable and can be
 * adjusted experimentally without editing the ROM.
 *
 * What the ROM does, in order:
 *
 *   02143c: move.w  #$ffff,$ffdb48.l   flag the 68010's display-list waiter
 *   021444: move.l  d0,-(a7)          save D0 (the interrupted D0)
 *   021446: andi.w  #$ff7f,$c00008.l  clear HSTCTL bit 7 = ack the GSP's INT
 *   02144e: move.l  $8014.w,d0        D0 = 68010 free-running tick counter
 *   021452: sub.l   $ffdb36.l,d0      D0 -= last sampled tick
 *   021458: move.l  d0,$ff9baa.l      record elapsed ticks
 *   02145e: move.l  (a7)+,d0         restore D0
 *   021460: rte
 *
 * The `move.l d0,-(a7)` / `move.l (a7)+,d0` pair only preserves D0 across the
 * middle; it does not restore A7 to a pre-exception value, because the exception
 * frame already pushed D0 when the interrupt was taken. Modelled here as an
 * explicit save/restore of the interrupted D0, which is what it is.
 *
 * THE ANDI MASK IS NOT WHAT EARLIER SECTIONS SAID. $FF7F clears exactly one
 * bit: bit 7. Findings 45 and 47.7 each claimed it "preserves bits 11:0" and
 * "releases the GSP by clearing HLT"; both are wrong, and both were refuted
 * from the ROM bytes. Bit 7 is HSTCTLL.INTOUT -- the GSP-to-host interrupt
 * request -- so the instruction ACKNOWLEDGES the interrupt that just delivered
 * this handler. Bit 15 (HLT) is preserved by the mask and was already clear
 * when the handler ran (findings 48.2). Do not reintroduce either claim.
 */
#include <stddef.h>

#include "gsp_handoff_irq.h"

void stunrun_gsp_handoff_irq(stunrun_gsp_handoff_state_t *s)
{
    uint32_t ticks;

    if (s == NULL)
        return;

    /* $02143c: the wait flag. This is the only writer of $FFDB48 anywhere in
     * the program (findings section 40.1), which is why the 68010's $02FC82
     * wait can only be released by this interrupt. */
    s->wait_flag = (uint16_t)STUNRUN_GSP_HANDOFF_WAIT_FLAG;

    /* $021446: ANDI.W #$FF7F on HSTCTL. One bit cleared: bit 7, INTOUT. The
     * host arm of MAME's HSTCTLL write is clear-only (`newreg &= data |
     * ~0x0080`, tms34010.cpp:1676), so this drops the GSP's INT line and ends
     * the handshake that delivered the interrupt. Bits 15:8 -- HLT among them
     * -- and bit 11 (the HSTDAT auto-increment) all survive exactly as the
     * mask says, because the mask is not `~0x0080` and not `~0x8000`. */
    s->hstctl_after = (uint16_t)(s->hstctl & STUNRUN_GSP_HANDOFF_HSTCTL_MASK);

    /* $02144e-$021452: elapsed 68010 ticks since the last sample. Plain 32-bit
     * subtraction, as SUB.L does: a wrapped free-running counter yields a large
     * positive delta rather than a negative one, so do not clamp or "fix" it. */
    ticks = s->tick_counter - s->tick_last_sample;
    s->elapsed_ticks = ticks;

    /* $021458: publish the delta to $FF9BAA. $02145e restores D0 from the
     * stack push at $021444, so interrupted_d0 is left exactly as it arrived;
     * that is why nothing assigns it here. */
    s->elapsed_ticks_store = ticks;
}