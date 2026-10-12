/* ymfm_chip.cpp — ymfm-based YM2151 (OPM) synthesis wrapper.
   See ymfm_chip.hpp for overview. */

#include "ymfm_chip.hpp"

#include <cstdlib>
#include <cstring>
#include <algorithm>

/* ymfm headers — BSD-3-Clause, see third_party/ymfm/src/ymfm_opm.h */
#include "ymfm.h"
#include "ymfm_fm.h"
#include "ymfm_opm.h"

/* JSA-II YM2151 input clock: 3.579545 MHz / 12 = 298262 Hz.
   The chip divides this by 2 internally (DEFAULT_PRESCALE=2), giving
   a natural sample rate of approximately 29826 Hz.
   ymfm's fm_engine uses: input_clock / prescaler / 512 */
static constexpr uint32_t YM2151_INPUT_CLOCK_HZ   = 3579545 / 12;   /* 298262 */
static constexpr uint32_t YM2151_PRESCALER        = 2;
static constexpr uint32_t YM2151_FM_DIVIDER       = 512;
static constexpr uint32_t YM2151_NATIVE_SAMPLE_RATE = YM2151_INPUT_CLOCK_HZ / YM2151_PRESCALER / YM2151_FM_DIVIDER;  /* ≈ 29826 */

/* Internal C++ implementation class. */
class ymfm_chip_impl : public ymfm::ymfm_interface {
public:
    ymfm_chip_impl()
        : m_chip(*this)          /* ymfm_interface passed as the interface ref */
    {
        m_chip.reset();
    }

    /* ymfm_interface minimal implementations — not used by basic FM synthesis */
    void ymfm_sync_mode_write(uint8_t) override         { }
    void ymfm_sync_check_interrupts() override         { }
    void ymfm_set_timer(uint32_t, int32_t) override   { }
    void ymfm_set_busy_end(uint32_t) override         { }
    bool ymfm_is_busy() override                       { return false; }
    void ymfm_update_irq(bool) override               { }
    uint8_t ymfm_external_read(ymfm::access_class, uint32_t) override { return 0; }
    void ymfm_external_write(ymfm::access_class, uint32_t, uint8_t) override { }

    ymfm::ym2151 m_chip;
};

/* ---- C API --------------------------------------------------------------- */

