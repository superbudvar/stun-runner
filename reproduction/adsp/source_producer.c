#include "source_producer.h"

#include <string.h>

static int32_t signed16(uint16_t value)
{
    return (value & 0x8000u) != 0u
        ? (int32_t)value - 0x10000
        : (int32_t)value;
}

static int pm_operands_valid(const uint32_t *pm, size_t count)
{
    size_t i;

    if (pm == NULL)
        return 0;
    for (i = 0; i < count; ++i)
        if ((pm[i] & 0xff000000u) != 0u)
            return 0;
    return 1;
}

/* $014F-$0154 with the captured SE=-2. ADSP ASHIFT of a signed low word is
 * floor division by four; spell it out to avoid implementation-defined
 * negative right-shift behavior in portable C. */
static uint16_t captured_source_shift(uint16_t value)
{
    int32_t signed_value = signed16(value);
    int32_t shifted = signed_value < 0
        ? -((-signed_value + 3) / 4)
        : signed_value / 4;
    return (uint16_t)shifted;
}

/* MAME adsp21xx_device::mac_op_mr: fractional mode shifts the signed product
 * left one. The first two instructions are SS; the third is RND. */
static int64_t first_loop_mr(const uint16_t dm[3], const uint32_t pm[3])
{
    int64_t term[STUNRUN_ADSP_SOURCE_DOT_TERMS];
    int64_t mr;
    uint32_t round_low;
    size_t i;

    for (i = 0; i < STUNRUN_ADSP_SOURCE_DOT_TERMS; ++i) {
        uint16_t my0 = (uint16_t)((pm[i] >> 8) & 0xffffu);
        term[i] = (int64_t)signed16(dm[i]) * signed16(my0) * 2;
    }

    mr = term[0];
    mr += term[1];
    mr += term[2];
    round_low = (uint32_t)term[2] & 0xffffu;
    mr += 0x8000;
    /* MAME's ADSP-2100 RND tie case suppresses the increment at exactly
     * 0x8000 in the product's low word. */
    if (round_low == 0x8000u)
        mr &= ~INT64_C(0x10000);
    return mr;
}

static uint16_t first_loop_output(const uint16_t dm[3], const uint32_t pm[3],
                                  uint16_t aux)
{
    int64_t mr = first_loop_mr(dm, pm);
    uint16_t mr1 = (uint16_t)(((uint64_t)mr >> 16) & 0xffffu);
    return (uint16_t)(mr1 + aux);
}

/* $016D-$0171: MR0+0x4000 sets C; in the parallel $016E assignment SI receives
 * the old AR (the rounded low half), while AR receives MR1+C. The two
 * immediate shifts then combine AR<<1 with SI[15]. */
static uint16_t second_loop_output(const uint16_t dm[3], const uint32_t pm[3])
{
    int64_t mr = 0;
    uint16_t mr0;
    uint16_t mr1;
    uint32_t low_sum;
    uint16_t si;
    uint16_t ar;
    size_t i;

    for (i = 0; i < STUNRUN_ADSP_SOURCE_DOT_TERMS; ++i) {
        uint16_t my0 = (uint16_t)((pm[i] >> 8) & 0xffffu);
        int64_t product = (int64_t)signed16(dm[i]) * signed16(my0) * 2;
        mr += product;
    }
    mr0 = (uint16_t)((uint64_t)mr & 0xffffu);
    mr1 = (uint16_t)(((uint64_t)mr >> 16) & 0xffffu);
    low_sum = (uint32_t)mr0 + 0x4000u;
    si = (uint16_t)low_sum;
    ar = (uint16_t)(mr1 + (uint16_t)(low_sum >> 16));
    return (uint16_t)((uint16_t)(ar << 1) | (si >> 15));
}

int stunrun_adsp_source_produce(
    const uint16_t *simbuf_words, size_t simbuf_count, int8_t source_shift,
    const uint32_t *first_pm, size_t first_pm_count,
    const uint16_t *first_aux, size_t first_aux_count,
    const uint32_t *second_pm, size_t second_pm_count,
    uint16_t output[STUNRUN_ADSP_SOURCE_TOTAL_WRITES])
{
    uint16_t result[STUNRUN_ADSP_SOURCE_TOTAL_WRITES];
    uint16_t first_dm[STUNRUN_ADSP_SOURCE_FIRST_WRITES];
    size_t i;

    if (simbuf_words == NULL ||
        simbuf_count != STUNRUN_ADSP_SOURCE_SIMBUF_READS ||
        source_shift != -2 ||
        first_pm_count != STUNRUN_ADSP_SOURCE_FIRST_PM_READS ||
        second_pm_count != STUNRUN_ADSP_SOURCE_SECOND_PM_READS ||
        first_aux_count != STUNRUN_ADSP_SOURCE_FIRST_WRITES ||
        first_aux == NULL || output == NULL ||
        !pm_operands_valid(first_pm, first_pm_count) ||
        !pm_operands_valid(second_pm, second_pm_count))
        return 0;

    for (i = 0; i < STUNRUN_ADSP_SOURCE_FIRST_WRITES; ++i)
        first_dm[i] = captured_source_shift(simbuf_words[i]);

    for (i = 0; i < STUNRUN_ADSP_SOURCE_FIRST_WRITES; ++i) {
        const uint16_t *dm = &first_dm[(i / 3u) * 3u];
        const uint32_t *pm = &first_pm[i * 3u];
        result[i] = first_loop_output(dm, pm, first_aux[i]);
    }
    for (i = 0; i < STUNRUN_ADSP_SOURCE_SECOND_WRITES; ++i) {
        const uint16_t *dm = &simbuf_words[30u + (i / 3u) * 3u];
        const uint32_t *pm = &second_pm[i * 3u];
        result[STUNRUN_ADSP_SOURCE_FIRST_WRITES + i] =
            second_loop_output(dm, pm);
    }

    memcpy(output, result, sizeof(result));
    return 1;
}
