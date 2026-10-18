/* 68010 reset -> ADSP-handshake boot path, in C.
 *
 * SCOPE. This slice covers the path a `--service` run takes, from the reset
 * entry to the point where control is handed to the ADSP handshake loop:
 *
 *   $506c-$50b0  the reset entry and the callback chain (see MECHANICS below)
 *   $53f2        GSP host-port init + the 8-entry register table at $02b8
 *   $548e        whole-screen GSP fill, 0x383 outer x 0x100 inner
 *   $54c2        GSP fill, the move.w D0,$c0000c.l double-dbra ramp
 *   $566a        GSP fill of the page addressed by a string index in D0
 *   $6086-$60ac  the boot gate: clr.w D2/D3/D5, SELF-TEST, START1
 *   $0f94-$1010  the ADSP handshake init, incl. the seven flag clears
 *   $1032-$10ba  the handshake wait loop and its four flag tests
 *   $13ec-$1416  the /VSYNCB two-phase frame wait
 *
 * Not in scope and NOT transcribed here: $56e6 (the game-state dispatch the
 * loop-back stub reaches), $2326 (the index-draw routine), $10bc, $1368,
 * $145e, $10d2/$1206/$116e/$1756 (the $fa2 dispatch targets), and the GSP
 * register semantics. Each of those appears below only as a named seam with
 * its ROM address, so the call ORDER here is faithful while the bodies stay
 * with whoever owns them.
 *
 * ---------------------------------------------------------------------------
 * MECHANICS -- CONFIRMED: the chain is a continuation-in-a-register dispatch.
 * ---------------------------------------------------------------------------
 * Each stage is entered by `lea ($6,PC),A0` immediately followed by
 * `bra <routine>`, and each routine leaves by `jmp (A0)`. A0 is not a return
 * address in the stack sense; it IS the successor PC, and the register is the
 * only channel. Read literally, `lea ($6,PC),A0` at $5072 loads $507a -- the
 * address of the very next instruction -- and the branch goes to $53f2, which
 * ends `jmp (A0)` at $548c. Verified by the byte encodings, not assumed:
 *
 *   $5072: 41 fa 00 06    lea ($6,PC),A0   ; PC=$5076, +6 = $507a
 *   $5076: 60 00 03 7a    bra $53f2
 *   $507a: 41 fa 00 06    lea ($6,PC),A0   ; PC=$507e, +6 = $5082
 *   $507e: 60 00 04 0e    bra $548e
 *   $5082: 41 fa 00 06    lea ($6,PC),A0   ; PC=$5086, +6 = $508a
 *   $5086: 60 00 04 3a    bra $54c2
 *   $508a: 30 3c 00 47    move.w #$47,D0
 *   $508e: 45 fa 00 08    lea ($8,PC),A2   ; PC=$5092, +8 = $5098
 *   $5092: 4e f9 00 00 25 e8   jmp $25e8.l
 *   $5098: 4d fa 00 06    lea ($6,PC),A6   ; PC=$509c, +6 = $50a0
 *   $509c: 60 00 0f e8    bra $6086
 *   $50a0: 30 3c 00 54    move.w #$54,D0
 *   $50a4: 41 fa 00 06    lea ($6,PC),A0   ; = $50ac
 *   $50a8: 60 00 05 c0    bra $566a
 *   $50ac: 60 00 06 38    bra $56e6
 *
 * The text draw is reached by `jmp`, not `jsr`, and its successor arrives in
 * A2 -- so $25e8 returns through A2 while the GSP fills return through A0.
 * Two different continuation registers, in one chain.
 *
 * WHAT THE RESET VECTOR ACTUALLY POINTS AT. The 68010 reset vector at $000000
 * is SP=$ffffc000, PC=$00000216 -- NOT $506c. $506c is reached from $2b2:
 *
 *   $0216: lea $0000.w,A0 ; A7=(0).l ; VBR=0 ; clr.w $60c000.l ; reset
 *   $022a: ... btst #$5,$60c001.l ; beq $2a0
 *   $02a0: btst #$0,$60c001.l ; bne $2b2
 *   $02b2: jmp $506c.l
 *
 * `beq $2a0` is taken when the SELF-TEST bit is CLEAR, and `PORT_SERVICE(0x20,
 * IP_ACTIVE_LOW)` (harddriv.cpp:798) means CLEAR == pressed. So $506c is only
 * reachable with Service Mode held -- which is what "the path a --service run
 * takes" means here. Bit 0 of $60c001 is the "Diagnostic jumper" DIP
 * (PORT_DIPNAME(0x01,0x01), harddriv.cpp:791): set at its factory default,
 * clear sends $2ac to `jmp $8fee.l` instead.
 *
 * ---------------------------------------------------------------------------
 * $50a0 IS NOT ON THE RESET PATH -- INFERRED, and it matters.
 * ---------------------------------------------------------------------------
 * A6 is loaded with $50a0 at $5098 and $56e6's leaves end `jmp (A6)`; $5882's
 * clear-and-return is the same shape. So $50a0/$50a8/$50ac is the trampoline
 * the GAME loop returns through, not a boot stage: it re-fills GSP page
 * $54 and re-enters $56e6. It is transcribed here (as
 * stunrun_boot_gsp_fill_page plus the documented branch) because it is the
 * other arm of the same chain, but it is NOT visited by
 * stunrun_boot_chain_run and the stage list never contains it.
 *
 * ---------------------------------------------------------------------------
 * THE $801e FAMILY ARE ROM ADDRESSES -- CONFIRMED, and it changes the loop.
 * ---------------------------------------------------------------------------
 * `clr.w $801e.w` is absolute-SHORT, so the 68010 address is $0000801e.
 * `map(0x000000,0x0fffff).rom()` (multisync_68k_map, harddriv.cpp:602) covers
 * it, and the ADSP windows start at $00800000 (init_adsp, harddriv.cpp:4822).
 * These are therefore MAIN-CPU ROM locations, and the region is write-only-
 * discarded: MAME installs no write handler there, so every `clr.w`/`move.w`
 * below is discarded and every `tst.w` reads a constant ROM word. They are
 * modelled as ROM writes and ROM reads through write_rom16/read_rom16, NOT as
 * RAM, precisely so a test cannot accidentally depend on a write sticking.
 *
 * Reproduce the contents (de-interleaved 1 MiB image, file offset == address):
 *
 *   python3 -c "import pathlib;b=pathlib.Path('/tmp/maincpu_flat1m.bin').read_bytes();\
 *     print([(hex(a),hex(b[a]<<8|b[a+1]),chr(b[a]),chr(b[a+1])) \
 *     for a in (0x801e,0x8022,0x8036,0x8046,0x8056,0x8066,0x8076,0x8086)])"
 *
 *   $801e = 0x4552 'ER'   page counter
 *   $8036 = 0x4154 'AT'   advance-page request
 *   $8046 = 0x4255 'BU'   step-page-back request
 *   $8056 = 0x3638 '68'   exit/redraw request
 *   $8066 = 0x2a5a '*Z'   cleared, no reader in $0f94-$1100
 *   $8076 = 0x4645 'FE'   cleared, no reader in $0f94-$1100
 *   $8086 = 0x302e '0.'   cleared, no reader in $0f94-$1100
 *
 * They land in the middle of the ASCII self-test message blob that starts at
 * $8000 ("...ROTATE ONES THRU BUFFER ADDRESS*68010*ROTATE ZEROES THRU BUFFER
 * ADDRESS*..."). The game is writing its flags on top of its own error text.
 *
 * CONSEQUENCE FOR $1036 -- the whole wait loop collapses to one arm. `tst.w
 * $8036.w` can never read zero, so `beq $105a` is NEVER taken, so the $8046
 * and $8056 tests at $105a/$107c are unreachable, and the $fa2 dispatch at
 * $10a8 is unreachable. The only reachable path is:
 *
 *   $103e clr.w $8036.w          (discarded)
 *   $1042 D0 = $801e = 0x4552    (constant)
 *   $1046 D0 += 1  -> 0x4553
 *   $1048 cmpi.w #$4,D0  -> never equal, bne $1052 taken
 *   $1052 move.w D0,$801e.w      (discarded)
 *   $1056 bra $1092 -> led_bar, page_draw, $13ec, back to $1032
 *
 * $0f94 therefore never returns: it is an unbounded per-frame loop, one $13ec
 * (one frame) per iteration. That is why stunrun_boot_chain_run takes an
 * explicit iteration cap and reports whether it hit it.
 *
 * The same defect makes the self-test word checks elsewhere always fail:
 * `move.w $8090.w,D0 ; cmpi.w #$5555,D0` at $514a compares $8090='*B'=0x422a
 * against 0x5555 and always takes `bne $5164`; $516e repeats it against
 * $8092='UF'=0x5546. NOT ported here -- recorded because it is the same root
 * cause and a reader of $801e will hit it next.
 *
 * ---------------------------------------------------------------------------
 * GATE POLARITY -- CONFIRMED, and it is inverted from the usual reading.
 * ---------------------------------------------------------------------------
 *   $6086: 42 42 / 42 43 / 42 45          clr.w D2 / D3 / D5
 *   $608c: btst #$5,$60c001.l            ; SELF-TEST, PORT_SERVICE(0x20, IP_ACTIVE_LOW)
 *   $6094: beq $609c
 *   $6096: jmp $260.l                    ; SET   -> $260
 *   $609c: btst #$2,$a80001.l            ; IPT_START1 (harddriv.cpp:790-801 region)
 *   $60a4: bne $60ac
 *   $60a6: jmp $0f94.l                   ; CLEAR -> the ADSP handshake
 *   $60ac: lea $5ff6.l,A1 ...            ; CLEAR+SET -> the ROM self-test table
 *
 * So: SELF-TEST CLEAR (= Service Mode PRESSED) and START1 CLEAR takes
 * `jmp $0f94`. SELF-TEST SET (not pressed) takes `jmp $260`. The same
 * six-instruction idiom is repeated at $50d8, $554a, $6102, $6176 and $6202,
 * which is why the polarity is not a typo here.
 *
 * `btst #$2,$a80001.l` tests bit 2 of the byte at $a80001, i.e. bit 2 of
 * hd68k_a80000_r's low byte, i.e. IPT_START1. It is NOT active-low: the input
 * table (harddriv.cpp, "mainpcb:SW1"-adjacent shifter port) declares START1
 * IP_ACTIVE_HIGH. Bit SET -> fall through to the self-test; CLEAR -> handshake.
 *
 * ---------------------------------------------------------------------------
 * TRANSPORT NOTES (bus shape, measured elsewhere in this repo)
 * ---------------------------------------------------------------------------
 * `move.l` to the GSP host port is TWO 16-bit writes, high halfword first,
 * to consecutive offsets. That is not an assumption: road_fifo.h records the
 * measurement (32524 writes at $C0000C/$C0000E from one PC, 0 exceptions out
 * of 16262 pairs). stunrun_boot_gsp_wl() below does the same split.
 *
 * THE ONE READ-MODIFY-WRITE DEVIATION. `ori.w #$1800,$c00008.l` appears at the
 * entry of $53f2, $548e, $54c2 and $566a. ORI.W is read-modify-write, and
 * HSTCTL's prior contents are not readable through board_iface.h:
 * read_gsp_control() is the GSP control window ($f4xxxxxx in the GSP's own
 * space), not the 68010 host port. The mask, the address and the fact that
 * four routines do the same thing at entry are transcribed; the
 * preserve-the-other-bits half is not, and this writes 0x1800. A test must not
 * read a prior HSTCTL value out of a run of this slice.
 *
 * stunrun_boot_gsp_host() passes the offset through UNMODIFIED -- the value
 * the ROM itself names, i.e. (68010 address - $c00000). MAME's handler folds
 * it with `(offset / 2) ^ 1` (hd68k_gsp_io_w, harddriv_m.cpp:218); this slice
 * deliberately does not apply that fold, because recording what the ROM
 * addressed is the observable worth keeping.
 *
 * ADDRESSES board_iface.h DOES NOT EXPOSE, recorded not invented:
 *   $60400c / $60401c  clr.w, NWR latch (map(0x604000,0x607fff).w only,
 *                      harddriv.cpp:603). No board member, so these are
 *                      counted in the result as nwr_latch_writes and their
 *                      addresses are published below.
 *   $604000 / $600000  read-only board-ID probes at $0fc2/$0fc8, read from
 *                      unmapped space. Counted as probe_reads, not decoded.
 *
 * NO ROM CONTENTS ARE EMBEDDED OR REQUIRED by this file. The $02b8 register
 * table is read through the board's read_rom16; a test supplies a synthetic
 * image.
 */
