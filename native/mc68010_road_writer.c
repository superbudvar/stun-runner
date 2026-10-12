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
 *   $21462 road_fill_pattern
 *   $215DC road_table_write_loop
 *   $21602 road_delay
 *   $2166E road_lookup_and_write
 *   $216EA road_set_color_1
 *   $216B0 road_setup_strip
 *   $21700 road_enable_hstctl
 *   $22370 road_clear_hardware
 *   $22382 road_sequence
 *   $22468 road_trigger_fill
 *   $2256C road_sr_swap
 *   $22578 road_copy_words
 *   $2347E road_process_strip_data
 *   $234B6 road_expand_bits
 *   $23898 road_draw_strips
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

/* ── $21602: road_delay ─────────────────────────────────────────────── */

/* Simple delay loop: 0x96 iterations (150 decimal).
 * Used as a hardware settling delay. */
static void
stunrun_road_delay(void)
{
    volatile uint16_t i;
    for (i = 0; i < 150; i++) {
        /* empty - just burn cycles */
    }
}

/* ── $22370: road_clear_hardware ────────────────────────────────────── */

/* Clears two hardware locations at 0x60400c and 0x60401c,
 * then calls the delay routine. Leaf function. */
void
stunrun_mc68010_road_clear_hardware(stunrun_mc68010_road_port_t *port)
{
    if (port == NULL) {
        return;
    }
    port->bus_write(port->ctx, 0x0060400cu, 0x0000);
    stunrun_road_delay();
    port->bus_write(port->ctx, 0x0060401cu, 0x0000);
}

/* ── $215DC: road_table_write_loop ─────────────────────────────────── */

/* Writes a sequence of (address, word) pairs to the GSP host port.
 * The table starts at ROM address 0x215aa, with count at 0x215da.
 * Each entry is 6 bytes: 4-byte address + 2-byte word.
 * Writes through HSTADR/HSTDAT. */
void
stunrun_mc68010_road_table_write_loop(stunrun_mc68010_road_port_t *port,
                                       const uint8_t *rom)
{
    uint32_t table_base;
    uint16_t count;
    uint32_t addr;
    unsigned i;

    if (port == NULL || rom == NULL) {
        return;
    }

    table_base = 0x0215aau;
    count = rom_word(rom, 0x0215dau);

    for (i = 0; i < count; i++) {
        addr = rom_long(rom, table_base + i * 6u);
        port->bus_write(port->ctx, STUNRUN_TEXT_HSTADR_HI,
                        (uint16_t)(addr >> 16));
        port->bus_write(port->ctx, STUNRUN_TEXT_HSTADR_LO,
                        (uint16_t)(addr & 0xFFFFu));
        port->bus_write(port->ctx, STUNRUN_TEXT_HSTDAT,
                        rom_word(rom, table_base + i * 6u + 4u));
    }
}

/* ── $2166E: road_lookup_and_write ─────────────────────────────────── */

/* Table at 0x21650, indexed by D0 * 10.
 * Each entry is 8 bytes: long address, word count, 3 bytes of bitmask.
 * Reads bitmask bytes from A1 and produces a combined word in D3. */
void
stunrun_mc68010_road_lookup_and_write(stunrun_mc68010_road_port_t *port,
                                       const uint8_t *rom,
                                       uint16_t index)
{
    uint32_t table_base;
    uint32_t entry_addr;
    uint16_t loop_count;
    uint16_t result;
    unsigned i;

    if (port == NULL || rom == NULL) {
        return;
    }

    table_base = 0x021650u;
    entry_addr = table_base + (uint32_t)index * 10u;

    /* Read count from table entry + 4 */
    loop_count = rom_word(rom, entry_addr + 4u);
    result = 0;

    for (i = 0; i < loop_count; i++) {
        /* Read 3 bitmask bytes and combine */
        uint8_t byte0 = rom[entry_addr + 6u];
        uint8_t byte1 = rom[entry_addr + 7u];
        uint8_t byte2 = rom[entry_addr + 8u];

        /* D0 = byte0 & byte1, shift left by byte2 */
        uint16_t combined = (uint16_t)((uint16_t)byte0 & (uint16_t)byte1);
        if (byte2 > 0) {
            combined = (uint16_t)(combined << byte2);
        }
        result = (uint16_t)(result | combined);
    }

    /* Write address and data to GSP host port */
    port->bus_write(port->ctx, STUNRUN_TEXT_HSTADR_HI,
                    (uint16_t)(rom_long(rom, entry_addr) >> 16));
    port->bus_write(port->ctx, STUNRUN_TEXT_HSTADR_LO,
                    (uint16_t)(rom_long(rom, entry_addr) & 0xFFFFu));
    port->bus_write(port->ctx, STUNRUN_TEXT_HSTDAT, result);
}

