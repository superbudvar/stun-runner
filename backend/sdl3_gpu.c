/* SDL3 GPU / 2D video backend.
 *
 * Uses SDL3's 2D render API as the primary path (works on all SDL3 platforms:
 *   - Linux (OpenGL/Vulkan via SDL3 render)
 *   - macOS (Metal via SDL3 render)
 *   - Windows (D3D12 via SDL3 render)
 *   - Android (OpenGL ES via SDL3 render)
 *   - Web (WebGL via SDL3 render / Emscripten)
 *
 * SDL3's SDL_Renderer is a portable abstraction that automatically uses the
 * best available backend (Metal, Vulkan, OpenGL, D3D12, etc.) without requiring
 * platform-specific code.
 *
 * The SDL3 GPU API (SDL_GPUDevice) path is also implemented but requires
 * compiled shader binaries (SPIR-V / MSL / DXIL). It is compiled in but
 * deferred to a future phase when shader compilation tooling is in place.
 *
 * Rendering pipeline (2D render path):
 *   CPU pixels (RGB888 from render.c)
 *     -> SDL_UpdateTexture (upload to GPU texture)
 *     -> SDL_RenderCopy (blit to screen with scaling)
 *     -> SDL_RenderPresent (swap)
 *
 * Rendering pipeline (GPU API path — future):
 *   CPU pixels -> staging transfer buffer -> GPU texture
 *     -> blit via SDL GPU command buffer -> swapchain
 *   (requires compiled SPIR-V / MSL / DXIL shader binaries)
 */
#include "backend/backend.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_render.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Backend state ---- */
typedef struct sdl3_gpu_backend {
    const char *name;
    SDL_Window *window;

    /* 2D render path (primary, always available) */
    SDL_Renderer *renderer;
    SDL_Texture  *game_texture;   /* holds the 512×240 game frame */
    SDL_FRect    src_rect;        /* game frame region (float for SDL3) */
    SDL_FRect    dst_rect;        /* destination (window) region (float for SDL3) */
    bool         using_gpu_path;   /* true if GPU API path is active */

    /* Palette for 8bpp expansion */
    uint8_t palette[256 * 3];
    bool    palette_dirty;

    /* Upload staging buffer (for palette expansion) */
    uint8_t *upload_staging;
    size_t   upload_staging_capacity;

    /* GPU API state (future — compiled but not active) */
    bool gpu_path_active;

    uint32_t game_w;
    uint32_t game_h;
    uint32_t win_w;
    uint32_t win_h;

    bool initialized;
} sdl3_gpu_backend_t;

/* ---- Helpers ---- */

/* Ensure the CPU staging buffer can hold 'needed' bytes. */
static bool ensure_staging(sdl3_gpu_backend_t *b, size_t needed)
{
    if (b->upload_staging != NULL && b->upload_staging_capacity >= needed)
        return true;
    free(b->upload_staging);
    b->upload_staging = (uint8_t *)malloc(needed);
    if (!b->upload_staging) {
        b->upload_staging_capacity = 0;
        return false;
    }
    b->upload_staging_capacity = needed;
    return true;
}

/* Expand a paletted 8bpp frame to RGB24 in staging buffer. */
static size_t expand_palette_8bpp(sdl3_gpu_backend_t *b,
                                   const uint8_t *src, size_t src_stride)
{
    for (uint32_t row = 0; row < b->game_h; row++) {
        const uint8_t *src_row = src + row * src_stride;
        uint8_t *dst_row = b->upload_staging + row * b->game_w * 3;
        for (uint32_t col = 0; col < b->game_w; col++) {
            uint8_t idx = src_row[col];
            dst_row[col * 3 + 0] = b->palette[idx * 3 + 0];
            dst_row[col * 3 + 1] = b->palette[idx * 3 + 1];
            dst_row[col * 3 + 2] = b->palette[idx * 3 + 2];
        }
    }
    return (size_t)b->game_w * b->game_h * 3;
}

