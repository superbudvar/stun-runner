/* GSP display-list payload codec: the 8-word record container observed in the
 * SOM -> $810000 -> HSTDAT transfer.
 *
 * WHAT THIS IS. A faithful container codec plus named accessors for the fields
 * the GSP consumer demonstrably assigns. It is NOT a semantic decoder: words 3
 * through 7 are carried through untouched because the consumer paths that read
 * them have not been disassembled. Preserving them raw is the honest position --
 * see docs/mc68010-boot-findings.md sections 54 and 55.
 *
 * WHAT WAS MEASURED, and where each claim comes from.
 *
 *  - Records are 8 words, from offset 0, no header padding.
 *      tools/disasm-gsp: `SLL 4h,A5 / ADD A1,A5` at $FFF45000 gives a 16-byte
 *      stride. Independently, every `raw=` field in the committed captures is
 *      exactly 64 or 128 words = 8 or 16 whole records, never a remainder
 *      (findings 54.2 correction).
 *  - Word 0 is a control word, consumed into A4 at $FFF45090:
 *        BTST Fh,A4    bit 15    -> JRNE, a skip/end select
 *        BTST Ch,A4    bit 12    -> JRNE, a second mode select
 *        ANDI C00h,A0  bits 10-11 -> selects one of four code paths
 *        ANDI FFh,A0   bits 0-7   -> CMPI 4h on one of those paths
 *  - Word 1 is loaded into A8 and then moved to B0. The handler later executes
 *    FILL XY, not PIXBLT; B0's architectural PIXBLT role does not prove this
 *    payload field is a source address.
 *  - Word 2 is loaded into A6.
 *  - The worker sets B9/COLOR1 to either 0x01010101 at $00345200 or
 *    0x15151515 at $003452A0, depending on the control-word branch.
 *
 * WHAT IS DELIBERATELY NOT CLAIMED.
 *
 *  - The geometric meanings of the fields. They are consumed by XY addressing
 *    and FILL XY, but have not been tied to origin/extent/other shape terms.
 *  - That words 3..7 mean anything.
 *  - That these COLOR1 values correspond to particular pixel widths or
 *    expansion modes.
 */
#ifndef STUNRUN_GSP_PAYLOAD_CODEC_H
#define STUNRUN_GSP_PAYLOAD_CODEC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Observed record size. Changing this is a finding, not a refactor. */
#define STUNRUN_GSP_PAYLOAD_RECORD_WORDS 8u

/* The two B9/COLOR1 values loaded by the observed control-word branches. */
#define STUNRUN_GSP_PAYLOAD_COLOR1_A 0x01010101u
#define STUNRUN_GSP_PAYLOAD_COLOR1_B 0x15151515u
#define STUNRUN_GSP_PAYLOAD_COLOR_ACTION_INHERIT 0u
#define STUNRUN_GSP_PAYLOAD_COLOR_ACTION_SET_A   1u
#define STUNRUN_GSP_PAYLOAD_COLOR_ACTION_SET_B   2u
#define STUNRUN_GSP_PAYLOAD_COLOR_ACTION_SELECTOR_ALT 3u
#define STUNRUN_GSP_PAYLOAD_COLOR_ACTION_SKIP    4u
#define STUNRUN_GSP_PAYLOAD_COLOR_ACTION_MODE_ALT 5u

/* Control-word masks, named for the instruction that uses them rather than for
 * a meaning we have not established. */
#define STUNRUN_GSP_CTL_SKIP_BIT    15u     /* BTST Fh,A4   */
#define STUNRUN_GSP_CTL_MODE_BIT    12u     /* BTST Ch,A4   */
#define STUNRUN_GSP_CTL_EXPAND_MASK 0x0C00u  /* ANDI C00h,A0 */
#define STUNRUN_GSP_CTL_LOW8_MASK   0x00FFu  /* ANDI FFh,A0  */
#define STUNRUN_GSP_CTL_LOW8_VALUE  0x0004u  /* CMPI 4h,A0   */

