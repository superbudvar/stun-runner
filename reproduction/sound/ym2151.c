/* ym2151.c — YM2151 FM synthesis via ymfm.

   See ym2151.h for architecture notes.  This module owns the chip state:
   a ymfm::ym2151 instance is wrapped by a ymfm_chip_t C++ shim so the
   C caller can use it without direct C++ linkage.  The shim handles:
     - ymfm_interface (no-op — no external reads, timers, or IRQs needed)
     - register writes (identical to the silent stub)
     - sample generation at the chip's native rate (~29826 Hz) with optional
       resampling to the OKI6295 rate (~9039 Hz) for mixing

   The ymfm library (BSD-3-Clause, Aaron Giles) must be compiled and linked
   separately — see native/CMakeLists.txt.
 */
#include "ym2151.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ymfm_chip is the C++ wrapper around ymfm::ym2151. */
extern ymfm_chip_t *ymfm_chip_create(void);
extern void          ymfm_chip_destroy(ymfm_chip_t *);
extern void          ymfm_chip_write(ymfm_chip_t *, uint8_t addr, uint8_t data);
extern void          ymfm_chip_generate(ymfm_chip_t *, int16_t *, size_t, uint32_t);
extern uint32_t      ymfm_chip_native_sample_rate(void);

void ym2151_init(ym2151_t *c)
{
    memset(c, 0, sizeof(*c));
    /* Allocate the ymfm chip.  This is a heap allocation but it happens once
     * per ym2151_t, and there is only one ym2151 per JSA-II board. */
    c->chip = (void *)ymfm_chip_create();
    if (!c->chip) {
        fprintf(stderr, "ym2151: ymfm_chip_create() failed — FM music will be silent\n");
    }
}

void ym2151_write(ym2151_t *c, uint8_t addr, uint8_t data)
{
    if (addr < YM2151_REG_FILE_SIZE)
        c->reg[addr] = data;

    if (c->log_count < YM2151_LOG_CAP) {
        c->log[c->log_count].addr = addr;
        c->log[c->log_count].data = data;
    }
    c->log_count++;

    /* Print first few writes so developers can see real traffic. */
    if (getenv("YM2151_VERBOSE") && c->log_count <= 16) {
        fprintf(stderr, "ym2151: write $%02X = $%02X\n", addr, data);
    }

    /* Forward to ymfm (no-op if chip is NULL). */
    if (c->chip)
        ymfm_chip_write((ymfm_chip_t *)c->chip, addr, data);
}

void ym2151_generate(ym2151_t *c,
                    int16_t *stereo_samples,
                    size_t frame_size)
{
    if (!c || frame_size == 0 || !stereo_samples) {
        if (stereo_samples && frame_size > 0)
            memset(stereo_samples, 0, frame_size * 2 * sizeof(int16_t));
        return;
    }

    if (!c->chip) {
        /* Chip failed to allocate — produce silence. */
        memset(stereo_samples, 0, frame_size * 2 * sizeof(int16_t));
        return;
    }

    /* Generate at the native YM2151 rate (~29826 Hz) and resample to
     * target_rate_hz (the OKI6295 rate, so both PCM streams can be mixed
     * at a common rate). */
    uint32_t target_rate = c->target_rate_hz ? c->target_rate_hz : 9039;
    ymfm_chip_generate((ymfm_chip_t *)c->chip,
                      stereo_samples,
                      frame_size,
                      target_rate);
}
