/* oki_cmdmap_test.c — asserts the JSA-II 68010 -> OKI6295 translation.
 *
 * WHAT IS ASSERTED
 *   1. Every transcribed table matches the ROM, byte for byte, when the ROM
 *      can be found.  The expected bytes are NOT typed into this file: they
 *      are read from 136070-2123.10c at the 6502 addresses named in the
 *      tables' comments, so the test fails if the transcription drifts.
 *      ROM file offset == 6502 address (MAME loads the region at 0x00000,
 *      length 0x10000; harddriv.cpp:2707).
 *   2. The full 53-row class-19 map from docs/jsa2-sound-findings.md §4,
 *      as literal expectations: class, argument, voice, sample, volume,
 *      lock flag, OKI byte1 and OKI byte2.
 *   3. Class 20 ($66) is a STOP, not a play, and stops sound index 46 --
 *      the voice holding sample $30 -- because the ROM compares its
 *      argument against a sample NUMBER ($5c75: cmp $6532,y), not against a
 *      sound index.  See oki_cmdmap.h and BUGS.md.
 *   4. The $1E mode gate: class-0 commands write $1E = argument << 2, and
 *      while $1E is nonzero every class-19 play is suppressed (zero OKI
 *      writes).  $1E = 0 re-enables them.
 *   5. The voice-contention rule at $5c14-$5c21: a play onto a busy voice
 *      is abandoned with zero writes when the sound already on that voice
 *      has bit 7 of $6567 set, and is preempted when it does not.
 *
 * ROM LOCATION
 *   The test looks for the 6502 ROM at, in order:
 *     $STUNRUN_JSA_ROM         (environment, absolute path)
 *     <repo>/roms/stunrun/136070-2123.10c   (walked up from the CWD)
 *     <repo>/../mame-roms/...              (walked up from the CWD)
 *   If it finds none, checks 2-5 still run against the transcribed
 *   constants, but check 1 is reported as SKIP.  So the test passes in a
 *   checkout with no ROMs, and is strictly stronger in one with them.
 */
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
        if (a_ == e_) printf("PASS %s == 0x%lX\n", (label), e_);              \
        else { printf("FAIL %s: got 0x%lX want 0x%lX\n", (label), a_, e_);    \
               g_failures++; }                                                \
    } while (0)

/* ---- one row of docs/jsa2-sound-findings.md section 4 ------------------- */
typedef struct expected_row {
    uint8_t command;
    uint8_t klass;
    uint8_t argument;
    uint8_t voice;
    uint8_t sample;
    uint8_t volume;
    int    locked;      /* 1 = bit 7 of $6567 set */
    uint8_t byte1;
    uint8_t byte2;
} expected_row_t;

