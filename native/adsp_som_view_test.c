/* Tests for the ADSP SOM view (native/adsp_som_view.c).
 *
 * The view's job is to derive the captured transaction and draw it. The
 * assertion that matters is therefore that the DERIVED word stream matches the
 * capture -- if that did not hold, every pixel would be a picture of something
 * the original game never emitted. This file pins:
 *
 *   1. all nine records derive, and every one matches its captured words;
 *   2. the field values the plot is built from, word by word, for all 18 spans
 *      -- so a change in span_height or in either side value cannot pass;
 *   3. the header words and the $052F-$0544 entry pointers, which the view
 *      reports and the walk must have reproduced;
 *   4. that rendering is deterministic and actually draws: same hash twice, a
 *      different hash from the background, and a non-zero lit-pixel count;
 *   5. the documented colour ramp at chosen inputs, pinned so "it looked fine"
 *      is not the criterion;
 *   6. rejection of NULL and of a short record list, rather than a partial view.
 *
 * Inputs include the record/math fixture (`som_fixture.c`), sparse observed
 * selector words (`som_selector_fixture.c`), and measured source producer
 * operands (`source_producer_fixture.c`). No ring, span or record output is
 * supplied to the derivation. The record fixture's invariants are checked by
 * `tools/check-adsp-som-fixture`. See
 * docs/mc68010-boot-findings.md sections 66-72 and 92-95.
 */
#include <stdio.h>
#include <string.h>

#include "adsp_som_view.h"

/* The view's own background, so "the render drew something" is a comparison
 * against the exact canvas it started from rather than against a guess. */
#define BG_R 0x0au
#define BG_G 0x0cu
#define BG_B 0x14u

static int failures;

#define check(c) do { if (!(c)) { printf("FAIL %s\n", #c); failures++; } } while (0)

/* Per-span fields that ARE in the captured word stream: the four side-value
 * words and the run length, read straight out of the SOMLATCH records in the
 * fixture. Order is record, then span, then
 *   side_3e_lo side_3e_hi side_3c_lo side_3c_hi run_rows.
 *
 * AX1/AY0/AY1 are deliberately NOT tabulated here. Only the first span's AX1 is
 * observable (it is the record's start_y, the word latched at $056B); the rest
 * are internal walk registers that never reach SOMLATCH. Pinning them to
 * numbers taken from this C's own output would be pinning the view to itself,
 * so they are checked as invariants in test_span_invariants instead. */
typedef struct want_span {
    uint16_t e_lo, e_hi, c_lo, c_hi;
    uint16_t run_rows;
} want_span_t;

static const want_span_t WANT_SPANS[9][2] = {
    { {0x6DB8, 0xFFFF, 0xCCDA, 0xFFFD, 6}, {0x6DB8, 0xFFFF, 0x8000, 0x0003, 3} },
    { {0x3326, 0x0002, 0x9248, 0x0000, 6}, {0x8000, 0xFFFC, 0x9248, 0x0000, 3} },
    { {0x9248, 0x0000, 0x6DB8, 0xFFFF, 8}, {0x0000, 0x0000, 0x0000, 0x0000, 1} },
    { {0x8000, 0x0006, 0x4924, 0x0000, 3}, {0xCCDA, 0xFFFD, 0x4924, 0x0000, 6} },
    { {0xB6DC, 0xFFFF, 0x8000, 0xFFF9, 3}, {0xB6DC, 0xFFFF, 0x3326, 0x0002, 6} },
    { {0x0000, 0x0000, 0x8000, 0xFFFB, 3}, {0x0000, 0x0000, 0x7FFA, 0x0001, 7} },
    { {0x0000, 0x0001, 0x8006, 0xFFFE, 5}, {0x8000, 0xFFF9, 0x8006, 0xFFFE, 3} },
    { {0x7FFA, 0x0001, 0x0000, 0xFFFF, 5}, {0x7FFA, 0x0001, 0x8000, 0x0006, 3} },
    { {0x8000, 0x0004, 0x0000, 0x0000, 3}, {0x8006, 0xFFFE, 0x0000, 0x0000, 7} },
};

