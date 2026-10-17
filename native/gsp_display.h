/* TMS34010 display output for STUN Runner: scanline generation and palette
 * decode, transcribed from MAME 0.289 rather than guessed.
 *
 * This is the acceptance path for the display. Feeding it a board VRAM dump
 * that the real TMS34010 produced reproduces MAME's own output: on the
 * committed frame-600 service capture, with that capture's own registers, it
 * regenerates the self-test screen, and the 68010's first text cell
 * (HSTADR $FF896040 -> physical byte $12C08) is fetched at scanline 169,
 * x=8 -- where MAME puts the prompt. See findings section 38, which supersedes
 * the earlier "47 distinct colours / 21.71% dominant red" claim: those are
 * section 12's attract-mode numbers and do not hold against the only capture
 * committed here.
 *
 * What it deliberately does NOT do is draw: the pixels are already in VRAM
 * when it gets there, because the GSP microcode has put them there.
 * Implementing that microcode is the remaining work.
 *
 * Sources:
 *   scanline_multisync()     mame-ref/src/mame/atari/harddriv_v.cpp:448
 *                            (row base at :451, colour index at :540)
 *   get_display_params()     mame-ref/src/devices/cpu/tms34010/tms34010.cpp:1429
 *   the per-frame DPYADR reload  mame-ref/src/devices/cpu/tms34010/tms34010.cpp:1322
 *   gsp_palette_change()     mame-ref/src/mame/atari/harddriv_v.cpp:330
 *   share size / row base    the STUNRUN-DPY + BASE probe, see
 *                            docs/mc68010-boot-findings.md sections 12 and 38
 */
#ifndef STUNRUN_NATIVE_GSP_DISPLAY_H
#define STUNRUN_NATIVE_GSP_DISPLAY_H

#include <stddef.h>
#include <stdint.h>

#include "render.h"

/* Screen geometry, from stunrun_board_device_state::device_add_mconfig:
 *   m_screen->set_raw(5000000*2, 317*2, 0, 256*2, 262, 0, 228)
 * i.e. 634 x 262 total with 512 x 228 visible, two pixels per pixel clock. */
#define STUNRUN_GSP_DISPLAY_H_TOTAL 634u
#define STUNRUN_GSP_DISPLAY_H_BLANK 512u
#define STUNRUN_GSP_DISPLAY_V_TOTAL 262u
#define STUNRUN_GSP_DISPLAY_PIXPERCLOCK 2u

/* The board has 16 x 64Kx4 VRAM chips = 512 KiB = 0x40000 uint16 words.
 *
 * ── Address units: three of them meet here, and conflating any two is the ──
 * ── single most expensive mistake in this file's history (§§19/27/30-34). ──
 *
 * 1. GSP *program-space* address units are BITS.  The program space is
 *    `memory_access<32, 1, 3, little>` (tms34010.h:996) — datawidth 16,
 *    addrshift 3 — so one address unit is one bit of the GRAM, one 16-bit VRAM
 *    word is 0x10 address units, and the 0x400000-unit mapped window resolves
 *    to 512 KiB of physical backing.  Findings §34.2.
 *
 * 2. DPYADR / DPYSTRT are NOT in that space.  They are the screen-refresh
 *    address registers: the TMS34010 User's Guide puts DPYADR[15:4] on
 *    LAD12..LAD23 during row address time and DPYADR[2:6] on LAD6..LAD10
 *    during column address time (ug.txt:5027-5038, transcribed in
 *    docs/tms34010-reference.md §1.1).  DPYADR is a packed (row, column)
 *    pair, not a linear address, which is why `dpyadr ^ 0xFFFC` is the
 *    ORG=0 one's-complement of SRFADR and must be KEPT, and why feeding
 *    `dpyadr` in as a share word index is meaningless.
 *
 * 3. The share is BYTES: 524288 of them, 262144 uint16 words.
 *
 * The bridge from (2) to (3) is `rowaddr << 10` words — exactly what MAME's
 * scanline_multisync() computes (harddriv_v.cpp:451) — and that is also the
 * §34.2 bit-address conversion applied to the row's first bit address:
 *
 *   row R begins at GSP bit address 0xFF800000 + R * 0x4000
 *   word index = ((0xFF800000 + R*0x4000) - 0xFF800000) >> 3 >> 1
 *              = (R << 14) >> 4 = R << 10                 (words)
 *
 * stunrun_gsp_display_row_base_word() and
 * stunrun_gsp_display_word_from_bit_address() are separate functions so that
 * equality is asserted by a test rather than taken on trust.
 */
