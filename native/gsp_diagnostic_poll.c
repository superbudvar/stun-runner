#include "gsp_diagnostic_poll.h"

#include <assert.h>

enum {
    GSP_RAM_BASE = 0xFFC00000u,
    GSP_RAM_STATUS = 0xFFC00020u,
    STATUS_WORD = 0xFF9042u,
    DIAGNOSTIC_BUFFER = 0xFF9048u,
    READY_MARKER = 0xFF945Eu
};

size_t stunrun_gsp_diagnostic_poll(stunrun_gsp_diagnostic_io_t *io)
{
    uint32_t i;
    uint32_t status;
    uint16_t a1;
    uint16_t a0 = 1;

    assert(io != NULL);
    io->watchdog_clear(io->ctx);
    io->call_gsp_reset(io->ctx);
    io->watchdog_clear(io->ctx);

    for (i = 0; i < 8; ++i)
        io->write_main_long(io->ctx, READY_MARKER, 0x01010101u);

    io->call_10932(io->ctx);
    io->call_4A48(io->ctx);

    /* D0=$1D4C is the observed timeout.  The bounded loop preserves the
     * selected ROM path while avoiding an unbounded host-side wait. */
    for (i = 0; i < 0x1D4Cu; ++i) {
        status = io->read_main_long(io->ctx, STATUS_WORD);
        io->watchdog_clear(io->ctx);
        if (status >= 0x1D4Cu)
            break;
    }
    if (status < 0x1D4Cu) {
        io->write_main_byte(io->ctx, READY_MARKER, 0xFFu);
        io->watchdog_clear(io->ctx);
        io->call_gsp_reset(io->ctx);
        io->watchdog_clear(io->ctx);
        return 1;
    }

    for (;;) {
        io->set_host_address(io->ctx, GSP_RAM_BASE);
        a1 = io->read_host_data(io->ctx);
        if (a1 <= a0)
            break;
        a0 = a1;
        if (a0 == 8)
            break;
    }

    if (a0 != 8) {
        io->write_main_byte(io->ctx, READY_MARKER, 0xFFu);
        io->watchdog_clear(io->ctx);
        io->call_gsp_reset(io->ctx);
        io->watchdog_clear(io->ctx);
        return 1;
    }

    io->call_4A48(io->ctx);
    for (i = 0; i < 16; ++i) {
        uint16_t value;
        io->set_host_address(io->ctx, GSP_RAM_STATUS + i * 0x10u);
        value = io->read_host_data(io->ctx);
        io->write_main_long(io->ctx, DIAGNOSTIC_BUFFER, value);
        io->write_main_byte(io->ctx, READY_MARKER, (uint8_t)value);
        io->write_main_byte(io->ctx, READY_MARKER + 1u,
                            (uint8_t)(value >> 8));
    }
    io->watchdog_clear(io->ctx);
    io->call_gsp_reset(io->ctx);
    io->watchdog_clear(io->ctx);
    return 16;
}
