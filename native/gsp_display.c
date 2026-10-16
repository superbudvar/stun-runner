#include "gsp_display.h"

#include <string.h>

/* Frame 600 of the real board, from the STUNRUN-DPY probe added to
 * scanline_multisync() in mame-ref/src/mame/atari/harddriv_v.cpp:512:
 *
 *   STUNRUN-DPY call=1 f=600 rowaddr=003c coladdr=0400 yoffset=0
 *              heblnk=110 hsblnk=622 vcount=0013 vsblnk=0103 veblnk=0013
 *              palbank=0 finescroll=7
 *   BASE off=61440 (0x00f000) byte=0x01e000 vram_words=262144 mask=0x003ffff
 */
void stunrun_gsp_display_frame600_regs(stunrun_gsp_display_regs_t *regs)
{
    if (regs == NULL) {
        return;
    }
    memset(regs, 0, sizeof(*regs));
    regs->dpystrt = 0xFC3Cu;
    regs->dpytap = 0x0000u;
    regs->dpyctl = 0xF004u; /* NIL | ORG=0 | DUDATE=4 */
    regs->heblnk = 110u;    /* 0x37 * 2 */
    regs->hsblnk = 622u;    /* 0x137 * 2 */
    regs->veblnk = 19u;
    regs->vsblnk = 259u;
    regs->finescroll = 7u;
    regs->palbank = 0u;
    /* DPYADR is derived from DPYSTRT by the first refresh step, exactly as the
     * hardware does, so it is not an independent input. */
    regs->dpyadr = 0u;
}

/* Port of tms340x0_device::get_display_params() at tms34010.cpp:1429.
 *
 * DPYADR is the screen-refresh address register, NOT a program-space address:
 * DPYADR[15:4] is the row and DPYADR[2:6] the high column bits, so there is no
 * "DPYADR as a VRAM word index" reading available and the row base has to come
 * from `x >> 4` followed by the row stride (see
 * stunrun_gsp_display_row_base_word).
 *
 * With ORG=0 the register holds the one's complement of the linear start
 * address, which the TMS34010 User's Guide (SPVU001A section 9.9.1.4) states
 * applies to the address bits only; bits 0-1 carry LNCNT and are left alone,
 * hence the 0xfffc mask. KEEP the XOR: tms34010.cpp:1442-1443 and
 * docs/tms34010-reference.md §1.2 both confirm it, and it is reproduced
 * word-for-word against MAME's own STUNRUN-DPY probe -- see
 * docs/mc68010-boot-findings.md §38. */
void stunrun_gsp_display_params(const stunrun_gsp_display_regs_t *regs,
                                uint16_t dpyadr, unsigned *rowaddr,
                                unsigned *coladdr, unsigned *yoffset)
{
    unsigned x;

    if ((regs->dpyctl & STUNRUN_DPYCTL_ORG) == 0u) {
        x = (unsigned)dpyadr ^ 0xFFFCu;
    } else {
        x = dpyadr;
    }
    if (rowaddr != NULL) {
        *rowaddr = x >> 4;
    }
    if (coladdr != NULL) {
        *coladdr = ((x & 0x007Cu) << 4) | ((unsigned)regs->dpytap & 0x3FFFu);
    }
    if (yoffset != NULL) {
        *yoffset = ((unsigned)regs->dpystrt - (unsigned)dpyadr) & 3u;
    }
}

/* harddriv_v.cpp:451, `&m_gsp_vram[(params->rowaddr << 10) & m_vram_mask]`.
 *
 * `rowaddr << 10` is 1024 uint16 words = 2048 physical bytes per VRAM row,
 * which is a TMS34010 display row: 2048 pixels of 2 bytes each.  Expressed
 * through the §34.2 unit chain it is row R starting at GSP bit address
 * 0xFF800000 + R*0x4000, and (R*0x4000 >> 3 >> 1) = R << 10 words.  The mask
 * is required and not decorative: rowaddr is twelve bits, so without it every
 * row at or above 0x40 lands in the wrong place -- which is why the first 64
 * lines of a frame can be pixel-perfect while the rest is garbage. */
