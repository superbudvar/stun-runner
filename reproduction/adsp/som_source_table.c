#include "som_source_table.h"

#include <string.h>

#include "source_producer.h"
#include "source_producer_fixture.h"

int stunrun_adsp_som_build_source_table(
    uint16_t *table, size_t table_word_count, uint16_t table_base)
{
    const stunrun_adsp_source_fixture_t *fixture =
        stunrun_adsp_source_fixture();
    uint32_t first_pm[STUNRUN_ADSP_SOURCE_FIRST_PM_READS];
    uint32_t second_pm[STUNRUN_ADSP_SOURCE_SECOND_PM_READS];
    uint16_t writes[STUNRUN_ADSP_SOURCE_TOTAL_WRITES];
    uint16_t result[STUNRUN_ADSP_SOM_SOURCE_TABLE_WORDS] = {0};
    size_t offset;

    if (table == NULL || table_word_count != STUNRUN_ADSP_SOM_SOURCE_TABLE_WORDS ||
        table_base != STUNRUN_ADSP_SOM_SOURCE_TABLE_BASE ||
        STUNRUN_ADSP_SOM_SOURCE_TABLE_FIRST_WRITE < table_base)
        return 0;
    offset = (size_t)(STUNRUN_ADSP_SOM_SOURCE_TABLE_FIRST_WRITE - table_base);
    if (offset + STUNRUN_ADSP_SOURCE_TOTAL_WRITES > table_word_count ||
        !stunrun_adsp_source_fixture_fill_pm(first_pm, second_pm) ||
        !stunrun_adsp_source_produce(
            fixture->simbuf_words, STUNRUN_ADSP_SOURCE_SIMBUF_READS, -2,
            first_pm, STUNRUN_ADSP_SOURCE_FIRST_PM_READS,
            fixture->first_aux_words, STUNRUN_ADSP_SOURCE_FIRST_WRITES,
            second_pm, STUNRUN_ADSP_SOURCE_SECOND_PM_READS, writes))
        return 0;

    memcpy(result + offset, writes, sizeof(writes));
    memcpy(table, result, sizeof(result));
    return 1;
}
