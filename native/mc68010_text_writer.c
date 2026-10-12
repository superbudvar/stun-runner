/* MC68010 text-draw routine at ROM $25E8-$269F.  See mc68010_text_writer.h
 * for provenance, the host-window encode and what is deliberately not
 * modelled.
 *
 * Register names in the comments (D0-D7, A0, A1) are the ROM's, not the C
 * function's.  They are kept because several of the oddities below -- which
 * register carries the address, which one the colour, which increment is a
 * device increment rather than a ROM one -- only read correctly next to the
 * original instruction.
 *
 * This is a decompilation, not an improvement: loop seeds, write order and
 * the read-modify-write on HSTDAT are transcribed as the ROM performs them.
 */
#include "mc68010_text_writer.h"

/* ── ROM helpers ──────────────────────────────────────────────────────── */

static uint16_t rom_word(const uint8_t *rom, uint32_t address)
{
    return (uint16_t)(((uint16_t)rom[address] << 8) | (uint16_t)rom[address + 1]);
}

static uint32_t rom_long(const uint8_t *rom, uint32_t address)
{
    return ((uint32_t)rom[address] << 24) | ((uint32_t)rom[address + 1] << 16) |
           ((uint32_t)rom[address + 2] << 8) | (uint32_t)rom[address + 3];
}

/* ── The host port, at the level the 68010 sees it ────────────────────── */

static void bus_read(stunrun_mc68010_text_port_t *port, uint32_t address,
                     uint16_t value)
{
    if (port->bus_read != NULL)
        port->bus_read(port->ctx, address, value);
}

static void bus_write(stunrun_mc68010_text_port_t *port, uint32_t address,
                      uint16_t value)
{
    if (port->bus_write != NULL)
        port->bus_write(port->ctx, address, value);
}

/* move.l D0,$c00002.l ($2656): the 68010 delivers the most significant word
 * first, and $C00002 is HSTADRH.  No extension word, so this is the short
 * (d8,An,Dn.L) form the ROM uses for `movea.l (A0,D0.l),A0` too. */
static void poke_hstadr_hi(stunrun_mc68010_text_port_t *port, uint16_t value)
{
    bus_write(port, STUNRUN_TEXT_HSTADR_HI, value);
    port->hstadr = (port->hstadr & 0x0000FFFFu) | ((uint32_t)value << 16);
}

static void poke_hstadr_lo(stunrun_mc68010_text_port_t *port, uint16_t value)
{
    bus_write(port, STUNRUN_TEXT_HSTADR_LO, value);
    port->hstadr = (port->hstadr & 0xFFFF0000u) | (uint32_t)value;
}

static uint16_t peek_hstadr_hi(stunrun_mc68010_text_port_t *port)
{
    uint16_t value = (uint16_t)(port->hstadr >> 16);
    bus_read(port, STUNRUN_TEXT_HSTADR_HI, value);
    return value;
}

static uint16_t peek_hstadr_lo(stunrun_mc68010_text_port_t *port)
{
    uint16_t value = (uint16_t)(port->hstadr & 0xFFFFu);
    bus_read(port, STUNRUN_TEXT_HSTADR_LO, value);
    return value;
}

/* move.w D7,$c0000c.l ($2666) and or.w D6,$c0000c.l ($2674) both land here. */
static int poke_hstdat(stunrun_mc68010_text_port_t *port, uint16_t value)
{
    bus_write(port, STUNRUN_TEXT_HSTDAT, value);
    if (port->gsp_write != NULL &&
        !port->gsp_write(port->ctx, port->hstadr & 0xFFFFFFF0u, value))
        return 0;
    /* HSTCTL 0x0800: post-increment after a HSTDAT write.  `$25EE` clears it,
     * so this never fires inside this routine -- it is here because the
     * register is caller-preset and the ROM's own `ori.w #$1800` sites would
     * otherwise have nothing to model against. */
    if ((port->hstctl & STUNRUN_TEXT_HSTCTL_WRITE_INCR) != 0u)
        port->hstadr += 0x10u;
    return 1;
}

