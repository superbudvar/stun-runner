/* GSP boot / fill / palette routines, transcribed from the ROM.
 * See gsp_boot.h for provenance, the host-window encoding and the address map.
 *
 * Transcribed from /tmp/sr_boot.s (ROM byte addresses 0x00034A-0x0007D4).
 * Register names in the comments (D0-D7, A0-A2) are the ROM's, not the C
 * function's; they are kept because several oddities (which register is
 * overwritten, which byte is stepped, which register latches the address) only
 * make sense next to the original instruction.
 *
 * Nothing here implements the transport: every effect on hardware goes through
 * the callbacks in stunrun_gsp_host_t.
 */
#include "gsp_boot.h"

/* ── ROM addresses referenced by name ─────────────────────────────────── */

#define ROM_RECORD_TABLE 0x02B8u      /* lea $2b8.l,A0 (0x34A) */
#define ROM_RECORD_COUNT 0x02E8u      /* move.w $2e8.l,D0 (0x34A) */
#define ROM_RECORD_STRIDE 6u          /* adda.l #$6,A0 (0x34A) */
#define ROM_PACKED_BITFIELD_TABLE 0x032Cu /* lea $32c.l,A0 (0x37C) */
#define ROM_PACKED_BITFIELD_STRIDE 10u    /* mulu.w #$a,D0 (0x37C) */
#define ROM_DISPLAY_FLAG 0x02F9u      /* tst.b $2f9.l (0x3BE) */

/* ── Small ROM helpers ────────────────────────────────────────────────── */

static uint16_t rom_word(const uint8_t *rom, uint32_t address)
{
    return (uint16_t)(((uint16_t)rom[address] << 8) | (uint16_t)rom[address + 1]);
}

static uint32_t rom_long(const uint8_t *rom, uint32_t address)
{
    return ((uint32_t)rom[address] << 24) | ((uint32_t)rom[address + 1] << 16) |
           ((uint32_t)rom[address + 2] << 8) | (uint32_t)rom[address + 3];
}

/* ── Shared idioms ────────────────────────────────────────────────────── */

/* ROM `move.l #imm,$c00002.l`: $C00002 is HOST_ADDRESS_H and $C00004 is
 * HOST_ADDRESS_L, so the long write delivers the high half first. */
void stunrun_gsp_latch_address(stunrun_gsp_host_t *io, uint32_t address)
{
    if (io == NULL) {
        return;
    }
    io->set_host_address_hi(io->ctx, (uint16_t)(address >> 16));
    io->set_host_address_lo(io->ctx, (uint16_t)(address & 0xFFFFu));
}

/* One helper for the six sibling routines at 0x636, 0x67A, 0x6AC, 0x6EE, 0x756
 * and 0x7B0: latch an address once, then push a constant word through the
 * FIFO.
 *
 * Loop counts are the ROM's `dbra` seeds, so each loop body runs seed + 1
 * times.  Where the ROM reloads the inner seed at the top of the outer loop
 * (0x660, 0x696, 0x6D4) the C does the same, because the inner seed is a
 * per-pass local.
 *
 * Three of the six sites use the nested form with a watchdog kick after each
 * inner pass (0x636, 0x67A, 0x6AC: `clr.w $608000.l`).  0x6EE runs two FLAT
 * 256-word loops with no kick, which this helper expresses as
 * outer_seed = inner_seed = 0xFF, inner_seed = 0 and kick_watchdog = 0.
 * 0x756 is the one sibling this helper cannot express at all - see
 * stunrun_gsp_boot_stride_fill_2bpp_vram. */
void stunrun_gsp_push_word_block(stunrun_gsp_host_t *io, uint32_t address,
                                 uint16_t word, uint16_t outer_seed,
                                 uint16_t inner_seed, int kick_watchdog)
{
    unsigned outer;
    unsigned inner_count;

    if (io == NULL) {
        return;
    }

    /* `clr.w $608000.l` is the watchdog reset window, harddriv.cpp:603. */
    stunrun_gsp_latch_address(io, address);

    inner_count = (unsigned)inner_seed + 1u; /* dbra counts seed + 1 passes */

    for (outer = 0u; outer <= (unsigned)outer_seed; outer++) {
        unsigned i;
        for (i = 0u; i < inner_count; i++) {
            io->write_fifo_word(io->ctx, word);
        }
        if (kick_watchdog) {
            io->poke_word(io->ctx, STUNRUN_MAIN_WATCHDOG, 0u);
        }
    }
}

