/* Bounded transcription of the captured ADSP $0214-$0237 record control word.
 *
 * DM($0036) is latched at $0515 as the record's control word and is written
 * three times per record: $01A0 writes $0002 (that half belongs to the
 * record_schedule slice), $0236 writes the value modelled here, and $01D0
 * rewrites $0030 afterwards. $0214-$0237 is the routine that produces it.
 *
 * Two of its inputs are produced by code OUTSIDE this slice and therefore
 * arrive measured:
 *
 *   dm0036_in          what $01A0 wrote. $021B reads it, $0231 reads it again,
 *                      and nothing between those two addresses writes it.
 *   dm003a_after_call  what DM($003A) holds when $022E reads it, i.e. AFTER the
 *                      $022D CALL to $0238. $0238-$0253 is a separate routine
 *                      and is NOT transcribed here; it is why this is an input.
 *                      See "WHAT WOULD REMOVE THE SECOND INPUT" below.
 *
 * It also reads two memory windows, because the MAC loop at $0221-$022C
 * genuinely reads them:
 *
 *   DM(I3,M1)  with I3=$0014, M1=$0001, L3=$0003 -> DM($0014)-DM($0016), twice
 *   PM(I4,M4)  with I4=$1019, M4=$0001, L4=$0000 -> PM($1019)-PM($101E)
 *
 * These are NOT "the two DM values" of the record-level contract above; they
 * are the loop's own operands. They cannot be dropped: the nine captured $022C
 * values are a function of them, and hard-coding them would be exactly the
 * lookup table this slice is forbidden to contain. Whether the caller wants
 * them inlined into a wider record context is an integration decision left to
 * the parent; see docs/mc68010-boot-findings.md sec. 99.
 *
 * WHAT IS DELIBERATELY NOT HERE:
 *   - $0238-$0254. `CALL $0238` at $022D is modelled as the identity on
 *     DM($003A): the captured run always reached $0253, which writes $0000, so
 *     dm003a_after_call is zero for all nine records. Removing that input would
 *     mean transcribing $0238-$0253: its DM($1FE6)/DM($08EA)/DM($0033) reads,
 *     the $023A EQ early-out, the two $023F/$0241 signed shifts, the $0243
 *     borrow test, the $0246 $FF00 mask, the $024C-$024F (SU) multiply and both
 *     of its writes to DM($003A) at $0249 and $0253.
 *   - $021E/$021F `AR = AR AND AY0` / `DM($0036) = AR` and the $0220 RTS. The
 *     capture took $021D on every record (DM($0036) was $0002, and 2 - 31 is
 *     negative), so the mask-and-return path never ran. What DM($0036) would
 *     have become is UNKNOWN; it is rejected, not invented.
 *   - $022B `IF LT AR = AF`. Never taken on any captured record: MR1 + $2D41 is
 *     positive in all nine. UNKNOWN, rejected.
 *   - The X register MX0 at $0221/$0228, which is stale there (nothing in
 *     $0214-$0237 sets it). Its value is not observable in this capture and is
 *     not an input: the Y operand at both sites is the ZERO register, so the
 *     product is zero whatever MX0 held. See sec. 99.
 *
 * No caller semantics, no record identity and no meaning for DM($0014)-DM($0016)
 * or PM($1019)-PM($101E) is claimed. Every number is a number.
 */
#ifndef STUNRUN_ADSP_CONTROL_WORD_H
#define STUNRUN_ADSP_CONTROL_WORD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* $0214-$0219: the two index registers the MAC loop runs on. */
#define STUNRUN_ADSP_CONTROL_WORD_I3 0x0014u
#define STUNRUN_ADSP_CONTROL_WORD_I4 0x1019u
#define STUNRUN_ADSP_CONTROL_WORD_L3 0x0003u
#define STUNRUN_ADSP_CONTROL_WORD_L4 0x0000u
#define STUNRUN_ADSP_CONTROL_WORD_M1 0x0001u
#define STUNRUN_ADSP_CONTROL_WORD_M4 0x0001u

/* $0222 CNTR = $0002, and the $0223 DO body is $0224-$0227: two passes of
 * three DAG pairs each, so six DM reads and six PM reads. */
#define STUNRUN_ADSP_CONTROL_WORD_CNTR 0x0002u
#define STUNRUN_ADSP_CONTROL_WORD_DM_READS 6u
#define STUNRUN_ADSP_CONTROL_WORD_PM_READS 6u

/* $021A AY0 = $001F, the guard $021C subtracts before $021D's LT test. */
#define STUNRUN_ADSP_CONTROL_WORD_GUARD 0x001Fu
/* $0229 AY1 = $2D41, the bias $022A adds to MR1. */
#define STUNRUN_ADSP_CONTROL_WORD_BIAS 0x2D41u
/* $022F MY0 = $0012, $0232 shifts SI left by 4, $0233 AY0 = $0010. */
#define STUNRUN_ADSP_CONTROL_WORD_MULTIPLIER 0x0012u
#define STUNRUN_ADSP_CONTROL_WORD_SHIFT 4u
#define STUNRUN_ADSP_CONTROL_WORD_FLAG 0x0010u

typedef struct stunrun_adsp_control_word {
    /* $022C DM($003A) = AR -- MR1 + $2D41. On all nine captured records the
     * $0238 call overwrote this with $0000 before $022E read it, so it is
     * modelled but never consumed. Kept as an output because it is the only
     * thing in the capture that distinguishes the two writes to DM($003A). */
    uint16_t dm003a_022c;
    /* $0236 DM($0036) = AR -- MR1 OR (SR0 + $0010). */
    uint16_t dm0036_0236;
} stunrun_adsp_control_word_t;

/* Build the control word from $0214-$0237.
 *
 * dm0036_in is what $01A0 wrote; dm003a_after_call is DM($003A) as $022E reads
 * it, after the out-of-scope $0238 call.
 *
 * dm_words/pm_words are absolute-address windows over DM and PM exactly as
 * som_math.c uses them: DM(a) is dm_words[a - dm_base], PM(a) is
 * pm_words[a - pm_base], and a read outside the window is rejected. MY0 is the
 * PM dword's bits 23:8, which is what pgm_read_dag2() hands the MAC
 * (2100ops.hxx:689-698).
 *
 * Returns 1 on success. Returns 0 and leaves *out untouched for a NULL window or
 * out, a zero-length window, any read outside a window, DM($0036) >= $001F (the
 * unobserved $021E/$021F mask-and-return), or MR1 + $2D41 < 0 (the unobserved
 * $022B IF LT). */
int stunrun_adsp_control_word_build(
    uint16_t dm0036_in, uint16_t dm003a_after_call,
    const uint16_t *dm_words, size_t dm_word_count, uint16_t dm_base,
    const uint32_t *pm_words, size_t pm_word_count, uint32_t pm_base,
    stunrun_adsp_control_word_t *out);

#ifdef __cplusplus
}
#endif

#endif /* STUNRUN_ADSP_CONTROL_WORD_H */
