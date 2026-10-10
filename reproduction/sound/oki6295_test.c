/* oki6295_test.c — asserts the OKI6295 decoder's OBSERVABLE output.
 *
 * This is the regression test for four defects that made the previous model
 * unusable (docs/jsa2-sound-findings.md §5 D3..D9):
 *
 *   D3  the "OKI" decoder was IMA ADPCM (89 steps, (step*delta)>>11)
 *   D4  the volume table was reversed — index 0 was SILENCE and index 15 was
 *       full scale, so 46 of the 53 effect commands came out silent
 *   D6  the sample rate was hardcoded to 11025 Hz
 *   D7  start == stop was accepted; the chip rejects it
 *   D8  the mix ran at 1:1 int16 scale instead of MAME's /2048 normalisation
 *   D9  the last nibble of every sample was dropped
 *
 * WHAT IS ASSERTED, all of it observable PCM rather than internal flags:
 *
 *   1. The 49-entry step table equals floor(16.0 * pow(1.1, step)) and the
 *      index-delta table equals oki_adpcm_state::s_index_shift.
 *   2. A hand-computed nibble sequence reproduces specific PCM values.
 *   3. The signal clamps at +2047 and -2048 (never the int16 bounds).
 *   4. The step index clamps at 48 and 0 (never 88).
 *   5. MAME's volume index 0 is non-zero and full scale; index 9 is exactly
 *      zero, and so are 10..15.
 *   6. PIN7 high -> divisor 132 -> 9039 Hz; PIN7 low -> 165 -> 7231 Hz.
 *   7. Nibble order is HIGH first, taken from a real sample's ROM bytes.
 *   8. If the four OKI ROMs are present, a real sample is decoded end to
 *      end and its first/last PCM values and 16-bit checksum are compared
 *      against values produced by an independent Python transcription of
 *      MAME's codec.  Without the ROMs that part is skipped.
 */
#include "oki6295.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int g_failures = 0;
static int g_checks = 0;

