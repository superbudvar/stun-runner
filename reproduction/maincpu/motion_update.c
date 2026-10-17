#include "motion_update.h"

static int32_t sign_extend_word(uint16_t word)
{
    if ((word & 0x8000u) != 0u)
        return (int32_t)word - 0x10000;
    return (int32_t)word;
}

stunrun_motion_update_state_t stunrun_motion_update_step(
    stunrun_motion_update_state_t state)
{
    if (state.timer != 0u) {
        /* ROM $03AB80-$03ABAA: once on the timer path, the decay/base-sum
         * path is skipped even when the field is already at the ramp limit. */
        state.timer = (uint16_t)(state.timer - 1u);
        if (state.timer == 0u) {
            state.secondary_timer = 0u;
            state.timer_expired_flag = 0xffu;
        }
        if (state.motion_word < STUNRUN_MOTION_RAMP_LIMIT)
            state.motion_word =
                (uint16_t)(state.motion_word + STUNRUN_MOTION_RAMP_STEP);
    } else {
        /* MOVE.W / EXT.L and CMP.L / BLS: the comparison is unsigned over
         * sign-extended 32-bit operands, not a signed C comparison. */
        uint32_t motion_long = (uint32_t)sign_extend_word(state.motion_word);
        int32_t limit_long = (int32_t)state.local_limit +
                             STUNRUN_MOTION_RAMP_STEP;
        if (motion_long > (uint32_t)limit_long) {
            state.motion_word =
                (uint16_t)(state.motion_word - STUNRUN_MOTION_DECAY_STEP);
        } else {
            int32_t sum = sign_extend_word((uint16_t)state.source_a) +
                          sign_extend_word((uint16_t)state.source_b);
            state.motion_word = (uint16_t)sum;
        }
    }

    /* ROM $03ABE4-$03ABEC: MOVE.W / EXT.L then ADD.L, wrapping modulo 2^32. */
    state.accumulator += (uint32_t)sign_extend_word(state.motion_word);
    return state;
}