/* Shared prologue of 0x48E, 0x570 and 0x6EE: enable FIFO auto-increment, then
 * write 0 to control_hi offsets 0x20 and 0x30.  In MAME both offsets decode to
 * `m_shiftreg_enable = (offset >> 3) & 1`, which is 0 here, so these are the
 * shiftreg-disable writes. */
static void stunrun_gsp_prepare_shiftregs(stunrun_gsp_host_t *io)
{
    io->or_host_control_mask(io->ctx, STUNRUN_GSP_HOSTCTL_AUTO_INCREMENT);
    stunrun_gsp_latch_address(io, STUNRUN_GSP_CONTROL_HI_SHIFTEREG_20);
    io->write_fifo_word(io->ctx, 0x0000);
    stunrun_gsp_latch_address(io, STUNRUN_GSP_CONTROL_HI_SHIFTEREG_30);
    io->write_fifo_word(io->ctx, 0x0000);
}

/* ── 0x34A: record table upload ───────────────────────────────────────── */

void stunrun_gsp_boot_upload_record_table(stunrun_gsp_host_t *io,
                                         const uint8_t *rom)
{
    uint32_t entry;
    unsigned entries;
    unsigned i;

    if (io == NULL || rom == NULL) {
        return;
    }

    entry = ROM_RECORD_TABLE;
    /* `move.w $2e8.l,D0` then `dbra D0,$356`: the seed counts down from its
     * own value, so the body runs seed + 1 times. */
    entries = (unsigned)rom_word(rom, ROM_RECORD_COUNT) + 1u;

    for (i = 0u; i < entries; i++) {
        /* move.l (A0),$c00002.l - a 4-byte address per record. */
        stunrun_gsp_latch_address(io, rom_long(rom, entry));
        /* move.w (4,A0),$c0000c.l - a 2-byte word per record. */
        io->write_fifo_word(io->ctx, rom_word(rom, entry + 4));
        /* adda.l #$6,A0 - 4-byte address + 2-byte word. */
        entry += ROM_RECORD_STRIDE;
    }
}

/* ── 0x370: cycle burn ────────────────────────────────────────────────── */

void stunrun_gsp_boot_bus_delay(void)
{
    uint16_t counter; /* D0.w - the move.l #$190 only clears the upper word */

    /* move.l #$190,D0 / subq.w #1,D0 / bne - counts the LOW WORD only, so the
     * branch runs 0x190 (400) times.  No observable effect. */
    counter = 0x0190u;
    while (counter != 0u) {
        counter = (uint16_t)(counter - 1u);
    }
}

/* ── 0x37C: packed bitfield ───────────────────────────────────────────── */