/* The read half of `or.w D6,$c0000c.l`.  HSTCTL 0x1000 post-increments on a
 * HSTDAT read; `$25EE` clears that too.
 *
 * DECISION (ambiguous in the ROM, see mc68010_text_writer.h): what a host read
 * of HSTDAT returns.  Two readings survive:
 *
 *   (a) the word this port last stored at HSTADR & ~0xF.  This is what
 *       native/musashi_cpu.c `gsp_host_read_word` implements for the shell's
 *       flat word model, and it is the only reading under which `$2666`
 *       followed by `$2674` computes the value the routine plainly intends
 *       (write the low colour, then add the high colour to it).
 *   (b) the GSP memory word as the board actually keeps it.  MAME's
 *       `tms340x0_device::host_r` does read GSP memory, but for this board
 *       that word is the *output* of `hdgsp_vram_2bpp_w`, a masked
 *       read-modify-write of 32-bit VRAM words, so it is not the host word at
 *       all -- and how it comes out depends on the 4 MiB VRAM extent question
 *       in findings section 19, which is still open (stunrun-2ua).
 *
 * (a) is implemented.  (b) is not, and could not be without also answering
 * stunrun-2ua.  The difference is observable: it is exactly what a nibble-4
 * pixel with no preceding `move.w` writes, i.e. the pixels that carry only
 * the second colour plane. */
static uint16_t peek_hstdat(stunrun_mc68010_text_port_t *port)
{
    uint16_t value = 0u;
    if (port->gsp_read != NULL)
        value = port->gsp_read(port->ctx, port->hstadr & 0xFFFFFFF0u);
    bus_read(port, STUNRUN_TEXT_HSTDAT, value);
    if ((port->hstctl & STUNRUN_TEXT_HSTCTL_READ_INCR) != 0u)
        port->hstadr += 0x10u;
    return value;
}

/* ── The whole routine ────────────────────────────────────────────────── */