/* ── $21462: road_fill_pattern ─────────────────────────────────────── */

/* Fills two VRAM regions with 0xFF pattern.
 * Region 1: -0xB7FFF60 to -0xB7FFF50 (16 words of 0xFF)
 * Region 2: -0xB000000 to -0xA800000 (256 words of 0xFF, stepping +0x101)
 * Used to initialize road strip buffers. */
void
stunrun_mc68010_road_fill_pattern(stunrun_mc68010_road_port_t *port)
{
    uint16_t i;

    if (port == NULL) {
        return;
    }

    /* Region 1: latches at -0xB7FFF60 and -0xB7FFF50 */
    port->bus_write(port->ctx, STUNRUN_TEXT_HSTADR_HI, 0xB7FFu);
    port->bus_write(port->ctx, STUNRUN_TEXT_HSTADR_LO, 0xFF60u);
    port->bus_write(port->ctx, STUNRUN_TEXT_HSTDAT, 0x0000);

    port->bus_write(port->ctx, STUNRUN_TEXT_HSTADR_HI, 0xB7FFu);
    port->bus_write(port->ctx, STUNRUN_TEXT_HSTADR_LO, 0xFF50u);
    port->bus_write(port->ctx, STUNRUN_TEXT_HSTDAT, 0x0000);

    /* Region 2: -0xB000000 step 0x101 for 256 words */
    for (i = 0; i < 256; i++) {
        port->bus_write(port->ctx, STUNRUN_TEXT_HSTADR_HI, 0xB000u);
        port->bus_write(port->ctx, STUNRUN_TEXT_HSTADR_LO, (uint16_t)(i * 0x101u));
        port->bus_write(port->ctx, STUNRUN_TEXT_HSTDAT, 0x00FFu);
    }
}

/* ── $216EA: road_set_color_1 ──────────────────────────────────────── */

/* Sets D0 to -0x3FFFFEB0 and writes colour 1 to HSTDAT. */
void
stunrun_mc68010_road_set_color_1(stunrun_mc68010_road_port_t *port)
{
    if (port == NULL) {
        return;
    }
    port->bus_write(port->ctx, STUNRUN_TEXT_HSTADR_HI, 0xC000u);
    port->bus_write(port->ctx, STUNRUN_TEXT_HSTADR_LO, 0x0000u);
    port->bus_write(port->ctx, STUNRUN_TEXT_HSTDAT, 0x0001);
}

/* ── $216B0: road_setup_strip ─────────────────────────────────────── */

/* Sets up VRAM region at -0xB800000 with address manipulation.
 * Reads a byte from 0x2161d and conditionally inverts D0. */
void
stunrun_mc68010_road_setup_strip(stunrun_mc68010_road_port_t *port,
                                 const uint8_t *rom)
{
    uint16_t d0_val;
    uint8_t flag;

    if (port == NULL || rom == NULL) {
        return;
    }

    /* Start with D0 = 0, then logical shift right by 8 */
    d0_val = 0x0000;

    flag = rom[0x02161du];

    /* If flag is zero, NOT D0 (makes it 0xFFFF) */
    if (flag == 0) {
        d0_val = (uint16_t)(~d0_val);
    }

    /* AND with -0x4 (0xFFFC) then OR with 0x3 (0xFFFF) */
    d0_val = (uint16_t)((d0_val & 0xFFFC) | 0x0003);

    /* Write to -0x3FFFFF70 first */
    port->bus_write(port->ctx, STUNRUN_TEXT_HSTADR_HI, 0xC000u);
    port->bus_write(port->ctx, STUNRUN_TEXT_HSTADR_LO, 0x0000u);
    port->bus_write(port->ctx, STUNRUN_TEXT_HSTDAT, d0_val);

    /* Then set address to -0xB800000 and write 0 */
    port->bus_write(port->ctx, STUNRUN_TEXT_HSTADR_HI, 0xB800u);
    port->bus_write(port->ctx, STUNRUN_TEXT_HSTADR_LO, 0x0000u);
    port->bus_write(port->ctx, STUNRUN_TEXT_HSTDAT, 0x0000);
}

/* ── $21700: road_enable_hstctl ────────────────────────────────────── */

/* Enables HSTCTL auto-increment and fills VRAM region with 0xFFFF.
 * Region: -0xC000000 for 96 words (loop D2=0x5F, D1=0xFF).
 * Clears watchdog at end. */
