#include "gsp_display.h"

#include <string.h>

/* Frame 600 of the real board, from the STUNRUN-DPY probe added to
 * scanline_multisync() in mame-ref/src/mame/atari/harddriv_v.cpp:435:
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

/* Port of tms340x0_device::get_display_params() at tms34010.cpp:1160.
 *
 * With ORG=0 the register holds the one's complement of the linear start
 * address, which the TMS34010 User's Guide (SPVU001A section 9.9.1.4) states
 * applies to the address bits only; bits 0-1 carry LNCNT and are left alone,
 * hence the 0xfffc mask. */
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

    /* The mask comes from the share size, so a dump of the 4 MiB address
     * window (the share mirrored eight times) masks correctly too. */
    mask = (vram_words > 0u) ? (vram_words - 1u) : (size_t)STUNRUN_GSP_VRAM_MASK;

    dpyadr = regs->dpyadr != 0u ? regs->dpyadr : regs->dpystrt;
    for (v = 0; v < y1; ++v) {
        unsigned rowaddr = 0u, coladdr = 0u, yoffset = 0u;
        unsigned base, col, enabled;
        size_t dest_row;

        stunrun_gsp_display_params(regs, dpyadr, &rowaddr, &coladdr, &yoffset);
        enabled = (regs->dpyctl & STUNRUN_DPYCTL_NIL) ? 1u : 0u;
        /* rowaddr is ten bits, so rowaddr << 10 needs twenty. Masking to
         * 0xFFFF instead of the share mask silently truncates every row at or
         * above 0x40 -- which is why the first 64 lines of the frame can be
         * pixel-perfect while the rest is garbage. */
        base = ((rowaddr << 10) & STUNRUN_GSP_VRAM_MASK);

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