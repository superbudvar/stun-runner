/* MC68010 road/FIFO routines.  See mc68010_road_writer.h and
 * docs/mc68010-boot-findings.md for provenance.
 *
 * ROM addresses:
 *   $22EC  road_write_fifo_words
 *   $2326  road_draw_n_polys
 *   $23F2  road_set_vram_base_F4000000
 *   $2418  road_vram_addr
 *   $2444  road_write_fifo_words_call  (leaf wrapper of $22EC)
 *   $245A  road_draw_n_polys_masked
 *   $2496  road_draw_n_polys_masked2
 *   $24B8  road_draw_quad
 *
 * Register names in comments are the ROM's (D0-D7, A0-A1).
 */
#include "mc68010_road_writer.h"

/* ── ROM helpers ──────────────────────────────────────────────────────── */

static uint16_t
rom_word(const uint8_t *rom, uint32_t address)
{
    return (uint16_t)(((uint16_t)rom[address] << 8) |
                      (uint16_t)rom[address + 1]);
}

static uint32_t
rom_long(const uint8_t *rom, uint32_t address)
{
    return ((uint32_t)rom[address] << 24) |
           ((uint32_t)rom[address + 1] << 16) |
           ((uint32_t)rom[address + 2] << 8) |
           ((uint32_t)rom[address + 3]);
}

/* ── $2418: road_vram_addr ───────────────────────────────────────────── */

/* The ROM computes this from D0/D1 at $2418.
 * x (D0 low byte) and y (D1 low byte) are combined as:
 *   vram = (y * 0x10) + (x * 0x2800) + 0x2000000
 * This puts the road polygon base at a fixed VRAM window location. */
uint32_t
stunrun_mc68010_road_vram_addr(uint8_t x, uint8_t y)
{
    return ((uint32_t)y * 0x10u) +
           ((uint32_t)x * 0x2800u) +
           0x2000000u;
}

/* ── $22EC: road_write_fifo_words ────────────────────────────────────── */

/* D0 = VRAM base (GSP local address, latched via HSTADR)
 * D1.B = character code (indexes the glyph table at FONT_BASE)
 *
 * The glyph is 8 × 8 pixels stored as 8 × 16-bit words.
 * Each word is written through HSTDAT directly (no bit-manipulation;
 * the road/FIFO path writes raw glyph words, not the 2bpp-expanded
 * colour words the text path uses).
 *
 * Row stride: +0x1000 per row (next scanline in the road buffer)
 * Cell stride: +0x10 per word (4 pixels of 4bpp)
 * After 8 rows: base advances by -0x6FC0 (wrapping to next character cell)
 *
 * Returns the address of the final write + 0x10 (for call chaining). */
uint32_t
stunrun_mc68010_road_write_fifo_words(stunrun_mc68010_road_port_t *port,
                                      const uint8_t *rom,
                                      uint32_t vram_base,
                                      uint8_t character)
{
    uint32_t addr;
    uint32_t glyph_off;
    unsigned row;

    if (port == NULL || rom == NULL) {
        return vram_base;
    }

    /* Glyph byte offset: FONT_BASE + (char - GLYPH_BIAS) * 16 bytes.
     * The glyph table entries are 16 bytes each (8 × 16-bit words). */
    if (character < STUNRUN_TEXT_GLYPH_BIAS) {
        return vram_base;
    }
    glyph_off = STUNRUN_TEXT_FONT_BASE +
                ((uint32_t)(character - STUNRUN_TEXT_GLYPH_BIAS) *
                 STUNRUN_TEXT_GLYPH_STRIDE);

    addr = vram_base;
    for (row = 0; row < 8; row++) {
        uint16_t glyph_word = rom_word(rom, glyph_off + row * 2u);
        /* Write one glyph row word through HSTDAT */
        stunrun_road_hstadr(port, addr);
        stunrun_road_hstdat(port, glyph_word);
        /* Advance 0x10 per row (4-pixel stride) */
        addr += 0x10;
    }

    /* After 8 rows, advance to next character cell:
     * 8 rows × 0x1000 - 8 cells × 0x40 = 0x7D00 - 0x280 = 0x6FC0 */
    return addr + (uint32_t)(-0x6FC0 - 0x80);  /* ≈ iVar1 + -0x6FC0 */
}

/* ── $23F2: set VRAM base F4000000, no colour ─────────────────────── */