void stunrun_gsp_boot_write_packed_bitfield(stunrun_gsp_host_t *io,
                                            const uint8_t *rom, uint16_t index)
{
    uint32_t entry;
    uint32_t source;
    uint16_t terms;
    uint16_t packed;
    unsigned i;

    if (io == NULL || rom == NULL) {
        return;
    }

    /* lea $32c.l,A0 / mulu.w #$a,D0 / adda.l D0,A0 - 10-byte entries. */
    entry = ROM_PACKED_BITFIELD_TABLE +
            (uint32_t)index * ROM_PACKED_BITFIELD_STRIDE;

    /* clr.w D3 - the accumulator starts empty. */
    packed = 0u;
    /* movea.l (A0),A1 - entry+0 is a 68010 address holding the source array. */
    source = rom_long(rom, entry);
    /* move.w (4,A0),D5 - entry+4 is the term count seed. */
    terms = rom_word(rom, entry + 4);

    /* dbra D5,$392: seed + 1 terms, each a 3-byte (value, mask, shift) triple. */
    for (i = 0u; i <= (unsigned)terms; i++) {
        uint16_t value;  /* D0.w */
        uint16_t mask;   /* D1.w */
        uint16_t shift;  /* D2.w, masked to 4 bits by a 68010 word shift */

        value = io->peek_byte(io->ctx, source);           /* move.b (A1),D0 */
        mask = io->peek_byte(io->ctx, source + 1);        /* clr.w D1 + move.b */
        shift = io->peek_byte(io->ctx, source + 2);       /* move.b (2,A1),D2 */
        value = (uint16_t)(value & mask);                  /* and.w D1,D0 */
        value = (uint16_t)(value << (shift & 15u));       /* lsl.w D2,D0 */
        packed = (uint16_t)(packed | value);              /* or.w D0,D3 */

        /* adda.w #$3,A1 */
        source += 3u;
    }

    /* move.l (6,A0),$c00002.l - entry+6 is the destination address. */
    stunrun_gsp_latch_address(io, rom_long(rom, entry + 6));
    io->write_fifo_word(io->ctx, packed);
}

/* ── 0x3BE: display window word ───────────────────────────────────────── */

void stunrun_gsp_boot_program_display_window(stunrun_gsp_host_t *io,
                                             const uint8_t *rom)
{
    uint16_t value;

    if (io == NULL || rom == NULL) {
        return;
    }

    stunrun_gsp_latch_address(io, STUNRUN_GSP_UNMAPPED_WINDOW_FLAGS);

    /* moveq #0,D0 / lsr.l #8,D0 - a LONG shift of zero, still zero. */
    value = 0x0000u;
    /* tst.b $2f9.l / bne - the flag byte being non-zero skips the NOT. */
    if (rom[ROM_DISPLAY_FLAG] == 0u) {
        value = (uint16_t)~value; /* not.w D0 - zero becomes 0xFFFF */
    }
    value = (uint16_t)(value & 0xFFFCu); /* andi.w #$fffc */
    value = (uint16_t)(value | 0x0003u); /* ori.w #$3 */
    io->write_fifo_word(io->ctx, value);

    stunrun_gsp_latch_address(io, STUNRUN_GSP_CONTROL_HI);
    io->write_fifo_word(io->ctx, 0x0000); /* clr.w $c0000c.l */
}

/* ── 0x3F8: boot entry point ──────────────────────────────────────────── */

void stunrun_gsp_boot_init(stunrun_gsp_host_t *io, const uint8_t *rom)
{
    if (io == NULL || rom == NULL) {
        return;
    }

    /* clr.w $60400c.l - ZP1 latch (data is ignored by hd68k_nwr_w; the address
     * is what matters). */
    io->poke_word(io->ctx, STUNRUN_MAIN_ZP_LATCH_A, 0x0000u);
    stunrun_gsp_boot_bus_delay();
    /* clr.w $6041c.l - likewise ZP1. */
    io->poke_word(io->ctx, STUNRUN_MAIN_ZP_LATCH_B, 0x0000u);

    /* move.w #$a000,$c00008.l - a plain HSTCTL write, NOT the usual OR.  0x0800
     * is the write post-increment, 0x2000 has no decode in MAME. */
    io->set_host_control(io->ctx, STUNRUN_GSP_HOSTCTL_BOOT_INIT);

    /* move.w #$2,D0 / bsr $37c */
    stunrun_gsp_boot_write_packed_bitfield(io, rom, 0x0002u);
    /* bsr $34a */
    stunrun_gsp_boot_upload_record_table(io, rom);
    /* bsr $7c2 */
    stunrun_gsp_boot_program_reset_flag(io);
    /* bsr $3be */
    stunrun_gsp_boot_program_display_window(io, rom);
    /* move.w #$1,D0 / bsr $37c */
    stunrun_gsp_boot_write_packed_bitfield(io, rom, 0x0001u);
    /* move.w #$0,D0 / bsr $37c */
    stunrun_gsp_boot_write_packed_bitfield(io, rom, 0x0000u);
    /* bsr $48e */
    stunrun_gsp_boot_configure_display_and_clear_palette(io);

    /* A1/A2 are lea'd over $C00002/$C0000C and the next six instructions inline
     * routine 0x468 rather than calling it; the write order is identical. */
    stunrun_gsp_boot_clear_display_control(io);

    /* move.w #$0,D0 / bsr $67a - D0 is the fill word, zero.  0x67A opens with
     * `ori.w #$1800`, so the 0xA000 written above is OR-ed to 0xB800 here and
     * auto-increment only becomes active from this point on. */
    stunrun_gsp_boot_fill_main_vram(io, 0x0000u);
}

