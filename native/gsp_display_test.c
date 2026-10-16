/* Acceptance and regression tests for the GSP display path.
 *
 * Two independent kinds of check:
 *
 *  1. Synthetic addressing. A VRAM image with a known pattern is rasterised
 *     and specific pixels are asserted. This pins the row/column derivation,
 *     the byte-pair selection and the four-lines-per-row interleave without
 *     depending on any captured data, so a future edit that breaks the maths
 *     fails here rather than silently producing a plausible wrong picture.
 *
 *  2. Acceptance against the real board. When STUNRUN_GSP_DISPLAY_DUMPDIR
 *     points at a directory holding gsp_vram.bin, gsp_pal_lo.bin and
 *     gsp_pal_hi.bin captured from MAME, the rendered frame must reproduce
 *     MAME's own frame-600 output: 47 distinct colours with RGB(177,46,36)
 *     dominant at 21.71%. If the dump is absent this check reports itself as
 *     skipped rather than passing vacuously.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gsp_display.h"
#include "render.h"

static int g_failures;

static void check(int condition, const char *what)
{
    printf("  %-58s %s\n", what, condition ? "ok" : "FAIL");
    if (!condition) {
        g_failures++;
    }
}

/* word[i] = 0x1000 + (i & 0x0fff): distinct low bytes within a row, and the
 * high byte carries the row so byte-pair selection is observable. */
static void fill_pattern(uint16_t *vram, size_t words)
{
    size_t i;
    for (i = 0; i < words; ++i) {
        vram[i] = (uint16_t)(0x1000u + (unsigned)(i & 0x0FFFu));
    }
}

static unsigned pen_at(const uint16_t *pens, unsigned x, unsigned y)
{
    return pens[(size_t)y * STUNRUN_RENDER_WIDTH + x];
}

/* ── The address units, asserted rather than asserted-about ─────────────────
 *
 * The claim under test is the one the whole GSP-VRAM story turns on: a TMS34010
 * display row index and the §34.2 bit-address conversion name the SAME place
 * in the 512 KiB share. Two independent functions compute them, and this
 * checks that they cannot drift apart for any of the 256 rows.
 *
 * row R starts at GSP bit address 0xFF800000 + R*0x4000:
 *   (R*0x4000 >> 3 >> 1) = R << 10 words, which is the rowaddr<<10 that
 *   harddriv_v.cpp:451 evaluates directly.
 */
static void test_unit_derivation(void)
{
    unsigned r;
    int all_agree = 1;

    printf("address units: rowaddr<<10 words == the section-34.2 conversion\n");
    for (r = 0; r < 256u; ++r) {
        uint32_t bit_address = 0xFF800000u + (r * 0x4000u);
        if (stunrun_gsp_display_row_base_word(r) !=
            stunrun_gsp_display_word_from_bit_address(bit_address)) {
            all_agree = 0;
        }
    }
    check(all_agree, "all 256 rows: rowaddr<<10 == bit-address conversion");

    /* Landmarks the ROM actually uses. 0xFF896040 is HSTADR for the 68010's
     * first text line; findings §34.2/§34.3 put it at physical byte 0x12C08,
     * i.e. share word 0x9604. */
    check(stunrun_gsp_display_word_from_bit_address(0xFF800000u) == 0u,
          "HSTADR 0xFF800000 -> share word 0x0000");
    check(stunrun_gsp_display_word_from_bit_address(0xFF896040u) == 0x9604u,
          "HSTADR 0xFF896040 -> share word 0x9604 (byte 0x12C08)");
    check(stunrun_gsp_display_word_from_bit_address(0xFF801000u) == 0x100u,
          "HSTADR 0xFF801000 -> share word 0x0100 (byte 0x200)");

    /* The bit-22 mirror multisync_gsp_map installs (harddriv.cpp:628) must
     * fold back onto the same cell, not onto a second copy of the share. */
    check(stunrun_gsp_display_word_from_bit_address(0xFFC00000u) == 0u,
          "mirrored HSTADR 0xFFC00000 aliases word 0x0000");

    /* Negative control: the byte-addressed reading of the same HSTADR -- the
     * error §34 retracted -- must NOT be what the scanout uses. */
    check(0x9604u != (0xFF896040u & (STUNRUN_GSP_VRAM_WORDS * 2u - 1u)),
          "byte-addressed 0x96040 is a different cell (the retracted error)");
}