/* First word whose meaning is unknown; everything from here on is carried raw. */
#define STUNRUN_GSP_PAYLOAD_FIRST_UNMAPPED_WORD 3u

typedef struct stunrun_gsp_payload_record {
    uint16_t raw[STUNRUN_GSP_PAYLOAD_RECORD_WORDS];
} stunrun_gsp_payload_record_t;

typedef struct stunrun_gsp_payload_control {
    uint16_t word;
    int      skip;    /* bit 15, per BTST Fh,A4 */
    int      mode;    /* bit 12, per BTST Ch,A4 */
    uint16_t expand;  /* bits 10-11, per ANDI C00h,A0 */
    uint16_t low8;    /* bits 0-7, per ANDI FFh,A0 */
    /* Result of literal control flow. Selector actions assume the bit-15
     * skip, bit-12 mode, and external A13 bit-3 gates were not taken. */
    uint8_t color_action;
} stunrun_gsp_payload_control_t;

/* Register-level setup produced by the primary FILL XY worker. These values
 * are XY/DYDX operands, not asserted screen-space pixel coordinates. */
typedef struct stunrun_gsp_fill_xy_setup {
    uint16_t destination_x; /* B2 low half: word 1 -> A8 -> B0 -> B2 */
    uint16_t destination_y; /* B2 high half: word 3 -> A8 -> MOVY -> B0 */
    uint16_t dx;            /* B7 low half: word 2 -> A6 -> B7 */
    uint16_t dy;            /* B7 high half: A11.Y added to A6 then SUBXY B2 */
} stunrun_gsp_fill_xy_setup_t;

/* Split a word stream into records.
 *
 * Rejects rather than guesses. A word count that is not a whole number of
 * records, a NULL stream or output, or an undersized buffer all return 0 with
 * *out_count set to 0. Returns the record count on success. */
size_t stunrun_gsp_payload_decode(const uint16_t *words, size_t word_count,
                                  stunrun_gsp_payload_record_t *records_out,
                                  size_t record_capacity,
                                  size_t *out_count);

/* Re-emit records as a word stream: the exact inverse of decode, so that for any
 * input decode accepted, encode(decode(x)) == x. Rejects the same inputs. */
size_t stunrun_gsp_payload_encode(const stunrun_gsp_payload_record_t *records,
                                  size_t record_count,
                                  uint16_t *words_out, size_t word_capacity,
                                  size_t *out_count);

/* Field accessors: the literal words the consumer loads at $FFF45090. */
uint16_t stunrun_gsp_payload_control_word(const stunrun_gsp_payload_record_t *r);
uint16_t stunrun_gsp_payload_field_a8_initial(
    const stunrun_gsp_payload_record_t *r);
uint16_t stunrun_gsp_payload_field_a6(const stunrun_gsp_payload_record_t *r);

/* Read one of the unmapped words (index 3..7). Reachable so a caller can carry
 * them; it asserts nothing about what they mean. Returns 0 for an out-of-range
 * index or a NULL record. */
uint16_t stunrun_gsp_payload_unmapped(const stunrun_gsp_payload_record_t *r,
                                      size_t index);

/* Derive the control word's literal tests and resulting B9 setup action. */
stunrun_gsp_payload_control_t stunrun_gsp_payload_control(
    const stunrun_gsp_payload_record_t *r);

/* Reconstruct the primary worker's FILL XY operands from one record.
 *
 * a11 is the caller-provided register value at the point the worker executes
 * ADDXY A11,A6. a13 is the live A13 register tested by BTST 3h,A13. Returns 1
 * and fills `out` only when this record takes the primary worker: bit15 skip,
 * bit12 mode jump, selector 0xC00, or A13 bit3 all reject the path. Returns 0
 * without touching output on NULL/alternate input. */
int stunrun_gsp_payload_primary_fill_xy(
    const stunrun_gsp_payload_record_t *r, uint32_t a11, uint32_t a13,
    stunrun_gsp_fill_xy_setup_t *out);

#ifdef __cplusplus
}
#endif

#endif /* STUNRUN_GSP_PAYLOAD_CODEC_H */
