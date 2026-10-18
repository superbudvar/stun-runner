/* See adsp_board_test.h for provenance and confidence notes. */
#include "adsp_board_test.h"

/* ---- $5ff6 descriptor table, transcribed ------------------------------- */

static const stunrun_adsp_test_descriptor_t kDescriptors[STUNRUN_ADSP_TEST_DESC_COUNT] = {
    /* $5ff6 */ { 0x0001u, 0x01u, 0x02u, 0x0Fu, 0x0Eu, 0x00000000u, 0xFF8285C0u, 0xFF81E5C0u },
    /* $6008 */ { 0x0001u, 0x03u, 0x04u, 0x0Du, 0x0Cu, 0x00020000u, 0xFF828540u, 0xFF81E540u },
    /* $601a */ { 0x0001u, 0x05u, 0x06u, 0x0Bu, 0x0Au, 0x00040000u, 0xFF8284C0u, 0xFF81E4C0u },
    /* $602c */ { 0x0001u, 0x07u, 0x08u, 0x09u, 0x08u, 0x00060000u, 0xFF828440u, 0xFF81E440u },
    /* $603e */ { 0x0001u, 0x09u, 0x10u, 0x07u, 0x06u, 0x00080000u, 0xFF8283C0u, 0xFF81E3C0u },
    /* $6050 */ { 0x0001u, 0x11u, 0x12u, 0x05u, 0x04u, 0x000A0000u, 0xFF828340u, 0xFF81E340u },
    /* $6062 */ { 0x0000u, 0x13u, 0x14u, 0x03u, 0x02u, 0x000C0000u, 0xFF8282C0u, 0xFF81E2C0u },
    /* $6074 */ { 0x0000u, 0x15u, 0x16u, 0x01u, 0x00u, 0x000E0000u, 0xFF828240u, 0xFF81E240u },
};

/* $52bc-$52f4: tst.l $ff9010 / $ff9014 / $ff9020 / $ff9024 / $ff9028 / $ff9038.
 * Not an ascending run -- $ff9028 is followed by $ff9038, skipping
 * $ff902c-$ff9034 -- which is why the order is carried by an array. */
static const uint32_t kRamAddresses[STUNRUN_ADSP_RAM_CHECK_COUNT] = {
    0x00FF9010u, 0x00FF9014u, 0x00FF9020u,
    0x00FF9024u, 0x00FF9028u, 0x00FF9038u,
};

/* $524e-$527a compare in register order D4,D0,D2,D5,D1,D3 against
 * $17,$18,$19,$20,$21,$22. Registers[] is that order over scratch[0..5],
 * where scratch[] is D0..D5. */
static const unsigned kScratchRegister[STUNRUN_ADSP_SCRATCH_COUNT] = { 4, 0, 2, 5, 1, 3 };

static const uint8_t kScratchExpected[STUNRUN_ADSP_SCRATCH_COUNT] = {
    STUNRUN_ADSP_SCRATCH_D4, STUNRUN_ADSP_SCRATCH_D0, STUNRUN_ADSP_SCRATCH_D2,
    STUNRUN_ADSP_SCRATCH_D5, STUNRUN_ADSP_SCRATCH_D1, STUNRUN_ADSP_SCRATCH_D3,
};

uint32_t stunrun_adsp_board_ram_address(unsigned index)
{
    if (index >= STUNRUN_ADSP_RAM_CHECK_COUNT)
        return 0u;
    return kRamAddresses[index];
}

const uint32_t *stunrun_adsp_board_ram_addresses(void)
{
    return kRamAddresses;
}

const stunrun_adsp_test_descriptor_t *stunrun_adsp_test_descriptor(unsigned index)
{
    if (index >= STUNRUN_ADSP_TEST_DESC_COUNT)
        return NULL;
    return &kDescriptors[index];
}

const char *stunrun_adsp_board_string(unsigned index)
{
    switch (index) {
    case STUNRUN_ADSP_STRING_ROM_OK:       return "PROGRAM ROM OK";
    case STUNRUN_ADSP_STRING_ROM_BLANK:    return "BAD PROGRAM ROM IS BLANK BOX";
    case STUNRUN_ADSP_STRING_ROM_200R:     return "ROMS AT 200R AND 210R";
    case STUNRUN_ADSP_STRING_ROM_SELFTEST: return "CONTAIN THE SELF-TEST PROGRAM";
    case 93u:                              return "ADSP BOARD DOES NOT RESPOND";
    case STUNRUN_ADSP_STRING_TIMED:        return "ADSP TEST: 35 SECS - 3 MINS";
    case STUNRUN_ADSP_STRING_BOARD_OK:     return "ADSP BOARD OK";
    case STUNRUN_ADSP_STRING_BAD_RAM:      return "BAD ADSP RAM";
    case STUNRUN_ADSP_STRING_NO_RESPONSE:  return "ADSP-2100 DOES NOT RESPOND";
    case STUNRUN_ADSP_STRING_BAD_SCRATCH:  return "BAD ADSP ROM CHECKSUM";
    case STUNRUN_ADSP_STRING_PRESS_START:  return "PRESS START TO SELECT TEST MENU";
    default:                               return NULL;
    }
}