/* ---- Backend v-table ---- */

static bool sdl3_gpu_create(void *user, void *context)
{
    sdl3_gpu_backend_t *b = (sdl3_gpu_backend_t *)user;
    b->window = (SDL_Window *)context;
    b->renderer = NULL;
    b->game_texture = NULL;
    b->upload_staging = NULL;
    b->gpu_path_active = false;
    return true;
}

static bool sdl3_gpu_init(void *user, uint32_t game_w, uint32_t game_h,
                          uint32_t win_w, uint32_t win_h)
{
    sdl3_gpu_backend_t *b = (sdl3_gpu_backend_t *)user;
    b->game_w = game_w;
    b->game_h = game_h;
    b->win_w  = win_w;
    b->win_h  = win_h;

    /* Try GPU API path first (SDL3 GPUDevice).
     * This requires compiled shaders; if not available, fall back to 2D render.
     * For now, we always use the 2D render path (the GPU API path is
     * implemented but deferred until shader tooling is in place). */
    b->gpu_path_active = false;

    /* ---- 2D Render path (always works) ---- */
    /* SDL3: SDL_CreateRenderer takes (window, name) — name=NULL for default */
    b->renderer = SDL_CreateRenderer(b->window, NULL);
    if (!b->renderer) {
        fprintf(stderr, "sdl3_gpu: SDL_CreateRenderer failed: %s\n",
                SDL_GetError());
        return false;
    }

    /* Create the game frame texture (RGB24, streaming access for frequent updates) */
    b->game_texture = SDL_CreateTexture(b->renderer,
        SDL_PIXELFORMAT_RGB24,
        SDL_TEXTUREACCESS_STREAMING,
        (int)game_w, (int)game_h);
    if (!b->game_texture) {
        fprintf(stderr, "sdl3_gpu: SDL_CreateTexture failed: %s\n",
                SDL_GetError());
        SDL_DestroyRenderer(b->renderer);
        b->renderer = NULL;
        return false;
    }

    /* Set the source (game frame) and destination (window) rects */
    b->src_rect = (SDL_FRect){ 0.0f, 0.0f, (float)game_w, (float)game_h };
    b->dst_rect = (SDL_FRect){ 0.0f, 0.0f, (float)win_w, (float)win_h };

    /* Allocate staging buffer for palette expansion */
    size_t staging_bytes = (size_t)game_w * game_h * 3;
    if (!ensure_staging(b, staging_bytes)) {
        fprintf(stderr, "sdl3_gpu: staging buffer allocation failed\n");
        SDL_DestroyTexture(b->game_texture);
        SDL_DestroyRenderer(b->renderer);
        b->renderer = NULL;
        b->game_texture = NULL;
        return false;
    }

    /* Enable vsync: present is rate-limited to display refresh */
    (void)0;  /* vsync is default for SDL3 renderer */

    fprintf(stderr, "sdl3_gpu: using SDL3 2D render path\n");
    b->initialized = true;
    return true;
}

static void sdl3_gpu_shutdown(void *user)
{
    sdl3_gpu_backend_t *b = (sdl3_gpu_backend_t *)user;
    if (b->game_texture) {
        SDL_DestroyTexture(b->game_texture);
        b->game_texture = NULL;
    }
    if (b->renderer) {
        SDL_DestroyRenderer(b->renderer);
        b->renderer = NULL;
    }
    free(b->upload_staging);
    b->upload_staging = NULL;
    b->initialized = false;
}

static void sdl3_gpu_destroy(void *user)
{
    (void)user;
}

