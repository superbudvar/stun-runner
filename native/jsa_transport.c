/* 68010 <-> JSA-II window transport.  See jsa_transport.h for provenance. */
#include "jsa_transport.h"

#include "../reproduction/sound/jsa_latch.h"

/* Recover one byte's value from the 16-bit bus word and lane mask the 68010
 * drove for it.
 *
 * The even lane (mask 0xFF00) is the one MAME actually produced for the only
 * access we observed (frame 447: bus 0x1E1E / mask 0xFF00 / candidate 0x1E),
 * so that lane goes through the observed extractor and the observation stays
 * load-bearing rather than decorative.
 *
 * The odd lane (mask 0x00FF) is this transport's documented extension of the
 * 68010 bus rule.  stunrun_jsa_command_byte() refuses it on purpose -- it
 * reports only what was observed, and jsa_latch_test.c:42 pins that refusal --
 * so the odd lane is taken directly here.  If the observed extractor ever
 * declines the even lane too, we propagate "no byte" rather than guess. */
static int lane_byte(unsigned int bus_word, unsigned int lane_mask)
{
    if (lane_mask == 0xFF00u)
        return stunrun_jsa_command_byte(bus_word, 0xFF00u);
    return (int)(bus_word & 0x00FFu);
}

int stunrun_jsa_in_window(unsigned int address)
{
    return (address >= STUNRUN_JSA_XFER_BASE &&
            address <= STUNRUN_JSA_XFER_LAST) ? 1 : 0;
}

unsigned int stunrun_jsa_lane_mask(unsigned int address, unsigned int width)
{
    /* A 68000/68010 byte access drives D8-D15 from an even address and
     * D0-D7 from an odd one; wider accesses drive both lanes per byte cell.
     * MAME reports the affected bus lanes in mem_mask. */
    unsigned int mask = 0u;
    unsigned int lane;
    if (width == 0u || width > STUNRUN_JSA_XFER_MAX_BYTES)
        return 0u;
    for (lane = 0u; lane < width; lane++) {
        if ((address + lane) & 1u)
            mask |= 0x00FFu;
        else
            mask |= 0xFF00u;
    }
    return mask;
}

unsigned int stunrun_jsa_command_bytes(unsigned int address, unsigned int width,
                                       unsigned int value,
                                       uint8_t *out_bytes)
{
    unsigned int count = 0u;
    unsigned int i;

    if (!stunrun_jsa_in_window(address))
        return 0u;
    if (width == 0u || width > STUNRUN_JSA_XFER_MAX_BYTES)
        return 0u;

    /* Big-endian byte order: the highest byte of the access lands at the
     * lowest address, so it is delivered first and is the byte a single-byte
     * ROM store would have latched. */
    for (i = 0u; i < width; i++) {
        unsigned int shift = (width - 1u - i) * 8u;
        unsigned int cell = address + i;
        unsigned int byte = (value >> shift) & 0xFFu;
        unsigned int bus_word = stunrun_jsa_lane_mask(cell, 1u) == 0xFF00u
                                    ? byte << 8
                                    : byte;
        int seen = lane_byte(bus_word, stunrun_jsa_lane_mask(cell, 1u));
        if (seen == STUNRUN_JSA_NO_BYTE)
            continue;
        if (out_bytes)
            out_bytes[count] = (uint8_t)seen;
        count++;
    }
    return count;
}

unsigned int stunrun_jsa_transport_write(struct stunrun_jsa_latches *latches,
                                         unsigned int address, unsigned int width,
                                         unsigned int value)
{
    uint8_t bytes[STUNRUN_JSA_XFER_MAX_BYTES];
    unsigned int count;
    unsigned int i;

    if (latches == NULL)
        return 0u;

    count = stunrun_jsa_command_bytes(address, width, value, bytes);
    for (i = 0u; i < count; i++) {
        /* Each byte address of an 8-bit device invokes main_command_w once;
         * the board's single holding register keeps the last value. */
        (void)stunrun_jsa_main_command_write(latches, bytes[i]);
    }
    return count;
}

int stunrun_jsa_transport_read(struct stunrun_jsa_latches *latches,
                               unsigned int address, unsigned int width,
                               uint8_t *out_byte)
{
    int byte;

    if (latches == NULL)
        return 0;
    if (!stunrun_jsa_in_window(address))
        return 0;
    if (width == 0u || width > STUNRUN_JSA_XFER_MAX_BYTES)
        return 0;

    /* Only the first byte cell of the access can carry the response; the
     * remaining bytes are the not-ready value on the real board.  The command
     * latch is never touched here. */
    byte = stunrun_jsa_main_response_read(latches);
    if (byte == STUNRUN_JSA_NO_BYTE)
        return 0;
    if (out_byte)
        *out_byte = (uint8_t)byte;
    return 1;
}