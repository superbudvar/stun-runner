/* MC68010 text dispatch and selector-draw routines at ROM $196-$2580.
 * See docs/mc68010-boot-findings.md for provenance.
 *
 * These are the "direct" text routines that bypass the selector table lookup
 * done by $25E8 (stunrun_mc68010_text_draw).  They take:
 *   D0 = pre-computed VRAM base address (GSP local)
 *   A0 = pointer to character data (descriptor or string)
 *   D7 = colour (word, byte-replicated on write)
 *
 * Unlike $25E8 which reads a selector number and does a table lookup,
 * these routines accept a VRAM address directly.
 *
 * ROM coverage:
 *   $196   text_selector_dispatch  — D0 selector lookup + dispatch loop
 *   $2580  text_draw_from_descriptor — A0 char pointer, D0 VRAM base, D7 colour
 *
 * Relationship to $25E8 (mc68010_text_writer.c):
 *   All three share the same inner loop (8 rows × 8 pixels).
 *   $196   and $2580 differ only in how the colour and character pointer
 *   are obtained: $196 reads colour from a selector-descriptor table,
 *   $2580 takes it as a direct argument in D7.
 *   See mc68010_text_writer.h for the shared glyph/host-port encoding.
 */
#ifndef STUNRUN_MC68010_TEXT_SELECTOR_WRITER_H
#define STUNRUN_MC68010_TEXT_SELECTOR_WRITER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Port interface (identical to mc68010_text_writer.h) ─────────────── */

typedef struct stunrun_mc68010_text_port {
    void *ctx;
    void (*bus_write)(void *ctx, uint32_t address, uint16_t value);
    uint32_t hstadr;
} stunrun_mc68010_text_port_t;

static inline void
stunrun_text_hstadr_hi(stunrun_mc68010_text_port_t *port, uint16_t value)
{
    port->hstadr = (port->hstadr & 0x0000FFFFu) | ((uint32_t)value << 16);
    port->bus_write(port->ctx, 0x00C00002u, value);
}

static inline void
stunrun_text_hstadr_lo(stunrun_mc68010_text_port_t *port, uint16_t value)
{
    port->hstadr = (port->hstadr & 0xFFFF0000u) | (uint32_t)value;
    port->bus_write(port->ctx, 0x00C00004u, value);
}

static inline void
stunrun_text_hstadr(stunrun_mc68010_text_port_t *port, uint32_t addr)
{
    stunrun_text_hstadr_hi(port, (uint16_t)(addr >> 16));
    stunrun_text_hstadr_lo(port, (uint16_t)(addr & 0xFFFFu));
}

static inline void
stunrun_text_hstdat(stunrun_mc68010_text_port_t *port, uint16_t value)
{
    port->bus_write(port->ctx, 0x00C0000Cu, value);
}

static inline void
stunrun_text_hstctl_and(stunrun_mc68010_text_port_t *port, uint16_t mask)
{
    (void)port;
    (void)mask;
    /* Caller performs read-modify-write on the bus */
}

/* ── Constants (shared with mc68010_text_writer.h) ──────────────────── */

#define STUNRUN_TEXT_FONT_BASE    0x01FBCu
#define STUNRUN_TEXT_GLYPH_STRIDE 16u
#define STUNRUN_TEXT_GLYPH_BIAS  0x2Au
#define STUNRUN_TEXT_SELECTOR_TABLE 0x10136u

/* ── $196: selector dispatch ─────────────────────────────────────────── */

/* Takes D0.L = selector number, looks up the descriptor at
 * SELECTOR_TABLE + selector * 4, and dispatches.
 *
 * The selector descriptor format (4 bytes):
 *   +0: VRAM x position (0x0..0xFF, × 0x40)
 *   +1: VRAM y position (0x0..0xFF, × 0xA000)
 *   +2: colour (byte, replicated as described for $25E8)
 *   +3: character count
 *   +4..+N: character codes
 *
 * After the loop returns; caller is responsible for any final cleanup.
 *
 * The inner pixel loop is identical to $25E8: bit 0 → colour low byte,
 * bit 2 → colour high byte, cell stride +0x10, row stride +0x1000. */
void
stunrun_mc68010_text_selector_dispatch(stunrun_mc68010_text_port_t *port,
                                        const uint8_t *rom,
                                        uint16_t selector);

/* ── $2580: draw text from character array ──────────────────────────── */

/* Takes D0 = VRAM base address, A0 = pointer to character array,
 * D7.W = colour word (byte-replicated).
 *
 * This is the variant used by the road/display code that already has
 * a computed VRAM base and character data pointer.
 * The loop structure is identical to $25E8 and $196.
 *
 * rom: pointer to the ROM image, used to look up glyph data from the
 *      font table at STUNRUN_TEXT_FONT_BASE.  The glyph table is part
 *      of the same ROM image as the character data. */
void
stunrun_mc68010_text_draw_from_descriptor(stunrun_mc68010_text_port_t *port,
                                           const uint8_t *rom,
                                           uint32_t vram_base,
                                           const uint8_t *characters,
                                           uint8_t count,
                                           uint16_t colour_word);

#ifdef __cplusplus
}
#endif

#endif /* STUNRUN_MC68010_TEXT_SELECTOR_WRITER_H */
