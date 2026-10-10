#ifndef STUNRUN_GSP_WORD_PAIR_H
#define STUNRUN_GSP_WORD_PAIR_H

#include <stdint.h>

typedef struct {
    void *ctx;
    void (*write_host_data)(void *ctx, uint16_t value);
} stunrun_gsp_word_pair_io_t;

/* ROM $2FE5E-$2FE78.  The stack words are emitted in reverse order. */
void stunrun_gsp_emit_word_pair(stunrun_gsp_word_pair_io_t *io,
                                uint16_t first,
                                uint16_t second);

#endif