#ifndef STUNRUN_BOOT_CHAIN_H
#define STUNRUN_BOOT_CHAIN_H

#include <stddef.h>
#include <stdint.h>

/* The shared board contract. boot_chain.h's public API takes a
 * stunrun_board_t *, so this cannot be optional. */
#include "board_iface.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- ROM addresses of the transcribed stages ---------------------------- */

#define STUNRUN_BOOT_RESET_SP            0xFFFFC000u
#define STUNRUN_BOOT_ENTRY               0x0000506Cu
#define STUNRUN_BOOT_STAGE_GSP_INIT      0x000053F2u
#define STUNRUN_BOOT_STAGE_CLEAR_ALL     0x0000548Eu
#define STUNRUN_BOOT_STAGE_CLEAR_RAMP    0x000054C2u
#define STUNRUN_BOOT_STAGE_DRAW_BANNER   0x0000508Au
#define STUNRUN_BOOT_STAGE_GATE          0x00006086u
#define STUNRUN_BOOT_STAGE_HANDSHAKE     0x00000F94u
#define STUNRUN_BOOT_STAGE_FILL_PAGE     0x0000566Au
#define STUNRUN_BOOT_STAGE_VSYNC         0x000013ECu

/* Targets the gate can reach. */
#define STUNRUN_BOOT_TARGET_HALT         0x00000260u
#define STUNRUN_BOOT_TARGET_SELFTEST     0x000060ACu
#define STUNRUN_BOOT_TARGET_HANDSHAKE    0x00000F94u

