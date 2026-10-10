#include "gsp_word_pair.h"

#include <stddef.h>

void stunrun_gsp_emit_word_pair(stunrun_gsp_word_pair_io_t *io,
                                uint16_t first,
                                uint16_t second)
{
    if (io == NULL || io->write_host_data == NULL)
        return;

    /* $2FE68 reads 10(A6), then $2FE6E reads 8(A6). */
    io->write_host_data(io->ctx, second);
    io->write_host_data(io->ctx, first);
}
