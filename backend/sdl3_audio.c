/* SDL3 audio backend.
 *
 * Targets all SDL3-supported platforms:
 *   - Linux (PulseAudio / ALSA via SDL3 audio)
 *   - macOS (CoreAudio via SDL3)
 *   - Windows (DirectSound / WASAPI via SDL3)
 *   - Android (AAudio via SDL3)
 *   - Raspberry Pi (ALSA via SDL3)
 *   - Web (Web Audio API via SDL3/Emscripten)
 *
 * Audio pipeline (SDL3 AudioStream API):
 *   Core produces PCM (S16LE stereo or mono)
 *     -> SDL_AudioStream (handles format conversion automatically)
 *     -> SDL audio device (platform audio API)
 *
 * SDL3 replaces SDL2's manual ring-buffer + callback model with SDL_AudioStream,
 * which handles resampling and format conversion in one step. This backend
 * owns the AudioStream and feeds it from the game thread; SDL3's audio
 * thread handles the callback-less playback.
 *
 * ADPCM decoding:
 *   The JSA II uses standard ADPCM (IMA-style) for speech/effects samples.
 *   When stream_adpcm() is called, the backend decodes the nibbles into
 *   signed 16-bit PCM using the standard ADPCM step-table algorithm, then
 *   enqueues the result via SDL_PutAudioStreamData.
 */
#include "backend/backend.h"

#include <SDL3/SDL.h>

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
typedef struct sdl3_audio_backend {
    const char *name;
    SDL_AudioDeviceID dev;         /* physical audio device */
    SDL_AudioStream *stream;       /* SDL3 AudioStream for conversion */
    SDL_AudioSpec   dev_spec;      /* actual device format */
    SDL_AudioSpec   want_spec;     /* format we request */

    /* ADPCM state, one per stream_id */
    adpcm_channel_state_t adpcm[MAX_ADPCM_STREAMS];

    /* Gain */
    float gain;

    /* Mute */
    bool mute;

    /* True once audio device is opened and stream is running */
    bool active;
} sdl3_audio_backend_t;

/* ---- ADPCM decode ---- */
static void adpcm_decode_nibble(int8_t nibble, adpcm_channel_state_t *ch)
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

static void adpcm_decode_byte(uint8_t byte, adpcm_channel_state_t *ch)
{
    adpcm_decode_nibble((int8_t)(byte & 0x0F), ch);
    adpcm_decode_nibble((int8_t)((byte >> 4) & 0x0F), ch);
}

/* ---- Backend v-table ---- */

static bool sdl3_audio_create(void *user)
{
    sdl3_audio_backend_t *b = (sdl3_audio_backend_t *)user;
    b->stream = NULL;
    b->dev = 0;
    b->gain = 1.0f;
    b->mute = false;
    b->active = false;
    for (int i = 0; i < MAX_ADPCM_STREAMS; i++)
        b->adpcm[i].active = false;
    return true;
}

static bool sdl3_audio_init(void *user, uint32_t default_sample_rate)
{
    sdl3_audio_backend_t *b = (sdl3_audio_backend_t *)user;

    SDL_AudioSpec want;
    SDL_zero(want);
    want.freq     = (int)default_sample_rate;
    want.format   = SDL_AUDIO_S16LE;  /* signed 16-bit little-endian stereo */
    want.channels = 2;

    /* SDL3: open default playback device with the desired spec.
     * SDL3 will pick the closest supported format automatically. */
    b->dev = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &want);
    if (b->dev == 0) {
        fprintf(stderr, "sdl3_audio: SDL_OpenAudioDevice failed: %s\n",
                SDL_GetError());
        return false;
    }

    /* Get the actual device format + buffer size */
    int sample_frames = 0;
    if (!SDL_GetAudioDeviceFormat(b->dev, &b->dev_spec, &sample_frames)) {
        fprintf(stderr, "sdl3_audio: SDL_GetAudioDeviceFormat failed: %s\n",
                SDL_GetError());
        SDL_CloseAudioDevice(b->dev);
        b->dev = 0;
        return false;
    }
    (void)sample_frames;  /* used for latency estimation */

    /* Create an AudioStream that converts from our internal format
     * (S16 stereo at game sample rate) to the device format. */
    SDL_AudioSpec src_spec;
    SDL_zero(src_spec);
    src_spec.freq     = (int)default_sample_rate;
    src_spec.format   = SDL_AUDIO_S16LE;
    src_spec.channels = 2;

    b->stream = SDL_CreateAudioStream(&src_spec, &b->dev_spec);
    if (!b->stream) {
        fprintf(stderr, "sdl3_audio: SDL_CreateAudioStream failed: %s\n",
                SDL_GetError());
        SDL_CloseAudioDevice(b->dev);
        b->dev = 0;
        return false;
    }

    /* Bind the stream to the device — SDL3 handles playback from here */
    if (!SDL_BindAudioStream(b->dev, b->stream)) {
        fprintf(stderr, "sdl3_audio: SDL_BindAudioStream failed: %s\n",
                SDL_GetError());
        SDL_DestroyAudioStream(b->stream);
        b->stream = NULL;
        SDL_CloseAudioDevice(b->dev);
        b->dev = 0;
        return false;
    }

    b->active = true;
    return true;
}

static void sdl3_audio_shutdown(void *user)
{
    sdl3_audio_backend_t *b = (sdl3_audio_backend_t *)user;
    if (b->active) {
        if (b->stream) {
            SDL_UnbindAudioStream(b->stream);
            SDL_DestroyAudioStream(b->stream);
            b->stream = NULL;
        }
        if (b->dev != 0) {
            SDL_CloseAudioDevice(b->dev);
            b->dev = 0;
        }
    }
    b->active = false;
}