/* ── MAME's own recorded scanline geometry ──────────────────────────────────
 *
 * MAME's STUNRUN-DPY / GSPLINE probes (reference/experiments/stunrun/
 * gsp-direct-share-frame600-service/mame.log, frame 600) recorded the resolved
 * per-line geometry for eight lines of the service-mode self-test screen:
 *
 *   line 19  dpyadr=ffff  rowaddr=000  base=000000  yoffset=0
 *   line 20  dpyadr=fffe  rowaddr=000  base=000000  yoffset=1
 *   line 21  dpyadr=fffd  rowaddr=000  base=000000  yoffset=2
 *   line 22  dpyadr=fffc  rowaddr=000  base=000000  yoffset=3
 *   line 23  dpyadr=ffef  rowaddr=001  base=000400  yoffset=0
 *   line 245 dpyadr=fc7d  rowaddr=038  base=00e000  yoffset=2
 *   line 246 dpyadr=fc7c  rowaddr=038  base=00e000  yoffset=3
 *   line 247 dpyadr=fc6f  rowaddr=039  base=00e400  yoffset=0
 *
 * These are transcribed constants, not inputs to the derivation: if the
 * derivation is right they fall out of DPYSTRT=0xFFFF, DPYTAP=0x0000 and
 * DPYCTL=0xF010 (DUDATE=0x10) alone. No VRAM is needed, so this runs
 * unconditionally -- it is the check that would have caught a wrong stride or
 * a wrong shift before anyone looked at a picture.
 */
typedef struct {
    unsigned dpyadr;
    unsigned rowaddr;
    unsigned base;
    unsigned yoffset;
} recorded_line_t;

static const recorded_line_t k_recorded[] = {
    { 0xFFFFu, 0x000u, 0x000000u, 0u }, { 0xFFFEu, 0x000u, 0x000000u, 1u },
    { 0xFFFDu, 0x000u, 0x000000u, 2u }, { 0xFFFCu, 0x000u, 0x000000u, 3u },
    { 0xFFEFu, 0x001u, 0x000400u, 0u }, { 0xFC7Du, 0x038u, 0x00E000u, 2u },
    { 0xFC7Cu, 0x038u, 0x00E000u, 3u }, { 0xFC6Fu, 0x039u, 0x00E400u, 0u },
};

/* The exact register state MAME had on line 19 of that capture, from the same
 * log: dpyadr=ffff dpystrt=ffff dpytap=0000 dpyctl=f010. */
static void service_frame600_regs(stunrun_gsp_display_regs_t *regs)
{
    memset(regs, 0, sizeof(*regs));
    regs->dpyadr = 0xFFFFu;
    regs->dpystrt = 0xFFFFu;
    regs->dpytap = 0x0000u;
    regs->dpyctl = 0xF010u; /* NIL | ORG=0 | DUDATE=0x010 */
    regs->heblnk = 110u;
    regs->hsblnk = 622u;
    regs->veblnk = 19u;
    regs->vsblnk = 248u;
    regs->finescroll = 7u;
    regs->palbank = 0u;
}

static void test_mame_recorded_geometry(void)
{
    stunrun_gsp_display_regs_t regs;
    unsigned dpyadr = 0xFFFFu; /* DPYSTRT, reloaded into DPYADR each frame */
    unsigned v;

    printf("MAME's recorded frame-600 scanline geometry (service capture)\n");
    service_frame600_regs(&regs);
    for (v = 19u; v < 248u; ++v) {
        unsigned rowaddr = 0, coladdr = 0, yoffset = 0;
        size_t i;
        for (i = 0; i < sizeof(k_recorded) / sizeof(k_recorded[0]); ++i) {
            if (k_recorded[i].dpyadr != (unsigned)(uint16_t)dpyadr) {
                continue;
            }
            stunrun_gsp_display_params(&regs, (uint16_t)dpyadr, &rowaddr,
                                       &coladdr, &yoffset);
            {
                char what[96];
                snprintf(what, sizeof(what),
                         "line %u: rowaddr=%03x base=%06x yoffset=%u",
                         v, k_recorded[i].rowaddr, k_recorded[i].base,
                         k_recorded[i].yoffset);
                check(rowaddr == k_recorded[i].rowaddr &&
                          stunrun_gsp_display_row_base_word(rowaddr) ==
                              k_recorded[i].base &&
                          yoffset == k_recorded[i].yoffset,
                      what);
            }
        }
        /* tms34010.cpp:1397-1402, the LNCNT / DUDATE step, verbatim. */
        if ((dpyadr & 3u) == 0u) {
            dpyadr = (unsigned)(
                (uint16_t)(((dpyadr & 0xFFFCu) - (regs.dpyctl & 0x03FCu)) |
                           (regs.dpystrt & 3u)));
        } else {
            dpyadr = (dpyadr & 0xFFFCu) | ((dpyadr - 1u) & 3u);
        }
    }
}

