/* Oracle vectors from the same-run frame-551 ADSP DM and PM event streams. */
#include "source_producer.h"
#include "source_producer_fixture.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define check(condition) do { \
    if (!(condition)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        failures++; \
    } \
} while (0)

static int same_words(const uint16_t *a, const uint16_t *b, size_t n)
{
    return memcmp(a, b, n * sizeof(*a)) == 0;
}

static int32_t signed16(uint16_t value)
{
    return (value & 0x8000u) != 0u
        ? (int32_t)value - 0x10000
        : (int32_t)value;
}

static uint16_t shift_right_two(uint16_t value)
{
    int32_t sample = signed16(value);
    int32_t shifted = sample < 0 ? -((-sample + 3) / 4) : sample / 4;
    return (uint16_t)shifted;
}

static void test_simbuf_staging(void)
{
    const stunrun_adsp_source_fixture_t *fixture =
        stunrun_adsp_source_fixture();
    uint16_t derived[STUNRUN_ADSP_SOURCE_FIRST_WRITES];
    size_t i;
    for (i = 0; i < STUNRUN_ADSP_SOURCE_FIRST_WRITES; ++i)
        derived[i] = shift_right_two(fixture->simbuf_words[i]);
    check(same_words(derived, fixture->staged_dm_words,
                     STUNRUN_ADSP_SOURCE_FIRST_WRITES));
}

static void test_captured_writes(void)
{
    const stunrun_adsp_source_fixture_t *fixture =
        stunrun_adsp_source_fixture();
    uint32_t first_pm[STUNRUN_ADSP_SOURCE_FIRST_PM_READS];
    uint32_t second_pm[STUNRUN_ADSP_SOURCE_SECOND_PM_READS];
    uint16_t output[STUNRUN_ADSP_SOURCE_TOTAL_WRITES];
    check(stunrun_adsp_source_fixture_fill_pm(first_pm, second_pm));
    check(stunrun_adsp_source_produce(
        fixture->simbuf_words, STUNRUN_ADSP_SOURCE_SIMBUF_READS, -2,
        first_pm, STUNRUN_ADSP_SOURCE_FIRST_PM_READS,
        fixture->first_aux_words, STUNRUN_ADSP_SOURCE_FIRST_WRITES,
        second_pm, STUNRUN_ADSP_SOURCE_SECOND_PM_READS, output));
    check(same_words(output, fixture->expected_dm_writes,
                     STUNRUN_ADSP_SOURCE_TOTAL_WRITES));
}

static void test_negative_controls_and_bounds(void)
{
    const stunrun_adsp_source_fixture_t *fixture =
        stunrun_adsp_source_fixture();
    uint32_t first_pm[STUNRUN_ADSP_SOURCE_FIRST_PM_READS];
    uint32_t second_pm[STUNRUN_ADSP_SOURCE_SECOND_PM_READS];
    uint16_t changed_simbuf[STUNRUN_ADSP_SOURCE_SIMBUF_READS];
    uint32_t changed_pm[STUNRUN_ADSP_SOURCE_FIRST_PM_READS];
    uint16_t output[STUNRUN_ADSP_SOURCE_TOTAL_WRITES];
    uint16_t changed[STUNRUN_ADSP_SOURCE_TOTAL_WRITES];
    size_t i;

    check(stunrun_adsp_source_fixture_fill_pm(first_pm, second_pm));
    check(stunrun_adsp_source_produce(
        fixture->simbuf_words, STUNRUN_ADSP_SOURCE_SIMBUF_READS, -2,
        first_pm, STUNRUN_ADSP_SOURCE_FIRST_PM_READS,
        fixture->first_aux_words, STUNRUN_ADSP_SOURCE_FIRST_WRITES,
        second_pm, STUNRUN_ADSP_SOURCE_SECOND_PM_READS, output));

    memcpy(changed_simbuf, fixture->simbuf_words, sizeof(changed_simbuf));
    changed_simbuf[0] ^= 4u;
    check(stunrun_adsp_source_produce(
        changed_simbuf, STUNRUN_ADSP_SOURCE_SIMBUF_READS, -2,
        first_pm, STUNRUN_ADSP_SOURCE_FIRST_PM_READS,
        fixture->first_aux_words, STUNRUN_ADSP_SOURCE_FIRST_WRITES,
        second_pm, STUNRUN_ADSP_SOURCE_SECOND_PM_READS, changed));
    check(!same_words(changed, fixture->expected_dm_writes,
                      STUNRUN_ADSP_SOURCE_TOTAL_WRITES));

    memcpy(changed_pm, first_pm, sizeof(first_pm));
    changed_pm[0] = 0;
    check(stunrun_adsp_source_produce(
        fixture->simbuf_words, STUNRUN_ADSP_SOURCE_SIMBUF_READS, -2,
        changed_pm, STUNRUN_ADSP_SOURCE_FIRST_PM_READS,
        fixture->first_aux_words, STUNRUN_ADSP_SOURCE_FIRST_WRITES,
        second_pm, STUNRUN_ADSP_SOURCE_SECOND_PM_READS, changed));
    check(!same_words(changed, fixture->expected_dm_writes,
                      STUNRUN_ADSP_SOURCE_TOTAL_WRITES));

    for (i = 0; i < STUNRUN_ADSP_SOURCE_TOTAL_WRITES; ++i)
        changed[i] = 0xa55a;
    check(!stunrun_adsp_source_produce(
        fixture->simbuf_words, STUNRUN_ADSP_SOURCE_SIMBUF_READS - 1u, -2,
        first_pm, STUNRUN_ADSP_SOURCE_FIRST_PM_READS,
        fixture->first_aux_words, STUNRUN_ADSP_SOURCE_FIRST_WRITES,
        second_pm, STUNRUN_ADSP_SOURCE_SECOND_PM_READS, changed));
    check(!stunrun_adsp_source_produce(
        fixture->simbuf_words, STUNRUN_ADSP_SOURCE_SIMBUF_READS, -1,
        first_pm, STUNRUN_ADSP_SOURCE_FIRST_PM_READS,
        fixture->first_aux_words, STUNRUN_ADSP_SOURCE_FIRST_WRITES,
        second_pm, STUNRUN_ADSP_SOURCE_SECOND_PM_READS, changed));
    check(!stunrun_adsp_source_produce(
        fixture->simbuf_words, STUNRUN_ADSP_SOURCE_SIMBUF_READS, -2,
        first_pm, STUNRUN_ADSP_SOURCE_FIRST_PM_READS - 1u,
        fixture->first_aux_words, STUNRUN_ADSP_SOURCE_FIRST_WRITES,
        second_pm, STUNRUN_ADSP_SOURCE_SECOND_PM_READS, changed));
    for (i = 0; i < STUNRUN_ADSP_SOURCE_TOTAL_WRITES; ++i)
        check(changed[i] == 0xa55a);
}

int main(void)
{
    test_simbuf_staging();
    test_captured_writes();
    test_negative_controls_and_bounds();
    if (failures == 0)
        puts("PASS");
    return failures == 0 ? 0 : 1;
}
