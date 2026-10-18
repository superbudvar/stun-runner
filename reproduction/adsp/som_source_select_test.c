/* Oracle-backed data-side vectors for ADSP $01E8/$01B0.
 *
 * DM-table words are the writes at PCs $015C/$0171 in the frame-551
 * producer_events. SIMBUF selector words are the sequential reads at
 * $01EC/$01FC/$01B1. Expected source windows are the later $04BD/$04BF/$04C0
 * reads in the same captured transaction. MAME SHA-256:
 * 75b09ee422732ebce54d4c641e3bf38ddc9de4976963844691c60c361fb46b06.
 */
#include <stdio.h>
#include <string.h>

#include "som_fixture.h"
#include "som_clamp.h"
#include "som_producer.h"
#include "source_producer_fixture.h"
#include "record_schedule.h"
#include "som_source_select.h"
#include "som_selector_fixture.h"
#include "som_source_table.h"
#include "som_transaction.h"
#include "som_walk.h"

#define DM_BASE STUNRUN_ADSP_SOM_SOURCE_TABLE_BASE
#define DM_WORDS STUNRUN_ADSP_SOM_SOURCE_TABLE_WORDS
#define USER1_BASE_WORD STUNRUN_ADSP_SOM_SELECTOR_ROM_BASE_WORD
#define USER1_WORDS STUNRUN_ADSP_SOM_SELECTOR_ROM_WORDS
#define RECORDS 9u
#define USER1 (stunrun_adsp_som_selector_fixture_rom())
#define RECORD_DM_BASE STUNRUN_ADSP_SOM_FIXTURE_DM_BASE
#define RECORD_DM_WORDS STUNRUN_ADSP_SOM_FIXTURE_DM_WORDS
#define RECORD_WORDS STUNRUN_ADSP_SOM_FIXTURE_REC_WORDS
#define RING_OFFSET (0x0040u - RECORD_DM_BASE)

/* DM($0034) as $0174 finds it: one measured caller input. The routine's own
 * origin for it is the SIMBUF read at $0172, which is upstream of this slice. */
#define SCHEDULE_ENTRY_DM0034 0x9989u

/* The nine starts the capture latched at $01A7. These are the ORACLE, compared
 * against what record_schedule derives from the object-ROM pointer chain; the
 * selection below then runs on the DERIVED values, not on these. */
#define CAPTURED_STARTS (stunrun_adsp_som_selector_fixture_simclk_starts())

static stunrun_adsp_record_schedule_t schedule;

static int failures;

#define check(condition) do { \
    if (!(condition)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        failures++; \
    } \
} while (0)

/* DM($010A..$0145) is built by $0147-$0173 from the captured DM/SIMBUF/PM
 * operands; the selector below consumes that computed table, not a copied
 * source tuple table. */
static uint16_t DM_TABLE[DM_WORDS];

/* AX0 at each captured PC $05D4, from the same-run register companion. The
 * final record's value survives to PC $004C and is the observed GINT payload. */
static const uint16_t EXPECTED_COMPLETION_AX0[RECORDS] = {
    0x0045u,0x0041u,0x0041u,0x0045u,0x0041u,
    0x0043u,0x0043u,0x0043u,0x0045u
};

static const uint16_t EXPECTED_WEIGHTS[RECORDS][STUNRUN_ADSP_SOM_WEIGHT_WORDS] = {
    {0x1927u,0x3344u,0xE323u}, {0xE6D9u,0x3344u,0xE323u},
    {0x0000u,0x3989u,0xE3F8u}, {0x3883u,0x0968u,0xE385u},
    {0xC77Du,0x0968u,0xE385u}, {0x1948u,0xCC4Au,0xE419u},
    {0xCC7Bu,0xE687u,0xE3E7u}, {0x3385u,0xE687u,0xE3E7u},
    {0xE6B8u,0xCC4Au,0xE419u},
};

