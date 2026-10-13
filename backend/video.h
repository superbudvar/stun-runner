/* Cross-platform video backend interface.
 *
 * Each installed backend implements this interface. The native core never
 * calls GPU APIs directly; it always goes through one of these function
 * pointers.
 *
 * Lifecycle:
 *   create  -> init -> [upload_texture | set_palette | blitFramebuffer]* -> present -> shutdown -> destroy
 *
 * Backends are selected at link time (static linkage) or runtime (dynamic
 * loader).  See backend.h for the selection API.
 *
 * Coordinate system: (0,0) is top-left.  All coordinates are in
 * physical window pixels, not the internal game resolution.
 */
#ifndef STUNRUN_BACKEND_VIDEO_H
#define STUNRUN_BACKEND_VIDEO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Semantic pixel formats the core may produce. */
typedef enum stunrun_video_format {
    STUNRUN_VIDEO_FORMAT_PALETTED_2BPP,   /* 2-bit index into palette */
    STUNRUN_VIDEO_FORMAT_PALETTED_4BPP,   /* 4-bit index into palette */
    STUNRUN_VIDEO_FORMAT_PALETTED_8BPP,   /* 8-bit index into palette */
    STUNRUN_VIDEO_FORMAT_RGB565,           /* 16-bit RGB */
    STUNRUN_VIDEO_FORMAT_RGB888,           /* 24-bit RGB, stride == width*3 */
    STUNRUN_VIDEO_FORMAT_RGBA8888,          /* 32-bit RGBA */
} stunrun_video_format_t;

/* Layout of one video frame produced by the core. */
typedef struct stunrun_video_frame {
    const uint8_t *pixels;        /* may be NULL (blank frame) */
    uint32_t width;               /* game resolution width  */
    uint32_t height;              /* game resolution height */
    uint32_t stride;              /* bytes per row (pixels->stride gives row N) */
    stunrun_video_format_t format;
    uint32_t frame_index;         /* monotonic, wraps at UINT32_MAX */
    double   timestamp_seconds;    /* wall-clock time when frame was produced */
} stunrun_video_frame_t;

/* Palette descriptor.  The backend receives raw palette entries and is
 * responsible for uploading them to the GPU in whatever GPU-side format
 * the active shader/texture needs. */
typedef struct stunrun_video_palette {
    const uint8_t *entries;   /* flat RGB triplets, 256 entries × 3 bytes */
    uint32_t count;           /* must be 256 for PALETTED formats */
} stunrun_video_palette_t;

/* Optional: direct OpenGL texture upload path.
 * When backend provides this, the core can pass GPU-uploadable texture
 * data directly without an extra CPU-side copy.  Set to NULL if not
 * supported by this backend. */
typedef int (*stunrun_video_uploadTex_fn)(
    void *user,
    uint32_t width, uint32_t height,
    stunrun_video_format_t format,
    const uint8_t *data, size_t data_size);

typedef struct stunrun_video_backend {
    const char *name;  /* e.g. "sdl_opengl" or "metal" — for diagnostics */

    /* Lifecycle.  All return true on success. */
    bool (*create)(void *user, void *context);   /* context = SDL_Window*, CA::MetalLayer*, etc. */
    bool (*init)(void *user, uint32_t game_width, uint32_t game_height,
                 uint32_t window_width, uint32_t window_height);
    void (*shutdown)(void *user);
    void (*destroy)(void *user);

    /* Frame pipeline.
     *
     * present() is called once per "vsync tick".  The backend is free to
     * block or use a compositor.  The pixels pointer remains valid until
     * after present() returns or until the next upload() call, whichever
     * comes later.
     *
     * If the backend owns a swap chain / double buffer, present() must
     * ensure the previous frame stays available until the next vsync.
     */
    int  (*upload)(void *user, const stunrun_video_frame_t *frame);
    int  (*set_palette)(void *user, const stunrun_video_palette_t *palette);
    void (*present)(void *user);

    /* Window / resize events (sent by app-level event loop). */
    void (*on_resize)(void *user, uint32_t window_width, uint32_t window_height);

    /* Optional direct-to-GPU path.  Set to NULL if not implemented. */
    stunrun_video_uploadTex_fn upload_tex;

    void *user;   /* passed as first arg to every function above */
    size_t user_size; /* zero keeps the registry's small default allocation */
} stunrun_video_backend_t;

/* Helper for implementations: initialise a frame descriptor for a flat
 * RGB888 buffer sitting in a contiguous flat uint8_t array. */
static inline void
stunrun_video_frame_init_rgb888(stunrun_video_frame_t *out,
                                const uint8_t *pixels,
                                uint32_t w, uint32_t h,
                                uint32_t frame_index)
{
    out->pixels       = pixels;
    out->width        = w;
    out->height       = h;
    out->stride       = w * 3;  /* RGB888: 3 bytes per pixel */
    out->format       = STUNRUN_VIDEO_FORMAT_RGB888;
    out->frame_index  = frame_index;
    out->timestamp_seconds = 0.0;
}

/* Same helper for RGBA8888 buffers. */
static inline void
stunrun_video_frame_init_rgba8888(stunrun_video_frame_t *out,
                                  const uint8_t *pixels,
                                  uint32_t w, uint32_t h,
                                  uint32_t frame_index)
{
    out->pixels       = pixels;
    out->width        = w;
    out->height       = h;
    out->stride       = w * 4;
    out->format       = STUNRUN_VIDEO_FORMAT_RGBA8888;
    out->frame_index  = frame_index;
    out->timestamp_seconds = 0.0;
}

/* Same helper for paletted 8bpp buffers. */
static inline void
stunrun_video_frame_init_paletted_8bpp(stunrun_video_frame_t *out,
                                        const uint8_t *pixels,
                                        uint32_t w, uint32_t h,
                                        uint32_t frame_index)
{
    out->pixels       = pixels;
    out->width        = w;
    out->height       = h;
    out->stride       = w;      /* 1 byte per pixel = palette index */
    out->format       = STUNRUN_VIDEO_FORMAT_PALETTED_8BPP;
    out->frame_index  = frame_index;
    out->timestamp_seconds = 0.0;
}

/* Generic frame initialiser — caller specifies format explicitly. */
static inline void
stunrun_video_frame_init(stunrun_video_frame_t *out,
                         uint32_t w, uint32_t h,
                         uint32_t stride,
                         stunrun_video_format_t fmt,
                         const uint8_t *pixels)
{
    out->pixels       = pixels;
    out->width        = w;
    out->height       = h;
    out->stride       = stride;
    out->format       = fmt;
    out->frame_index  = 0;
    out->timestamp_seconds = 0.0;
}

/* Palette initialiser. */
static inline void
stunrun_video_palette_init(stunrun_video_palette_t *out,
                           uint32_t count,
                           const uint8_t *entries)
{
    out->entries = entries;
    out->count  = count;
}

#ifdef __cplusplus
}
#endif

#endif /* STUNRUN_BACKEND_VIDEO_H */
