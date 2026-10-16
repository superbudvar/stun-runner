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
    int terminated;     /* 1 when $058E/$05AE/$05C7 ended the walk */
} stunrun_adsp_som_walk_t;

/* Walk one record's spans.
 *
 * dm is the ADSP data-memory window, addressed by absolute DM address:
 * DM(a) is dm[a - dm_base]. Reading outside the window fails rather than
 * inventing geometry, because a truncated capture would otherwise produce
 * plausible-looking wrong output.
 *
 * i1 and i2 are the index register values as observed at $0564, i.e. after any
 * $0563 `MODIFY (I2,M0)`. length is the L register ($0006 in the capture).
 *
 * Returns 1 on success. Returns 0, writing nothing, when an argument is NULL,
 * when a DM address leaves the window, or when the walk needs more than
 * span_capacity spans. */
int stunrun_adsp_som_walk_record(
    const uint16_t *dm, size_t dm_word_count, uint16_t dm_base,
    uint16_t i1, uint16_t i2, uint16_t length,
    stunrun_adsp_som_span_t *spans, size_t span_capacity,
    stunrun_adsp_som_walk_t *out);

#ifdef __cplusplus
}
#endif

#endif /* STUNRUN_ADSP_SOM_WALK_H */