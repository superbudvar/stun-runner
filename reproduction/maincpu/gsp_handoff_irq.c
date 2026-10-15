/* GSP handoff interrupt handler, ROM $0002143C-$00021460 (MC68010 level-3
 * autovector). Transcribed verbatim from the ROM; this is the one routine the
 * project deliberately owns in C so its effects are observable and can be
 * adjusted experimentally without editing the ROM.
 *
 * What the ROM does, in order:
 *
 *   02143c: move.w  #$ffff,$ffdb48.l   flag the 68010's display-list waiter
 *   021444: move.l  d0,-(a7)          save D0 (the interrupted D0)
 *   021446: andi.w  #$ff7f,$c00008.l  clear HSTCTL bit 15 = RELEASE the GSP
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
 * $8014 is the 68010 tick counter at offset 0x14 of the $8000 page
 * ($008014); $FFDB36 and $FF9BAA are work RAM; $00C00008 is the GSP host
 * HSTCTL. See docs/mc68010-boot-findings.md section 45. Do not add behaviour
 * here without updating that section.
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

    /* $021446: ANDI.W #$FF7F on HSTCTL. Bit 15 is HLT, so this releases the GSP
     * in the same breath as it releases the waiter. Bits 11:0 survive, exactly
     * as ANDI does -- notably 0x0800, the HSTDAT auto-increment. */
    s->hstctl_after =
        (uint16_t)(s->hstctl & (uint16_t)~STUNRUN_GSP_HANDOFF_HSTCTL_HLT);

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