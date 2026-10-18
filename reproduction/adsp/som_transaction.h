/* Bounded SOMCLK -> SOMLATCH -> GINT transaction composer for frame 551. */
#ifndef STUNRUN_ADSP_SOM_TRANSACTION_H
#define STUNRUN_ADSP_SOM_TRANSACTION_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STUNRUN_ADSP_SOM_TRANSACTION_RECORDS 9u
#define STUNRUN_ADSP_SOM_TRANSACTION_RECORD_WORDS 16u
#define STUNRUN_ADSP_SOM_TRANSACTION_PROLOGUE_WORDS 2u
#define STUNRUN_ADSP_SOM_TRANSACTION_WORDS \
    (STUNRUN_ADSP_SOM_TRANSACTION_PROLOGUE_WORDS + \
     STUNRUN_ADSP_SOM_TRANSACTION_RECORDS * \
     STUNRUN_ADSP_SOM_TRANSACTION_RECORD_WORDS)
#define STUNRUN_ADSP_SOM_ADDRESS_MASK 0x1fffu
#define STUNRUN_ADSP_SOM_GINT_REQUEST_MASK 0x4000u
#define STUNRUN_ADSP_SOM_GINT_PC 0x004cu

typedef struct stunrun_adsp_som_latch_write {
    uint16_t address_word;
    uint16_t value;
} stunrun_adsp_som_latch_write_t;

typedef struct stunrun_adsp_som_transaction {
    uint16_t host_bank;
    uint16_t producer_bank;
    uint16_t som_start_word;
    size_t som_write_count;
    stunrun_adsp_som_latch_write_t writes[STUNRUN_ADSP_SOM_TRANSACTION_WORDS];
    uint16_t gint_pc;
    uint16_t gint_value;
    size_t gint_after_write_count;
    int gint_written;
} stunrun_adsp_som_transaction_t;

/* Compose the captured nine-record transaction from already-derived record
 * words. The envelope matches PC $0034 SOMCLK, two zero SOMLATCH prologue
 * writes, then nine records; the final record's surviving AX0 becomes the
 * value written to GINT at PC $004C when the observed request bit is set.
 *
 * `final_ax0_valid` must be true (the walk reached its pointer-comparison
 * tail), `gint_request_word` is the observed DM(I5,M7) command with bit 14 set,
 * and host_bank selects the bank the 68010 has published. The ADSP writes the
 * opposite bank. Addresses wrap in the 8K-word SOM window. Unsupported or
 * truncated input fails without modifying `out`.
 */
int stunrun_adsp_som_transaction_build(
    const uint16_t *record_words, size_t record_word_count,
    uint16_t host_bank, uint16_t somclk_word,
    int final_ax0_valid, uint16_t final_ax0,
    uint16_t gint_request_word,
    stunrun_adsp_som_transaction_t *out);

#ifdef __cplusplus
}
#endif

#endif /* STUNRUN_ADSP_SOM_TRANSACTION_H */
