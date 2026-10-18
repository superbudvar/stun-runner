/* ROM-free self-check for the ADSP board-test verdict and the $5ff6
 * program-ROM self-test driver.
 *
 * Every expectation here is transcribed from the disassembly in
 * adsp_board_test.h, not from adsp_board_test.c's output. The three groups:
 *
 *  - the verdict chain $5224-$533c. The observable is the chosen string
 *    index, because that index IS what the routine exists to produce: it is
 *    loaded into D0 and handed to the $25e8 string drawer. Every branch is
 *    a separate case -- three gates, six scratch bytes, six RAM longs.
 *  - the six $ff90xx addresses in the order $52bc-$52f4 tests them. The
 *    order is part of the contract (the run is not ascending) so it is
 *    compared against a hand-written array, not against the array in the
 *    implementation.
 *  - the $5ff6 descriptors, compared field by field against a hand-written
 *    transcription, plus the derived structure (8 entries of $12 bytes,
 *    $20000 sum-base stride, bits 15..0 used exactly once across the eight
 *    failure-bit fields).
 *
 * The driver cases use a sparse fake ROM: an explicit list of (address,
 * word) overrides over an all-zero background, so every expected sum is
 * hand-computable. One case is chosen specifically to discriminate the ROM's
 * `clr.b D0` second sum from a plain `sum >> 8`; see the comment there.
 */
#include <stdio.h>
#include <string.h>

#include "adsp_board_test.h"

static int g_failures = 0;

