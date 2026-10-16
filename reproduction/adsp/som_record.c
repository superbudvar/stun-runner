/* Bounded C transcription of the ADSP-2100 SOM record output sequence. */
#include "som_record.h"

size_t stunrun_adsp_som_record_words(size_t span_count)
{
    /* Guard the multiply before it can wrap: SIZE_MAX/5 spans is far beyond
     * any real ADSP chain, and silently wrapping would report a short length. */
    if (span_count > (SIZE_MAX - (STUNRUN_ADSP_SOM_HEADER_WORDS +
                                 STUNRUN_ADSP_SOM_SENTINEL_WORDS))
                     / STUNRUN_ADSP_SOM_SPAN_WORDS)
        return 0;
    return STUNRUN_ADSP_SOM_HEADER_WORDS +
           span_count * STUNRUN_ADSP_SOM_SPAN_WORDS +
           STUNRUN_ADSP_SOM_SENTINEL_WORDS;
}

uint16_t stunrun_adsp_som_span_height(uint16_t ax1, uint16_t ay0, uint16_t ay1)
{
    uint16_t base;

    /* $057B / $057C select the branch; each branch then subtracts one of the
     * two endpoints, and the guard at $0574/$0577 already established that the
     * selected endpoint is above AX1. */
    if (ay1 > ay0)
        base = ay0;   /* $059D: AF = AY0 - AX1 */
    else if (ay1 == ay0)
        base = ay1;   /* $057D: AF = AY1 - AX1 */
    else
        base = ay1;   /* $05B7: AF = AY1 - AX1 */

    return (uint16_t)(base - ax1 + 1u);
}

size_t stunrun_adsp_som_emit_record(const stunrun_adsp_som_record_t *record,
                                    uint16_t *out, size_t out_capacity)
{
    size_t needed;
    size_t written = 0;
    size_t i;

    if (record == NULL || out == NULL)
        return 0;
    if (record->span_count > 0 && record->spans == NULL)
        return 0;

    needed = stunrun_adsp_som_record_words(record->span_count);
    if (needed == 0 || out_capacity < needed)
        return 0;

    /* Validate every span before writing anything: a rejected record must not
     * leave a half-emitted prefix behind for the caller to mistake for output. */
    for (i = 0; i < record->span_count; i++) {
        const stunrun_adsp_som_span_t *span = &record->spans[i];
        if (i == 0) {
            /* $0574 / $0577 use IF LE. */
            if (span->ay0 <= span->ax1 || span->ay1 <= span->ax1)
                return 0;
        } else {
            /* Loop tails use IF LT. */
            if (span->ay0 < span->ax1 || span->ay1 < span->ax1)
                return 0;
        }
    }

    out[written++] = record->control;  /* $0515 */
    out[written++] = record->start_x;  /* $0565 */
    out[written++] = record->delta_x;  /* $0567 */
    out[written++] = record->start_y;  /* $056B */

    for (i = 0; i < record->span_count; i++) {
        const stunrun_adsp_som_span_t *span = &record->spans[i];
        /* $057D/$059D/$05B7 and the three sibling tails share this write order:
         * DM($003E), DM($003F), DM($003C), DM($003D), then the run length. */
        out[written++] = span->dm_3e.lo;
        out[written++] = span->dm_3e.hi;
        out[written++] = span->dm_3c.lo;
        out[written++] = span->dm_3c.hi;
        out[written++] = stunrun_adsp_som_span_height(span->ax1, span->ay0,
                                                      span->ay1);
    }

    /* $05D1 sets AR to $FFFF and $05D2/$05D4 latch it twice. */
    out[written++] = STUNRUN_ADSP_SOM_TERMINATOR;
    out[written++] = STUNRUN_ADSP_SOM_TERMINATOR;

    return written;
}