/* ── 0x468: clear display control ─────────────────────────────────────── */

void stunrun_gsp_boot_clear_display_control(stunrun_gsp_host_t *io)
{
    if (io == NULL) {
        return;
    }
    stunrun_gsp_latch_address(io, STUNRUN_GSP_CONTROL_HI_SHIFTEREG_10);
    io->write_fifo_word(io->ctx, 0x0000u);
    stunrun_gsp_latch_address(io, STUNRUN_GSP_CONTROL_HI_SHIFTEREG_90);
    io->write_fifo_word(io->ctx, 0xFFFFu);
}

/* ── 0x48E: configure display, clear palette ──────────────────────────── */

void stunrun_gsp_boot_configure_display_and_clear_palette(
    stunrun_gsp_host_t *io)
{
    uint16_t value; /* D0.w */
    unsigned i;

    if (io == NULL) {
        return;
    }

    stunrun_gsp_prepare_shiftregs(io);

    /* palette_lo: a grey ramp, both bytes stepped together. */
    stunrun_gsp_latch_address(io, STUNRUN_GSP_PALETTE_LO);
    value = 0x0000u; /* clr.w D0 */
    for (i = 0u; i <= 0xFFu; i++) {
        io->write_fifo_word(io->ctx, value);
        value = (uint16_t)(value + 0x0101u); /* addi.w #$101,D0 */
    }

    /* palette_hi: the blue channel alone, 0x0000..0x00FF. */
    value = 0x0000u;
    stunrun_gsp_latch_address(io, STUNRUN_GSP_PALETTE_HI);
    for (i = 0u; i <= 0xFFu; i++) {
        io->write_fifo_word(io->ctx, value);
        value = (uint16_t)(value + 1u); /* addq.w #1,D0 */
    }

    stunrun_gsp_boot_clear_display_control(io);
}

/* ── 0x4F6: 32-word two-channel ramp (palette_lo) ─────────────────────── */

void stunrun_gsp_boot_write_ramp_pair(stunrun_gsp_host_t *io,
                                      uint8_t high_step, uint8_t low_step)
{
    uint8_t high; /* D3.b - palette_lo high byte is RED */
    uint8_t low;  /* D4.b - palette_lo low byte is GREEN */
    unsigned i;

    if (io == NULL) {
        return;
    }

    high = 0u; /* clr.w D3 */
    low = 0u;  /* clr.w D4 */

    /* move.w #$1f,D2 ... dbra D2,$502 - 32 words. */
    for (i = 0u; i <= 0x1Fu; i++) {
        uint16_t word;
        word = (uint16_t)(((uint16_t)high << 8) & 0xFF00u); /* lsl.w #8,D5 */
        word = (uint16_t)(word | (uint16_t)low);          /* move.b D4,D5 */
        io->write_fifo_word(io->ctx, word);
        high = (uint8_t)(high + high_step); /* add.b D0,D3 */
        low = (uint8_t)(low + low_step);    /* add.b D1,D4 */
    }
}

/* ── 0x518: 32-word single-channel ramp (palette_hi) ───────────────────── */

void stunrun_gsp_boot_write_ramp_single(stunrun_gsp_host_t *io, uint8_t step)
{
    uint8_t value; /* D3.b - palette_hi's low byte is BLUE */
    unsigned i;

    if (io == NULL) {
        return;
    }

    value = 0u; /* clr.w D3 */

    for (i = 0u; i <= 0x1Fu; i++) {
        io->write_fifo_word(io->ctx, (uint16_t)value);
        value = (uint8_t)(value + step); /* add.b D0,D3 */
    }
}

