/* Metal video backend stub for Apple platforms.
 *
 * Targets: macOS, iOS, tvOS (all Apple platforms via Metal).
 *
 * This backend:
 *   - Creates a CAMetalLayer-backed drawable on the given NSWindow/UIView.
 *   - Uploads pixel data from the game frame to an MTLTexture (512×240).
 *   - Blits the game texture to the drawable texture on every present().
 *
 * MTKView integration: For a real integration the view would be an MTKView
 * subclass that owns the Metal device, command queue, and render pass
 * descriptor.  This stub shows the minimal object graph without the AppKit/UIKit
 * plumbing.
 */
#include "backend/backend.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Forward-declare Metal types so we can use them without Objective-C headers
 * in pure-C function signatures. */
#ifdef __OBJC__
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#else
typedef void * id;
typedef void * MTLDevice;
typedef void * MTLCommandQueue;
typedef void * MTLTexture;
typedef void * MTLCommandBuffer;
typedef void * MTLRenderPassDescriptor;
#endif

typedef struct metal_video_backend {
    const char *name;
    bool initialized;

    /* Metal objects (void* so this .c file compiles in non-ObjC translation) */
    MTLDevice        device;
    MTLCommandQueue  command_queue;

    /* Game frame texture (512×240) */
    MTLTexture       game_texture;
    uint32_t         game_w;
    uint32_t         game_h;

    /* Window / drawable dimensions */
    uint32_t         win_w;
    uint32_t         win_h;

    /* Upload staging buffer (system RAM) */
    uint8_t         *staging_buf;
    size_t           staging_capacity;
} metal_video_backend_t;

/* ---- Backend v-table ---- */
static bool metal_video_create(void *user, void *context)
{
    metal_video_backend_t *b = (metal_video_backend_t *)user;
    (void)b; (void)context;
#ifdef __OBJC__
    /* TODO_Metal: acquire the NSWindow/UIView pointer, create a CAMetalLayer,
     * configure it with device pixel format, and attach it to the view.
     * For now, the Metal device is lazily created in init(). */
    b->device = nil;
    b->command_queue = nil;
    b->game_texture = nil;
    b->staging_buf = NULL;
    b->staging_capacity = 0;
#endif
    return true;
}

static bool metal_video_init(void *user,
                              uint32_t game_w, uint32_t game_h,
                              uint32_t win_w, uint32_t win_h)
{
    metal_video_backend_t *b = (metal_video_backend_t *)user;
    b->game_w = game_w; b->game_h = game_h;
    b->win_w  = win_w;  b->win_h  = win_h;

#ifdef __OBJC__
    /* TODO_Metal:
     *   1. MTLCreateSystemDefaultDevice() to get the GPU device.
     *   2. [device newCommandQueue] for the command queue.
     *   3. Create MTLTextureDescriptor for the 512×240 game frame:
     *        - pixelFormat = MTLPixelFormatBGRA8Unorm (or R8Unorm if paletted)
     *        - usage = MTLTextureUsageShaderRead
     *        - storageMode = MTLStorageModeShared (CPU-uploaded)
     *   4. Allocate staging_buf for format conversion (RGB888 -> BGRA).
     */
    b->initialized = (b->device != nil && b->command_queue != nil);
#else
    /* In non-ObjC translation units (C-only), we cannot call Metal directly.
     * The Metal framework is only available in Objective-C or Swift.  This
     * stub returns true so the registry still links, but the backend will
     * gracefully no-op at upload/present time. */
    b->initialized = true;
#endif
    return b->initialized;
}

static void metal_video_shutdown(void *user)
{
    metal_video_backend_t *b = (metal_video_backend_t *)user;
#ifdef __OBJC__
    if (b->game_texture) {
        [b->game_texture release];
        b->game_texture = nil;
    }
    if (b->command_queue) {
        [b->command_queue release];
        b->command_queue = nil;
    }
    if (b->device) {
        [b->device release];
        b->device = nil;
    }
#endif
    free(b->staging_buf);
    b->staging_buf = NULL;
    b->staging_capacity = 0;
    b->initialized = false;
}

static void metal_video_destroy(void *user)
{
    (void)user;
}

static int metal_video_upload(void *user,
                               const stunrun_video_frame_t *frame)
{
    metal_video_backend_t *b = (metal_video_backend_t *)user;
    if (!b->initialized || !frame || !frame->pixels)
        return 0;

#ifdef __OBJC__
    /* TODO_Metal:
     *   1. If paletted: expand palette to BGRA in staging_buf.
     *   2. If RGB565: convert to BGRA in staging_buf.
     *   3. Replace MTLTexture contents via:
     *        MTLRegion region = MTLRegionMake2D(0, 0, game_w, game_h);
     *        [game_texture replaceRegion:region mipmapLevel:0
     *                           withBytes:staging_buf
     *                         bytesPerRow:game_w * 4];
     */
    (void)frame;
#else
    /* C-only: silently discard, or implement via a helper C-callable shim. */
#endif
    return 1;
}

static int metal_video_set_palette(void *user,
                                    const stunrun_video_palette_t *pal)
{
    (void)user; (void)pal;
    return 1;
}

static void metal_video_present(void *user)
{
    metal_video_backend_t *b = (metal_video_backend_t *)user;
    if (!b->initialized)
        return;
#ifdef __OBJC__
    /* TODO_Metal:
     *   1. Acquire next drawable (CAMetalLayer nextDrawable).
     *   2. Build a MTLBlitCommandEncoder:
     *        [encoder copyFromTexture:game_texture
     *                    sourceSlice:0 sourceLevel:0
     *                sourceOrigin:MTLOriginMake(0,0,0)
     *                   sourceSize:MTLSizeMake(game_w, game_h, 1)
     *                    toTexture:drawable.texture
     *             destinationSlice:0 destinationLevel:0
     *            destinationOrigin:MTLOriginMake(0,0,0)];
     *   3. Commit and present. */
#else
    (void)b;
#endif
}

static void metal_video_on_resize(void *user, uint32_t win_w, uint32_t win_h)
{
    metal_video_backend_t *b = (metal_video_backend_t *)user;
    b->win_w = win_w; b->win_h = win_h;
    /* TODO_Metal: update CAMetalLayer.drawableSize */
}

/* ---- Registration ---- */
static stunrun_video_backend_t the_backend = {
    .name        = "metal",
    .create      = metal_video_create,
    .init        = metal_video_init,
    .shutdown    = metal_video_shutdown,
    .destroy     = metal_video_destroy,
    .upload      = metal_video_upload,
    .set_palette = metal_video_set_palette,
    .present     = metal_video_present,
    .on_resize   = metal_video_on_resize,
    .user        = NULL,
};

const stunrun_video_backend_t *stunrun_video_backend_metal(void)
{
    return &the_backend;
}

const stunrun_video_backend_t *stunrun_video_backend_metal_register(void)
{
    extern void stunrun_backend_register_video(const stunrun_video_backend_t *);
    stunrun_backend_register_video(&the_backend);
    return &the_backend;
}
static const stunrun_video_backend_t *
    (* const reg_fn)(void) = stunrun_video_backend_metal_register;
