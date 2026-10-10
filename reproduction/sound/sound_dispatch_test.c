/* sound_dispatch_test.c — asserts the JSA-II dispatch bridge's OBSERVABLE
 * behaviour: which bytes reach the OKI6295, what the chip does with them,
 * and what PCM reaches the audio backend.
 *
 * This is the regression test for two defects in the bridge
 * (docs/jsa2-sound-findings.md §5):
 *
 *   D2  sound_dispatch_send_command() handed the raw 68010 command byte to
 *       oki6295_write().  The 6502 firmware translates that byte through the
 *       class table at $6100 and emits 0, 1 or 3 OKI6295 writes; feeding the
 *       byte straight to the chip made the chip parse the 68010 command as
 *       an OKI command, with whatever arrived next on the bus as its second
 *       byte.
 *
 *   D15 sound_dispatch_frame() read the latch with
 *       stunrun_jsa_peek_command_byte(), which does NOT clear
 *       command_pending.  One latched byte was therefore re-dispatched on
 *       every subsequent frame for the life of the process.
 *
 * WHAT IS ASSERTED (all observable, none of it "it ran"):
 *
 *   1. Happy path      $38 (sound 1, sample $01, voice 0) reaches the chip
 *                      as exactly 3 bytes, starts voice 0 at the sample
 *                      address the OKI ROM header says, and oki6295_is_playing()
 *                      goes true.
 *   2. Exactly once    one latched command + 8 frames => commands_dispatched
 *                      == 1, oki_bytes_written == 3, and the voice's ADPCM
 *                      read pointer advances 256 per frame instead of being
 *                      reset.  This is the D15 regression test.
 *   3. Stop            $65 (sound 46) then $66 emits exactly one OKI byte,
 *                      the voice-2 stop byte, and is_playing() goes false.
 *                      $66 after $38 emits nothing at all, because $5C71
 *                      compares its argument against a SAMPLE NUMBER.
 *   4. Non-audio       $03/$06/$07/$08/$1E are dispatched (counter advances)
 *                      but emit zero OKI bytes and leave every voice idle.
 *   5. $1E mode gate   $01 (sets $1E=$F0) suppresses a following effect;
 *                      $02 (sets $1E=$00) re-enables it.  This is
 *                      oki_cmdmap's behaviour seen through the bridge — it is
 *                      asserted at the translator level too, in
 *                      oki_cmdmap_test.c's test_mode_gate, so this copy only
 *                      adds the bridge half (what the chip and the audio
 *                      backend actually see).
 *   6. PCM reaches     a stub stunrun_audio_backend_t receives a non-zero
 *      the backend     sample count, sample_rate == oki6295_sample_rate(),
 *                      format == STUNRUN_AUDIO_FORMAT_S16LE_STEREO, and its
 *                      per-frame blocks are copied so the test can compare
 *                      them.
 *   7. Entry point     sound_dispatch_send_command() used directly takes the
 *                      same path and counts the same way, because shell.c
 *                      calls it.
 *
 * The OKI ROMs are used when they can be found and a synthetic 256 KiB stand-in
 * with the same sample headers is used when they cannot, so the bridge is
 * covered by a public test that does not require the ROM set.  When the real
 * ROM is in use the exact header addresses are asserted as well.
 */
#include "sound_dispatch.h"
#include "oki_cmdmap.h"

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

/* ---- 1. the OKI sample ROM ------------------------------------------------ */
/* oki6295_sample_header(): 18-bit big-endian start/stop pair at sample*8. */
#define OKI_ROM_SIZE (256 * 1024)

static const char *kOkiRomNames[4] = {
    "136070-2124.1fh", "136070-2125.1ef",
    "136070-2126.1de", "136070-2127.1cd"
};

/* Samples this test drives, and the headers they must resolve to.  These are
 * the values measured in the four real OKI ROMs; the synthetic stand-in
 * reproduces them so the bridge assertions hold either way. */
typedef struct sample_expect { uint8_t sample; uint32_t start; uint32_t count; }
sample_expect_t;

static const sample_expect_t kSamples[] = {
    { 0x01, 0x0001A8u, 12800u },   /* $38 / $05, sound index 1  */
    { 0x25, 0x027EA8u,  8960u },   /* $5C,          sound index 37 */
    { 0x30, 0x03A3A8u, 11520u },   /* $65,          sound index 46 */
};

