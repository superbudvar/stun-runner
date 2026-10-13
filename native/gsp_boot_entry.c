/* ROM $506C-$5078: set SP, materialise the first continuation address, branch
 * into the GSP boot sequence.  See gsp_boot_entry.h for the transcription and
 * the provenance. */

#include "gsp_boot_entry.h"

uint32_t stunrun_gsp_boot_entry_pc_relative(uint32_t pc_base,
                                            int16_t displacement)
{
    int32_t signed_disp = (int32_t)displacement;
    uint32_t offset;

    /* `(d,PC)` is a signed 16-bit form, so the extension word is sign
     * extended.  Negate the magnitude in the unsigned domain rather than
     * casting a negative value straight to uint32_t: both wrap the same way,
     * but the first form says so and the second only looks correct. */
    if (signed_disp < 0)
        offset = 0u - (uint32_t)(-signed_disp);
    else
        offset = (uint32_t)signed_disp;

    return pc_base + offset;
}

void stunrun_gsp_boot_entry_init(stunrun_gsp_boot_entry_state_t *state)
{
    if (state == NULL)
        return;

    state->ctx = NULL;
    state->emit = NULL;

    /* A caller SP and A0 that are not the values the ROM wants, so that a
     * test can observe that $506C overwrites both.  $506C has no caller whose
     * stack it is extending: the two `jmp $506C.l` sites at $0106 and $0172
     * are thunk-table entries and the third, $02B2, is reached straight out of
     * a port test. */
    state->a7 = 0xDEADBEEFu;
    state->a0 = 0x12345678u;

    state->lea_pc_base = STUNRUN_GSP_BOOT_ENTRY_LEA_PC_BASE;
    state->lea_displacement = (int16_t)STUNRUN_GSP_BOOT_ENTRY_LEA_DISP;
    state->bra_pc_base = STUNRUN_GSP_BOOT_ENTRY_BRA_PC_BASE;
    state->bra_displacement = (int16_t)STUNRUN_GSP_BOOT_ENTRY_BRA_DISP;

    state->continuation = 0u;
    state->transfer_pc = 0u;
}

void stunrun_gsp_boot_entry_step(stunrun_gsp_boot_entry_state_t *state)
{
    uint32_t continuation;
    uint32_t target;

    if (state == NULL || state->emit == NULL)
        return;

    /* $506C: movea.l #$ffffc000,A7.  A replace, not an add. */
    state->a7 = STUNRUN_GSP_BOOT_ENTRY_SP;
    state->emit(state->ctx, STUNRUN_GSP_BOOT_ENTRY_SET_SP, state->a7);

    /* $5072: lea ($6,PC),A0 -- $5074 + $0006 = $507A.  The base is the address
     * of this instruction's extension word. */
    continuation = stunrun_gsp_boot_entry_pc_relative(state->lea_pc_base,
                                                      state->lea_displacement);
    state->a0 = continuation;
    state->continuation = continuation;
    state->emit(state->ctx, STUNRUN_GSP_BOOT_ENTRY_SET_CONTINUATION, state->a0);

    /* $5076: bra.s $53f2 -- $5078 + $037A = $53F2, which ends in `jmp (A0)` at
     * $548C and so comes back to the A0 just materialised. */
    target = stunrun_gsp_boot_entry_pc_relative(state->bra_pc_base,
                                                state->bra_displacement);
    state->transfer_pc = target;
    state->emit(state->ctx, STUNRUN_GSP_BOOT_ENTRY_TRANSFER, target);
}