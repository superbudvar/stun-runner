/* ADSP-2100 index/modifier walk that produces a SOM record's spans.
 *
 * This transcribes $0564-$05D1 of the SOM record emitter: the two header reads,
 * then the span walk that merges the two vertex chains until their pointers
 * meet. The span VALUES (the two 32-bit interpolated side pairs) are not
 * computed here -- they are what $05DD/$0607 leave in DM, owned by som_math.c
 * and filled in by the caller.
 *
 * The addressing is not "index + modifier". The pinned MAME source
 * (src/devices/cpu/adsp2100/2100ops.hxx, data_read_dag1/data_read_dag2) reads
 * at the CURRENT index and then adds the modifier, wrapping inside the L
 * register's circular window:
 *
 *     base = index & lmask(length)
 *     value = dm[index]
 *     index = (index + modifier) & 0x3fff
 *     if (index < base)             index += length
 *     else if (index >= base+length) index -= length
 *
 * and `base` is recomputed whenever the register is written. That circular wrap
 * is load-bearing here: the vertex data is a six-word ring, and without it the
 * walk runs off the end of the buffer. It is what makes records 4 and 6 take
 * the back edge at $056A.
 *
 * See docs/mc68010-boot-findings.md section 67.
 */
#ifndef STUNRUN_ADSP_SOM_WALK_H
#define STUNRUN_ADSP_SOM_WALK_H

#include <stddef.h>
#include <stdint.h>

#include "som_math.h"
#include "som_record.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Modifiers in force while the emitter runs, all set by $0548-$054F. */
#define STUNRUN_ADSP_M_HDR    1    /* M1/M2 at $0564/$0566 */
#define STUNRUN_ADSP_M_FWD    2    /* M0, and M2 in the $058F/$05C9 tails */
#define STUNRUN_ADSP_M_A1     0    /* M3 at $056A/$0592/$05CC */
#define STUNRUN_ADSP_M_BACK  -2    /* M4 at $056D/$0597/$05B1 */
#define STUNRUN_ADSP_M_A0     0    /* M5 at $056E/$0598/$05B2 */

/* MAME's m_mask_table, keyed by the L register value. */
uint16_t stunrun_adsp_lmask(uint16_t length);

/* One index register with its circular window. */
typedef struct stunrun_adsp_index {
    uint16_t index;
    uint16_t base;
    uint16_t length;
} stunrun_adsp_index_t;

/* Register write: $0568 `I0 = I2`, $056C `I4 = I1`, and every plain MODIFY. */
void stunrun_adsp_index_write(stunrun_adsp_index_t *reg, uint16_t value);

/* `MODIFY (Ix,Mx)` with no memory access. */
void stunrun_adsp_index_modify(stunrun_adsp_index_t *reg, int modifier);

typedef struct stunrun_adsp_som_walk {
    uint16_t start_x;   /* $0565, DM(I1,M1) */
    uint16_t delta_x;   /* $0567, DM(I2,M2) */
    uint16_t start_y;   /* $056B, AX1 = DM(I0,M0); also the first span's AX1 */
    size_t span_count;
    int terminated;     /* 1 when the walk reached the $05D1 terminator,
                         * by whichever exit: the pointers meeting at
                         * $058E or a guard failing at $0594/$059A/
                         * $05B4/$05CE */
} stunrun_adsp_som_walk_t;

/* Walk one record's spans, including the two interpolated side values.
 *
 * dm is the ADSP data-memory window, addressed by absolute DM address:
 * DM(a) is dm[a - dm_base]. Reading outside the window fails rather than
 * inventing geometry, because a truncated capture would otherwise produce
 * plausible-looking wrong output.
 *
 * program_words is the ADSP program memory holding the coefficient table that
 * $05DD/$0607 index at $1036; the words are MAME's 32-bit containers for
 * 24-bit PM values, and the helper takes bits 23:8. The captured table is
 * floor($8000/n) for n = 1..$40 and zero at n = 0, identical at frames 551
 * and 600 -- see docs/mc68010-boot-findings.md section 68.
 *
 * i1 and i2 are the index register values as observed at $0564, i.e. after any
 * $0563 `MODIFY (I2,M0)`. length is the L register ($0006 in the capture).
 *
 * Each span's side values come from the two helpers: $05DD is entered with
 * AR = AY1 - AX1 and the forward chain's pointers (I0, I2); $0607 with
 * AR = AY0 - AX1 and the backward chain's (I4, I1). The ADSP calls them at
 * $0575/$0578 and then refreshes whichever side a loop tail advanced, so a
 * span that only advanced one side reuses the other side's previous result --
 * transcribed here rather than recomputed.
 *
 * Returns 1 on success. Returns 0, writing nothing, when an argument is NULL,
 * when a DM address leaves the window, when a helper declines its selector
 * (the > $40 scaled branch is not implemented), or when the walk needs more
 * than span_capacity spans. */
int stunrun_adsp_som_walk_record(
    const uint16_t *dm, size_t dm_word_count, uint16_t dm_base,
    const uint32_t *program_words, size_t program_word_count,
    uint16_t i1, uint16_t i2, uint16_t length,
    stunrun_adsp_som_span_t *spans, size_t span_capacity,
    stunrun_adsp_som_walk_t *out);

/* Addresses the emitter uses before the walk, all ROM constants: $0514 reads
 * the control word, $051F seeds the scan at $0040, and $0526/$052E both read
 * the same word for the ring length and the loop count. */
#define STUNRUN_ADSP_SOM_CONTROL_ADDR 0x0036u
#define STUNRUN_ADSP_SOM_CNTR_ADDR    0x0039u
#define STUNRUN_ADSP_SOM_SCAN_BASE    0x0040u

/* $0526-$0544: pick the two chain starting pointers by walking the ring.
 *
 * $0529 derives the ring length L = 2 * cntr and $052E loads the same word
 * into the loop counter, so both come from DM($0039). The walk advances I0
 * with M0 = +1, one word at a time, and keeps I1/I2 on whichever chain tracks
 * the smaller value at each step.
 *
 * cntr is the caller's DM($0039). cntr below 2 is rejected rather than guessed:
 * the capture only exercises cntr = 3, and a ring that short has no
 * meaningful wrap.
 *
 * Returns 1 on success, writing *ring_length with 2 * cntr. Returns 0 on a
 * NULL argument, a DM address outside the window, or an unsupported cntr. */
int stunrun_adsp_som_scan_select(
    const uint16_t *dm, size_t dm_word_count, uint16_t dm_base,
    uint16_t cntr, uint16_t *i1, uint16_t *i2, uint16_t *ring_length);

/* $0514-$05D1 end to end: control word, pointer scan, span walk, interpolation.
 *
 * This is the first entry point that takes no measured value. Give it the
 * data-memory window covering DM($0036), DM($0039) and the ring, plus the
 * coefficient table, and it produces the complete record; the caller then
 * passes *record to stunrun_adsp_som_emit_record.
 *
 * spans must have room for the record's span list and stays pointed at by
 * record->spans, so keep it alive as long as the record is used.
 *
 * Returns 1 on success; 0 on any argument, bounds, or unsupported-selector
 * failure, writing nothing. */
int stunrun_adsp_som_derive_record(
    const uint16_t *dm, size_t dm_word_count, uint16_t dm_base,
    const uint32_t *program_words, size_t program_word_count,
    stunrun_adsp_som_span_t *spans, size_t span_capacity,
    stunrun_adsp_som_record_t *record);

#ifdef __cplusplus
}
#endif

#endif /* STUNRUN_ADSP_SOM_WALK_H */