unsigned stunrun_gsp_display_row_base_word(unsigned rowaddr)
{
    return ((rowaddr << 10) & STUNRUN_GSP_VRAM_MASK);
}

/* §34.2: GSP program-space addresses are bit addresses (addrshift 3), so
 * ((addr - 0xFF800000) >> 3) is the physical byte offset and >> 1 the word
 * index.  The & 0x7FFFF is the share extent and absorbs the bit-22 mirror
 * that multisync_gsp_map installs (harddriv.cpp:628): 0xFF800000 + 0x400000
 * aliases back to 0xFF800000. */
unsigned stunrun_gsp_display_word_from_bit_address(uint32_t gsp_bit_address)
{
    unsigned byte_offset =
        ((gsp_bit_address - 0xFF800000u) >> 3) & (STUNRUN_GSP_VRAM_WORDS * 2u - 1u);

    return byte_offset >> 1;
}

int stunrun_gsp_display_force_byte_offset(stunrun_gsp_display_regs_t *regs,
                                          uint32_t byte_offset)
{
    unsigned rowaddr;
    unsigned row_byte_base;

    if (regs == NULL || byte_offset >= STUNRUN_GSP_VRAM_WORDS * 2u)
        return 0;

    rowaddr = byte_offset >> 11u; /* 2048 physical bytes per display row */
    row_byte_base = rowaddr << 11u;
    /* DPYADR[15:4] contains the packed row field. For ORG=0 it is the
     * one's-complement of SRFADR; encode rowaddr<<4 before complementing.
     * Using rowaddr directly here silently truncated the row to rowaddr>>4. */
    regs->dpyadr = (uint16_t)(((rowaddr << 4u) ^ 0xFFFCu) & 0xFFFFu);
    regs->dpytap = (uint16_t)((byte_offset - row_byte_base) >> 3u);
    regs->dpyctl &= (uint16_t)~STUNRUN_DPYCTL_ORG;
    return 1;
}

int stunrun_gsp_display_build_palette(const uint16_t *lo, const uint16_t *hi,
                                      size_t count, uint8_t *rgb,
                                      size_t rgb_size)
{
    size_t i;

    if (rgb == NULL || rgb_size < STUNRUN_GSP_PALETTE_ENTRIES * 3u) {
        return -1;
    }
    for (i = 0; i < STUNRUN_GSP_PALETTE_ENTRIES; ++i) {
        uint16_t l = (lo != NULL && i < count) ? lo[i] : 0u;
        uint16_t h = (hi != NULL && i < count) ? hi[i] : 0u;
        rgb[i * 3u + 0u] = (uint8_t)((l >> 8) & 0xFFu); /* red   */
        rgb[i * 3u + 1u] = (uint8_t)(l & 0xFFu);        /* green */
        rgb[i * 3u + 2u] = (uint8_t)(h & 0xFFu);        /* blue  */
    }
    return 0;
}

/* update_display_params() from harddriv_v.cpp. Note the advance is gated on
 * the visible window: get_display_params() reloads from DPYSTRT at the top of
 * each line and only steps once per line while veblnk <= v < vsblnk, so DPYADR
 * is frozen through vblank. Advancing unconditionally shears the picture by
 * (VTOTAL - (vsblnk - veblnk)) rows, which is the single easiest way to get a
 * plausible-looking but wrong frame. */
static uint16_t dpyadr_advance(uint16_t dpyadr, uint16_t dpyctl,
                               uint16_t dpystrt)
{
    unsigned dudate = (unsigned)dpyctl & STUNRUN_DPYCTL_DUDATE;

    if ((dpyadr & 3u) == 0u) {
        return (uint16_t)(((unsigned)dpyadr & 0xFFFCu) -
                          (dudate & 0xFFFCu) | ((unsigned)dpystrt & 3u));
    }
    return (uint16_t)(((unsigned)dpyadr & 0xFFFCu) | ((unsigned)dpyadr - 1u) & 3u);
}

