/* Build the bounded absolute DM table consumed by $01E8/$01B0. */
#ifndef STUNRUN_ADSP_SOM_SOURCE_TABLE_H
#define STUNRUN_ADSP_SOM_SOURCE_TABLE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STUNRUN_ADSP_SOM_SOURCE_TABLE_BASE 0x010au
#define STUNRUN_ADSP_SOM_SOURCE_TABLE_WORDS 60u
#define STUNRUN_ADSP_SOM_SOURCE_TABLE_FIRST_WRITE 0x010du

/* Derive DM[$010A..$0145] by running the measured $0147-$0173 input through
 * the source producer. Only this captured 60-word absolute window is accepted;
 * the three leading cells are zero-initialized because the captured selectors
 * do not address them. */
int stunrun_adsp_som_build_source_table(
    uint16_t *table, size_t table_word_count, uint16_t table_base);

#ifdef __cplusplus
}
#endif

#endif /* STUNRUN_ADSP_SOM_SOURCE_TABLE_H */
