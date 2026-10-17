/* The captured frame-551 rings are all in range; the second vector is
 * synthetic source-level coverage for the disassembled saturation branches. */
#include "som_clamp.h"
#include "som_fixture.h"

#include <stdio.h>
#include <string.h>

#define RECORDS STUNRUN_ADSP_SOM_FIXTURE_RECORDS
#define DM_BASE STUNRUN_ADSP_SOM_FIXTURE_DM_BASE
#define DM_WORDS STUNRUN_ADSP_SOM_FIXTURE_DM_WORDS
#define RING_OFFSET (0x0040u - DM_BASE)

static int failures;

#define check(condition) do { \
    if (!(condition)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        failures++; \
    } \
} while (0)

static int same_words(const uint16_t *a, const uint16_t *b, size_t count)
{
    return memcmp(a, b, count * sizeof(*a)) == 0;
}

static void test_captured_rings_are_unchanged(void)
{
    size_t record;
    for (record = 0; record < RECORDS; ++record) {
        const uint16_t *dm = stunrun_adsp_som_fixture_dm((unsigned)record);
        uint16_t clipped[STUNRUN_ADSP_SOM_CLAMP_WORDS];
        check(stunrun_adsp_som_clamp_ring(
            dm + RING_OFFSET, STUNRUN_ADSP_SOM_CLAMP_WORDS, clipped));
        check(same_words(clipped, dm + RING_OFFSET,
                         STUNRUN_ADSP_SOM_CLAMP_WORDS));
    }
}

static void test_synthetic_saturation_boundaries(void)
{
    static const uint16_t input[STUNRUN_ADSP_SOM_CLAMP_WORDS] = {
        0xffffu, 0x0000u, 0x01ffu, 0x0200u, 0x7fffu, 0x8000u
    };
    static const uint16_t expected[STUNRUN_ADSP_SOM_CLAMP_WORDS] = {
        0x0000u, 0x0000u, 0x01ffu, 0x01ffu, 0x01ffu, 0x0000u
    };
    uint16_t output[STUNRUN_ADSP_SOM_CLAMP_WORDS];

    check(stunrun_adsp_som_clamp_ring(input, STUNRUN_ADSP_SOM_CLAMP_WORDS,
                                      output));
    check(same_words(output, expected, STUNRUN_ADSP_SOM_CLAMP_WORDS));
}

static void test_rejects_truncation_without_partial_output(void)
{
    static const uint16_t input[STUNRUN_ADSP_SOM_CLAMP_WORDS] = {0};
    uint16_t output[STUNRUN_ADSP_SOM_CLAMP_WORDS];
    size_t i;

    for (i = 0; i < STUNRUN_ADSP_SOM_CLAMP_WORDS; ++i)
        output[i] = 0xa55au;
    check(!stunrun_adsp_som_clamp_ring(
        input, STUNRUN_ADSP_SOM_CLAMP_WORDS - 1u, output));
    check(!stunrun_adsp_som_clamp_ring(NULL,
        STUNRUN_ADSP_SOM_CLAMP_WORDS, output));
    for (i = 0; i < STUNRUN_ADSP_SOM_CLAMP_WORDS; ++i)
        check(output[i] == 0xa55au);
}

int main(void)
{
    test_captured_rings_are_unchanged();
    test_synthetic_saturation_boundaries();
    test_rejects_truncation_without_partial_output();
    if (failures == 0)
        puts("PASS");
    return failures == 0 ? 0 : 1;
}
