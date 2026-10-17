/* Bounded C transcription of the observed ADSP source-DM producer. */
#ifndef STUNRUN_ADSP_SOURCE_PRODUCER_H
#define STUNRUN_ADSP_SOURCE_PRODUCER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STUNRUN_ADSP_SOURCE_DOT_TERMS 3u
#define STUNRUN_ADSP_SOURCE_SIMBUF_READS 58u
#define STUNRUN_ADSP_SOURCE_FIRST_WRITES 30u
#define STUNRUN_ADSP_SOURCE_SECOND_WRITES 27u
#define STUNRUN_ADSP_SOURCE_FIRST_PM_READS \
    (STUNRUN_ADSP_SOURCE_FIRST_WRITES * STUNRUN_ADSP_SOURCE_DOT_TERMS)
#define STUNRUN_ADSP_SOURCE_SECOND_PM_READS \
    (STUNRUN_ADSP_SOURCE_SECOND_WRITES * STUNRUN_ADSP_SOURCE_DOT_TERMS)
#define STUNRUN_ADSP_SOURCE_TOTAL_WRITES \
    (STUNRUN_ADSP_SOURCE_FIRST_WRITES + STUNRUN_ADSP_SOURCE_SECOND_WRITES)

/* Execute the captured $0147-$0173 path. The first loop emits 30 $015C
 * writes. simbuf_words are the exact 58 same-run reads from DM($2000): the
 * first 30 feed the $014F-$0154 ASHIFT/staging path, word 30 is the handoff
 * into the second loop, words 31..56 feed that loop, and word 57 is its final
 * prefetch. first_pm/second_pm contain the ordered full 24-bit program-space
 * reads paired with each dot product. first_aux contains the 30 $015A DM
 * reads. Output order is DM[$010D..$0145].
 *
 * This entry point deliberately accepts only the observed 30/27 iteration
 * counts. Unsupported counts, malformed PM dwords, or missing buffers fail
 * without modifying output. */
int stunrun_adsp_source_produce(
    const uint16_t *simbuf_words, size_t simbuf_count, int8_t source_shift,
    const uint32_t *first_pm, size_t first_pm_count,
    const uint16_t *first_aux, size_t first_aux_count,
    const uint32_t *second_pm, size_t second_pm_count,
    uint16_t output[STUNRUN_ADSP_SOURCE_TOTAL_WRITES]);

#ifdef __cplusplus
}
#endif

#endif
