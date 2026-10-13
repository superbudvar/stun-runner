/* Independent OpenGL scanout for comparison with native/gsp_display.c.
 * This consumes raw VRAM/palette/register state; it must not receive the CPU
 * renderer's RGB framebuffer. SDL/OpenGL types stay behind this seam. */
#ifndef STUNRUN_BACKEND_GSP_SCANOUT_GL_H
#define STUNRUN_BACKEND_GSP_SCANOUT_GL_H

#include <stddef.h>
#include <stdint.h>

#include "../native/gsp_display.h"

typedef struct SDL_Window SDL_Window;
typedef struct stunrun_gsp_gl_scanout stunrun_gsp_gl_scanout_t;

stunrun_gsp_gl_scanout_t *stunrun_gsp_gl_scanout_create(SDL_Window *window);
int stunrun_gsp_gl_scanout_present(stunrun_gsp_gl_scanout_t *scanout,
                                    const uint16_t *vram, size_t vram_words,
                                    const uint16_t *palette_lo,
                                    const uint16_t *palette_hi,
                                    size_t palette_count,
                                    const stunrun_gsp_display_regs_t *regs,
                                    uint8_t *readback_rgb,
                                    size_t readback_capacity);
void stunrun_gsp_gl_scanout_destroy(stunrun_gsp_gl_scanout_t *scanout);

#endif