/* D0 values the chain hands to the $25e8 draw, and the continuations it loads
 * into the registers just before. $50a4/$50a8's pair is the loop-back arm and
 * is not on the reset path. */
#define STUNRUN_BOOT_TEXT_BANNER         0x0047u   /* $508a: move.w #$47,D0 */
#define STUNRUN_BOOT_TEXT_PAGE           0x0054u   /* $50a0: move.w #$54,D0 */
#define STUNRUN_BOOT_CONT_BANNER         0x00005098u /* $508e: A2 -> $5098 */
#define STUNRUN_BOOT_CONT_LOOPBACK       0x000050ACu /* $50a4: A0 -> $50ac */

/* $50f2 / $50a8: the $02b8 register table has 8 entries of {long, word}. */
#define STUNRUN_BOOT_TABLE_ROM           0x000002B8u
#define STUNRUN_BOOT_TABLE_ENTRY_SIZE    6u
#define STUNRUN_BOOT_TABLE_ENTRIES       8u

/* $53fe: the power-on settle spin. move.l #$96 sets D0.l, and dbra tests the
 * low word, so this is 150 iterations of subq.w/bne with no board access. */
#define STUNRUN_BOOT_DELAY_ITERATIONS    150u

/* $548e: dbra D1 ($ff) inside dbra D2 ($383). */
#define STUNRUN_BOOT_CLEAR_ALL_INNER     256u
#define STUNRUN_BOOT_CLEAR_ALL_OUTER     900u
/* $54c2: two dbra $ff ramps, 256 writes each. */
#define STUNRUN_BOOT_CLEAR_RAMP_ENTRIES  256u
/* $566a: dbra $ff, 256 writes of $ffff. */
#define STUNRUN_BOOT_FILL_PAGE_ENTRIES   256u

