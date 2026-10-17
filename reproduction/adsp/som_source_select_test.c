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
#include "som_source_select.h"
#include "som_walk.h"

#define DM_BASE 0x010au
#define DM_WORDS 60u
#define USER1_BASE_WORD 0u
#define USER1_WORDS 0x199cau
#define RECORDS 9u
#define RECORD_DM_BASE STUNRUN_ADSP_SOM_FIXTURE_DM_BASE
#define RECORD_DM_WORDS STUNRUN_ADSP_SOM_FIXTURE_DM_WORDS
#define RECORD_WORDS STUNRUN_ADSP_SOM_FIXTURE_REC_WORDS
#define RING_OFFSET (0x0040u - RECORD_DM_BASE)

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

/* Sparse synthetic user1 word image: only the observed MP=1 SIMBUF stream
 * and the two observed $04B2 coefficient words are populated. */
static const uint16_t USER1[USER1_WORDS] = {
    [0x1208Cu] = 0x07DEu,
    [0x123ACu] = 0x072Du,
    [0x1999Du] = 0x000Bu,[0x1999Eu] = 0x0001u,
    [0x1999Fu] = 0x0002u,[0x199A0u] = 0x0003u,[0x199A1u] = 0xFFFFu,
    [0x199A2u] = 0x000Cu,[0x199A3u] = 0x0004u,
    [0x199A4u] = 0x0002u,[0x199A5u] = 0x0005u,[0x199A6u] = 0xFFFFu,
    [0x199A7u] = 0x000Du,[0x199A8u] = 0x0003u,
    [0x199A9u] = 0x0002u,[0x199AAu] = 0x0004u,[0x199ABu] = 0xFFFFu,
    [0x199ACu] = 0x000Eu,[0x199ADu] = 0x0006u,
    [0x199AEu] = 0x0002u,[0x199AFu] = 0x0001u,[0x199B0u] = 0xFFFFu,
    [0x199B1u] = 0x000Fu,[0x199B2u] = 0x0005u,
    [0x199B3u] = 0x0002u,[0x199B4u] = 0x0007u,[0x199B5u] = 0xFFFFu,
    [0x199B6u] = 0x0010u,[0x199B7u] = 0x0008u,
    [0x199B8u] = 0x0002u,[0x199B9u] = 0x0009u,[0x199BAu] = 0xFFFFu,
    [0x199BBu] = 0x0011u,[0x199BCu] = 0x0007u,
    [0x199BDu] = 0x0002u,[0x199BEu] = 0x000Au,[0x199BFu] = 0xFFFFu,
    [0x199C0u] = 0x0012u,[0x199C1u] = 0x0009u,
    [0x199C2u] = 0x0002u,[0x199C3u] = 0x0006u,[0x199C4u] = 0xFFFFu,
    [0x199C5u] = 0x0013u,[0x199C6u] = 0x0008u,
    [0x199C7u] = 0x000Au,[0x199C8u] = 0x0002u,[0x199C9u] = 0xFFFFu,
};

static const uint16_t SIMCLK_START[RECORDS] = {
    0x999Du,0x99A2u,0x99A7u,0x99ACu,0x99B1u,
    0x99B6u,0x99BBu,0x99C0u,0x99C5u,
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
    uint32_t first_pm[STUNRUN_ADSP_SOURCE_FIRST_PM_READS];
    uint32_t second_pm[STUNRUN_ADSP_SOURCE_SECOND_PM_READS];
    uint16_t writes[STUNRUN_ADSP_SOURCE_TOTAL_WRITES];

    if (!stunrun_adsp_source_fixture_fill_pm(first_pm, second_pm) ||
        !stunrun_adsp_source_produce(
            fixture->simbuf_words, STUNRUN_ADSP_SOURCE_SIMBUF_READS, -2,
            first_pm, STUNRUN_ADSP_SOURCE_FIRST_PM_READS,
            fixture->first_aux_words, STUNRUN_ADSP_SOURCE_FIRST_WRITES,
            second_pm, STUNRUN_ADSP_SOURCE_SECOND_PM_READS, writes) ||
        !same_words(writes, fixture->expected_dm_writes,
                    STUNRUN_ADSP_SOURCE_TOTAL_WRITES))
        return 0;

    memset(DM_TABLE, 0, sizeof(DM_TABLE));
    memcpy(&DM_TABLE[0x010du - DM_BASE], writes, sizeof(writes));
    return 1;
}

static void test_composed_source_selection_and_som_derivation(void)
{
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

        check(stunrun_adsp_som_select_source_window(
            DM_TABLE, DM_WORDS, DM_BASE, USER1, USER1_WORDS,
            USER1_BASE_WORD, 1u, SIMCLK_START[record_index], &selected));
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
        check(stunrun_adsp_som_emit_record(&record, som_words,
                                           RECORD_WORDS) == RECORD_WORDS);
        check(same_words(som_words,
                         stunrun_adsp_som_fixture_expected(
                             (unsigned)record_index),
                         RECORD_WORDS));
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
    if (failures != 0)
        return 1;

    for (record = 0; record < RECORDS; record++) {
        check(stunrun_adsp_som_select_source_window(
            DM_TABLE, DM_WORDS, DM_BASE,
            USER1, USER1_WORDS, USER1_BASE_WORD, 1u, SIMCLK_START[record],
            &out));
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
        altered_user1, USER1_WORDS, USER1_BASE_WORD, 1u, SIMCLK_START[0],
        &changed));
    check(!same_words(changed.source_words, EXPECTED_SOURCE[0],
                      STUNRUN_ADSP_SOM_SOURCE_WINDOW_WORDS));

    memcpy(altered_table, DM_TABLE, sizeof(altered_table));
    altered_table[0x10Du - DM_BASE] = 0u;
    check(stunrun_adsp_som_select_source_window(
        altered_table, DM_WORDS, DM_BASE,
        USER1, USER1_WORDS, USER1_BASE_WORD, 1u, SIMCLK_START[0], &changed));
    check(changed.source_words[0] == 0u);

    /* Unsupported branch/count shapes and truncated object/DM windows reject
     * rather than return plausible-looking source words. */
    check(!stunrun_adsp_som_select_source_window(
        DM_TABLE, DM_WORDS, DM_BASE,
        USER1, USER1_WORDS, USER1_BASE_WORD, 0u, SIMCLK_START[0], &changed));
    check(!stunrun_adsp_som_select_source_window(
        DM_TABLE, DM_WORDS, DM_BASE,
        USER1, 0x199A1u, USER1_BASE_WORD, 1u, SIMCLK_START[0], &changed));
    check(!stunrun_adsp_som_select_source_window(
        DM_TABLE, 8u, DM_BASE,
        USER1, USER1_WORDS, USER1_BASE_WORD, 1u, SIMCLK_START[0], &changed));
    check(!stunrun_adsp_som_select_source_window(
        DM_TABLE, DM_WORDS, DM_BASE,
        USER1, USER1_WORDS, USER1_BASE_WORD, 1u, SIMCLK_START[0], NULL));

    test_composed_source_selection_and_som_derivation();

    if (failures == 0)
        puts("PASS");
    return failures == 0 ? 0 : 1;
}