extern "C" {

struct ymfm_chip {
    ymfm_chip_impl *impl;
};

ymfm_chip_t *ymfm_chip_create(void)
{
    ymfm_chip *c = (ymfm_chip *)std::calloc(1, sizeof(ymfm_chip));
    if (!c)
        return nullptr;
    c->impl = new (std::nothrow) ymfm_chip_impl();
    if (!c->impl) {
        std::free(c);
        return nullptr;
    }
    return c;
}

void ymfm_chip_destroy(ymfm_chip_t *chip)
{
    if (!chip)
        return;
    delete chip->impl;
    std::free(chip);
}

void ymfm_chip_write(ymfm_chip_t *chip, uint8_t addr, uint8_t data)
{
    if (!chip || !chip->impl)
        return;
    /* ymfm uses the two-step address/data write sequence. */
    chip->impl->m_chip.write_address(addr);
    chip->impl->m_chip.write_data(data);
}

uint32_t ymfm_chip_native_sample_rate(void)
{
    return YM2151_NATIVE_SAMPLE_RATE;
}

/* Linear interpolation between two int32 samples, producing an int16 result. */
static inline int16_t interpolate(int32_t s0, int32_t s1, uint32_t frac)
{
    /* frac is a 16-bit fractional position between s0 and s1 (0..65535). */
    int64_t result = (int64_t)s0 * (int64_t)(65536 - frac);
    result += (int64_t)s1 * (int64_t)frac;
    result >>= 16;
    return (int16_t)std::max(-32768, std::min(32767, (int32_t)result));
}

void ymfm_chip_generate(ymfm_chip_t *chip,
                         int16_t *output,
                         size_t frame_count,
                         uint32_t target_sample_rate)
{
    if (!chip || !chip->impl || frame_count == 0 || !output) {
        if (output && frame_count > 0)
            std::memset(output, 0, frame_count * 2 * sizeof(int16_t));
        return;
    }

    /* If the target rate equals the native rate, generate directly with no
       resampling — just a simple per-sample clamp. */
    if (target_sample_rate == YM2151_NATIVE_SAMPLE_RATE) {
        ymfm::ym2151::output_data out[64];
        int16_t *dst = output;
        size_t remaining = frame_count;

        while (remaining > 0) {
            size_t chunk = (remaining < 64) ? remaining : 64;
            chip->impl->m_chip.generate(out, chunk);

            for (size_t i = 0; i < chunk; i++) {
                int32_t l = std::max(-32768, std::min(32767, out[i].data[0]));
                int32_t r = std::max(-32768, std::min(32767, out[i].data[1]));
                dst[i * 2]     = static_cast<int16_t>(l);
                dst[i * 2 + 1] = static_cast<int16_t>(r);
            }
            dst += chunk * 2;
            remaining -= chunk;
        }
        return;
    }

    /* Resampling via linear interpolation.
       We keep two native samples buffered (s0 = previous, s1 = current) and
       use a 32-bit fixed-point phase accumulator to step through the native
       stream at the target rate.  Each output frame advances the phase by
       FIXED_ONE (65536) * target_rate / native_rate. */
    static constexpr uint32_t FIXED_ONE = 65536U;
    static constexpr size_t CHUNK = 512;   /* native samples per generate() call */

    ymfm::ym2151::output_data chunk_buf[CHUNK];
    size_t chunk_start = 0;   /* absolute native sample index of chunk_buf[0] */
    size_t chunk_filled = 0;  /* how many valid samples are in chunk_buf */
    size_t chunk_idx = 0;     /* current index within chunk_buf */

    uint32_t phase = 0;       /* fixed-point position: FIXED_ONE = one native sample */
    uint32_t phase_step = (uint64_t)target_sample_rate * FIXED_ONE / YM2151_NATIVE_SAMPLE_RATE;

    int32_t prev_l = 0, prev_r = 0;
    int32_t curr_l = 0, curr_r = 0;
    bool have_curr = false;

    /* Pre-load one chunk. */
    chip->impl->m_chip.generate(chunk_buf, CHUNK);
    chunk_filled = CHUNK;

    for (size_t out_frame = 0; out_frame < frame_count; out_frame++) {
        /* Advance phase to the next output sample's native position.
           When phase wraps past FIXED_ONE, we need to advance in the native stream. */
        uint32_t prev_phase = phase;
        phase += phase_step;

        /* Check if we crossed a native sample boundary. */
        while (phase >= FIXED_ONE) {
            phase -= FIXED_ONE;
            chunk_idx++;

            /* Refill chunk if we've exhausted it. */
            if (chunk_idx >= chunk_filled) {
                chunk_start += chunk_filled;
                chip->impl->m_chip.generate(chunk_buf, CHUNK);
                chunk_filled = CHUNK;
                chunk_idx = 0;
            }

            /* Shift current to previous and load new current. */
            prev_l = curr_l;
            prev_r = curr_r;
            curr_l = chunk_buf[chunk_idx].data[0];
            curr_r = chunk_buf[chunk_idx].data[1];
            have_curr = true;
        }

        /* Fractional sample position within the interval [prev, curr]. */
        uint32_t frac = phase;   /* 0..FIXED_ONE-1 */

        int32_t s0_l = have_curr ? prev_l : curr_l;
        int32_t s0_r = have_curr ? prev_r : curr_r;
        int32_t s1_l = curr_l;
        int32_t s1_r = curr_r;

        int16_t l = interpolate(s0_l, s1_l, frac);
        int16_t r = interpolate(s0_r, s1_r, frac);

        output[out_frame * 2]     = l;
        output[out_frame * 2 + 1] = r;
    }
}

} /* extern "C" */