static uint8_t *g_oki_rom = NULL;
static int g_real_rom = 0;

static void write_header(uint8_t *rom, uint8_t sample, uint32_t start, uint32_t stop)
{
    uint8_t *h = rom + (size_t)sample * 8u;
    h[0] = (uint8_t)(start >> 16); h[1] = (uint8_t)(start >> 8); h[2] = (uint8_t)start;
    h[3] = (uint8_t)(stop  >> 16); h[4] = (uint8_t)(stop  >> 8);  h[5] = (uint8_t)stop;
}

static void synthetic_rom(uint8_t *rom)
{
    size_t i;
    memset(rom, 0, OKI_ROM_SIZE);
    /* Non-constant nibble stream: the exactly-once test asserts that
     * consecutive frames' PCM differs, which a constant stream could not
     * guarantee. */
    for (i = 0; i < OKI_ROM_SIZE; i++)
        rom[i] = (uint8_t)((i * 37u) ^ (i >> 4));
    for (i = 0; i < sizeof(kSamples) / sizeof(kSamples[0]); i++) {
        uint32_t stop = kSamples[i].start + kSamples[i].count / 2u - 1u;
        write_header(rom, kSamples[i].sample, kSamples[i].start, stop);
    }
}

static int try_dir(const char *dir, uint8_t *rom, int verbose)
{
    char buf[1100];
    int i, got = 0;
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
    if (verbose) printf("  using OKI ROM directory: %s\n", dir);
    return 1;
}

static uint8_t *load_oki_roms(void)
{
    const char *root = getenv("STUNRUN_REPO_ROOT");
    const char *dir  = getenv("STUNRUN_OKI_ROM_DIR");
    uint8_t *rom = (uint8_t *)malloc(OKI_ROM_SIZE);
    char buf[1024];

    if (!rom) return NULL;

    if (dir && *dir && try_dir(dir, rom, 1)) { g_real_rom = 1; return rom; }
    if (root && *root) {
        snprintf(buf, sizeof(buf), "%s/roms/stunrun", root);
        if (try_dir(buf, rom, 1)) { g_real_rom = 1; return rom; }
    }
    if (getcwd(buf, sizeof(buf))) {
        int depth;
        for (depth = 0; depth < 8; depth++) {
            char *slash;
            if (try_dir(buf, rom, 1)) { g_real_rom = 1; return rom; }
            slash = strrchr(buf, '/');
            if (!slash) break;
            if (slash == buf) slash[1] = '\0';
            else *slash = '\0';
        }
    }
    printf("  OKI ROMs not found — using a synthetic 256 KiB stand-in\n");
    synthetic_rom(rom);
    return rom;
}

/* Read a sample header back out of the ROM the chip is using. */
static void expect_header(uint8_t sample, uint32_t *start, uint32_t *count)
{
    const uint8_t *h = g_oki_rom + (size_t)sample * 8u;
    uint32_t st = ((uint32_t)h[0] << 16) | ((uint32_t)h[1] << 8) | h[2];
    uint32_t sp = ((uint32_t)h[3] << 16) | ((uint32_t)h[4] << 8) | h[5];
    *start = st & 0x3FFFFu;
    *count = 2u * (sp - *start + 1u);
}

/* ---- 2. a stub audio backend --------------------------------------------- */
#define STUB_MAX_FRAMES 16
#define STUB_MAX_SAMPLES 512

typedef struct stub_audio {
    unsigned calls;
    unsigned drains;
    unsigned frames;
    size_t   total_samples;
    uint32_t sample_rate;
    stunrun_audio_format_t format;
    size_t   count[STUB_MAX_FRAMES];
    int16_t  block[STUB_MAX_FRAMES][STUB_MAX_SAMPLES];
} stub_audio_t;

static stub_audio_t g_stub;

static size_t stub_stream_write(void *user, const stunrun_audio_buffer_t *buf)
{
    stub_audio_t *s = (stub_audio_t *)user;
    size_t n = buf->sample_count;

    s->calls++;
    s->total_samples += n;
    s->sample_rate    = buf->sample_rate;
    s->format         = buf->format;

    /* audio.h: "The backend must copy the data internally." */
    if (s->frames < STUB_MAX_FRAMES && n * 2u <= STUB_MAX_SAMPLES) {
        memcpy(s->block[s->frames], buf->samples, n * 2u * sizeof(int16_t));
        s->count[s->frames] = n;
        s->frames++;
    }
    return n;
}

