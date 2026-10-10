/*
 * test_oracle_vram.c — Feed MAME oracle GSP VRAM dumps into the render pipeline
 *
 * Loads the three files produced by mame_oracle_convert.py:
 *   vram.bin         — 1 MB GSP VRAM dump (raw bytes)
 *   palette_low.bin  — 512 bytes (256 × LE uint16_t: R<<8 | G)
 *   palette_high.bin — 256 bytes (256 × 1 byte: B)
 *
 * Each palette entry is: R = palette_low>>8, G = palette_low&0xFF, B = palette_high
 *
 * and passes them through stunrun_gsp_video_load_palette_planes() → render → PNG.
 *
 * Usage:
 *   ./test_oracle_vram <oracle_dir> [frame_number]
 *
 * Exit 0 — oracle loaded and rendered without error
 * Exit 1 — file missing or render error
 * Exit 2 — usage error
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gsp_video.h"
#include "render.h"

static int file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f) { fclose(f); return 1; }
    return 0;
}

int main(int argc, char **argv)
{
    const char *oracle_dir;
    int frame = 600;

    if (argc < 2 || argc > 3) {
        fprintf(stderr, "Usage: %s <oracle_dir> [frame_number]\n", argv[0]);
        return 2;
    }
    oracle_dir = argv[1];
    if (argc >= 3)
        frame = atoi(argv[2]);

    char vram_path[512], pal_low_path[512], pal_high_path[512], out_path[512];
    snprintf(vram_path,     sizeof(vram_path),     "%s/vram.bin",         oracle_dir);
    snprintf(pal_low_path,  sizeof(pal_low_path),  "%s/palette_low.bin",  oracle_dir);
    snprintf(pal_high_path, sizeof(pal_high_path), "%s/palette_high.bin", oracle_dir);
    snprintf(out_path,      sizeof(out_path),      "%s/oracle_f%04d.png", oracle_dir, frame);

    if (!file_exists(vram_path))     { fprintf(stderr, "MISSING: %s\n", vram_path);     return 1; }
    if (!file_exists(pal_low_path))   { fprintf(stderr, "MISSING: %s\n", pal_low_path);  return 1; }
    if (!file_exists(pal_high_path))  { fprintf(stderr, "MISSING: %s\n", pal_high_path); return 1; }

    /* Initialise the GSP video state */
    stunrun_gsp_video_state_t state;
    stunrun_gsp_video_init(&state);

    /* Load VRAM + palette from oracle dumps */
    printf("test_oracle: loading VRAM from %s\n", vram_path);
    int ok = stunrun_gsp_video_load_palette_planes(&state, vram_path,
                                                   pal_low_path, pal_high_path);
    if (!ok) {
        fprintf(stderr, "ERROR: stunrun_gsp_video_load_palette_planes failed\n");
        stunrun_gsp_video_free(&state);
        return 1;
    }
    printf("test_oracle: loaded VRAM words=%zu palette=OK\n", state.vram_word_count);

    /* Initialise the software renderer */
    stunrun_renderer_t renderer;
    stunrun_render_init(&renderer);
    renderer.frame = (unsigned)frame;

    /* Render GSP visible region to pixels buffer */
    ok = stunrun_gsp_video_render(&state, &renderer);
    if (!ok) {
        fprintf(stderr, "ERROR: stunrun_gsp_video_render failed\n");
        stunrun_gsp_video_free(&state);
        return 1;
    }

    /* Write PNG output */
    ok = stunrun_render_write_png(&renderer, out_path);
    if (!ok) {
        fprintf(stderr, "ERROR: stunrun_render_write_png failed for %s\n", out_path);
        stunrun_gsp_video_free(&state);
        return 1;
    }
    printf("test_oracle: frame=%d VRAM_words=%zu → %s\n", frame, state.vram_word_count, out_path);

    stunrun_gsp_video_free(&state);
    return 0;
}