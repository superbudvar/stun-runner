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

uint16_t stunrun_gsp_payload_saddr(const stunrun_gsp_payload_record_t *r)
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
    c.expand_mask = STUNRUN_GSP_PAYLOAD_EXPAND_MASK_NONE;
    return c;
}
