/* OKI6295 ADPCM chip emulator.
 *
 * Models the OKI6295 ADPCM sample playback chip used in the Atari JSA II
 * sound board.  The chip stores 4-bit ADPCM-compressed samples in ROM and
 * decodes them to 16-bit PCM on playback.
 *
 * Protocol (from OKI6295 data sheet):
 *   Byte with bit 7 set  → store as sample index (0-127), wait for 2nd byte
 *   Byte with bit 7 clear → stop command (bits 3-6 = voice mask)
 *   2nd byte (after 1xxx xxxx first byte):
 *     upper 4 bits = voice mask (which of 4 voices to start)
 *     lower 4 bits = volume (0-15) — INDEX 0 IS LOUDEST, 9..15 are silent
 *
 * Each sample index refers to an 8-byte header in ROM:
 *   bytes 0-2: 20-bit start address  (big-endian, masked to 18 bits = 256KB)
 *   bytes 3-5: 20-bit stop address   (big-endian, masked to 18 bits)
 *   bytes 6-7: (unused padding)
 *
 * A sample is accepted only when start < stop (okim6295.cpp).  start == stop
 * is rejected, exactly as on the real chip.
 *
 * The sample ROM is 256 KB (one OKI6295 bank).  The pin7 state selects the
 * clock divisor (132 high, 165 low) and is written by the 6502 through the
 * JSA-II's WRIO register, bit 3 (atarijsa.cpp:503).
 *
 * Reference: MAME src/devices/sound/okiadpcm.cpp (oki_adpcm_state) and
 * src/devices/sound/okim6295.cpp.
 */
#ifndef STUNRUN_OKI6295_H
#define STUNRUN_OKI6295_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Number of independent playback voices. */
#define OKI6295_VOICES 4

/* The OKI6295's step index runs 0..48 (49 entries).  89 is the IMA table's
 * size and never applied to this chip. */
#define OKI6295_STEPS 49

/* The decoder's signal runs -2048..+2047. */
#define OKI6295_SIGNAL_MIN (-2048)
#define OKI6295_SIGNAL_MAX ( 2047)

/* MAME normalises each voice by 2048 (okim6295.cpp:
 *   stream.add_int(0, sampindex, m_adpcm.clock(nibble) * m_volume, 2048)).
 * An int16 buffer therefore needs the x16 gain 32768/2048 to reach the same
 * relative level. */
#define OKI6295_MIX_GAIN 16

/* JSA-II clocking, transcribed from atarijsa.cpp:
 *   #define JSA_MASTER_CLOCK XTAL(3'579'545)        (atarijsa.cpp:80)
 *   OKIM6295(config, m_oki1, JSA_MASTER_CLOCK/3,
 *            okim6295_device::PIN7_HIGH);           (atarijsa.cpp:901)
 * With PIN7 high the chip divides that by 132 -> 9039 Hz; PIN7 low, by 165 ->
 * 7231 Hz.  Nothing in this tree hardcodes either rate. */
#define OKI6295_JSA_MASTER_CLOCK_HZ 3579545
#define OKI6295_JSA_CLOCK_DIVISOR   3
#define OKI6295_PIN7_HIGH_DIVISOR   132
#define OKI6295_PIN7_LOW_DIVISOR    165

/* Volume table from okim6295_device::s_volume_table, raw numerators.
 * INDEX 0 IS THE LOUDEST (0x20 == full scale) and indices 9..15 are DIGITAL
 * SILENCE — the OKI manual lists only 9 usable steps.  Divide by 0x20 for a
 * linear factor; oki6295_volume_float() does that for you. */
extern const uint8_t oki6295_volume_table[16];

/* Build the diff-lookup table.  Idempotent; called for you by
 * oki6295_init() and defensively by the decoder. */
void oki6295_build_tables(void);

/* Linear volume for a 4-bit OKI volume nibble (index 0 = 1.0, 9..15 = 0.0). */
float oki6295_volume_float(uint8_t index);

/* One voice's ADPCM decoder state. */
typedef struct oki6295_adpcm_state {
    int16_t  signal;   /* running sample, clamped to -2048..+2047 */
    uint8_t  step;     /* step index into the 49-entry OKI step table */
} oki6295_adpcm_state_t;