static const expected_row_t kExpected[] = {
    /* cmd  cls arg  voice sample vol lock  byte1 byte2 */
    { 0x05, 0x13,  0,   0,  0x01,  1,  0,  0x81, 0x11 },
    { 0x38, 0x13,  1,   0,  0x01,  0,  1,  0x81, 0x10 },
    { 0x39, 0x13,  2,   0,  0x02,  0,  1,  0x82, 0x10 },
    { 0x3A, 0x13,  3,   0,  0x03,  0,  1,  0x83, 0x10 },
    { 0x3B, 0x13,  4,   0,  0x04,  0,  1,  0x84, 0x10 },
    { 0x3C, 0x13,  5,   0,  0x05,  0,  1,  0x85, 0x10 },
    { 0x3D, 0x13,  6,   0,  0x06,  0,  1,  0x86, 0x10 },
    { 0x3E, 0x13,  7,   0,  0x07,  0,  1,  0x87, 0x10 },
    { 0x3F, 0x13,  8,   0,  0x08,  0,  1,  0x88, 0x10 },
    { 0x40, 0x13,  9,   0,  0x20,  0,  1,  0xA0, 0x10 },
    { 0x41, 0x13, 10,   0,  0x22,  0,  1,  0xA2, 0x10 },
    { 0x42, 0x13, 11,   0,  0x21,  0,  1,  0xA1, 0x10 },
    { 0x43, 0x13, 12,   0,  0x23,  0,  1,  0xA3, 0x10 },
    { 0x44, 0x13, 13,   0,  0x09,  0,  0,  0x89, 0x10 },
    { 0x45, 0x13, 14,   0,  0x0A,  0,  1,  0x8A, 0x10 },
    { 0x46, 0x13, 15,   0,  0x0B,  0,  1,  0x8B, 0x10 },
    { 0x47, 0x13, 16,   0,  0x0C,  0,  1,  0x8C, 0x10 },
    { 0x48, 0x13, 17,   0,  0x0D,  0,  1,  0x8D, 0x10 },
    { 0x49, 0x13, 18,   0,  0x0E,  0,  1,  0x8E, 0x10 },
    { 0x4A, 0x13, 19,   0,  0x0F,  0,  1,  0x8F, 0x10 },
    { 0x4B, 0x13, 20,   0,  0x10,  0,  1,  0x90, 0x10 },
    { 0x4C, 0x13, 21,   0,  0x11,  0,  1,  0x91, 0x10 },
    { 0x4D, 0x13, 22,   0,  0x12,  0,  1,  0x92, 0x10 },
    { 0x4E, 0x13, 23,   0,  0x13,  0,  1,  0x93, 0x10 },
    { 0x4F, 0x13, 24,   0,  0x14,  0,  1,  0x94, 0x10 },
    { 0x50, 0x13, 25,   0,  0x15,  0,  1,  0x95, 0x10 },
    { 0x51, 0x13, 26,   0,  0x16,  0,  1,  0x96, 0x10 },
    { 0x52, 0x13, 27,   0,  0x17,  0,  1,  0x97, 0x10 },
    { 0x53, 0x13, 28,   0,  0x18,  0,  1,  0x98, 0x10 },
    { 0x54, 0x13, 29,   0,  0x19,  0,  1,  0x99, 0x10 },
    { 0x55, 0x13, 30,   0,  0x1A,  0,  1,  0x9A, 0x10 },
    { 0x56, 0x13, 31,   0,  0x1B,  0,  1,  0x9B, 0x10 },
    { 0x57, 0x13, 32,   0,  0x1C,  0,  1,  0x9C, 0x10 },
    { 0x58, 0x13, 33,   0,  0x1D,  0,  1,  0x9D, 0x10 },
    { 0x59, 0x13, 34,   0,  0x1E,  0,  1,  0x9E, 0x10 },
    { 0x5A, 0x13, 35,   0,  0x1F,  0,  1,  0x9F, 0x10 },
    { 0x5B, 0x13, 36,   0,  0x24,  0,  0,  0xA4, 0x10 },
    { 0x5C, 0x13, 37,   0,  0x25,  0,  0,  0xA5, 0x10 },
    { 0x5D, 0x13, 38,   1,  0x29,  0,  0,  0xA9, 0x20 },
    { 0x5E, 0x13, 39,   1,  0x26,  1,  0,  0xA6, 0x21 },
    { 0x5F, 0x13, 40,   1,  0x33,  2,  0,  0xB3, 0x22 },
    { 0x60, 0x13, 41,   1,  0x2E,  0,  0,  0xAE, 0x20 },
    { 0x61, 0x13, 42,   2,  0x28,  0,  0,  0xA8, 0x40 },
    { 0x62, 0x13, 43,   2,  0x2C,  1,  0,  0xAC, 0x41 },
    { 0x63, 0x13, 44,   2,  0x2F,  0,  0,  0xAF, 0x40 },
    { 0x64, 0x13, 45,   2,  0x2D,  0,  0,  0xAD, 0x40 },
    { 0x65, 0x13, 46,   2,  0x30,  0,  0,  0xB0, 0x40 },
    { 0x67, 0x13, 47,   3,  0x27,  1,  0,  0xA7, 0x81 },
    { 0x68, 0x13, 48,   3,  0x2A,  0,  0,  0xAA, 0x80 },
    { 0x69, 0x13, 49,   3,  0x32,  0,  0,  0xB2, 0x80 },
    { 0x6A, 0x13, 50,   3,  0x31,  1,  0,  0xB1, 0x81 },
    { 0x6B, 0x13, 51,   3,  0x34,  0,  0,  0xB4, 0x80 },
    { 0x6C, 0x13, 52,   3,  0x2B,  1,  0,  0xAB, 0x81 },
};
#define EXPECTED_ROW_COUNT ((int)(sizeof(kExpected) / sizeof(kExpected[0])))

