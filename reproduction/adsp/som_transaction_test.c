#include "som_transaction.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define check(condition) do { \
    if (!(condition)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        failures++; \
    } \
} while (0)

static void test_opposite_bank_order_and_completion(void)
{
    uint16_t records[STUNRUN_ADSP_SOM_TRANSACTION_RECORDS *
                     STUNRUN_ADSP_SOM_TRANSACTION_RECORD_WORDS];
    stunrun_adsp_som_transaction_t tx;
    size_t i;

    for (i = 0; i < sizeof(records) / sizeof(records[0]); ++i)
        records[i] = (uint16_t)(0x1200u + i);
    check(stunrun_adsp_som_transaction_build(
        records, sizeof(records) / sizeof(records[0]), 1u, 0u,
        1, 0x0045u, 0x4000u, &tx));
    check(tx.host_bank == 1u && tx.producer_bank == 0u);
    check(tx.som_start_word == 0u);
    check(tx.som_write_count == STUNRUN_ADSP_SOM_TRANSACTION_WORDS);
    check(tx.writes[0].address_word == 0u && tx.writes[0].value == 0u);
    check(tx.writes[1].address_word == 1u && tx.writes[1].value == 0u);
    for (i = 0; i < sizeof(records) / sizeof(records[0]); ++i) {
        size_t slot = i + STUNRUN_ADSP_SOM_TRANSACTION_PROLOGUE_WORDS;
        check(tx.writes[slot].address_word == slot);
        check(tx.writes[slot].value == records[i]);
    }
    check(tx.gint_written == 1);
    check(tx.gint_pc == 0x004cu);
    check(tx.gint_value == 0x0045u);
    check(tx.gint_after_write_count == STUNRUN_ADSP_SOM_TRANSACTION_WORDS);
}

static void test_window_wrap_and_rejection(void)
{
    uint16_t records[STUNRUN_ADSP_SOM_TRANSACTION_RECORDS *
                     STUNRUN_ADSP_SOM_TRANSACTION_RECORD_WORDS] = {0};
    stunrun_adsp_som_transaction_t tx;
    stunrun_adsp_som_transaction_t untouched;

    check(stunrun_adsp_som_transaction_build(
        records, sizeof(records) / sizeof(records[0]), 0u, 0x1fffu,
        1, 0x0045u, 0x4000u, &tx));
    check(tx.writes[0].address_word == 0x1fffu);
    check(tx.writes[1].address_word == 0u);
    check(tx.writes[2].address_word == 1u);
    check(tx.producer_bank == 1u);

    memset(&untouched, 0xa5, sizeof(untouched));
    tx = untouched;
    check(!stunrun_adsp_som_transaction_build(
        records, sizeof(records) / sizeof(records[0]) - 1u,
        1u, 0u, 1, 0x45u, 0x4000u, &tx));
    check(memcmp(&tx, &untouched, sizeof(tx)) == 0);
    check(!stunrun_adsp_som_transaction_build(
        records, sizeof(records) / sizeof(records[0]),
        2u, 0u, 1, 0x45u, 0x4000u, &tx));
    check(!stunrun_adsp_som_transaction_build(
        records, sizeof(records) / sizeof(records[0]),
        1u, 0u, 0, 0x45u, 0x4000u, &tx));
    check(!stunrun_adsp_som_transaction_build(
        records, sizeof(records) / sizeof(records[0]),
        1u, 0u, 1, 0x45u, 0x0000u, &tx));
}

int main(void)
{
    test_opposite_bank_order_and_completion();
    test_window_wrap_and_rejection();
    if (failures == 0)
        puts("PASS");
    return failures == 0 ? 0 : 1;
}
