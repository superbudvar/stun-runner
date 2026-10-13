/* Object-hit -> award-select -> score-add chain. See award_path.h.
 *
 * This is a thin adapter over the canonical model in
 * native/mc68010_physics.c. There must be ONE implementation of the
 * $03A298/$03A2E2/$03A2EC chain; this file keeps the older, narrower
 * reproduction/ API working without re-deriving the arithmetic.
 *
 * The canonical model also covers the dedupe at $03A294, the 16-bit
 * counter wrap, the 32-bit score add, and the course milestone lookup
 * in the ROM table at $4EE4E. Those are not reachable through this
 * narrower signature, so the adapter passes values that keep the
 * dedupe from firing (distinct record and latch) and passes a NULL
 * threshold table, which models an all-zero table and suppresses the
 * milestone. Callers that need those behaviours should call
 * stunrun_mc68010_collision_award() directly.
 */

#include "award_path.h"

#include "mc68010_physics.h"

#include <stddef.h>

stunrun_award_event_t stunrun_award_event(unsigned course_value, int hit)
{
    stunrun_collision_award_t full;
    stunrun_award_event_t event = {0, 0, 0};

    full = stunrun_mc68010_collision_award(hit,
                                           1u /* record      */,
                                           2u /* last_record: differs, so
                                                 * the $03A294 dedupe
                                                 * does not fire */,
                                           0u /* hit_count   */,
                                           0u /* score       */,
                                           (uint16_t)course_value,
                                           NULL /* thresholds */);

    /* PC 0x03A298: increment the 0xFFDD02 hit counter. */
    event.counter_inc = (unsigned)full.counter_written;

    /* PC 0x03A2E2 / $03A2EA: 0x1F4 when the course word is nonzero,
     * else 0x32. Taken from the canonical model so the two never drift. */
    event.award = full.score_written
                      ? (uint16_t)(course_value != 0u ? STUNRUN_AWARD_HIGH_VALUE
                                                      : STUNRUN_AWARD_LOW_VALUE)
                      : (uint16_t)0;

    /* PC 0x03A2EC: add the selected award to 0xFF9532. */
    event.score_add = event.award;
    return event;
}
