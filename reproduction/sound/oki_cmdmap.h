/* oki_cmdmap.h — JSA-II 68010 sound-command -> OKI6295 byte translation.
 *
 * The 68010 sound byte that arrives over /SIO2 is NOT an OKI6295 command.
 * The JSA-II 6502 sound CPU firmware ROM (136070-2123.10c) translates it.
 * This module is a transcription of that translation.
 *
 * Everything below the seam: no toolkit, no hardware, no I/O.  It is a pure
 * function of (command byte, small state) -> OKI byte sequence.
 *
 * ---------------------------------------------------------------------------
 * EVIDENCE
 * ---------------------------------------------------------------------------
 * ROM: roms/stunrun/136070-2123.10c (64 KiB).  MAME loads it at region
 * offset 0x00000 length 0x10000 (harddriv.cpp:2707), so
 *
 *     ROM file offset == 6502 address
 *
 * All table bytes in oki_cmdmap.c were extracted from that file with
 * `xxd -s 0x6100 -l 109` and friends; see oki_cmdmap_test.c, which re-reads
 * the ROM when it can find it and fails if the transcription drifts.
 *
 * Dispatcher, $4371 (verbatim, [C] = confirmed byte disassembly):
 *
 *     $4371  cpy #$6d      ; command >= $6D -> leave the A-register alone
 *     $4373  bcs $4389
 *     $4375  lda $6100,y   ; class byte
 *     $4378  cmp #$15      ; class >= $15 -> leave the A-register alone
 *     $437a  bcs $4389
 *     $437c  asl a
 *     $437d  tax           ; X = class * 2
 *     $437e  lda $4730,x
 *     $4381  pha
 *     $4382  lda $472f,x
 *     $4385  pha
 *     $4386  lda $616d,y   ; <<< the per-command ARGUMENT lands in A here
 *     $4389  rts           ; RTS-trick: the pushed address is the handler
 *
 * Two consequences that the header comment of the original report got wrong,
 * both confirmed against the ROM bytes and therefore reproduced here:
 *
 *   1. The handler entry is the PUSHED address + 1, because RTS adds 1 to
 *      the address it pops.  Table entry 19 is $5BFD -> entry $5BFE; entry 20
 *      is $5C6E -> entry $5C6F.
 *
 *   2. The value in A on handler entry is the ARGUMENT from $616D, NOT the
 *      command byte.  For class 19 the argument is a 0..52 sound index into
 *      $6532 (sample) / $6567 (voice+volume) / $659C (priority).  For class 20
 *      it is compared against sample *numbers* — see the class-20 section.
 *
 * ---------------------------------------------------------------------------
 * CLASS 19 ($13) — OKI PLAY, handler $5BFE
 * ---------------------------------------------------------------------------
 * Commands: $05, $38..$65, $67..$6C (53 of them, arguments 0..52 in order).
 *
 * Verbatim [C]:
 *
 *     $5bfe  ldy $001e     ; the mode gate
 *     $5c01  bne $5c61     ; nonzero -> skip the play entirely, `sec`/`rts`
 *     $5c03  php
 *     $5c04  sei
 *     $5c05  tay           ; Y = argument (sound index 0..52)
 *     $5c06  txa           ; X is still class*2 from the dispatcher
 *     $5c07  pha
 *     $5c08  lda $6567,y
 *     $5c0b  and #$30      ; voice in bits 4-5
 *     $5c0d  lsr a / lsr a / lsr a / lsr a
 *     $5c11  tax           ; X = voice 0..3
 *     $5c12  sty $50       ; stash the sound index
 *     $5c14  lda $5c67,x   ; per-voice status bit: 01 02 04 08
 *     $5c17  and $2808     ; /RDV: bit set = that voice is busy
 *     $5c1a  beq $5c23     ; voice idle -> fall through to the stop write
 *     $5c1c  ldy $52,x     ; sound index currently on voice X
 *     $5c1e  lda $6567,y   ; its "not preemptible" flag (bit 7)
 *     $5c21  bmi $5c5e     ; locked -> abandon, write nothing at all
 *     $5c23  lda $5c6b,x   ; voice stop byte: 08 10 20 40
 *     $5c26  sta $2a00     ; <<< WRITE 1
 *     $5c29  ldy #$7f / nop / dey / bpl $5c2b   ; short delay
 *     $5c2f  lda $5c67,x
 *     $5c32  and $2808
 *     $5c35  bne $5c23     ; poll until the voice reports idle, re-stopping
 *     $5c37  ldy $50
 *     $5c39  sty $52,x     ; record the sound index on the voice
 *     $5c3b  lda #$01 / $5c3d sta $5a,x
 *     $5c3f  lda #$00 / $5c41 sta $56,x
 *     $5c43  lda $6532,y   ; sample number
 *     $5c46  ora #$80
 *     $5c48  sta $2a00     ; <<< WRITE 2: sample | $80
 *     $5c4b  lda $6567,y   ; volume in bits 0-3
 *     $5c4e  and #$0f
 *     $5c50  ora $5c63,x   ; voice start mask: 10 20 40 80
 *     $5c53  sta $2a00     ; <<< WRITE 3: voice mask | volume
 *     $5c56  lda $659c,y   ; priority
 *     $5c59  sta $5e,x
 *     $5c5b  jsr $5c87
 *     $5c5e  pla / tax / plp
 *     $5c61  sec / rts
 *
 * The stop write at $5c23 is on the fall-through path from BOTH the "voice
 * idle" branch and the "voice busy but unlocked" path, so an accepted play is
 * ALWAYS three writes.  That is why oki_cmdmap_translate() can return 0, 1
 * or 3 and never 2; the two-byte *play command* of the findings report is
 * exposed separately as ok->byte1 / ok->byte2.
 *
 * The $5C2F..$5C35 poll loop can re-issue WRITE 1 more than once if the chip
 * takes several passes to clear the voice-status bit.  oki_cmdmap_translate()
 * emits it once — the loop is a hardware handshake, not a translation step —
 * and says so at the decision site in oki_cmdmap.c.
 *
 * ---------------------------------------------------------------------------
 * CLASS 20 ($14) — OKI STOP, handler $5C6F
 * ---------------------------------------------------------------------------
 * Exactly one command: $66, argument 48.  Verbatim [C]:
 *
 *     $5c6f  php
 *     $5c70  sei
 *     $5c71  ldx #$03      ; scan voices 3,2,1,0
 *     $5c73  ldy $52,x     ; sound index currently on voice X
 *     $5c75  cmp $6532,y   ; <<< A is still the ARGUMENT (48)
 *     $5c78  beq $5c7f
 *     $5c7a  dex
 *     $5c7b  bpl $5c73
 *     $5c7d  plp
 *     $5c7e  rts           ; nothing matched: no write at all
 *     $5c7f  lda $5c6b,x
 *     $5c82  sta $2a00     ; <<< WRITE 1: voice stop byte
 *     $5c85  plp
 *     $5c86  rts
 *
 * DECISION SITE — this is an original-code quirk, reproduced faithfully:
 * `cmp $6532,y` compares the raw ARGUMENT against a SAMPLE NUMBER, not
 * against a sound index.  With argument 48 the only entry of $6532 holding
 * the value 48 (=$30) is index 46, so $66 stops whichever voice is currently
 * playing sound index 46 (sample $30, $6567[46]=$20 => voice 2, stop byte
 * $20).  It does NOT stop sound index 48 (sample $2A, voice 3) as the
 * findings report assumed.  Because $6532 has 53 entries and the arguments
 * are 0..52, the two readings coincide for only one of the possible stop
 * arguments, and for $66 they do not coincide.  Reproduced, not corrected;
 * see BUGS.md.
 *
 * ---------------------------------------------------------------------------
 * CLASS 0 — the $1E mode gate, handler $438A
 * ---------------------------------------------------------------------------
 * Verbatim [C]:  $438A  asl a / asl a / sta $1e / rts
 *
 * DECISION SITE: the report read this as "mode = (arg & 3)".  There is no
 * AND; ASL A twice is `A << 2`, so $1E = (uint8_t)(argument << 2).  The gate
 * at $5C01 only tests nonzero, so both readings agree operationally for the
 * two class-0 commands ($01 -> $F0, $02 -> $00), but the arithmetic differs
 * and the arithmetic is what is transcribed.  $1E is cleared to 0 by the
 * firmware's reset path ($412B: ldx #$ff / stx $0234 / inx / stx $1e).
 */