static const uint16_t WANT_START_X[9] = {
    0x00FF, 0x00FF, 0x00FF, 0x00F2, 0x010C, 0x00FF, 0x0108, 0x00F6, 0x00FF
};
static const uint16_t WANT_START_Y[9] = {
    0x0099, 0x0099, 0x0099, 0x0097, 0x0097, 0x0091, 0x0093, 0x0093, 0x0091
};
static const unsigned WANT_ENTRY[9] = {
    0x0042, 0x0042, 0x0042, 0x0040, 0x0044, 0x0040, 0x0044, 0x0040, 0x0040
};

static void test_builds_and_matches_capture(void)
{
    stunrun_adsp_som_view_t view;
    unsigned i;

    check(stunrun_adsp_som_view_build(&view));
    check(view.record_count == STUNRUN_ADSP_SOM_FIXTURE_RECORDS);
    check(view.total_words == 9u * STUNRUN_ADSP_SOM_FIXTURE_REC_WORDS);
    check(view.total_words == 144u);
    check(view.total_spans == 18u);
    check(view.records_matching == STUNRUN_ADSP_SOM_FIXTURE_RECORDS);
    check(view.transaction_matches_capture);
    check(view.transaction.host_bank == 1u);
    check(view.transaction.producer_bank == 0u);
    check(view.transaction.som_start_word == 0u);
    check(view.transaction.som_write_count ==
          STUNRUN_ADSP_SOM_TRANSACTION_WORDS);
    check(view.transaction.writes[0].value == 0u);
    check(view.transaction.writes[1].value == 0u);
    check(view.transaction.gint_written);
    check(view.transaction.gint_pc == 0x004cu);
    check(view.transaction.gint_value == 0x0045u);
    check(view.transaction.gint_after_write_count ==
          STUNRUN_ADSP_SOM_TRANSACTION_WORDS);
    for (i = 0; i < view.record_count; i++) {
        unsigned word;
        check(view.records[i].matches_capture);
        check(view.records[i].word_count ==
              STUNRUN_ADSP_SOM_FIXTURE_REC_WORDS);
        check(view.records[i].control == 0x0030u);
        check(view.records[i].start_x == WANT_START_X[i]);
        /* start_x and delta_x latch equal in all nine captured records; the
         * header order is not discriminated by the capture, so this is a
         * property of the data, not a claim about the ROM's field order. */
        check(view.records[i].delta_x == WANT_START_X[i]);
        check(view.records[i].start_y == WANT_START_Y[i]);
        check(view.records[i].entry_i1 == WANT_ENTRY[i]);
        check(view.records[i].entry_i2 == WANT_ENTRY[i]);
        for (word = 0; word < STUNRUN_ADSP_SOM_FIXTURE_REC_WORDS; word++) {
            unsigned bus_slot = STUNRUN_ADSP_SOM_TRANSACTION_PROLOGUE_WORDS +
                i * STUNRUN_ADSP_SOM_TRANSACTION_RECORD_WORDS + word;
            check(view.transaction.writes[bus_slot].address_word == bus_slot);
            check(view.transaction.writes[bus_slot].value ==
                  view.records[i].words[word]);
        }
    }
    check(view.run_rows_min == 1u);
    check(view.run_rows_max == 8u);
}

static void test_span_fields(void)
{
    stunrun_adsp_som_view_t view;
    unsigned i, s;

    check(stunrun_adsp_som_view_build(&view));
    for (i = 0; i < view.record_count; i++) {
        check(view.records[i].span_count == 2u);
        for (s = 0; s < 2u; s++) {
            const stunrun_adsp_som_view_span_t *got = &view.records[i].spans[s];
            const want_span_t *want = &WANT_SPANS[i][s];
            if (got->side_3e_lo != want->e_lo || got->side_3e_hi != want->e_hi ||
                got->side_3c_lo != want->c_lo || got->side_3c_hi != want->c_hi ||
                got->run_rows != want->run_rows) {
                printf("FAIL record %u span %u: got "
                       "e=%04X/%04X c=%04X/%04X h=%u, want "
                       "e=%04X/%04X c=%04X/%04X h=%u\n",
                       i, s, got->side_3e_lo, got->side_3e_hi,
                       got->side_3c_lo, got->side_3c_hi, got->run_rows,
                       want->e_lo, want->e_hi, want->c_lo, want->c_hi,
                       want->run_rows);
                failures++;
            }
        }
    }
}

