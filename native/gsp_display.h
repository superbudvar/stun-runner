/* TMS34010 display output for STUN Runner: scanline generation and palette
 * decode, transcribed from MAME 0.289 rather than guessed.
 *
 * This is the acceptance path for the display. Feeding it a board VRAM dump
 * that the real TMS34010 produced reproduces MAME's own output exactly
 * (47 distinct colours, every reference colour matched to 0.01%), which proves
 * our understanding of where the picture lives and how it is addressed. What
 * it deliberately does NOT do is draw: the pixels are already in VRAM when it
 * gets there, because the GSP microcode has put them there. Implementing that
 * microcode is the remaining work.
 *
 * Sources:
 *   scanline_multisync()     mame-ref/src/mame/atari/harddriv_v.cpp:436
 *   get_display_params()     mame-ref/src/devices/cpu/tms34010/tms34010.cpp:1160
 *   gsp_palette_change()     mame-ref/src/mame/atari/harddriv_v.cpp:326
 *   share size / row base    the STUNRUN-DPY + BASE probe, see
 *                            docs/mc68010-boot-findings.md section 12
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
 * GSP program addresses are bit addresses (addrshift=3), so the 0x400000-unit
 * mapped window resolves to this physical backing store. A row base is
 * (rowaddr << 10) words and must be masked to the backing extent. */
#define STUNRUN_GSP_VRAM_WORDS 0x040000u   /* 512 KiB / 2 */
#define STUNRUN_GSP_VRAM_MASK (STUNRUN_GSP_VRAM_WORDS - 1u)  /* 0x03FFFF */

/* paletteram is 8 banks of 256 pens; gsp_palette_change() indexes all 1024. */
#define STUNRUN_GSP_PALETTE_ENTRIES 1024u

/* DPYCTL bit fields, from tms34010.h. */
#define STUNRUN_DPYCTL_ORG 0x0400u  /* 0 => DPYADR is one's-complemented */
#define STUNRUN_DPYCTL_DUDATE 0x03FCu
#define STUNRUN_DPYCTL_NIL 0x8000u  /* display enable */

typedef struct stunrun_gsp_display_regs {
    uint16_t dpyadr;  /* SRFADR: refresh start address, pre-complement */
    uint16_t dpystrt;
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
