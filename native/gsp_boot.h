/* Transcription of the STUN Runner 68010 GSP boot / fill / palette routines.
 *
 * Provenance: ROM byte addresses 0x00034A-0x0007D4 (the "GSP boot" code
 * reached from the autovector table at 0x000112 -> 0x3F8 and 0x00011E ->
 * 0x570, and called directly from 0x3F8).  Every routine below has a comment
 * giving its ROM address range.
 *
 * This is a decompilation, not an improvement: literals, loop seeds, register
 * shuffle order and write order are transcribed as the ROM performs them, and
 * oddities are annotated rather than corrected.
 *
 * ── Host port encoding ───────────────────────────────────────────────────
 * The 68010 cannot address GSP memory directly.  Everything below reaches the
 * TMS34010 through the 16KB window at 0xC00000-0xC03FFF, which MAME installs
 * as `hd68k_gsp_io_r/w` (atari/harddriv.cpp:602).  The driver computes
 *     offset = (offset / 2) ^ 1;
 * and hands the result to `tms340x0_device::host_w`
 * (devices/cpu/tms34010/tms34010.cpp:1644), where the TMS34010 host port
 * registers are ADDRESS_L = 0, ADDRESS_H = 1, DATA = 2, CONTROL = 3
 * (tms34010.h:1094-1097).
 *
 * MAME delivers a *word* index to the handler (emumem_hedp.cpp:99-106 shifts
 * the delegate offset right by Width+AddrShift, i.e. by one for the 68010's
 * 16-bit bus), so the ROM's two window addresses resolve as:
 *
 *     move.l #imm,$c00002.l  ->  word index 1 -> ADDRESS_H, then word index 2
 *                               -> ADDRESS_L.  The high half is delivered
 *                               first, which is why the ROM always latches
 *                               the high word at $C00002 and the low word at
 *                               $C00004.
 *     move.w #$1800,$c00008.l -> word index 4 -> HOST_CONTROL (ORI: set the
 *                               0x0800 post-increment-on-write and 0x1000
 *                               post-increment-on-read bits).
 *     move.w data,$c0000c.l  -> word index 6 -> HOST_DATA: writes the word at
 *                               (HSTADRH << 16 | HSTADRL) & 0xFFFFFFF0 and then,
 *                               if HSTCTL bit 0x0800 is set, adds 0x10 to the
 *                               address latches.
 *
 * Wiring note: native/musashi_cpu.c `gsp_host_reg()` currently decodes a *byte*
 * offset, which places HOST_DATA at 0xC00006 and drops every ROM write to
 * 0xC0000C on the floor.  Whoever owns the host-port transport should feed the
 * MAME convention (word index) or drop the /2 in that decoder; the constants
 * below state the window offsets this ROM actually uses.  Wiring is deliberately
 * not done here.
 *
 * ── GSP-side address map (atari/harddriv.cpp:619-629) ────────────────────
 *   0x02000000-0x020FFFFF  2bpp VRAM
 *   0xF4000000-0xF40000FF  control_lo (plain register array; only offset 0 is
 *                          decoded, offsets 0x10/0x20/0x30/0x70/0x90 are
 *                          shadow writes that log when the value changes)
 *   0xF4800000-0xF48000FF  control_hi.  Offsets 0x00/0x20/0x30/0x70/0x90/0xF0
 *                          all decode to `m_shiftreg_enable = (offset>>3)&1`
 *                          (harddriv_v.cpp:211-256), which is 0 for every
 *                          address this ROM writes.
 *   0xF5000000-0xF5000FFF  palette_lo: red = (word >> 8) & 0xff,
 *                          green = word & 0xff
 *   0xF5800000-0xF5800FFF  palette_hi: blue = word & 0xff
 *   0xFF800000-0xFFBFFFFF  main 16bpp VRAM (mirrored every 0x0400000)
 *   0x00000000-0x0000200F  noprw ("hit during self-test")
 *
 * The transport itself is NOT implemented here: the callbacks below are the
 * seam a host port implementation plugs into.  All callbacks must be non-NULL.
 */