/* NWR-latch addresses clr.w'd at $53f2 / $53f4-equivalent; see header text. */
#define STUNRUN_BOOT_NWR_LATCH_A         0x0060400Cu
#define STUNRUN_BOOT_NWR_LATCH_B         0x0060401Cu
#define STUNRUN_BOOT_NWR_LATCH_WRITES    2u
/* Unmapped board-ID probes at $0fc2 / $0fc8. */
#define STUNRUN_BOOT_PROBE_A             0x00604000u
#define STUNRUN_BOOT_PROBE_B             0x00600000u
#define STUNRUN_BOOT_PROBE_READS         2u

/* GSP host-port offsets, named as the ROM names them (not MAME-folded). */
#define STUNRUN_BOOT_GSP_HSTADR          0x0002u
#define STUNRUN_BOOT_GSP_HSTCTL          0x0008u
#define STUNRUN_BOOT_GSP_HSTDAT          0x000Cu

/* hd68k_a80000_r bit 2 is IPT_START1. board_iface.h documents the port but
 * publishes no mask for it, so it is named here. */
#define STUNRUN_BOOT_A80000_BIT_START1   0x0004u

/* ---- the $801e family: absolute-SHORT, therefore main-CPU ROM ----------- */

#define STUNRUN_BOOT_FLAG_COUNT          7u
#define STUNRUN_BOOT_FLAG_PAGE           0x0000801Eu /* cleared at $0ff8 */
#define STUNRUN_BOOT_FLAG_ADVANCE        0x00008036u /* tested  at $1036 */
#define STUNRUN_BOOT_FLAG_BACK           0x00008046u /* tested  at $105a */
#define STUNRUN_BOOT_FLAG_EXIT           0x00008056u /* tested  at $107c */
#define STUNRUN_BOOT_FLAG_SPARE_1        0x00008066u /* cleared, no reader */
#define STUNRUN_BOOT_FLAG_SPARE_2        0x00008076u /* cleared, no reader */
#define STUNRUN_BOOT_FLAG_SPARE_3        0x00008086u /* cleared, no reader */