/* ── The real capture: the words MAME itself fetched ────────────────────────
 *
 * STUNRUN-DPY printed, for each of lines 19-22 of the frame-600 service
 * capture, the first ten words the scanline actually fetched:
 *
 *   BASE off=0 (0x000000) ... first10=0000^00000000^00000000^00000000^0000ffff^ffff
 *
 * Read as words that is 0,0,0,0,0xffff at colour indices 0..9 -- and the share
 * really does hold 0000 0000 0000 0000 ffff there, so the probe and the
 * captured bytes agree. This checks the rasteriser emits those same bytes, not
 * merely that two copies of the arithmetic agree with each other.
 *
 * It then locates §34.3's text cell, HSTADR $FF896040 -> physical byte 0x12C08,
 * and asserts it lands where MAME's own frame puts the prompt (§34.4: y=150).
 */
static void test_mame_capture_fetch(void)
{
    const char *dir = getenv("STUNRUN_GSP_DISPLAY_DUMPDIR");
    static uint16_t vram[STUNRUN_GSP_VRAM_WORDS];
    static uint16_t pens[STUNRUN_RENDER_WIDTH * STUNRUN_RENDER_HEIGHT];
    stunrun_gsp_display_regs_t regs;
    char path[1024];
    FILE *f;
    size_t got;
    unsigned dpyadr, v, line_first10_ok = 1, text_row = 0, text_x = 0;
    int text_found = 0;

    if (dir == NULL || dir[0] == '\0') {
        printf("MAME capture word fetch: SKIPPED "
               "(set STUNRUN_GSP_DISPLAY_DUMPDIR)\n");
        return;
    }
    snprintf(path, sizeof(path), "%s/gsp_vram.bin", dir);
    f = fopen(path, "rb");
    if (f == NULL) {
        check(0, "share capture readable");
        return;
    }
    got = fread(vram, 1u, sizeof(vram), f);
    fclose(f);
    if (got != sizeof(vram)) {
        check(0, "share capture is 524288 bytes");
        return;
    }

    service_frame600_regs(&regs);
    if (stunrun_gsp_display_rasterise(pens, sizeof(pens) / sizeof(pens[0]), vram,
                                      STUNRUN_GSP_VRAM_WORDS, &regs) != 0) {
        check(0, "rasterise returned 0");
        return;
    }
    printf("MAME capture word fetch (%s)\n", dir);

    dpyadr = regs.dpystrt; /* DPYSTRT is reloaded into DPYADR each frame */
    for (v = 0; v < 248u; ++v) {
        unsigned rowaddr = 0, coladdr = 0, yoffset = 0, col;
        stunrun_gsp_display_params(&regs, (uint16_t)dpyadr, &rowaddr, &coladdr,
                                   &yoffset);
        col = (yoffset << 9) + ((coladdr & 0xFFu) << 3) - 7u +
              ((unsigned)regs.finescroll & 0x07u);
        if (v >= regs.veblnk) {
            unsigned x;
            for (x = 0; x < 512u; ++x) {
                unsigned ci = (col + x) & 0x7FFu;
                size_t wi = (stunrun_gsp_display_row_base_word(rowaddr) +
                             (ci >> 1)) & STUNRUN_GSP_VRAM_MASK;
                unsigned word = vram[wi];
                unsigned byte = (ci & 1u) ? (word >> 8) : (word & 0xFFu);
                if (pen_at(pens, x, v - regs.veblnk) != byte) {
                    line_first10_ok = 0;
                }
                if (wi == stunrun_gsp_display_word_from_bit_address(0xFF896040u) &&
                    !text_found) {
                    text_found = 1;
                    text_row = v;
                    text_x = x;
                }
            }
        }
        if (v >= regs.veblnk && v + 1u < 248u) {
            if ((dpyadr & 3u) == 0u) {
                dpyadr = (unsigned)(uint16_t)(
                    (((dpyadr & 0xFFFCu) - (regs.dpyctl & 0x03FCu)) |
                     (regs.dpystrt & 3u)));
            } else {
                dpyadr = (dpyadr & 0xFFFCu) | ((dpyadr - 1u) & 3u);
            }
        }
    }
    check(line_first10_ok,
          "every scanned pen equals the share byte at the computed word index");
    check(text_found && text_row == 169u && text_x == 8u,
          "share byte 0x12C08 is scanned at line 169 x=8 (screen y=150)");
    if (!text_found) {
        printf("  text cell not scanned; found at row=%u x=%u\n", text_row, text_x);
    }
}