/* ── 0x530: draw parameter latches ────────────────────────────────────── */

void stunrun_gsp_boot_latch_draw_parameters(stunrun_gsp_host_t *io)
{
    if (io == NULL) {
        return;
    }

    /* Two address+word pairs through A1/A2. */
    stunrun_gsp_latch_address(io, STUNRUN_GSP_CONTROL_HI_SHIFTEREG_70);
    io->write_fifo_word(io->ctx, 0x0000u);
    stunrun_gsp_latch_address(io, STUNRUN_GSP_CONTROL_HI_SHIFTEREG_F0);
    io->write_fifo_word(io->ctx, 0x0000u);

    /* Five address latches with NO data word.  These are the ROM's final
     * latched address when the routine returns, so anything that writes to the
     * FIFO afterwards without latching first lands on $02000000. */
    stunrun_gsp_latch_address(io, STUNRUN_GSP_DRAW_PARAM_0);
    stunrun_gsp_latch_address(io, STUNRUN_GSP_DRAW_PARAM_1);
    stunrun_gsp_latch_address(io, STUNRUN_GSP_DRAW_PARAM_2);
    stunrun_gsp_latch_address(io, STUNRUN_GSP_DRAW_PARAM_3);
    stunrun_gsp_latch_address(io, STUNRUN_GSP_DRAW_PARAM_4);
}

/* ── 0x570: palette walk ──────────────────────────────────────────────── */

void stunrun_gsp_boot_walk_palette(stunrun_gsp_host_t *io)
{
    if (io == NULL) {
        return;
    }

    stunrun_gsp_prepare_shiftregs(io);

    /* 8 ramps of 32 words = 256 palette_lo entries (bank 0).  The step pairs
     * are in the ROM's call order, so the 8 x 8 x 8 cube is walked in an
     * unusual order: red steps 0,0,0,0,8,8,8,8 and green 0,0,8,8,0,0,8,8. */
    stunrun_gsp_latch_address(io, STUNRUN_GSP_PALETTE_LO);
    stunrun_gsp_boot_write_ramp_pair(io, 0, 0);
    stunrun_gsp_boot_write_ramp_pair(io, 0, 0);
    stunrun_gsp_boot_write_ramp_pair(io, 0, 8);
    stunrun_gsp_boot_write_ramp_pair(io, 0, 8);
    stunrun_gsp_boot_write_ramp_pair(io, 8, 0);
    stunrun_gsp_boot_write_ramp_pair(io, 8, 0);
    stunrun_gsp_boot_write_ramp_pair(io, 8, 8);
    stunrun_gsp_boot_write_ramp_pair(io, 8, 8);

    /* 8 ramps of 32 words = 256 palette_hi entries (blue).  The blue step
     * alternates 0,8,0,8,... which does NOT line up with the red/green blocks
     * above; the resulting cube is skewed, not an aligned RGB cube. */
    stunrun_gsp_latch_address(io, STUNRUN_GSP_PALETTE_HI);
    stunrun_gsp_boot_write_ramp_single(io, 0);
    stunrun_gsp_boot_write_ramp_single(io, 8);
    stunrun_gsp_boot_write_ramp_single(io, 0);
    stunrun_gsp_boot_write_ramp_single(io, 8);
    stunrun_gsp_boot_write_ramp_single(io, 0);
    stunrun_gsp_boot_write_ramp_single(io, 8);
    stunrun_gsp_boot_write_ramp_single(io, 0);
    stunrun_gsp_boot_write_ramp_single(io, 8);

    /* bsr $530 */
    stunrun_gsp_boot_latch_draw_parameters(io);
    /* bsr $468 */
    stunrun_gsp_boot_clear_display_control(io);
}

/* ── 0x636: 2bpp VRAM fill with $ffff ─────────────────────────────────── */

