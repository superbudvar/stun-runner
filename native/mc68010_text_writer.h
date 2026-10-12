/* MC68010 text-draw routine, ROM $25E8 - $269F (maincpu program ROM).
 *
 * Source chip: the STUN Runner maincpu program ROM, assembled flat by
 * `tools/build-maincpu-image` from the six 136070-210x/300x/5xxx/6xxx pairs.
 * Image used for transcription and fixtures:
 *   roms/stunrun/stunrun_maincpu.bin
 *   sha256 29c911a21d1abe780419cd3a924080756490f2d008776995dd48c7b2bedc5b6c
 * The routine is entered by 30 `jmp $25E8.l` sites, each preceded by
 * `move.w #$SEL,D0` / `lea (disp,PC),A2`; A2 is only read, by `jmp (A2)` at
 * $269E, so it does not appear in this slice's interface.
 *
 * It is the MC68010-side text path: it resolves a selector through a ROM
 * table, decodes a four-byte descriptor, and pushes 4x8-cell glyphs at the
 * TMS34010 (GSP) host port.  It is NOT the GSP-side text path
 * (`reproduction/gsp/text_cursor.c`), which renders descriptors uploaded into
 * VRAM by GSP code.
 *
 * `$25E8` is one of two entry points into this loop body.  The other, `$2580`
 * (reached from the dispatch table at `$00196`), skips the prologue, the
 * selector resolution and the descriptor decode, takes `D0`/`D3`/`D7`/`D6` from
 * its caller, and returns with `rts` at `$25E6` rather than `jmp (A2)`.  Only
 * `$25E8` is ported here; see findings section 22.10.
 *
 * ── Host window encoding ────────────────────────────────────────────────
 * The 68010 reaches the GSP through the 16 KB window at $C00000-$C03FFF,
 * installed as `hd68k_gsp_io_r/w`.  The byte offsets this ROM uses are fixed
 * by decode, not by the mnemonics:
 *
 *     $C00000-$C00003  HSTADRH      $C00008-$C0000B  HSTCTLH:HSTCTLL
 *     $C00004-$C00007  HSTADRL      $C0000C-$C0000F  HSTDAT
 *
 * See native/musashi_cpu.c `gsp_host_reg()` and findings section 3.1.  The
 * routine writes a long address as `move.l D0,$c00002.l`, and the 68010 puts
 * the MOST significant word on the lower address first, so the high half
 * lands at $C00002 and the low half at $C00004.  `addi.l #$10,$c00002.l`
 * ($267A) is therefore a read-modify-write of the *device* HSTADR and not of
 * the ROM's D0 shadow; the shadow only moves by `addi.l #$1000,D0` per row
 * and by `subi.l #$7fc0,D0` per character.
 *
 * ── What is NOT modelled ────────────────────────────────────────────────
 * `gsp_read`/`gsp_write` are flat 16-bit GSP word accesses.  On the real
 * board the HSTDAT write lands in `hdgsp_vram_2bpp_w`
 * (atari/harddriv_v.cpp:300-316), a masked read-modify-write of 32-bit VRAM
 * words out of `m_mask_table`, so the bytes VRAM ends up holding are NOT the
 * word the host wrote.  This slice stops at the port.  Closing that hop needs
 * the 4 MiB VRAM extent work recorded in findings section 19 (stunrun-2ua).
 * `or.w D6,$c0000c.l` ($2674) reads HSTDAT back, so the choice is observable:
 * see the decision note at that line in mc68010_text_writer.c.
 *
 * ── Relationship to stunrun_mc68010_text_write ───────────────────────────
 * `stunrun_mc68010_text_write` below is the earlier, narrower slice: it takes
 * an already-resolved descriptor and emits ONE word per pixel.  It disagrees
 * with the ROM in two documented ways and is kept only because CTest
 * `mc68010-text-writer-slice-c` asserts those values:
 *
 *   1. the ROM emits no HSTDAT cycle at all for a zero font nibble (both
 *      `beq` at $2662 and $2670 are taken); the old function writes 0;
 *   2. the ROM emits two HSTDAT writes for a pixel whose nibble has both bit 0
 *      and bit 2 ($2666 then $2674); the old function emits one.
 *
 * `stunrun_mc68010_text_draw` is the faithful one.  Do not use the old
 * function's write count as a pixel count.
 */
#ifndef STUNRUN_MC68010_TEXT_WRITER_H
#define STUNRUN_MC68010_TEXT_WRITER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── ROM addresses the routine reads ──────────────────────────────────── */

/* lea $10136.l,A0 ($25F6): 394 long pointers, selector $00..$189. */
#define STUNRUN_TEXT_SELECTOR_TABLE 0x10136u
/* lea $1fbc.l,A1 ($2646): 16 bytes per glyph, indexed by character - $2A. */
#define STUNRUN_TEXT_FONT_BASE 0x1FBCu
#define STUNRUN_TEXT_GLYPH_STRIDE 16u
/* subi.b #$2a,D1 ($263A). */
#define STUNRUN_TEXT_GLYPH_BIAS 0x2Au

