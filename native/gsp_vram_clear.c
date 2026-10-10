#include "gsp_vram_clear.h"

size_t stunrun_gsp_vram_clear(stunrun_gsp_vram_clear_io_t *io)
{
    size_t writes = 0u;
    unsigned pass;
    unsigned word;

    if (io == NULL || io->set_address_high == NULL ||
        io->set_address_low == NULL || io->set_control == NULL ||
        io->write_word == NULL || io->watchdog_clear == NULL) {
        return 0u;
    }

    io->set_address_high(io->ctx, 0xFF80u);
    io->set_address_low(io->ctx, 0x0000u);
    io->set_control(io->ctx, 0xB800u);

    /* The ROM's DBRA seed is 4, but the first DBRA is before the clear loop;
     * therefore the observed outer loop executes four, not five, passes. */
    for (pass = 0u; pass < STUNRUN_GSP_VRAM_CLEAR_PASSES; ++pass) {
        for (word = 0u; word < STUNRUN_GSP_VRAM_CLEAR_LONGWORDS; ++word) {
            io->write_word(io->ctx, 0u);
            io->write_word(io->ctx, 0u);
            writes += 2u;
        }
        io->watchdog_clear(io->ctx);
    }
    return writes;
}
