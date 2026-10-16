/* See road_fifo.h for provenance and confidence notes. */
#include "road_fifo.h"

static uint32_t pack_long(uint16_t high, uint16_t low)
{
    return ((uint32_t)high << 16) | (uint32_t)low;
}

size_t stunrun_road_fifo_drain(const uint16_t *base, size_t words,
                               uint32_t *fifo_out, size_t fifo_cap)
{
    size_t i;

    if (base == NULL || fifo_out == NULL ||
        words != STUNRUN_ROAD_FIFO_SOURCE_WORDS ||
        fifo_cap < STUNRUN_ROAD_FIFO_WRITES) {
        return 0;
    }
    for (i = 0; i < STUNRUN_ROAD_FIFO_WRITES; i++)
        fifo_out[i] = pack_long(base[i * 2u], base[i * 2u + 1u]);
    return STUNRUN_ROAD_FIFO_WRITES;
}

size_t stunrun_road_fifo_host_writes(const uint32_t *longs, size_t count,
                                     uint16_t *host_out, size_t host_cap)
{
    size_t i;

    if (longs == NULL || host_out == NULL || host_cap < count * 2u)
        return 0u;
    for (i = 0; i < count; i++) {
        /* 68010 big-endian move.l: high halfword at the base address first. */
        host_out[i * 2u] = (uint16_t)(longs[i] >> 16);
        host_out[i * 2u + 1u] = (uint16_t)(longs[i] & 0xFFFFu);
    }
    return count * 2u;
}

size_t stunrun_road_fifo_drain_range(const uint16_t *base, size_t words,
                                     size_t source_offset,
                                     size_t source_count,
                                     uint32_t *fifo_out, size_t fifo_cap)
{
    size_t writes;
    size_t i;

    if (base == NULL || fifo_out == NULL ||
        source_offset > words || source_count > words - source_offset ||
        (source_offset & 1u) != 0u || (source_count & 1u) != 0u)
        return 0u;
    writes = source_count / 2u;
    if (fifo_cap < writes)
        return 0u;
    for (i = 0; i < writes; i++)
        fifo_out[i] = pack_long(base[source_offset + i * 2u],
                                base[source_offset + i * 2u + 1u]);
    return writes;
}