/* ── 68010 addresses the routine writes ───────────────────────────────── */

/* clr.w $608000.l ($25E8), the watchdog kick. */
#define STUNRUN_TEXT_WATCHDOG 0x00608000u
#define STUNRUN_TEXT_HSTADR_HI 0x00C00002u
#define STUNRUN_TEXT_HSTADR_LO 0x00C00004u
#define STUNRUN_TEXT_HSTCTL 0x00C00008u
#define STUNRUN_TEXT_HSTDAT 0x00C0000Cu

/* andi.w #$e7ff,$c00008.l ($25EE): clears HSTCTL 0x0800 (post-increment on
 * HSTDAT write) and 0x1000 (post-increment on HSTDAT read), so the rest of
 * the routine advances HSTADR by hand at $267A. */
#define STUNRUN_TEXT_HSTCTL_KEEP 0xE7FFu
#define STUNRUN_TEXT_HSTCTL_WRITE_INCR 0x0800u
#define STUNRUN_TEXT_HSTCTL_READ_INCR 0x1000u

/* ── The whole routine ────────────────────────────────────────────────── */

/* The transport seam.  Register-level state (`hstadr`, `hstctl`) is held here
 * so a caller can preset it -- which is what makes the prologue's
 * `andi.w #$e7ff,$c00008.l` observable -- while every 16-bit cycle the routine
 * drives is reported separately.
 *
 * THIS MUST STAY IN STEP with native/musashi_cpu.c `gsp_host_read_word` /
 * `gsp_host_write_word`, which is the same port bound to a live machine.
 * `stunrun_gsp_host_t` in gsp_boot.h is the same idea for the GSP boot
 * routines; it has no HSTDAT read, which `$2674` needs, so this routine
 * carries its own seam rather than a partial second copy of that one.
 */
typedef struct stunrun_mc68010_text_port {
    void *ctx;

    /* HSTADRH ($00) : HSTADRL ($04).  Preset by the caller; the first
     * `move.l D0,$c00002.l` overwrites it. */
    uint32_t hstadr;
    /* HSTCTLH:HSTCTLL ($08). */
    uint16_t hstctl;

    /* GSP memory behind HSTDAT.  `address` is HSTADR & 0xFFFFFFF0, matching
     * tms340x0_device::host_r/host_w.  `gsp_write` returns 0 to abort. */
    uint16_t (*gsp_read)(void *ctx, uint32_t address);
    int (*gsp_write)(void *ctx, uint32_t address, uint16_t value);

    /* 68010 program-space bus observer.  Every cycle is reported, INCLUDING
     * the read half of each read-modify-write, with the value the device
     * returned or was given.  That is what tells `move.l D0,$c00002.l` ($2656)
     * from `addi.l #$10,$c00002.l` ($267A) in a trace, and it is the shape the
     * committed capture is in: a bare two-write pair at $C00002/$C00004 with no
     * reads in front of it. */
    void (*bus_read)(void *ctx, uint32_t address, uint16_t value);
    void (*bus_write)(void *ctx, uint32_t address, uint16_t value);
} stunrun_mc68010_text_port_t;

/* ROM $25E8-$269F, whole routine.
 *
 * `rom` is the flat MC68010 program image and is indexed by the ROM addresses
 * the routine itself computes ($10136 for the selector table, $1FBC for the
 * font, and the descriptor pointer the table yields).  `port->hstctl` is read
 * and rewritten by the prologue.
 *
 * Returns the number of 16-bit bus writes driven, or 0 if the arguments are
 * unusable (NULL, or a ROM read that falls outside `rom`).  A usable call
 * always writes at least the two prologue words, so 0 unambiguously means
 * "did not run". */
size_t stunrun_mc68010_text_draw(uint16_t selector, const uint8_t *rom,
                                 size_t rom_size,
                                 stunrun_mc68010_text_port_t *port);

/* ── The earlier, narrower glyph-loop slice ───────────────────────────── */

/* Sink for stunrun_mc68010_text_write: `(host_address, word)`. */
typedef int (*stunrun_text_write_fn)(void *ctx, uint32_t address,
                                     uint16_t value);

/* Observable host-port writes from the executed $25E8 glyph loop.
 * SUPERSEDED by stunrun_mc68010_text_draw for anything bus- or VRAM-accurate;
 * see the file comment for the two ways it differs from the ROM. */
size_t stunrun_mc68010_text_write(const uint8_t *descriptor,
                                  size_t descriptor_size,
                                  const uint16_t *font_words,
                                  size_t font_word_count,
                                  stunrun_text_write_fn write, void *ctx);

#ifdef __cplusplus
}
#endif

#endif