/* See adsp_som_view.h for what this does and, at length, what it does not. */
#include "adsp_som_view.h"

#include <string.h>

#include "som_clamp.h"
#include "som_producer.h"
#include "som_selector_fixture.h"
#include "som_source_select.h"
#include "som_source_table.h"
#include "source_producer_fixture.h"
#include "som_walk.h"

/* Layout. The interesting structure is a run-length encoding: each record is a
 * column of spans stepping down the ROW axis, so rows go on Y and records go
 * on X. An earlier version put AX1 on X against a $80..$110 window, which
 * squeezed eighteen spans into sixteen distinct values and made them
 * indistinguishable -- and plotted start_x on the same axis as AX1, implying
 * the two are comparable when nothing establishes that. */
#define HEADER_TOP 0u
#define HEADER_BOT 22u
#define PLOT_TOP   30u
#define PLOT_BOT   228u
#define PLOT_LEFT  64u
#define PLOT_RIGHT 504u
#define COLUMN_W   ((PLOT_RIGHT - PLOT_LEFT) / \
                    STUNRUN_ADSP_SOM_FIXTURE_RECORDS)
#define BAR_W      12u
#define GUTTER_X   8u

/* Rows present in the capture: AX1 runs $0091..$00A0 and the spans reach
 * $00A0, with a little margin either side. An index window chosen to fit the
 * data, not a coordinate space recovered from the hardware. */
#define ROW_MIN 0x0090u
#define ROW_MAX 0x00A2u

#define BACKGROUND_R 0x0au
#define BACKGROUND_G 0x0cu
#define BACKGROUND_B 0x14u
#define LANE_R       0x13u
#define LANE_G       0x17u
#define LANE_B       0x21u
#define AXIS_R       0x46u
#define AXIS_G       0x4eu
#define AXIS_B       0x5eu
#define HEADER_R     0xc8u
#define HEADER_G     0xd8u
#define HEADER_B     0xf0u
/* The far end of a span, max(AY0,AY1), is derived but unexplained; it is
 * drawn as a tick so the shape is visible rather than hidden. */
#define FAREND_R 0xffu
#define FAREND_G 0x80u
#define FAREND_B 0x40u

static int row_to_y(uint16_t row)
{
    unsigned span = ROW_MAX - ROW_MIN;
    if (span == 0u)
        return (int)PLOT_TOP;
    if (row < ROW_MIN)
        row = ROW_MIN;
    if (row > ROW_MAX)
        row = ROW_MAX;
    return (int)PLOT_TOP +
           (int)((row - ROW_MIN) * (PLOT_BOT - PLOT_TOP) / span);
}

static unsigned column_left(unsigned index)
{
    return PLOT_LEFT + index * COLUMN_W;
}


