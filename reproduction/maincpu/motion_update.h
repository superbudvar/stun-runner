/* Literal 68010 motion-field update block at ROM $03AB7A-$03ABEC. */
#ifndef STUNRUN_MOTION_UPDATE_H
#define STUNRUN_MOTION_UPDATE_H

#include <stdint.h>

#define STUNRUN_MOTION_RAMP_LIMIT 0x0B00
#define STUNRUN_MOTION_RAMP_STEP  0x0040
#define STUNRUN_MOTION_DECAY_STEP 0x0020

/* Input/output words and long at the routine boundary. These are mechanism
 * names; the caller's timer, source words, and motion field's game semantics
 * are not assigned here. The ROM-visible addresses are:
 *   motion_word        $FFDD16
 *   timer              $FFDD4E
 *   secondary_timer    $FFDD50
 *   timer_expired_flag $FFDD0A
 *   source_a           $FFDD1E
 *   source_b           $FFDD1A
 *   accumulator        $FFDBFE
 * local_limit is the caller's stack word at A6-$18 (normally $07E0).
 */
typedef struct stunrun_motion_update_state {
    uint16_t motion_word;
    uint16_t timer;
    uint16_t secondary_timer;
    uint8_t timer_expired_flag;
    int16_t local_limit;
    int16_t source_a;
    int16_t source_b;
    uint32_t accumulator;
} stunrun_motion_update_state_t;

/* Execute the branch and side effects of $03AB7A-$03ABEC once. It preserves
 * 68010 word wrapping and signed EXT.L values, uses the ROM's unsigned BCC/BLS
 * conditions, and adds the sign-extended result to the wrapping longword. */
stunrun_motion_update_state_t stunrun_motion_update_step(
    stunrun_motion_update_state_t state);

#endif /* STUNRUN_MOTION_UPDATE_H */