#ifndef STUNRUN_GSP_BOOT_H
#define STUNRUN_GSP_BOOT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── 68010-side addresses the boot code writes or reads ───────────────── */

/* clr.w $60400c.l / clr.w $60401c.l in the boot routine.  Both land in
 * harddriv_m.cpp:400 `hd68k_nwr_w` with (offset & 7) == 4, i.e. latch ZP1, and
 * that handler derives its value from bit 3 of the address rather than from
 * the written data - so a clr.w here asserts ZP1 = 1. */
#define STUNRUN_MAIN_ZP_LATCH_A 0x0060400Cu
#define STUNRUN_MAIN_ZP_LATCH_B 0x0060401Cu

/* clr.w $608000.l - the watchdog reset window (harddriv.cpp:603), kicked once
 * per 256-word pass by the three nested fill loops. */
#define STUNRUN_MAIN_WATCHDOG 0x00608000u

/* move.w $8018.w / move.b $801b.w / move.w $801c.w in the RAM palette loader. */
#define STUNRUN_MAIN_PALETTE_WORD 0x008018u
#define STUNRUN_MAIN_PALETTE_BYTE 0x00801Bu
#define STUNRUN_MAIN_PALETTE_BLUE 0x00801Cu

/* ── GSP-side destination addresses latched by the boot code ───────────── */

#define STUNRUN_GSP_2BPP_VRAM_BASE 0x02000000u
#define STUNRUN_GSP_MAIN_VRAM_BASE 0xFF800000u
#define STUNRUN_GSP_CONTROL_LO 0xF4000000u
#define STUNRUN_GSP_CONTROL_HI 0xF4800000u
#define STUNRUN_GSP_CONTROL_HI_SHIFTEREG_20 0xF4800020u
#define STUNRUN_GSP_CONTROL_HI_SHIFTEREG_30 0xF4800030u
#define STUNRUN_GSP_CONTROL_HI_SHIFTEREG_10 0xF4800010u
#define STUNRUN_GSP_CONTROL_HI_SHIFTEREG_90 0xF4800090u
#define STUNRUN_GSP_CONTROL_HI_SHIFTEREG_70 0xF4800070u
#define STUNRUN_GSP_CONTROL_HI_SHIFTEREG_F0 0xF48000F0u
#define STUNRUN_GSP_PALETTE_LO 0xF5000000u
#define STUNRUN_GSP_PALETTE_HI 0xF5800000u

/* Two latched addresses that MAME's GSP map does not decode at all.  Their
 * meaning is UNKNOWN; they are only ever paired with a data word here. */
#define STUNRUN_GSP_UNMAPPED_WINDOW_FLAGS 0xC0000090u
#define STUNRUN_GSP_UNMAPPED_RESET_FLAG 0xC0000150u

/* Draw-parameter latches at 0x530: address-only, no FIFO word follows.  These
 * also fall outside every decoded region in the GSP map. */
#define STUNRUN_GSP_DRAW_PARAM_0 0x26088050u
#define STUNRUN_GSP_DRAW_PARAM_1 0x26088190u
#define STUNRUN_GSP_DRAW_PARAM_2 0x26088210u
#define STUNRUN_GSP_DRAW_PARAM_3 0x26088350u
#define STUNRUN_GSP_DRAW_PARAM_4 0x02000000u

/* ── Host port control masks used by the boot code ─────────────────────── */

/* ori.w #$1800,$c00008.l - 0x0800 post-increments after a HOST_DATA write,
 * 0x1000 post-increments after a HOST_DATA read. */
#define STUNRUN_GSP_HOSTCTL_AUTO_INCREMENT 0x1800u

/* move.w #$a000,$c00008.l - written (not OR-ed) once, by the boot routine.
 * 0xA000 = 0x8000 | 0x2000; neither bit has a decode in MAME's host_w, and in
 * particular 0x0800 is NOT set here, so auto-increment is still OFF when the
 * record table, the reset flag and the display window word are pushed.  The
 * first `ori.w #$1800` (inside 0x48E) turns it on; 0x67A ORs 0x1800 again, so
 * the register reaches 0xB800. */
