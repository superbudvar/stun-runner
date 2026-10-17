/* Bounded F0 prefix decoder and primary FILL XY register mapping. */

#include "payload_codec.h"

int stunrun_gsp_payload_decode_prefix(const uint16_t *words, size_t word_count,
                                      size_t word_offset,
                                      stunrun_gsp_payload_prefix_t *out)
{
    stunrun_gsp_payload_prefix_t decoded;

    if (words == NULL || out == NULL || word_offset > word_count ||
        word_count - word_offset < STUNRUN_GSP_PAYLOAD_PREFIX_WORDS)
        return 0;

    decoded.control = words[word_offset + 0u];
    decoded.left_x  = words[word_offset + 1u];
    decoded.right_x = words[word_offset + 2u];
    decoded.y       = words[word_offset + 3u];
    *out = decoded;
    return 1;
}

size_t stunrun_gsp_payload_encode_prefix(
    const stunrun_gsp_payload_prefix_t *prefix,
    uint16_t *words_out, size_t word_capacity)
{
    if (prefix == NULL || words_out == NULL ||
        word_capacity < STUNRUN_GSP_PAYLOAD_PREFIX_WORDS)
        return 0u;

    words_out[0] = prefix->control;
    words_out[1] = prefix->left_x;
    words_out[2] = prefix->right_x;
    words_out[3] = prefix->y;
    return STUNRUN_GSP_PAYLOAD_PREFIX_WORDS;
}

stunrun_gsp_payload_control_t stunrun_gsp_payload_control(
    const stunrun_gsp_payload_prefix_t *prefix)
{
    stunrun_gsp_payload_control_t c = {0};

    if (prefix == NULL)
        return c;

    c.word = prefix->control;
    c.skip = (uint8_t)((c.word >> STUNRUN_GSP_CTL_SKIP_BIT) & 1u);
    c.mode_jump = (uint8_t)((c.word >> STUNRUN_GSP_CTL_MODE_BIT) & 1u);
    c.selector = (uint16_t)(c.word & STUNRUN_GSP_CTL_SELECTOR_MASK);
    c.low8 = (uint16_t)(c.word & STUNRUN_GSP_CTL_LOW8_MASK);

    /* Outer dispatch at $FFF450A0-$FFF450E0 precedes this selector:
     * bit15 returns at $FFF44D50; bit12 jumps to $FFF48FC0. */
    if (c.skip) {
        c.color_action = STUNRUN_GSP_PAYLOAD_COLOR_ACTION_SKIP;
        return c;
    }
    if (c.mode_jump) {
        c.color_action = STUNRUN_GSP_PAYLOAD_COLOR_ACTION_MODE_ALT;
        return c;
    }

    /* The selector at $FFF45160-$FFF452A0. The external A13 bit-3 branch is
     * not derivable from this prefix and is handled by primary_fill_xy(). */
    switch (c.selector) {
    case 0x0000u:
        c.color_action = c.low8 == STUNRUN_GSP_CTL_LOW8_VALUE
            ? STUNRUN_GSP_PAYLOAD_COLOR_ACTION_SET_B
            : STUNRUN_GSP_PAYLOAD_COLOR_ACTION_INHERIT;
        break;
    case 0x0400u:
        c.color_action = STUNRUN_GSP_PAYLOAD_COLOR_ACTION_SET_B;
        break;
    case 0x0800u:
        c.color_action = STUNRUN_GSP_PAYLOAD_COLOR_ACTION_SET_A;
        break;
    default:
        c.color_action = STUNRUN_GSP_PAYLOAD_COLOR_ACTION_SELECTOR_ALT;
        break;
    }
    return c;
}

int stunrun_gsp_payload_primary_fill_xy(
    const stunrun_gsp_payload_prefix_t *prefix, uint32_t a11, uint32_t a13,
    stunrun_gsp_fill_xy_setup_t *out)
{
    stunrun_gsp_payload_control_t control;
    stunrun_gsp_fill_xy_setup_t result;

    if (prefix == NULL || out == NULL)
        return 0;

    control = stunrun_gsp_payload_control(prefix);
    if (control.skip || control.mode_jump ||
        control.color_action == STUNRUN_GSP_PAYLOAD_COLOR_ACTION_SELECTOR_ALT ||
        (a13 & (1u << 3)) != 0u)
        return 0;

    /* Literal dataflow:
     *   F0 word1 -> A8 -> B0.X (inclusive left endpoint)
     *   F0 word2 -> A6.X (inclusive right endpoint)
     *   F0 word3 -> A8 -> MOVY A8,B0 and MOVY A8,A6 (both Y starts)
     *   ADDXY A11,A6; B7=A6; SUBXY B2,B7 leaves B7.DY=A11.Y and
     *     B7.DX=word2-(word1-1)=rightX-leftX+1
     *   FILL XY consumes B2=DADDR and B7=DYDX. */
    result.destination_x = prefix->left_x;
    result.destination_y = prefix->y;
    result.width = (uint16_t)(prefix->right_x - prefix->left_x + 1u);
    result.dy = (uint16_t)(a11 >> 16);
    *out = result;
    return 1;
}
