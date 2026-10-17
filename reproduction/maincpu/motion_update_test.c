#include "motion_update.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    stunrun_motion_update_state_t state;

    state = (stunrun_motion_update_state_t){
        0x0100u, 2u, 0x1234u, 0x12u, 0x07E0, 0x0011, 0x0022, 0x1000u
    };
    state = stunrun_motion_update_step(state);
    assert(state.motion_word == 0x0140u);
    assert(state.timer == 1u);
    assert(state.secondary_timer == 0x1234u);
    assert(state.timer_expired_flag == 0x12u);
    assert(state.accumulator == 0x1140u);

    /* Timer 1 expires on this pass, but the original timer path still skips
     * decay: BCC at $03ABA0 jumps directly to the accumulator update. */
    state = (stunrun_motion_update_state_t){
        0x0B00u, 1u, 0x1234u, 0x12u, 0x07E0, 0x0011, 0x0022, 0x1000u
    };
    state = stunrun_motion_update_step(state);
    assert(state.motion_word == 0x0B00u);
    assert(state.timer == 0u);
    assert(state.secondary_timer == 0u);
    assert(state.timer_expired_flag == 0xFFu);
    assert(state.accumulator == 0x1B00u);

    /* With timer zero, BLS compares the sign-extended longs as unsigned. */
    state = (stunrun_motion_update_state_t){
        0x0900u, 0u, 0x7777u, 0x34u, 0x07E0, 0x0011, 0x0022, 0x2000u
    };
    state = stunrun_motion_update_step(state);
    assert(state.motion_word == 0x08E0u);
    assert(state.secondary_timer == 0x7777u);
    assert(state.timer_expired_flag == 0x34u);
    assert(state.accumulator == 0x28E0u);

    /* Equality takes the BLS branch to the sign-extended source-word sum. */
    state = (stunrun_motion_update_state_t){
        0x0820u, 0u, 0u, 0u, 0x07E0, 0x0011, 0x0022, 0x3000u
    };
    state = stunrun_motion_update_step(state);
    assert(state.motion_word == 0x0033u);
    assert(state.accumulator == 0x3033u);

    /* The D3 value was EXT.L'd before CMP.L; raw FFFF is -1, hence unsigned
     * FFFFFFFF exceeds the positive limit and the word subtraction wraps. */
    state = (stunrun_motion_update_state_t){
        0xFFFFu, 0u, 0u, 0u, 0x07E0, 0, 0, 0u
    };
    state = stunrun_motion_update_step(state);
    assert(state.motion_word == 0xFFDFu);
    assert(state.accumulator == 0xFFFFFFDFu);

    puts("motion-update state transition: all checks passed");
    return 0;
}