void
stunrun_mc68010_road_enable_hstctl(stunrun_mc68010_road_port_t *port)
{
    unsigned i, j;

    if (port == NULL) {
        return;
    }

    /* ori.w #$1800,(0x00C00008).l — enable auto-increment */
    port->bus_write(port->ctx, STUNRUN_TEXT_HSTCTL, 0x1800u);

    /* Fill -0xC000000 with 0xFFFF × 96 */
    for (i = 0; i < 0x60; i++) {  /* D2 = 0x5F = 95 iterations */
        for (j = 0; j < 256; j++) {  /* D1 = 0xFF = 255 iterations */
            port->bus_write(port->ctx, STUNRUN_TEXT_HSTADR_HI, 0xC000u);
            port->bus_write(port->ctx, STUNRUN_TEXT_HSTADR_LO, 0x0000u);
            port->bus_write(port->ctx, STUNRUN_TEXT_HSTDAT, 0xFFFFu);
        }
    }

    /* clr.w $608000.l — watchdog */
    port->bus_write(port->ctx, 0x00608000u, 0x0000);
}

/* ── $234B6: road_expand_bits ──────────────────────────────────────── */

/* Bit expansion helper: takes a word from table and expands to 32-bit value.
 * Uses rotate-left-through-carry to spread bits. */
static uint32_t
stunrun_road_expand_bits(uint16_t word)
{
    uint32_t d6 = 0;
    uint16_t d5 = word;
    unsigned i;

    /* roxl.w #1,D5 followed by roxl.l #2,D6, 16 times */
    for (i = 0; i < 16; i++) {
        /* Rotate D5 left through carry by 1 */
        d5 = (uint16_t)((d5 << 1) | (d6 >> 31));
        /* Rotate D6 left through carry by 2 */
        d6 = (d6 << 2) | (d5 >> 15);
    }

    /* lsr.l #1,D6 — final shift */
    d6 >>= 1;

    return d6;
}

/* ── $2347E: road_process_strip_data ───────────────────────────────── */

/* Reads strip data from ROM table at 0x22E1E, processes 16 words.
 * D0 = base address, D1.B = something for table indexing.
 * Writes processed data to GSP via HSTADR/HSTDAT. */
void
stunrun_mc68010_road_process_strip_data(stunrun_mc68010_road_port_t *port,
                                        const uint8_t *rom,
                                        uint32_t vram_base,
                                        uint8_t table_index)
{
    uint32_t table_base;
    uint16_t d3;
    unsigned strip;

    if (port == NULL || rom == NULL) {
        return;
    }

    /* table_base = 0x22E1E + (table_index - 0x2A) * 32 */
    table_base = 0x022E1Eu + ((uint32_t)table_index - 0x2Au) * 32u;

    /* D3 = 0xF, loop runs 16 times via dbf */
    d3 = 0xF;

    for (strip = 0; strip <= d3; strip++) {
        uint32_t d0 = vram_base;
        port->bus_write(port->ctx, STUNRUN_TEXT_HSTADR_HI,
                        (uint16_t)(d0 >> 16));
        port->bus_write(port->ctx, STUNRUN_TEXT_HSTADR_LO,
                        (uint16_t)(d0 & 0xFFFFu));

        /* Call expand_bits on the word at table_base */
        {
            uint16_t word = rom_word(rom, table_base + strip * 2u);
            uint32_t expanded = stunrun_road_expand_bits(word);
            port->bus_write(port->ctx, STUNRUN_TEXT_HSTDAT,
                            (uint16_t)(expanded >> 16));
            port->bus_write(port->ctx, STUNRUN_TEXT_HSTADR_LO,
                            (uint16_t)((d0 + 0x10u) & 0xFFFFu));
            port->bus_write(port->ctx, STUNRUN_TEXT_HSTDAT,
                            (uint16_t)(expanded & 0xFFFFu));
        }

        vram_base += 0x400u;  /* addi.l #$400,D0 */
    }

    /* After loop: subi.l #$4000,D0 then addi.l #$20,D0 */
    /* Address adjustment not needed in C version - caller manages VRAM base */
    (void)d3;  /* suppress unused warning */
}

/* ── $23898: road_draw_strips ──────────────────────────────────────── */

/* Main road strip drawer. Reads road strip data and draws to VRAM.
 * Stack params: word at 0xf = count, word at 0x6 = x, word at 0xa = y.
 * Reads bytes from A0 and writes processed data via road_process_strip_data. */
