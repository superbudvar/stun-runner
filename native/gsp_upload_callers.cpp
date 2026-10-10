#include "gsp_upload_callers.h"

static void upload(stunrun_gsp_upload_callers_t *io, uint32_t destination,
                   uint32_t pointer_slot)
{
    io->write_long(io->opaque, 0x00c00002u, destination);
    io->compile(io->opaque, io->read_long(io->opaque, pointer_slot));
}

void stunrun_gsp_upload_small(stunrun_gsp_upload_callers_t *io)
{
    /* $2D770-$2D7B6: the saved A2 is only the compiler function pointer.
     * Stack saves/restores have no application-visible effects at this seam. */
    upload(io, 0xfff23f00u, 0x000bfe78u);
    upload(io, 0xfff2b760u, 0x000bfe80u);
    upload(io, 0xfff3fd80u, 0x000bfe84u);
    upload(io, 0xfff3fb20u, 0x000bfe88u);
}

static void state_writes(stunrun_gsp_upload_callers_t *io)
{
    /* State-field meanings unknown: preserve the literal copy and marker. */
    io->write_long(io->opaque, 0x00ff9baeu,
                   io->read_long(io->opaque, 0x00047f58u));
    io->write_word(io->opaque, 0x00ff9bceu, 2u);
}

void stunrun_gsp_upload_initial(stunrun_gsp_upload_callers_t *io)
{
    upload(io, 0xffde0000u, 0x000bfe04u);
    stunrun_gsp_upload_small(io);
    upload(io, 0xffe00000u, 0x000bfe08u);
    state_writes(io);
}

void stunrun_gsp_upload_reload(stunrun_gsp_upload_callers_t *io)
{
    upload(io, 0xffe00000u, 0x000bfe08u);
    state_writes(io);
}

void stunrun_gsp_upload_chunks(stunrun_gsp_upload_callers_t *io,
                               uint32_t address, uint32_t length)
{
    /* $2D83E cmp.l / bcs uses an unsigned strict comparison. The final call
     * exists even for zero length; exact multiples end with a full-size call.
     * $2247A is opaque: the 2:1 address increment does not establish its units. */
    while (length > 0x00010000u) {
        io->transfer(io->opaque, address, 0x00010000u);
        length -= 0x00010000u;
        address += 0x00020000u;
    }
    io->transfer(io->opaque, address, length);
}

void stunrun_gsp_upload_emit_byte(stunrun_gsp_upload_callers_t *io,
                                  uint32_t value)
{
    uint16_t remaining = io->read_word(io->opaque, 0x00ff9c68u);
    /* TST clears C; BLS therefore branches precisely when remaining == 0,
     * including a nonzero word with bit 15 set in the decrement path. */
    if (remaining != 0u) {
        io->write_word(io->opaque, 0x00ff9c68u, (uint16_t)(remaining - 1u));
    } else if (io->read_word(io->opaque, 0x00ff9c66u) != 0u) {
        uint32_t previous = io->read_word(io->opaque, 0x00ff9c64u);
        /* D1 is not masked before OR.L. Preserve its high-byte contribution,
         * then truncate only at the MOVE.W host write. No final-byte flush. */
        io->write_word(io->opaque, 0x00c0000cu,
                       (uint16_t)((previous << 8u) | value));
        io->write_word(io->opaque, 0x00ff9c66u, 0u);
    } else {
        io->write_word(io->opaque, 0x00ff9c64u, (uint16_t)value);
        io->write_word(io->opaque, 0x00ff9c66u, 1u);
    }
}