static void sdl3_audio_destroy(void *user)
{
    (void)user;
    /* All resources freed in shutdown() */
}

static size_t sdl3_audio_stream_write(void *user,
                                       const stunrun_audio_buffer_t *buf)
{
    sdl3_audio_backend_t *b = (sdl3_audio_backend_t *)user;
    if (!b->active || !buf || !buf->samples)
        return 0;

    /* SDL3 AudioStream accepts raw PCM data and handles conversion.
     * We always submit stereo S16LE at the game's sample rate. */
    if (buf->format != STUNRUN_AUDIO_FORMAT_S16LE_STEREO) {
        fprintf(stderr, "sdl3_audio: format %d not yet implemented; use S16LE_STEREO\n",
                (int)buf->format);
        return 0;
    }

    /* Bytes = sample_count * channels * sizeof(int16_t) */
    size_t bytes = buf->sample_count * 2 * sizeof(int16_t);
    if (SDL_PutAudioStreamData(b->stream, buf->samples, (int)(bytes)) < 0) {
        fprintf(stderr, "sdl3_audio: SDL_PutAudioStreamData failed: %s\n",
                SDL_GetError());
        return 0;
    }
    return buf->sample_count;
}

static size_t sdl3_audio_stream_adpcm(void *user,
                                       const stunrun_audio_adpcm_t *adpcm)
{
    sdl3_audio_backend_t *b = (sdl3_audio_backend_t *)user;
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
    size_t max_samples = adpcm->nibble_count;
    if (max_samples > 8192) max_samples = 8192;

    static int16_t decoded[8192];  /* static to avoid stack overflow */

    for (size_t i = 0; i < adpcm->nibble_count && samples_decoded < max_samples; i++) {
        adpcm_decode_byte(adpcm->nibbles[i], ch);
        /* Output mono duplicated to stereo */
        decoded[samples_decoded * 2]     = ch->sample;  /* L */
        decoded[samples_decoded * 2 + 1] = ch->sample;  /* R */
        samples_decoded++;
    }

    /* Write decoded stereo samples to the AudioStream */
    size_t bytes = samples_decoded * 2 * sizeof(int16_t);
    if (SDL_PutAudioStreamData(b->stream, decoded, (int)(bytes)) < 0)
        return 0;
    return samples_decoded;
}

static void sdl3_audio_drain(void *user)
{
    sdl3_audio_backend_t *b = (sdl3_audio_backend_t *)user;
    if (!b->active || !b->stream)
        return;
    /* Flush the stream to ensure all data is sent to the device */
    SDL_FlushAudioStream(b->stream);
}

static void sdl3_audio_set_gain(void *user, float gain)
{
    sdl3_audio_backend_t *b = (sdl3_audio_backend_t *)user;
    if (gain < 0.0f) gain = 0.0f;
    if (gain > 1.0f) gain = 1.0f;
    b->gain = gain;
    /* SDL3 AudioStream doesn't have a built-in gain control;
     * gain would need to be applied to sample data before feeding
     * the stream. For now, store it for diagnostics. */
}

static void sdl3_audio_set_master_volume(void *user,
                                         uint8_t stream_id, float volume)
{
    (void)user; (void)stream_id; (void)volume;
    /* Per-stream volume not yet implemented */
}

static void sdl3_audio_send_command(void *user, uint8_t command_byte)
{
    sdl3_audio_backend_t *b = (sdl3_audio_backend_t *)user;
    (void)b; (void)command_byte;
    /* TODO: look up sample index from command_byte using JSA II sample table */
}

static float sdl3_audio_get_latency_ms(void *user)
{
    sdl3_audio_backend_t *b = (sdl3_audio_backend_t *)user;
    if (!b->active || !b->stream)
        return 0.0f;
    /* Approximate: query device buffer size via SDL_GetAudioDeviceFormat.
     * Latency = (device_buffer_frames / freq) * 1000 */
    SDL_AudioSpec spec;
    int sample_frames = 0;
    if (SDL_GetAudioDeviceFormat(b->dev, &spec, &sample_frames)) {
        if (spec.freq > 0)
            return (float)(sample_frames * 1000 / spec.freq);
    }
    return 0.0f;
}

static float sdl3_audio_get_load(void *user)
{
    (void)user;
    return 0.0f;
}

/* ---- Backend registration ---- */

static stunrun_audio_backend_t the_backend = {
    .name               = "sdl3_audio",
    .create             = sdl3_audio_create,
    .init               = sdl3_audio_init,
    .shutdown           = sdl3_audio_shutdown,
    .destroy            = sdl3_audio_destroy,
    .stream_write       = sdl3_audio_stream_write,
    .stream_adpcm       = sdl3_audio_stream_adpcm,
    .drain              = sdl3_audio_drain,
    .set_gain           = sdl3_audio_set_gain,
    .set_master_volume  = sdl3_audio_set_master_volume,
    .send_command       = sdl3_audio_send_command,
    .get_latency_ms     = sdl3_audio_get_latency_ms,
    .get_load           = sdl3_audio_get_load,
    .user               = NULL,
};

const stunrun_audio_backend_t *stunrun_audio_backend_sdl3_audio(void)
{
    return &the_backend;
}

const stunrun_audio_backend_t *stunrun_audio_backend_sdl3_audio_register(void)
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
    (void)stunrun_audio_backend_sdl3_audio_register();
}