/* ---- $5224-$533c ------------------------------------------------------- */

int stunrun_adsp_board_test_scratch_match(const stunrun_adsp_gates_t *gates)
{
    unsigned i;

    if (gates == NULL)
        return 0;
    for (i = 0; i < STUNRUN_ADSP_SCRATCH_COUNT; i++) {
        /* cmpi.b -- only the low byte of the data-RAM word is compared. */
        if (gates->scratch[kScratchRegister[i]] != kScratchExpected[i])
            return 0;
    }
    return 1;
}

uint16_t stunrun_adsp_board_test_select(const stunrun_adsp_gates_t *gates)
{
    if (gates == NULL)
        return STUNRUN_ADSP_STRING_NO_RESPONSE;
    /* Three cmpi.w #$5555,D7, each branching to $530a -> string 97. */
    if (gates->loopback_d7 != STUNRUN_ADSP_PASS_WORD)
        return STUNRUN_ADSP_STRING_NO_RESPONSE;
    if (gates->irq_d7 != STUNRUN_ADSP_PASS_WORD)
        return STUNRUN_ADSP_STRING_NO_RESPONSE;
    if (gates->scratch_d7 != STUNRUN_ADSP_PASS_WORD)
        return STUNRUN_ADSP_STRING_NO_RESPONSE;
    /* Six cmpi.b, any mismatch branching to $531c -> string 98. */
    if (!stunrun_adsp_board_test_scratch_match(gates))
        return STUNRUN_ADSP_STRING_BAD_SCRATCH;
    return STUNRUN_ADSP_STRING_TIMED;
}

int stunrun_adsp_board_test_continue(int service_mode_pressed,
                                     int start1_pressed)
{
    /* $5294-$5296: SELF-TEST clear means Service Mode is pressed, and the
     * ROM leaves for $260 before it ever looks at START1. */
    if (service_mode_pressed)
        return 0;
    /* $52a4 bne $52ac, else $52a6 jmp $f94.l. */
    return start1_pressed ? 1 : 0;
}

uint16_t stunrun_adsp_board_test_select_ram(
    const uint32_t ram[STUNRUN_ADSP_RAM_CHECK_COUNT])
{
    unsigned i;

    if (ram == NULL)
        return STUNRUN_ADSP_STRING_BAD_RAM;
    for (i = 0; i < STUNRUN_ADSP_RAM_CHECK_COUNT; i++) {
        /* tst.l $ff90xx / bne $532e -- first non-zero long short-circuits. */
        if (ram[i] != 0u)
            return STUNRUN_ADSP_STRING_BAD_RAM;
    }
    return STUNRUN_ADSP_STRING_BOARD_OK;
}

uint16_t stunrun_adsp_board_test_followup(uint16_t verdict)
{
    /* $5220, $5318 and $532a all `bra $533c`, and so does $5306 after
     * 95/96. $533c prints 99 -- but the 94 outcome never gets there: its
     * continuation is the $528c poll. */
    if (verdict == STUNRUN_ADSP_STRING_TIMED)
        return 0u;
    return STUNRUN_ADSP_STRING_PRESS_START;
}

unsigned stunrun_adsp_board_test_screen(const stunrun_adsp_gates_t *gates,
                                        const uint32_t ram[STUNRUN_ADSP_RAM_CHECK_COUNT],
                                        int service_mode_pressed,
                                        int start1_pressed, uint16_t *out)
{
    uint16_t verdict = stunrun_adsp_board_test_select(gates);
    unsigned n = 0;

    if (out == NULL)
        return 0u;
    out[n++] = verdict;
    if (verdict == STUNRUN_ADSP_STRING_TIMED) {
        /* $528c-$52aa diverts to $260 or $f94 and the screen ends there --
         * no $533c, no 99. */
        if (!stunrun_adsp_board_test_continue(service_mode_pressed,
                                              start1_pressed))
            return n;
        /* $52f8 (all six longs clear) or $532e (one is not); both fall
         * through to $5306 `bra $533c`. */
        out[n++] = stunrun_adsp_board_test_select_ram(ram);
    }
    out[n++] = STUNRUN_ADSP_STRING_PRESS_START;
    return n;
}

/* ---- $60ac-$6176 ------------------------------------------------------- */

/* $60d2-$60da: D0 = 0, then $8000 times `add.w (A0)+,D0`. dbra with $7fff
 * counts $8000 iterations. */
static uint16_t stunrun_adsp_sum_plain(stunrun_board_t *board, uint32_t base,
                                       uint16_t seed)
{
    uint32_t addr = base;
    uint16_t d0 = seed;
    unsigned k;

    for (k = 0; k < STUNRUN_ADSP_TEST_SUM_HALF_WORDS; k++) {
        d0 = (uint16_t)(d0 + stunrun_b_rom16(board, addr));
        addr += 2u;
    }
    return d0;
}