static const uint16_t EXPECTED_SOURCE[RECORDS][STUNRUN_ADSP_SOM_SOURCE_WINDOW_WORDS] = {
    {0xFF4Du,0x001Cu,0x208Cu,0x0000u,0x00A6u,0x23ACu,0xFFC2u,0xFFE3u,0x208Cu},
    {0x003Eu,0xFFE3u,0x208Cu,0x0000u,0x00A6u,0x23ACu,0x00B3u,0x001Cu,0x208Cu},
    {0xFFC2u,0xFFE3u,0x208Cu,0x0000u,0x00A6u,0x23ACu,0x003Eu,0xFFE3u,0x208Cu},
    {0xFF32u,0x00BFu,0x208Cu,0x0000u,0x00A6u,0x23ACu,0xFF4Du,0x001Cu,0x208Cu},
    {0x00B3u,0x001Cu,0x208Cu,0x0000u,0x00A6u,0x23ACu,0x00CEu,0x00BFu,0x208Cu},
    {0x0000u,0x017Eu,0x208Cu,0x0000u,0x00A6u,0x23ACu,0xFF6Du,0x0136u,0x208Cu},
    {0x00CEu,0x00BFu,0x208Cu,0x0000u,0x00A6u,0x23ACu,0x0093u,0x0136u,0x208Cu},
    {0xFF6Du,0x0136u,0x208Cu,0x0000u,0x00A6u,0x23ACu,0xFF32u,0x00BFu,0x208Cu},
    {0x0000u,0x017Eu,0x208Cu,0x0093u,0x0136u,0x208Cu,0x0000u,0x00A6u,0x23ACu},
};

static int same_words(const uint16_t *a, const uint16_t *b, size_t n)
{
    return memcmp(a, b, n * sizeof(*a)) == 0;
}

static int build_dm_table(void)
{
    const stunrun_adsp_source_fixture_t *fixture =
        stunrun_adsp_source_fixture();

    if (!stunrun_adsp_som_build_source_table(DM_TABLE, DM_WORDS, DM_BASE))
        return 0;
    return same_words(&DM_TABLE[0x010du - DM_BASE],
                      fixture->expected_dm_writes,
                      STUNRUN_ADSP_SOURCE_TOTAL_WRITES);
}

/* Derive the record schedule from the object ROM, then check it against what
 * the capture latched at $01A7 before anything downstream uses it. */
static int build_schedule(void)
{
    size_t index;

    if (!stunrun_adsp_record_schedule_run(
            USER1, USER1_WORDS, USER1_BASE_WORD, 1u,
            SCHEDULE_ENTRY_DM0034, &schedule))
        return 0;
    if (schedule.record_count != RECORDS)
        return 0;
    for (index = 0; index < RECORDS; ++index)
        if (schedule.passes[index].record_start != CAPTURED_STARTS[index])
            return 0;
    return 1;
}