int stunrun_adsp_som_view_render(stunrun_renderer_t *renderer,
                                 const stunrun_adsp_som_view_t *view)
{
    unsigned index;
    unsigned row;

    if (renderer == NULL || view == NULL)
        return 0;
    if (view->record_count != STUNRUN_ADSP_SOM_FIXTURE_RECORDS)
        return 0;

    if (!stunrun_render_fill_xy(renderer, 0, 0, (int)STUNRUN_RENDER_WIDTH - 1,
                                (int)STUNRUN_RENDER_HEIGHT - 1,
                                BACKGROUND_R, BACKGROUND_G, BACKGROUND_B))
        return 0;

    /* Row gridlines and the record separators, so a reader can locate a bar by
     * its raw row value without a legend to look up. */
    for (row = ROW_MIN; row <= ROW_MAX; row++) {
        if (!stunrun_render_line(renderer, (int)PLOT_LEFT, row_to_y((uint16_t)row),
                                 (int)PLOT_RIGHT, row_to_y((uint16_t)row),
                                 LANE_R, LANE_G, LANE_B))
            return 0;
    }
    for (index = 0; index < view->record_count; index++) {
        unsigned left = column_left(index);
        if (!stunrun_render_line(renderer, (int)left, (int)PLOT_TOP,
                                 (int)left, (int)PLOT_BOT,
                                 AXIS_R, AXIS_G, AXIS_B))
            return 0;
        /* Record number in the gutter, drawn as a tally: the render layer has
         * no font, and a tick count is not a claim about anything. */
        if (!stunrun_render_fill_xy(renderer, (int)GUTTER_X,
                                    (int)(PLOT_TOP + 4u + index * 4u),
                                    (int)(GUTTER_X + 3u + index),
                                    (int)(PLOT_TOP + 8u + index * 4u),
                                    HEADER_R, HEADER_G, HEADER_B))
            return 0;
    }
    if (!stunrun_render_line(renderer, (int)PLOT_LEFT, (int)PLOT_TOP,
                             (int)PLOT_LEFT, (int)PLOT_BOT,
                             AXIS_R, AXIS_G, AXIS_B))
        return 0;
    if (!stunrun_render_line(renderer, (int)PLOT_RIGHT, (int)PLOT_TOP,
                             (int)PLOT_RIGHT, (int)PLOT_BOT,
                             AXIS_R, AXIS_G, AXIS_B))
        return 0;

    for (index = 0; index < view->record_count; index++) {
        const stunrun_adsp_som_view_record_t *record = &view->records[index];
        unsigned left = column_left(index);
        unsigned centre = left + COLUMN_W / 2u;
        size_t span;

        if (!stunrun_render_fill_xy(renderer, (int)left, (int)PLOT_TOP,
                                    (int)(left + COLUMN_W - 1u), (int)PLOT_BOT,
                                    LANE_R, LANE_G, LANE_B))
            return 0;
        if (!stunrun_render_line(renderer, (int)left, (int)PLOT_TOP,
                                 (int)left, (int)PLOT_BOT,
                                 AXIS_R, AXIS_G, AXIS_B))
            return 0;

        for (span = 0; span < record->span_count; span++) {
            const stunrun_adsp_som_view_span_t *s = &record->spans[span];
            uint16_t low = s->ay0 < s->ay1 ? s->ay0 : s->ay1;
            uint16_t high = s->ay0 < s->ay1 ? s->ay1 : s->ay0;
            uint8_t red, green, blue;

            stunrun_adsp_som_view_side_color(s->side_3c_hi, &red, &green, &blue);
            /* The run itself: AX1 down to min(AY0,AY1), which is exactly the
             * run length the emitter latched. */
            if (!stunrun_render_fill_xy(renderer, (int)(centre - BAR_W / 2u),
                                        row_to_y(s->ax1), (int)(centre + BAR_W / 2u),
                                        row_to_y(low), red, green, blue))
                return 0;
            /* The far end, max(AY0,AY1), as a tick at the top of the bar. */
            if (high > low &&
                !stunrun_render_fill_xy(renderer, (int)(centre - BAR_W),
                                        row_to_y(high), (int)(centre + BAR_W),
                                        (int)(row_to_y(high) + 1),
                                        FAREND_R, FAREND_G, FAREND_B))
                return 0;
        }
    }
    return 1;
}

void stunrun_adsp_som_view_side_color(uint16_t side_hi, uint8_t *red,
                                      uint8_t *green, uint8_t *blue)
{
    /* A ramp over the raw 16-bit value, and nothing more. Deliberately not a
     * palette: no claim is made that these values are colours. The low byte
     * drives brightness so that a value's own magnitude stays readable. */
    unsigned hi = side_hi >> 8;
    unsigned lo = side_hi & 0xffu;
    unsigned level = (lo * 2u) / 3u + 48u;   /* 48..218, never fully black */
    unsigned quadrant = (hi >> 6u) & 3u;

    if (red == NULL || green == NULL || blue == NULL)
        return;
    switch (quadrant) {
    case 0: *red = (uint8_t)level; *green = (uint8_t)(level / 3u);
            *blue = (uint8_t)(255u - level); break;
    case 1: *red = (uint8_t)(255u - level); *green = (uint8_t)level;
            *blue = (uint8_t)(level / 4u); break;
    case 2: *red = (uint8_t)(level / 4u); *green = (uint8_t)(255u - level);
            *blue = (uint8_t)level; break;
    default: *red = (uint8_t)level; *green = (uint8_t)(255u - level);
             *blue = (uint8_t)(level / 3u); break;
    }
}

