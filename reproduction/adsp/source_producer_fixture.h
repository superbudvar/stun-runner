/* Same-run frame-551 operands and expected DM writes for $0147-$0173. */
#ifndef STUNRUN_ADSP_SOURCE_PRODUCER_FIXTURE_H
#define STUNRUN_ADSP_SOURCE_PRODUCER_FIXTURE_H

#include <stddef.h>
#include <stdint.h>

#include "source_producer.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STUNRUN_ADSP_SOURCE_FIXTURE_SIMBUF_WORDS 58u
#define STUNRUN_ADSP_SOURCE_FIXTURE_AUX_WORDS 30u
#define STUNRUN_ADSP_SOURCE_FIXTURE_TABLE_BASE 0x010au
#define STUNRUN_ADSP_SOURCE_FIXTURE_TABLE_WORDS 60u

typedef struct stunrun_adsp_source_fixture {
    const uint16_t *simbuf_words;
    const uint16_t *first_aux_words;
    const uint16_t *staged_dm_words;
    const uint16_t *expected_dm_writes;
} stunrun_adsp_source_fixture_t;

const stunrun_adsp_source_fixture_t *stunrun_adsp_source_fixture(void);

int stunrun_adsp_source_fixture_fill_pm(
    uint32_t first_pm[STUNRUN_ADSP_SOURCE_FIRST_PM_READS],
    uint32_t second_pm[STUNRUN_ADSP_SOURCE_SECOND_PM_READS]);

#ifdef __cplusplus
}
#endif

#endif /* STUNRUN_ADSP_SOURCE_PRODUCER_FIXTURE_H */
