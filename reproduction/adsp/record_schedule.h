/* Bounded transcription of the captured ADSP $0174-$01A9 record schedule.
 *
 * $0174 is a CALL entry (the sole `CALL $0174` in the program image is at
 * $00AB) that walks a stride-2 pointer chain out of the object ROM, reads two
 * words per pass, and latches a record start. This header models ONLY the
 * DM-visible effects of $0174-$01A7 plus the $0186 indirect jump that ends the
 * walk, because those are the addresses whose side effects the frame-551
 * capture holds at every pass.
 *
 * What is deliberately NOT here:
 *   - $01A8 `CALL $01E8` and $01A9 `IF GE JUMP $01E4`. The E8 flags that $01A9
 *     tests are produced by a routine this slice does not model, so the
 *     decision is UNKNOWN and this slice returns after each record instead of
 *     pretending to know it. In the capture that branch was never taken.
 *   - Everything at or beyond $01DF (the loop back-edge is at $01E1
 *     `IF EQ JUMP $017A`, i.e. the *caller's* count-down logic), $01E4's
 *     SIMBUF drain, and the $0236/$01D0 writes that later overwrite
 *     DM($0036). `dm0036_final` here is the value $01A0 wrote, not the value
 *     DM($0036) holds once the rest of the routine has run.
 *
 * No caller semantics, no object identity and no meaning for any of the words
 * are claimed. Every number is a number: the start is the word $017F read,
 * the two fields are the masks $019E and $01A3 apply.
 */
#ifndef STUNRUN_ADSP_RECORD_SCHEDULE_H
#define STUNRUN_ADSP_RECORD_SCHEDULE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* $0183-$0186 index a 16-entry table based at $0187 with SI's high nibble, so
 * a walk cannot exceed this. The capture's chain uses ten passes; 32 is a
 * bound, not an observed count. */
#define STUNRUN_ADSP_RECORD_SCHEDULE_MAX_PASSES 32u

/* The two jump-table indices the oracle exercised: 0 reaches the RTS at $0187
 * and ends the walk, 15 reaches the JUMP at $0196 and continues it. Every other
 * index lands on $0188 (which writes DM($0038)) or on $0189-$0195 (NOPs into
 * $0196). Those were never executed by the capture, so they are rejected
 * rather than transcribed -- see docs/mc68010-boot-findings.md sec. 98. */
#define STUNRUN_ADSP_RECORD_SCHEDULE_EXIT_INDEX 0u
#define STUNRUN_ADSP_RECORD_SCHEDULE_CONTINUE_INDEX 15u

typedef struct stunrun_adsp_record_schedule_pass {
    /* $017A AY1 = DM($0034); $017B DM($2001) = AY1. */
    uint16_t dm2001_latched;
    /* $017F AX1 = DM($2000): the object-ROM word index the SIMBUF port read,
     * and the value it returned. */
    uint32_t start_word_index;
    uint16_t start;
    /* $0182 SI = DM($2000): the next word of the same pass. */
    uint32_t command_word_index;
    uint16_t command;
    /* $0183-$0186 SI >> 12, the index the JUMP (I4) at $0186 used. */
    uint8_t jump_index;
    /* $017E DM($0038) = L1. Written on EVERY pass, including the terminating
     * one, so it is listed before the $0199-or-later group rather than inside
     * it. Zero on every modelled path; the field exists so the test asserts
     * this slice's value rather than a constant it supplies itself. */
    uint16_t dm0038;
    /* $019B DM($0034) = AY1 + 2, and $01A0/$01A6/$01A7 below, are written only
     * on a pass that took the $0196 join. On the terminating pass every one of
     * these is zero and produced_record is 0. */
    uint16_t dm0034_after;
    uint16_t dm0036;                 /* $01A0 DM($0036) = SI AND $003F */
    uint16_t dm0035;                 /* $01A6 DM($0035) = ((SI>>6)&$3F), or 1
                                     * when that mask is zero ($01A4) */
    uint16_t record_start;           /* $01A7 DM($2001) = AX1 */
    int produced_record;
} stunrun_adsp_record_schedule_pass_t;

typedef struct stunrun_adsp_record_schedule {
    uint16_t entry_dm0034;           /* the $017A read on the first pass */
    size_t pass_count;               /* loop passes executed, including the
                                     * one that ended the walk */
    size_t record_count;             /* passes that reached $01A7 */
    /* DM state as $0187's RTS leaves it. DM($0036) and DM($0035) hold the last
     * record's values: the terminating pass does not rewrite them. */
    uint16_t dm2001_final;
    /* NOTE: dm2001_final and dm0034_final are equal on every accepted path.
     * $017B copies DM($0034) into DM($2001) at the top of each pass and the only
     * exit is index 0, which returns before $019B advances DM($0034). No input
     * separates them, so a test cannot distinguish them either; they are kept as
     * separate fields because they name different registers, and the pair pins
     * that both were left on the terminating pointer rather than on the last
     * record's start. */
    uint16_t dm0034_final;
    uint16_t dm0036_final;
    uint16_t dm0035_final;
    uint16_t dm0038_final;
    /* Object-ROM word index the auto-incrementing SIMBUF port would read next.
     * Two words past the last pass's command word; note this is *ahead* of
     * dm0034_final whenever the walk ended at $0187. */
    uint32_t next_simbuf_word_index;
    stunrun_adsp_record_schedule_pass_t
        passes[STUNRUN_ADSP_RECORD_SCHEDULE_MAX_PASSES];
} stunrun_adsp_record_schedule_t;

/* Walk the record schedule from entry_dm0034.
 *
 * rom_words/rom_word_count/rom_base_word are a bounded absolute-address view
 * of the word-addressed object ROM; mp must be 1 for this captured path, and
 * MP supplies the $10000-word bank base the SIMBUF port reads at
 * (`m_adsp_eprom_base = 0x10000 * MP`, `harddriv_m.cpp:896`). entry_dm0034 is
 * the /MP=1 SIM address the routine found in DM($0034); its own origin is a
 * SIMBUF read at $0172 and is out of scope, so it stays an input. It is taken
 * as uint32_t so a caller can hand over an address outside the /MP=1 bank and
 * be told so rather than having it silently truncated.
 *
 * Returns 1 on success. Returns 0 and leaves *out untouched for a NULL rom or
 * out, mp != 1, a zero-length ROM window, an entry address outside the /MP=1
 * bank, any read outside the supplied window, a walk longer than
 * STUNRUN_ADSP_RECORD_SCHEDULE_MAX_PASSES, or a jump-table index / exit the
 * capture never exercised (an index other than 0 or 15, or the $019F exit). */
int stunrun_adsp_record_schedule_run(
    const uint16_t *rom_words, size_t rom_word_count, uint32_t rom_base_word,
    uint16_t mp, uint32_t entry_dm0034,
    stunrun_adsp_record_schedule_t *out);

#ifdef __cplusplus
}
#endif

#endif /* STUNRUN_ADSP_RECORD_SCHEDULE_H */