/* ---- ROM discovery -------------------------------------------------------- */
static FILE *try_open(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f) printf("  using 6502 ROM: %s\n", path);
    return f;
}

static FILE *find_rom(void)
{
    const char *env = getenv("STUNRUN_JSA_ROM");
    const char *root = getenv("STUNRUN_REPO_ROOT");
    const char *suffix = "/roms/stunrun/136070-2123.10c";
    char buf[4096];
    char *slash;
    int depth;
    FILE *f;

    if (env && *env) {
        f = try_open(env);
        if (f) return f;
    }
    if (root && *root) {
        snprintf(buf, sizeof(buf), "%s%s", root, suffix);
        f = try_open(buf);
        if (f) return f;
    }

    /* Walk up from the CWD: build/, native/, stun-runner/, repo root. */
    if (!getcwd(buf, sizeof(buf))) return NULL;
    for (depth = 0; depth < 8; depth++) {
        size_t n = strlen(buf);
        snprintf(buf + n, sizeof(buf) - n, "%s", suffix);
        f = try_open(buf);
        if (f) return f;
        buf[n] = '\0';                       /* undo the append */
        slash = strrchr(buf, '/');
        if (!slash || slash == buf) break;
        *slash = '\0';                       /* one level up */
    }
    return NULL;
}

/* Read `n` bytes of ROM and diff them against a transcribed table. */
static void diff_table(const char *name, uint16_t addr,
                       const uint8_t *table, size_t n, FILE *rom)
{
    unsigned char buf[256];
    size_t i, bad = 0;

    if (n > sizeof(buf)) { printf("FAIL %s: n too big\n", name); g_failures++; return; }
    if (fseek(rom, (long)addr, SEEK_SET) != 0 ||
        fread(buf, 1, n, rom) != n) {
        printf("FAIL %s: short read at $%04X\n", name, addr);
        g_failures++;
        return;
    }
    for (i = 0; i < n; i++) {
        if (buf[i] != table[i]) {
            printf("  %s[%02X] @ $%04X: rom $%02X != table $%02X\n",
                   name, (unsigned)i, addr + (unsigned)i, buf[i], table[i]);
            bad++;
        }
    }
    g_checks++;
    if (bad == 0)
        printf("PASS %s matches ROM $%04X..$%04X (%u bytes)\n",
               name, addr, addr + (unsigned)n - 1, (unsigned)n);
    else {
        printf("FAIL %s: %u of %u bytes differ\n", name, (unsigned)bad, (unsigned)n);
        g_failures++;
    }
}

/* ---- the documented map --------------------------------------------------- */
static void test_expected_map(void)
{
    int i;

    for (i = 0; i < EXPECTED_ROW_COUNT; i++) {
        const expected_row_t *e = &kExpected[i];
        oki_cmdmap_entry_t ok;
        char label[96];

        memset(&ok, 0, sizeof(ok));
        if (!oki_cmdmap_lookup(e->command, &ok)) {
            g_checks++;
            printf("FAIL lookup(cmd $%02X) returned false\n", e->command);
            g_failures++;
            continue;
        }

        snprintf(label, sizeof(label), "$%02X klass",  e->command);
        check_eq(ok.klass,    e->klass,   label);
        snprintf(label, sizeof(label), "$%02X argument", e->command);
        check_eq(ok.argument, e->argument, label);
        snprintf(label, sizeof(label), "$%02X voice",   e->command);
        check_eq(ok.voice,    e->voice,    label);
        snprintf(label, sizeof(label), "$%02X sample",  e->command);
        check_eq(ok.sample,   e->sample,   label);
        snprintf(label, sizeof(label), "$%02X volume",  e->command);
        check_eq(ok.volume,   e->volume,   label);
        snprintf(label, sizeof(label), "$%02X locked",  e->command);
        check_eq(ok.locked,   e->locked,   label);
        snprintf(label, sizeof(label), "$%02X byte1",   e->command);
        check_eq(ok.byte1,    e->byte1,    label);
        snprintf(label, sizeof(label), "$%02X byte2",   e->command);
        check_eq(ok.byte2,    e->byte2,    label);
    }

    /* The report claims 34 of 53 class-19 sounds carry the lock bit, and
     * that every one of them uses volume 0. */
    {
        int locked = 0, locked_nonzero_vol = 0;
        for (i = 0; i < EXPECTED_ROW_COUNT; i++) {
            if (!kExpected[i].locked) continue;
            locked++;
            if (kExpected[i].volume != 0) locked_nonzero_vol++;
        }
        check_eq(locked, 34, "locked sound count");
        check_eq(locked_nonzero_vol, 0, "locked sounds with nonzero volume");
    }
}