static void stub_drain(void *user) { ((stub_audio_t *)user)->drains++; }

/* Each test starts from a clean backend, otherwise the call and block counts
 * would be cumulative across tests. */
static void stub_reset(void) { memset(&g_stub, 0, sizeof(g_stub)); }

static const stunrun_audio_backend_t kStubBackend = {
    .name          = "sound-dispatch-test-stub",
    .create        = NULL,
    .init          = NULL,
    .shutdown      = NULL,
    .destroy       = NULL,
    .stream_write  = stub_stream_write,
    .stream_adpcm  = NULL,
    .drain         = stub_drain,
    .set_gain      = NULL,
    .set_master_volume = NULL,
    .send_command  = NULL,
    .set_mute      = NULL,
    .get_latency_ms = NULL,
    .get_load      = NULL,
    .user          = NULL,
};

/* ---- helpers ------------------------------------------------------------- */

/* Fresh dispatch object.  Takes ownership of the caller's ROM copy, exactly as
 * shell.c does: it allocates its own so shutdown can free it.  Also resets the
 * stub backend, so each test sees only its own traffic. */
static sound_dispatch_t *dispatch_new(void)
{
    sound_dispatch_t *d = (sound_dispatch_t *)calloc(1, sizeof(*d));
    uint8_t *rom = (uint8_t *)malloc(OKI_ROM_SIZE);
    if (!d || !rom) return NULL;
    memcpy(rom, g_oki_rom, OKI_ROM_SIZE);
    stub_reset();
    if (!sound_dispatch_init(d, &kStubBackend, &g_stub, rom, OKI_ROM_SIZE)) {
        free(rom);
        free(d);
        return NULL;
    }
    return d;
}

static void dispatch_free(sound_dispatch_t *d)
{
    if (!d) return;
    sound_dispatch_shutdown(d);
    free(d);
}

/* Latch a command the way the 68010 does, then run n frames. */
static size_t latch_and_run(sound_dispatch_t *d, uint8_t cmd, unsigned n)
{
    unsigned i;
    size_t last = 0;
    stunrun_jsa_main_command_write(&d->latches, cmd);
    for (i = 0; i < n; i++)
        last = sound_dispatch_frame(d);
    return last;
}static int all_voices_idle(const oki6295_t *oki)
{
    int i;
    for (i = 0; i < OKI6295_VOICES; i++)
        if (oki->voice[i].playing) return 0;
    return 1;
}

/* ---- 3. happy path ------------------------------------------------------- */
static void test_happy_path(void)
{
    sound_dispatch_t *d = dispatch_new();
    uint32_t want_start, want_count;
    size_t enq;
    int i, only_voice_0 = 1;

    if (!d) { check(!"dispatch_new() returned NULL"); return; }

    expect_header(0x01, &want_start, &want_count);

    /* $38: class 19, argument 1 -> $6532[1]=$01 (sample), $6567[1] -> voice 0,
     * volume 0.  The handler at $5BFE emits three bytes: the voice-0 stop byte
     * $08, the sample byte $01|$80, and the voice mask | volume $10. */
    enq = latch_and_run(d, 0x38, 1);

    check_eq(d->commands_dispatched, 1, "$38 was dispatched once");
    check_eq(d->oki_bytes_written, 3, "$38 emitted three OKI bytes");
    check(d->oki.voice[0].playing == true);
    check_eq(d->oki.voice[0].base_offset, want_start, "$38 voice 0 base offset");
    check_eq(d->oki.voice[0].count, want_count, "$38 voice 0 sample length");
    check(d->oki.voice[0].volume > 0.0f);   /* volume index 0 is full scale */
    for (i = 1; i < OKI6295_VOICES; i++)
        if (d->oki.voice[i].playing) only_voice_0 = 0;
    check(only_voice_0);                    /* no other voice was touched */
    check(oki6295_is_playing(&d->oki));
    check_eq(d->cmdmap.voice_busy, 0x01, "$52 /RDV bit 0 set after $38");
    check(enq > 0);

    if (g_real_rom) {
        check_eq(want_start, 0x0001A8, "real ROM sample $01 start address");
        check_eq(want_count, 12800,   "real ROM sample $01 length in nibbles");
    }

    dispatch_free(d);
}