void stunrun_gsp_boot_fill_2bpp_vram_ffff(stunrun_gsp_host_t *io,
                                          uint16_t control_word)
{
    if (io == NULL) {
        return;
    }

    io->or_host_control_mask(io->ctx, STUNRUN_GSP_HOSTCTL_AUTO_INCREMENT);
    stunrun_gsp_push_word_block(io, STUNRUN_GSP_CONTROL_LO, control_word, 0u,
                                0u, 0);
    /* 0x5f + 1 = 96 outer passes of 0xff + 1 = 256 words, one watchdog kick
     * per pass: 24576 words = 0xC000 bytes of 2bpp VRAM, 0x02000000 up to
     * 0x0200BFFF.  This is NOT the whole 1MB 2bpp region.  The inner seed is
     * reloaded at the top of every outer pass (0x660), so it is a per-pass
     * local - stunrun_gsp_push_word_block hoists it, which is equivalent. */
    stunrun_gsp_push_word_block(io, STUNRUN_GSP_2BPP_VRAM_BASE, 0xFFFFu, 0x005Fu,
                                0x00FFu, 1);
}

/* ── 0x67A: main VRAM fill ────────────────────────────────────────────── */

void stunrun_gsp_boot_fill_main_vram(stunrun_gsp_host_t *io, uint16_t word)
{
    if (io == NULL) {
        return;
    }

    /* ori.w #$1800,$c00008.l - like 0x636/0x6AC, this routine enables FIFO
     * auto-increment itself rather than trusting the caller. */
    io->or_host_control_mask(io->ctx, STUNRUN_GSP_HOSTCTL_AUTO_INCREMENT);

    /* lea $c0000c.l,A0 then `move.w D0,(A0)` in the inner loop: the fill word
     * is pushed straight from D0 rather than being re-latched, so the write
     * ORDER is address-once-then-words, identical to the helper's. */
    /* 0x383 + 1 = 900 outer passes of 0xff + 1 = 256 words = 230400 words =
     * 0x384000 bytes, filling 0xFF800000 up to 0xFFB83FFF.  The address latch
     * itself ends at 0xFFB84000, so the top 0x7C000 bytes of the 0x400000-byte
     * main VRAM window are NOT filled here. */
    stunrun_gsp_push_word_block(io, STUNRUN_GSP_MAIN_VRAM_BASE, word, 0x0383u,
                                0x00FFu, 1);
}

/* ── 0x6AC: 2bpp VRAM fill with a caller-supplied word ────────────────── */

void stunrun_gsp_boot_fill_2bpp_vram_from_d1(stunrun_gsp_host_t *io,
                                              uint16_t control_word,
                                              uint16_t fill_word)
{
    if (io == NULL) {
        return;
    }

    io->or_host_control_mask(io->ctx, STUNRUN_GSP_HOSTCTL_AUTO_INCREMENT);
    stunrun_gsp_push_word_block(io, STUNRUN_GSP_CONTROL_LO, control_word, 0u,
                                0u, 0);

    /* The ROM does `move.w D1,D0` here, so the value the caller passed in D1 is
     * consumed as the fill word and D1 is then reused as the inner seed.  The
     * C captures it in `fill_word` before the seeds are set up, which is the
     * same data flow without the clobber. */
    stunrun_gsp_push_word_block(io, STUNRUN_GSP_2BPP_VRAM_BASE, fill_word,
                                0x005Fu, 0x00FFu, 1);
}

/* ── 0x6EE: palette upload from 68010 RAM ──────────────────────────────── */

