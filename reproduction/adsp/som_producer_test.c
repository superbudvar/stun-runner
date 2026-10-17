/* Oracle-backed vectors for ADSP PC $04B2-$04DF.
 *
 * Source DM words and SIMBUF addresses are transcribed from the same-run
 * producer_events in reference/experiments/stunrun/adsp-som-transaction-f551/
 * (frame 551, input=none, power_on, stock-behaviour MAME SHA-256
 * 75b09ee422732ebce54d4c641e3bf38ddc9de4976963844691c60c361fb46b06).
 * Expected pass-1 values are the DM writes at $04C8/$04CA tabulated in
 * docs/mc68010-boot-findings.md §72.2. Expected final rings come from the
 * committed per-record DM windows in som_fixture.c. The test then composes
 * those derived rings with the already-tested pointer scan/walk and checks the
 * complete SOM record stream.
 */
#include <stdio.h>
#include <string.h>

#include "som_fixture.h"
#include "som_producer.h"
#include "som_walk.h"

#define RECORDS STUNRUN_ADSP_SOM_FIXTURE_RECORDS
#define RECORD_WORDS STUNRUN_ADSP_SOM_FIXTURE_REC_WORDS
#define DM_BASE STUNRUN_ADSP_SOM_FIXTURE_DM_BASE
#define DM_WORDS STUNRUN_ADSP_SOM_FIXTURE_DM_WORDS
#define RING_OFFSET (0x0040u - DM_BASE)
#define USER1_WORDS 0x123adu

static int failures;

#define check(condition) do { \
    if (!(condition)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        failures++; \
    } \
} while (0)

/* Ten sequential DM reads per record: x0, y0, SIMCLK0, x1, y1, SIMCLK1,
 * x2, y2, SIMCLK2, and the final x prefetch at $04C9. These values are inputs,
 * not SOM outputs. */
static const uint16_t SOURCE_DM[RECORDS][STUNRUN_ADSP_SOM_SOURCE_WORDS] = {
    {0xFF4Du,0x001Cu,0x208Cu,0x0000u,0x00A6u,0x23ACu,0xFFC2u,0xFFE3u,0x208Cu,0x0000u},
    {0x003Eu,0xFFE3u,0x208Cu,0x0000u,0x00A6u,0x23ACu,0x00B3u,0x001Cu,0x208Cu,0x0000u},
    {0xFFC2u,0xFFE3u,0x208Cu,0x0000u,0x00A6u,0x23ACu,0x003Eu,0xFFE3u,0x208Cu,0x0000u},
    {0xFF32u,0x00BFu,0x208Cu,0x0000u,0x00A6u,0x23ACu,0xFF4Du,0x001Cu,0x208Cu,0x0000u},
    {0x00B3u,0x001Cu,0x208Cu,0x0000u,0x00A6u,0x23ACu,0x00CEu,0x00BFu,0x208Cu,0x0000u},
    {0x0000u,0x017Eu,0x208Cu,0x0000u,0x00A6u,0x23ACu,0xFF6Du,0x0136u,0x208Cu,0x0000u},
    {0x00CEu,0x00BFu,0x208Cu,0x0000u,0x00A6u,0x23ACu,0x0093u,0x0136u,0x208Cu,0x0000u},
    {0xFF6Du,0x0136u,0x208Cu,0x0000u,0x00A6u,0x23ACu,0xFF32u,0x00BFu,0x208Cu,0x0000u},
    {0x0000u,0x017Eu,0x208Cu,0x0093u,0x0136u,0x208Cu,0x0000u,0x00A6u,0x23ACu,0x0000u},
};

static const uint16_t EXPECTED_PASS1[RECORDS][STUNRUN_ADSP_SOM_RING_WORDS] = {
    {0x00F4u,0x00FDu,0x00FFu,0x00F6u,0x00FBu,0x0101u},
    {0x0103u,0x0101u,0x00FFu,0x00F6u,0x010Au,0x00FDu},
    {0x00FBu,0x0101u,0x00FFu,0x00F6u,0x0103u,0x0101u},
    {0x00F2u,0x00F3u,0x00FFu,0x00F6u,0x00F4u,0x00FDu},
    {0x010Au,0x00FDu,0x00FFu,0x00F6u,0x010Cu,0x00F3u},
    {0x00FFu,0x00E8u,0x00FFu,0x00F6u,0x00F6u,0x00ECu},
    {0x010Cu,0x00F3u,0x00FFu,0x00F6u,0x0108u,0x00ECu},
    {0x00F6u,0x00ECu,0x00FFu,0x00F6u,0x00F2u,0x00F3u},
    {0x00FFu,0x00E8u,0x0108u,0x00ECu,0x00FFu,0x00F6u},
};

/* Sparse synthetic user1 word image. The only populated locations are the
 * two observed SIMBUF reads: MP=1 makes their full m_sim_memory word addresses
 * 0x10000+0x208C and 0x10000+0x23AC. */