/* The walk registers that never reach SOMLATCH, checked as invariants rather
 * than as literals, because no captured word carries them.
 *
 * The cross-span check below is that each span's baseline is the PREVIOUS
 * span's last row. That is an inference from the run-length structure, not a
 * measurement, and it is stated as such: it holds for all nine captured
 * records. It is not implied by the within-span arithmetic, which is derived
 * from the same register, so it is a real constraint on the walk.
 */
static void test_span_invariants(void)
{
    stunrun_adsp_som_view_t view;
    unsigned i, s;

    check(stunrun_adsp_som_view_build(&view));
    for (i = 0; i < view.record_count; i++) {
        const stunrun_adsp_som_view_record_t *record = &view.records[i];

        /* The first span's baseline IS the record's start_y -- the word the
         * emitter latched at $056B -- so this one is measured, not inferred. */
        check(record->spans[0].ax1 == record->start_y);

        for (s = 0; s < record->span_count; s++) {
            const stunrun_adsp_som_view_span_t *span = &record->spans[s];
            unsigned low = span->ay0 < span->ay1 ? span->ay0 : span->ay1;

            /* The emitter's run length is min(AY0,AY1) - AX1 + 1, so the span
             * must close exactly on its own baseline. */
            check((unsigned)(low - span->ax1) + 1u == span->run_rows);
            /* Both guards the walk enforces: AY0 and AY1 never below AX1. */
            check(span->ay0 >= span->ax1);
            check(span->ay1 >= span->ax1);
            /* Chain continuity: the next span's baseline is the PREVIOUS
             * span's last row, min(AY0,AY1) -- so consecutive spans share
             * their boundary row rather than butting up against it. This is
             * not implied by the arithmetic above, so a walk that advanced a
             * chain by the wrong amount fails here. */
            if (s > 0u) {
                const stunrun_adsp_som_view_span_t *prev =
                    &record->spans[s - 1u];
                unsigned prev_low = prev->ay0 < prev->ay1 ? prev->ay0 : prev->ay1;
                check(span->ax1 == (uint16_t)prev_low);
                check(prev_low ==
                      (unsigned)(prev->ax1 + prev->run_rows - 1u));
            }
        }
    }
}

static void test_render_is_deterministic_and_draws(void)
{
    stunrun_adsp_som_view_t view;
    stunrun_renderer_t a, b, background;
    uint32_t hash_a, hash_b, hash_bg;
    unsigned lit = 0, colours = 0;

    check(stunrun_adsp_som_view_build(&view));

    stunrun_render_init(&a);
    stunrun_render_begin(&a, 0u, BG_R, BG_G, BG_B);
    check(stunrun_adsp_som_view_render(&a, &view));

    stunrun_render_init(&b);
    stunrun_render_begin(&b, 0u, BG_R, BG_G, BG_B);
    check(stunrun_adsp_som_view_render(&b, &view));

    hash_a = stunrun_render_hash(&a);
    hash_b = stunrun_render_hash(&b);
    check(hash_a == hash_b);

    stunrun_render_content_stats(&a, &lit, &colours);
    check(lit > 0u);
    check(colours > 1u);

    /* A hash alone cannot distinguish "drew the view" from "drew a stable
     * wrong thing", so also require it to differ from the same canvas filled
     * with the view's background. */
    stunrun_render_init(&background);
    stunrun_render_begin(&background, 0u, BG_R, BG_G, BG_B);
    hash_bg = stunrun_render_hash(&background);
    check(hash_a != hash_bg);
}

/* Count span pixels in a column, ignoring the flat fills the view paints over
 * the whole column. A frame-level lit-pixel count cannot tell "drew nine
 * records" from "drew one record very brightly", so each record's column is
 * checked separately -- that is what catches a partial render.
 *
 * Both the column background AND the axis separator colour are excluded. An
 * earlier version excluded only the background, and every partial-render
 * control passed: the separator line down the left of each column was being
 * counted as bar content, which is exactly the "a test that cannot fail"
 * failure this file exists to avoid. */
