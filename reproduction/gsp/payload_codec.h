/* Primary FILL XY prefix decoder for the TMS34010 SOM-fed display-list stream.
 *
 * This deliberately does NOT impose a fixed record or entry size. The program
 * configures F0=16 bits and F1=32 bits; after the first four F0 fields it reads
 * F1 fields that span two 16-bit words. The overall entry boundary is therefore
 * not established by this prefix decoder. See docs/mc68010-boot-findings.md §61.
 *
 * Proven primary-path field mapping at $FFF45090-$FFF454E0:
 *   F0 word 0 -> control
 *   F0 word 1 -> A8 -> B0.X (FILL DADDR.X)
 *   F0 word 2 -> A6.X (FILL DYDX.DX)
 *   F0 word 3 -> MOVY into B0/A6 (FILL DADDR.Y)
 * Further F1/F0 fields are left to the caller as raw stream words.
 */
#ifndef STUNRUN_GSP_PAYLOAD_CODEC_H
#define STUNRUN_GSP_PAYLOAD_CODEC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STUNRUN_GSP_PAYLOAD_PREFIX_WORDS 4u
#define STUNRUN_GSP_PAYLOAD_COLOR1_A 0x01010101u
#define STUNRUN_GSP_PAYLOAD_COLOR1_B 0x15151515u

#define STUNRUN_GSP_PAYLOAD_COLOR_ACTION_INHERIT 0u
#define STUNRUN_GSP_PAYLOAD_COLOR_ACTION_SET_A   1u
#define STUNRUN_GSP_PAYLOAD_COLOR_ACTION_SET_B   2u
#define STUNRUN_GSP_PAYLOAD_COLOR_ACTION_SELECTOR_ALT 3u
#define STUNRUN_GSP_PAYLOAD_COLOR_ACTION_SKIP    4u
#define STUNRUN_GSP_PAYLOAD_COLOR_ACTION_MODE_ALT 5u

#define STUNRUN_GSP_CTL_SKIP_BIT      15u
#define STUNRUN_GSP_CTL_MODE_BIT      12u
#define STUNRUN_GSP_CTL_SELECTOR_MASK 0x0C00u
#define STUNRUN_GSP_CTL_LOW8_MASK     0x00FFu
#define STUNRUN_GSP_CTL_LOW8_VALUE    0x0004u

typedef struct stunrun_gsp_payload_prefix {
    uint16_t control; /* F0 word 0 -> A4 */
    uint16_t x;       /* F0 word 1 -> A8 -> B0.X */
    uint16_t dx;      /* F0 word 2 -> A6.X -> B7.DX */
    uint16_t y;       /* F0 word 3 -> MOVY -> B0.Y / A6.Y */
} stunrun_gsp_payload_prefix_t;

typedef struct stunrun_gsp_payload_control {
    uint16_t word;
    uint8_t skip;
    uint8_t mode_jump;
    uint16_t selector;
    uint16_t low8;
    /* Selector-path B9/COLOR1 action, assuming outer gates are not taken and
     * external A13 bit 3 is clear. */
    uint8_t color_action;
} stunrun_gsp_payload_control_t;

typedef struct stunrun_gsp_fill_xy_setup {
    uint16_t destination_x;
    uint16_t destination_y;
    uint16_t dx;
    uint16_t dy;
} stunrun_gsp_fill_xy_setup_t;

/* Decode exactly the four initial F0 fields at word_offset in a larger stream.
 * Rejects short/NULL input without changing *out. Returns 1 on success. */
int stunrun_gsp_payload_decode_prefix(const uint16_t *words, size_t word_count,
                                      size_t word_offset,
                                      stunrun_gsp_payload_prefix_t *out);

/* Encode the four F0 prefix words; returns 4 on success, 0 on invalid input. */
size_t stunrun_gsp_payload_encode_prefix(
    const stunrun_gsp_payload_prefix_t *prefix,
    uint16_t *words_out, size_t word_capacity);

stunrun_gsp_payload_control_t stunrun_gsp_payload_control(
    const stunrun_gsp_payload_prefix_t *prefix);

/* Reconstruct primary FILL XY operands.
 * a11 is caller state consumed by ADDXY A11,A6; a13 is live external branch
 * state consumed by BTST 3h,A13. Rejects all paths that divert from primary. */
int stunrun_gsp_payload_primary_fill_xy(
    const stunrun_gsp_payload_prefix_t *prefix, uint32_t a11, uint32_t a13,
    stunrun_gsp_fill_xy_setup_t *out);

#ifdef __cplusplus
}
#endif

#endif /* STUNRUN_GSP_PAYLOAD_CODEC_H */