#define STUNRUN_GSP_VRAM_WORDS 0x040000u   /* 512 KiB / 2 */
#define STUNRUN_GSP_VRAM_MASK (STUNRUN_GSP_VRAM_WORDS - 1u)  /* 0x03FFFF */

/* paletteram is 8 banks of 256 pens; gsp_palette_change() indexes all 1024. */
#define STUNRUN_GSP_PALETTE_ENTRIES 1024u

/* DPYCTL bit fields, from tms34010.h. */
#define STUNRUN_DPYCTL_ORG 0x0400u  /* 0 => DPYADR is one's-complemented */
#define STUNRUN_DPYCTL_DUDATE 0x03FCu
#define STUNRUN_DPYCTL_NIL 0x8000u  /* display enable */

typedef struct stunrun_gsp_display_regs {
    uint16_t dpyadr;  /* SRFADR as OBSERVED mid-frame; NOT an input (see below) */
    uint16_t dpystrt; /* SRFADR at frame start: the timing generator reloads
                       * DPYADR from this at vcount == VSBLNK, every frame
                       * (tms34010.cpp:1321-1327), so this is the frame origin */
    uint16_t dpytap;
    uint16_t dpyctl;
    uint16_t heblnk;  /* horizontal blank start, in PIXELS (register 0x37 * 2) */
    uint16_t hsblnk;  /* horizontal blank end, in pixels */
    uint16_t veblnk;  /* vertical blank, in lines */
    uint16_t vsblnk;
    uint8_t finescroll;
    uint8_t palbank; /* from control_hi bits 0x02/0x03/0x04 */
} stunrun_gsp_display_regs_t;

/* The register set observed at frame 600 (attract mode), so the shell can
 * show a picture from a board dump before any GSP core exists. */
void stunrun_gsp_display_frame600_regs(stunrun_gsp_display_regs_t *regs);

/* get_display_params(): turn DPYADR into a VRAM row/column pair. */
void stunrun_gsp_display_params(const stunrun_gsp_display_regs_t *regs,
                                uint16_t dpyadr, unsigned *rowaddr,
                                unsigned *coladdr, unsigned *yoffset);

/* Row base as a 16-bit word index into the share: (rowaddr << 10) masked to
 * the backing extent, which is the literal expression harddriv_v.cpp:451
 * evaluates. See the unit derivation at the top of this header. */
unsigned stunrun_gsp_display_row_base_word(unsigned rowaddr);

/* §34.2 conversion: GSP *program-space bit* address -> 16-bit share word
 * index. Independent of the display-address path above; the test asserts the
 * two agree for all 256 rows, which is what makes "the scanout reads the
 * share with the right units" a checked statement rather than a comment. */
unsigned stunrun_gsp_display_word_from_bit_address(uint32_t gsp_bit_address);

/* Diagnostic bridge from a physical share byte offset to the TMS34010's
 * packed DPYSTRT/DPYTAP refresh registers. `byte_offset` must be in the
 * 512-KiB share. DPYSTRT, not DPYADR, because DPYADR is reloaded from DPYSTRT
 * at the top of every frame and a caller-supplied DPYADR cannot be the frame
 * origin (stunrun_gsp_display_rasterise ignores it). */
int stunrun_gsp_display_force_byte_offset(stunrun_gsp_display_regs_t *regs,
                                          uint32_t byte_offset);

/* gsp_palette_change(): red=(lo>>8)&0xff, green=lo&0xff, blue=hi&0xff.
 * `count` is the number of 16-bit entries in each plane. */
int stunrun_gsp_display_build_palette(const uint16_t *lo, const uint16_t *hi,
                                      size_t count, uint8_t *rgb,
                                      size_t rgb_size);

/* Rasterise the visible window straight into a renderer. Writes exactly
 * STUNRUN_RENDER_WIDTH x STUNRUN_RENDER_HEIGHT pixels (512 x 240), which is
 * [heblnk, hsblnk) x [veblnk, vsblnk). */
int stunrun_gsp_display_render(stunrun_renderer_t *renderer,
                               const uint16_t *vram, size_t vram_words,
                               const uint16_t *palette_lo,
                               const uint16_t *palette_hi, size_t palette_count,
                               const stunrun_gsp_display_regs_t *regs);

/* Same, but emitting pen indices instead of RGB, for tests that want to check
 * addressing without going through the palette. `dest` receives
 * STUNRUN_RENDER_WIDTH * STUNRUN_RENDER_HEIGHT pen indices. */
int stunrun_gsp_display_rasterise(uint16_t *dest, size_t dest_count,
                                  const uint16_t *vram, size_t vram_words,
                                  const stunrun_gsp_display_regs_t *regs);

#endif