int stunrun_gsp_display_rasterise(uint16_t *dest, size_t dest_count,
                                  const uint16_t *vram, size_t vram_words,
                                  const stunrun_gsp_display_regs_t *regs)
{
    unsigned x0, x1, y0, y1, width, height, v, x;
    size_t mask;
    uint16_t dpyadr;

    if (dest == NULL || vram == NULL || regs == NULL) {
        return -1;
    }
    /* heblnk/hsblnk are already in pixels. The probe prints the register in
     * pixel clocks (0x37 -> 110) because PIXPERCLOCK is 2, so multiplying
     * again here silently shrinks the window to 414 px and desynchronises
     * every row stride -- which reads as a plausible but wrong picture
     * rather than as an error. */
    x0 = regs->heblnk;
    x1 = regs->hsblnk;
    y0 = regs->veblnk;
    y1 = regs->vsblnk;
    if (x1 <= x0 || y1 <= y0) {
        return -1;
    }
    if (x1 > STUNRUN_GSP_DISPLAY_H_TOTAL) {
        x1 = STUNRUN_GSP_DISPLAY_H_TOTAL;
    }
    if (y1 > STUNRUN_GSP_DISPLAY_V_TOTAL) {
        y1 = STUNRUN_GSP_DISPLAY_V_TOTAL;
    }
    width = x1 - x0;
    height = y1 - y0;
    if (dest_count < (size_t)width * height) {
        return -1;
    }

    /* The mask comes from the backing share size. The TMS34010 address window
     * is bit-addressed; its 4 Mi address units are only 512 KiB of physical
     * VRAM (addrshift=3). */
    mask = (vram_words > 0u) ? (vram_words - 1u) : (size_t)STUNRUN_GSP_VRAM_MASK;

    /* Frame start address: RESIDUAL KNOWN-WRONG, left in place deliberately.
     *
     * The TMS34010 reloads DPYADR from DPYSTRT prior to the start of each
     * frame (tms34010.cpp:1322-1327, and docs/tms34010-reference.md §1.2
     * quoting ug.txt:5432-5440: "the starting address points to the location
     * in memory of the first pixel output to the screen during each frame"),
     * so the first VISIBLE line is addressed by DPYSTRT and only later lines
     * by the running DPYADR.  Taking a mid-frame snapshot of DPYADR as the
     * frame's first address is therefore wrong whenever the caller can observe
     * DPYADR at an arbitrary instant -- which is exactly what the live RTL
     * path does.
     *
     * It is not changed here because the same heuristic is duplicated in
     * backend/gsp_scanout_gl.c:104 (outside this task's ownership), so
     * changing one copy alone desynchronises the CPU and GPU scanout windows
     * and breaks gsp-scanout-gl-parity. Both copies have to move together;
     * see docs/mc68010-boot-findings.md §38.4. */
    dpyadr = regs->dpyadr != 0u ? regs->dpyadr : regs->dpystrt;
    for (v = 0; v < y1; ++v) {
        unsigned rowaddr = 0u, coladdr = 0u, yoffset = 0u;
        unsigned base, col, enabled;
        size_t dest_row;

        stunrun_gsp_display_params(regs, dpyadr, &rowaddr, &coladdr, &yoffset);
        enabled = (regs->dpyctl & STUNRUN_DPYCTL_NIL) ? 1u : 0u;
        base = stunrun_gsp_display_row_base_word(rowaddr);

        if (v >= y0) {
            dest_row = (size_t)(v - y0) * width;
            if (enabled == 0u) {
                /* Display disabled: MAME's scanline callback is not called at
                 * all, so the line keeps whatever the bitmap already held. We
                 * zero it, which is what a cleared framebuffer does. */
                for (x = 0; x < width; ++x) {
                    dest[dest_row + x] = 0u;
                }
            } else {
                col = (yoffset << 9) + ((coladdr & 0xFFu) << 3) - 7u +
                      ((unsigned)regs->finescroll & 0x07u);
                for (x = x0; x < x1; ++x) {
                    /* 0x7FF, matching harddriv_v.cpp:540 exactly. The sibling
                     * scanline_driver() masks at 0xFFF because it builds
                     * 4096-pixel rows with `(coladdr & 0xff) << 4`; a
                     * TMS34010 display row is 2048 pixels, so the two
                     * callbacks and the two row strides are internally
                     * consistent and 0xFFF must not be imported from the
                     * wrong one. */
                    unsigned ci = col & 0x7FFu;
                    size_t word_index = ((size_t)base + (ci >> 1)) & mask;
                    uint16_t word =
                        (word_index < vram_words) ? vram[word_index] : 0u;
                    unsigned colour = (ci & 1u) ? ((unsigned)word >> 8)
                                                : ((unsigned)word & 0xFFu);
                    dest[dest_row + (x - x0)] =
                        (uint16_t)(((unsigned)regs->palbank * 256u + colour) &
                                   0x3FFu);
                    col += 1u;
                }
            }
        }
        if (v >= y0 && v + 1u < y1) {
            dpyadr = dpyadr_advance(dpyadr, regs->dpyctl, regs->dpystrt);
        }
    }
    return 0;
}

