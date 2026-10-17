#include "../backend/gsp_scanout_gl.h"
#include "gsp_display.h"
#include "musashi_cpu.h"

#include <SDL2/SDL.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    const size_t vram_words = MUSASHI_GSP_VRAM_WORDS;
    const size_t image_bytes = (size_t)STUNRUN_RENDER_WIDTH *
                               STUNRUN_RENDER_HEIGHT * 3u;
    uint16_t *vram = (uint16_t *)malloc(vram_words * sizeof(*vram));
    uint16_t palette_lo[STUNRUN_GSP_PALETTE_ENTRIES];
    uint16_t palette_hi[STUNRUN_GSP_PALETTE_ENTRIES];
    uint8_t *gpu_rgb = (uint8_t *)malloc(image_bytes);
    stunrun_renderer_t cpu;
    stunrun_gsp_display_regs_t regs;
    SDL_Window *window;
    SDL_GLContext probe_context;
    stunrun_gsp_gl_scanout_t *scanout;
    size_t i;
    unsigned case_index;
    unsigned mismatches = 0;
    unsigned max_delta = 0;

    if (!vram || !gpu_rgb) return 1;
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SKIP: SDL video init unavailable: %s\n", SDL_GetError());
        free(vram); free(gpu_rgb);
        return 77;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    window = SDL_CreateWindow("scanout parity", SDL_WINDOWPOS_UNDEFINED,
                              SDL_WINDOWPOS_UNDEFINED, STUNRUN_RENDER_WIDTH,
                              STUNRUN_RENDER_HEIGHT,
                              SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!window) {
        fprintf(stderr, "SKIP: hidden GL window unavailable: %s\n", SDL_GetError());
        SDL_Quit(); free(vram); free(gpu_rgb);
        return 77;
    }
    probe_context = SDL_GL_CreateContext(window);
    if (!probe_context) {
        fprintf(stderr, "SKIP: GL context unavailable: %s\n", SDL_GetError());
        SDL_DestroyWindow(window); SDL_Quit(); free(vram); free(gpu_rgb);
        return 77;
    }
    SDL_GL_DeleteContext(probe_context);
    scanout = stunrun_gsp_gl_scanout_create(window);
    /* This test compares the GL scanout against the CPU reference pixel for
     * pixel in an unscaled window.  The display path letterboxes to 4:3, which
     * in a 512x240 window would pillarbox the image and make the bars read as
     * a content disagreement.  Turn it off HERE ONLY. */
    if (scanout)
        stunrun_gsp_gl_scanout_set_letterbox(scanout, 0);
    if (!scanout) {
        fprintf(stderr, "FAIL: GL scanout initialization failed\n");
        SDL_DestroyWindow(window); SDL_Quit(); free(vram); free(gpu_rgb);
        return 1;
    }

    for (i = 0; i < vram_words; ++i) {
        uint8_t lo = (uint8_t)(i * 31u + (i >> 8));
        uint8_t hi = (uint8_t)(i * 17u + (i >> 5));
        vram[i] = (uint16_t)(lo | ((uint16_t)hi << 8));
    }
    for (i = 0; i < STUNRUN_GSP_PALETTE_ENTRIES; ++i) {
        palette_lo[i] = (uint16_t)(((i * 29u) & 0xFFu) << 8) |
                        (uint16_t)((i * 13u) & 0xFFu);
        palette_hi[i] = (uint16_t)((i * 47u) & 0xFFu);
    }
    for (case_index = 0; case_index < 3u; ++case_index) {
        stunrun_gsp_display_frame600_regs(&regs);
        if (case_index == 1u) {
            /* Exercise fine scroll, tap offset, and a non-default origin.
             * The non-default origin goes in DPYSTRT, not DPYADR: DPYADR is
             * reloaded from DPYSTRT at the top of every frame, so a DPYADR
             * value is a mid-frame sample and cannot move the window. */
            regs.dpystrt = 0xFC39u;
            regs.dpytap = 0x001Du;
            regs.finescroll = 3u;
            regs.dpyctl = 0xF008u;
        } else if (case_index == 2u) {
            /* NIL clear must produce black in both independent paths. */
            regs.dpyctl &= (uint16_t)~STUNRUN_DPYCTL_NIL;
        }
        stunrun_render_init(&cpu);
        if (stunrun_gsp_display_render(&cpu, vram, vram_words, palette_lo,
                                      palette_hi,
                                      STUNRUN_GSP_PALETTE_ENTRIES, &regs) != 0 ||
            stunrun_gsp_gl_scanout_present(
                scanout, vram, vram_words, palette_lo, palette_hi,
                STUNRUN_GSP_PALETTE_ENTRIES, &regs, gpu_rgb,
                image_bytes) != 0) {
            fprintf(stderr, "FAIL: CPU/GPU render case %u failed\n",
                    case_index);
            stunrun_gsp_gl_scanout_destroy(scanout); SDL_DestroyWindow(window);
            SDL_Quit(); free(vram); free(gpu_rgb);
            return 1;
        }
        for (i = 0; i < image_bytes; ++i) {
            unsigned delta = gpu_rgb[i] > cpu.pixels[i]
                                 ? gpu_rgb[i] - cpu.pixels[i]
                                 : cpu.pixels[i] - gpu_rgb[i];
            if (delta != 0u) ++mismatches;
            if (delta > max_delta) max_delta = delta;
        }
    }
    printf("gsp-scanout-gl: cases=3 bytes-per-case=%zu mismatches=%u max-delta=%u\n",
           image_bytes, mismatches, max_delta);
    stunrun_gsp_gl_scanout_destroy(scanout);
    SDL_DestroyWindow(window);
    SDL_Quit();
    free(vram); free(gpu_rgb);
    if (max_delta > 1u || mismatches > image_bytes / 1000u) {
        fprintf(stderr, "FAIL: CPU/GPU scanout exceeded parity tolerance\n");
        return 1;
    }
    puts("gsp-scanout-gl: PASS");
    return 0;
}