/* $1048: cmpi.w #$4,D0 -- the page counter's modulus. */
#define STUNRUN_BOOT_PAGE_MODULUS        4u
/* $1070: subq.w wrapped negative, so D0 reloads with 3 -- modulus - 1. */
#define STUNRUN_BOOT_PAGE_WRAP           3u

/* The words this ROM image actually holds at those addresses. Published so a
 * test can build a board that behaves like the real ROM; NOT used by the port.
 * See the header comment for the command that reproduces them. */
#define STUNRUN_BOOT_ROM_WORD_PAGE       0x4552u /* 'ER' */
#define STUNRUN_BOOT_ROM_WORD_ADVANCE    0x4154u /* 'AT' */
#define STUNRUN_BOOT_ROM_WORD_BACK       0x4255u /* 'BU' */
#define STUNRUN_BOOT_ROM_WORD_EXIT       0x3638u /* '68' */

/* $fa2: the four dispatch targets, indexed by (page * 4). Read through the
 * board, never embedded. Only $8056's arm reaches them, and per the header
 * that arm is unreachable with this ROM image. */
#define STUNRUN_BOOT_DISPATCH_TABLE_ROM  0x00000FA2u

/* ---- bounds ------------------------------------------------------------- */

/* The $1032 loop has no exit on the reachable path. This cap exists only so a
 * slice test terminates; it is NOT ROM behaviour and the result records
 * whether it was hit.
 *
 * Why 64: on the reachable path exactly one $13ec -- one frame at 60 Hz --
 * runs per iteration, so 64 iterations is ~1.07 s of board time. The ROM
 * polls these flags once per frame from $1032 itself, so any flag raised by
 * anything the hardware does would be seen long inside 64 iterations, and the
 * test finishes instantly. Pass 0 to allow no iterations at all (defined, not
 * an error); it reports handshake_cap_hit. */
#define STUNRUN_BOOT_HANDSHAKE_MAX_ITERATIONS 64u

/* $13ec waits on a hardware line with no ROM-side bound. Same reasoning:
 * 512 reads per phase. Any real frame is at most a few thousand 68010
 * instructions of slack, so a correct board never reaches it; a stub that
 * holds the line still terminates and reports vsync.capped. */
#define STUNRUN_BOOT_VSYNC_MAX_READS     512u

#define STUNRUN_BOOT_MAX_STAGES          16u
#define STUNRUN_BOOT_MAX_TEXT_DRAWS      4u

/* ---- which arm of the $6086 gate was taken ------------------------------ */