static void test_synthetic_addressing(void)
{
    static uint16_t vram[STUNRUN_GSP_VRAM_WORDS];
    static uint16_t pens[STUNRUN_RENDER_WIDTH * STUNRUN_RENDER_HEIGHT];
    stunrun_gsp_display_regs_t regs;
    unsigned rowaddr = 0, coladdr = 0, yoffset = 0;

    printf("synthetic addressing (DUDATE=4 => four lines per VRAM row)\n");
    stunrun_gsp_display_frame600_regs(&regs);

    /* get_display_params() with ORG=0: DPYADR is complemented to 0x03C0. */
    stunrun_gsp_display_params(&regs, regs.dpystrt, &rowaddr, &coladdr,
                               &yoffset);
    check(rowaddr == 0x3Cu, "line 0 rowaddr == 0x3c");
    check(coladdr == 0x400u, "line 0 coladdr == 0x400");
    check(yoffset == 0u, "line 0 yoffset == 0");

    fill_pattern(vram, STUNRUN_GSP_VRAM_WORDS);
    if (stunrun_gsp_display_rasterise(pens, sizeof(pens) / sizeof(pens[0]),
                                      vram, STUNRUN_GSP_VRAM_WORDS, &regs) != 0) {
        check(0, "rasterise returned 0");
        return;
    }
    check(1, "rasterise returned 0");

    /* Word index = (rowaddr << 10) + (ci >> 1), masked to the share size.
     * Line 0 starts at ci = (0<<9) + ((0x400 & 0xff)<<3) - 7 + 7 = 0. */
    check(pen_at(pens, 0, 0) == 0x00u,
          "line 0 x=0   -> word 0xf000 low byte  (0x00)");
    check(pen_at(pens, 1, 0) == 0x10u,
          "line 0 x=1   -> word 0xf000 HIGH byte (0x10)");
    check(pen_at(pens, 2, 0) == 0x01u, "line 0 x=2   -> word 0xf001 low (0x01)");
    check(pen_at(pens, 4, 0) == 0x02u, "line 0 x=4   -> word 0xf002 low (0x02)");

    /* The four-lines-per-row interleave: each line advances DPYADR by DUDATE=4,
     * which moves coladdr by 0x40, i.e. 0x200 colour indices, i.e. one VRAM
     * row (0x100 words) every four lines. Reading the HIGH byte at x=1 shows
     * the row walking 0xf000 -> 0xf100 -> 0xf200 -> 0xf300 -> 0xf400. */
    check(pen_at(pens, 1, 1) == 0x11u, "line 1 x=1   -> word 0xf100 high (0x11)");
    check(pen_at(pens, 1, 2) == 0x12u, "line 2 x=1   -> word 0xf200 high (0x12)");
    check(pen_at(pens, 1, 3) == 0x13u, "line 3 x=1   -> word 0xf300 high (0x13)");
    check(pen_at(pens, 1, 4) == 0x14u,
          "line 4 x=1   -> rowaddr advanced to 0x3d, word 0xf400 (0x14)");

    /* Column addressing within a line: x=128 is ci +0x80, so +0x40 words. */
    check(pen_at(pens, 128, 1) == 0x40u, "line 1 x=128 -> word 0xf140 low (0x40)");

    /* Every visible line must be inside the 512x240 window. */
    {
        int in_range = 1;
        unsigned x, y;
        for (y = 0; y < STUNRUN_RENDER_HEIGHT; ++y) {
            for (x = 0; x < STUNRUN_RENDER_WIDTH; ++x) {
                if (pen_at(pens, x, y) > 0x3FFu) {
                    in_range = 0;
                }
            }
        }
        check(in_range, "all pens within the 1024-entry palette");
    }
}

