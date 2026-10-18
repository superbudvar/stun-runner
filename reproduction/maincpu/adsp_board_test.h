/* ADSP board-test verdict ($5224-$533c) and the program-ROM self-test
 * driver ($60ac-$6176) that reads the descriptor table at $5ff6, in C.
 *
 * WHAT THIS SLICE COVERS. Three ranges on the 68010 power-on / service path,
 * all transcribed from the main-CPU ROM:
 *
 *   1. $5224-$52a2  the ADSP liveness verdict gate: three gates, then six
 *      scratch bytes, selecting 94 / 97 / 98.
 *   2. $528c-$533c  the Service/START poll the 94 outcome runs into, the
 *      ADSP shared-RAM check behind it (95 / 96), and the $533c tail (99).
 *      NOTE $52ac is NOT downstream of the $524e-$527a byte compares in the
 *      sense of "always": it is reached only through the $528c-$52aa poll,
 *      i.e. only when Service Mode is clear AND START1 ($a80001 bit2) is
 *      set. Both stages are modelled separately for that reason.
 *   3. $60ac-$6176  the program-ROM self-test driver and its eight $12-byte
 *      descriptor table at $5ff6.
 *
 * WHY TWO UNRELATED ROUTINES SHARE A FILE. Both are read here off the same
 * board-test screen: $5202 (entered from $51fa) runs the shared-RAM
 * write/read-back test $56a8, then the ADSP gate, then prints one of the six
 * ADSP strings 93-99; $509c (entered from $5086) runs the program-ROM
 * self-test and prints strings 67-70. The routine that WRITES the $5ff6
 * table's results onto the screen is $556a, which is a different routine and
 * is NOT transcribed here; only the addresses it reads are carried through.
 *
 * Provenance — all CONFIRMED by direct reading of the pinned main-CPU image
 * (/tmp/maincpu_flat1m.bin, de-interleaved 1 MiB, file offset == 68010
 * address). Every address and every literal below was read off the bytes;
 * there is no runtime capture behind any of it, so nothing here is
 * OBSERVED-IN-TRACE.
 *
 *   $51fa  bra $5202          entry to the ADSP branch
 *   $5202  jsr $2326          print the marker string (100 or 101)
 *   $5208  bsr  $56a8         ADSP data-RAM write/read-back; D2 = 0 on pass
 *   $520c  tst.w D2 / beq $5224
 *   $5224  jsr $b864          gate 1 -> D7      ($b8a6..$b8b2)
 *   $5232  jsr $b8b4          gate 2 -> D7      ($b902/$b90a)
 *   $5240  jsr $b916          gate 3 -> D7, and D0..D5  ($b9a6/$b9ac)
 *   $524e-$527a  six cmpi.b against $17 $18 $19 $20 $21 $22, in the register
 *                 order D4,D0,D2,D5,D1,D3
 *   $527e/$528c  the 94 outcome and its continuation: $528c reads $60c001,
 *                 $529c reads $a80001, $52a4 bne $52ac, else $260 / $f94
 *   $52ac  jsr $31dc          post $0101 into the $ff90xx shared RAM
 *   $52b2  move.w #5,D0 / jsr $65e0   clear GSP page 5 (display only)
 *   $52bc-$52f4  six tst.l at $ff9010 $ff9014 $ff9020 $ff9024 $ff9028 $ff9038
 *   $533c  jsr $8f68 / move.w #$63 / jsr $2326  -- the tail the 95/96/97/98
 *          outcomes fall into. The 94 outcome does NOT reach it.
 *   $6086  clr.w D2,D3,D5     self-test entry (reached from $509c)
 *   $60ac  lea $5ff6.l,A1     descriptor-table walk
 *   $60b8  tst.w (A1,D4.w) / $60bc beq $6168   entries 6 and 7 are disabled
 *   $60c0  cmpi.w #$7,D2 / $60c4 beq $6176     behind the tst.w, so dead
 *   $60c8  movea.l ($6,A1,D4.w),A0   block base for both sums
 *   $60d2-$60f4  first sum  : $10000 words, low byte vs +2
 *   $6132-$615a  second sum : $10000 words again ($6128 reloads A0) with
 *                              clr.b D0 after every add, then lsr #8, vs +3
 *   $6288  tst.w D5 -> string 67, else strings 68,69,70
 *
 * CONFIRMED. Reading $b916 settles a question a previous note got wrong: it
 * is NOT a ROM checksum routine. It clears ADSP data-RAM words at DM offsets
 * $0c,$0e,$10,$12,$14,$16, starts the DSP, waits for its IRQ ($7530 bounded),
 * then reads those six words back into D0-D5 and returns $5555 in D7. So the
 * six constants $17..$22 that $524e-$527a compare are the six words THE
 * ADSP PROGRAM WROTE. They are not checksums of anything in the 68010 ROM,
 * and nothing in this file calls them that. (String 98 does read "BAD ADSP
 * ROM CHECKSUM" -- that is the 1989 text, quoted, not a description of the
 * comparison.)
 *
 * The $5ff6 table is likewise NOT an ADSP table: the strings the driver
 * leads to ($6288) are 67 "PROGRAM ROM OK" and 68-70 "BAD PROGRAM ROM IS
 * BLANK BOX" / "ROMS AT 200R AND 210R" / "CONTAIN THE SELF-TEST PROGRAM".
 * It is the blanked-program-ROM check, which is why each descriptor compares
 * a running sum of 128 KiB of ROM against a baked constant.
 *
 * INFERRED (stated, not proven):
 *  - descriptor +$a / +$e. CONFIRMED to be 32-bit values read by $556a and
 *    written straight to the GSP host port ($c00002) as the paint address of
 *    each test's on-screen indicator. That they are "display address, first
 *    sum" and "display address, second sum" is INFERRED from the branch that
 *    pairs +$a with the +4 bit and +$e with the +5 bit. Their VRAM geometry
 *    is UNKNOWN.
 *  - descriptor +4 / +5 as a failure-bit pair. INFERRED but strongly
 *    constrained: all eight descriptors together use bits 15..0 exactly
 *    once, in descending pairs (i -> bits 15-2i and 14-2i), which is only
 *    consistent with one bit per (descriptor, sum) outcome feeding the
 *    `bset D6,D5` / `tst.w D5` mask. Confirmed mechanically; the intent is
 *    inferred.
 *  - descriptor +2 / +3 as expected sum bytes. CONFIRMED mechanically (they
 *    are the `cmp.b` operands). The shipped sequence is 1,2 3,4 5,6 7,8 9,16
 *    17,18 19,20 21,22 -- it JUMPS from 9 to 16, so it is not a simple
 *    2i+1/2i+2 enumeration. The transcription preserves that; what the gap
 *    means is UNKNOWN.
 *
 * DOCUMENTED CHOICES.
 *  - $6176, the `cmpi.w #$7,D2` target, re-sums the board's $e0000/$e8000
 *    window through $630a instead of the +6 block. It is NOT transcribed
 *    here, and with the shipped table it is unreachable: $60b8's `tst.w
 *    (A1,D4.w)` is tested before $60c0's `cmpi.w`, and descriptor 7's +0 word
 *    is 0. Entries 6 and 7 are likewise disabled ($0100 vs $0000), so only
 *    six of the eight are exercised. See stunrun_adsp_board_test_run().
 *  - board_iface has no main-RAM accessor, so the six $ff90xx longs are
 *    supplied by the caller rather than read through the board contract.
 *    The order is fixed by stunrun_adsp_board_ram_address().
 *
 * ROM DATA EMBEDDED: the eight $5ff6 descriptors, transcribed literally.
 * That is the deliverable, so unlike its siblings this module does embed
 * ROM bytes -- as constants in one table, not as a captured buffer.
 */