typedef enum stunrun_boot_gate {
    /* SELF-TEST SET (not pressed): jmp $260.l -- the halt/error handler. */
    STUNRUN_BOOT_GATE_HALT = 0,
    /* SELF-TEST CLEAR (pressed) and START1 CLEAR: jmp $0f94.l. */
    STUNRUN_BOOT_GATE_HANDSHAKE = 1,
    /* SELF-TEST CLEAR (pressed) and START1 SET: fall through to $60ac, the
     * ROM self-test table. Its body is not transcribed here. */
    STUNRUN_BOOT_GATE_SELFTEST_TABLE = 2
} stunrun_boot_gate_t;

/* ---- out-of-scope ROM calls, identified by address ----------------------- */

typedef enum stunrun_boot_seam_id {
    STUNRUN_BOOT_SEAM_TEXT_DRAW = 0, /* $25e8  draw string D0, return via A2 */
    STUNRUN_BOOT_SEAM_PRE_67A,       /* $67a   */
    STUNRUN_BOOT_SEAM_PRE_3F8,       /* $3f8   */
    STUNRUN_BOOT_SEAM_PRE_570,       /* $570   */
    STUNRUN_BOOT_SEAM_PRE_636,       /* $636   */
    STUNRUN_BOOT_SEAM_ADSP_DM_CLEAR, /* $132a  0x2000 longword clears */
    STUNRUN_BOOT_SEAM_IRQ_VECTOR,    /* $1014-$1024, writes main RAM */
    STUNRUN_BOOT_SEAM_INPUT_SCAN,    /* $1368  also re-checks the gate */
    STUNRUN_BOOT_SEAM_LED_BAR,       /* $145e */
    STUNRUN_BOOT_SEAM_PAGE_DRAW,     /* $10bc  reads the ROM word at $801e */
    STUNRUN_BOOT_SEAM_EXIT_PAGE,     /* a $fa2 dispatch target */
    STUNRUN_BOOT_SEAM_COUNT
} stunrun_boot_seam_id_t;

/*
 * The seams. Every member is OPTIONAL: a NULL entry is skipped and counted in
 * result->seam_calls[], so a test can tell "not called" from "not wired".
 *
 * CHOICE OF SEAM, stated: function pointers, not a second context struct and
 * not weak symbols. The text draw is the case that forced it -- $25e8 is being
 * ported separately as stunrun_text_draw and this file must not depend on it,
 * and a pointer lets a caller wire that routine in later with no edit here. The
 * same choice is applied to every other out-of-scope call so the file has one
 * mechanism rather than two. `ctx` is the seams' own; the board keeps its own.
 */
typedef struct stunrun_boot_seams {
    void *ctx;

    /* $25e8. `index` is D0.w, the string-table index. `cont` is the 68010
     * address the ROM had just loaded into A2, i.e. the PC the drawer returns
     * to -- the seam is expected to resume there conceptually. Passing it
     * rather than calling back keeps the continuation visible as data. */
    void (*text_draw)(void *ctx, uint16_t index, uint32_t cont);

    void (*pre_67a)(void *ctx);            /* $67a  */
    void (*pre_3f8)(void *ctx);            /* $3f8  */
    void (*pre_570)(void *ctx);            /* $570  */
    void (*pre_636)(void *ctx);            /* $636  */
    void (*adsp_dm_clear)(void *ctx);      /* $132a */
    void (*adsp_irq_vector_init)(void *ctx); /* $1014-$1024 */
    void (*input_scan)(void *ctx);         /* $1368 */
    void (*led_bar)(void *ctx);            /* $145e */
    void (*page_draw)(void *ctx);          /* $10bc */
    /* A $fa2 dispatch target, selected by (page * 4) at $10b2-$10b8. `page` is
     * the low word of D0 after lsl.w #2, `target` is the long the table at
     * $fa2 holds there -- read through the board, never embedded. */
    void (*exit_page)(void *ctx, uint16_t page, uint32_t target);
} stunrun_boot_seams_t;

/* ---- $13ec-$1416: the two-phase /VSYNCB frame wait ---------------------- */

typedef struct stunrun_boot_vsync {
    uint32_t reads;    /* every read_port0 the routine issued */
    uint8_t  capped;   /* a phase hit max_reads_per_phase */
    uint8_t  halted;   /* $1406-$1410: SELF-TEST SET -> jmp $260.l */
} stunrun_boot_vsync_t;

/* ---- what the chain did ------------------------------------------------- */

