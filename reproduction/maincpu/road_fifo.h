/* Settled road-buffer to GSP-FIFO drain, in C.
 *
 * Literal mechanism only. The FIFO payload's field semantics and the GSP's
 * interpretation remain unresolved.
 *
 * TRANSPORT (measured, not inferred). The drain is a 68010 `move.l` to the
 * GSP host data register: every FIFO datum is a 32-bit long delivered as TWO
 * 16-bit writes, the high halfword to $C0000C first and the low halfword to
 * $C0000E second. MAME's write trace over frames 1281..1300 of the canonical
 * `late_drive` run recorded 32524 writes at exactly those two addresses from
 * PC $02248E, in strict C-then-E order, 0 exceptions out of 16262 pairs.
 *
 * Consecutive source words are packed into consecutive longs -- source[2i] is
 * the HIGH halfword and source[2i+1] the LOW halfword of long i. This is NOT an
 * even-lane selection; see docs/mc68010-boot-findings.md section 75 for the
 * measurement that refuted the earlier even-lane reading and the slice that
 * carried it.
 */
#ifndef STUNRUN_ROAD_FIFO_H
#define STUNRUN_ROAD_FIFO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STUNRUN_ROAD_FIFO_SOURCE_WORDS 384u
#define STUNRUN_ROAD_FIFO_WRITES 192u
#define STUNRUN_ROAD_FIFO_PC 0x02248Eu
#define STUNRUN_ROAD_FIFO_DEST 0xC0000Cu
#define STUNRUN_ROAD_FIFO_DEST_HIGH 0xC0000Cu
#define STUNRUN_ROAD_FIFO_DEST_LOW 0xC0000Eu

/* Emit one complete base-buffer burst: 384 source halfwords packed into 192
 * 32-bit longs. This is a bus/lane contract, not payload semantics. */
size_t stunrun_road_fifo_drain(const uint16_t *base, size_t words,
                               uint32_t *fifo_out, size_t fifo_cap);

/* Split packed longs into the host writes the bus actually carries, in order:
 * high halfword to $C0000C, low halfword to $C0000E. Returns the number of
 * 16-bit host writes produced (2 * long_count), or 0 on invalid/undersized
 * input. No partial output. */
size_t stunrun_road_fifo_host_writes(const uint32_t *longs, size_t count,
                                     uint16_t *host_out, size_t host_cap);

/* Drain a bounded source sub-range using the same observed consecutive packing.
 * This represents a burst observed across multiple frames: source_offset must
 * be even, and long i is (base[source_offset + 2i] << 16) |
 * base[source_offset + 2i + 1]. */
size_t stunrun_road_fifo_drain_range(const uint16_t *base, size_t words,
                                     size_t source_offset,
                                     size_t source_count,
                                     uint32_t *fifo_out, size_t fifo_cap);

#ifdef __cplusplus
}
#endif

#endif /* STUNRUN_ROAD_FIFO_H */