int stunrun_adsp_som_view_build(stunrun_adsp_som_view_t *view)
{
    uint16_t dm_table[STUNRUN_ADSP_SOM_SOURCE_TABLE_WORDS];
    uint16_t all_record_words[STUNRUN_ADSP_SOM_TRANSACTION_RECORDS *
                              STUNRUN_ADSP_SOM_TRANSACTION_RECORD_WORDS];
    const uint16_t *object_rom = stunrun_adsp_som_selector_fixture_rom();
    const uint16_t *simclk_starts =
        stunrun_adsp_som_selector_fixture_simclk_starts();
    unsigned index;
    size_t total_words = 0;
    uint16_t final_ax0 = 0u;
    int final_ax0_valid = 0;

    if (view == NULL)
        return 0;
    memset(view, 0, sizeof(*view));

    if (!stunrun_adsp_som_build_source_table(
            dm_table, STUNRUN_ADSP_SOM_SOURCE_TABLE_WORDS,
            STUNRUN_ADSP_SOM_SOURCE_TABLE_BASE))
        return 0;

    for (index = 0; index < STUNRUN_ADSP_SOM_FIXTURE_RECORDS; index++) {
        const uint16_t *captured_dm = stunrun_adsp_som_fixture_dm(index);
        const uint16_t *expected = stunrun_adsp_som_fixture_expected(index);
        stunrun_adsp_som_view_record_t *record = &view->records[index];
        stunrun_adsp_som_span_t spans[STUNRUN_ADSP_SOM_VIEW_MAX_SPANS];
        stunrun_adsp_som_source_selection_t selected;
        stunrun_adsp_som_record_t derived;
        uint16_t source_dm[STUNRUN_ADSP_SOM_SOURCE_WORDS];
        uint16_t pass1[STUNRUN_ADSP_SOM_RING_WORDS];
        uint16_t ring[STUNRUN_ADSP_SOM_RING_WORDS];
        uint16_t clipped_ring[STUNRUN_ADSP_SOM_RING_WORDS];
        uint16_t dm[STUNRUN_ADSP_SOM_FIXTURE_DM_WORDS];
        size_t span;

        if (captured_dm == NULL || expected == NULL) {
            memset(view, 0, sizeof(*view));
            return 0;
        }

        if (!stunrun_adsp_som_select_source_window(
                dm_table, STUNRUN_ADSP_SOM_SOURCE_TABLE_WORDS,
                STUNRUN_ADSP_SOM_SOURCE_TABLE_BASE,
                object_rom, STUNRUN_ADSP_SOM_SELECTOR_ROM_WORDS,
                STUNRUN_ADSP_SOM_SELECTOR_ROM_BASE_WORD, 1u,
                simclk_starts[index], &selected)) {
            memset(view, 0, sizeof(*view));
            return 0;
        }
        memcpy(source_dm, selected.source_words,
               STUNRUN_ADSP_SOM_SOURCE_WINDOW_WORDS * sizeof(*source_dm));
        /* The captured $04C9 prefetch is outside the selected 9-word window;
         * retain its measured value as an explicit bounded input. */
        source_dm[STUNRUN_ADSP_SOM_SOURCE_WINDOW_WORDS] = 0u;
        if (!stunrun_adsp_som_produce_ring(
                source_dm, STUNRUN_ADSP_SOM_SOURCE_WORDS, 0x0040u, 0x0040u,
                object_rom, STUNRUN_ADSP_SOM_SELECTOR_ROM_WORDS,
                STUNRUN_ADSP_SOM_PASS2_GUARD,
                STUNRUN_ADSP_SOM_PASS2_Y_MULTIPLIER, pass1, ring) ||
            !stunrun_adsp_som_clamp_ring(
                ring, STUNRUN_ADSP_SOM_RING_WORDS, clipped_ring)) {
            memset(view, 0, sizeof(*view));
            return 0;
        }

        memcpy(dm, captured_dm, sizeof(dm));
        memcpy(&dm[0x0040u - STUNRUN_ADSP_SOM_FIXTURE_DM_BASE],
               clipped_ring, sizeof(clipped_ring));
        dm[STUNRUN_ADSP_SOM_CNTR_ADDR - STUNRUN_ADSP_SOM_FIXTURE_DM_BASE] =
            selected.dm0039;
        memset(spans, 0, sizeof(spans));
        if (!stunrun_adsp_som_derive_record(
                dm, STUNRUN_ADSP_SOM_FIXTURE_DM_WORDS,
                STUNRUN_ADSP_SOM_FIXTURE_DM_BASE,
                stunrun_adsp_som_fixture_pm(),
                STUNRUN_ADSP_SOM_FIXTURE_PM_WORDS,
                spans, STUNRUN_ADSP_SOM_VIEW_MAX_SPANS, &derived)) {
            memset(view, 0, sizeof(*view));
            return 0;
        }
        record->word_count = (unsigned)stunrun_adsp_som_emit_record(
            &derived, record->words,
            STUNRUN_ADSP_SOM_FIXTURE_REC_WORDS);
        if (record->word_count != STUNRUN_ADSP_SOM_FIXTURE_REC_WORDS) {
            memset(view, 0, sizeof(*view));
            return 0;
        }

        record->control = derived.control;
        record->start_x = derived.start_x;
        record->delta_x = derived.delta_x;
        record->start_y = derived.start_y;
        record->span_count = (unsigned)derived.span_count;
        if (record->span_count > STUNRUN_ADSP_SOM_VIEW_MAX_SPANS) {
            memset(view, 0, sizeof(*view));
            return 0;
        }
        for (span = 0; span < derived.span_count; span++) {
            stunrun_adsp_som_view_span_t *out = &record->spans[span];
            out->ax1 = spans[span].ax1;
            out->ay0 = spans[span].ay0;
            out->ay1 = spans[span].ay1;
            out->side_3e_lo = spans[span].dm_3e.lo;
            out->side_3e_hi = spans[span].dm_3e.hi;
            out->side_3c_lo = spans[span].dm_3c.lo;
            out->side_3c_hi = spans[span].dm_3c.hi;
            out->run_rows = stunrun_adsp_som_span_height(
                spans[span].ax1, spans[span].ay0, spans[span].ay1);
        }
        record->matches_capture = memcmp(record->words, expected,
                                         sizeof(record->words)) == 0;
        record->entry_i1 = stunrun_adsp_som_fixture_entry_i1(index);
        record->entry_i2 = stunrun_adsp_som_fixture_entry_i2(index);

        view->total_words += record->word_count;
        view->total_spans += record->span_count;
        view->records_matching += record->matches_capture ? 1u : 0u;
        if (index == 0u) {
            for (span = 0; span < record->span_count; span++) {
                view->run_rows_min = record->spans[span].run_rows;
                view->run_rows_max = record->spans[span].run_rows;
            }
        } else {
            for (span = 0; span < record->span_count; span++) {
                unsigned rows = record->spans[span].run_rows;
                if (view->run_rows_min == 0u || rows < view->run_rows_min)
                    view->run_rows_min = rows;
                if (rows > view->run_rows_max)
                    view->run_rows_max = rows;
            }
        }
        total_words += record->word_count;
        memcpy(&all_record_words[index * STUNRUN_ADSP_SOM_TRANSACTION_RECORD_WORDS],
               record->words, sizeof(record->words));
        final_ax0 = derived.completion_ax0;
        final_ax0_valid = derived.completion_ax0_valid;
    }

    view->record_count = STUNRUN_ADSP_SOM_FIXTURE_RECORDS;
    /* 146 SOMLATCH words: 9 records of 16 plus the two $0000 prologue words at
     * $0035/$0039, which no slice here produces. Getting this far means the
     * nine records came out whole, which is the same statement. */
    if (total_words + 2u != 146u) {
        memset(view, 0, sizeof(*view));
        return 0;
    }
    if (!stunrun_adsp_som_transaction_build(
            all_record_words, total_words, 1u, 0u,
            final_ax0_valid, final_ax0, STUNRUN_ADSP_SOM_GINT_REQUEST_MASK,
            &view->transaction)) {
        memset(view, 0, sizeof(*view));
        return 0;
    }
    view->transaction_matches_capture =
        view->transaction.host_bank == 1u &&
        view->transaction.producer_bank == 0u &&
        view->transaction.som_write_count ==
            STUNRUN_ADSP_SOM_TRANSACTION_WORDS &&
        view->transaction.gint_written &&
        view->transaction.gint_value == 0x0045u;
    for (index = 0; view->transaction_matches_capture &&
                     index < STUNRUN_ADSP_SOM_TRANSACTION_WORDS; ++index) {
        uint16_t expected_word;
        if (index < STUNRUN_ADSP_SOM_TRANSACTION_PROLOGUE_WORDS) {
            expected_word = 0u;
        } else {
            size_t record_index =
                (index - STUNRUN_ADSP_SOM_TRANSACTION_PROLOGUE_WORDS) /
                STUNRUN_ADSP_SOM_TRANSACTION_RECORD_WORDS;
            size_t word_index =
                (index - STUNRUN_ADSP_SOM_TRANSACTION_PROLOGUE_WORDS) %
                STUNRUN_ADSP_SOM_TRANSACTION_RECORD_WORDS;
            expected_word = stunrun_adsp_som_fixture_expected(
                (unsigned)record_index)[word_index];
        }
        if (view->transaction.writes[index].address_word != index ||
            view->transaction.writes[index].value != expected_word)
            view->transaction_matches_capture = 0;
    }
    if (!view->transaction_matches_capture) {
        memset(view, 0, sizeof(*view));
        return 0;
    }
    return 1;
}