#ifndef STUNRUN_ADSP_BOARD_TEST_H
#define STUNRUN_ADSP_BOARD_TEST_H

#include <stddef.h>
#include <stdint.h>

#include "board_iface.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- string indices ($25e8/$2326 draw from the pointer table at $10136) --- */

/* Printed by $5224-$52a2 and $52ac-$533c. */
#define STUNRUN_ADSP_STRING_TIMED        94u  /* "ADSP TEST: 35 SECS - 3 MINS" */
#define STUNRUN_ADSP_STRING_BOARD_OK     95u  /* "ADSP BOARD OK"                */
#define STUNRUN_ADSP_STRING_BAD_RAM      96u  /* "BAD ADSP RAM"                 */
#define STUNRUN_ADSP_STRING_NO_RESPONSE  97u  /* "ADSP-2100 DOES NOT RESPOND"   */
#define STUNRUN_ADSP_STRING_BAD_SCRATCH  98u  /* "BAD ADSP ROM CHECKSUM" (1989) */
#define STUNRUN_ADSP_STRING_PRESS_START  99u  /* "PRESS START TO SELECT TEST MENU" */

/* Printed by the $60ac driver's own verdict at $6288-$62d8. */
#define STUNRUN_ADSP_STRING_ROM_OK       67u  /* "PROGRAM ROM OK"                */
#define STUNRUN_ADSP_STRING_ROM_BLANK    68u  /* "BAD PROGRAM ROM IS BLANK BOX"  */
#define STUNRUN_ADSP_STRING_ROM_200R     69u  /* "ROMS AT 200R AND 210R"         */
#define STUNRUN_ADSP_STRING_ROM_SELFTEST 70u  /* "CONTAIN THE SELF-TEST PROGRAM" */