/* ---- class 20: the stop --------------------------------------------------- */
static void test_stop_class(void)
{
    oki_cmdmap_entry_t ok;
    oki_cmdmap_state_t st;
    uint8_t out[OKI_CMDMAP_MAX_WRITES];
    size_t n;

    /* $66 is class $14 with argument 48. */
    check_eq(jsa_class_table[0x66], JSA_CLASS_OKI_STOP, "class of $66");
    check_eq(jsa_argument_table[0x66], 48, "argument of $66");

    /* $66 is NOT a play: oki_cmdmap_lookup() must decline it. */
    memset(&ok, 0, sizeof(ok));
    check(!oki_cmdmap_lookup(0x66, &ok));
    check(!oki_cmdmap_plays_anything(0x66));

    /* With nothing playing, $66 writes nothing at all ($5c7d/$5c7e). */
    oki_cmdmap_init(&st);
    n = oki_cmdmap_translate(&st, 0x66, out);
    check_eq(n, 0, "OKI writes for $66 with no voice playing");

    /* The ROM compares the ARGUMENT (48) against $6532[cur], i.e. against a
     * sample NUMBER.  Sound index 46 is the only entry with sample $30 ==
     * 48, so $66 stops whichever voice is playing sound index 46. */
    check_eq(jsa_sample_table[46], 48, "sample number of sound index 46");
    check_eq((jsa_voicevolume_table[46] >> 4) & 3, 2, "voice of sound index 46");
    check_eq(jsa_voice_stop_byte[2], 0x20, "stop byte for voice 2");

    /* Sound index 48 (sample $2A, voice 3) is what the report claimed; the
     * ROM does not stop it. */
    check_eq(jsa_sample_table[48], 0x2A, "sample number of sound index 48");

    oki_cmdmap_init(&st);
    oki_cmdmap_note_play(&st, 46);
    n = oki_cmdmap_translate(&st, 0x66, out);
    check_eq(n, 1, "OKI writes for $66 with sound index 46 playing");
    check_eq(out[0], 0x20, "stop byte emitted for $66");
    check_eq(st.voice_busy & 0x04, 0, "voice 2 cleared by $66");

    /* ...and it leaves sound index 48 alone even if that is playing. */
    oki_cmdmap_init(&st);
    oki_cmdmap_note_play(&st, 48);
    n = oki_cmdmap_translate(&st, 0x66, out);
    check_eq(n, 0, "OKI writes for $66 with only sound index 48 playing");
    check_eq(st.voice_busy & 0x08, 0x08, "voice 3 untouched by $66");
}

/* ---- the $1E mode gate ---------------------------------------------------- */
static void test_mode_gate(void)
{
    oki_cmdmap_state_t st;
    uint8_t out[OKI_CMDMAP_MAX_WRITES];
    size_t n;

    /* $01 and $02 are the two class-0 commands. */
    check_eq(jsa_class_table[0x01], JSA_CLASS_MODE, "class of $01");
    check_eq(jsa_class_table[0x02], JSA_CLASS_MODE, "class of $02");
    check_eq(jsa_argument_table[0x01], 0x3C, "argument of $01");
    check_eq(jsa_argument_table[0x02], 0x00, "argument of $02");

    /* Class-0 commands write $1E and emit no OKI byte.  $438A is
     * asl a / asl a / sta $1e / rts, so the value is argument << 2. */
    oki_cmdmap_init(&st);
    check_eq(st.mode, 0, "$1E after reset");
    n = oki_cmdmap_translate(&st, 0x01, out);
    check_eq(n, 0, "OKI writes for $01");
    check_eq(st.mode, 0x3C << 2, "$1E after $01 (argument << 2)");
    n = oki_cmdmap_translate(&st, 0x02, out);
    check_eq(n, 0, "OKI writes for $02");
    check_eq(st.mode, 0x00 << 2, "$1E after $02");

    /* $1E nonzero: every class-19 play is skipped ($5c01 bne $5c61). */
    oki_cmdmap_init(&st);
    st.mode = 0xF0;
    n = oki_cmdmap_translate(&st, 0x38, out);
    check_eq(n, 0, "OKI writes for $38 with $1E set");
    check_eq(st.voice_busy, 0, "no voice started with $1E set");

    /* Same for every one of the 53 class-19 commands. */
    {
        int i, any = 0;
        oki_cmdmap_init(&st);
        st.mode = 0x01;
        for (i = 0; i < EXPECTED_ROW_COUNT; i++) {
            any += (int)oki_cmdmap_translate(&st, kExpected[i].command, out);
        }
        check_eq(any, 0, "total OKI writes for all 53 plays with $1E set");
        check_eq(st.voice_busy, 0, "no voice busy after gated plays");
    }

    /* $1E back to zero: they all play again. */
    {
        int i, any = 0;
        oki_cmdmap_init(&st);
        st.mode = 0x00;
        for (i = 0; i < EXPECTED_ROW_COUNT; i++)
            any += (int)oki_cmdmap_translate(&st, kExpected[i].command, out);
        /* Each accepted play is three writes; contention may suppress some.
         * At least one must succeed and none may be more than 3. */
        check(any > 0 && any <= 3 * EXPECTED_ROW_COUNT);
    }
}

