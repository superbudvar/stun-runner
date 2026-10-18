/* View of the ADSP SOM producer's derived output.
 *
 * `./run.sh --adsp-som` draws this. It exists so the work in reproduction/adsp/
 * is visible rather than buried in slice tests: the ADSP is what produces the
 * display geometry, and until now there was no way to look at what the C
 * derived.
 *
 * WHAT THIS IS NOT. This is a plot of decoded record FIELDS, not the game's
 * rendering. The GSP's consumer of the SOM record is not decoded (bead q9t), so
 * nobody can honestly say what DM($003E)..DM($003D) mean, and this module does
 * not guess. The axes are an index space defined here and labelled with the raw
 * values; they are NOT a claim about the game's coordinate system, and the
 * colours are a documented ramp over one raw 16-bit field, not a palette.
 *
 * The word stream is derived from measured source-DM/PM inputs, sparse SIMBUF
 * selector/coefficient words, source selection, the ring producer, and the SOM
 * scan/walk. No ring, span list, interpolation result or record word is supplied
 * as a derivation input. The resulting 146 SOM words and GINT value are compared
 * with the capture; `records_matching` and `transaction_matches_capture` are the
 * claims, not the pixels.
 *
 * See docs/mc68010-boot-findings.md sections 66-72.
 */
#ifndef STUNRUN_NATIVE_ADSP_SOM_VIEW_H
#define STUNRUN_NATIVE_ADSP_SOM_VIEW_H

#include <stdint.h>

#include "render.h"
#include "som_fixture.h"
#include "som_record.h"
#include "som_transaction.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Every captured record carries two spans; the walk's own ceiling is higher and
 * this view does not need it. */
#define STUNRUN_ADSP_SOM_VIEW_MAX_SPANS 8u

typedef struct stunrun_adsp_som_view_span {
    uint16_t ax1;       /* span baseline, DM($003E)/DM($003F) side left raw */
    uint16_t ay0;
    uint16_t ay1;
    uint16_t side_3e_lo;
    uint16_t side_3e_hi;
    uint16_t side_3c_lo;
    uint16_t side_3c_hi;
    uint16_t run_rows;  /* min(AY0,AY1) - AX1 + 1, the emitter's run length */
} stunrun_adsp_som_view_span_t;

typedef struct stunrun_adsp_som_view_record {
    uint16_t control;
    uint16_t start_x;
    uint16_t delta_x;
    uint16_t start_y;
    unsigned span_count;
    stunrun_adsp_som_view_span_t spans[STUNRUN_ADSP_SOM_VIEW_MAX_SPANS];
    uint16_t words[STUNRUN_ADSP_SOM_FIXTURE_REC_WORDS];
    unsigned word_count;
    /* 1 when the derived word stream equals the captured SOMLATCH words.
     * 0 means the picture is of something the original game did not emit. */
    int matches_capture;
    /* The pointer $052F-$0544 selected, for the report. Not an input. */
    unsigned entry_i1;
    unsigned entry_i2;
} stunrun_adsp_som_view_record_t;

typedef struct stunrun_adsp_som_view {
    unsigned record_count;
    stunrun_adsp_som_view_record_t
        records[STUNRUN_ADSP_SOM_FIXTURE_RECORDS];
    unsigned total_words;
    unsigned total_spans;
    unsigned records_matching;
    unsigned long run_rows_min;
    unsigned long run_rows_max;
    stunrun_adsp_som_transaction_t transaction;
    int transaction_matches_capture;
} stunrun_adsp_som_view_t;

/* Derive the frame-551 transaction through source-table production, SIMBUF
 * selection, the two-pass ring transform, clipping, SOM walk/emit and the bus
 * envelope; compare the derived record and transaction streams with capture.
 *
 * Entry DM/SIMBUF/PM operands and the record-start schedule are the shared
 * measured fixtures. No ring, span list, interpolated side value or record word
 * is supplied as an input to the derivation.
 *
 * Returns 1 on success. Returns 0, writing nothing, on a NULL argument or if
 * any record fails to derive, emit, or fit -- a partial view would be a picture
 * of a partial transaction, which is worse than none. */
int stunrun_adsp_som_view_build(stunrun_adsp_som_view_t *view);

/* Draw the decoded record fields. Deterministic: the same view always produces
 * the same pixels. Returns 1 on success, 0 on a NULL argument, an out-of-range
 * record count, or a renderer rejection. */
int stunrun_adsp_som_view_render(stunrun_renderer_t *renderer,
                                 const stunrun_adsp_som_view_t *view);

/* The colour ramp used for side_3c_hi, as a documented function of that raw
 * value -- exposed so the test can pin it and so nothing mistakes it for a
 * palette. */
void stunrun_adsp_som_view_side_color(uint16_t side_hi, uint8_t *red,
                                      uint8_t *green, uint8_t *blue);

#ifdef __cplusplus
}
#endif

#endif /* STUNRUN_NATIVE_ADSP_SOM_VIEW_H */
