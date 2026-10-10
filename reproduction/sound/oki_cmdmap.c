/* oki_cmdmap.c — transcription of the JSA-II 6502 sound translator.
 *
 * Every byte of every table below was read out of
 * roms/stunrun/136070-2123.10c at the 6502 address named in the comment
 * (ROM file offset == 6502 address; see oki_cmdmap.h for why).  Do not
 * "tidy" these arrays: they are data, they are the specification, and
 * oki_cmdmap_test.c re-extracts them from the ROM and diffs them.
 *
 * Toolkit-free: this file is pure C99 + stdbool/stdint.  No I/O, no
 * hardware, no globals that persist across calls.
 */
#include "oki_cmdmap.h"

#include <string.h>

/* ---- $6100: command byte -> sound class (109 bytes) --------------------- */
const uint8_t jsa_class_table[JSA_COMMAND_COUNT] = {
    0x03, 0x00, 0x00, 0xFF, 0x08, 0x13, 0xFF, 0xFF, 0xFF, 0x09, 0x08, 0x0B, 0x08,
    0x08, 0x0B, 0x08, 0x08, 0x08, 0x08, 0x0A, 0x08, 0x0A, 0x08, 0x08, 0x08, 0x0A,
    0x08, 0x08, 0x08, 0x08, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x08, 0x08, 0x08,
    0x0A, 0x08, 0x0A, 0x08, 0x08, 0x08, 0x08, 0x05, 0x08, 0x08, 0x08, 0x08, 0x08,
    0x08, 0x08, 0x05, 0x08, 0x13, 0x13, 0x13, 0x13, 0x13, 0x13, 0x13, 0x13, 0x13,
    0x13, 0x13, 0x13, 0x13, 0x13, 0x13, 0x13, 0x13, 0x13, 0x13, 0x13, 0x13, 0x13,
    0x13, 0x13, 0x13, 0x13, 0x13, 0x13, 0x13, 0x13, 0x13, 0x13, 0x13, 0x13, 0x13,
    0x13, 0x13, 0x13, 0x13, 0x13, 0x13, 0x13, 0x13, 0x13, 0x13, 0x13, 0x14, 0x13,
    0x13, 0x13, 0x13, 0x13, 0x13
};

/* ---- $616D: command byte -> argument / sound index (109 bytes) ---------- */
const uint8_t jsa_argument_table[JSA_COMMAND_COUNT] = {
    0x00, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x55, 0x01, 0x27, 0x02,
    0x03, 0x29, 0x04, 0x05, 0x06, 0x07, 0x12, 0x08, 0x14, 0x09, 0x0A, 0x0B, 0x18,
    0x0C, 0x0D, 0x0E, 0x0F, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x10, 0x11, 0x12,
    0x26, 0x13, 0x28, 0x14, 0x15, 0x16, 0x17, 0x2D, 0x18, 0x19, 0x1A, 0x1B, 0x1C,
    0x1D, 0x1E, 0x35, 0x1F, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
    0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
    0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23,
    0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x30, 0x2F,
    0x30, 0x31, 0x32, 0x33, 0x34
};

/* ---- $6532: sound index -> OKI sample number (53 bytes) -----------------
 * DECISION SITE: this is NOT a permutation.  It has 53 entries and 52
 * distinct values; $01 occurs twice, at sound index 0 (command $05) and
 * sound index 1 (command $38).  Left as-is — it is ROM data.
 */
const uint8_t jsa_sample_table[JSA_SOUND_COUNT] = {
    0x01, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x20, 0x22, 0x21, 0x23,
    0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
    0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x24, 0x25, 0x29,
    0x26, 0x33, 0x2E, 0x28, 0x2C, 0x2F, 0x2D, 0x30, 0x27, 0x2A, 0x32, 0x31, 0x34,
    0x2B
};

/* ---- $6567: sound index -> voice<<4 | volume, bit 7 = locked ----------- */
const uint8_t jsa_voicevolume_table[JSA_SOUND_COUNT] = {
    0x01, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x00, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x00, 0x00, 0x10,
    0x11, 0x12, 0x10, 0x20, 0x21, 0x20, 0x20, 0x20, 0x31, 0x30, 0x30, 0x31, 0x30,
    0x31
};

/* ---- $659C: sound index -> priority -------------------------------------- */
const uint8_t jsa_priority_table[JSA_SOUND_COUNT] = {
    0x00, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x08, 0x08, 0x08, 0x08,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x08, 0x07, 0x00,
    0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00
};

