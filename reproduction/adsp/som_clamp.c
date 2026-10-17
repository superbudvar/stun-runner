#include "som_clamp.h"

#include <string.h>

static int16_t signed16(uint16_t value)
{
    int32_t signed_value = (value & 0x8000u) != 0u
        ? (int32_t)value - 0x10000
        : (int32_t)value;
    return (int16_t)signed_value;
}

int stunrun_adsp_som_clamp_ring(
    const uint16_t *ring_in, size_t ring_word_count,
    uint16_t ring_out[STUNRUN_ADSP_SOM_CLAMP_WORDS])
{
    uint16_t result[STUNRUN_ADSP_SOM_CLAMP_WORDS];
    size_t i;

    if (ring_in == NULL || ring_out == NULL ||
        ring_word_count != STUNRUN_ADSP_SOM_CLAMP_WORDS)
        return 0;

    for (i = 0; i < STUNRUN_ADSP_SOM_CLAMP_WORDS; ++i) {
        int16_t value = signed16(ring_in[i]);
        if (value < 0)
            result[i] = 0;
        else if ((uint16_t)value > STUNRUN_ADSP_SOM_CLAMP_MAX)
            result[i] = STUNRUN_ADSP_SOM_CLAMP_MAX;
        else
            result[i] = (uint16_t)value;
    }

    memcpy(ring_out, result, sizeof(result));
    return 1;
}