void stunrun_gsp_boot_load_palette_from_ram(stunrun_gsp_host_t *io)
{
    uint16_t blue_word;
    uint16_t red_green_word;

    if (io == NULL) {
        return;
    }

    stunrun_gsp_prepare_shiftregs(io);

    /* move.w $801c.w,D1 - the 256-entry blue bank, one word. */
    blue_word = io->peek_word(io->ctx, STUNRUN_MAIN_PALETTE_BLUE);

    /* move.w $8018.w,D1 / lsl.w #8,D1 - red goes in the high byte ... */
    red_green_word = io->peek_word(io->ctx, STUNRUN_MAIN_PALETTE_WORD);
    red_green_word = (uint16_t)(red_green_word << 8);
    /* ... and `move.b $801b.w,D1` puts green in the low byte.  That instruction
     * fetches a WORD at $801B and keeps only its low byte, so it reads the low
     * byte of the blue word at $801C, NOT a standalone byte at $801B.  Kept
     * verbatim; a naive transcription would use peek_byte($801B) here. */
    red_green_word =
        (uint16_t)(red_green_word |
                   (uint16_t)(io->peek_word(io->ctx, STUNRUN_MAIN_PALETTE_BYTE) &
                              0x00FFu));

    /* Two flat 256-word loops with no watchdog kicks, expressed as a single
     * outer loop of 256 with no inner loop. */
    stunrun_gsp_push_word_block(io, STUNRUN_GSP_PALETTE_HI, blue_word, 0x00FFu,
                                0x0000u, 0);
    stunrun_gsp_push_word_block(io, STUNRUN_GSP_PALETTE_LO, red_green_word,
                                0x00FFu, 0x0000u, 0);
}

/* ── 0x756: strided 2bpp VRAM fill ────────────────────────────────────── */

void stunrun_gsp_boot_stride_fill_2bpp_vram(stunrun_gsp_host_t *io)
{
    uint32_t address; /* D0.l - a RUNNING address, not a constant */
    uint16_t word;    /* D3.w */
    unsigned i;

    if (io == NULL) {
        return;
    }

    /* move.l #$2000200,D0 - starts 0x200 bytes into the 2bpp VRAM. */
    address = 0x02000200u;

    stunrun_gsp_latch_address(io, STUNRUN_GSP_CONTROL_LO);
    io->write_fifo_word(io->ctx, 0xFFFFu);
    io->write_fifo_word(io->ctx, 0xFFFFu);

    word = 0xFFFFu; /* move.w #$ffff,D3 */
    for (i = 0u; i <= 0x017Fu; i++) { /* dbra D5,$77e - 384 passes */
        stunrun_gsp_latch_address(io, address);
        io->poke_word(io->ctx, STUNRUN_MAIN_WATCHDOG, 0x0000u);
        stunrun_gsp_boot_short_delay();
        io->write_fifo_word(io->ctx, word);
        io->write_fifo_word(io->ctx, word);
        /* addi.l #$400,D0 - 0x400 bytes per pass, 2 of every 4 words. */
        address += 0x400u;
    }
}

/* ── 0x7A6: short cycle burn ──────────────────────────────────────────── */

void stunrun_gsp_boot_short_delay(void)
{
    uint16_t counter; /* D1.w */

    /* move.w #$fa0,D1 / subq.w #1,D1 / bne - 0xFA0 (4000) iterations.
     * No observable effect. */
    counter = 0x0FA0u;
    while (counter != 0u) {
        counter = (uint16_t)(counter - 1u);
    }
}

/* ── 0x7B0: single control_lo word ────────────────────────────────────── */

void stunrun_gsp_boot_write_control_lo_word(stunrun_gsp_host_t *io,
                                            uint16_t word)
{
    if (io == NULL) {
        return;
    }
    /* No HSTCTL write in this routine, so it relies on auto-increment already
     * being enabled by its caller. */
    stunrun_gsp_push_word_block(io, STUNRUN_GSP_CONTROL_LO, word, 0u, 0u, 0);
}

/* ── 0x7C2: reset flag ────────────────────────────────────────────────── */

void stunrun_gsp_boot_program_reset_flag(stunrun_gsp_host_t *io)
{
    if (io == NULL) {
        return;
    }
    /* $C0000150 is not decoded by any region of MAME's GSP map; the ROM pushes
     * $0001 there and the meaning is UNKNOWN. */
    stunrun_gsp_latch_address(io, STUNRUN_GSP_UNMAPPED_RESET_FLAG);
    io->write_fifo_word(io->ctx, 0x0001u);
}