#ifndef STUNRUN_OKI_CMDMAP_H
#define STUNRUN_OKI_CMDMAP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- transcribed ROM tables (byte-exact, see header comment) -------------
 * $6100 is indexed by the 68010 command byte; $616D likewise.  $6532/$6567/
 * $659C are indexed by the class-19 argument (sound index 0..52).
 * $5C63/$5C67/$5C6B are indexed by voice 0..3.
 */
#define JSA_CLASS_TABLE_ADDR   0x6100
#define JSA_ARGUMENT_TABLE_ADDR 0x616D
#define JSA_SOUND_COUNT        53
#define JSA_VOICE_COUNT        4
#define JSA_COMMAND_COUNT      109

extern const uint8_t jsa_class_table[JSA_COMMAND_COUNT];          /* $6100 */
extern const uint8_t jsa_argument_table[JSA_COMMAND_COUNT];      /* $616D */
extern const uint8_t jsa_sample_table[JSA_SOUND_COUNT];          /* $6532 */
extern const uint8_t jsa_voicevolume_table[JSA_SOUND_COUNT];     /* $6567 */
extern const uint8_t jsa_priority_table[JSA_SOUND_COUNT];        /* $659C */
extern const uint8_t jsa_voice_start_mask[JSA_VOICE_COUNT];      /* $5C63 */
extern const uint8_t jsa_voice_status_bit[JSA_VOICE_COUNT];      /* $5C67 */
extern const uint8_t jsa_voice_stop_byte[JSA_VOICE_COUNT];       /* $5C6B */

