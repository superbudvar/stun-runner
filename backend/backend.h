/* Backend registry and runtime selection.
 *
 * The native core links against exactly one "stunrun" static library.  The
 * final executable selects backends at startup by name (e.g. from a config
 * file or command-line flag).  Backends are registered via the
 * STUNRUN_REGISTER_VIDEO_BACKEND / STUNRUN_REGISTER_AUDIO_BACKEND macros
 * in their respective .c files.
 *
 * Registration happens at program startup through the C++ "static
 * initialization order" fiasco workaround: each backend .c file contains a
 * static struct that registers itself at program start, before main().
 *
 * Dynamic loading (dlopen) is also supported for plugin backends.
 */
#ifndef STUNRUN_BACKEND_BACKEND_H
#define STUNRUN_BACKEND_BACKEND_H

#include "video.h"
#include "audio.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*--- Backend registry -----------------------------------------------*/

typedef struct stunrun_backend_registry_entry {
    const char *name;
    void *dl_handle;   /* NULL for statically linked, set for dynamically loaded */
    union {
        const stunrun_video_backend_t *video;
        const stunrun_audio_backend_t *audio;
        void *opaque;
    } iface;
    struct stunrun_backend_registry_entry *next;
} stunrun_backend_registry_entry_t;

/* Returns a NULL-terminated list of all registered video backends.
 * The list is owned by the registry; do not free or modify it. */
const stunrun_backend_registry_entry_t *
stunrun_video_backends_list(void);

/* Returns a NULL-terminated list of all registered audio backends. */
const stunrun_backend_registry_entry_t *
stunrun_audio_backends_list(void);

/* Find a video backend by name.  Returns NULL if not found. */
const stunrun_video_backend_t *
stunrun_video_backend_find(const char *name);

/* Find an audio backend by name.  Returns NULL if not found. */
const stunrun_audio_backend_t *
stunrun_audio_backend_find(const char *name);

/* Dynamically load a backend plugin (.so / .dylib / .dll).
 * The plugin must export a symbol named "stunrun_<type>_backend" returning
 * the appropriate backend struct pointer (video_ or audio_). */
void *stunrun_backend_load_plugin(const char *path, const char *type,
                                  char *error_buf, size_t error_buf_size);

/* Auto-detection: return the best available video backend for this platform.
 * Order of preference:
 *   1. Environment variable STUNRUN_VIDEO_BACKEND
 *   2. Apple platforms → "metal"
 *   3. Android         → "vulkan" (or "opengles" if Vulkan unavailable)
 *   4. WASM / Emscripten → "webgl"
 *   5. Raspberry Pi    → "opengles" then "sdl_opengl"
 *   6. Linux / macOS / Windows → "sdl_opengl"
 *   7. Fallback        → "null" (headless)
 */
const stunrun_video_backend_t *stunrun_video_backend_autoselect(void);

/* Same auto-detection for audio.
 * Order:
 *   1. Environment variable STUNRUN_AUDIO_BACKEND
 *   2. All platforms → "sdl_audio" (uses SDL2 audio API, works everywhere)
 *   3. Fallback      → "null" (silent, no-op)
 */
const stunrun_audio_backend_t *stunrun_audio_backend_autoselect(void);

/*--- Registration macros -------------------------------------------*/

/* Use these in exactly ONE .c file per backend implementation:
 *
 *   STUNRUN_REGISTER_VIDEO_BACKEND(my_video_backend);
 *   STUNRUN_REGISTER_AUDIO_BACKEND(my_audio_backend);
 *
 * The backend struct must be named stunrun_<type>_backend_<name>
 * and must be declared extern in a shared header.
 */
#define STUNRUN_REGISTER_VIDEO_BACKEND(backend_ptr) \
    extern const stunrun_video_backend_t *stunrun_video_backend_register__(void) \
        __attribute__((alias(#backend_ptr "_register")))

#define STUNRUN_REGISTER_AUDIO_BACKEND(backend_ptr) \
    extern const stunrun_audio_backend_t *stunrun_audio_backend_register__(void) \
        __attribute__((alias(#backend_ptr "_register")))

/*--- Convenience: init both backends from environment or autoselect -----*/

typedef struct stunrun_backend_pair {
    stunrun_video_backend_t  video;
    stunrun_audio_backend_t  audio;
    void                    *video_user;
    void                    *audio_user;
} stunrun_backend_pair_t;

/* Allocate and initialise both backends for a window + audio device.
 * Returns true if both succeeded.
 *
 * The video backend is selected via STUNRUN_VIDEO_BACKEND env var or
 * stunrun_video_backend_autoselect().  Pass a valid SDL_Window* in
 * window_ctx (or NULL for headless).
 *
 * The audio backend is selected via STUNRUN_AUDIO_BACKEND env var or
 * stunrun_audio_backend_autoselect().
 *
 * On failure, any partially-initialised backends are shut down before
 * returning.
 */
bool stunrun_backend_pair_init(stunrun_backend_pair_t *pair,
                               void *window_ctx,
                               uint32_t game_w, uint32_t game_h,
                               uint32_t window_w, uint32_t window_h,
                               uint32_t audio_sample_rate);

void stunrun_backend_pair_shutdown(stunrun_backend_pair_t *pair);
void stunrun_backend_pair_destroy(stunrun_backend_pair_t *pair);

#ifdef __cplusplus
}
#endif

#endif /* STUNRUN_BACKEND_BACKEND_H */
