/* Observable self-check for the GSP handoff interrupt (ROM $0002143C).
 *
 * Every assertion is an EFFECT the ROM produces, not "it ran":
 *   - the wait flag becomes exactly $FFFF
 *   - HSTCTL loses exactly bit 7 (the INTOUT acknowledge) and keeps all
 *     fifteen other bits, including 0x8000 (HLT) and 0x0800 (auto-increment)
 *   - the elapsed tick delta wraps instead of going negative
 *   - $FF9BAA receives the delta
 *   - the interrupted D0 comes back untouched
 *
 * The HSTCTL cases below are chosen so the assertions can DISTINGUISH the
 * ROM's real mask from the two plausible wrong ones. The earlier version of
 * this test used 0xB800 only, which has bit 7 already clear, so 0xFF7F,
 * ~0x8000 and ~0x0080 all produced the same answer and the test could not tell
 * a faithful transcription from either wrong reading. See findings 48.2.
 */
#include <stdio.h>

#include "gsp_handoff_irq.h"

static int failures;

#define check(cond) do { \
    if (!(cond)) { printf("FAIL %s\n", #cond); failures++; } \
} while (0)

/* Run the handler on `in` and return HSTCTL afterwards. */
static uint16_t hstctl_after(uint16_t in)
{
    stunrun_gsp_handoff_state_t s;
    s.hstctl = in;
    s.tick_counter = 0u;
    s.tick_last_sample = 0u;
    s.interrupted_d0 = 0u;
    s.wait_flag = 0u;
    s.hstctl_after = 0u;
    s.elapsed_ticks = 0u;
    s.elapsed_ticks_store = 0u;
    stunrun_gsp_handoff_irq(&s);
    return s.hstctl_after;
}

int main(void)
{
    stunrun_gsp_handoff_state_t s;

    /* Addresses and mask as disassembled from the ROM bytes. */
    check(STUNRUN_GSP_HANDOFF_ADDR_WAIT_FLAG == 0x00FFDB48u);
    check(STUNRUN_GSP_HANDOFF_ADDR_TICK_SAMP == 0x00FFDB36u);
    check(STUNRUN_GSP_HANDOFF_ADDR_ELAPSED == 0x00FF9BAAu);
    check(STUNRUN_GSP_HANDOFF_ADDR_HSTCTL == 0x00C00008u);
    check(STUNRUN_GSP_HANDOFF_HSTCTL_MASK == 0xFF7Fu);
    check(STUNRUN_GSP_HANDOFF_HSTCTL_INTOUT == 0x0080u);
    check(STUNRUN_GSP_HANDOFF_HSTCTL_HLT == 0x8000u);

    /* The mask clears bit 7 and NOTHING else. Each of these three inputs
     * separates the ROM's mask from a wrong one:
     *   input     ROM ($FF7F)   wrong ~0x8000   wrong ~0x0080
     *   0xB880 -> 0xB800         0x3880          0xB880
     *   0xB800 -> 0xB800         0x3800          0xB800
     *   0x0880 -> 0x0800         0x0880          0x0880
     */
    check(hstctl_after(0xB880u) == 0xB800u);
    check(hstctl_after(0xB800u) == 0xB800u);
    check(hstctl_after(0x0880u) == 0x0800u);

    /* HLT is PRESERVED, not cleared. This is the refutation of the "releases
     * the GSP" reading: $FF7F has bit 15 set, so an input with HLT set comes
     * out with HLT set. */
    check((hstctl_after(0xB880u) & 0x8000u) == 0x8000u);
    check((hstctl_after(0x8000u) & 0x8000u) == 0x8000u);

    /* The auto-increment (bit 11) survives, and the acknowledge is the only
     * bit that moves. XOR against the input names exactly the bits that
     * changed, so this cannot pass for a mask that moves a second one. */
    check((hstctl_after(0xB880u) & 0x0800u) != 0u);
    check((uint16_t)(hstctl_after(0xB880u) ^ 0xB880u) == 0x0080u);

    /* Normal case: the live-observed value at the handshake. */
    s.hstctl = 0x3800u;
    s.tick_counter = 1000u;
    s.tick_last_sample = 400u;
    s.interrupted_d0 = 0xDEADBEEFu;
    s.wait_flag = 0u;
    s.hstctl_after = 0u;
    s.elapsed_ticks = 0u;
    s.elapsed_ticks_store = 0u;
    stunrun_gsp_handoff_irq(&s);
    check(s.wait_flag == 0xFFFFu);
    check(s.hstctl_after == 0x3800u);
    check(s.elapsed_ticks == 600u);
    check(s.elapsed_ticks_store == 600u);
    check(s.interrupted_d0 == 0xDEADBEEFu);  /* saved and restored */

    /* A bare INTOUT request is fully acknowledged. */
    check(hstctl_after(0x0080u) == 0x0000u);

    /* The exhaustive case: ALL FIFTEEN other bits must survive. An earlier
     * version of this test only used inputs whose bit 8 was clear, so a mask
     * that wrongly cleared bit 8 as well -- $FE7F -- still passed every
     * assertion. $FFFF pins all fifteen survivors at once. */
    check(hstctl_after(0xFFFFu) == 0xFF7Fu);
    check(hstctl_after(0x0180u) == 0x0100u);   /* bits 7 AND 8 set */
    check((hstctl_after(0xFFFFu) & 0x0080u) == 0x0000u);

    /* Wrap: the tick counter is free-running, so SUB.L must wrap to a large
     * positive delta rather than a negative one. */
    s.hstctl = 0x0080u;
    s.tick_counter = 5u;
    s.tick_last_sample = 0xFFFFFFF0u;
    s.interrupted_d0 = 0u;
    stunrun_gsp_handoff_irq(&s);
    check(s.elapsed_ticks == 21u);
    check(s.elapsed_ticks_store == 21u);
    check(s.wait_flag == 0xFFFFu);
    check(s.hstctl_after == 0x0000u);

    /* Idempotence of the flag: a second delivery leaves the same observable
     * flag value, so a waiter polling it cannot tell how many times it fired. */
    s.wait_flag = 0u;
    stunrun_gsp_handoff_irq(&s);
    check(s.wait_flag == 0xFFFFu);

    /* Negative control: a NULL state must be a safe no-op. */
    stunrun_gsp_handoff_irq(NULL);

    if (failures == 0)
        printf("PASS\n");
    return failures == 0 ? 0 : 1;
}