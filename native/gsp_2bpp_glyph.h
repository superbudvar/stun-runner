#ifndef STUNRUN_GSP_2BPP_GLYPH_H
#define STUNRUN_GSP_2BPP_GLYPH_H

#include <stdint.h>

typedef struct {
    void *ctx;
    void (*set_host_address)(void *ctx, uint32_t address);
    void (*write_host_data)(void *ctx, uint16_t value);
} stunrun_gsp_2bpp_glyph_io_t;

/* ROM $234B6-$234E0.  Expand one 16-bit glyph row into two host words. */
void stunrun_gsp_2bpp_emit_row(stunrun_gsp_2bpp_glyph_io_t *io,
                               uint32_t address,
                               uint16_t source,
                               unsigned initial_extend_bit);

#endif
