#include "gsp_2bpp_glyph.h"

#include <stddef.h>

void stunrun_gsp_2bpp_emit_row(stunrun_gsp_2bpp_glyph_io_t *io,
                               uint32_t address,
                               uint16_t source,
                               unsigned initial_extend_bit)
{
    uint32_t expanded = 0u;
    unsigned extend = initial_extend_bit & 1u;
    unsigned i;

    if (io == NULL || io->set_host_address == NULL ||
        io->write_host_data == NULL)
        return;

    /* $234BE rotates each source bit into two output bits.  The initial X
     * state is not established in this routine, so the seam exposes it. */
    for (i = 0u; i < 16u; ++i) {
        unsigned next = (unsigned)(source >> 15);
        source = (uint16_t)((source << 1) | extend);
        extend = next;

        next = (unsigned)(expanded >> 31);
        expanded = (expanded << 1) | extend;
        extend = next;
        next = (unsigned)(expanded >> 31);
        expanded = (expanded << 1) | extend;
        extend = next;
    }

    expanded >>= 1; /* $234C6: lsr.l #1,D6 */
    io->set_host_address(io->ctx, address);
    io->write_host_data(io->ctx, (uint16_t)expanded);
    io->set_host_address(io->ctx, address + 0x10u); /* $234D0 */
    io->write_host_data(io->ctx, (uint16_t)(expanded >> 16)); /* swap */
}
