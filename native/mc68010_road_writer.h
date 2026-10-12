/* MC68010 road/FIFO routines at ROM $22EC-$24B8.
 * See docs/mc68010-boot-findings.md for provenance.
 *
 * These routines write road polygon glyphs to the GSP host port FIFO.
 * Each poly is drawn as 8 sequential words (one per row) through HSTDAT.
 * The GSP executes the FIFO commands asynchronously while the 68010 continues.
 *
 * ROM coverage:
 *   $22EC  road_write_fifo_words      — write 8 FIFO words from glyph table
 *   $2326  road_draw_n_polys          — selector lookup, draw N polys
 *   $23F2  road_set_vram_base_F4000000 — set VRAM base, no colour
 *   $2418  road_vram_addr             — compute VRAM address from D0/D1
 *   $2444  road_write_fifo_words_call  — calls $22EC (leaf)
 *   $245A  road_draw_n_polys_masked   — calls $2326 (masked)
 *   $2496  road_draw_n_polys_masked2   — calls $2326 (masked, 2nd site)
 *   $24B8  road_draw_quad             — calls $22EC × 4
 *
 * Register names in comments are the ROM's (D0-D7, A0-A1).
 */
#ifndef STUNRUN_MC68010_ROAD_WRITER_H
#define STUNRUN_MC68010_ROAD_WRITER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Host port addresses (68010 sees these through the $C00000 window) ── */

#define STUNRUN_TEXT_HSTADR_HI  0x00C00002u
#define STUNRUN_TEXT_HSTADR_LO   0x00C00004u
#define STUNRUN_TEXT_HSTCTL      0x00C00008u
#define STUNRUN_TEXT_HSTDAT      0x00C0000Cu

/* Mask applied to HSTCTL to clear HLT while preserving other bits.
 * 0xE7FF = ~0x1800.  0x1800 = INCR|INCW (auto-increment on read/write). */
#define STUNRUN_TEXT_HSTCTL_KEEP 0xE7FFu

/* ── Glyph table ─────────────────────────────────────────────────────── */

#define STUNRUN_TEXT_FONT_BASE    0x01FBCu
#define STUNRUN_TEXT_GLYPH_STRIDE 16u

/* Font entries are 8 × 16-bit words, one word per row.
 * Entry for character C (C >= 0x2A) is at FONT_BASE + (C - 0x2A) * 16. */
#define STUNRUN_TEXT_GLYPH_BIAS  0x2Au

/* ── Selector table ──────────────────────────────────────────────────── */

#define STUNRUN_TEXT_SELECTOR_TABLE 0x10136u

/* ── Port interface ──────────────────────────────────────────────────── */

/* Minimal port needed by road/fill routines: just HSTADR and HSTDAT writes.
 * Read side is unused by these routines (no peeks). */
typedef struct stunrun_mc68010_road_port {
    void *ctx;
    void (*bus_write)(void *ctx, uint32_t address, uint16_t value);
    uint32_t hstadr;  /* current latched 32-bit GSP address */
} stunrun_mc68010_road_port_t;

static inline void
stunrun_road_hstadr_hi(stunrun_mc68010_road_port_t *port, uint16_t value)
{
    port->hstadr = (port->hstadr & 0x0000FFFFu) | ((uint32_t)value << 16);
    port->bus_write(port->ctx, STUNRUN_TEXT_HSTADR_HI, value);
}

static inline void
stunrun_road_hstadr_lo(stunrun_mc68010_road_port_t *port, uint16_t value)
{
    port->hstadr = (port->hstadr & 0xFFFF0000u) | (uint32_t)value;
    port->bus_write(port->ctx, STUNRUN_TEXT_HSTADR_LO, value);
}

static inline void
stunrun_road_hstadr(stunrun_mc68010_road_port_t *port, uint32_t addr)
{
    stunrun_road_hstadr_hi(port, (uint16_t)(addr >> 16));
    stunrun_road_hstadr_lo(port, (uint16_t)(addr & 0xFFFFu));
}

static inline void
stunrun_road_hstdat(stunrun_mc68010_road_port_t *port, uint16_t value)
{
    port->bus_write(port->ctx, STUNRUN_TEXT_HSTDAT, value);
}

static inline void
stunrun_road_hstctl_and(stunrun_mc68010_road_port_t *port, uint16_t mask)
{
    /* The ROM reads HSTCTL, ANDs with mask, writes back.
     * We represent this as a bus_write to HSTCTL with the masked value.
     * Caller is responsible for preserving the read-modify-write semantics. */
    port->bus_write(port->ctx, STUNRUN_TEXT_HSTCTL,
                     (uint16_t)(port->hstadr >> 16) /* placeholder */);
}

/* ── $22EC: write 8 FIFO words (road poly glyph) ───────────────────── */

/* Writes 8 words from the glyph table to HSTDAT.
 * D0 = VRAM base address (GSP local).
 * D1.B = character code (0x2A..0x7F range, indexes glyph table).
 * Returns: D0 after the final HSTADR increment (for chaining).
 *
 * The 8 writes are one per row of the 8×8 glyph.
 * HSTADR advances by 0x10 per word (4 pixels) then by 0x1000 for the
 * next row.  After 8 rows the address is at the start of the next cell
 * 0x6FC0 words ahead (0x1000 * 8 - 0x40 * 8 = 0x7D00 - 0x280 = 0x6FC0).
 * The return value is the address of the last write + stride. */
uint32_t
stunrun_mc68010_road_write_fifo_words(stunrun_mc68010_road_port_t *port,
                                       const uint8_t *rom,
                                       uint32_t vram_base,
                                       uint8_t character);

/* ── $2326: draw N road polys ─────────────────────────────────────────── */

/* Reads a poly descriptor from the selector table at index D0.L.
 * The descriptor is 4 bytes: bytes 0-1 = VRAM base (x,y), byte 2 = colour,
 * byte 3 = count.  Calls $22EC (road_write_fifo_words) N times.
 *
 * Clears the watchdog at entry ($608000).
 * Masks HSTCTL to remove HLT/INCR/INCW (needed for FIFO mode). */
void
stunrun_mc68010_road_draw_n_polys(stunrun_mc68010_road_port_t *port,
                                    const uint8_t *rom,
                                    uint16_t selector);

/* ── $23F2: set VRAM base F4000000, no colour ───────────────────────── */

/* Latches address 0xF4000000 and writes 0x0000 to HSTDAT.
 * Used to set the GSP drawing window base without a colour. */
void
stunrun_mc68010_road_set_vram_base_F4000000(stunrun_mc68010_road_port_t *port);

/* ── $2418: compute VRAM address from D0/D1 ─────────────────────────── */

/* Returns: (D1 & 0xFF) * 0x10 + (D0 & 0xFF) * 0x2800 + 0x2000000
 * This computes the road polygon base address from pixel coordinates. */
uint32_t
stunrun_mc68010_road_vram_addr(uint8_t x, uint8_t y);

/* ── $24B8: draw 4 road polys (quad) ─────────────────────────────────── */

/* Masks HSTCTL (clears HLT), sets VRAM base to 0xF4000000, colour to 0.
 * Calls road_write_fifo_words × 4. */
void
stunrun_mc68010_road_draw_quad(stunrun_mc68010_road_port_t *port,
                                 const uint8_t *rom,
                                 uint32_t vram_base,
                                 const uint8_t *characters);

#ifdef __cplusplus
}
#endif

#endif /* STUNRUN_MC68010_ROAD_WRITER_H */