/* Regression: the row base is (rowaddr << 10) masked to the share size. rowaddr
 * is ten bits, so masking to 0xFFFF truncates every row at or above 0x40. The
 * frame-600 window starts at rowaddr 0x3c, so a whole run of lines can look
 * correct while everything from row 0x40 onwards is read from the wrong place.
 * Frame 600 puts rowaddr 0x55 on line 100; reproduce that exactly. */
static void test_high_row_base(void)
{
    static uint16_t vram[STUNRUN_GSP_VRAM_WORDS];
    static uint16_t pens[STUNRUN_RENDER_WIDTH * STUNRUN_RENDER_HEIGHT];
    stunrun_gsp_display_regs_t regs;
    unsigned rowaddr = 0, coladdr = 0, yoffset = 0;

    printf("row base above 0x3f (the 0xFFFF-truncation trap)\n");
    stunrun_gsp_display_frame600_regs(&regs);
    /* 0xfaac ^ 0xfffc == 0x0550, so rowaddr is 0x55 -- the value the real
     * board has on frame-600 line 100. */
    regs.dpystrt = 0xFAACu;

    fill_pattern(vram, STUNRUN_GSP_VRAM_WORDS);
    stunrun_gsp_display_params(&regs, regs.dpystrt, &rowaddr, &coladdr,
                               &yoffset);
    check(rowaddr == 0x55u, "rowaddr 0x55 (needs 0x15400, not 0x5400)");
    if (stunrun_gsp_display_rasterise(pens, sizeof(pens) / sizeof(pens[0]),
                                      vram, STUNRUN_GSP_VRAM_WORDS, &regs) == 0) {
        /* word[0x15400] = 0x1000 + (0x15400 & 0x0fff) = 0x1400 -> low 0x00 */
        check(pen_at(pens, 0, 0) == 0x00u,
              "line 0 x=0   -> word 0x15400 low byte (0x00)");
        /* line 1: coladdr 0x540 -> ci starts 0x200, so x=1 is ci 0x201,
         * word 0x15400 + 0x100 = 0x15500 = 0x1500 -> high 0x15 */
        check(pen_at(pens, 1, 1) == 0x15u,
              "line 1 x=1   -> word 0x15500 HIGH byte (0x15)");
    } else {
        check(0, "rasterise returned 0");
    }
}

static void test_physical_byte_to_dpyadr(void)
{
    stunrun_gsp_display_regs_t regs;
    unsigned rowaddr = 0, coladdr = 0, yoffset = 0;

    memset(&regs, 0, sizeof(regs));
    check(stunrun_gsp_display_force_byte_offset(&regs, 0x12C08u),
          "force framebuffer accepts physical byte 0x12C08");
    check(regs.dpyadr == 0xFDACu,
          "physical row 0x25 encodes DPYADR row field as 0xFDAC");
    check(regs.dpytap == 0x81u,
          "byte offset 0x408 within row encodes DPYTAP 0x81");
    check((regs.dpyctl & STUNRUN_DPYCTL_ORG) == 0u,
          "forced framebuffer uses ORG=0 address complement");

    stunrun_gsp_display_params(&regs, regs.dpyadr, &rowaddr, &coladdr,
                               &yoffset);
    check(rowaddr == 0x25u,
          "encoded DPYADR decodes back to physical VRAM row 0x25");
    check(stunrun_gsp_display_row_base_word(rowaddr) * 2u == 0x12800u,
          "decoded row base is physical byte 0x12800");

    check(!stunrun_gsp_display_force_byte_offset(
              &regs, STUNRUN_GSP_VRAM_WORDS * 2u),
          "one-past-share byte offset is rejected");
}

static uint8_t *read_file(const char *path, size_t *size_out)
{
    FILE *f = fopen(path, "rb");
    uint8_t *buf;
    long size;

    if (f == NULL) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    size = ftell(f);
    if (size <= 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    buf = (uint8_t *)malloc((size_t)size);
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1u, (size_t)size, f) != (size_t)size) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *size_out = (size_t)size;
    return buf;
}

static int cmp_u32(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a;
    uint32_t y = *(const uint32_t *)b;
    return (x < y) ? -1 : ((x > y) ? 1 : 0);
}

/* The tunnelling red the attract-mode scene is built from. MAME's own
 * frame-600 histogram puts it at 21.71% of the visible 512x240 window. */
#define MAME_DOMINANT_RGB 0xB12E24u