static int is_flat(unsigned char r, unsigned char g, unsigned char b)
{
    /* LANE_R/G/B, the per-column background */
    if (r == 0x13u && g == 0x17u && b == 0x21u)
        return 1;
    /* AXIS_R/G/B, the column separators and the row gridlines */
    if (r == 0x46u && g == 0x4eu && b == 0x5eu)
        return 1;
    /* BACKGROUND_R/G/B. This one matters more than it looks: a render that
     * skips a record leaves that column at the page background rather than at
     * the lane colour, so without this the undrawn pixels all counted as
     * content and every "only record 0 is drawn" control passed. Two rounds of
     * a control that would not fail. */
    if (r == 0x0au && g == 0x0cu && b == 0x14u)
        return 1;
    return 0;
}

/* Rows of record `index`'s column that carry any span content, as a bitmap.
 *
 * KNOWN LIMIT, recorded rather than worked around: this compares the UNION of
 * a record's spans, and consecutive spans share their boundary row by
 * construction (the chain advances to min(AY0,AY1) of the previous span). So
 * mutating a bar to end at max(AY0,AY1) instead of min(AY0,AY1) is an
 * EQUIVALENT MUTANT here -- verified: it passes, and it passes for all nine
 * records, because the next span's bar already covers the rows the change would
 * add. Catching it needs per-span discrimination, and since both spans share a
 * column centre there is no x-axis evidence either. Eight other render controls
 * do fail, including dropping a record, dropping a span, collapsing a bar and
 * inverting the row mapping.
 * Structural rather than a pixel COUNT on purpose: an exact pixel count would
 * make this test a second implementation of the renderer's geometry, which is
 * its own kind of wrong. "Which rows are occupied" pins the thing that matters
 * -- every span drawn, at the right height, in the right column -- and is
 * insensitive to how wide the bar is. */
static void rows_with_content(const stunrun_renderer_t *renderer, unsigned index,
                              unsigned char *mask)
{
    const unsigned left = 64u + index * ((504u - 64u) / 9u);
    const unsigned right = left + ((504u - 64u) / 9u) - 1u;
    unsigned x, y;

    for (y = 0; y < STUNRUN_RENDER_HEIGHT; y++)
        mask[y] = 0;
    for (y = 31u; y <= 227u; y++) {
        for (x = left; x <= right; x++) {
            const uint8_t *pixel =
                &renderer->pixels[(y * STUNRUN_RENDER_WIDTH + x) *
                                  STUNRUN_RENDER_CHANNELS];
            if (!is_flat(pixel[0], pixel[1], pixel[2])) {
                mask[y] = 1;
                break;
            }
        }
    }
}

/* The view's row mapping, recomputed from the documented constants rather than
 * imported, for the same reason as column_left() above. */
static int expect_row_to_y(uint16_t row)
{
    if (row < 0x0090u)
        row = 0x0090u;
    if (row > 0x00A2u)
        row = 0x00A2u;
    return 30 + (int)((row - 0x0090u) * (228u - 30u) / (0x00A2u - 0x0090u));
}

