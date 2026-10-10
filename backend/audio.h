/* Cross-platform audio backend interface.
 *
 * The JSA II / ADPCM sound system in STUN Runner is a 6502-class CPU that
 * receives commands from the main MC68010 via a latch (soundcomm) and plays
 * ADPCM samples.  This interface models the OUTPUT side of that hardware —
 * the backend receives decoded PCM sample data and plays it through the
 * host platform's audio API.
 *
 * The core never touches audio hardware directly; it always goes through
 * one of these function pointers.
 *
 * Supported stream types:
 *   - PCM signed 16-bit stereo at 11025, 22050 or 44100 Hz (ADPCM decoded)
 *   - PCM signed 16-bit mono  (pre-downmixed or single-channel samples)
 *
 * Lifecycle:
 *   create -> init -> [stream_write | stream_queue | set_gain | set_palette]* -> drain -> shutdown -> destroy
 */
#ifndef STUNRUN_BACKEND_AUDIO_H
#define STUNRUN_BACKEND_AUDIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Waveform encoding the core may emit. */
typedef enum stunrun_audio_format {
    STUNRUN_AUDIO_FORMAT_S16LE_STEREO,   /* signed 16-bit little-endian, stereo */
    STUNRUN_AUDIO_FORMAT_S16LE_MONO,      /* signed 16-bit little-endian, mono   */
    STUNRUN_AUDIO_FORMAT_U8_STEREO,       /* unsigned 8-bit, stereo             */
    STUNRUN_AUDIO_FORMAT_U8_MONO,         /* unsigned 8-bit, mono              */
} stunrun_audio_format_t;

/* Audio sample buffer descriptor. */
typedef struct stunrun_audio_buffer {
    const int16_t *samples;   /* stereo: LRLR...  mono: LLLL... */
    size_t sample_count;      /* total number of samples (NOT bytes) */
    uint32_t sample_rate;     /* 11025, 22050 or 44100 Hz */
    stunrun_audio_format_t format;
    uint64_t buffer_index;    /* monotonic, for ordering / deduplication */
    double   timestamp_seconds;
} stunrun_audio_buffer_t;

/* ADPCM sample descriptor (as decoded from the JSA II ADPCM streams). */
typedef struct stunrun_audio_adpcm {
    const uint8_t *nibbles;   /* ADPCM nibbles to decode */
    size_t nibble_count;      /* number of nibbles; samples = nibble_count * 2 (stereo) or nibble_count (mono) */
    uint32_t sample_rate;
    uint8_t  channel_count;   /* 1 or 2 */
    uint64_t stream_id;       /* identifies which ADPCM stream this belongs to */
    double   timestamp_seconds;
} stunrun_audio_adpcm_t;

/* Mute / unmute the audio output (does not drop queued samples). */
typedef void (*stunrun_audio_set_mute_fn)(void *user, bool mute);

/* Backend v-table. */
typedef struct stunrun_audio_backend {
    const char *name;

    /* Lifecycle.  All return true on success. */
    bool (*create)(void *user);
    bool (*init)(void *user, uint32_t default_sample_rate);
    void (*shutdown)(void *user);
    void (*destroy)(void *user);

    /* Playback.
     *
     * stream_write: non-blocking enqueue of raw PCM samples.
     *               The backend must copy the data internally.
     *               Returns the number of samples actually enqueued.
     *
     * stream_adpcm:  Decode ADPCM nibbles in-place and enqueue the resulting
     *               PCM.  Backends may forward the raw nibbles to a hardware
     *               decoder if available.  Returns samples enqueued after decode.
     *
     * drain:        Block until all previously enqueued samples have finished
     *               playing.  Used before shutdown / context change.
     *
     * set_gain:     Linear gain 0.0..1.0 applied to all subsequent samples.
     *
     * set_master_volume: Same as set_gain but on a per-stream-id basis.
     *                    Set stream_id = 0 for the master channel.
     */
    size_t (*stream_write)(void *user, const stunrun_audio_buffer_t *buf);
    size_t (*stream_adpcm)(void *user, const stunrun_audio_adpcm_t *adpcm);
    void   (*drain)(void *user);
    void   (*set_gain)(void *user, float gain);
    void   (*set_master_volume)(void *user, uint8_t stream_id, float volume);

    /* Optional: send a JSA II command byte to the audio subsystem.
     * Commands are 8-bit values that encode "play sample N", "stop stream",
     * "set volume" etc.  If the backend has access to the real sample ROMs
     * it can interpret these; otherwise it may store them for diagnostics.
     * Set to NULL if not implemented. */
    void (*send_command)(void *user, uint8_t command_byte);

    /* Mute / unmute (does not drop queued samples).  Set to NULL if not needed. */
    void (*set_mute)(void *user, bool mute);

    /* Diagnostics.  Set to NULL if not needed. */
    float (*get_latency_ms)(void *user);   /* approximate playback latency */
    float (*get_load)(void *user);          /* 0.0..1.0, backend CPU load */

    void *user;
} stunrun_audio_backend_t;

/* Helper: build a stereo interleaved S16LE buffer from separate L/R arrays. */
typedef struct stunrun_audio_stereo_helper {
    int16_t buffer[8192];  /* sufficient for ~186ms at 44.1kHz stereo */
    size_t  capacity;
} stunrun_audio_stereo_helper_t;

/* Initialize a helper object.  Returns false if sample_count exceeds capacity. */
static inline bool
stunrun_audio_stereo_helper_init(stunrun_audio_stereo_helper_t *h, size_t sample_count)
{
    h->capacity = sizeof(h->buffer) / sizeof(h->buffer[0]);
    return sample_count * 2 <= h->capacity;
}

/* Fill helper buffer from separate L/R arrays (both sample_count samples). */
static inline void
stunrun_audio_stereo_helper_from_lr(stunrun_audio_stereo_helper_t *h,
                                   const int16_t *L, const int16_t *R,
                                   size_t sample_count)
{
    for (size_t i = 0; i < sample_count; i++) {
        h->buffer[i * 2]     = L[i];
        h->buffer[i * 2 + 1] = R[i];
    }
}

#ifdef __cplusplus
}
#endif

#endif /* STUNRUN_BACKEND_AUDIO_H */