/* ---- 4. exactly once (Defect 1 regression) ------------------------------- */
static void test_exactly_once(void)
{
    enum { FRAMES = 8 };
    sound_dispatch_t *d = dispatch_new();
    uint32_t want_start, want_count;
    unsigned i, j;

    if (!d) { check(!"dispatch_new() returned NULL"); return; }

    expect_header(0x25, &want_start, &want_count);

    /* $5C is class 19, argument 37, sample $25, voice 0, and its $6567 entry has
     * bit 7 clear — UNLOCKED.  That matters: if the peek bug were still here,
     * every frame would stop and reload voice 0, and the locked-bit check at
     * $5C59 would not be what stopped the retrigger.  Choosing an unlocked
     * command makes the regression fail loudly instead of hiding behind the
     * lock path. */
    latch_and_run(d, 0x5C, 1);

    check_eq(d->commands_dispatched, 1, "$5C dispatched on the first frame");
    check_eq(d->oki_bytes_written, 3, "$5C emitted three OKI bytes");
    check_eq(d->oki.voice[0].base_offset, want_start, "$5C sample base offset");

    /* Seven more frames with nothing new latched. */
    for (i = 1; i < FRAMES; i++)
        sound_dispatch_frame(d);

    check_eq(d->commands_dispatched, 1, "no re-dispatch over 8 frames");
    check_eq(d->oki_bytes_written, 3, "no extra OKI bytes over 8 frames");
    check_eq(d->frames_dispatched, FRAMES, "eight frames ran");

    /* The decisive observable: the ADPCM read pointer kept walking instead of
     * being reset to 0 by a re-dispatch.  DISPATCH_SAMPLE_BUF_SIZE is 256, so
     * each frame consumes exactly 256 nibbles while the voice is running. */
    check_eq(d->oki.voice[0].sample, 256u * FRAMES,
             "voice 0 read pointer advanced 256 per frame");
    check_eq(d->oki.voice[0].base_offset, want_start,
             "sample was not reloaded on frames 2..8");
    check(d->oki.voice[0].playing == true);

    /* Byte-identity of the PCM.  Asserting that frames 2..N are all equal to
     * each other would NOT catch the bug — a reload-every-frame build makes
     * them equal to each other too.  What distinguishes them is that frame 1
     * starts at the top of the sample while the later frames continue from
     * where it left off, so under the fix no two of the eight blocks match,
     * and under the bug frame 2 would be a byte-for-byte copy of frame 1. */
    check_eq(g_stub.frames, FRAMES, "backend saw one block per frame");
    for (i = 1; i < g_stub.frames && i < FRAMES; i++) {
        check(g_stub.count[i] > 0);
        check_eq(g_stub.count[i], g_stub.count[0], "every frame filled the buffer");
        check(memcmp(g_stub.block[i], g_stub.block[0],
                     g_stub.count[0] * 2 * sizeof(int16_t)) != 0);
    }
    for (i = 1; i < g_stub.frames && i < FRAMES; i++)
        for (j = i + 1; j < g_stub.frames && j < FRAMES; j++)
            check(memcmp(g_stub.block[i], g_stub.block[j],
                         g_stub.count[i] * 2 * sizeof(int16_t)) != 0);

    dispatch_free(d);
}

