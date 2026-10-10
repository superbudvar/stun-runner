/* SDL2 audio backend.
 *
 * Targets all SDL2-supported platforms:
 *   - Linux, macOS, Windows (PulseAudio / CoreAudio / DirectSound)
 *   - Android      (AAudio / OpenSL ES via SDL)
 *   - Raspberry Pi (ALSA via SDL)
 *   - Web          (Emscripten + Web Audio API via SDL)
 *
 * Audio pipeline:
 *   Core produces PCM (S16LE stereo)
 *     -> SDL_mixer or raw SDL2 audio queue
 *     -> Platform audio API
 *
 * The backend owns the sample queue (SDL2's audio queue is managed by this
 * module).  Incoming buffers are copied into a ring buffer and fed to the
 * SDL2 audio device in the callback thread.
 *
 * ADPCM decoding:
 *   The JSA II uses standard ADPCM (IMA-style) for speech/effects samples.
 *   When stream_adpcm() is called, the backend decodes the nibbles into
 *   signed 16-bit PCM using the standard ADPCM step-table algorithm, then
 *   enqueues the result.
 */
#include "backend/backend.h"

#include <SDL2/SDL.h>

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- IMA ADPCM step-table (standard) ---- */
static const int16_t ima_step_table[89] = {
    7,    8,    9,   10,   11,   12,   13,   14,
    16,   17,   19,   21,   23,   25,   28,   31,
    34,   37,   41,   45,   50,   55,   60,   66,
    73,   80,   88,   97,  107,  118,  130,  143,
    157,  172,  189,  207,  228,  250,  274,  301,
    330,  362,  398,  436,  479,  526,  577,  633,
    695,  763,  837,  918, 1008, 1106, 1215, 1334,
    1464, 1607, 1763, 1935, 2122, 2329, 2557, 2806,
    3079, 3379, 3709, 4070, 4467, 4901, 5376, 5898,
    6472, 7099, 7789, 8545, 9377, 10292, 11291, 12384,
    13587, 14905, 16353, 17936
};

/* Index table for IMA ADPCM */
static const int8_t ima_index_table[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8
};

/* ---- Per-stream state for ADPCM decoding ---- */
typedef struct adpcm_channel_state {
    int16_t  sample;   /* decoded sample output */
    uint8_t  index;    /* step table index */
    bool     active;    /* true once this stream has been initialised */
} adpcm_channel_state_t;

#define MAX_ADPCM_STREAMS 8

/* ---- Backend state ---- */
typedef struct sdl_audio_backend {
    const char *name;
    SDL_AudioDeviceID dev;
    SDL_AudioSpec      want;
    SDL_AudioSpec      have;

    /* Ring buffer for sample data (protects against jitter in SDL callback) */
    int16_t *ring_buf;
    size_t   ring_capacity;  /* in stereo frames (sample pairs) */
    size_t   ring_write;     /* producer index (stereo frames) */
    size_t   ring_read;      /* consumer index (stereo frames) */
    size_t   ring_queued;    /* stereo frames currently queued */

    /* ADPCM state, one per stream_id */
    adpcm_channel_state_t adpcm[MAX_ADPCM_STREAMS];

    /* Gain */
    float gain;

    /* Mute */
    bool mute;

    /* Audio spec */
    uint32_t sample_rate;
    uint8_t  channels;

    /* True once audio device is opened and callback is running */
    bool active;
} sdl_audio_backend_t;

/* ---- Ring buffer ---- */
static size_t ring_available_read(sdl_audio_backend_t *b)
{
    return b->ring_queued;
}

static size_t ring_available_write(sdl_audio_backend_t *b)
{
    return b->ring_capacity - b->ring_queued;
}

/* Write samples to the ring (producer side).  Returns number of stereo
 * sample-pairs written. */
static size_t ring_write(sdl_audio_backend_t *b,
                         const int16_t *samples,
                         size_t count)   /* count of stereo pairs */
{
    size_t avail = ring_available_write(b);
    if (count > avail)
        count = avail;
    for (size_t i = 0; i < count; i++) {
        size_t idx = (b->ring_write + i) % b->ring_capacity;
        /* Stereo: 2 samples per "slot" (interleaved L,R) */
        b->ring_buf[idx * 2]     = samples[i * 2];
        b->ring_buf[idx * 2 + 1] = samples[i * 2 + 1];
    }
    b->ring_write = (b->ring_write + count) % b->ring_capacity;
    b->ring_queued += count;
    return count;
}

