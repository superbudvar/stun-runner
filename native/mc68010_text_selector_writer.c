/* MC68010 text selector dispatch and direct text draw.
 * See mc68010_text_selector_writer.h and docs/mc68010-boot-findings.md.
 *
 * ROM addresses:
 *   $196   text_selector_dispatch
 *   $2580  text_draw_from_descriptor
 *
 * Both share the same inner loop as $25E8 (mc68010_text_writer.c):
 *   - 8 rows per character, 8 pixels per row
 *   - Bit 0 of the shifted glyph nibble → write colour low byte to HSTDAT
 *   - Bit 2 of the shifted glyph nibble → write colour high byte to HSTDAT
 *   - Cell stride: +0x10 per pixel
 *   - Row stride: +0x1000 per row
 *   - Character stride: -0x6FC0 (next cell back)
 *
 * Register names in comments are the ROM's (D0-D7, A0-A1).
 */
#include "mc68010_text_selector_writer.h"

/* ── ROM helpers ──────────────────────────────────────────────────────── */

static uint16_t
rom_word(const uint8_t *rom, uint32_t address)
{
    return (uint16_t)(((uint16_t)rom[address] << 8) |
                      (uint16_t)rom[address + 1]);
}

/* ── $196: selector dispatch ─────────────────────────────────────────────── */

/* D0.L = selector number (0..393 for the 394-entry table).
 *
 * Looks up the 4-byte selector descriptor at
 *     SELECTOR_TABLE + selector * 4
 *
 * Descriptor format:
 *   +0: VRAM x position byte (combined with +1)
 *   +1: VRAM y position byte
 *       → VRAM base = byte1 * 0xA0 * 0x100 + byte0 * 0x40 - 0x800000
 *   +2: colour (byte, replicated: low → bit 0, high → bit 2)
 *   +3: character count
 *   +4..+N: character codes (one byte each)
 *
 * The inner loop is identical to $25E8:
 *   - 8 rows per character, latching HSTADR once per row
 *   - 8 pixels per row via nibble-shifting
 *   - bit 0 of nibble → HSTDAT with colour_lo (byte-replicated)
 *   - bit 2 of nibble → HSTDAT with colour_hi (byte-replicated)
 *   - row stride: +0x1000, cell stride: +0x10, next char: -0x6FC0 */
void
stunrun_mc68010_text_selector_dispatch(stunrun_mc68010_text_port_t *port,
                                        const uint8_t *rom,
                                        uint16_t selector)
{
    uint32_t entry;
    uint32_t vram_base;
    uint8_t colour;
    uint8_t count;
    uint32_t addr;
    unsigned char_idx;

    if (port == NULL || rom == NULL) {
        return;
    }

    /* Selector table lookup */
    entry = STUNRUN_TEXT_SELECTOR_TABLE + ((uint32_t)selector * 4u);

    /* VRAM base: byte1 * 0xA0 * 0x100 + byte0 * 0x40 - 0x800000 */
    vram_base = (((uint32_t)rom[entry + 1] * 0xA0u) << 8u) |
                ((uint32_t)rom[entry + 0] * 0x40u) -
                0x800000u;

    colour = rom[entry + 2];
    count  = rom[entry + 3];

    addr = vram_base;

    for (char_idx = 0; char_idx < count; char_idx++) {
        uint8_t ch = rom[entry + 4 + char_idx];
        uint32_t glyph_byte_offset;
        uint16_t colour_lo;
        uint16_t colour_hi;
        unsigned row;

        /* Glyph byte offset: FONT_BASE + (ch - GLYPH_BIAS) * 16 */
        if (ch < STUNRUN_TEXT_GLYPH_BIAS) {
            continue;
        }
        glyph_byte_offset = STUNRUN_TEXT_FONT_BASE +
                           ((uint32_t)(ch - STUNRUN_TEXT_GLYPH_BIAS) *
                            STUNRUN_TEXT_GLYPH_STRIDE);

        /* Colour byte-replicated: low byte (bits 0-7), high byte (bits 8-15).
         * The ROM writes colour_lo when nibble bit 0 is set,
         * and colour_hi when nibble bit 2 is set. */
        colour_lo = (uint16_t)((uint16_t)colour | ((uint16_t)colour << 8));
        colour_hi = (uint16_t)((uint16_t)colour << 8);

        for (row = 0; row < 8; row++) {
            uint16_t glyph_word = rom_word(rom, glyph_byte_offset + row * 2u);
            unsigned p;

            stunrun_text_hstadr(port, addr);

            /* 8 pixels: test nibble bits 0 (→ colour_lo) and 2 (→ colour_hi) */
            for (p = 0; p < 8; p++) {
                uint16_t nibble = (uint16_t)((glyph_word >> (p * 4)) & 0x000Fu);
                if ((nibble & 1u) != 0u) {
                    stunrun_text_hstdat(port, colour_lo);
                }
                if ((nibble & 4u) != 0u) {
                    stunrun_text_hstdat(port, colour_hi);
                }
            }

            addr += 0x1000u;  /* next row */
        }

        /* Next character cell: -0x6FC0 offset from current */
        addr += (uint32_t)(-0x6FC0 - 0x80);  /* ≈ -0x6FC0 */
    }
}

