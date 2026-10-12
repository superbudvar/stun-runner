/* ymfm_chip.hpp — ymfm-based YM2151 (OPM) synthesis wrapper.

   This thin C++ shim wraps ymfm::ym2151 so it can be called from the
   existing C ym2151_generate() API in ym2151.c.  The wrapper:

     1. Implements ymfm_interface (the minimal no-op implementation is
        sufficient — the YM2151 does not need external reads, timers, or IRQs
        for basic FM synthesis);
     2. Owns a single ymfm::ym2151 instance;
     3. Provides a C-callable generate function that clocks the chip at its
        native sample rate (JSA_MASTER_CLOCK / 12 / 2 ≈ 29826 Hz) and
        resamples the stereo output down to the OKI6295 sample rate (~9039 Hz)
        using linear interpolation, so the two PCM streams can be mixed at
        a common rate before sending to the audio backend.

   Integration: compile this file as C++14 and link it with the ymfm source
   files.  Replace the stub body of ym2151_generate() in ym2151.c to call
   ymfm_chip_generate() — the public API is identical.

   Reference: ymfm/src/ymfm_opm.h, ymfm/src/ymfm.h
   License: BSD-3-Clause (see ymfm/src/ymfm_opm.h)
*/
#ifndef YMFM_CHIP_HPP
#define YMFM_CHIP_HPP

/* For C callers */
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
/* For C++ callers: make stddef.h types available at global scope */
namespace std { using ::size_t; using ::int16_t; using ::uint8_t; using ::uint32_t; }
#include <cstring>   /* memset */
extern "C" {
#endif

/* Opaque chip state — forward-declared so callers don't need the C++
   header.  Internally this is a ymfm_chip_impl * cast to void *. */
typedef struct ymfm_chip ymfm_chip_t;

/* Allocate and initialise a ymfm YM2151 wrapper.
   Call exactly once before any other ymfm_chip_* function.
   Returns NULL on allocation failure. */
ymfm_chip_t *ymfm_chip_create(void);

/* Free a chip created with ymfm_chip_create(). */
void ymfm_chip_destroy(ymfm_chip_t *chip);

/* Write a (register_address, register_value) pair to the YM2151.
   This is identical to ym2151_write() — the same data flows into the
   same reg[] array; the only difference is that a real ymfm chip is
   now behind it. */
void ymfm_chip_write(ymfm_chip_t *chip, uint8_t addr, uint8_t data);

/* Generate stereo PCM samples, downsampled to target_sample_rate Hz.
   output: must point to an array of frame_count * 2 int16 values (L,R,L,R...).
   frame_count: number of stereo sample frames to produce.
   target_sample_rate: the output sample rate (pass oki6295_sample_rate here).

   The chip runs at its natural rate (~29826 Hz).  Linear interpolation is
   used to resample from the chip rate to target_sample_rate.  If
   target_sample_rate equals the chip's native rate, no resampling is done. */
void ymfm_chip_generate(ymfm_chip_t *chip,
                         int16_t *output,
                         size_t frame_count,
                         uint32_t target_sample_rate);

/* Return the YM2151's native output sample rate in Hz.
   This is JSA_MASTER_CLOCK / 12 / 2 ≈ 29826 Hz. */
uint32_t ymfm_chip_native_sample_rate(void);

#ifdef __cplusplus
}
#endif

#endif /* YMFM_CHIP_HPP */