#define check(cond) \
    do { \
        if (cond) { \
            printf("PASS %s\n", #cond); \
        } else { \
            printf("FAIL %s\n", #cond); \
            g_failures++; \
        } \
    } while (0)

/* ---- sparse fake ROM + recording board ---------------------------------- */

#define FAKE_MAX 8u

typedef struct fake_word {
    uint32_t addr;
    uint16_t value;
} fake_word_t;

static fake_word_t g_words[FAKE_MAX];
static unsigned g_word_count;
static unsigned long g_reads;
static unsigned g_kicks;
static uint32_t g_min_addr;
static uint32_t g_max_addr;
static uint32_t g_last_addr;
static uint32_t g_run_start[8];
static unsigned long g_run_len[8];
static unsigned g_run_count;

static void fake_clear(void)
{
    memset(g_words, 0, sizeof(g_words));
    g_word_count = 0u;
    g_reads = 0ul;
    g_kicks = 0u;
    g_min_addr = 0u;
    g_max_addr = 0u;
    g_last_addr = 0u;
    g_run_count = 0u;
}

static void fake_put(uint32_t addr, uint16_t value)
{
    g_words[g_word_count].addr = addr;
    g_words[g_word_count].value = value;
    g_word_count++;
}

static uint16_t fake_read_rom16(void *ctx, uint32_t addr)
{
    unsigned i;

    (void)ctx;
    /* $60d8/$6138 use `add.w (A0)+,D0`: +2 per word, no gaps, and the walk
     * restarts only where the ROM reloads A0. */
    if (g_reads == 0ul || addr != g_last_addr + 2u) {
        if (g_run_count < 8u) {
            g_run_start[g_run_count] = addr;
            g_run_len[g_run_count] = 0ul;
        }
        g_run_count++;
    }
    if (g_run_count > 0u && g_run_count <= 8u)
        g_run_len[g_run_count - 1u]++;
    g_last_addr = addr;
    if (g_reads == 0ul || addr < g_min_addr)
        g_min_addr = addr;
    if (g_reads == 0ul || addr > g_max_addr)
        g_max_addr = addr;
    g_reads++;
    for (i = 0; i < g_word_count; i++) {
        if (g_words[i].addr == addr)
            return g_words[i].value;
    }
    return 0u;
}

static void fake_write_watchdog(void *ctx)
{
    (void)ctx;
    g_kicks++;
}

static stunrun_board_t g_board;

static void board_init(void)
{
    memset(&g_board, 0, sizeof(g_board));
    /* Only these two members are reachable from $60ac-$616e; the rest of the
     * contract stays NULL and the port must not touch it. */
    g_board.read_rom16 = fake_read_rom16;
    g_board.write_watchdog = fake_write_watchdog;
}

/* ---- $5ff6, transcribed by hand from the image ------------------------- */

static const stunrun_adsp_test_descriptor_t kHandDesc[STUNRUN_ADSP_TEST_DESC_COUNT] = {
    { 0x0001u, 0x01u, 0x02u, 0x0Fu, 0x0Eu, 0x00000000u, 0xFF8285C0u, 0xFF81E5C0u },
    { 0x0001u, 0x03u, 0x04u, 0x0Du, 0x0Cu, 0x00020000u, 0xFF828540u, 0xFF81E540u },
    { 0x0001u, 0x05u, 0x06u, 0x0Bu, 0x0Au, 0x00040000u, 0xFF8284C0u, 0xFF81E4C0u },
    { 0x0001u, 0x07u, 0x08u, 0x09u, 0x08u, 0x00060000u, 0xFF828440u, 0xFF81E440u },
    { 0x0001u, 0x09u, 0x10u, 0x07u, 0x06u, 0x00080000u, 0xFF8283C0u, 0xFF81E3C0u },
    { 0x0001u, 0x11u, 0x12u, 0x05u, 0x04u, 0x000A0000u, 0xFF828340u, 0xFF81E340u },
    { 0x0000u, 0x13u, 0x14u, 0x03u, 0x02u, 0x000C0000u, 0xFF8282C0u, 0xFF81E2C0u },
    { 0x0000u, 0x15u, 0x16u, 0x01u, 0x00u, 0x000E0000u, 0xFF828240u, 0xFF81E240u },
};

/* $52bc, $52c6, $52d0, $52da, $52e4, $52ee -- in that order. */
static const uint32_t kHandRamOrder[STUNRUN_ADSP_RAM_CHECK_COUNT] = {
    0x00FF9010u, 0x00FF9014u, 0x00FF9020u,
    0x00FF9024u, 0x00FF9028u, 0x00FF9038u,
};

/* The +2/+3 expected-sum bytes as shipped: 1,2 3,4 5,6 7,8 9,16 17,18 19,20
 * 21,22 -- note the jump from 9 to 16, which the transcription preserves
 * rather than "correcting". */
static const uint8_t kHandExpectLow[STUNRUN_ADSP_TEST_DESC_COUNT] = {
    0x01u, 0x03u, 0x05u, 0x07u, 0x09u, 0x11u, 0x13u, 0x15u,
};
static const uint8_t kHandExpectHigh[STUNRUN_ADSP_TEST_DESC_COUNT] = {
    0x02u, 0x04u, 0x06u, 0x08u, 0x10u, 0x12u, 0x14u, 0x16u,
};

static void fill_all_gates_pass(stunrun_adsp_gates_t *g)
{
    memset(g, 0, sizeof(*g));
    g->loopback_d7 = STUNRUN_ADSP_PASS_WORD;
    g->irq_d7      = STUNRUN_ADSP_PASS_WORD;
    g->scratch_d7  = STUNRUN_ADSP_PASS_WORD;
    /* scratch[] is D0..D5 in register order. */
    g->scratch[0] = 0x18u; /* D0 */
    g->scratch[1] = 0x21u; /* D1 */
    g->scratch[2] = 0x19u; /* D2 */
    g->scratch[3] = 0x22u; /* D3 */
    g->scratch[4] = 0x17u; /* D4 */
    g->scratch[5] = 0x20u; /* D5 */
}

int main(void)
{
    stunrun_adsp_gates_t gates;
    uint32_t ram[STUNRUN_ADSP_RAM_CHECK_COUNT];
    uint16_t seq[3];
    unsigned i;

    board_init();

    /* ---- string indices, from the $10136 pointer table ------------------ */
    check(STUNRUN_ADSP_STRING_TIMED == 94u);
    check(STUNRUN_ADSP_STRING_BOARD_OK == 95u);
    check(STUNRUN_ADSP_STRING_BAD_RAM == 96u);
    check(STUNRUN_ADSP_STRING_NO_RESPONSE == 97u);
    check(STUNRUN_ADSP_STRING_BAD_SCRATCH == 98u);
    check(STUNRUN_ADSP_STRING_PRESS_START == 99u);
    check(STUNRUN_ADSP_STRING_ROM_OK == 67u);
    check(stunrun_adsp_board_string(94) != NULL &&
          strcmp(stunrun_adsp_board_string(94),
                 "ADSP TEST: 35 SECS - 3 MINS") == 0);
    check(stunrun_adsp_board_string(95) != NULL &&
          strcmp(stunrun_adsp_board_string(95), "ADSP BOARD OK") == 0);
    check(stunrun_adsp_board_string(96) != NULL &&
          strcmp(stunrun_adsp_board_string(96), "BAD ADSP RAM") == 0);
    check(stunrun_adsp_board_string(97) != NULL &&
          strcmp(stunrun_adsp_board_string(97),
                 "ADSP-2100 DOES NOT RESPOND") == 0);
    check(stunrun_adsp_board_string(98) != NULL &&
          strcmp(stunrun_adsp_board_string(98),
                 "BAD ADSP ROM CHECKSUM") == 0);
    check(stunrun_adsp_board_string(99) != NULL &&
          strcmp(stunrun_adsp_board_string(99),
                 "PRESS START TO SELECT TEST MENU") == 0);
    check(stunrun_adsp_board_string(93) != NULL &&
          strcmp(stunrun_adsp_board_string(93),
                 "ADSP BOARD DOES NOT RESPOND") == 0);
    check(stunrun_adsp_board_string(67) != NULL &&
          strcmp(stunrun_adsp_board_string(67), "PROGRAM ROM OK") == 0);
    check(stunrun_adsp_board_string(68) != NULL &&
          strcmp(stunrun_adsp_board_string(68),
                 "BAD PROGRAM ROM IS BLANK BOX") == 0);
    check(stunrun_adsp_board_string(69) != NULL &&
          strcmp(stunrun_adsp_board_string(69),
                 "ROMS AT 200R AND 210R") == 0);
    check(stunrun_adsp_board_string(70) != NULL &&
          strcmp(stunrun_adsp_board_string(70),
                 "CONTAIN THE SELF-TEST PROGRAM") == 0);
    check(stunrun_adsp_board_string(1u) == NULL);

    /* ---- $52bc-$52f4: the six RAM longs, in ROM order ------------------- */
    for (i = 0; i < STUNRUN_ADSP_RAM_CHECK_COUNT; i++) {
        if (stunrun_adsp_board_ram_address(i) != kHandRamOrder[i]) {
            printf("FAIL ram address %u: got 0x%08lX want 0x%08lX\n", i,
                   (unsigned long)stunrun_adsp_board_ram_address(i),
                   (unsigned long)kHandRamOrder[i]);
            g_failures++;
        } else {
            printf("PASS ram address %u\n", i);
        }
    }
    check(stunrun_adsp_board_ram_address(STUNRUN_ADSP_RAM_CHECK_COUNT) == 0u);
    check(memcmp(stunrun_adsp_board_ram_addresses(), kHandRamOrder,
                 sizeof(kHandRamOrder)) == 0);
    /* The run is not ascending: $ff902c-$ff9034 is skipped. */
    check(kHandRamOrder[4] + 4u != kHandRamOrder[5]);

    /* ---- $5224-$52a2: the verdict gate ---------------------------------- */
    fill_all_gates_pass(&gates);
    memset(ram, 0, sizeof(ram));
    check(stunrun_adsp_board_test_select(&gates) == 94u);
    /* The gate reads no shared RAM: $52ac is behind $528c, not behind $527e. */
    memset(ram, 0xAA, sizeof(ram));
    check(stunrun_adsp_board_test_select(&gates) == 94u);
    check(stunrun_adsp_board_test_followup(94u) == 0u);

    /* Each gate, D7 = 0 (the ROM's own failure return, $b8b2/$b90a/$b9ac). */
    fill_all_gates_pass(&gates);
    gates.loopback_d7 = 0u;
    check(stunrun_adsp_board_test_select(&gates) == 97u);
    check(stunrun_adsp_board_test_followup(97u) == 99u);

    fill_all_gates_pass(&gates);
    gates.irq_d7 = 0u;
    check(stunrun_adsp_board_test_select(&gates) == 97u);

    fill_all_gates_pass(&gates);
    gates.scratch_d7 = 0u;
    check(stunrun_adsp_board_test_select(&gates) == 97u);

    /* Near-miss pass words: the ROM's cmpi.w is an exact compare. */
    fill_all_gates_pass(&gates);
    gates.loopback_d7 = 0x5556u;
    check(stunrun_adsp_board_test_select(&gates) == 97u);
    fill_all_gates_pass(&gates);
    gates.irq_d7 = 0x5554u;
    check(stunrun_adsp_board_test_select(&gates) == 97u);
    fill_all_gates_pass(&gates);
    gates.scratch_d7 = 0xAAAAu;
    check(stunrun_adsp_board_test_select(&gates) == 97u);

    /* A gate failure outranks a scratch mismatch: $5246 tests D7 first. */
    fill_all_gates_pass(&gates);
    gates.scratch_d7 = 0u;
    gates.scratch[4] = 0x00u;
    check(stunrun_adsp_board_test_select(&gates) == 97u);

    /* Six scratch bytes, one failing case each. $524e cmpi.b #$17,D4 ... */
    fill_all_gates_pass(&gates);
    gates.scratch[4] = 0x00u; /* D4 != $17 */
    check(stunrun_adsp_board_test_select(&gates) == 98u);
    check(stunrun_adsp_board_test_followup(98u) == 99u);
    fill_all_gates_pass(&gates);
    gates.scratch[0] = 0x00u; /* D0 != $18 */
    check(stunrun_adsp_board_test_select(&gates) == 98u);
    fill_all_gates_pass(&gates);
    gates.scratch[2] = 0x00u; /* D2 != $19 */
    check(stunrun_adsp_board_test_select(&gates) == 98u);
    fill_all_gates_pass(&gates);
    gates.scratch[5] = 0x00u; /* D5 != $20 */
    check(stunrun_adsp_board_test_select(&gates) == 98u);
    fill_all_gates_pass(&gates);
    gates.scratch[1] = 0x00u; /* D1 != $21 */
    check(stunrun_adsp_board_test_select(&gates) == 98u);
    fill_all_gates_pass(&gates);
    gates.scratch[3] = 0x00u; /* D3 != $22 */
    check(stunrun_adsp_board_test_select(&gates) == 98u);

    /* Off-by-one rather than zero, so the constants are pinned, not just
     * "any wrong value". */
    fill_all_gates_pass(&gates);
    gates.scratch[4] = 0x16u; /* $17 - 1 */
    check(stunrun_adsp_board_test_select(&gates) == 98u);
    fill_all_gates_pass(&gates);
    gates.scratch[0] = 0x19u; /* $18 + 1 */
    check(stunrun_adsp_board_test_select(&gates) == 98u);
    fill_all_gates_pass(&gates);
    gates.scratch[2] = 0x18u; /* $19 - 1 */
    check(stunrun_adsp_board_test_select(&gates) == 98u);
    fill_all_gates_pass(&gates);
    gates.scratch[5] = 0x21u; /* $20 + 1 */
    check(stunrun_adsp_board_test_select(&gates) == 98u);
    fill_all_gates_pass(&gates);
    gates.scratch[1] = 0x20u; /* $21 - 1 */
    check(stunrun_adsp_board_test_select(&gates) == 98u);
    fill_all_gates_pass(&gates);
    gates.scratch[3] = 0x23u; /* $22 + 1 */
    check(stunrun_adsp_board_test_select(&gates) == 98u);

    fill_all_gates_pass(&gates);
    check(stunrun_adsp_board_test_scratch_match(&gates) == 1);

    /* ---- $528c-$52aa: the poll that gates the second stage -------------- */
    check(stunrun_adsp_board_test_continue(0, 1) == 1); /* start held      */
    check(stunrun_adsp_board_test_continue(0, 0) == 0); /* neither: $f94   */
    check(stunrun_adsp_board_test_continue(1, 1) == 0); /* service: $260   */
    check(stunrun_adsp_board_test_continue(1, 0) == 0);

    /* ---- $52ac-$5300: the shared-RAM verdict --------------------------- */
    memset(ram, 0, sizeof(ram));
    check(stunrun_adsp_board_test_select_ram(ram) == 95u);
    check(stunrun_adsp_board_test_followup(95u) == 99u);

    /* Six longs, one non-zero each -> 96 ("BAD ADSP RAM"). */
    for (i = 0; i < STUNRUN_ADSP_RAM_CHECK_COUNT; i++) {
        memset(ram, 0, sizeof(ram));
        ram[i] = 0x00000101u; /* the value $31dc posted, not cleared */
        if (stunrun_adsp_board_test_select_ram(ram) != 96u) {
            printf("FAIL ram long %u did not select 96\n", i);
            g_failures++;
        } else {
            printf("PASS ram long %u selects 96\n", i);
        }
    }
    check(stunrun_adsp_board_test_followup(96u) == 99u);

    /* Only the high half of a long matters; a cleared word is enough. */
    memset(ram, 0, sizeof(ram));
    ram[3] = 0x00010000u;
    check(stunrun_adsp_board_test_select_ram(ram) == 96u);

    /* ---- the screen sequence ------------------------------------------- */
    fill_all_gates_pass(&gates);
    memset(ram, 0, sizeof(ram));
    /* 94, then with START held the RAM verdict, then the $533c tail. */
    check(stunrun_adsp_board_test_screen(&gates, ram, 0, 1, seq) == 3u);
    check(seq[0] == 94u);
    check(seq[1] == 95u);
    check(seq[2] == 99u);
    memset(ram, 0, sizeof(ram));
    check(stunrun_adsp_board_test_screen(&gates, ram, 0, 0, seq) == 1u);
    check(seq[0] == 94u);
    memset(ram, 0xFF, sizeof(ram));
    check(stunrun_adsp_board_test_screen(&gates, ram, 0, 1, seq) == 3u);
    check(seq[1] == 96u);
    check(seq[2] == 99u);
    memset(ram, 0, sizeof(ram));
    check(stunrun_adsp_board_test_screen(&gates, ram, 1, 1, seq) == 1u);
    /* A gate failure short-circuits the whole $528c poll. */
    gates.scratch_d7 = 0u;
    check(stunrun_adsp_board_test_screen(&gates, ram, 0, 1, seq) == 2u);
    check(seq[0] == 97u);
    check(seq[1] == 99u);
    gates.scratch_d7 = STUNRUN_ADSP_PASS_WORD;
    gates.scratch[0] = 0x00u;
    check(stunrun_adsp_board_test_screen(&gates, ram, 0, 1, seq) == 2u);
    check(seq[0] == 98u);
    check(seq[1] == 99u);
    check(stunrun_adsp_board_test_screen(NULL, ram, 0, 1, seq) == 2u);
    check(seq[0] == 97u);
    check(stunrun_adsp_board_test_screen(&gates, ram, 0, 1, NULL) == 0u);

    /* NULL safety on the parts. */
    check(stunrun_adsp_board_test_select(NULL) == 97u);
    check(stunrun_adsp_board_test_scratch_match(NULL) == 0);
    check(stunrun_adsp_board_test_select_ram(NULL) == 96u);

    /* ---- $5ff6: the descriptor table ------------------------------------ */
    check(STUNRUN_ADSP_TEST_DESC_COUNT == 8u);
    check(STUNRUN_ADSP_TEST_DESC_SIZE == 0x12u);
    check(STUNRUN_ADSP_TEST_DESC_BASE == 0x00005FF6u);
    for (i = 0; i < STUNRUN_ADSP_TEST_DESC_COUNT; i++) {
        const stunrun_adsp_test_descriptor_t *d = stunrun_adsp_test_descriptor(i);
        if (d == NULL || memcmp(d, &kHandDesc[i], sizeof(kHandDesc[i])) != 0) {
            printf("FAIL descriptor %u\n", i);
            g_failures++;
        } else {
            printf("PASS descriptor %u\n", i);
        }
    }
    check(stunrun_adsp_test_descriptor(STUNRUN_ADSP_TEST_DESC_COUNT) == NULL);

    /* Structure visible in the shipped data: the failure-bit fields use
     * bits 15..0 exactly once, in descending pairs, one pair per entry, and
     * the sum bases step by the $20000 stride each entry's block occupies. */
    {
        unsigned bit, seen = 0u;
        int shape_ok = 1;
        for (i = 0; i < STUNRUN_ADSP_TEST_DESC_COUNT; i++) {
            const stunrun_adsp_test_descriptor_t *d = stunrun_adsp_test_descriptor(i);
            if (d->enable != (i < 6u ? 0x0001u : 0x0000u))
                shape_ok = 0;
            if (d->expect_low != kHandExpectLow[i])
                shape_ok = 0;
            if (d->expect_high != kHandExpectHigh[i])
                shape_ok = 0;
            if (d->fail_bit_low != (uint8_t)(15u - 2u * i))
                shape_ok = 0;
            if (d->fail_bit_high != (uint8_t)(14u - 2u * i))
                shape_ok = 0;
            if (d->sum_base != (uint32_t)i * STUNRUN_ADSP_TEST_BLOCK_STRIDE)
                shape_ok = 0;
            if (d->fail_bit_low < 16u)
                seen |= 1u << d->fail_bit_low;
            if (d->fail_bit_high < 16u)
                seen |= 1u << d->fail_bit_high;
        }
        check(shape_ok);
        check(seen == 0xFFFFu);
        for (bit = 0; bit < 16u; bit++)
            check((seen >> bit & 1u) == 1u);
    }

    /* ---- $60ac-$6176: one descriptor ------------------------------------ */
    /* Case A: a single word (expect_high << 8) | expect_low at base+2, rest
     * zero. First sum = that word, low byte = expect_low. Second sum: the
     * add yields the word, clr.b D0 keeps the high byte, lsr #8 gives
     * expect_high. Both cmp.b match -> no bit set. */
    fake_clear();
    fake_put(0x00000002u, 0x0201u);
    check(stunrun_adsp_board_test_run(&g_board, 0u) == 0x0000u);
    /* A0 advances continuously across the $dbra arm boundary, so each sum is
     * one unbroken +2 walk of the block from +6 and $6128 is the only place
     * it jumps back. Two walks of $10000 words, $20000 reads. */
    check(g_reads == (unsigned long)STUNRUN_ADSP_TEST_READS_PER_ENTRY);
    check(g_run_count == 2u);
    check(g_run_start[0] == 0x00000000u);
    check(g_run_start[1] == 0x00000000u);
    check(g_run_len[0] == (unsigned long)STUNRUN_ADSP_TEST_SUM_WORDS);
    check(g_run_len[1] == (unsigned long)STUNRUN_ADSP_TEST_SUM_WORDS);
    check(g_min_addr == 0x00000000u);
    check(g_max_addr == 0x0001FFFEu);
    check(g_kicks == 6u);

    /* Case B: same, low byte wrong -> fail_bit_low ($0f -> bit 15). */
    fake_clear();
    fake_put(0x00000002u, 0x0202u);
    check(stunrun_adsp_board_test_run(&g_board, 0u) == 0x8000u);

    /* Case C: low byte right, high byte wrong -> fail_bit_high ($0e -> 14). */
    fake_clear();
    fake_put(0x00000002u, 0x0301u);
    check(stunrun_adsp_board_test_run(&g_board, 0u) == 0x4000u);

    /* Case D: both wrong. */
    fake_clear();
    fake_put(0x00000002u, 0x0302u);
    check(stunrun_adsp_board_test_run(&g_board, 0u) == 0xC000u);

    /* Case E: the discriminator. Words 0x02FF then 0x0002, both inside the
     * first $8000-word arm. Plain sum = 0x0301, low byte $01 = expect_low,
     * and plain>>8 = $03 which would MISMATCH expect_high $02. The ROM's
     * second sum clears the low byte after every add, discarding the carry
     * out of $02FF, so D0 is $0200 before the second word and $0200 after:
     * lsr #8 gives $02 = expect_high. A port that used `sum >> 8` here
     * would wrongly raise bit 14. */
    fake_clear();
    fake_put(0x00000002u, 0x02FFu);
    fake_put(0x00000004u, 0x0002u);
    check(stunrun_adsp_board_test_run(&g_board, 0u) == 0x0000u);

    /* Case F: descriptor 1's block holds the word descriptor 1 expects
     * ($0403), so descriptor 1 must pass while descriptor 0, reading only
     * its own block, sees all zeros and raises both its bits. */
    fake_clear();
    fake_put(0x00020002u, 0x0403u);
    check(stunrun_adsp_board_test_run(&g_board, 0u) == 0xC000u);
    fake_clear();
    fake_put(0x00020002u, 0x0403u);
    check(stunrun_adsp_board_test_run(&g_board, 1u) == 0x0000u);
    fake_clear();
    fake_put(0x00020002u, 0x0403u);
    check(stunrun_adsp_board_test_run(&g_board, 2u) == 0x0C00u);

    /* A block that is entirely zero fails both halves of every descriptor
     * (expected sums are all non-zero). */
    fake_clear();
    check(stunrun_adsp_board_test_run(&g_board, 0u) == 0xC000u);
    check(stunrun_adsp_board_test_run(&g_board, 5u) == 0x0030u);

    /* Disabled entries ($60b8 tst.w / $60bc beq) read nothing at all, and
     * entry 7's $6176 divert cannot be reached behind that test. */
    fake_clear();
    check(stunrun_adsp_board_test_run(&g_board, 6u) == 0x0000u);
    check(g_reads == 0ul);
    check(g_kicks == 0u);
    fake_clear();
    check(stunrun_adsp_board_test_run(&g_board, 7u) == 0x0000u);
    check(g_reads == 0ul);
    check(g_kicks == 0u);
    fake_clear();
    check(stunrun_adsp_board_test_run(&g_board, 8u) == 0x0000u);
    check(g_reads == 0ul);
    check(stunrun_adsp_board_test_run(NULL, 0u) == 0x0000u);

    /* ---- $6086-$6172: the whole walk ------------------------------------ */
    /* Every enabled entry holding its own expected sum -> nothing set. */
    fake_clear();
    for (i = 0; i < 6u; i++) {
        const stunrun_adsp_test_descriptor_t *d = stunrun_adsp_test_descriptor(i);
        fake_put(d->sum_base + 2u,
                 (uint16_t)((uint16_t)d->expect_high << 8) | d->expect_low);
    }
    check(stunrun_adsp_board_test_run_all(&g_board) == 0x0000u);
    check(g_reads == 6ul * (unsigned long)STUNRUN_ADSP_TEST_READS_PER_ENTRY);
    check(g_kicks == 36u);
    check(stunrun_adsp_board_test_run_verdict(0x0000u) == 67u);

    /* Only descriptor 0 correct; the other five fail both halves, which is
     * every bit except 15 and 14 (descriptor 0's own pair). */
    fake_clear();
    fake_put(0x00000002u, 0x0201u);
    check(stunrun_adsp_board_test_run_all(&g_board) == 0x3FF0u);
    check(g_reads == 6ul * (unsigned long)STUNRUN_ADSP_TEST_READS_PER_ENTRY);
    check(stunrun_adsp_board_test_run_verdict(0x3FF0u) == 68u);

    /* $6288 tst.w D5 / bne $62ac. */
    check(stunrun_adsp_board_test_run_verdict(0x0001u) == 68u);
    check(stunrun_adsp_board_test_run_verdict(0x8000u) == 68u);
    check(stunrun_adsp_board_test_run_all(NULL) == 0x0000u);

    if (g_failures == 0)
        printf("adsp board test C port: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}