int stunrun_gsp_display_render(stunrun_renderer_t *renderer,
                               const uint16_t *vram, size_t vram_words,
                               const uint16_t *palette_lo,
                               const uint16_t *palette_hi, size_t palette_count,
                               const stunrun_gsp_display_regs_t *regs)
{
    static uint16_t pens[STUNRUN_RENDER_WIDTH * STUNRUN_RENDER_HEIGHT];
    uint8_t palette_rgb[STUNRUN_GSP_PALETTE_ENTRIES * 3u];
    unsigned x0, x1, y0, y1, width, height, x, y;

    if (renderer == NULL) {
        return -1;
    }
    if (stunrun_gsp_display_build_palette(palette_lo, palette_hi, palette_count,
                                          palette_rgb, sizeof(palette_rgb)) !=
        0) {
        return -1;
    }
    if (stunrun_gsp_display_rasterise(pens, sizeof(pens) / sizeof(pens[0]), vram,
                                      vram_words, regs) != 0) {
        return -1;
    }

    x0 = regs->heblnk;
    x1 = regs->hsblnk;
    y0 = regs->veblnk;
    y1 = regs->vsblnk;
    if (x1 > STUNRUN_GSP_DISPLAY_H_TOTAL) {
        x1 = STUNRUN_GSP_DISPLAY_H_TOTAL;
    }
    if (y1 > STUNRUN_GSP_DISPLAY_V_TOTAL) {
        y1 = STUNRUN_GSP_DISPLAY_V_TOTAL;
    }
    width = x1 - x0;
    height = y1 - y0;
    if (width > STUNRUN_RENDER_WIDTH) {
        width = STUNRUN_RENDER_WIDTH;
    }
    if (height > STUNRUN_RENDER_HEIGHT) {
        height = STUNRUN_RENDER_HEIGHT;
    }

    stunrun_render_init(renderer);
    stunrun_render_begin(renderer, renderer->frame, 0u, 0u, 0u);
    for (y = 0; y < height; ++y) {
        for (x = 0; x < width; ++x) {
            unsigned pen = pens[y * width + x] % STUNRUN_GSP_PALETTE_ENTRIES;
            (void)stunrun_render_set_pixel(renderer, x, y, palette_rgb[pen * 3u],
                                           palette_rgb[pen * 3u + 1u],
                                           palette_rgb[pen * 3u + 2u]);
        }
    }
    return 0;
}