static void test_render_draws_every_record(void)
{
    stunrun_adsp_som_view_t view;
    stunrun_renderer_t renderer;
    unsigned i, y, s;

    check(stunrun_adsp_som_view_build(&view));
    stunrun_render_init(&renderer);
    stunrun_render_begin(&renderer, 0u, BG_R, BG_G, BG_B);
    check(stunrun_adsp_som_view_render(&renderer, &view));

    for (i = 0; i < view.record_count; i++) {
        unsigned char got[STUNRUN_RENDER_HEIGHT];
        unsigned char want[STUNRUN_RENDER_HEIGHT];
        int differs = 0;

        rows_with_content(&renderer, i, got);
        for (y = 0; y < STUNRUN_RENDER_HEIGHT; y++)
            want[y] = 0;
        for (s = 0; s < view.records[i].span_count; s++) {
            const stunrun_adsp_som_view_span_t *span = &view.records[i].spans[s];
            uint16_t low = span->ay0 < span->ay1 ? span->ay0 : span->ay1;
            uint16_t high = span->ay0 < span->ay1 ? span->ay1 : span->ay0;
            int top = expect_row_to_y(span->ax1);
            int bottom = expect_row_to_y(low);
            for (y = (unsigned)top; y <= (unsigned)bottom; y++)
                want[y] = 1;
            /* The far-end tick, when the span's two ends differ. */
            if (high > low) {
                int far = expect_row_to_y(high);
                if (far < top)
                    far = top;
                want[(unsigned)far] = 1;
                if ((unsigned)far + 1u < STUNRUN_RENDER_HEIGHT)
                    want[(unsigned)far + 1u] = 1;
            }
        }
        for (y = 0; y < STUNRUN_RENDER_HEIGHT; y++)
            if (got[y] != want[y]) {
                differs = 1;
                break;
            }
        if (differs) {
            printf("FAIL record %u column content rows differ from its spans\n", i);
            for (y = 0; y < STUNRUN_RENDER_HEIGHT; y++)
                if (got[y] != want[y])
                    printf("   row %3u: got %u want %u\n", y, got[y], want[y]);
            failures++;
        }
    }
}

static void test_side_colour_ramp(void)
{
    uint8_t r, g, b;
    uint8_t r2, g2, b2;

    /* Pinned points, not "it looked fine". The ramp keys off the high byte in
     * four quadrants and the low byte drives brightness to
     * lo*2/3 + 48, so these are exact rather than approximate. */
    stunrun_adsp_som_view_side_color(0x0000u, &r, &g, &b);
    check(r == 48u && g == 16u && b == 207u);          /* quadrant 0, lo=0 */

    stunrun_adsp_som_view_side_color(0x00ffu, &r, &g, &b);
    check(r == 218u && g == 72u && b == 37u);          /* quadrant 0, lo=255 */

    stunrun_adsp_som_view_side_color(0x8000u, &r, &g, &b);
    check(r == 12u && g == 207u && b == 48u);          /* quadrant 2, lo=0 */

    stunrun_adsp_som_view_side_color(0xffffu, &r, &g, &b);
    check(r == 218u && g == 37u && b == 72u);          /* quadrant 3, lo=255 */

    stunrun_adsp_som_view_side_color(0xff00u, &r, &g, &b);
    stunrun_adsp_som_view_side_color(0x7fffu, &r2, &g2, &b2);
    check(r != r2 || g != g2 || b != b2);

    /* No output past full scale: the low byte drives brightness, and 255 is the
     * largest the capture holds. */
    stunrun_adsp_som_view_side_color(0x80ffu, &r, &g, &b);
    check(r <= 255u && g <= 255u && b <= 255u);

    stunrun_adsp_som_view_side_color(0x1234u, NULL, NULL, NULL); /* must not crash */
}

static void test_rejects_bad_arguments(void)
{
    stunrun_adsp_som_view_t view;
    stunrun_adsp_som_view_t truncated;
    stunrun_renderer_t renderer;

    check(!stunrun_adsp_som_view_build(NULL));
    check(stunrun_adsp_som_view_build(&view));
    check(!stunrun_adsp_som_view_render(NULL, &view));

    stunrun_render_init(&renderer);
    stunrun_render_begin(&renderer, 0u, BG_R, BG_G, BG_B);
    check(!stunrun_adsp_som_view_render(&renderer, NULL));

    /* A view that is not the full transaction must be refused: a partial
     * picture would look like a complete one. */
    truncated = view;
    truncated.record_count = view.record_count - 1u;
    check(!stunrun_adsp_som_view_render(&renderer, &truncated));

    truncated = view;
    truncated.record_count = view.record_count + 1u;
    check(!stunrun_adsp_som_view_render(&renderer, &truncated));
}

int main(void)
{
    test_builds_and_matches_capture();
    test_span_fields();
    test_span_invariants();
    test_render_is_deterministic_and_draws();
    test_render_draws_every_record();
    test_side_colour_ramp();
    test_rejects_bad_arguments();

    if (failures == 0)
        puts("PASS");
    return failures == 0 ? 0 : 1;
}