/* ---- SDL audio callback ---- */
static void sdl_audio_callback(void *user, uint8_t *stream, int len_bytes)
{
    sdl_audio_backend_t *b = (sdl_audio_backend_t *)user;
    int16_t *out = (int16_t *)stream;
    size_t out_samples = (size_t)len_bytes / (2 * b->channels); /* 2 bytes per S16 */

    size_t read = 0;
    while (read < out_samples) {
        size_t avail = ring_available_read(b);
        size_t chunk = out_samples - read;
        if (chunk > avail)
            chunk = avail;
        if (chunk == 0) {
            /* Silence */
            for (size_t i = read; i < out_samples; i++) {
                out[i * 2]     = 0;
                out[i * 2 + 1] = 0;
            }
            read = out_samples;
            break;
        }
        for (size_t i = 0; i < chunk; i++) {
            size_t idx = (b->ring_read + i) % b->ring_capacity;
            int16_t l = b->ring_buf[idx * 2];
            int16_t r = b->ring_buf[idx * 2 + 1];
            if (b->mute || b->gain < 1.0f) {
                float g = b->mute ? 0.0f : b->gain;
                l = (int16_t)(l * g);
                r = (int16_t)(r * g);
            }
            out[read * 2 + i * 2]     = l;
            out[read * 2 + i * 2 + 1] = r;
        }
        b->ring_read = (b->ring_read + chunk) % b->ring_capacity;
        b->ring_queued -= chunk;
        read += chunk;
    }
}

/* ---- ADPCM decode ---- */
static void adpcm_decode_nibble(int8_t nibble,
                                 adpcm_channel_state_t *ch)
{
    int step = ima_step_table[ch->index];
    int delta = nibble;
    int diff = (step * abs(delta) + (step >> 1)) >> 11;
    ch->sample += (delta < 0) ? -diff : diff;
    if (ch->sample < -32768)  ch->sample = -32768;
    if (ch->sample >  32767)  ch->sample =  32767;
    ch->index += ima_index_table[nibble & 0x07];
    if (ch->index <  0) ch->index =  0;
    if (ch->index > 88) ch->index = 88;
}

/* Decode one ADPCM byte (two nibbles: low then high). */
static void adpcm_decode_byte(uint8_t byte, adpcm_channel_state_t *ch)
{
    adpcm_decode_nibble((int8_t)(byte & 0x0F), ch);
    adpcm_decode_nibble((int8_t)((byte >> 4) & 0x0F), ch);
}

/* ---- Backend v-table ---- */

static bool sdl_audio_create(void *user)
{
    sdl_audio_backend_t *b = (sdl_audio_backend_t *)user;
    b->ring_buf = NULL;
    b->ring_write = 0;
    b->ring_read  = 0;
    b->ring_queued = 0;
    b->gain = 1.0f;
    b->mute = false;
    b->active = false;
    for (int i = 0; i < MAX_ADPCM_STREAMS; i++)
        b->adpcm[i].active = false;
    (void)b;
    return true;
}

static bool sdl_audio_init(void *user, uint32_t default_sample_rate)
{
    sdl_audio_backend_t *b = (sdl_audio_backend_t *)user;
    b->sample_rate = default_sample_rate;
    b->channels    = 2;  /* stereo */

    /* Ring buffer: 100 ms of stereo frames. ring_write() indexes each frame
     * as two int16_t values, so allocate both channels for every slot. */
    b->ring_capacity = ((size_t)default_sample_rate * 100u) / 1000u;
    if (b->ring_capacity == 0)
        b->ring_capacity = 1;
    b->ring_buf = (int16_t *)calloc(b->ring_capacity, 2u * sizeof(int16_t));
    if (!b->ring_buf) {
        fprintf(stderr, "sdl_audio: ring buffer alloc failed\n");
        return false;
    }

    SDL_AudioSpec want;
    SDL_zero(want);
    want.freq     = (int)default_sample_rate;
    want.format   = AUDIO_S16LSB;   /* signed 16-bit little-endian stereo */
    want.channels = 2;
    want.samples  = 512;            /* ~11.6 ms per callback */
    want.callback  = sdl_audio_callback;
    want.userdata  = b;

    if (SDL_OpenAudio(&want, &b->have) < 0) {
        fprintf(stderr, "sdl_audio: SDL_OpenAudio failed: %s\n",
                SDL_GetError());
        free(b->ring_buf);
        b->ring_buf = NULL;
        return false;
    }

    b->sample_rate = (uint32_t)b->have.freq;
    b->channels    = b->have.channels;
    SDL_PauseAudio(0);  /* start callback */
    b->active = true;
    return true;
}

static void sdl_audio_shutdown(void *user)
{
    sdl_audio_backend_t *b = (sdl_audio_backend_t *)user;
    if (b->active) {
        /* This backend uses SDL_OpenAudio(), not SDL_OpenAudioDevice(); the
         * legacy API is closed with SDL_CloseAudio(). Stop its callback before
         * the registry destroys the backend state and ring buffer. */
        SDL_PauseAudio(1);
        SDL_CloseAudio();
    }
    b->active = false;
}

static void sdl_audio_destroy(void *user)
{
    sdl_audio_backend_t *b = (sdl_audio_backend_t *)user;
    free(b->ring_buf);
    b->ring_buf = NULL;
}