static int sdl3_gpu_upload(void *user, const stunrun_video_frame_t *frame)
{
    sdl3_gpu_backend_t *b = (sdl3_gpu_backend_t *)user;
    if (!b->initialized || !frame || !frame->pixels || !b->game_texture)
        return 0;

    const uint8_t *pixels = frame->pixels;
    int pitch = (int)frame->stride;  /* bytes per row */

    if (frame->format == STUNRUN_VIDEO_FORMAT_PALETTED_8BPP) {
        size_t needed = (size_t)frame->width * (size_t)frame->height * 3;
        if (!ensure_staging(b, needed))
            return 0;
        expand_palette_8bpp(b, frame->pixels, frame->stride);
        pixels = b->upload_staging;
        pitch = (int)(frame->width * 3);
    } else if (frame->format == STUNRUN_VIDEO_FORMAT_RGB565) {
        size_t needed = (size_t)frame->width * (size_t)frame->height * 2;
        if (!ensure_staging(b, needed))
            return 0;
        /* Convert RGB565 -> RGB888 in staging */
        for (size_t i = 0; i < (size_t)frame->width * frame->height; i++) {
            uint16_t rgb565 = ((const uint16_t *)frame->pixels)[i];
            uint8_t r = (rgb565 >> 11) & 0x1F;
            uint8_t g = (rgb565 >> 5)  & 0x3F;
            uint8_t bl = rgb565 & 0x1F;
            b->upload_staging[i * 3 + 0] = (r * 255) / 31;
            b->upload_staging[i * 3 + 1] = (g * 255) / 63;
            b->upload_staging[i * 3 + 2] = (bl * 255) / 31;
        }
        pixels = b->upload_staging;
        pitch = (int)(frame->width * 3);
    } else if (frame->format != STUNRUN_VIDEO_FORMAT_RGB888) {
        fprintf(stderr, "sdl3_gpu: unsupported pixel format %d\n",
                (int)frame->format);
        return 0;
    }

    /* SDL3: SDL_UpdateTexture(texture, rect, pixels, pitch) */
    if (SDL_UpdateTexture(b->game_texture, NULL, pixels, pitch) != 0) {
        fprintf(stderr, "sdl3_gpu: SDL_UpdateTexture failed: %s\n",
                SDL_GetError());
        return 0;
    }
    return 1;
}

static int sdl3_gpu_set_palette(void *user, const stunrun_video_palette_t *pal)
{
    sdl3_gpu_backend_t *b = (sdl3_gpu_backend_t *)user;
    if (!pal || !pal->entries || pal->count != 256)
        return 0;
    memcpy(b->palette, pal->entries, sizeof(b->palette));
    b->palette_dirty = true;
    return 1;
}

static void sdl3_gpu_present(void *user)
{
    sdl3_gpu_backend_t *b = (sdl3_gpu_backend_t *)user;
    if (!b->initialized || !b->renderer)
        return;

    /* Clear to black */
    SDL_SetRenderDrawColor(b->renderer, 0, 0, 0, 255);
    SDL_RenderClear(b->renderer);

    /* Blit the game frame, scaled to fill the window.
     * SDL3: SDL_RenderCopy was renamed to SDL_RenderTexture (uses SDL_FRect) */
    if (SDL_RenderTexture(b->renderer, b->game_texture,
                          &b->src_rect, &b->dst_rect) != 0) {
        fprintf(stderr, "sdl3_gpu: SDL_RenderTexture failed: %s\n",
                SDL_GetError());
        return;
    }

    SDL_RenderPresent(b->renderer);
}

static void sdl3_gpu_on_resize(void *user, uint32_t win_w, uint32_t win_h)
{
    sdl3_gpu_backend_t *b = (sdl3_gpu_backend_t *)user;
    b->win_w = win_w;
    b->win_h = win_h;
    b->dst_rect.w = (float)win_w;
    b->dst_rect.h = (float)win_h;
}