#define STUNRUN_GSP_HOSTCTL_BOOT_INIT 0xA000u

/* ── Host port seam ────────────────────────────────────────────────────── */

typedef struct stunrun_gsp_host {
    void *ctx;

    /* HSTADRL (ROM `move.l #imm,$c00002.l`, high half first). */
    void (*set_host_address_lo)(void *ctx, uint16_t value);
    /* HSTADRH. */
    void (*set_host_address_hi)(void *ctx, uint16_t value);
    /* HSTCTL, plain write (ROM `move.w #$a000,$c00008.l`). */
    void (*set_host_control)(void *ctx, uint16_t value);
    /* HSTCTL |= mask (ROM `ori.w #$1800,$c00008.l`). */
    void (*or_host_control_mask)(void *ctx, uint16_t mask);
    /* HSTDAT: write `value` to the latched GSP address, honouring the 0x0800
     * post-increment.  ROM `move.w <data>,$c0000c.l`. */
    void (*write_fifo_word)(void *ctx, uint16_t value);

    /* Plain 68010 memory, needed by the routines that read RAM (the packed
     * bitfield source array, the RAM palette loader). */
    uint16_t (*peek_word)(void *ctx, uint32_t address);
    uint8_t (*peek_byte)(void *ctx, uint32_t address);
    /* 68010 store, needed for the ZP latch clears and the watchdog kicks. */
    void (*poke_word)(void *ctx, uint32_t address, uint16_t value);
} stunrun_gsp_host_t;

/* ── Shared idioms ─────────────────────────────────────────────────────── */

/* ROM `move.l #imm,$c00002.l`: deliver the high half of the GSP address before
 * the low half, because $C00002 is HOST_ADDRESS_H and $C00004 is
 * HOST_ADDRESS_L. */
void stunrun_gsp_latch_address(stunrun_gsp_host_t *io, uint32_t address);

/* The one shape shared by 0x636, 0x67A, 0x6AC, 0x6EE, 0x756 and 0x7B0:
 * latch an address once, then push a constant word through the FIFO.
 *
 * Loop counts are the ROM's `dbra` seeds, so the body runs seed + 1 times;
 * pass a seed of 0 for a single pass.  `kick_watchdog` selects whether the
 * ROM emits `clr.w $608000.l` after each inner pass (0x636 / 0x67A / 0x6AC do,
 * 0x6EE does not). */
void stunrun_gsp_push_word_block(stunrun_gsp_host_t *io, uint32_t address,
                                 uint16_t word, uint16_t outer_seed,
                                 uint16_t inner_seed, int kick_watchdog);

/* ── Individual routines ───────────────────────────────────────────────── */

/* 0x00034A-0x00036E: walk the ROM record table and push each (address, word)
 * pair.  Seeded by the word at ROM 0x2E8, so it runs seed + 1 records of 6
 * bytes (4-byte address, 2-byte word). */
void stunrun_gsp_boot_upload_record_table(stunrun_gsp_host_t *io,
                                         const uint8_t *rom);

/* 0x000370-0x00037A: pure cycle burn, no observable effect. */
void stunrun_gsp_boot_bus_delay(void);

/* 0x00037C-0x0003BC: build one palette word out of a byte triple table and
 * push it.  `index` selects a 10-byte entry at ROM 0x32C + index * 10. */
void stunrun_gsp_boot_write_packed_bitfield(stunrun_gsp_host_t *io,
                                            const uint8_t *rom,
                                            uint16_t index);

/* 0x0003BE-0x0003F6: push a flag word (0xFFFF or 0x0003, chosen by ROM byte
 * 0x2F9) to 0xC0000090, then clear 0xF4800000.  Target semantics UNKNOWN. */
void stunrun_gsp_boot_program_display_window(stunrun_gsp_host_t *io,
                                             const uint8_t *rom);

/* 0x0003F8-0x000466: the boot entry point (autovector 0x112). */
void stunrun_gsp_boot_init(stunrun_gsp_host_t *io, const uint8_t *rom);