/* The six words $b916 reads out of ADSP data RAM into D0-D5, as the low
 * bytes $524e-$527a compare them. These are what the ADSP program wrote. */
#define STUNRUN_ADSP_SCRATCH_D0 0x18u
#define STUNRUN_ADSP_SCRATCH_D1 0x21u
#define STUNRUN_ADSP_SCRATCH_D2 0x19u
#define STUNRUN_ADSP_SCRATCH_D3 0x22u
#define STUNRUN_ADSP_SCRATCH_D4 0x17u
#define STUNRUN_ADSP_SCRATCH_D5 0x20u
#define STUNRUN_ADSP_SCRATCH_COUNT 6u

/* The six ADSP shared-RAM longs $52bc-$52f4 tests, in ROM order. $31dc has
 * just posted $0101 into a word of each, so a non-zero long means the DSP
 * did not clear the pair. */
#define STUNRUN_ADSP_RAM_CHECK_COUNT 6u
uint32_t stunrun_adsp_board_ram_address(unsigned index);
const uint32_t *stunrun_adsp_board_ram_addresses(void);

/* Outcomes of the three liveness gates. Each field is the D7 the gate
 * returned; the ROM only ever asks `cmpi.w #$5555,D7`. scratch[] is D0..D5
 * low bytes in register order (index 0 is D0), NOT in compare order -- the
 * ROM's compare order is D4,D0,D2,D5,D1,D3 and lives in the .c. */
typedef struct stunrun_adsp_gates {
    uint16_t loopback_d7; /* $b864: wrote $1234 to DM $06, read DM $0a back  */
    uint16_t irq_d7;      /* $b8b4: DSP raised IRQ before the $1770 bound     */
    uint16_t scratch_d7;  /* $b916: DSP raised IRQ, then D0..D5 were filled   */
    uint8_t  scratch[STUNRUN_ADSP_SCRATCH_COUNT];
} stunrun_adsp_gates_t;

/* $5224-$528a, the first-stage verdict. Exactly one of
 *   97  some gate's D7 was not $5555        ($52a2/$52e bne $530a)
 *   98  all gates passed, a scratch byte differs ($5252..$527a bne $531c)
 *   94  all gates passed and all six bytes match  ($527e)
 * It reads no shared RAM. NULL reads as a dead DSP (97). */
uint16_t stunrun_adsp_board_test_select(const stunrun_adsp_gates_t *gates);

/* The six cmpi.b of $524e-$527a on their own: nonzero when every scratch word
 * matches. NULL reads as no match. */
int stunrun_adsp_board_test_scratch_match(const stunrun_adsp_gates_t *gates);

/* $528c-$52aa, the poll the 94 outcome's continuation runs before anything
 * else: bit5 of $60c001 (SELF-TEST, active low) is tested first, then bit2
 * of $a80001 (START1). Returns nonzero only when control reaches $52ac --
 * i.e. Service Mode CLEAR and START1 SET. Otherwise the ROM leaves for
 * $260 or $f94 and the shared-RAM check never runs. */
int stunrun_adsp_board_test_continue(int service_mode_pressed,
                                     int start1_pressed);

/* $52ac-$5300, the second-stage verdict, reached only as above: 96 if any
 * of the six longs is non-zero ($52c2..$52f4 bne $532e), else 95. NULL
 * reads as 96. */