/* $6138-$613c: one $8000-word arm of the second sum. `clr.b D0` after every
 * add means only the high byte survives, and the carry thrown out of each
 * word's low byte is discarded rather than kept -- so this is NOT simply
 * `sum >> 8`. D0 is threaded across the two arms because the ROM does not
 * re-clear it at $6146; only the watchdog write sits between them. */
static uint16_t stunrun_adsp_sum_high_arm(stunrun_board_t *board, uint32_t addr,
                                          uint16_t d0)
{
    unsigned k;

    for (k = 0; k < STUNRUN_ADSP_TEST_SUM_HALF_WORDS; k++) {
        d0 = (uint16_t)(d0 + stunrun_b_rom16(board, addr));
        d0 = (uint16_t)(d0 & 0xFF00u);
        addr += 2u;
    }
    return d0;
}

uint16_t stunrun_adsp_board_test_run(stunrun_board_t *board, unsigned index)
{
    const stunrun_adsp_test_descriptor_t *d = stunrun_adsp_test_descriptor(index);
    uint16_t mask = 0u;

    if (d == NULL || board == NULL)
        return 0u;
    /* $60b8 tst.w (A1,D4.w) / $60bc beq $6168: a disabled entry reads nothing. */
    if (d->enable == 0u)
        return 0u;
    if (index == STUNRUN_ADSP_TEST_DESC_ALT) {
        /* $60c0-$60c4: descriptor 7 diverts to $6176, which sums the board's
         * $e0000/$e8000 window through $630a instead of the +6 block. That
         * alternate path is deliberately not transcribed. It cannot be
         * reached from $608c with the shipped table, because $60b8's tst.w
         * is tested first and this entry's +0 word is 0 -- so the branch
         * above is dead and returning 0 here observes nothing. */
        return 0u;
    }

    /* First sum: two $8000-word arms over the +6 block, plain 16-bit add.
     * $60d2 clr.w D0 seeds the first arm; A0 advances $8000 words ($10000
     * bytes) and the second arm carries the same accumulator on from there,
     * so the whole sum is one unbroken +2 walk of $10000 words. Note that
     * `2 * STUNRUN_ADSP_TEST_SUM_HALF_WORDS` is that arm's length in BYTES
     * (0x8000 words x 2), which is why it can be added to an address. */
    stunrun_b_watchdog(board);                     /* $60cc */
    {
        uint16_t d0 = stunrun_adsp_sum_plain(board, d->sum_base, 0u);
        stunrun_b_watchdog(board);                 /* $60de */
        d0 = stunrun_adsp_sum_plain(board,
                                    d->sum_base +
                                        2u * STUNRUN_ADSP_TEST_SUM_HALF_WORDS,
                                    d0);
        stunrun_b_watchdog(board);                 /* $60ee */
        /* $60f4 cmp.b ($2,A1,D4.w),D0 / $60f8 beq $6102 */
        if ((uint8_t)d0 != d->expect_low) {
            mask |= (uint16_t)(1u << d->fail_bit_low);  /* $60fc/$6100 bset */
        }
    }

    /* Second sum: $6128 reloads A0 from +6, so this walks the same
     * $10000-word block a second time. */
    {
        uint16_t d0 = 0u;                          /* $6132 clr.w D0 */
        stunrun_b_watchdog(board);                 /* $612c */
        d0 = stunrun_adsp_sum_high_arm(board, d->sum_base, d0);
        stunrun_b_watchdog(board);                 /* $6140 */
        d0 = stunrun_adsp_sum_high_arm(
                 board,
                 d->sum_base + 2u * STUNRUN_ADSP_TEST_SUM_HALF_WORDS, d0);
        stunrun_b_watchdog(board);                 /* $6154 */
        d0 = (uint16_t)(d0 >> 8);                  /* $6152 lsr.w #8,D0 */
        /* $615a cmp.b ($3,A1,D4.w),D0 / $615e beq $6168 */
        if (d0 != d->expect_high) {
            mask |= (uint16_t)(1u << d->fail_bit_high); /* $6162/$6166 bset */
        }
    }
    return mask;
}

uint16_t stunrun_adsp_board_test_run_all(stunrun_board_t *board)
{
    uint16_t mask = 0u;
    unsigned i;

    if (board == NULL)
        return 0u;
    /* $6168-$616e: addq.w #1,D2 / cmpi.w #$8,D2 / bne $608c, D2 from 0. */
    for (i = 0; i < STUNRUN_ADSP_TEST_DESC_COUNT; i++)
        mask |= stunrun_adsp_board_test_run(board, i);
    return mask;
}

uint16_t stunrun_adsp_board_test_run_verdict(uint16_t fail_mask)
{
    /* $6288-$6292: tst.w D5 / bne $62ac. */
    if (fail_mask == 0u)
        return STUNRUN_ADSP_STRING_ROM_OK;
    return STUNRUN_ADSP_STRING_ROM_BLANK;
}