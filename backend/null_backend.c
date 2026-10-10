/* Null / headless backend for video and audio.
 *
 * This backend accepts all upload/stream operations and silently discards
 * them.  It is used for:
 *   - Running the core in headless mode (e.g. in CI, or for checkpoint
 *     verification where no display is needed).
 *   - As a reference when implementing a new backend.
 */
#include "backend/backend.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Null video backend ---- */
typedef struct null_video_backend {
    const char *name;
    uint32_t game_w, game_h;
    uint32_t win_w, win_h;
    bool initialized;
} null_video_backend_t;

static bool null_video_create(void *user, void *context)
{
    (void)user; (void)context;
    return true;
}

static bool null_video_init(void *user, uint32_t game_w, uint32_t game_h,
                           uint32_t win_w, uint32_t win_h)
{
    null_video_backend_t *b = (null_video_backend_t *)user;
    b->game_w = game_w; b->game_h = game_h;
    b->win_w  = win_w;  b->win_h  = win_h;
    b->initialized = true;
    return true;
}

static void null_video_shutdown(void *user)
{
    null_video_backend_t *b = (null_video_backend_t *)user;
    b->initialized = false;
}

static void null_video_destroy(void *user)
{
    (void)user;
}

static int null_video_upload(void *user, const stunrun_video_frame_t *frame)
{
    (void)user;
    if (!frame) return 0;
    /* Silently discard pixel data.  In debug builds, count bytes dropped. */
#if defined(DEBUG_BACKEND)
    static size_t total_bytes;
    size_t bytes = (size_t)frame->height * (size_t)frame->stride;
    total_bytes += bytes;
    fprintf(stderr, "null_video: dropped %zu bytes (total: %zu)\n", bytes, total_bytes);
#endif
    return 1;
}

static int null_video_set_palette(void *user,
                                   const stunrun_video_palette_t *pal)
{
    (void)user; (void)pal;
    return 1;
}

static void null_video_present(void *user)
{
    (void)user;
    /* Nothing to present in headless mode */
}

static void null_video_on_resize(void *user, uint32_t win_w, uint32_t win_h)
{
    null_video_backend_t *b = (null_video_backend_t *)user;
    b->win_w = win_w; b->win_h = win_h;
}

static stunrun_video_backend_t the_video_backend = {
    .name        = "null",
    .create      = null_video_create,
    .init        = null_video_init,
    .shutdown    = null_video_shutdown,
    .destroy     = null_video_destroy,
    .upload      = null_video_upload,
    .set_palette = null_video_set_palette,
    .present     = null_video_present,
    .on_resize   = null_video_on_resize,
    .user        = NULL,
};

const stunrun_video_backend_t *stunrun_video_backend_null(void)
{
    return &the_video_backend;
}

const stunrun_video_backend_t *stunrun_video_backend_null_register(void)
{
    extern void stunrun_backend_register_video(const stunrun_video_backend_t *);
    stunrun_backend_register_video(&the_video_backend);
    return &the_video_backend;
}

/* ---- Null audio backend ---- */
typedef struct null_audio_backend {
    const char *name;
    uint32_t sample_rate;
    uint8_t channels;
    float gain;
    bool mute;
    size_t samples_written;
    bool initialized;
} null_audio_backend_t;

static bool null_audio_create(void *user)
{
    null_audio_backend_t *b = (null_audio_backend_t *)user;
    b->gain = 1.0f;
    b->mute = false;
    b->samples_written = 0;
    b->initialized = false;
    return true;
}

static bool null_audio_init(void *user, uint32_t default_sample_rate)
{
    null_audio_backend_t *b = (null_audio_backend_t *)user;
    b->sample_rate = default_sample_rate;
    b->channels    = 2;
    b->initialized = true;
    return true;
}

static void null_audio_shutdown(void *user)
{
    null_audio_backend_t *b = (null_audio_backend_t *)user;
    b->initialized = false;
}

static void null_audio_destroy(void *user)
{
    (void)user;
}

static size_t null_audio_stream_write(void *user,
                                      const stunrun_audio_buffer_t *buf)
{
    null_audio_backend_t *b = (null_audio_backend_t *)user;
    if (!buf || !buf->samples)
        return 0;
    size_t n = buf->sample_count;
#if defined(DEBUG_BACKEND)
    fprintf(stderr, "null_audio: dropped %zu stereo samples (total: %zu)\n",
            n, b->samples_written + n);
#endif
    b->samples_written += n;
    return n;
}

static size_t null_audio_stream_adpcm(void *user,
                                       const stunrun_audio_adpcm_t *adpcm)
{
    null_audio_backend_t *b = (null_audio_backend_t *)user;
    if (!adpcm || !adpcm->nibbles)
        return 0;
    size_t n = adpcm->nibble_count;
    (void)b;
#if defined(DEBUG_BACKEND)
    fprintf(stderr, "null_audio: dropped %zu ADPCM samples\n", n);
#endif
    return n;
}

static void null_audio_drain(void *user)
{
    (void)user;
}

static void null_audio_set_gain(void *user, float gain)
{
    null_audio_backend_t *b = (null_audio_backend_t *)user;
    b->gain = (gain < 0.0f) ? 0.0f : (gain > 1.0f) ? 1.0f : gain;
}

static void null_audio_set_master_volume(void *user,
                                          uint8_t stream_id, float volume)
{
    (void)user; (void)stream_id; (void)volume;
}

static void null_audio_send_command(void *user, uint8_t command_byte)
{
    (void)user; (void)command_byte;
}

static float null_audio_get_latency_ms(void *user)
{
    (void)user;
    return 0.0f;
}

static float null_audio_get_load(void *user)
{
    (void)user;
    return 0.0f;
}

static stunrun_audio_backend_t the_audio_backend = {
    .name              = "null",
    .create            = null_audio_create,
    .init              = null_audio_init,
    .shutdown          = null_audio_shutdown,
    .destroy           = null_audio_destroy,
    .stream_write      = null_audio_stream_write,
    .stream_adpcm      = null_audio_stream_adpcm,
    .drain             = null_audio_drain,
    .set_gain          = null_audio_set_gain,
    .set_master_volume = null_audio_set_master_volume,
    .send_command      = null_audio_send_command,
    .get_latency_ms    = null_audio_get_latency_ms,
    .get_load          = null_audio_get_load,
    .user              = NULL,
};

const stunrun_audio_backend_t *stunrun_audio_backend_null(void)
{
    return &the_audio_backend;
}

const stunrun_audio_backend_t *stunrun_audio_backend_null_register(void)
{
    extern void stunrun_backend_register_audio(const stunrun_audio_backend_t *);
    stunrun_backend_register_audio(&the_audio_backend);
    return &the_audio_backend;
}

/* Force registration to run before main().
 *
 * A static function POINTER only forces the linker to keep the symbol; it does
 * not call it, so the registry stayed empty and every backend looked "not
 * found".  A constructor attribute actually invokes the register function. */
static void __attribute__((constructor))
stunrun_backend_auto_register(void)
{
    (void)stunrun_video_backend_null_register();
    (void)stunrun_audio_backend_null_register();
}