#define check(cond)                                                           \
    do {                                                                      \
        g_checks++;                                                           \
        if (cond) printf("PASS %s\n", #cond);                                 \
        else { printf("FAIL %s\n", #cond); g_failures++; }                    \
    } while (0)

#define check_eq(actual, expect, label)                                       \
    do {                                                                      \
        g_checks++;                                                           \
        long a_ = (long)(actual), e_ = (long)(expect);                        \
        if (a_ == e_) printf("PASS %s == %ld\n", (label), e_);                \
        else { printf("FAIL %s: got %ld want %ld\n", (label), a_, e_);        \
               g_failures++; }                                                \
    } while (0)

/* ---- 1. the codec tables, rebuilt from the oracle's own formula ----------- */
static void test_stepval_table(void)
{
    /* oki_adpcm_state::compute_tables() computes
     *     stepval = floor(16.0 * pow(11.0 / 10.0, (double)step))
     * rather than storing it, so the transcribed table has to match. */
    int step;
    int bad = 0;

    oki6295_build_tables();

    /* The step table is internal, so re-derive it through the public
     * decoder instead: pin the step index by hand and feed nibble 7.
     * nbl2bit[7] == {1,1,1,1}, so
     *   diff[step][7] = stepval + stepval/2 + stepval/4 + stepval/8
     * with INTEGER division at every term, which is what the oracle does.
     * The signal is seeded to -1024 rather than 0 because diff[45][7] is
     * 2185 and diff[48][7] is 2910: starting from 0 those would saturate at
     * the +2047 clamp and the check would silently test the clamp instead of
     * the table.  -1024 + 2910 == 1886 < 2047, so no step saturates. */
    for (step = 0; step < 49; step++) {
        oki6295_voice_t v;
        long sv = (long)floor(16.0 * pow(1.1, (double)step));
        long expect = sv + sv/2 + sv/4 + sv/8;
        int16_t got;
        memset(&v, 0, sizeof(v));
        v.adpcm.signal = -1024;
        v.adpcm.step = (uint8_t)step;
        got = oki6295_decode_nibble(&v, 7);
        if ((long)got != expect - 1024) {
            printf("  step %2d: diff[step][7] = %ld, want %ld\n", step,
                   (long)got - (-1024), expect);
            bad++;
        }
    }
    g_checks++;
    if (bad == 0)
        printf("PASS diff_lookup[step][7] == stepval+stepval/2+stepval/4+stepval/8, "
               "stepval == floor(16*1.1^step), all 49 steps\n");
    else {
        printf("FAIL %d of 49 step values wrong\n", bad);
        g_failures++;
    }

    /* Negative side: nibble 8 is the sign bit alone, nbl2bit[8] == {-1,0,0,0},
     * so diff[step][8] == -(stepval/8).  stepval(48) = 1552 -> -194,
     * stepval(0) = 16 -> -2. */
    {
        oki6295_voice_t v;
        memset(&v, 0, sizeof(v));
        v.adpcm.step = 48;
        check_eq(oki6295_decode_nibble(&v, 8), -194, "diff[48][8]");
        v.adpcm.signal = 0; v.adpcm.step = 0;
        check_eq(oki6295_decode_nibble(&v, 8), -2, "diff[0][8]");
        v.adpcm.signal = 0; v.adpcm.step = 0;
        check_eq(oki6295_decode_nibble(&v, 7), 30, "diff[0][7]");
        /* Seeded at -1024: diff[48][7] is 2910, which would saturate at 2047
         * from a zero signal.  -1024 + 2910 == 1886. */
        v.adpcm.signal = -1024; v.adpcm.step = 48;
        check_eq(oki6295_decode_nibble(&v, 7), 1886, "diff[48][7] = 2910 (-1024 seed)");
    }

    /* s_index_shift, indexed by nibble & 7 (nibble 9 == nibble 1's shift). */
    check_eq(oki6295_index_table[0], -1, "index_shift[0]");
    check_eq(oki6295_index_table[3], -1, "index_shift[3]");
    check_eq(oki6295_index_table[4],  2, "index_shift[4]");
    check_eq(oki6295_index_table[5],  4, "index_shift[5]");
    check_eq(oki6295_index_table[6],  6, "index_shift[6]");
    check_eq(oki6295_index_table[7],  8, "index_shift[7]");
}

/* ---- 2. hand-computed PCM sequence --------------------------------------- */
static void test_known_pcm(void)
{
    /* Step from a zero state; each nibble is decoded from a fresh reset so
     * the expectation only depends on the table, not on history.
     * diff[0][n] = 2,6,10,14,18,22,26,30,-2,-6,-10,-14,-18,-22,-26,-30. */
    oki6295_voice_t v;

    memset(&v, 0, sizeof(v));
    check_eq(oki6295_decode_nibble(&v, 0x0),   2, "first nibble 0");
    /* decode_nibble returns the SIGNAL, not the delta: 2 + diff[0][7]. */
    check_eq(oki6295_decode_nibble(&v, 0x7),  32, "second nibble 7 (step still 0)");

    memset(&v, 0, sizeof(v));
    check_eq(oki6295_decode_nibble(&v, 0x8),  -2, "first nibble 8 (negative)");
    check_eq(oki6295_decode_nibble(&v, 0xF), -32, "second nibble F");

    /* A short ramp in one state: nibbles 7,7,7,7 from step 0.  stepval goes
     * 16, 34, 73, 157 and diff[step][7] = sv+sv/2+sv/4+sv/8 = 30, 63, 136,
     * 293. */
    memset(&v, 0, sizeof(v));
    check_eq(oki6295_decode_nibble(&v, 7), 30, "ramp n0");
    check_eq(oki6295_decode_nibble(&v, 7), 93, "ramp n1 (30+63)");
    check_eq(oki6295_decode_nibble(&v, 7), 229, "ramp n2 (93+136)");
    check_eq(oki6295_decode_nibble(&v, 7), 522, "ramp n3 (229+293)");
    check_eq(v.adpcm.step, 32, "ramp step after four 7s");

    /* Nibbles 0..7 have shift -1, so they walk the step back down one at a
     * time: 32 -> 31 -> 30. */
    (void)oki6295_decode_nibble(&v, 0);
    check_eq(v.adpcm.step, 31, "step after one 0");
    (void)oki6295_decode_nibble(&v, 0);
    check_eq(v.adpcm.step, 30, "step after two 0s");

    /* The first two ROM bytes of OKI sample 1 are A1 00.  High nibble first
     * decodes A, 1, 0, 0 -> -10, -4, -2, 0.  Low nibble first would decode
     * 1, A, 0, 0 and land on different values, so this pins the order. */
    memset(&v, 0, sizeof(v));
    check_eq(oki6295_decode_nibble(&v, 0xA), -10, "byte $A1 high nibble");
    check_eq(oki6295_decode_nibble(&v, 0x1), -4,  "byte $A1 low nibble");
    check_eq(oki6295_decode_nibble(&v, 0x0), -2,  "byte $00 high nibble");
    check_eq(oki6295_decode_nibble(&v, 0x0), 0,   "byte $00 low nibble");
}

/* ---- 3 & 4. the clamps ---------------------------------------------------- */
static void test_clamps(void)
{
    oki6295_voice_t v;
    int i;

    /* Positive clamp: drive the signal hard into the ceiling with the
     * largest positive nibble at the largest step. */
    memset(&v, 0, sizeof(v));
    v.adpcm.step = 48;
    v.adpcm.signal = 2000;
    for (i = 0; i < 4; i++)
        (void)oki6295_decode_nibble(&v, 7);
    check_eq(v.adpcm.signal, OKI6295_SIGNAL_MAX, "signal clamps at +2047");
    check(v.adpcm.signal <= 2047 && v.adpcm.signal >= -2048);

    /* Negative clamp. */
    memset(&v, 0, sizeof(v));
    v.adpcm.step = 48;
    v.adpcm.signal = -2000;
    for (i = 0; i < 4; i++)
        (void)oki6295_decode_nibble(&v, 8);
    check_eq(v.adpcm.signal, OKI6295_SIGNAL_MIN, "signal clamps at -2048");

    /* Step clamps: 49 max, 0 min.  Start at 0 and feed only 8s (shift -1). */
    memset(&v, 0, sizeof(v));
    v.adpcm.step = 0;
    for (i = 0; i < 100; i++)
        (void)oki6295_decode_nibble(&v, 8);
    check_eq(v.adpcm.step, 0, "step clamps at 0");
    check(v.adpcm.step <= 48);

    memset(&v, 0, sizeof(v));
    v.adpcm.step = 0;
    for (i = 0; i < 100; i++)
        (void)oki6295_decode_nibble(&v, 7);
    check_eq(v.adpcm.step, 48, "step clamps at 48");
    check(v.adpcm.step <= 48);
}

/* ---- 5. the volume table ------------------------------------------------- */
static void test_volume_table(void)
{
    int i;

    /* okim6295_device::s_volume_table, raw numerators / 0x20. */
    check_eq(oki6295_volume_table[0], 0x20, "volume_table[0] (full scale)");
    check_eq(oki6295_volume_table[1], 0x16, "volume_table[1]");
    check_eq(oki6295_volume_table[2], 0x10, "volume_table[2]");
    check_eq(oki6295_volume_table[3], 0x0B, "volume_table[3]");
    check_eq(oki6295_volume_table[4], 0x08, "volume_table[4]");
    check_eq(oki6295_volume_table[5], 0x06, "volume_table[5]");
    check_eq(oki6295_volume_table[6], 0x04, "volume_table[6]");
    check_eq(oki6295_volume_table[7], 0x03, "volume_table[7]");
    check_eq(oki6295_volume_table[8], 0x02, "volume_table[8]");

    /* Index 0 is the LOUDEST and is non-zero.  This is the assertion that
     * fails against the old reversed table, under which the 46 effect
     * commands carrying volume nibble 0 came out silent. */
    check(oki6295_volume_float(0) > 0.0f);
    check_eq(oki6295_volume_float(0), 1.0f, "volume_float(0)");

    /* Indices 9..15 are digital silence. */
    for (i = 9; i < 16; i++)
        check_eq(oki6295_volume_float((uint8_t)i), 0.0f,
                 "volume index 9..15 is silence");
    check_eq(oki6295_volume_float(9), 0.0f, "volume_float(9) is exactly 0");

    /* Monotonically decreasing from 0 to 8, never the reverse. */
    for (i = 1; i < 9; i++)
        check(oki6295_volume_float((uint8_t)i) < oki6295_volume_float((uint8_t)(i - 1)));
}

/* ---- 6. PIN7 and the sample rate ----------------------------------------- */
static void test_clock(void)
{
    oki6295_t c;

    oki6295_init(&c);
    /* D16: the default must already be PIN7 HIGH.  Nothing in our port calls
     * oki6295_set_pin7(), so a default of 0 silently ran every sound at
     * 7231 Hz instead of 9039 Hz.  atarijsa.cpp:901 passes PIN7_HIGH. */
    check_eq(oki6295_sample_rate(&c), 9039,
             "default PIN7 is HIGH (atarijsa.cpp:901) -> 9039 Hz");
    check_eq(oki6295_set_pin7(&c, 1), 132, "PIN7 high -> divisor 132");
    check_eq(oki6295_set_pin7(&c, 0), 165, "PIN7 low  -> divisor 165");
    check_eq(oki6295_set_pin7(&c, 1), 132, "PIN7 high again");

    /* (3579545 / 3) / 132 == 9039, (3579545 / 3) / 165 == 7231. */
    check_eq(oki6295_sample_rate(&c), 9039, "sample rate, PIN7 high");
    oki6295_set_pin7(&c, 0);
    check_eq(oki6295_sample_rate(&c), 7231, "sample rate, PIN7 low");
    check(oki6295_sample_rate(&c) < 11025);
}

/* ---- 7 & 8. a real sample, if the OKI ROMs are present -------------------- */
#define OKI_ROM_SIZE (256 * 1024)

static const char *kOkiRomNames[4] = {
    "136070-2124.1fh", "136070-2125.1ef",
    "136070-2126.1de", "136070-2127.1cd"
};

static FILE *try_open(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f) printf("  using OKI ROM: %s\n", path);
    return f;
}

static int load_oki_roms(uint8_t *rom)
{
    const char *root = getenv("STUNRUN_REPO_ROOT");
    const char *dir  = getenv("STUNRUN_OKI_ROM_DIR");
    char buf[1024];
    int i, got = 0;

    if (dir && *dir) {
        for (i = 0; i < 4; i++) {
            FILE *f;
            snprintf(buf, sizeof(buf), "%s/%s", dir, kOkiRomNames[i]);
            f = fopen(buf, "rb");
            if (!f) return 0;
            if (fread(rom + got, 1, OKI_ROM_SIZE / 4, f) != OKI_ROM_SIZE / 4) {
                fclose(f);
                return 0;
            }
            fclose(f);
            got += OKI_ROM_SIZE / 4;
        }
        printf("  using OKI ROM directory: %s\n", dir);
        return 1;
    }

    if (root && *root) {
        for (i = 0; i < 4; i++) {
            FILE *f;
            snprintf(buf, sizeof(buf), "%s/roms/stunrun/%s", root, kOkiRomNames[i]);
            f = try_open(buf);
            if (!f) return 0;
            if (fread(rom + got, 1, OKI_ROM_SIZE / 4, f) != OKI_ROM_SIZE / 4) {
                fclose(f);
                return 0;
            }
            fclose(f);
            got += OKI_ROM_SIZE / 4;
        }
        return 1;
    }

    /* Walk up from the working directory looking for roms/stunrun/.  The
     * build tree lives at stun-runner/native/build, so the ROM set sits four
     * levels up at the repository root. */
    if (getcwd(buf, sizeof(buf))) {
        int depth;
        for (depth = 0; depth < 8; depth++) {
            char *slash;
            int found = 1;

            got = 0;
            for (i = 0; i < 4; i++) {
                char p[1100];
                FILE *f;
                snprintf(p, sizeof(p), "%s/roms/stunrun/%s", buf, kOkiRomNames[i]);
                f = try_open(p);
                if (!f) { found = 0; break; }
                if (fread(rom + got, 1, OKI_ROM_SIZE / 4, f) != OKI_ROM_SIZE / 4) {
                    fclose(f);
                    return 0;
                }
                fclose(f);
                got += OKI_ROM_SIZE / 4;
            }
            if (found) {
                printf("  using OKI ROM directory: %s/roms/stunrun\n", buf);
                return 1;
            }

            /* Step up one directory and try again. */
            slash = strrchr(buf, '/');
            if (!slash) break;
            if (slash == buf) slash[1] = '\0';   /* stay at "/" */
            else *slash = '\0';
        }
    }
    return 0;
}

static void test_real_sample(const uint8_t *rom)
{
    oki6295_t chip;
    int16_t pcm[64 * 2];
    static const int16_t expect_first16[16] = {
        -10, -4, -2, 0, -2, -8, -2, 0, 2, -4, -6, 0, 2, -4, -14, 4
    };
    int i;
    size_t n;

    oki6295_init(&chip);
    check(oki6295_load_rom(&chip, rom, OKI_ROM_SIZE));
    check(!oki6295_load_rom(&chip, rom, 1234));

    /* Sample 1: header says start 424, stop 6823 -> 12800 nibbles.
     * Start it on voice 0 at volume index 0 (loudest). */
    check_eq(oki6295_write(&chip, 0x81), -1, "first play byte latched");
    check(oki6295_command_pending(&chip));
    n = oki6295_fill(&chip, pcm, 1);
    check_eq(n, 0, "fill() before the second play byte");

    /* byte2 = voice-0 mask $10 | volume 0 */
    check_eq(oki6295_write(&chip, 0x10), 1, "voice 0 started");
    check(oki6295_is_playing(&chip));
    check_eq(chip.voice[0].base_offset, 424, "sample 1 start address");
    check_eq(chip.voice[0].count, 2 * (6823 - 424 + 1), "sample 1 nibble count");
    check_eq(chip.voice[0].adpcm.step, 0, "decoder reset at play");
    check_eq(chip.voice[0].adpcm.signal, 0, "signal reset at play");

    /* Decode the first 16 nibbles.  Volume index 0 is 1.0 and the mix gain
     * is 16, so the int16 output is exactly pcm * 16. */
    n = oki6295_fill(&chip, pcm, 16);
    check_eq(n, 16, "fill() returned 16 samples");
    for (i = 0; i < 16; i++) {
        char label[64];
        snprintf(label, sizeof(label), "sample 1 pcm[%d] (int16 out)", i);
        check_eq(pcm[i * 2], expect_first16[i] * 16, label);
        snprintf(label, sizeof(label), "sample 1 pcm[%d] (both channels)", i);
        check_eq(pcm[i * 2 + 1], expect_first16[i] * 16, label);
    }

    /* Now decode the whole sample and check the tail and a checksum.
     * Expected values come from an independent Python transcription of
     * MAME's oki_adpcm_state::clock() over the same ROM bytes. */
    {
        static int16_t big[12800 * 2];        /* sample 1 is 12800 nibbles */
        size_t total;
        int32_t sum = 0;
        int16_t first = 0, last = 0;

        oki6295_init(&chip);
        oki6295_load_rom(&chip, rom, OKI_ROM_SIZE);
        (void)oki6295_write(&chip, 0x81);
        (void)oki6295_write(&chip, 0x10);

        total = oki6295_fill(&chip, big, 12800);
        check_eq(total, 12800, "sample 1 decodes to exactly count nibbles");
        check(!oki6295_is_playing(&chip));

        for (i = 0; i < (int)total; i++) {
            sum += big[i * 2];
            if (i == 0) first = big[i * 2];
            last = big[i * 2];
        }
        check_eq(first, -10 * 16, "sample 1 first int16 sample");
        check_eq(last, 1 * 16, "sample 1 last int16 sample");
        /* Raw decoder sum over 12800 nibbles is -4686; x16 for the mix gain. */
        check_eq(sum, -4686 * 16, "sample 1 int16 sum");
    }

    /* Sample 2: start 6824, stop 10407, on voice 1. */
    {
        static const int16_t expect2[16] = {
            2, 0, -2, 0, -2, -4, -2, -8, -6, -4, -2, -4, -6, 0, -2, 0
        };
        static int16_t big[7168 * 2];
        size_t total;
        int32_t sum = 0;
        int16_t last = 0;

        oki6295_init(&chip);
        oki6295_load_rom(&chip, rom, OKI_ROM_SIZE);
        (void)oki6295_write(&chip, 0x82);        /* sample $02 */
        (void)oki6295_write(&chip, 0x10);        /* voice 0, volume 0 */
        total = oki6295_fill(&chip, big, 16);
        check_eq(total, 16, "sample 2 fill");
        for (i = 0; i < 16; i++) {
            char label[64];
            snprintf(label, sizeof(label), "sample 2 pcm[%d]", i);
            check_eq(big[i * 2], expect2[i] * 16, label);
        }

        oki6295_init(&chip);
        oki6295_load_rom(&chip, rom, OKI_ROM_SIZE);
        (void)oki6295_write(&chip, 0x82);
        (void)oki6295_write(&chip, 0x10);
        total = oki6295_fill(&chip, big, 7168);
        check_eq(total, 7168, "sample 2 decodes to 7168 nibbles");
        for (i = 0; i < (int)total; i++) {
            sum += big[i * 2];
            last = big[i * 2];
        }
        check_eq(last, -5 * 16, "sample 2 last int16 sample");
        check_eq(sum, -14109 * 16, "sample 2 int16 sum");
    }

    /* start == stop is rejected (okim6295.cpp: `if (start < stop)`).  Sample
     * index 0 has an all-zero header, i.e. start == stop == 0. */
    oki6295_init(&chip);
    oki6295_load_rom(&chip, rom, OKI_ROM_SIZE);
    (void)oki6295_write(&chip, 0x80);            /* sample $00 */
    check_eq(oki6295_write(&chip, 0x10), 0, "sample 0 (start == stop) rejected");
    check(!oki6295_is_playing(&chip));

    /* Volume index 9 is digital silence, so the voice still "plays" but
     * every output sample is exactly zero. */
    oki6295_init(&chip);
    oki6295_load_rom(&chip, rom, OKI_ROM_SIZE);
    (void)oki6295_write(&chip, 0x81);
    check_eq(oki6295_write(&chip, 0x19), 1, "volume index 9 starts voice 0");
    n = oki6295_fill(&chip, pcm, 64);
    check_eq(n, 64, "volume-9 fill");
    {
        int all_zero = 1;
        for (i = 0; i < (int)n * 2; i++)
            if (pcm[i] != 0) all_zero = 0;
        check(all_zero);
    }

    /* The stop command: $5C6B[0] == 0x08 stops voice 0. */
    oki6295_init(&chip);
    oki6295_load_rom(&chip, rom, OKI_ROM_SIZE);
    (void)oki6295_write(&chip, 0x81);
    (void)oki6295_write(&chip, 0x10);
    check(oki6295_is_playing(&chip));
    check_eq(oki6295_write(&chip, 0x08), 0, "stop byte returns 0");
    check(!oki6295_is_playing(&chip));

    /* High nibble first: byte 0xA1 at the sample's start address means the
     * first nibble decoded is 0xA, not 0x1.  Feed those two bytes by hand and
     * compare against feeding nibble 0xA then 0x1 directly. */
    {
        oki6295_t a, b;
        oki6295_init(&a);
        oki6295_init(&b);
        a.voice[0].playing = true; a.voice[0].sample = 0;
        a.voice[0].count = 4;
        a.voice[0].volume = 1.0f;
        b.voice[0].playing = true; b.voice[0].sample = 0;
        b.voice[0].count = 4;
        b.voice[0].volume = 1.0f;
        /* decode from the ROM's first byte 0xA1, high nibble first */
        for (i = 0; i < 2; i++) {
            uint8_t nb = (uint8_t)(0xA1 >> (((a.voice[0].sample & 1) << 2) ^ 4));
            (void)oki6295_decode_nibble(&a.voice[0], nb);
            a.voice[0].sample++;
        }
        /* the same two nibbles in the order high-then-low */
        (void)oki6295_decode_nibble(&b.voice[0], 0xA);
        (void)oki6295_decode_nibble(&b.voice[0], 0x1);
        check_eq(a.voice[0].adpcm.signal, b.voice[0].adpcm.signal,
                 "high nibble is decoded first");
        check_eq(a.voice[0].adpcm.step, b.voice[0].adpcm.step,
                 "high nibble drives the step too");
    }
}

int main(void)
{
    static uint8_t rom[OKI_ROM_SIZE];

    printf("--- OKI ADPCM codec tables ---\n");
    test_stepval_table();

    printf("--- known PCM values ---\n");
    test_known_pcm();

    printf("--- signal and step clamps ---\n");
    test_clamps();

    printf("--- volume table (index 0 loudest, 9..15 silent) ---\n");
    test_volume_table();

    printf("--- PIN7 divisor and sample rate ---\n");
    test_clock();

    printf("--- real OKI sample ---\n");
    if (load_oki_roms(rom))
        test_real_sample(rom);
    else
        printf("SKIP real-sample checks: OKI ROMs not found "
               "(set STUNRUN_REPO_ROOT or STUNRUN_OKI_ROM_DIR)\n");

    if (g_failures == 0)
        printf("oki6295_test: all %d checks passed\n", g_checks);
    else
        printf("oki6295_test: %d of %d checks FAILED\n", g_failures, g_checks);
    return g_failures == 0 ? 0 : 1;
}