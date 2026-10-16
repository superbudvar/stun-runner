/* Bounded C transcription of the observed ADSP SOM interpolation helper. */
#include "som_math.h"

int stunrun_adsp_som_interpolation_low_branch(
    uint16_t lookup_index, uint16_t i0, uint16_t i2,
    const uint16_t *data_words, size_t data_word_count,
    const uint32_t *program_words, size_t program_word_count,
    stunrun_adsp_mac_pair_t *out)
{
    size_t coefficient_address;
    uint16_t my0;
    uint16_t sample0, sample1, difference_bits;
    int32_t difference;
    int64_t product;
    uint32_t result;

    if (data_words == NULL || program_words == NULL || out == NULL ||
        lookup_index > STUNRUN_ADSP_SOM_COEFF_LIMIT || i0 == 0u || i2 == 0u)
        return 0;

    coefficient_address = STUNRUN_ADSP_SOM_COEFF_BASE + lookup_index;
    if (coefficient_address >= program_word_count ||
        (size_t)(i0 - 1u) >= data_word_count ||
        (size_t)(i2 - 1u) >= data_word_count)
        return 0;

    /* $05DD: M6=AR; PM($1036+AR) -> MY0. MAME stores ADSP 24-bit PM values
     * in 32-bit containers; captured PM[$103D]=$00124900 yielded MY0=$1249. */
    my0 = (uint16_t)((program_words[coefficient_address] >> 8) & 0xffffu);

    /* $05ED/$05EF: read DM(I0-1,M3) and DM(I2-1,M5), then AR=sample0-sample1.
     * The post-increment modifiers affect the address registers after these
     * reads; they do not alter the two current sample addresses. */
    sample0 = data_words[i0 - 1u];
    sample1 = data_words[i2 - 1u];
    difference_bits = (uint16_t)(sample0 - sample1);
    difference = (difference_bits & 0x8000u) != 0u
        ? (int32_t)difference_bits - 0x10000
        : (int32_t)difference_bits;

    /* $05F1: MR = AR * MY0 (SU), fractional multiply. Two captured vectors
     * show MR1:MR0 == 2 * signed_AR * unsigned_MY0 (low 32 bits). */
    product = (int64_t)difference * (int64_t)my0 * 2;
    result = (uint32_t)product;
    out->mr0 = (uint16_t)(result & 0xffffu);
    out->mr1 = (uint16_t)(result >> 16);
    return 1;
}