size_t stunrun_mc68010_text_draw(uint16_t selector, const uint8_t *rom,
                                 size_t rom_size,
                                 stunrun_mc68010_text_port_t *port)
{
    size_t table_entry = (size_t)STUNRUN_TEXT_SELECTOR_TABLE +
                         4u * (size_t)selector;
    uint32_t descriptor;
    uint32_t characters;
    uint32_t base;
    uint16_t low_colour;
    uint16_t high_colour;
    uint8_t count;
    size_t writes = 0u;

    if (rom == NULL || port == NULL)
        return 0u;
    if (table_entry + 4u > rom_size)
        return 0u;

    /* $25E8 clr.w $608000.l -- the watchdog kick.  Not incidental: it runs
     * before the descriptor is even read, and it is the first word the
     * routine puts on the bus. */
    bus_write(port, STUNRUN_TEXT_WATCHDOG, 0u);
    writes++;

    /* $25EE andi.w #$e7ff,$c00008.l.  Also not incidental: without it the
     * device would auto-increment HSTADR on every HSTDAT access and the
     * hand-rolled `addi.l #$10` at $267A would double-step. */
    bus_read(port, STUNRUN_TEXT_HSTCTL, port->hstctl);
    bus_write(port, STUNRUN_TEXT_HSTCTL,
              (uint16_t)(port->hstctl & STUNRUN_TEXT_HSTCTL_KEEP));
    port->hstctl &= STUNRUN_TEXT_HSTCTL_KEEP;
    writes++;

    /* $25F6 lea $10136.l,A0 / $25FC andi.l #$ffff,D0 / $2602 lsl.l #2,D0 /
     * $2604 movea.l (A0,D0.l),A0. */
    descriptor = rom_long(rom, (uint32_t)table_entry);
    if ((size_t)descriptor + 4u > rom_size)
        return 0u;
    characters = descriptor + 4u; /* $2632 adda.l #$4,A0 */

    /* $2608-$2610: D7 = descriptor[2] (colour); D6 = D7 << 8 (high colour). */
    low_colour = rom[descriptor + 2u];
    high_colour = (uint16_t)(low_colour << 8);

    /* $2612-$2628: D0 = descriptor[1] * $A0 * $100 + descriptor[0] * $40
     * - $800000.  The two `clr.l` at $2612/$261E are what keep the upper
     * bytes of D0/D1 clear before the byte loads; without them the result
     * would depend on the caller's D0, which `move.w #$SEL,D0` leaves
     * holding the selector in its high word. */
    base = (((uint32_t)rom[descriptor + 1u] * 0xA0u) << 8) +
           ((uint32_t)rom[descriptor + 0u] << 6) - 0x800000u;

    /* $262E move.b ($3,A0),D3 */
    count = rom[descriptor + 3u];
    if ((size_t)(characters + count) > rom_size)
        return 0u;

    /* $2638-$269C.  Three nested loops, and the order matters:
     *
     *   D3 ($269A subq.b #1,D3)  one pass per character, seeded from the
     *                            descriptor's count byte.
     *   D2 ($264E move.w #$7,D2, dbra at $2690)  eight passes per character,
     *                            one per glyph row, seeded OUTSIDE the D1
     *                            loop and re-decided by $268A.
     *   D1 ($2652 move.w #$7,D1, dbra at $2686)  eight passes per row, one
     *                            per font nibble, re-seeded at the top of
     *                            every D2 pass.
     *
     * A0 advances by exactly one per character ($2638 `move.b (A0)+`), which
     * is why A0 in the capture is descriptor + 5 + char_index.  A1 advances
     * by two per ROW, not per nibble: `move.w (A1)+,D5` at $265C sits above
     * the `dbra D1` at $2686, so one glyph is 16 bytes and A1 lands on the
     * next glyph's first word after the eight nibbles are consumed. */
    while (count != 0u) {
        long glyph;
        uint32_t word_address;
        uint8_t character = rom[characters];
        uint16_t row_seed;
        uint8_t pixel_seed;

        characters++;

        /* $263A subi.b #$2a,D1 / $263E andi.l #$ff,D1 / $2644 lsl.w #4,D1 /
         * $2646 lea $1fbc.l,A1 / $264C adda.w D1,A1.  `lsl.w` and `adda.w`
         * both work on a 16-bit quantity that is sign-extended on use, so a
         * character that underflows the $2A bias indexes backwards off the
         * font.  Nothing in the 394-descriptor table does; the bound below
         * turns it into an error rather than an out-of-range read. */
        glyph = (long)STUNRUN_TEXT_FONT_BASE +
                (long)(int16_t)(uint16_t)((uint8_t)(character -
                                                    STUNRUN_TEXT_GLYPH_BIAS)
                                         << 4);
        if (glyph < (long)STUNRUN_TEXT_FONT_BASE ||
            (size_t)glyph + 2u * 8u > rom_size)
            return 0u;
        /* A1 is set once per CHARACTER, at $2646/$264C, and only ever stepped
         * by `move.w (A1)+,D5` -- which is above the `dbra D1` at $2686.  One
         * glyph is therefore eight consecutive font words and A1 lands on the
         * next glyph's first word after the last row, not after eight nibbles. */
        word_address = (uint32_t)glyph;

        row_seed = 7u; /* $264E move.w #$7,D2 */
        do {       /* $2652 ... dbra D2,$2652 ($2690) */
            uint16_t bits;

            /* $2656 move.l D0,$c00002.l */
            poke_hstadr_hi(port, (uint16_t)(base >> 16));
            poke_hstadr_lo(port, (uint16_t)base);
            writes += 2u;

            /* $265C move.w (A1)+,D5 -- one font word per row. */
            bits = rom_word(rom, word_address);
            word_address += 2u;

            pixel_seed = 7u; /* $2652 move.w #$7,D1 */
            do {         /* $265E ... dbra D1,$265e ($2686) */
                /* $265E btst #$0,D5 / $2662 beq $266c / $2666 move.w D7,
                 * $c0000c.l -- a clean nibble sets the low colour plane and
                 * OVERWRITES the word. */
                if ((bits & 1u) != 0u) {
                    if (!poke_hstdat(port, low_colour))
                        return 0u;
                    writes++;
                }
                /* $266C btst #$2,D5 / $2670 beq $267a / $2674 or.w D6,
                 * $c0000c.l -- a nibble that also carries bit 2 adds the high
                 * colour plane to what was just written, because `or.w` is a
                 * read-modify-write of HSTDAT.  A nibble with bit 2 and not
                 * bit 0 (value 4) therefore ORs into whatever the background
                 * already left in VRAM, and a nibble of 0 reaches neither
                 * branch and emits no HSTDAT cycle at all. */
                if ((bits & 4u) != 0u) {
                    if (!poke_hstdat(port,
                                     (uint16_t)(peek_hstdat(port) | high_colour)))
                        return 0u;
                    writes++;
                }
                /* $267A addi.l #$10,$c00002.l -- a long read-modify-write of
                 * the DEVICE address.  D0 is untouched, so the next row's
                 * `move.l D0,$c00002.l` re-latches the row base and the eight
                 * increments of this row are discarded. */
                {
                    uint16_t high = peek_hstadr_hi(port);
                    uint16_t low = peek_hstadr_lo(port);
                    /* The cast matters: `high << 16` on a promoted int
                     * overflows for any address in $FFxxxxxx. */
                    uint32_t next =
                        (((uint32_t)high << 16) | (uint32_t)low) + 0x10u;
                    poke_hstadr_hi(port, (uint16_t)(next >> 16));
                    poke_hstadr_lo(port, (uint16_t)next);
                    writes += 2u;
                }
                /* $2684 lsr.w #4,D5 -- eight shifts over a 16-bit word, so
                 * nibbles five to seven are read as zero and the pixel is
                 * left alone.  The glyph is four pixels wide; the cell
                 * stride below is $40, which is four pixels. */
                bits = (uint16_t)(bits >> 4);
            } while (pixel_seed-- != 0u);

            /* $268A addi.l #$1000,D0 -- 256 GSP words per glyph row.  This is
             * the ROM's own D0, the same register the latch above read, so
             * the eight rows of a cell walk one contiguous $8000 block. */
            base += 0x1000u;
        } while (row_seed-- != 0u);

        /* $2694 subi.l #$7fc0,D0 -- back out the eight rows ($8000) and step
         * one four-pixel cell ($40) to the right: -($8000 - $40).  The eight
         * per-row $10 increments are already gone, because they moved the
         * device register, not D0. */
        base -= 0x7FC0u;
        count--; /* $269A subq.b #1,D3, $269C bne $2638 */
    }

    /* $269E jmp (A2) -- the caller's return address.  No bus effect. */
    return writes;
}

