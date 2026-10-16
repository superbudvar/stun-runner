/* ADSP-2100 SOM record emitter: the $0564-$05DC output sequence.
 *
 * One captured transaction (reference/experiments/stunrun/adsp-som-transaction-
 * f551/) is shaped as:
 *
 *   SOMCLK DM($2003) at PC $0034          resets the SOM word pointer
 *   2 zero words       PC $0035, $0039    AR is still $0000
 *   9 records          PC $0515..$05D4    $0834 calls this emitter
 *   GINT   DM($2006)   PC $004C
 *
 * Each record is four header words, zero or more five-word spans, then the
 * two-word $FFFF terminator:
 *
 *   $0515  control     AR  = DM($0036)
 *   $0565  start_x     AR  = DM(I1,M1)
 *   $0567  delta_x     AR  = DM(I2,M2)
 *   $056B  start_y     AX1 = DM(I0,M0)
 *   span   DM($003E)   AX0 = DM($003E)          (side value, low word)
 *   span   DM($003F)   AX0 = DM($003F)          (side value, high word)
 *   span   DM($003C)   AX0 = DM($003C)          (other side, low word)
 *   span   DM($003D)   AX0 = DM($003D)          (other side, high word)
 *   span   height      AR  = <branch>           run length in rows
 *   $05D2  $FFFF terminator
 *   $05D4  $FFFF terminator
 *
 * SCOPE. This slice covers the word ORDER, the three-way height branch, the
 * guard levels, and the terminator. It does NOT cover: how the caller chooses
 * the span list, the index/modifier walk that produces the header words and the
 * per-span side values, the $05DD/$0607 interpolation itself (that is
 * som_math.c), or the loop bound. The span list is therefore an input, and the
 * side values are the 32-bit results the interpolation helpers leave in DM.
 *
 * See docs/mc68010-boot-findings.md section 66.
 */
#ifndef STUNRUN_ADSP_SOM_RECORD_H
#define STUNRUN_ADSP_SOM_RECORD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STUNRUN_ADSP_SOM_HEADER_WORDS  4u
#define STUNRUN_ADSP_SOM_SPAN_WORDS    5u
#define STUNRUN_ADSP_SOM_SENTINEL_WORDS 2u
#define STUNRUN_ADSP_SOM_TERMINATOR    0xffffu

/* A 32-bit side value as DM($003E)/DM($003F) or DM($003C)/DM($003D) hold it:
 * the low word is latched first, so `lo` is emitted before `hi`. */
typedef struct stunrun_adsp_som_side {
    uint16_t lo;
    uint16_t hi;
} stunrun_adsp_som_side_t;

/* One span: the walk registers live in AX1/AY0/AY1 at the branch, and the two
 * interpolated side values live in DM. */
typedef struct stunrun_adsp_som_span {
    uint16_t ax1;  /* AX1 at $057D/$059D/$05B7: the span baseline */
    uint16_t ay0;  /* AY0 */
    uint16_t ay1;  /* AY1 */
    stunrun_adsp_som_side_t dm_3e;  /* DM($003E)/DM($003F) */
    stunrun_adsp_som_side_t dm_3c;  /* DM($003C)/DM($003D) */
} stunrun_adsp_som_span_t;

typedef struct stunrun_adsp_som_record {
    uint16_t control;  /* DM($0036) */
    uint16_t start_x;  /* DM(I1,M1) */
    uint16_t delta_x;  /* DM(I2,M2) */
    uint16_t start_y;  /* DM(I0,M0) */
    const stunrun_adsp_som_span_t *spans;
    size_t span_count;
} stunrun_adsp_som_record_t;

/* Words the emitter writes for a record with `span_count` spans, including the
 * $FFFF terminator. Saturates instead of overflowing. */
size_t stunrun_adsp_som_record_words(size_t span_count);

/* The three emitting branches all reduce to the run length in rows:
 *
 *   $059D (AY1 >  AY0):  AR = AY0 - AX1 + 1
 *   $057D (AY1 == AY0):  AR = AY1 - AX1 + 1
 *   $05B7 (AY1 <  AY0):  AR = AY1 - AX1 + 1
 *
 * which is min(AY0,AY1) - AX1 + 1 over unsigned 16-bit arithmetic. Kept as an
 * explicit three-way branch so the transcribed structure stays visible and a
 * wrong choice of AY0/AY1 cannot pass silently. */
uint16_t stunrun_adsp_som_span_height(uint16_t ax1, uint16_t ay0, uint16_t ay1);

/* Emit one record: header, spans, $FFFF $FFFF.
 *
 * Guard levels differ by path in the original and this models the stricter of
 * the two rather than pretending every path is identical:
 *
 *   - the first span comes from the $0574/$0577 prologue, which uses IF LE, so
 *     AY0 and AY1 must both be strictly greater than AX1;
 *   - later spans come from loop tails that use IF LT, so equal is legal (one
 *     captured span has AY0 == AY1 == AX1). Every tail checks AY1; only the
 *     $059A tail checks AY0, so AY0 >= AX1 is enforced here as a conservative
 *     precondition, not as a claim about every path.
 *
 * A record with span_count 0 emits only the header and the terminator, which is
 * what $05D1 produces when a prologue guard bails.
 *
 * Returns the number of words written, which is always at least 6 for a valid
 * record, so 0 unambiguously means failure: NULL arguments, capacity too small,
 * or a span violating its guard. Nothing is written on failure. */
size_t stunrun_adsp_som_emit_record(const stunrun_adsp_som_record_t *record,
                                    uint16_t *out, size_t out_capacity);

#ifdef __cplusplus
}
#endif

#endif /* STUNRUN_ADSP_SOM_RECORD_H */