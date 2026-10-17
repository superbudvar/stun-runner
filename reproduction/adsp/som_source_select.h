/* Bounded data-side transcription of the captured ADSP $01E8/$01B0 selector.
 *
 * This selects the observed three-point DM source window from a DM table using
 * the current MP=1 SIMBUF pointer stream. It does not reproduce the E8 weighted
 * accumulator's MR2 return flags or the caller's $01A9 loop decision.
 */
#ifndef STUNRUN_ADSP_SOM_SOURCE_SELECT_H
#define STUNRUN_ADSP_SOM_SOURCE_SELECT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STUNRUN_ADSP_SOM_SOURCE_WINDOW_WORDS 9u
#define STUNRUN_ADSP_SOM_WEIGHT_WORDS 3u
#define STUNRUN_ADSP_SOM_SOURCE_TABLE_BASE 0x010au
#define STUNRUN_ADSP_SOM_SIMBUF_MP_WORDS 0x10000u

typedef struct stunrun_adsp_som_source_selection {
    uint16_t source_words[STUNRUN_ADSP_SOM_SOURCE_WINDOW_WORDS];
    uint16_t mirrored_first_point[3]; /* DM($08E7..$08E9) */
    uint16_t weight_words[STUNRUN_ADSP_SOM_WEIGHT_WORDS]; /* DM($0014..$0016) */
    uint16_t dm0039;
    uint16_t dm08ea;
    size_t simbuf_reads;
} stunrun_adsp_som_source_selection_t;

/* Select the two additional three-word DM points following the initial point.
 *
 * dm_table is a bounded absolute-address DM window. simbuf_words is a bounded
 * view of the word-addressed object-ROM image starting at simbuf_base_word.
 * simclk_start is the SIMCLK value established by the caller; MP must be one
 * for this captured path. The captured stream has two header selectors, two
 * point selectors and a $FFFF terminator. Other selector counts fail rather
 * than inventing a ring layout.
 *
 * Outputs represent the observed DM writes in $01E8/$01B0 only. MR2/flags from
 * $0211/$0212 and the caller's subsequent branch are explicitly not modeled.
 * Returns 1 on success, 0 on invalid/truncated windows or unsupported values;
 * output is left unchanged on failure. */
int stunrun_adsp_som_select_source_window(
    const uint16_t *dm_table, size_t dm_word_count, uint16_t dm_base,
    const uint16_t *simbuf_words, size_t simbuf_word_count,
    uint32_t simbuf_base_word, uint16_t mp, uint16_t simclk_start,
    stunrun_adsp_som_source_selection_t *out);

#ifdef __cplusplus
}
#endif

#endif /* STUNRUN_ADSP_SOM_SOURCE_SELECT_H */