static void test_composed_source_selection_and_som_derivation(void)
{
    uint16_t all_record_words[RECORDS][RECORD_WORDS];
    uint16_t final_ax0 = 0u;
    int final_ax0_valid = 0;
    size_t record_index;

    for (record_index = 0; record_index < RECORDS; record_index++) {
        stunrun_adsp_som_source_selection_t selected;
        stunrun_adsp_som_span_t spans[8];
        stunrun_adsp_som_record_t record;
        uint16_t source_dm[STUNRUN_ADSP_SOM_SOURCE_WORDS];
        uint16_t pass1[STUNRUN_ADSP_SOM_RING_WORDS];
        uint16_t ring[STUNRUN_ADSP_SOM_RING_WORDS];
        uint16_t clipped_ring[STUNRUN_ADSP_SOM_RING_WORDS];
        uint16_t dm[RECORD_DM_WORDS];
        uint16_t som_words[RECORD_WORDS];

        /* The start comes from the DERIVED schedule, not from the captured
         * array; build_schedule() already compared the two. */
        check(stunrun_adsp_som_select_source_window(
            DM_TABLE, DM_WORDS, DM_BASE, USER1, USER1_WORDS,
            USER1_BASE_WORD, 1u, schedule.passes[record_index].record_start,
            &selected));
        memcpy(source_dm, selected.source_words,
               STUNRUN_ADSP_SOM_SOURCE_WINDOW_WORDS * sizeof(*source_dm));
        /* PC $04C9 reads the unused final x prefetch from DM[$0049]. The
         * captured value is zero; it does not enter either output. */
        source_dm[STUNRUN_ADSP_SOM_SOURCE_WINDOW_WORDS] = 0u;

        check(stunrun_adsp_som_produce_ring(
            source_dm, STUNRUN_ADSP_SOM_SOURCE_WORDS, 0x0040u, 0x0040u,
            USER1, USER1_WORDS, STUNRUN_ADSP_SOM_PASS2_GUARD,
            STUNRUN_ADSP_SOM_PASS2_Y_MULTIPLIER, pass1, ring));
        check(stunrun_adsp_som_clamp_ring(
            ring, STUNRUN_ADSP_SOM_RING_WORDS, clipped_ring));
        check(same_words(clipped_ring, ring, STUNRUN_ADSP_SOM_RING_WORDS));
        check(same_words(ring,
                         stunrun_adsp_som_fixture_dm((unsigned)record_index)
                             + RING_OFFSET,
                         STUNRUN_ADSP_SOM_RING_WORDS));

        /* Replace the fixture's observed ring with the computed ring before
         * running the existing $052F-$05D1 scan/walk. */
        memcpy(dm, stunrun_adsp_som_fixture_dm((unsigned)record_index),
               sizeof(dm));
        memcpy(&dm[RING_OFFSET], clipped_ring, sizeof(clipped_ring));
        check(stunrun_adsp_som_derive_record(
            dm, RECORD_DM_WORDS, RECORD_DM_BASE,
            stunrun_adsp_som_fixture_pm(), STUNRUN_ADSP_SOM_FIXTURE_PM_WORDS,
            spans, 8u, &record));
        check(record.completion_ax0_valid);
        if (record.completion_ax0 != EXPECTED_COMPLETION_AX0[record_index])
            printf("completion AX0 record=%zu got=%04X expected=%04X\n",
                   record_index + 1u, record.completion_ax0,
                   EXPECTED_COMPLETION_AX0[record_index]);
        check(record.completion_ax0 ==
              EXPECTED_COMPLETION_AX0[record_index]);
        final_ax0 = record.completion_ax0;
        final_ax0_valid = record.completion_ax0_valid;
        check(stunrun_adsp_som_emit_record(&record, som_words,
                                           RECORD_WORDS) == RECORD_WORDS);
        check(same_words(som_words,
                          stunrun_adsp_som_fixture_expected(
                              (unsigned)record_index),
                          RECORD_WORDS));
        memcpy(all_record_words[record_index], som_words, sizeof(som_words));
    }

    {
        stunrun_adsp_som_transaction_t transaction;
        size_t word;
        check(stunrun_adsp_som_transaction_build(
            &all_record_words[0][0], RECORDS * RECORD_WORDS,
            1u, 0u, final_ax0_valid, final_ax0, 0x4000u, &transaction));
        check(transaction.host_bank == 1u);
        check(transaction.producer_bank == 0u);
        check(transaction.som_start_word == 0u);
        check(transaction.som_write_count == STUNRUN_ADSP_SOM_TRANSACTION_WORDS);
        check(transaction.writes[0].address_word == 0u &&
              transaction.writes[0].value == 0u);
        check(transaction.writes[1].address_word == 1u &&
              transaction.writes[1].value == 0u);
        for (word = 0; word < RECORDS * RECORD_WORDS; ++word) {
            size_t slot = word + STUNRUN_ADSP_SOM_TRANSACTION_PROLOGUE_WORDS;
            check(transaction.writes[slot].address_word == slot);
            check(transaction.writes[slot].value ==
                  all_record_words[word / RECORD_WORDS][word % RECORD_WORDS]);
        }
        check(transaction.gint_written == 1);
        check(transaction.gint_pc == 0x004cu);
        check(transaction.gint_value == 0x0045u);
        check(transaction.gint_after_write_count ==
              STUNRUN_ADSP_SOM_TRANSACTION_WORDS);
    }
}

