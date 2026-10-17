/* Bounded transcription of the $04FE-$050D SOM-ring clipping loop. */
#ifndef STUNRUN_ADSP_SOM_CLAMP_H
#define STUNRUN_ADSP_SOM_CLAMP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STUNRUN_ADSP_SOM_CLAMP_WORDS 6u
#define STUNRUN_ADSP_SOM_CLAMP_MAX 0x01ffu

/* Apply the observed signed 16-bit clamp [0, 0x01ff] to exactly six ring
 * words. Returns 0 for NULL or a non-six-word input and leaves output intact. */
int stunrun_adsp_som_clamp_ring(
    const uint16_t *ring_in, size_t ring_word_count,
    uint16_t ring_out[STUNRUN_ADSP_SOM_CLAMP_WORDS]);

#ifdef __cplusplus
}
#endif

#endif /* STUNRUN_ADSP_SOM_CLAMP_H */
