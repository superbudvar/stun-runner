/* 68010 <-> JSA-II window transport for the Musashi core.
 *
 * MAME maps the whole 0x600000-0x603FFF window to ONE pair of handlers with
 * NO offset decode (harddriv.cpp:4806):
 *
 *     install_readwrite_handler(0x600000, 0x603fff,
 *         read8smo_delegate(*m_jsa, atari_jsa_base_device::main_response_r),
 *         write8smo_delegate(*m_jsa, atari_jsa_base_device::main_command_w),
 *         0xff00);
 *
 * So on the 68010 side there is nothing to classify per-address: every write
 * in the window is a command write and every read is a response read.  The
 * JSA-II 6502's own per-register decode lives at $2800-$2A06 in its private
 * space (atarijsa.cpp:116-131) and is what stunrun_jsa_classify_read() and
 * stunrun_jsa_is_response_reg() in reproduction/sound/jsa_latch.h describe;
 * neither applies to a 68010 address, so neither is called from here.
 *
 * Byte-lane provenance:
 * - A 68000/68010 byte access to an EVEN address drives D8-D15, so MAME
 *   reports mem_mask == 0xFF00.  That is exactly the tap captured at frame
 *   447: bus 0x1E1E / mask 0xFF00 / candidate 0x1E at PC 0x023EF6
 *   (reference/experiments/stunrun/sound-boundary-tap.metadata.json), and
 *   stunrun_jsa_command_byte(0x1E1E, 0xFF00) reproduces 0x1E.
 * - A byte access to an ODD address drives D0-D7 (mask 0x00FF).  No STUN
 *   Runner ROM access uses it, so this lane is a documented extension of the
 *   68010 bus rule, not an observation.  stunrun_jsa_command_byte() refuses
 *   it by design (jsa_latch_test.c:42 pins that), so the transport applies
 *   the odd lane itself.
 *
 * Multi-byte widths: the map is an 8-bit-wide device, so a wider access
 * splits into one handler call per byte address, big-endian (highest byte
 * at the lowest address), and the 8-bit holding register keeps the LAST
 * byte written.  Musashi hands us the whole word/long in one callback, so we
 * reproduce the split here.  MAME's src/emu/memory.{h,cpp} is trimmed out of
 * both reference trees, so memory_device::write's split arithmetic is
 * INFERENCE; the observable consequence (final latch byte) is asserted.
 */
#ifndef STUNRUN_JSA_TRANSPORT_H
#define STUNRUN_JSA_TRANSPORT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Defined in reproduction/sound/jsa_latch.h; forward-declared here so this
 * header stays free of the reproduction include path. */
struct stunrun_jsa_latches;

/* The 68010-visible JSA window (harddriv.cpp:4806). */
#define STUNRUN_JSA_XFER_BASE 0x600000u
#define STUNRUN_JSA_XFER_LAST 0x603FFFu

/* Widest split this transport models: one handler call per byte address. */
#define STUNRUN_JSA_XFER_MAX_BYTES 4u

/* Nonzero when `address` lands in the JSA window and therefore routes to the
 * command-write / response-read handlers.  Addresses immediately outside
 * (0x5FFFFF, 0x604000, ...) return zero, which is what stops the window
 * from leaking into its neighbours. */
int stunrun_jsa_in_window(unsigned int address);

/* Bus mask an N-byte access at `address` presents to its byte cell. */
unsigned int stunrun_jsa_lane_mask(unsigned int address, unsigned int width);

/* Split one 68010 bus write into the command bytes it delivers, in bus order.
 * Writes 0-4 bytes to out_bytes (pass NULL to only count) and returns the
 * count.  Returns 0 for addresses outside the window and for widths other
 * than 1/2/4.  Pure classification: no latch is touched. */
unsigned int stunrun_jsa_command_bytes(unsigned int address, unsigned int width,
                                       unsigned int value,
                                       uint8_t *out_bytes);

/* Latch the command bytes of one 68010 bus write, in bus order, via
 * stunrun_jsa_main_command_write() -- so a later byte overrides an earlier
 * one, matching the single 8-bit holding register on the real board.
 * Returns the number of command bytes latched. */
unsigned int stunrun_jsa_transport_write(struct stunrun_jsa_latches *latches,
                                         unsigned int address, unsigned int width,
                                         unsigned int value);

/* Serve one 68010 bus read from the window through
 * stunrun_jsa_main_response_read().  Returns 1 and writes *out_byte when a
 * response was pending, 0 otherwise -- in which case the caller presents the
 * not-ready byte 0xFF, the same value the stub returned before this slice.
 *
 * A read NEVER touches the command latch: command_pending is left alone by
 * construction, because only the write path calls
 * stunrun_jsa_main_command_write().
 *
 * Only the first byte of a multi-byte read can carry a response; the rest
 * are the not-ready byte, matching the 8-bit device split. */
int stunrun_jsa_transport_read(struct stunrun_jsa_latches *latches,
                               unsigned int address, unsigned int width,
                               uint8_t *out_byte);

#ifdef __cplusplus
}
#endif

#endif /* STUNRUN_JSA_TRANSPORT_H */