static const uint16_t USER1[USER1_WORDS] = {
    [0x1208Cu] = 0x07DEu,
    [0x123ACu] = 0x072Du,
};

static int arrays_equal(const uint16_t *left, const uint16_t *right, size_t n)
{
    return memcmp(left, right, n * sizeof(*left)) == 0;
}

static void test_captured_records(void)
{
    size_t record_index;

    for (record_index = 0; record_index < RECORDS; record_index++) {
        uint16_t pass1[STUNRUN_ADSP_SOM_RING_WORDS];
        uint16_t ring[STUNRUN_ADSP_SOM_RING_WORDS];
        uint16_t dm[DM_WORDS];
        uint16_t som_words[RECORD_WORDS];
        stunrun_adsp_som_span_t spans[8];
        stunrun_adsp_som_record_t record;

        check(stunrun_adsp_som_produce_ring(
            SOURCE_DM[record_index], STUNRUN_ADSP_SOM_SOURCE_WORDS,
            0x0040u, 0x0040u, USER1, USER1_WORDS,
            STUNRUN_ADSP_SOM_PASS2_GUARD,
            STUNRUN_ADSP_SOM_PASS2_Y_MULTIPLIER, pass1, ring));
        check(arrays_equal(pass1, EXPECTED_PASS1[record_index],
                           STUNRUN_ADSP_SOM_RING_WORDS));
        check(arrays_equal(ring,
                           stunrun_adsp_som_fixture_dm((unsigned)record_index)
                               + RING_OFFSET,
                           STUNRUN_ADSP_SOM_RING_WORDS));

        /* Compose the derived ring, not the captured ring, with the existing
         * $052F-$05D1 walk. Preserve only the other measured DM inputs. */
        memcpy(dm, stunrun_adsp_som_fixture_dm((unsigned)record_index),
               sizeof(dm));
        memcpy(&dm[RING_OFFSET], ring, sizeof(ring));
        check(stunrun_adsp_som_derive_record(
            dm, DM_WORDS, DM_BASE, stunrun_adsp_som_fixture_pm(),
            STUNRUN_ADSP_SOM_FIXTURE_PM_WORDS, spans, 8u, &record));
        check(stunrun_adsp_som_emit_record(&record, som_words,
                                           RECORD_WORDS) == RECORD_WORDS);
        check(arrays_equal(som_words,
                           stunrun_adsp_som_fixture_expected(
                               (unsigned)record_index),
                           RECORD_WORDS));
    }
}