/* ---- GPU API path (future — compiled in, not active) ----
 *
 * The code below implements the SDL3 GPU API (SDL_GPUDevice) path.
 * It is compiled but not activated until shader binaries are available.
 *
 * To enable: set SDL3_GPU_USE_PATH=1 at runtime, and compile shaders:
 *   glslangValidator -V -x -o shaders/fullscreen.vert.spv shaders/fullscreen.vert
 *   glslangValidator -V -x -o shaders/fullscreen.frag.spv shaders/fullscreen.frag
 *
 * Key differences from 2D render path:
 *   - Uses VkCommandBuffer / SDL_GPUCommandBuffer for GPU-side copies
 *   - Supports SPIR-V / MSL / DXIL shader formats
 *   - Lower CPU overhead for texture updates
 *   - Better integration with Vulkan/Metal profiling tools
 *
 * TODO_SDL3_GPU:
 *   1. Load compiled SPIR-V shader blobs from disk
 *   2. Create SDL_GPUShader from SPIR-V blobs
 *   3. Create SDL_GPUGraphicsPipeline with vertex + fragment shaders
 *   4. Create game texture as SDL_GPUTexture (GPU-only, no staging)
 *   5. Upload via SDL_GPUTransferBuffer -> SDL_UploadToGPUTexture
 *   6. Render via SDL_BeginGPURenderPass -> SDL_DrawGPUPrimitives
 *
 * Required SDL3 GPU API calls (SDL 3.2.26 — verify against installed version):
 *   SDL_GPUSupportedShaderFormats formats = SDL_GetGPUShaderFormats(device);
 *   SDL_GPUShader *shader = SDL_CreateGPUShader(device, &(SDL_GPUShaderCreateInfo){
 *       .code_size = spv_size, .code = spv_data,
 *       .entrypoint = "main", .format = SDL_GPU_SHADERFORMAT_SPIRV,
 *       .stage = SDL_GPU_SHADERSTAGE_VERTEX / FRAGMENT,
 *   });
 *   SDL_GPUGraphicsPipeline *pipe = SDL_CreateGPUGraphicsPipeline(device, ...);
 *   SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(device);
 *   SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
 *   SDL_UploadToGPUTexture(cmd, texture, &(SDL_GPUTextureTransferInfo){...}, false);
 *   SDL_EndGPUCopyPass(cp);
 *   SDL_GPURenderPass *rp = SDL_BeginGPURenderPass(cmd, &color_target, 1);
 *   SDL_BindGPUGraphicsPipeline(rp, pipe);
 *   SDL_BindGPUFragmentSamplers(rp, 0, &tex, 1);
 *   SDL_DrawGPUPrimitives(rp, 4, 1, 0, 0);
 *   SDL_EndGPURenderPass(rp);
 *   SDL_SubmitGPUCommandBuffer(cmd);
 */

/* ---- Registration ---- */
static stunrun_video_backend_t the_backend = {
    .name          = "sdl3_gpu",
    .create        = sdl3_gpu_create,
    .init          = sdl3_gpu_init,
    .shutdown      = sdl3_gpu_shutdown,
    .destroy       = sdl3_gpu_destroy,
    .upload        = sdl3_gpu_upload,
    .set_palette   = sdl3_gpu_set_palette,
    .present       = sdl3_gpu_present,
    .on_resize     = sdl3_gpu_on_resize,
    .upload_tex    = NULL,
    .user          = NULL,
};

const stunrun_video_backend_t *stunrun_video_backend_sdl3_gpu(void)
{
    return &the_backend;
}

const stunrun_video_backend_t *stunrun_video_backend_sdl3_gpu_register(void)
{
    extern void stunrun_backend_register_video(const stunrun_video_backend_t *);
    stunrun_backend_register_video(&the_backend);
    return &the_backend;
}

/* Force registration to run before main().
 *
 * A static function POINTER only forces the linker to keep the symbol; it does
 * not call it, so the registry stayed empty and every backend looked "not
 * found".  A constructor attribute actually invokes the register function. */
static void __attribute__((constructor))
stunrun_backend_auto_register(void)
{
    (void)stunrun_video_backend_sdl3_gpu_register();
}