/* ---- $5C63/$5C67/$5C6B: per-voice tables, indexed by voice 0..3 ---------- */
const uint8_t jsa_voice_start_mask[JSA_VOICE_COUNT] = { 0x10, 0x20, 0x40, 0x80 };
const uint8_t jsa_voice_status_bit[JSA_VOICE_COUNT] = { 0x01, 0x02, 0x04, 0x08 };
const uint8_t jsa_voice_stop_byte[JSA_VOICE_COUNT]  = { 0x08, 0x10, 0x20, 0x40 };

void oki_cmdmap_init(oki_cmdmap_state_t *st)
{
    int i;
    memset(st, 0, sizeof(*st));
    st->mode = 0x00;                      /* $412B: reset path stores X=0 */
    st->voice_busy = 0x00;                /* nothing playing out of reset */
    for (i = 0; i < JSA_VOICE_COUNT; i++)
        st->voice_sound[i] = JSA_SOUND_NONE;   /* $52..$55 */
}

bool oki_cmdmap_lookup(uint8_t command, oki_cmdmap_entry_t *out)
{
    uint8_t klass, arg;

    if (!out)
        return false;

    memset(out, 0, sizeof(*out));

    /* $4371: cpy #$6d / bcs $4389 — commands >= $6D are not translated. */
    if (command >= 0x6D)
        return false;

    klass = jsa_class_table[command];
    /* $4378: cmp #$15 / bcs $4389 — classes >= $15 are not translated. */
    if (klass >= 0x15)
        return false;

    arg = jsa_argument_table[command];    /* $4386: loaded into A */

    out->command     = command;
    out->klass       = klass;
    out->argument    = arg;
    out->sound_index = arg;

    if (klass != JSA_CLASS_OKI_PLAY)
        return false;                     /* class 0 and class 20 own no table */

    /* The class-19 argument is a sound index into three 53-byte tables.  Both
     * class-19 commands the ROM emits have arguments <= 52; a larger one
     * would run off the end of the ROM tables, which the firmware never
     * does.  Refuse rather than invent a row. */
    if (arg >= JSA_SOUND_COUNT)
        return false;

    out->sample    = jsa_sample_table[arg];              /* $6532 */
    out->voice     = (uint8_t)((jsa_voicevolume_table[arg] >> 4) & 0x03);
    out->volume    = (uint8_t)(jsa_voicevolume_table[arg] & 0x0F);
    out->locked    = (jsa_voicevolume_table[arg] & 0x80) != 0;
    out->priority  = jsa_priority_table[arg];             /* $659C */
    out->byte1     = (uint8_t)(out->sample | 0x80);      /* $5c43-$5c48 */
    out->byte2     = (uint8_t)(jsa_voice_start_mask[out->voice] | out->volume);
    out->stop_byte = jsa_voice_stop_byte[out->voice];    /* $5C6B */

    return true;
}

bool oki_cmdmap_plays_anything(uint8_t command)
{
    oki_cmdmap_entry_t ok;
    return oki_cmdmap_lookup(command, &ok);
}

void oki_cmdmap_note_play(oki_cmdmap_state_t *st, uint8_t sound_index)
{
    uint8_t voice;

    if (!st || sound_index >= JSA_SOUND_COUNT)
        return;

    voice = (uint8_t)((jsa_voicevolume_table[sound_index] >> 4) & 0x03);
    st->voice_sound[voice] = sound_index;
    st->voice_busy |= jsa_voice_status_bit[voice];
}