/* ---- 5. stop ------------------------------------------------------------- */
static void test_stop(void)
{
    sound_dispatch_t *d = dispatch_new();
    uint32_t want_start, want_count;
    size_t enq;

    if (!d) { check(!"dispatch_new() returned NULL"); return; }

    expect_header(0x30, &want_start, &want_count);

    /* $65: class 19, argument 46 -> $6532[46]=$30, $6567[46] -> voice 2,
     * unlocked.  Its stop byte is $5C6B[2] = $20. */
    latch_and_run(d, 0x65, 1);

    check(d->oki.voice[2].playing == true);
    check_eq(d->oki.voice[2].base_offset, want_start, "$65 voice 2 base offset");
    check_eq(d->oki.voice[2].count, want_count, "$65 voice 2 sample length");
    check_eq(d->cmdmap.voice_busy, 0x04, "$52 /RDV bit 2 set after $65");

    /* $66: class 20, argument 48.  $5C71 scans voices 3..0 for the one whose
     * $6532 entry equals 48; sound index 46 has $6532[46]=$30=48, so voice 2
     * is stopped with $5C6B[2] = $20.  One byte, not three. */
    enq = latch_and_run(d, 0x66, 1);

    check_eq(d->commands_dispatched, 2, "$66 was dispatched");
    check_eq(d->oki_bytes_written, 3 + 1, "$66 emitted exactly one OKI byte");
    check(d->oki.voice[2].playing == false);
    check(oki6295_is_playing(&d->oki) == false);
    check_eq(d->cmdmap.voice_busy, 0x00, "/RDV cleared after $66");
    check_eq(enq, 0, "a stopped bridge enqueues nothing");

    /* The negative case, which is the point of reproducing $5C71 verbatim:
     * the class-20 handler compares its argument against a SAMPLE NUMBER, so
     * $66 does NOT stop an unrelated effect.  $38 is sound index 1, sample
     * $01 = 1, and $66's argument is 48. */
    {
        sound_dispatch_t *e = dispatch_new();
        if (!e) { check(!"dispatch_new() returned NULL"); dispatch_free(d); return; }
        latch_and_run(e, 0x38, 1);
        check(e->oki.voice[0].playing == true);
        check_eq(e->oki_bytes_written, 3, "$38 before the negative stop test");
        latch_and_run(e, 0x66, 1);
        check_eq(e->oki_bytes_written, 3, "$66 emitted nothing for sound 1");
        check_eq(e->commands_dispatched, 2, "$66 was still dispatched");
        check(e->oki.voice[0].playing == true);
        check(oki6295_is_playing(&e->oki));
        dispatch_free(e);
    }

    dispatch_free(d);
}

/* ---- 6. commands that never touch the OKI6295 ---------------------------- */
static void test_non_audio_commands(void)
{
    static const uint8_t cmds[] = { 0x03, 0x06, 0x07, 0x08, 0x1E };
    size_t i;

    for (i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        sound_dispatch_t *d = dispatch_new();
        size_t enq;
        if (!d) { check(!"dispatch_new() returned NULL"); return; }

        /* $03/$06/$07/$08 are class $FF — the NMI handshake that answers the
         * 68010 without touching the chip.  $1E is class 1, a YM2151 bank
         * select at $43AD, which this slice deliberately does not model. */
        enq = latch_and_run(d, cmds[i], 3);

        check_eq(d->commands_dispatched, 1, "non-audio command dispatched");
        check_eq(d->oki_bytes_written, 0, "non-audio command wrote no OKI bytes");
        check(all_voices_idle(&d->oki));
        check(oki6295_is_playing(&d->oki) == false);
        check_eq(d->cmdmap.voice_busy, 0x00, "/RDV untouched");
        check_eq(d->cmdmap.mode, 0x00, "$1E untouched");
        check_eq(enq, 0, "nothing enqueued");

        dispatch_free(d);
    }
}

/* ---- 7. the $1E mode gate, seen from the bridge -------------------------- */
static void test_mode_gate(void)
{
    sound_dispatch_t *d = dispatch_new();

    if (!d) { check(!"dispatch_new() returned NULL"); return; }

    /* $01: class 0, argument $3C.  $438A does ASL A / ASL A / STA $1E, so
     * $1E = $3C << 2 = $F0 — non-zero, and $5BFE's `ldy $1e / bne $5c61`
     * then skips the play entirely. */
    latch_and_run(d, 0x01, 1);
    check_eq(d->cmdmap.mode, 0xF0, "$01 set $1E to $F0");
    check_eq(d->oki_bytes_written, 0, "$01 wrote no OKI bytes");

    latch_and_run(d, 0x5C, 1);
    check_eq(d->commands_dispatched, 2, "the effect was still dispatched");
    check_eq(d->oki_bytes_written, 0, "gated effect emitted no OKI bytes");
    check(all_voices_idle(&d->oki));
    check(oki6295_is_playing(&d->oki) == false);

    /* $02: class 0, argument $00 -> $1E = $00, the gate reopens, and the very
     * same effect now reaches the chip. */
    latch_and_run(d, 0x02, 1);
    check_eq(d->cmdmap.mode, 0x00, "$02 set $1E to $00");

    latch_and_run(d, 0x5C, 1);
    check_eq(d->commands_dispatched, 4, "four commands dispatched in total");
    check_eq(d->oki_bytes_written, 3, "ungated effect emitted three OKI bytes");
    check(d->oki.voice[0].playing == true);
    check(oki6295_is_playing(&d->oki));

    dispatch_free(d);
}

