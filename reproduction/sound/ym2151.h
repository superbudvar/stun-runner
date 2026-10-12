/* ym2151.h — YM2151 FM operator synthesis (JSA-II music channel).

   The JSA II uses a YM2151 to produce FM music.  The 6502 firmware
   at $4569 handles class-8 commands (32 of the 109 command bytes) by
   writing register/value pairs to the YM2151's two-port interface:

     $2000 → register address
     $2001 → register value

   The chip has 8 melody channels, each with 4 operators.  The actual
   synthesis is performed by the ymfm library (ymfm::ym2151), which this
   module wraps in a C-compatible API.

   References:
     MAME src/devices/sound/ym2151.cpp — YM2151 device emulator
     YM2151 data sheet — register map and FM operator architecture
     docs/jsa2-sound-findings.md §1.1 — Ghidra symbols for the 6502 handlers
     ymfm/src/ymfm_opm.h — ymfm OPM core (BSD-3-Clause, Aaron Giles)
  */
#ifndef STUNRUN_YM2151_H
#define STUNRUN_YM2151_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Chip state.  Defined here so that sound_dispatch.h can embed it
 * without requiring a pointer indirection. */
#define YM2151_REG_FILE_SIZE 128

/* Log buffer depth for debug / test validation. */
#define YM2151_LOG_CAP 64

/* Forward-declare the ymfm chip wrapper (opaque). */
typedef struct ymfm_chip ymfm_chip_t;

struct ym2151 {
    /* Chip register file (128 × 1 byte). */
    uint8_t reg[YM2151_REG_FILE_SIZE];

    /* Log of recent writes, for debug / test validation. */
    struct {
        uint8_t addr;
        uint8_t data;
    } log[YM2151_LOG_CAP];
    unsigned log_count;

    /* Opaque ymfm chip wrapper (NULL if unavailable or not yet init'd). */
    ymfm_chip_t *chip;

    /* Target sample rate for resampling (Hz).  Set this before calling
     * ym2151_generate() to the rate you want the output at (typically
     * the OKI6295's sample rate so both PCM streams can be mixed at a
     * common rate).  Default 9039 Hz. */
    uint32_t target_rate_hz;
};

/* Opaque alias for callers that only need a forward declaration. */
typedef struct ym2151 ym2151_t;

/* Initialise a YM2151 chip state.
   Must be called before any other ym2151_* function on a given instance. */
void ym2151_init(ym2151_t *c);

/* Write a register/data pair.
   addr: YM2151 register address (written to $2000)
   data: register value   (written to $2001)
   This models the JSA II's two-write sequence; the 6502 ($427F-$4289)
   programs voice parameters and ($4EAF-$4EB8, $4EFC-$4F08) streams
   note events through this interface. */
void ym2151_write(ym2151_t *c, uint8_t addr, uint8_t data);

/* Fill PCM samples for one frame into the provided stereo buffer.

   The chip runs at its natural rate (~29826 Hz).  If ym2151.target_rate_hz
   differs from the native rate, linear interpolation is used to resample
   to the target rate.

   stereo_samples: output buffer for frame_size stereo sample-pairs (L,R,L,R…
                   as int16, i.e. frame_size * 2 int16 values)
   frame_size:   number of stereo frames to produce.

   The output level is approximately ±32767 full scale.  Mix with the
   OKI6295 PCM (also int16 stereo) before sending to the audio backend. */
void ym2151_generate(ym2151_t *c,
                     int16_t *stereo_samples,
                     size_t frame_size);

#ifdef __cplusplus
}
#endif

#endif /* STUNRUN_YM2151_H */