/* Per-voice playback state. */
typedef struct oki6295_voice {
    bool     playing;          /* true if this voice is currently playing */
    uint32_t base_offset;       /* start address in ROM (byte offset) */
    uint32_t sample;           /* current sample index within the chunk */
    uint32_t count;            /* total sample count (including both nibbles) */
    float    volume;           /* linear volume (0.0..1.0) */
    oki6295_adpcm_state_t adpcm;
} oki6295_voice_t;

/* OKI6295 chip state. */
typedef struct oki6295 {
    /* ROM data (256 KB, mapped by bank).  NULL if not loaded. */
    const uint8_t *rom;

    /* ROM size in bytes (expected: 256 KB = 262144). */
    uint32_t rom_size;

    /* Pin7 state.  okim6295.h: PIN7_LOW = 0, PIN7_HIGH = 1, and
     * okim6295_device::device_start() picks
     *     int divisor = m_pin7_state ? 132 : 165;
     * so PIN7 HIGH (the JSA-II default) is the FASTER clock.  The 6502 sets
     * it from bit 3 of WRIO: atarijsa.cpp:503 does
     *     m_oki1->set_pin7(data & 8);   */
    int pin7_state;

    /* Pending command byte (-1 = none). */
    int pending_command;

    /* 4 voices. */
    oki6295_voice_t voice[OKI6295_VOICES];
} oki6295_t;

/* OKI ADPCM step-index delta per low nibble (oki_adpcm_state::s_index_shift).
 * Indexed by (nibble & 7); 16 entries so the whole nibble can index it. */
extern const int8_t  oki6295_index_table[16];

/* Initialise an OKI6295 chip state object (zero all fields). */
void oki6295_init(oki6295_t *c);

/* Load sample ROM data.  Takes ownership of the pointed-to buffer (caller
 * must not free it after this call).  Returns false if size != 262144. */
bool oki6295_load_rom(oki6295_t *c, const uint8_t *rom, uint32_t rom_size_bytes);

/* Set pin7 state (clock divisor).  Returns the NEW clock divisor:
 * 132 for PIN7 high, 165 for PIN7 low. */
int oki6295_set_pin7(oki6295_t *c, int state);

/* The chip's sample rate for the current pin7 state:
 * (JSA_MASTER_CLOCK / 3) / divisor, i.e. 9039 Hz high, 7231 Hz low.
 * Matches okim6295_device::device_start()'s stream_alloc(0, 1,
 * clock() / divisor) under atarijsa.cpp's OKIM6295(config, ..., JSA_MASTER_CLOCK/3). */
uint32_t oki6295_sample_rate(const oki6295_t *c);

/* Advance one ADPCM decoder by one nibble and return the signed 16-bit PCM
 * sample.  The decoder state is updated in-place in the voice. */
int16_t oki6295_decode_nibble(oki6295_voice_t *v, uint8_t nibble);

/* Process a single command byte.  This implements the two-byte command
 * protocol described above.
 *
 * When a sample-start command is completed (both bytes received), the
 * voice is set up but samples are NOT decoded here — call oki6295_fill()
 * each frame to decode and consume samples.
 *
 * Returns:  -1 = need more bytes (waiting for 2nd byte)
 *            0 = stop command processed
 *            1 = sample start command completed (voice started)
 */
int oki6295_write(oki6295_t *c, uint8_t byte);

/* Stop one voice (or all voices if voice_mask bits are 0).
 * voice_mask: bit N set = stop voice N.  Passing 0 stops all voices. */
void oki6295_stop(oki6295_t *c, uint8_t voice_mask);

/* Decode up to max_samples stereo PCM samples from all currently-playing
 * voices and mix them into the output buffer (interleaved LRLR... int16).
 *
 * The ADPCM decoder state is updated per-nibble in-place.
 *
 * Returns the number of samples actually written to the buffer.
 * The output buffer must have capacity for max_samples * 2 int16 values.
 *
 * If voices are active, this should be called every frame (at
 * oki6295_sample_rate() — 9039 Hz with PIN7 high, 7231 Hz with it low) to
 * keep the PCM stream going. */
size_t oki6295_fill(oki6295_t *c, int16_t *stereo_out, size_t max_samples);

/* Check if any voice is currently playing. */
bool oki6295_is_playing(const oki6295_t *c);

/* True if a command byte is pending (waiting for 2nd byte). */
bool oki6295_command_pending(const oki6295_t *c);

#ifdef __cplusplus
}
#endif

#endif /* STUNRUN_OKI6295_H */