/* 0x000468-0x00048C: write 0 then 0xFFFF to two control_hi shiftreg offsets. */
void stunrun_gsp_boot_clear_display_control(stunrun_gsp_host_t *io);

/* 0x00048E-0x0004F4: enable auto-increment, clear both palette banks (a grey
 * ramp into palette_lo, a blue ramp into palette_hi), then clear the display
 * control words. */
void stunrun_gsp_boot_configure_display_and_clear_palette(
    stunrun_gsp_host_t *io);

/* 0x0004F6-0x000516: 32-word ramp whose packed word is (high << 8) | low,
 * where `high` steps by `high_step` and `low` by `low_step` - used for
 * palette_lo, whose high byte is red and low byte is green. */
void stunrun_gsp_boot_write_ramp_pair(stunrun_gsp_host_t *io,
                                      uint8_t high_step, uint8_t low_step);

/* 0x000518-0x00052E: 32-word ramp with a single step - used for palette_hi,
 * whose low byte is blue. */
void stunrun_gsp_boot_write_ramp_single(stunrun_gsp_host_t *io,
                                        uint8_t step);

/* 0x000530-0x00056E: two address+word pairs, then five address latches that
 * are never followed by a FIFO word. */
void stunrun_gsp_boot_latch_draw_parameters(stunrun_gsp_host_t *io);

/* 0x000570-0x000634: the palette walk (autovector 0x11E).  Writes 256
 * palette_lo words as eight 32-word ramps, 256 palette_hi words as eight
 * 32-word ramps, then the draw parameters and the display control clears. */
void stunrun_gsp_boot_walk_palette(stunrun_gsp_host_t *io);

/* 0x000636-0x000678: one word to control_lo, then 96 x 256 words of 0xFFFF
 * into the 2bpp VRAM, watchdog kicked once per 256-word pass. */
void stunrun_gsp_boot_fill_2bpp_vram_ffff(stunrun_gsp_host_t *io,
                                          uint16_t control_word);

/* 0x00067A-0x0006AA: 900 x 256 words of `word` into the main VRAM, watchdog
 * kicked once per 256-word pass.  No control_lo seed write, but it DOES OR
 * 0x1800 into the host control register itself. */
void stunrun_gsp_boot_fill_main_vram(stunrun_gsp_host_t *io, uint16_t word);

/* 0x0006AC-0x0006EC: one word to control_lo, then 96 x 256 words of
 * `fill_word` into the 2bpp VRAM. */
void stunrun_gsp_boot_fill_2bpp_vram_from_d1(stunrun_gsp_host_t *io,
                                              uint16_t control_word,
                                              uint16_t fill_word);

/* 0x0006EE-0x000754: fill all 256 palette_hi entries with the word at 0x801C,
 * then all 256 palette_lo entries with (word at 0x8018 << 8) | byte at
 * 0x801B.  No watchdog kicks. */
void stunrun_gsp_boot_load_palette_from_ram(stunrun_gsp_host_t *io);

/* 0x000756-0x0007A4: two 0xFFFF words to control_lo, then 384 passes of
 * (latch a running address, watchdog, delay, two 0xFFFF words), striding
 * 0x400 bytes per pass. */
void stunrun_gsp_boot_stride_fill_2bpp_vram(stunrun_gsp_host_t *io);

/* 0x0007A6-0x0007AE: pure cycle burn, no observable effect. */
void stunrun_gsp_boot_short_delay(void);

/* 0x0007B0-0x0007C0: one word to control_lo.  Does NOT touch the host control
 * register, so this relies on auto-increment already being enabled. */
void stunrun_gsp_boot_write_control_lo_word(stunrun_gsp_host_t *io,
                                            uint16_t word);

/* 0x0007C2-0x0007D4: push the word 0x0001 to the undecoded address 0xC0000150.
 * Target semantics UNKNOWN. */
void stunrun_gsp_boot_program_reset_flag(stunrun_gsp_host_t *io);

#ifdef __cplusplus
}
#endif

#endif /* STUNRUN_GSP_BOOT_H */