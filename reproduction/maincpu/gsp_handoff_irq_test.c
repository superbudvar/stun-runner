/* Observable self-check for the GSP handoff interrupt (ROM $0002143C).
 *
 * Every assertion is an EFFECT the ROM produces, not "it ran":
 *   - the wait flag becomes exactly $FFFF
 *   - HSTCTL bit 15 (HLT) clears while every other bit survives, including the
 *     0x0800 HSTDAT auto-increment
 *   - the elapsed tick delta wraps instead of going negative
 *   - $FF9BAA receives the delta
 *   - the interrupted D0 comes back untouched
 *
 * The address constants are checked against the disassembly so a typo in the
 * header fails here rather than producing a plausible-looking number.
 */
#include <stdio.h>

#include "gsp_handoff_irq.h"

static int failures;

#define check(cond) do { \
    if (!(cond)) { printf("FAIL %s\n", #cond); failures++; } \
} while (0)

int main(void)
{
    stunrun_gsp_handoff_state_t s;

    /* Addresses as disassembled from the ROM. */
    check(STUNRUN_GSP_HANDOFF_ADDR_WAIT_FLAG == 0x00FFDB48u);
    check(STUNRUN_GSP_HANDOFF_ADDR_TICK_SAMP == 0x00FFDB36u);
    check(STUNRUN_GSP_HANDOFF_ADDR_ELAPSED == 0x00FF9BAAu);
    check(STUNRUN_GSP_HANDOFF_ADDR_HSTCTL == 0x00C00008u);
    check(STUNRUN_GSP_HANDOFF_HSTCTL_HLT == 0x8000u);

    /* Normal case: GSP held (HLT set) with auto-increment armed, as the 68010
     * leaves HSTCTL while uploading. */
    s.hstctl = 0xB800u;
    s.tick_counter = 1000u;
    s.tick_last_sample = 400u;
    s.interrupted_d0 = 0xDEADBEEFu;
    s.wait_flag = 0u;
    s.hstctl_after = 0u;
    s.elapsed_ticks = 0u;
    s.elapsed_ticks_store = 0u;
    stunrun_gsp_handoff_irq(&s);
    check(s.wait_flag == 0xFFFFu);
    check(s.hstctl_after == 0x3800u);        /* HLT cleared, 0x0800 preserved */
    check((s.hstctl_after & 0x0800u) != 0u); /* auto-increment survives */
    check((s.hstctl_after & 0x8000u) == 0u); /* HLT really cleared */
    check(s.elapsed_ticks == 600u);
    check(s.elapsed_ticks_store == 600u);
    check(s.interrupted_d0 == 0xDEADBEEFu);  /* saved and restored */

    /* Wrap: the tick counter is free-running, so SUB.L must wrap to a large
     * positive delta rather than a negative one. */
    s.hstctl = 0x8000u;
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

    /* Negative control: HLT already clear must still not disturb the other
     * bits, and a NULL state must be a safe no-op. */
    s.hstctl = 0x0800u;
    stunrun_gsp_handoff_irq(&s);
    check(s.hstctl_after == 0x0800u);
    stunrun_gsp_handoff_irq(NULL);

    if (failures == 0)
        printf("PASS\n");
    return failures == 0 ? 0 : 1;
}