void
stunrun_mc68010_road_draw_strips(stunrun_mc68010_road_port_t *port,
                                  const uint8_t *rom,
                                  const uint8_t *strip_data,
                                  uint16_t count,
                                  uint16_t x,
                                  uint16_t y)
{
    uint32_t vram_base;
    uint16_t x_masked, y_masked;
    const uint8_t *p;

    if (port == NULL || rom == NULL || strip_data == NULL) {
        return;
    }

    /* Compute VRAM base from x,y:
     * vram = (y & 0x1FF) << 9 | (x & 0x1F0) << 2 | 0x2000000
     * Actually the ROM does: D0 = (x << 8) | x (byte replicated)
     * Then D0 = (D0 & 0x1F0) | ((D1 & 0x1FF) << 9) + 0x2000000 */
    x_masked = (uint16_t)(x & 0x1F0u);
    y_masked = (uint16_t)(y & 0x1FFu);
    vram_base = 0x2000000u + ((uint32_t)y_masked << 9) + ((uint32_t)x_masked << 2);

    /* Save HSTCTL */
    /* andi.w #-0x1801,(0x00C00008).l — clear bits */

    p = strip_data;
    while (count > 0) {
        uint8_t byte = *p++;

        if (byte == 0) {
            break;  /* terminator */
        }

        if (byte == 0x20) {
            byte = 0x2A;  /* space to asterisk */
        }

        /* Process this strip */
        stunrun_mc68010_road_process_strip_data(port, rom, vram_base, byte);

        count--;
    }

    /* Restore HSTCTL */
}

/* ── $22382: road_sequence ──────────────────────────────────────────── */

/* Main road drawing sequence. Calls multiple subroutines in order:
 * 1. road_clear_hardware
 * 2. road_lookup_and_write with index 2
 * 3. road_table_write_loop
 * 4. road_set_color_1
 * 5. road_setup_strip
 * 6. road_lookup_and_write with index 1, then 0
 * 7. road_fill_pattern
 * 8. road_enable_hstctl (via jsr to $441ec+$404)
 *
 * This is the main entry point for road rendering. */
void
stunrun_mc68010_road_sequence(stunrun_mc68010_road_port_t *port,
                              const uint8_t *rom)
{
    if (port == NULL || rom == NULL) {
        return;
    }

    /* Save registers (movem done by caller) */

    /* Step 1: clear hardware */
    stunrun_mc68010_road_clear_hardware(port);

    /* Step 2: lookup_and_write with D0=2 */
    stunrun_mc68010_road_lookup_and_write(port, rom, 2);

    /* Step 3: table_write_loop */
    stunrun_mc68010_road_table_write_loop(port, rom);

    /* Step 4: set_color_1 */
    stunrun_mc68010_road_set_color_1(port);

    /* Step 5: setup_strip */
    stunrun_mc68010_road_setup_strip(port, rom);

    /* Step 6: lookup_and_write with D0=1 and D0=0 */
    stunrun_mc68010_road_lookup_and_write(port, rom, 1);
    stunrun_mc68010_road_lookup_and_write(port, rom, 0);

    /* Step 7: fill_pattern */
    stunrun_mc68010_road_fill_pattern(port);

    /* Step 8: road_enable_hstctl (called via jsr to $441ec+$404 in ROM)
     * In the native version we call it directly */
    stunrun_mc68010_road_enable_hstctl(port);
}

/* ── $22468: road_trigger_fill ─────────────────────────────────────── */

/* Simple wrapper that calls road_enable_hstctl with D0=0.
 * Used as one of the road sequence steps. */
void
stunrun_mc68010_road_trigger_fill(stunrun_mc68010_road_port_t *port)
{
    if (port == NULL) {
        return;
    }
    stunrun_mc68010_road_enable_hstctl(port);
}

/* ── $2256C: road_sr_swap ──────────────────────────────────────────── */

/* SR swap stub: saves SR, clears SR, restores from stack.
 * Used for interrupt handling during road operations. */
void
stunrun_mc68010_road_sr_swap(void)
{
    /* In the ROM this just swaps SR with D1 */
    /* The C version is a no-op for the basic port */
}

/* ── $22578: road_copy_words ───────────────────────────────────────── */

/* Memory copy routine: copies D0.W words from A0 to A1.
 * Disables interrupts during copy (SR = 0x0700).
 * Clears several hardware registers before and after. */
void
stunrun_mc68010_road_copy_words(stunrun_mc68010_road_port_t *port,
                                 const uint16_t *src,
                                 uint16_t *dst,
                                 uint16_t count)
{
    unsigned i;

    if (port == NULL || src == NULL || dst == NULL) {
        return;
    }

    /* Clear hardware registers */
    port->bus_write(port->ctx, 0x00604008u, 0x0000);
    port->bus_write(port->ctx, 0x0060401Au, 0x0000);

    /* Copy words */
    for (i = 0; i < count; i++) {
        dst[i] = src[i];
    }

    /* Clear more registers */
    port->bus_write(port->ctx, 0x00604018u, 0x0000);
    port->bus_write(port->ctx, 0x0060400Au, 0x0000);
}