/* Sound-effect classes.  Only these two reach the OKI6295. */
#define JSA_CLASS_MODE     0x00  /* $438A — writes the $1E gate */
#define JSA_CLASS_OKI_PLAY 0x13  /* $5BFE — 3-write OKI play */
#define JSA_CLASS_OKI_STOP 0x14  /* $5C6F — 1-write OKI stop */

/* "No sound index is playing on this voice" (zero-page $52..$55). */
#define JSA_SOUND_NONE 0xFF

/* One translated command. */
typedef struct oki_cmdmap_entry {
    uint8_t command;      /* the 68010 command byte                       */
    uint8_t klass;        /* $6100[command]                               */
    uint8_t argument;     /* $616D[command] — loaded into A at $4386      */
    uint8_t sound_index;  /* argument, for the class-19 tables            */
    uint8_t voice;        /* bits 4-5 of $6567[argument]                  */
    uint8_t sample;       /* $6532[argument] — OKI sample number          */
    uint8_t volume;       /* bits 0-3 of $6567[argument]                  */
    bool    locked;       /* bit 7 of $6567[argument]: NOT preemptible    */
    uint8_t priority;     /* $659C[argument]                              */
    uint8_t byte1;        /* OKI play byte 1: sample | $80                */
    uint8_t byte2;        /* OKI play byte 2: voice mask | volume         */
    uint8_t stop_byte;    /* OKI stop byte for `voice`: $5C6B[voice]      */
} oki_cmdmap_entry_t;

/* The handful of bytes of 6502 zero page the translation reads or writes. */
typedef struct oki_cmdmap_state {
    uint8_t mode;                    /* $1E — nonzero suppresses class 19 */
    uint8_t voice_busy;              /* $2808 — /RDV, bit N = voice N busy */
    uint8_t voice_sound[JSA_VOICE_COUNT]; /* $52..$55, JSA_SOUND_NONE = idle */
} oki_cmdmap_state_t;

/* Maximum OKI bytes one command can emit. */
#define OKI_CMDMAP_MAX_WRITES 3

/* Power-on state: the firmware reset path zeroes $1E and every voice. */
void oki_cmdmap_init(oki_cmdmap_state_t *st);

/* Table lookup only — no gate, no voice contention, no side effects.  This is
 * the raw $6100 -> ($6532,$6567,$659C) mapping that docs/jsa2-sound-findings.md
 * section 4 tabulates.  Returns false if the command is outside the table or
 * is not an OKI class. */
bool oki_cmdmap_lookup(uint8_t command, oki_cmdmap_entry_t *out);

/* Record that `voice` began playing `sound_index`, i.e. what the class-19
 * handler does to $52..$55 / $2808 once its writes land. */
void oki_cmdmap_note_play(oki_cmdmap_state_t *st, uint8_t sound_index);

/* Full translation: ROM state + command byte -> OKI6295 writes.
 *
 * `out` receives 0, 1 or OKI_CMDMAP_MAX_WRITES bytes; the function returns
 * how many were written.  Mutates *st (the $1E gate and the per-voice sound
 * index bookkeeping) exactly as the ROM does. */
size_t oki_cmdmap_translate(oki_cmdmap_state_t *st, uint8_t command,
                            uint8_t out[OKI_CMDMAP_MAX_WRITES]);

/* True when `command` can produce at least one OKI write with the gate open.
 * A command that only writes $1E (class 0) or that is not an OKI class
 * returns false. */
bool oki_cmdmap_plays_anything(uint8_t command);

#ifdef __cplusplus
}
#endif

#endif /* STUNRUN_OKI_CMDMAP_H */