/* ── $2580: text_draw_from_descriptor ─────────────────────────────────── */

/* D0 = VRAM base address (GSP local 32-bit)
 * A0 = pointer to character data (count bytes)
 * D7.W = colour word (byte-replicated: low byte = bits 0-7, high byte = 8-15)
 *
 * Unlike $196, this does NOT do a selector table lookup — the VRAM base,
 * character pointer, and colour are all supplied directly.
 * Used by road/display code that already has these computed. */
void
stunrun_mc68010_text_draw_from_descriptor(stunrun_mc68010_text_port_t *port,
                                          const uint8_t *rom,
                                          uint32_t vram_base,
                                          const uint8_t *characters,
                                          uint8_t count,
                                          uint16_t colour_word)
{
    uint32_t addr;
    uint16_t colour_lo;
    uint16_t colour_hi;

    if (port == NULL || rom == NULL || characters == NULL || count == 0) {
        return;
    }

    /* Colour word split: low byte → colour_lo, high byte → colour_hi.
     * Both are written as 16-bit words to HSTDAT. */
    colour_lo = (uint16_t)((uint8_t)(colour_word & 0xFFu) |
                          ((uint16_t)(uint8_t)(colour_word & 0xFFu) << 8));
    colour_hi = (uint16_t)((uint8_t)(colour_word >> 8) |
                          ((uint16_t)(uint8_t)(colour_word >> 8) << 8));

    addr = vram_base;

    while (count > 0) {
        uint8_t ch = *characters++;
        uint32_t glyph_off;
        unsigned row;

        count--;

        if (ch < STUNRUN_TEXT_GLYPH_BIAS) {
            continue;
        }

        glyph_off = STUNRUN_TEXT_FONT_BASE +
                    ((uint32_t)(ch - STUNRUN_TEXT_GLYPH_BIAS) *
                     STUNRUN_TEXT_GLYPH_STRIDE);

        for (row = 0; row < 8; row++) {
            uint16_t glyph_word = rom_word(rom, glyph_off + row * 2u);
            unsigned p;
            uint32_t row_base = addr;

            stunrun_text_hstadr(port, row_base);

            for (p = 0; p < 8; p++) {
                uint16_t nibble = (uint16_t)((glyph_word >> (p * 4)) & 0x000Fu);
                if ((nibble & 1u) != 0u) {
                    stunrun_text_hstdat(port, colour_lo);
                }
                if ((nibble & 4u) != 0u) {
                    stunrun_text_hstdat(port, colour_hi);
                }
            }

            addr += 0x1000u;
        }

        /* Next character cell: -0x6FC0 */
        addr += (uint32_t)(-0x6FC0 - 0x80);
    }
}