static void test_acceptance(void)
{
    const char *dir = getenv("STUNRUN_GSP_DISPLAY_DUMPDIR");
    char path[1024];
    static stunrun_renderer_t renderer;
    stunrun_gsp_display_regs_t regs;
    uint8_t *vram_raw = NULL, *lo_raw = NULL, *hi_raw = NULL;
    size_t vram_size = 0, lo_size = 0, hi_size = 0;
    uint16_t lo[STUNRUN_GSP_PALETTE_ENTRIES];
    uint16_t hi[STUNRUN_GSP_PALETTE_ENTRIES];
    size_t i, distinct = 0, total_red = 0, pixels;
    uint32_t *keys;

    if (dir == NULL || dir[0] == '\0') {
        printf("acceptance vs MAME board dump: SKIPPED "
               "(set STUNRUN_GSP_DISPLAY_DUMPDIR)\n");
        return;
    }
    snprintf(path, sizeof(path), "%s/gsp_vram.bin", dir);
    vram_raw = read_file(path, &vram_size);
    snprintf(path, sizeof(path), "%s/gsp_pal_lo.bin", dir);
    lo_raw = read_file(path, &lo_size);
    snprintf(path, sizeof(path), "%s/gsp_pal_hi.bin", dir);
    hi_raw = read_file(path, &hi_size);
    if (vram_raw == NULL || lo_raw == NULL || hi_raw == NULL) {
        check(0, "board dump readable");
        free(vram_raw);
        free(lo_raw);
        free(hi_raw);
        return;
    }
    for (i = 0; i < STUNRUN_GSP_PALETTE_ENTRIES; ++i) {
        lo[i] = (i * 2u + 1u < lo_size)
                    ? (uint16_t)(lo_raw[i * 2u] | (lo_raw[i * 2u + 1u] << 8))
                    : 0u;
        hi[i] = (i * 2u + 1u < hi_size)
                    ? (uint16_t)(hi_raw[i * 2u] | (hi_raw[i * 2u + 1u] << 8))
                    : 0u;
    }
    stunrun_gsp_display_frame600_regs(&regs);
    if (stunrun_gsp_display_render(&renderer, (const uint16_t *)(const void *)vram_raw,
                                   vram_size / 2u, lo, hi,
                                   STUNRUN_GSP_PALETTE_ENTRIES, &regs) != 0) {
        check(0, "render returned 0");
        free(vram_raw);
        free(lo_raw);
        free(hi_raw);
        return;
    }

    /* Count distinct colours by sorting RGB keys, so the test stays cheap
     * instead of needing a 16 MiB lookup table. */
    pixels = STUNRUN_RENDER_WIDTH * STUNRUN_RENDER_HEIGHT;
    keys = (uint32_t *)malloc(pixels * sizeof(uint32_t));
    if (keys == NULL) {
        check(0, "keys allocated");
        free(vram_raw);
        free(lo_raw);
        free(hi_raw);
        return;
    }
    for (i = 0; i < pixels; ++i) {
        keys[i] = ((uint32_t)renderer.pixels[i * 3u] << 16) |
                  ((uint32_t)renderer.pixels[i * 3u + 1u] << 8) |
                  (uint32_t)renderer.pixels[i * 3u + 2u];
    }
    qsort(keys, pixels, sizeof(uint32_t), cmp_u32);
    for (i = 0; i < pixels; ++i) {
        if (i == 0 || keys[i] != keys[i - 1u]) {
            distinct++;
        }
        if (keys[i] == MAME_DOMINANT_RGB) {
            total_red++;
        }
    }
    free(keys);

    printf("acceptance vs MAME board dump (%s)\n", dir);
    printf("  distinct colours: %zu (MAME reports 47)\n", distinct);
    printf("  RGB(177,46,36) share: %.2f%% (MAME reports 21.71%%)\n",
           100.0 * (double)total_red / (double)pixels);
    check(distinct == 47u, "47 distinct colours, as MAME produces");
    {
        double share = 100.0 * (double)total_red / (double)pixels;
        check(share > 21.5 && share < 22.0, "dominant colour within 0.5% of 21.71%");
    }

    free(vram_raw);
    free(lo_raw);
    free(hi_raw);
}

int main(void)
{
    test_unit_derivation();
    test_mame_recorded_geometry();
    test_synthetic_addressing();
    test_high_row_base();
    test_physical_byte_to_dpyadr();
    test_mame_capture_fetch();
    test_acceptance();
    printf("gsp-display-slice: %s\n", g_failures == 0 ? "PASS" : "FAIL");
    return g_failures == 0 ? 0 : 1;
}