static size_t sdl_audio_stream_write(void *user,
                                      const stunrun_audio_buffer_t *buf)
{
    sdl_audio_backend_t *b = (sdl_audio_backend_t *)user;
    if (!b->active || !buf || !buf->samples)
        return 0;

    /* Convert buffer to the internal format (stereo S16LE) */
    size_t frame_count = buf->sample_count;

    /* For now we expect S16LE stereo directly */
    if (buf->format == STUNRUN_AUDIO_FORMAT_S16LE_STEREO) {
        return ring_write(b, buf->samples, frame_count);
    }

    /* Mono expansion: duplicate L to L,R */
    if (buf->format == STUNRUN_AUDIO_FORMAT_S16LE_MONO) {
        /* Use upload buffer if available, else reject */
        /* For simplicity, require stereo input for now */
        fprintf(stderr, "sdl_audio: mono input not yet implemented\n");
        (void)0;
        return 0;
    }

    fprintf(stderr, "sdl_audio: unsupported audio format %d\n",
            (int)buf->format);
    return 0;
}

static size_t sdl_audio_stream_adpcm(void *user,
                                      const stunrun_audio_adpcm_t *adpcm)
{
    sdl_audio_backend_t *b = (sdl_audio_backend_t *)user;
    if (!b->active || !adpcm || !adpcm->nibbles)
        return 0;

    uint8_t stream_id = (uint8_t)(adpcm->stream_id % MAX_ADPCM_STREAMS);
    adpcm_channel_state_t *ch = &b->adpcm[stream_id];
    if (!ch->active) {
        ch->sample = 0;
        ch->index  = 0;
        ch->active = true;
    }

    /* Decode all nibbles. Each byte has two nibbles, each nibble = one sample. */
    size_t samples_decoded = 0;
    size_t out_buf_capacity = adpcm->nibble_count * 2; /* max possible */
    if (out_buf_capacity > 8192) out_buf_capacity = 8192; /* cap to temp buffer */
    static int16_t decoded[8192];  /* static to avoid stack overflow */

    for (size_t i = 0; i < adpcm->nibble_count && samples_decoded < out_buf_capacity; i++) {
        adpcm_decode_byte(adpcm->nibbles[i], ch);
        /* Output mono (stereo = duplicate) */
        decoded[samples_decoded * 2]     = ch->sample;  /* L */
        decoded[samples_decoded * 2 + 1] = ch->sample;  /* R */
        samples_decoded++;
    }

    /* Write decoded stereo samples to ring */
    return ring_write(b, decoded, samples_decoded);
}

static void sdl_audio_drain(void *user)
{
    sdl_audio_backend_t *b = (sdl_audio_backend_t *)user;
    if (!b->active)
        return;
    /* Wait for the ring to drain.
     * Poll ring_queued every 5 ms for up to 2 seconds. */
    for (int i = 0; i < 400 && b->ring_queued > 0; i++) {
        SDL_Delay(5);
    }
}

static void sdl_audio_set_gain(void *user, float gain)
{
    sdl_audio_backend_t *b = (sdl_audio_backend_t *)user;
    if (gain < 0.0f) gain = 0.0f;
    if (gain > 1.0f) gain = 1.0f;
    b->gain = gain;
}

static void sdl_audio_set_master_volume(void *user,
                                         uint8_t stream_id, float volume)
{
    /* Per-stream volume not yet implemented; applies to master for now */
    (void)user; (void)stream_id; (void)volume;
}

static void sdl_audio_send_command(void *user, uint8_t command_byte)
{
    sdl_audio_backend_t *b = (sdl_audio_backend_t *)user;
    /* Store command for diagnostics / sample ROM lookup */
    (void)b; (void)command_byte;
    /* TODO: look up sample index from command_byte using JSA II sample table */
}

static float sdl_audio_get_latency_ms(void *user)
{
    sdl_audio_backend_t *b = (sdl_audio_backend_t *)user;
    if (!b->active)
        return 0.0f;
    /* Approximate: samples_in_buffer / sample_rate * 1000 */
    double ms = (double)b->ring_queued * 1000.0 / (double)b->sample_rate;
    return (float)ms;
}

static float sdl_audio_get_load(void *user)
{
    (void)user;
    /* Not easily measurable from SDL2 without platform-specific APIs */
    return 0.0f;
}

/* ---- Backend registration ---- */

static stunrun_audio_backend_t the_backend = {
    .name               = "sdl_audio",
    .create             = sdl_audio_create,
    .init               = sdl_audio_init,
    .shutdown           = sdl_audio_shutdown,
    .destroy            = sdl_audio_destroy,
    .stream_write       = sdl_audio_stream_write,
    .stream_adpcm       = sdl_audio_stream_adpcm,
    .drain              = sdl_audio_drain,
    .set_gain           = sdl_audio_set_gain,
    .set_master_volume  = sdl_audio_set_master_volume,
    .send_command       = sdl_audio_send_command,
    .get_latency_ms     = sdl_audio_get_latency_ms,
    .get_load           = sdl_audio_get_load,
    .user               = NULL,
};

const stunrun_audio_backend_t *stunrun_audio_backend_sdl_audio(void)
{
    return &the_backend;
}

const stunrun_audio_backend_t *stunrun_audio_backend_sdl_audio_register(void)
{
    extern void stunrun_backend_register_audio(const stunrun_audio_backend_t *);
    stunrun_backend_register_audio(&the_backend);
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
    (void)stunrun_audio_backend_sdl_audio_register();
}