typedef struct stunrun_boot_result {
    /* $506c: the value A7 is given. */
    uint32_t reset_sp;

    /* Ordered stage addresses, by visit. stage_count may exceed the array;
     * the extra visits are counted but not stored. */
    unsigned stage_count;
    uint32_t stages[STUNRUN_BOOT_MAX_STAGES];

    /* Board accesses attributable to the transcribed code. */
    unsigned nwr_latch_writes;    /* $60400c + $60401c at $53f2 */
    unsigned probe_reads;         /* $604000 + $600000 at $0fc2/$0fc8 */
    unsigned delay_iterations;    /* $53fe, 150, no board access */
    unsigned gsp_host_writes;     /* every write_gsp_host this slice issued */
    unsigned watchdog_writes;

    /* $25e8 invocations and their arguments, in order. */
    unsigned text_draws;
    uint16_t text_index[STUNRUN_BOOT_MAX_TEXT_DRAWS];
    uint32_t text_cont[STUNRUN_BOOT_MAX_TEXT_DRAWS];

    /* $6086 gate. */
    uint8_t  selftest_bit;        /* $60c001 bit 5 as read */
    uint8_t  start1_bit;          /* $a80001 bit 2 as read */
    stunrun_boot_gate_t gate;

    /* $0f94: the seven flag clears, in the order issued. */
    unsigned flag_clears;
    uint32_t flag_clear_addr[STUNRUN_BOOT_FLAG_COUNT];

    /* $1032 loop. */
    uint32_t handshake_iterations;   /* passes through $1032 */
    uint8_t  handshake_cap_hit;      /* ran out of max_handshake_iterations */
    unsigned handshake_advance;      /* $8036 arm, $103e-$1056 */
    unsigned handshake_back;         /* $8046 arm, $1062-$1078 */
    unsigned handshake_exit;         /* $8056 arm, $1084-$1088 */
    unsigned handshake_dispatch;     /* $10a8 arm */
    unsigned handshake_halt;         /* SELF-TEST seen set inside $13ec */

    /* $13ec accounting, summed over every call the run made. */
    unsigned vsync_calls;
    unsigned port0_reads;            /* every read_port0 the run issued */
    unsigned a800_reads;             /* read_a80000: $609c's IPT_START1 */

    /* Page counter as the loop left it: the value last computed for $801e. */
    uint16_t page;

    /* Per-seam invocation counts, indexed by stunrun_boot_seam_id_t. */
    unsigned seam_calls[STUNRUN_BOOT_SEAM_COUNT];
} stunrun_boot_result_t;

/* ------------------------------------------------------------------------ */

/* Run $506c -> $53f2 -> $548e -> $54c2 -> $508a -> $6086, then the $0f94 arm
 * if the gate selects it. Returns by value; never NULL.
 *
 * `max_handshake_iterations` bounds the $1032 loop -- see
 * STUNRUN_BOOT_HANDSHAKE_MAX_ITERATIONS for why a bound exists at all.
 *
 * `seams` may be NULL, in which case every out-of-scope call is skipped and
 * counted. `board` must be non-NULL with read_port0, read_a80000, read_rom16,
 * write_rom16, write_watchdog and write_gsp_host wired. */
stunrun_boot_result_t stunrun_boot_chain_run(
    stunrun_board_t *board, const stunrun_boot_seams_t *seams,
    uint32_t max_handshake_iterations);

/* $13ec-$1416 on its own. max_reads_per_phase of 0 uses
 * STUNRUN_BOOT_VSYNC_MAX_READS. `out` may be NULL. */
void stunrun_boot_wait_vsync(stunrun_board_t *board,
                             uint32_t max_reads_per_phase,
                             stunrun_boot_vsync_t *out);

/* $566a-$5698: fill the GSP page for string index `index`. The address is
 * (index << 12) - 0x80000, computed exactly as $566a does (clr.l D1,
 * move.w D0,D1, two lsl.l, one addi.l). 256 writes of $ffff at $c0000c. */
void stunrun_boot_gsp_fill_page(stunrun_board_t *board, uint16_t index);

#ifdef __cplusplus
}
#endif

#endif /* STUNRUN_BOOT_CHAIN_H */