static void test_object_rom_and_branch_controls(void)
{
    uint16_t pass1[STUNRUN_ADSP_SOM_RING_WORDS];
    uint16_t ring[STUNRUN_ADSP_SOM_RING_WORDS];
    uint16_t changed_source[STUNRUN_ADSP_SOM_SOURCE_WORDS];
    uint16_t changed_rom[USER1_WORDS];
    uint16_t pass1_changed[STUNRUN_ADSP_SOM_RING_WORDS];
    uint16_t ring_changed[STUNRUN_ADSP_SOM_RING_WORDS];
    uint16_t pass1_only[STUNRUN_ADSP_SOM_RING_WORDS];
    uint16_t bypassed[STUNRUN_ADSP_SOM_RING_WORDS];
    size_t i;

    check(stunrun_adsp_som_produce_ring(
        SOURCE_DM[0], STUNRUN_ADSP_SOM_SOURCE_WORDS, 0x0040u, 0x0040u,
        USER1, USER1_WORDS, STUNRUN_ADSP_SOM_PASS2_GUARD,
        STUNRUN_ADSP_SOM_PASS2_Y_MULTIPLIER, pass1, ring));

    /* Negative control: the result must depend on the returned SIMBUF word,
     * not merely on a record index or a copied captured ring. */
    memcpy(changed_rom, USER1, sizeof(changed_rom));
    changed_rom[0x1208Cu] = 0u;
    check(stunrun_adsp_som_produce_ring(
        SOURCE_DM[0], STUNRUN_ADSP_SOM_SOURCE_WORDS, 0x0040u, 0x0040u,
        changed_rom, USER1_WORDS, STUNRUN_ADSP_SOM_PASS2_GUARD,
        STUNRUN_ADSP_SOM_PASS2_Y_MULTIPLIER, pass1_changed, ring_changed));
    check(!arrays_equal(pass1_changed, EXPECTED_PASS1[0],
                        STUNRUN_ADSP_SOM_RING_WORDS));

    /* Negative control: changing a source sample must affect the derived
     * stream; the pass must not be a lookup by record number. */
    memcpy(changed_source, SOURCE_DM[0], sizeof(changed_source));
    changed_source[0] = 0u;
    check(stunrun_adsp_som_produce_ring(
        changed_source, STUNRUN_ADSP_SOM_SOURCE_WORDS, 0x0040u, 0x0040u,
        USER1, USER1_WORDS, STUNRUN_ADSP_SOM_PASS2_GUARD,
        STUNRUN_ADSP_SOM_PASS2_Y_MULTIPLIER, pass1_changed, ring_changed));
    check(!arrays_equal(pass1_changed, EXPECTED_PASS1[0],
                        STUNRUN_ADSP_SOM_RING_WORDS));

    /* The zero-guard branch is static-disassembly coverage only; it was not
     * taken in the captured transaction. */
    check(stunrun_adsp_som_produce_ring(
        SOURCE_DM[0], STUNRUN_ADSP_SOM_SOURCE_WORDS, 0x0040u, 0x0040u,
        USER1, USER1_WORDS, 0u, STUNRUN_ADSP_SOM_PASS2_Y_MULTIPLIER,
        pass1_only, bypassed));
    check(arrays_equal(pass1_only, EXPECTED_PASS1[0],
                       STUNRUN_ADSP_SOM_RING_WORDS));
    check(arrays_equal(bypassed, pass1_only, STUNRUN_ADSP_SOM_RING_WORDS));

    /* Unsupported/truncated cases fail without publishing partial outputs. */
    for (i = 0; i < STUNRUN_ADSP_SOM_RING_WORDS; i++)
        pass1_changed[i] = ring_changed[i] = 0xA55Au;
    check(!stunrun_adsp_som_produce_ring(
        SOURCE_DM[0], STUNRUN_ADSP_SOM_SOURCE_WORDS - 1u,
        0x0040u, 0x0040u, USER1, USER1_WORDS,
        STUNRUN_ADSP_SOM_PASS2_GUARD,
        STUNRUN_ADSP_SOM_PASS2_Y_MULTIPLIER, pass1_changed, ring_changed));
    check(!stunrun_adsp_som_produce_ring(
        SOURCE_DM[0], STUNRUN_ADSP_SOM_SOURCE_WORDS,
        0x003Fu, 0x003Fu, USER1, USER1_WORDS,
        STUNRUN_ADSP_SOM_PASS2_GUARD,
        STUNRUN_ADSP_SOM_PASS2_Y_MULTIPLIER, pass1_changed, ring_changed));
    check(!stunrun_adsp_som_produce_ring(
        SOURCE_DM[0], STUNRUN_ADSP_SOM_SOURCE_WORDS,
        0x0040u, 0x0040u, USER1, 0x1208Cu,
        STUNRUN_ADSP_SOM_PASS2_GUARD,
        STUNRUN_ADSP_SOM_PASS2_Y_MULTIPLIER, pass1_changed, ring_changed));
    check(!stunrun_adsp_som_produce_ring(
        SOURCE_DM[0], STUNRUN_ADSP_SOM_SOURCE_WORDS,
        0x0040u, 0x0040u, USER1, USER1_WORDS,
        0x0201u, STUNRUN_ADSP_SOM_PASS2_Y_MULTIPLIER,
        pass1_changed, ring_changed));
    check(!stunrun_adsp_som_produce_ring(
        SOURCE_DM[0], STUNRUN_ADSP_SOM_SOURCE_WORDS,
        0x0040u, 0x0040u, USER1, USER1_WORDS,
        STUNRUN_ADSP_SOM_PASS2_GUARD, 0x013Fu,
        pass1_changed, ring_changed));
    check(!stunrun_adsp_som_produce_ring(
        SOURCE_DM[0], STUNRUN_ADSP_SOM_SOURCE_WORDS,
        0x0040u, 0x0040u, USER1, USER1_WORDS,
        STUNRUN_ADSP_SOM_PASS2_GUARD,
        STUNRUN_ADSP_SOM_PASS2_Y_MULTIPLIER, NULL, ring_changed));
    for (i = 0; i < STUNRUN_ADSP_SOM_RING_WORDS; i++)
        check(pass1_changed[i] == 0xA55Au && ring_changed[i] == 0xA55Au);

    /* $04C3 takes an unimplemented sibling for SIMCLK <= $0200. Reject it. */
    memcpy(changed_source, SOURCE_DM[0], sizeof(changed_source));
    changed_source[2] = 0x0200u;
    check(!stunrun_adsp_som_produce_ring(
        changed_source, STUNRUN_ADSP_SOM_SOURCE_WORDS, 0x0040u, 0x0040u,
        USER1, USER1_WORDS, STUNRUN_ADSP_SOM_PASS2_GUARD,
        STUNRUN_ADSP_SOM_PASS2_Y_MULTIPLIER, pass1_changed, ring_changed));
}

int main(void)
{
    test_captured_records();
    test_object_rom_and_branch_controls();
    if (failures == 0)
        puts("PASS");
    return failures == 0 ? 0 : 1;
}
