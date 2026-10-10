#ifndef STUNRUN_MC68010_TEXT_WRITER_H
#define STUNRUN_MC68010_TEXT_WRITER_H

#include <stddef.h>
#include <stdint.h>

typedef int (*stunrun_text_write_fn)(void *ctx, uint32_t address,
                                     uint16_t value);

/* Observable host-port writes from the executed $25E8 glyph loop. */
size_t stunrun_mc68010_text_write(const uint8_t *descriptor,
                                  size_t descriptor_size,
                                  const uint16_t *font_words,
                                  size_t font_word_count,
                                  stunrun_text_write_fn write,
                                  void *ctx);

#endif