/* ---- voice contention ($5c14-$5c21) --------------------------------------- */
static void test_contention(void)
{
    oki_cmdmap_state_t st;
    uint8_t out[OKI_CMDMAP_MAX_WRITES];
    size_t n;

    /* Sound index 1 ($38) is locked: bit 7 of $6567[1] is set. */
    check_eq(jsa_voicevolume_table[1], 0x80, "$6567[1]");
    /* Sound index 37 ($5C) is not locked and is on voice 0. */
    check_eq(jsa_voicevolume_table[37], 0x00, "$6567[37]");

    /* $5C onto a voice already running the locked $38 -> abandoned, 0 writes. */
    oki_cmdmap_init(&st);
    oki_cmdmap_note_play(&st, 1);
    n = oki_cmdmap_translate(&st, 0x5C, out);
    check_eq(n, 0, "OKI writes when preempting a locked sound");
    check_eq(st.voice_sound[0], 1, "locked sound still on voice 0");

    /* $5C onto a voice already running the unlocked $5B -> preempted. */
    oki_cmdmap_init(&st);
    oki_cmdmap_note_play(&st, 37);
    n = oki_cmdmap_translate(&st, 0x5C, out);
    check_eq(n, 3, "OKI writes when preempting an unlocked sound");
    check_eq(out[0], 0x08, "preempt: voice-0 stop byte first");
    check_eq(out[1], 0xA5, "preempt: byte1 = sample $25 | $80");
    check_eq(out[2], 0x10, "preempt: byte2 = voice-0 mask | volume 0");

    /* An idle voice takes the same three writes. */
    oki_cmdmap_init(&st);
    n = oki_cmdmap_translate(&st, 0x5C, out);
    check_eq(n, 3, "OKI writes onto an idle voice");
    check_eq(out[0], 0x08, "idle: voice-0 stop byte first");
    check_eq(out[1], 0xA5, "idle: byte1");
    check_eq(out[2], 0x10, "idle: byte2");

    /* A locked play onto its own busy voice is still abandoned: the ROM
     * tests the lock of what is ALREADY on the voice, not of the incoming
     * command. */
    oki_cmdmap_init(&st);
    oki_cmdmap_note_play(&st, 38);            /* $5D, voice 1, unlocked */
    n = oki_cmdmap_translate(&st, 0x5F, out); /* $5F, voice 1, unlocked */
    check_eq(n, 3, "OKI writes re-triggering the same voice");

    /* Commands at or above $6D, and classes at or above $15, do nothing. */
    oki_cmdmap_init(&st);
    check_eq(oki_cmdmap_translate(&st, 0x6D, out), 0, "writes for $6D");
    check_eq(oki_cmdmap_translate(&st, 0xFF, out), 0, "writes for $FF");
    check_eq(oki_cmdmap_translate(&st, 0x00, out), 0, "writes for $00 (class 3)");
    check_eq(oki_cmdmap_translate(&st, 0x06, out), 0, "writes for $06 (class 8 = YM2151)");
    check_eq(oki_cmdmap_translate(&st, 0x04, out), 0, "writes for $04 (class $FF)");
}

