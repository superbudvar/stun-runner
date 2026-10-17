#include "som_producer.h"

#include <limits.h>
#include <string.h>

/* ADSP registers are 16-bit; avoid implementation-defined uint16_t->int16_t
 * conversion when interpreting the captured signed source samples. */
static int32_t adsp_s16(uint16_t value)
{
    return (value & 0x8000u) != 0u
        ? (int32_t)value - 0x10000
        : (int32_t)value;
}

/* $04C6/$04C8: MAME's captured MSTAT has fractional mode enabled. mac_op_mr()
 * doubles the signed product, adds 0x8000, then reads MR1. MAME's tie case
 * clears bit 16 when the product low word is exactly 0x8000 [S10]. */
static int adsp_rnd_mr1(uint16_t x_bits, uint16_t y_bits, uint16_t *out)
{
    int64_t product = (int64_t)adsp_s16(x_bits) * adsp_s16(y_bits) * 2;
    int32_t temp;
    uint32_t low;
    uint64_t rounded;

    if (product < INT32_MIN || product > INT32_MAX || out == NULL)
        return 0;
    temp = (int32_t)product;
    low = (uint32_t)temp;
    rounded = (uint64_t)((int64_t)temp + 0x8000);

    if ((low & 0xffffu) == 0x8000u)
        rounded &= ~UINT64_C(0x10000);
    *out = (uint16_t)((rounded >> 16) & 0xffffu);
    return 1;
}

/* $04D6-$04DC: with SE=-10, LSHIFT(MR0,LO) | LSHIFT(MR1,HI) is the low
 * 32-bit MR word shifted right by ten, and DM(I0,M1) stores SR0. */
static uint16_t adsp_ss_shifted_low16(uint16_t x_bits, uint16_t y_bits)
{
    uint32_t mr_low = (uint32_t)((int64_t)adsp_s16(x_bits)
                               * (int64_t)adsp_s16(y_bits) * 2);
    return (uint16_t)((mr_low >> 10) & 0xffffu);
}

static int dm_window_read(const uint16_t *dm, size_t dm_word_count,
                          uint16_t dm_base, uint32_t address,
                          uint16_t *value)
{
    size_t offset;

    if (address < dm_base)
        return 0;
    offset = (size_t)(address - dm_base);
    if (offset >= dm_word_count)
        return 0;
    *value = dm[offset];
    return 1;
}

int stunrun_adsp_som_produce_ring(
    const uint16_t *source_dm, size_t source_dm_word_count,
    uint16_t source_dm_base, uint16_t start_i0,
    const uint16_t *object_rom_words, size_t object_rom_word_count,
    uint16_t pass2_guard, uint16_t pass2_y_multiplier,
    uint16_t pass1_out[STUNRUN_ADSP_SOM_RING_WORDS],
    uint16_t ring_out[STUNRUN_ADSP_SOM_RING_WORDS])
{
    uint16_t pass1[STUNRUN_ADSP_SOM_RING_WORDS];
    uint16_t final_ring[STUNRUN_ADSP_SOM_RING_WORDS];
    uint32_t i0;
    size_t source_offset;
    size_t tuple;

    if (source_dm == NULL || object_rom_words == NULL ||
        pass1_out == NULL || ring_out == NULL || start_i0 < source_dm_base ||
        start_i0 != 0x0040u ||
        (pass2_guard != 0u &&
         pass2_guard != STUNRUN_ADSP_SOM_PASS2_GUARD))
        return 0;
    if (pass2_guard != 0u &&
        pass2_y_multiplier != STUNRUN_ADSP_SOM_PASS2_Y_MULTIPLIER)
        return 0;

    /* Only the measured CNTR=3 loop and its single prefetch after the final
     * tuple are represented here. The final x word is read at $04C9 but not
     * consumed by an output multiply. */
    source_offset = (size_t)(start_i0 - source_dm_base);
    if (source_offset > source_dm_word_count ||
        source_dm_word_count - source_offset < STUNRUN_ADSP_SOM_SOURCE_WORDS)
        return 0;

    i0 = start_i0;
    {
        uint16_t mx0;
        if (!dm_window_read(source_dm, source_dm_word_count, source_dm_base,
                            i0++, &mx0))
            return 0;

        for (tuple = 0; tuple < STUNRUN_ADSP_SOM_SOURCE_TUPLES; tuple++) {
            uint16_t source_y;
            uint16_t simclk;
            uint16_t coefficient;
            uint16_t next_x;
            size_t rom_address;
            uint16_t negated_y;
            uint16_t x_mr1;
            uint16_t y_mr1;

            if (!dm_window_read(source_dm, source_dm_word_count,
                                source_dm_base, i0++, &source_y) ||
                !dm_window_read(source_dm, source_dm_word_count,
                                source_dm_base, i0++, &simclk))
                return 0;

            /* $04C3 branches to the unimplemented $04E0 path when SIMCLK is
             * <= $0200. Every captured tuple is above that threshold. */
            if (simclk <= 0x0200u)
                return 0;
            rom_address = (size_t)STUNRUN_ADSP_SOM_MP1_BASE_WORDS + simclk;
            if (rom_address >= object_rom_word_count)
                return 0;
            coefficient = object_rom_words[rom_address];

            /* $04C4 AR=-SR1; $04C5 fetches MY0 from SIMBUF. */
            negated_y = (uint16_t)(0u - source_y);
            if (!adsp_rnd_mr1(mx0, coefficient, &x_mr1) ||
                !adsp_rnd_mr1(negated_y, coefficient, &y_mr1))
                return 0;

            /* $04C7/$04C9 add MR1 to the 0x00FF centering values; the ADSP
             * register and DM stores wrap to 16 bits. */
            pass1[tuple * 2u] = (uint16_t)(0x00ffu + x_mr1);
            pass1[tuple * 2u + 1u] = (uint16_t)(0x00ffu + y_mr1);

            /* $04C9 prefetches the next x after storing the current pair,
             * including one final value that the third iteration does not use. */
            if (!dm_window_read(source_dm, source_dm_word_count,
                                source_dm_base, i0++, &next_x))
                return 0;
            mx0 = next_x;
        }
    }

    if (pass2_guard == 0u) {
        /* Static $04CD guard behavior; the observed transaction takes pass 2. */
        memcpy(final_ring, pass1, sizeof(final_ring));
    } else {
        size_t pair;

        for (pair = 0; pair < STUNRUN_ADSP_SOM_SOURCE_TUPLES; pair++) {
            final_ring[pair * 2u] = adsp_ss_shifted_low16(
                pass2_guard, pass1[pair * 2u]);
            final_ring[pair * 2u + 1u] = adsp_ss_shifted_low16(
                pass2_y_multiplier, pass1[pair * 2u + 1u]);
        }
    }

    memcpy(pass1_out, pass1, sizeof(pass1));
    memcpy(ring_out, final_ring, sizeof(final_ring));
    return 1;
}
