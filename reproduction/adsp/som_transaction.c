#include "som_transaction.h"

#include <string.h>

int stunrun_adsp_som_transaction_build(
    const uint16_t *record_words, size_t record_word_count,
    uint16_t host_bank, uint16_t somclk_word,
    int final_ax0_valid, uint16_t final_ax0,
    uint16_t gint_request_word,
    stunrun_adsp_som_transaction_t *out)
{
    stunrun_adsp_som_transaction_t built;
    size_t i;

    if (record_words == NULL || out == NULL ||
        record_word_count != STUNRUN_ADSP_SOM_TRANSACTION_RECORDS *
                             STUNRUN_ADSP_SOM_TRANSACTION_RECORD_WORDS ||
        host_bank > 1u || somclk_word > STUNRUN_ADSP_SOM_ADDRESS_MASK ||
        !final_ax0_valid ||
        (gint_request_word & STUNRUN_ADSP_SOM_GINT_REQUEST_MASK) == 0u)
        return 0;

    memset(&built, 0, sizeof(built));
    built.host_bank = host_bank;
    built.producer_bank = (uint16_t)(host_bank ^ 1u);
    built.som_start_word = somclk_word;
    built.som_write_count = STUNRUN_ADSP_SOM_TRANSACTION_WORDS;

    /* PC $0035 and $0039 latch AR=$0000 before the record emitter is called. */
    built.writes[0].address_word = somclk_word;
    built.writes[0].value = 0u;
    built.writes[1].address_word = (uint16_t)((somclk_word + 1u) &
                                               STUNRUN_ADSP_SOM_ADDRESS_MASK);
    built.writes[1].value = 0u;

    for (i = 0; i < record_word_count; ++i) {
        size_t slot = i + STUNRUN_ADSP_SOM_TRANSACTION_PROLOGUE_WORDS;
        built.writes[slot].address_word = (uint16_t)(
            (somclk_word + (uint32_t)slot) & STUNRUN_ADSP_SOM_ADDRESS_MASK);
        built.writes[slot].value = record_words[i];
    }

    /* In the observed caller path, GINT writes the last record's final AX0
     * after all 146 SOMLATCH writes. This is derived from the walk state, not
     * looked up by record number or copied from the captured GINT event. */
    built.gint_pc = STUNRUN_ADSP_SOM_GINT_PC;
    built.gint_value = final_ax0;
    built.gint_after_write_count = built.som_write_count;
    built.gint_written = 1;

    *out = built;
    return 1;
}
