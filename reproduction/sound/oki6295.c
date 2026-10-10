/* See oki6295.h for protocol documentation. */
#include "oki6295.h"

#include <assert.h>
#include <string.h>

/* ---- OKI ADPCM codec tables ---------------------------------------------
 *
 * Transcribed from MAME src/devices/sound/okiadpcm.cpp, class
 * oki_adpcm_state (the OKI6295 uses THIS variant; oki_adpcm2_state is a
 * different chip with 1.08^n step growth and is not what JSA-II has).
 *
 *   oki_adpcm_state::s_index_shift[8] = { -1,-1,-1,-1, 2, 4, 6, 8 }
 *
 * Exposed as 16 entries so it can be indexed by (nibble & 7) exactly as the
 * old code was; the values repeat.  The step index runs 0..48, NOT 0..88 —
 * 89 is the IMA table's size and was never right for this chip.
 */
const int8_t oki6295_index_table[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8
};

/* oki_adpcm_state::s_diff_lookup, i.e.
 *   compute_tables():
 *       stepval = floor(16.0 * pow(11.0 / 10.0, step))        step = 0..48
 *       diff_lookup[step*16 + nib] = nbl2bit[nib][0] * (stepval*nbl2bit[nib][1]
 *                        + stepval/2*nbl2bit[nib][2] + stepval/4*nbl2bit[nib][3]
 *                        + stepval/8)
 * where nbl2bit[nib][0] is the sign (+1 / -1).
 *
 * The table is built at load time from OKI6295_STEPVAL below rather than
 * pasted in as 784 magic numbers, so the arithmetic in the oracle stays
 * visible and testable.  It is built once, on first use; oki6295_init()
 * forces the build.
 */
static const int16_t s_stepval[49] = {
    16,   17,   19,   21,   23,   25,   28,   31,   34,   37,   41,   45,   50,
    55,   60,   66,   73,   80,   88,   97,  107,  118,  130,  143,  157,  173,
   190,  209,  230,  253,  279,  307,  337,  371,  408,  449,  494,  544,  598,
   658,  724,  796,  876,  963, 1060, 1166, 1282, 1411, 1552
};

/* nbl2bit[16][4]: { sign, bit2, bit1, bit0 } of the nibble. */
static const int8_t s_nbl2bit[16][4] = {
    { 1, 0, 0, 0}, { 1, 0, 0, 1}, { 1, 0, 1, 0}, { 1, 0, 1, 1},
    { 1, 1, 0, 0}, { 1, 1, 0, 1}, { 1, 1, 1, 0}, { 1, 1, 1, 1},
    {-1, 0, 0, 0}, {-1, 0, 0, 1}, {-1, 0, 1, 0}, {-1, 0, 1, 1},
    {-1, 1, 0, 0}, {-1, 1, 0, 1}, {-1, 1, 1, 0}, {-1, 1, 1, 1}
};

static int16_t s_diff_lookup[49 * 16];
static bool    s_tables_built;

void oki6295_build_tables(void)
{
    int step, nib;

    if (s_tables_built)
        return;

    for (step = 0; step < 49; step++) {
        int16_t stepval = s_stepval[step];
        for (nib = 0; nib < 16; nib++) {
            s_diff_lookup[step * 16 + nib] = (int16_t)(
                s_nbl2bit[nib][0] * (stepval   * s_nbl2bit[nib][1]
                                  + stepval/2 * s_nbl2bit[nib][2]
                                  + stepval/4 * s_nbl2bit[nib][3]
                                  + stepval   / 8));
        }
    }
    s_tables_built = true;
}

/* ---- volume -------------------------------------------------------------
 *
 * okim6295_device::s_volume_table[16], each entry divided by 0x20:
 *
 *   {0x20,0x16,0x10,0x0b,0x08,0x06,0x04,0x03,0x02, 0,0,0,0,0,0,0} / 0x20
 *
 * MAME's comment: "The manual lists only 9 steps, ~3dB per step ... Any
 * volume parameter beyond the 9th index results in silent playback."
 *
 * INDEX 0 IS THE LOUDEST.  Indices 9..15 are digital silence.  The table
 * below is the raw numerator, oki6295_volume_float() is the /0x20 form.
 */