/* One class-19 play: 0..3 OKI writes, exactly as $5BFE does. */
static size_t translate_play(oki_cmdmap_state_t *st, const oki_cmdmap_entry_t *ok,
                             uint8_t *out)
{
    uint8_t voice = ok->voice;
    uint8_t n = 0;

    /* $5bfe/$5c01: the mode gate.  While $1E is nonzero every class-19 play
     * is skipped and the ROM returns with carry set. */
    if (st->mode != 0x00)
        return 0;

    /* $5c14-$5c1a: if the voice is busy...
     * $5c1c-$5c21: ...and the sound already on it is locked (bit 7 of
     * $6567), abandon the whole command without writing anything. */
    if (st->voice_busy & jsa_voice_status_bit[voice]) {
        uint8_t cur = st->voice_sound[voice];
        if (cur < JSA_SOUND_COUNT && (jsa_voicevolume_table[cur] & 0x80))
            return 0;
    }

    /* $5c23-$5c26: stop the voice first.  Emitted unconditionally — on the
     * real chip this is followed by a poll loop ($5c2f-$5c35) that re-issues
     * the same write while /RDV still reports the voice busy; that loop is a
     * hardware handshake with a bounded number of iterations and is not part
     * of the translation, so one write is emitted here. */
    out[n++] = jsa_voice_stop_byte[voice];

    /* The voice is now idle and about to be reloaded. */
    st->voice_busy &= (uint8_t)~jsa_voice_status_bit[voice];

    /* $5c43-$5c48: sample | $80 */
    out[n++] = ok->byte1;

    /* $5c4b-$5c53: voice start mask | volume */
    out[n++] = ok->byte2;

    /* $5c37-$5c41 / $5c56-$5c59: bookkeeping the handler performs on the
     * 6502's own memory once the writes land. */
    st->voice_sound[voice] = ok->sound_index;
    st->voice_busy |= jsa_voice_status_bit[voice];

    return n;
}

/* One class-20 stop: 0 or 1 OKI write, exactly as $5C6F does. */
static size_t translate_stop(oki_cmdmap_state_t *st, uint8_t argument, uint8_t *out)
{
    int voice;

    /* $5c71-$5c7b: scan voices 3,2,1,0 for the one whose current sound maps
     * to the argument value.
     *
     * DECISION SITE: `cmp $6532,y` compares the raw argument against a SAMPLE
     * NUMBER, not against a sound index.  Reproduced verbatim, because for
     * $66 (argument 48) the only $6532 entry holding 48 is sound index 46 —
     * so the ROM stops sound index 46 (sample $30, voice 2), not sound index
     * 48 (sample $2A, voice 3).  Fixing this to index-by-sound would be
     * "improving" the original; see BUGS.md.  Note that `target` must NOT be
     * precomputed as jsa_sample_table[argument]: that would re-introduce the
     * index reading the ROM does not implement. */
    for (voice = JSA_VOICE_COUNT - 1; voice >= 0; voice--) {
        uint8_t cur = st->voice_sound[voice];
        if (cur >= JSA_SOUND_COUNT)     /* JSA_SOUND_NONE: voice idle */
            continue;
        if (jsa_sample_table[cur] != argument)
            continue;

        /* $5c7f-$5c82 */
        out[0] = jsa_voice_stop_byte[voice];
        st->voice_busy &= (uint8_t)~jsa_voice_status_bit[voice];
        st->voice_sound[voice] = JSA_SOUND_NONE;
        return 1;
    }

    /* $5c7d-$5c7e: no voice matched, no write. */
    return 0;
}

size_t oki_cmdmap_translate(oki_cmdmap_state_t *st, uint8_t command,
                            uint8_t out[OKI_CMDMAP_MAX_WRITES])
{
    uint8_t klass, argument;
    oki_cmdmap_entry_t ok;

    if (!st || !out)
        return 0;

    out[0] = out[1] = out[2] = 0;

    /* $4371: cpy #$6d / bcs $4389 */
    if (command >= 0x6D)
        return 0;

    klass = jsa_class_table[command];

    /* $4378: cmp #$15 / bcs $4389 */
    if (klass >= 0x15)
        return 0;

    argument = jsa_argument_table[command];   /* $4386 */

    switch (klass) {
    case JSA_CLASS_MODE:
        /* $438A: asl a / asl a / sta $1e / rts — A holds the argument, and
         * ASL A twice is A << 2, not A & 3.  No OKI write. */
        st->mode = (uint8_t)(argument << 2);
        return 0;

    case JSA_CLASS_OKI_PLAY:
        if (!oki_cmdmap_lookup(command, &ok))
            return 0;
        return translate_play(st, &ok, out);

    case JSA_CLASS_OKI_STOP:
        if (argument >= JSA_SOUND_COUNT)
            return 0;
        return translate_stop(st, argument, out);

    default:
        /* Every other class belongs to a chip this module does not model
         * (class 8 is the YM2151 sequencer, classes 1/3/5/9/$0A/$0B are the
         * rest of the 6502's board control).  The dispatcher enters those
         * handlers and they write no OKI bytes, which is what returning 0
         * says. */
        return 0;
    }
}
