/* GSP display-list payload codec. See payload_codec.h for what is measured and
 * what is deliberately left unclaimed. */

#include "payload_codec.h"

#include <stdint.h>
#include <string.h>

size_t stunrun_gsp_payload_decode(const uint16_t *words, size_t word_count,
                                  stunrun_gsp_payload_record_t *records_out,
                                  size_t record_capacity,
                                  size_t *out_count)
{
    size_t records;

    if (out_count != NULL)
        *out_count = 0u;
    if (words == NULL || records_out == NULL || out_count == NULL)
        return 0u;
    /* A trailing partial record means the producer and this reader disagree
     * about the record size. Guessing which is right would turn a framing
     * change into silently truncated geometry. */
    if (word_count % STUNRUN_GSP_PAYLOAD_RECORD_WORDS != 0u)
        return 0u;

    records = word_count / STUNRUN_GSP_PAYLOAD_RECORD_WORDS;
    if (records > record_capacity)
        return 0u;

    memcpy(records_out, words, word_count * sizeof(uint16_t));
    *out_count = records;
    return records;
}

size_t stunrun_gsp_payload_encode(const stunrun_gsp_payload_record_t *records,
                                  size_t record_count,
                                  uint16_t *words_out, size_t word_capacity,
                                  size_t *out_count)
{
    size_t words;

    if (out_count != NULL)
        *out_count = 0u;
    if (records == NULL || words_out == NULL || out_count == NULL)
        return 0u;
    if (record_count == 0u)
        return 0u;
    if (record_count > SIZE_MAX / STUNRUN_GSP_PAYLOAD_RECORD_WORDS)
        return 0u;

    words = record_count * STUNRUN_GSP_PAYLOAD_RECORD_WORDS;
    if (words > word_capacity)
        return 0u;

    memcpy(words_out, records, words * sizeof(uint16_t));
    *out_count = words;
    return words;
}

static uint16_t word(const stunrun_gsp_payload_record_t *r, size_t index)
{
    return r == NULL ? 0u : r->raw[index];
}

uint16_t stunrun_gsp_payload_control_word(const stunrun_gsp_payload_record_t *r)
{
    return word(r, 0u);
}

uint16_t stunrun_gsp_payload_field_a8_initial(
    const stunrun_gsp_payload_record_t *r)
{
    return word(r, 1u);
}

uint16_t stunrun_gsp_payload_field_a6(const stunrun_gsp_payload_record_t *r)
{
    return word(r, 2u);
}

uint16_t stunrun_gsp_payload_unmapped(const stunrun_gsp_payload_record_t *r,
                                      size_t index)
{
    if (r == NULL)
        return 0u;
    if (index < STUNRUN_GSP_PAYLOAD_FIRST_UNMAPPED_WORD ||
        index >= STUNRUN_GSP_PAYLOAD_RECORD_WORDS)
        return 0u;
    return r->raw[index];
}

stunrun_gsp_payload_control_t stunrun_gsp_payload_control(
    const stunrun_gsp_payload_record_t *r)
{
    stunrun_gsp_payload_control_t c;
    uint16_t w = stunrun_gsp_payload_control_word(r);

    memset(&c, 0, sizeof(c));
    c.word = w;
    c.skip = (int)((w >> STUNRUN_GSP_CTL_SKIP_BIT) & 1u);
    c.mode = (int)((w >> STUNRUN_GSP_CTL_MODE_BIT) & 1u);
    c.expand = (uint16_t)(w & STUNRUN_GSP_CTL_EXPAND_MASK);
    c.low8 = (uint16_t)(w & STUNRUN_GSP_CTL_LOW8_MASK);
    /* The handler branches first on bit 15 (skip/return), then bit 12 (jump
     * to alternate routine $FFF48FC0). Only with both clear does it reach the
     * B9/COLOR1 selector at $FFF45160-$FFF452A0. The selector is also bypassed
     * when external state A13 bit 3 is set; this record-only function cannot
     * observe A13 and reports the per-record action assuming that gate is clear.
     *
     * Literal B9/COLOR1 branch tree:
     *
     *   (ctl & 0x0c00) == 0:
     *       (ctl & 0x00ff) == 4 -> load COLOR1_B at $3452A0
     *       otherwise           -> retain B9=A12 (set at $450C0)
     *   (ctl & 0x0c00) == 0x0400 -> load COLOR1_B at $3452A0
     *   (ctl & 0x0c00) == 0x0800 -> load COLOR1_A at $345200
     *   (ctl & 0x0c00) == 0x0c00 -> branch to alternate worker $FFF45800
     *
     * These are observed code paths, not claims about geometric meaning or
     * pixel size. */
    if (c.skip) {
        c.color_action = STUNRUN_GSP_PAYLOAD_COLOR_ACTION_SKIP;
        return c;
    }
    if (c.mode) {
        c.color_action = STUNRUN_GSP_PAYLOAD_COLOR_ACTION_MODE_ALT;
        return c;
    }

    switch (c.expand) {
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
    const stunrun_gsp_payload_record_t *r, uint32_t a11, uint32_t a13,
    stunrun_gsp_fill_xy_setup_t *out)
{
    stunrun_gsp_payload_control_t control;
    stunrun_gsp_fill_xy_setup_t result;

    if (r == NULL || out == NULL)
        return 0;

    control = stunrun_gsp_payload_control(r);
    if (control.skip || control.mode ||
        control.color_action == STUNRUN_GSP_PAYLOAD_COLOR_ACTION_SELECTOR_ALT ||
        (a13 & (1u << 3)) != 0u)
        return 0;

    /* Literal dataflow from the disassembly:
     *   word1 -> A8 -> B0.X
     *   word3 -> A8 -> MOVY A8,B0, so B0.Y = word3
     *   word2 -> A6.X; MOVY word3,A6 sets A6.Y
     *   ADDXY A11,A6; B7=A6; SUBXY B2,B7 leaves B7.X=word2 and
     *   B7.Y=A11.Y, since B2.Y is the same word3 start value.
     *   FILL XY consumes B2 as DADDR and B7 as DYDX. */
    result.destination_x = r->raw[1];
    result.destination_y = r->raw[3];
    result.dx = r->raw[2];
    result.dy = (uint16_t)(a11 >> 16);
    *out = result;
    return 1;
}