const uint8_t oki6295_volume_table[16] = {
    0x20, 0x16, 0x10, 0x0B, 0x08, 0x06, 0x04, 0x03,
    0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

float oki6295_volume_float(uint8_t index)
{
    return (float)oki6295_volume_table[index & 0x0F] / 32.0f;
}

void oki6295_init(oki6295_t *c)
{
    memset(c, 0, sizeof(*c));
    c->pending_command = -1;
    /* D16: the board straps PIN7 HIGH.  atarijsa.cpp:901 instantiates the
     * OKIM6295 with okim6295_device::PIN7_HIGH, which is a board-level
     * configuration, not something the 68010 or the 6502 changes at run
     * time -- the 6502 reaches pin 7 only through WRIO bit 3, and nothing
     * in the STUN Runner title path writes that bit.  Defaulting to 0 gave
     * divisor 165 (7231 Hz) instead of 132 (9039 Hz). */
    c->pin7_state = 1;
    oki6295_build_tables();
}

bool oki6295_load_rom(oki6295_t *c, const uint8_t *rom, uint32_t rom_size_bytes)
{
    if (rom_size_bytes != 256 * 1024)
        return false;
    c->rom = rom;
    c->rom_size = rom_size_bytes;
    return true;
}

int oki6295_set_pin7(oki6295_t *c, int state)
{
    /* okim6295_device:  int divisor = m_pin7_state ? 132 : 165;
     * set_pin7(p)  ->  m_pin7_state = p ? 1 : 0
     *
     * PIN7 HIGH (the JSA-II default, atarijsa.cpp:901 passes PIN7_HIGH) is
     * the FASTER of the two: divisor 132.  PIN7 LOW is 165.  Returns the
     * divisor, not a 1/2 ratio.
     */
    c->pin7_state = state ? 1 : 0;
    return c->pin7_state ? 132 : 165;
}

uint32_t oki6295_sample_rate(const oki6295_t *c)
{
    /* okim6295_device::device_start():
     *     int divisor = m_pin7_state ? 132 : 165;
     *     m_stream = stream_alloc(0, 1, clock() / divisor);
     *
     * For JSA-II the clock handed to OKIM6295() is JSA_MASTER_CLOCK / 3 with
     * JSA_MASTER_CLOCK == XTAL(3'579'545) (atarijsa.cpp:80 and :901), so
     * the default rate is 1193181 / 132 = 9039 Hz.
     */
    return OKI6295_JSA_MASTER_CLOCK_HZ / OKI6295_JSA_CLOCK_DIVISOR /
           (uint32_t)(c->pin7_state ? 132 : 165);
}

int16_t oki6295_decode_nibble(oki6295_voice_t *v, uint8_t nibble)
{
    /* oki_adpcm_state::clock(uint8_t nibble).  m_step and m_signal are both
     * int32_t in okiadpcm.h, which is what makes the `else if (m_step < 0)`
     * clamp reachable: s_index_shift is signed and the sum can go negative.
     * Storing the step in a uint8_t instead (as this model did) wraps 0-1 to
     * 255 and then pins the step at 48 forever, so every quiet passage came
     * out at maximum gain. */
    int step = v->adpcm.step;
    int next = step + oki6295_index_table[nibble & 0x07];

    oki6295_build_tables();

    v->adpcm.signal = (int16_t)(v->adpcm.signal +
                                s_diff_lookup[step * 16 + (nibble & 15)]);
    if (v->adpcm.signal >  2047) v->adpcm.signal =  2047;
    if (v->adpcm.signal < -2048) v->adpcm.signal = -2048;

    if (next > 48) next = 48;
    if (next <  0) next =  0;
    v->adpcm.step = (uint8_t)next;

    return v->adpcm.signal;
}

int oki6295_write(oki6295_t *c, uint8_t byte)
{
    /* If a command is pending, this is the second byte → start sample(s). */
    if (c->pending_command >= 0) {
        int sample_index = c->pending_command;
        int voicemask = (byte >> 4) & 0x0F;   /* bits 0-3 = voices 0-3 */
        int vol_idx   = byte & 0x0F;            /* volume index */
        c->pending_command = -1;

        if (voicemask == 0)
            return 0;  /* valid stop-on-second-byte (volume 0 = silence? ignore) */

        /* ROM header offset for this sample: 8 bytes per sample entry. */
        uint32_t base = (uint32_t)sample_index * 8U;

        float volume = oki6295_volume_float((uint8_t)vol_idx);

        int started = 0;
        for (int vn = 0; vn < OKI6295_VOICES; vn++, voicemask >>= 1) {
            if (!(voicemask & 1))
                continue;
            oki6295_voice_t *v = &c->voice[vn];
            if (v->playing)
                continue;  /* can't restart a running voice */

            /* Read start/stop from ROM (big-endian 20-bit, masked to 18 bits = 256KB). */
            uint32_t start = ((uint32_t)c->rom[base + 0] << 16)
                           | ((uint32_t)c->rom[base + 1] << 8)
                           | ((uint32_t)c->rom[base + 2]);
            start &= 0x3FFFFU;

            uint32_t stop  = ((uint32_t)c->rom[base + 3] << 16)
                           | ((uint32_t)c->rom[base + 4] << 8)
                           | ((uint32_t)c->rom[base + 5]);
            stop &= 0x3FFFFU;

            /* okim6295_device::write():  if (start < stop) { ...start it... }
             * else logerror("Requested to play invalid sample %02x").
             * start == stop is therefore REJECTED, and the check is made
             * before the bounds test — the chip does not range-check.
             * Reproduced: the previous code accepted start == stop. */
            if (start >= stop)
                continue;

            /* No explicit bounds test is needed: both are masked to 18 bits
             * (<= $3FFFF == 262143) and start < stop, and load_rom() insists
             * on exactly 262144 bytes, so both indices are in range.  Asserted
             * rather than assumed. */
            assert(start < c->rom_size && stop < c->rom_size);

            v->playing       = true;
            v->base_offset   = start;
            v->sample        = 0;
            v->count         = 2U * (stop - start + 1U);   /* 2 nibbles per byte */
            v->volume        = volume;
            v->adpcm.signal  = 0;
            v->adpcm.step    = 0;
            started = 1;
        }
        return started ? 1 : 0;
    }

    /* No command pending. */
    if (byte & 0x80) {
        /* Start of 2-byte sequence: store sample index (bits 0-6). */
        c->pending_command = (int)(byte & 0x7F);
        return -1;
    } else {
        /* Stop command: bits 3-6 = voice mask. */
        uint8_t mask = (byte >> 3) & 0x0F;
        oki6295_stop(c, mask);
        return 0;
    }
}

void oki6295_stop(oki6295_t *c, uint8_t voice_mask)
{
    if (voice_mask == 0) {
        /* Stop all voices. */
        for (int i = 0; i < OKI6295_VOICES; i++)
            c->voice[i].playing = false;
    } else {
        for (int i = 0; i < OKI6295_VOICES; i++, voice_mask >>= 1)
            if (voice_mask & 1)
                c->voice[i].playing = false;
    }
}

size_t oki6295_fill(oki6295_t *c, int16_t *stereo_out, size_t max_samples)
{
    if (!c->rom)
        return 0;

    oki6295_build_tables();

    size_t written = 0;

    for (size_t s = 0; s < max_samples; s++) {
        int32_t mixed = 0;
        int voices_active = 0;

        for (int vn = 0; vn < OKI6295_VOICES; vn++) {
            oki6295_voice_t *v = &c->voice[vn];
            if (!v->playing)
                continue;
            voices_active++;

            /* okim_voice::generate_adpcm():
             *     int nibble = rom.read_byte(m_base_offset + m_sample / 2)
             *                  >> (((m_sample & 1) << 2) ^ 4);
             * An even m_sample shifts by 4 -> the HIGH nibble comes first;
             * an odd one shifts by 0 -> the LOW nibble.  The previous code
             * did the opposite. */
            uint8_t rom_byte = c->rom[v->base_offset + v->sample / 2];
            uint8_t nibble = (uint8_t)(rom_byte >> (((v->sample & 1) << 2) ^ 4));

            int16_t pcm = oki6295_decode_nibble(v, nibble);

            /* stream.add_int(0, sampindex, m_adpcm.clock(nibble) * m_volume,
             *                 2048);
             *
             * MAME normalises every voice by 2048: one voice at full volume
             * spans the whole +/-2048 range the decoder can reach, and the
             * four voices sum into that same range.  sound_stream::add_int
             * takes an s32 and divides by `max`, so the product is truncated
             * toward zero on the way in.  Our output buffer is int16, so the
             * same normalisation is the x16 gain 32768/2048 below; mixing at
             * 1:1 (the previous behaviour) put everything 16x too quiet. */
            mixed += (int32_t)((float)pcm * v->volume * 16.0f);

            /* if (++m_sample >= m_count) { m_playing = false; break; }
             * Post-increment: the LAST nibble is emitted, then the voice
             * stops.  The previous code tested before decoding and so lost
             * the tail. */
            v->sample++;
            if (v->sample >= v->count) {
                v->playing = false;
            }
        }

        if (voices_active == 0) {
            /* All voices stopped. */
            break;
        }

        /* Clamp mixed sample to int16 range. */
        if (mixed >  32767) mixed =  32767;
        if (mixed < -32768) mixed = -32768;
        int16_t s16 = (int16_t)mixed;

        /* Write stereo (same sample to both channels). */
        stereo_out[s * 2]     = s16;
        stereo_out[s * 2 + 1] = s16;
        written++;
    }

    return written;
}

bool oki6295_is_playing(const oki6295_t *c)
{
    for (int i = 0; i < OKI6295_VOICES; i++)
        if (c->voice[i].playing)
            return true;
    return false;
}

bool oki6295_command_pending(const oki6295_t *c)
{
    return c->pending_command >= 0;
}
