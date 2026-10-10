#include "mc68010_text_writer.h"

size_t stunrun_mc68010_text_write(const uint8_t *descriptor,
                                  size_t descriptor_size,
                                  const uint16_t *font_words,
                                  size_t font_word_count,
                                  stunrun_text_write_fn write,
                                  void *ctx)
{
    size_t character;
    size_t emitted = 0u;
    uint32_t address;
    uint16_t low_colour;
    uint16_t high_colour;

    if (descriptor == NULL || descriptor_size < 4u || font_words == NULL ||
        write == NULL || (size_t)descriptor[3] > descriptor_size - 4u)
        return 0u;
    address = (((uint32_t)descriptor[1] * 0xA0u) << 8u) +
              ((uint32_t)descriptor[0] << 6u) - 0x800000u;
    low_colour = descriptor[2];
    high_colour = (uint16_t)(low_colour << 8u);
    for (character = 0u; character < descriptor[3]; character++) {
        size_t glyph = (size_t)((uint8_t)(descriptor[4u + character] - 0x2Au));
        size_t row;
        if (glyph > font_word_count / 8u || glyph * 8u + 7u >= font_word_count)
            return 0u;
        for (row = 0u; row < 8u; row++) {
            uint32_t row_address = address;
            uint16_t bits = font_words[glyph * 8u + row];
            unsigned column;
            for (column = 0u; column < 8u; column++) {
                uint16_t value = (bits & 1u) ? low_colour : 0u;
                if ((bits & 4u) != 0u)
                    value = (uint16_t)(value | high_colour);
                if (!write(ctx, row_address, value))
                    return 0u;
                emitted++;
                row_address += 0x10u;
                bits >>= 4u;
            }
            address += 0x1000u;
        }
        /* The ROM's `subi.l #$7fc0,D0` advances one glyph cell. */
        address -= 0x7FC0u;
    }
    return emitted;
}