/* ---- per-voice tables ----------------------------------------------------- */
static void test_voice_tables(void)
{
    check_eq(jsa_voice_start_mask[0], 0x10, "$5C63[0]");
    check_eq(jsa_voice_start_mask[1], 0x20, "$5C63[1]");
    check_eq(jsa_voice_start_mask[2], 0x40, "$5C63[2]");
    check_eq(jsa_voice_start_mask[3], 0x80, "$5C63[3]");
    check_eq(jsa_voice_status_bit[0], 0x01, "$5C67[0]");
    check_eq(jsa_voice_status_bit[1], 0x02, "$5C67[1]");
    check_eq(jsa_voice_status_bit[2], 0x04, "$5C67[2]");
    check_eq(jsa_voice_status_bit[3], 0x08, "$5C67[3]");
    check_eq(jsa_voice_stop_byte[0], 0x08, "$5C6B[0]");
    check_eq(jsa_voice_stop_byte[1], 0x10, "$5C6B[1]");
    check_eq(jsa_voice_stop_byte[2], 0x20, "$5C6B[2]");
    check_eq(jsa_voice_stop_byte[3], 0x40, "$5C6B[3]");

    /* byte2 is always the voice's start mask OR'd with the volume nibble. */
    {
        int i;
        for (i = 0; i < EXPECTED_ROW_COUNT; i++) {
            oki_cmdmap_entry_t ok;
            if (!oki_cmdmap_lookup(kExpected[i].command, &ok)) continue;
            if (ok.byte2 != (jsa_voice_start_mask[ok.voice] | ok.volume) ||
                ok.byte1 != (ok.sample | 0x80)) {
                g_checks++;
                printf("FAIL byte derivation for $%02X\n", kExpected[i].command);
                g_failures++;
            }
        }
        g_checks++;
        printf("PASS byte1/byte2 derive from sample, voice mask and volume\n");
    }
}

int main(void)
{
    FILE *rom;

    printf("--- oki_cmdmap: transcribed tables vs ROM ---\n");
    rom = find_rom();
    if (rom) {
        diff_table("jsa_class_table",       JSA_CLASS_TABLE_ADDR,
                   jsa_class_table,       JSA_COMMAND_COUNT, rom);
        diff_table("jsa_argument_table",    JSA_ARGUMENT_TABLE_ADDR,
                   jsa_argument_table,    JSA_COMMAND_COUNT, rom);
        diff_table("jsa_sample_table",      0x6532,
                   jsa_sample_table,      JSA_SOUND_COUNT,   rom);
        diff_table("jsa_voicevolume_table", 0x6567,
                   jsa_voicevolume_table, JSA_SOUND_COUNT,   rom);
        diff_table("jsa_priority_table",    0x659C,
                   jsa_priority_table,    JSA_SOUND_COUNT,   rom);
        diff_table("jsa_voice_start_mask",  0x5C63,
                   jsa_voice_start_mask,  JSA_VOICE_COUNT,   rom);
        diff_table("jsa_voice_status_bit",  0x5C67,
                   jsa_voice_status_bit,  JSA_VOICE_COUNT,   rom);
        diff_table("jsa_voice_stop_byte",   0x5C6B,
                   jsa_voice_stop_byte,   JSA_VOICE_COUNT,   rom);
        fclose(rom);
    } else {
        printf("SKIP rom diff: 136070-2123.10c not found (set STUNRUN_JSA_ROM)\n");
        printf("      falling back to the transcribed constants only\n");
    }

    printf("--- oki_cmdmap: documented map (jsa2-sound-findings.md section 4) ---\n");
    test_expected_map();

    printf("--- oki_cmdmap: per-voice tables ---\n");
    test_voice_tables();

    printf("--- oki_cmdmap: class 20 ($66) is a STOP, and it is the ROM's stop ---\n");
    test_stop_class();

    printf("--- oki_cmdmap: the $1E mode gate ---\n");
    test_mode_gate();

    printf("--- oki_cmdmap: voice contention ---\n");
    test_contention();

    if (g_failures == 0)
        printf("oki_cmdmap_test: all %d checks passed\n", g_checks);
    else
        printf("oki_cmdmap_test: %d of %d checks FAILED\n", g_failures, g_checks);
    return g_failures == 0 ? 0 : 1;
}