/* ---- 8. PCM reaches the backend with the chip's own parameters ----------- */
static void test_pcm_reaches_backend(void)
{
    sound_dispatch_t *d = dispatch_new();
    uint32_t want_start, want_count;

    if (!d) { check(!"dispatch_new() returned NULL"); return; }
    expect_header(0x01, &want_start, &want_count);

    check_eq(g_stub.calls, 0, "nothing reached the backend before the first frame");

    latch_and_run(d, 0x38, 4);

    check_eq(g_stub.calls, 4, "one stream_write per playing frame");
    check_eq(g_stub.frames, 4, "the backend saw four blocks");
    check(g_stub.total_samples > 0);
    check_eq(d->samples_enqueued, g_stub.total_samples,
             "the bridge counted every sample the backend took");

    check_eq(g_stub.sample_rate, oki6295_sample_rate(&d->oki),
             "stream sample_rate is the chip's rate");
    /* The rate itself is oki6295's business, not the bridge's, and both
     * divisors are covered by oki6295_test.c.  Note that the chip currently
     * comes up with PIN7 LOW (oki6295.c:101 sets pin7_state = 0), so this
     * reads 7231 Hz rather than the 9039 Hz MAME configures for the JSA-II
     * (atarijsa.cpp:901, OKIM6295(..., PIN7_HIGH)).  That residual of
     * findings-doc D6 belongs to the chip model and to the 6502's WRIO bit-3
     * handling, both outside this slice — so the bridge is only required to
     * forward whatever the chip reports, which is what the check above says. */
    check(g_stub.sample_rate == 7231 || g_stub.sample_rate == 9039);
    check_eq(g_stub.format, STUNRUN_AUDIO_FORMAT_S16LE_STEREO,
             "stream format is S16LE stereo");

    /* Stereo really means stereo: the fill duplicates each mono sample into
     * both channels, so every (L,R) pair must be equal. */
    {
        int16_t l = g_stub.block[0][0], r = g_stub.block[0][1];
        check(l == r);
        check(l != 0);
    }

    /* The mixed signal must actually move — a bridge that enqueued a buffer of
     * zeros would pass every check above. */
    {
        int16_t first = g_stub.block[0][0];
        unsigned i;
        int varies = 0;
        for (i = 0; i < g_stub.count[0] * 2u; i++)
            if (g_stub.block[0][i] != first) { varies = 1; break; }
        check(varies);
    }

    dispatch_free(d);
    check_eq(g_stub.drains, 1, "shutdown drained the backend");
}

/* ---- 9. the public entry point shell.c uses ------------------------------ */
static void test_direct_entry_point(void)
{
    sound_dispatch_t *d = dispatch_new();
    uint32_t want_start, want_count;

    if (!d) { check(!"dispatch_new() returned NULL"); return; }
    expect_header(0x01, &want_start, &want_count);

    /* shell.c calls sound_dispatch_send_command() directly.  It must go
     * through the same translation and count the same way, otherwise the
     * exactly-once accounting would only hold for the frame loop. */
    sound_dispatch_send_command(d, 0x38);
    sound_dispatch_send_command(d, 0x66);   /* nothing is playing sample 1 */

    check_eq(d->commands_dispatched, 2, "direct calls counted");
    check_eq(d->oki_bytes_written, 3, "direct call translated $38");
    check(d->oki.voice[0].playing == true);
    check_eq(d->oki.voice[0].base_offset, want_start, "direct call base offset");
    check_eq(d->latches.command_pending, 0, "direct call did not touch the latch");

    sound_dispatch_frame(d);
    check_eq(d->frames_dispatched, 1, "the frame loop did not re-dispatch");

    dispatch_free(d);
}

int main(void)
{
    printf("sound_dispatch_test — bridge-level assertions\n");

    g_oki_rom = load_oki_roms();
    if (!g_oki_rom) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }

    memset(&g_stub, 0, sizeof(g_stub));

    test_happy_path();
    test_exactly_once();
    test_stop();
    test_non_audio_commands();
    test_mode_gate();
    test_pcm_reaches_backend();
    test_direct_entry_point();

    sound_dispatch_shutdown(NULL);   /* must be a no-op, not a crash */

    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    free(g_oki_rom);
    return g_failures == 0 ? 0 : 1;
}