int main(void)
{
    stunrun_adsp_som_source_selection_t out;
    stunrun_adsp_som_source_selection_t changed;
    uint16_t altered_table[DM_WORDS];
    static uint16_t altered_user1[USER1_WORDS];
    size_t record;

    check(build_dm_table());
    check(build_schedule());
    if (failures != 0)
        return 1;

    for (record = 0; record < RECORDS; record++) {
        check(stunrun_adsp_som_select_source_window(
            DM_TABLE, DM_WORDS, DM_BASE,
            USER1, USER1_WORDS, USER1_BASE_WORD, 1u,
            schedule.passes[record].record_start, &out));
        check(same_words(out.source_words, EXPECTED_SOURCE[record],
                         STUNRUN_ADSP_SOM_SOURCE_WINDOW_WORDS));
        check(same_words(out.weight_words, EXPECTED_WEIGHTS[record],
                         STUNRUN_ADSP_SOM_WEIGHT_WORDS));
        check(out.mirrored_first_point[0] == EXPECTED_SOURCE[record][0]);
        check(out.mirrored_first_point[1] == EXPECTED_SOURCE[record][1]);
        check(out.mirrored_first_point[2] == EXPECTED_SOURCE[record][2]);
        check(out.dm0039 == 3u);
        check(out.dm08ea == 0x208Cu);
        check(out.simbuf_reads == 5u);
    }

    /* Sensitivity: changing a selector in the actual SIMBUF stream changes
     * the selected tuple; changing a DM table value changes the output too. */
    memcpy(altered_user1, USER1, sizeof(altered_user1));
    altered_user1[0x1999Fu] = 0x0003u;
    check(stunrun_adsp_som_select_source_window(
        DM_TABLE, DM_WORDS, DM_BASE,
        altered_user1, USER1_WORDS, USER1_BASE_WORD, 1u, schedule.passes[0].record_start,
        &changed));
    check(!same_words(changed.source_words, EXPECTED_SOURCE[0],
                      STUNRUN_ADSP_SOM_SOURCE_WINDOW_WORDS));

    memcpy(altered_table, DM_TABLE, sizeof(altered_table));
    altered_table[0x10Du - DM_BASE] = 0u;
    check(stunrun_adsp_som_select_source_window(
        altered_table, DM_WORDS, DM_BASE,
        USER1, USER1_WORDS, USER1_BASE_WORD, 1u, schedule.passes[0].record_start, &changed));
    check(changed.source_words[0] == 0u);

    /* Unsupported branch/count shapes and truncated object/DM windows reject
     * rather than return plausible-looking source words. */
    check(!stunrun_adsp_som_select_source_window(
        DM_TABLE, DM_WORDS, DM_BASE,
        USER1, USER1_WORDS, USER1_BASE_WORD, 0u, schedule.passes[0].record_start, &changed));
    check(!stunrun_adsp_som_select_source_window(
        DM_TABLE, DM_WORDS, DM_BASE,
        USER1, 0x199A1u, USER1_BASE_WORD, 1u, schedule.passes[0].record_start, &changed));
    check(!stunrun_adsp_som_select_source_window(
        DM_TABLE, 8u, DM_BASE,
        USER1, USER1_WORDS, USER1_BASE_WORD, 1u, schedule.passes[0].record_start, &changed));
    check(!stunrun_adsp_som_select_source_window(
        DM_TABLE, DM_WORDS, DM_BASE,
        USER1, USER1_WORDS, USER1_BASE_WORD, 1u, schedule.passes[0].record_start, NULL));

    test_composed_source_selection_and_som_derivation();

    if (failures == 0)
        puts("PASS");
    return failures == 0 ? 0 : 1;
}