/* ── The earlier, narrower glyph-loop slice ───────────────────────────── */

/* Kept byte-for-byte as it was.  It models the NET value per pixel; the
 * routine it came from emits two HSTDAT writes for a both-bits nibble and
 * none at all for a zero nibble.  See mc68010_text_writer.h. */
size_t stunrun_mc68010_text_write(const uint8_t *descriptor,
                                  size_t descriptor_size,
                                  const uint16_t *font_words,
                                  size_t font_word_count,
                                  stunrun_text_write_fn write, void *ctx)
{
    size_t character;
    size_t emitted = 0u;
    uint32_t address;
    uint16_t low_colour;
    uint16_t high_colour;

    if (descriptor == NULL || descriptor_size < 4u || font_words == NULL ||
        write == NULL || (size_t)descriptor[3] > descriptor_size - 4u)
        return 0u;
    address = (((uint32_t)descriptor[1] * 0xA0u) << 8u) +
              ((uint32_t)descriptor[0] << 6u) - 0x800000u;
    low_colour = descriptor[2];
    high_colour = (uint16_t)(low_colour << 8u);
    for (character = 0u; character < descriptor[3]; character++) {
        size_t glyph = (size_t)((uint8_t)(descriptor[4u + character] - 0x2Au));
        size_t row;
        if (glyph > font_word_count / 8u || glyph * 8u + 7u >= font_word_count)
            return 0u;
        for (row = 0u; row < 8u; row++) {
            uint32_t row_address = address;
            uint16_t bits = font_words[glyph * 8u + row];
            unsigned column;
            for (column = 0u; column < 8u; column++) {
                uint16_t value = (bits & 1u) ? low_colour : 0u;
                if ((bits & 4u) != 0u)
                    value = (uint16_t)(value | high_colour);
                if (!write(ctx, row_address, value))
                    return 0u;
                emitted++;
                row_address += 0x10u;
                bits >>= 4u;
            }
            address += 0x1000u;
        }
        /* The ROM's `subi.l #$7fc0,D0` advances one glyph cell. */
        address -= 0x7FC0u;
    }
    return emitted;
}