/* movea.l #$F4000000,A0  →  lea $F4000000,A0
 * move.w (A0),$C0000C   →  writes 0x0000 (the long at $F4000000 is 0)
 * This latches the drawing window base without pushing a colour. */
void
stunrun_mc68010_road_set_vram_base_F4000000(stunrun_mc68010_road_port_t *port)
{
    if (port == NULL) {
        return;
    }
    /* The address $F4000000 is latched via HSTADR; the word at that
     * location in GSP local memory is 0x0000, so we write 0 to HSTDAT. */
    stunrun_road_hstadr(port, 0xF4000000u);
    stunrun_road_hstdat(port, 0x0000);
}

/* ── $2326: road_draw_n_polys ────────────────────────────────────────── */

/* Selector table lookup: descriptor at SELECTOR_TABLE + selector * 4.
 * Descriptor bytes:
 *   +0: VRAM base low  (combined with +1 to form offset × 0xA0 × 0x100)
 *   +1: VRAM base high
 *   +2: colour (byte)
 *   +3: poly count
 *
 * For each poly:
 *   - latch colour word (via $F4000000 + colour byte, byte-replicated)
 *   - call $22EC (write_fifo_words)
 *
 * Clears watchdog ($608000) at entry.
 * Masks HSTCTL to remove HLT and auto-increment bits. */
void
stunrun_mc68010_road_draw_n_polys(stunrun_mc68010_road_port_t *port,
                                   const uint8_t *rom,
                                   uint16_t selector)
{
    uint32_t vram_base;
    uint32_t selector_entry;
    uint8_t colour;
    uint8_t count;
    unsigned p;

    if (port == NULL || rom == NULL) {
        return;
    }

    /* clr.w $608000.l — watchdog reset */
    port->bus_write(port->ctx, 0x00608000u, 0x0000);

    /* andi.w #$E7FF,$C00008.l — clear HLT|INCR|INCW (FIFO mode) */
    port->bus_write(port->ctx, STUNRUN_TEXT_HSTCTL,
                     STUNRUN_TEXT_HSTCTL_KEEP);

    /* Selector table lookup: long at TABLE + selector * 4 */
    selector_entry = STUNRUN_TEXT_SELECTOR_TABLE +
                     ((uint32_t)(selector & 0xFFFFu) * 4u);
    vram_base = rom_long(rom, selector_entry);

    /* Colour: the ROM reads byte at descriptor+2 and byte-replicates it
     * through a HSTDAT write to $F4000000 (control_lo window). */
    colour = rom[selector_entry + 2];
    stunrun_road_hstadr(port, 0xF4000000u);
    stunrun_road_hstdat(port, (uint16_t)((uint16_t)colour << 8) |
                                        colour);

    /* Poly count in byte +3 */
    count = rom[selector_entry + 3];

    /* Draw N polys */
    for (p = 0; p < count; p++) {
        uint8_t ch = rom[selector_entry + 4 + p];
        stunrun_mc68010_road_write_fifo_words(port, rom, vram_base, ch);
        /* The ROM advances vram_base by +0x2800 per poly (next row band) */
        vram_base += 0x2800u;
    }
}

/* ── $24B8: road_draw_quad (4 polys) ─────────────────────────────────── */

/* andi.w #$E7FF,$C00008.l       — clear HSTCTL
 * movea.l #$F4000000,A0
 * move.w (A0),$C0000C           — colour 0 via window read
 * Calls road_write_fifo_words × 4 with consecutive characters.
 *
 * The four characters are stored at descriptor+4 through descriptor+7,
 * and the VRAM base for each is offset by 0x2800 from the previous. */
void
stunrun_mc68010_road_draw_quad(stunrun_mc68010_road_port_t *port,
                                 const uint8_t *rom,
                                 uint32_t vram_base,
                                 const uint8_t *characters)
{
    unsigned i;

    if (port == NULL || rom == NULL || characters == NULL) {
        return;
    }

    /* andi.w #$E7FF,$C00008.l */
    port->bus_write(port->ctx, STUNRUN_TEXT_HSTCTL,
                     STUNRUN_TEXT_HSTCTL_KEEP);

    /* Colour 0: latch 0xF4000000, write 0x0000 to HSTDAT */
    stunrun_road_hstadr(port, 0xF4000000u);
    stunrun_road_hstdat(port, 0x0000);

    /* Four polys at successive VRAM bases */
    for (i = 0; i < 4; i++) {
        stunrun_mc68010_road_write_fifo_words(port, rom, vram_base,
                                               characters[i]);
        vram_base += 0x2800u;
    }
}