uint16_t stunrun_adsp_board_test_select_ram(
    const uint32_t ram[STUNRUN_ADSP_RAM_CHECK_COUNT]);

/* $533c: the string index the screen ends on. 99 for 95, 96, 97 and 98 --
 * every outcome that reaches $533c -- and 0 for 94, whose path detours at
 * $528c instead. */
uint16_t stunrun_adsp_board_test_followup(uint16_t verdict);

/* The whole screen in order, for callers that want the sequence rather than
 * one step. Writes up to THREE indices to `out` -- the verdict, then either
 * the second-stage verdict or the $533c tail, or both -- and returns how
 * many. `out` must have room for three; NULL yields 0. */
unsigned stunrun_adsp_board_test_screen(const stunrun_adsp_gates_t *gates,
                                        const uint32_t ram[STUNRUN_ADSP_RAM_CHECK_COUNT],
                                        int service_mode_pressed,
                                        int start1_pressed, uint16_t *out);

/* The 1989 text behind a string index, or NULL if this module does not carry
 * it. Only the indices listed above are transcribed. */
const char *stunrun_adsp_board_string(unsigned index);

/* ---- $5ff6 descriptor table ---------------------------------------------- */

#define STUNRUN_ADSP_TEST_DESC_BASE  0x00005FF6u
#define STUNRUN_ADSP_TEST_DESC_SIZE  0x12u
#define STUNRUN_ADSP_TEST_DESC_COUNT 8u

/* Descriptor 7 is the one $60c0 diverts to $6176. Unreachable with the
 * shipped table; see the header's DOCUMENTED CHOICE note. */
#define STUNRUN_ADSP_TEST_DESC_ALT 7u

/* $8000 words per dbra arm, two arms per sum, and each sum restarts the
 * block from +6 ($6128 reloads A0), so one entry walks its $10000-word
 * block twice: $20000 words read. */
#define STUNRUN_ADSP_TEST_SUM_HALF_WORDS 0x8000u
#define STUNRUN_ADSP_TEST_SUM_WORDS      0x10000u
#define STUNRUN_ADSP_TEST_READS_PER_ENTRY 0x20000u
#define STUNRUN_ADSP_TEST_BLOCK_STRIDE    0x20000u

typedef struct stunrun_adsp_test_descriptor {
    uint16_t enable;        /* +0  word: $60b8 tst.w -- 0 skips the entry    */
    uint8_t  expect_low;    /* +2  byte: $60f4 cmp.b against the first sum's
                             *        low byte                              */
    uint8_t  expect_high;   /* +3  byte: $615a cmp.b against the second sum  */
    uint8_t  fail_bit_low;  /* +4  byte: $60fc bset D6,D5 on a first-sum miss */
    uint8_t  fail_bit_high; /* +5  byte: $6162 bset D6,D5 on a second-sum miss*/
    uint32_t sum_base;      /* +6  long: $60c8 A0, base of both sums         */
    uint32_t paint_low;     /* +$a long: $557e GSP paint address, first sum  */
    uint32_t paint_high;    /* +$e long: $55a2 GSP paint address, second sum */
} stunrun_adsp_test_descriptor_t;

const stunrun_adsp_test_descriptor_t *stunrun_adsp_test_descriptor(
    unsigned index);

/* $60ac-$616e for one descriptor. Returns that entry's contribution to the
 * D5 failure mask (0 when it passes, and 0 when the entry is disabled and
 * nothing is read at all). Reads $10000 words through read_rom16 per sum and
 * kicks the watchdog six times, exactly as the ROM does. */
uint16_t stunrun_adsp_board_test_run(stunrun_board_t *board, unsigned index);

/* $6086-$6172: the whole eight-entry walk, returning the accumulated D5 mask.
 * A NULL board returns 0 without reading (the shipped table's enabled
 * entries are the only ones that touch the bus). */
uint16_t stunrun_adsp_board_test_run_all(stunrun_board_t *board);

/* $6288-$62d8: what a D5 mask leads to -- 67 when the mask is zero, else 68
 * (69 and 70 follow from $62c2/$62d0 in the same run). */
uint16_t stunrun_adsp_board_test_run_verdict(uint16_t fail_mask);

#ifdef __cplusplus
}
#endif

#endif /* STUNRUN_ADSP_BOARD_TEST_H */