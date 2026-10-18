/* The 68010 text output path, transcribed to C.
 *
 * This is the routine that draws every character the game puts on screen --
 * the attract-mode title block and the service/self-test screens alike. It
 * takes a string *index*, resolves it through a 321-entry pointer table in
 * ROM, reads a four-byte descriptor header, and pushes pixels into the GSP
 * through its host port.
 *
 * ROM ADDRESS RANGES (main CPU, MC68010; file offset == address in the
 * de-interleaved 1 MiB image):
 *
 *   $25E8-$25F6  PROLOGUE, the entry the callers jump to (`move.w #$SEL,D0`
 *                then `jmp $25E8.l` is how a string is selected):
 *                  $25E8 clr.w  $608000.l      -- kick the watchdog
 *                  $25EE andi.w #$e7ff,$c00008.l -- clear HSTCTL $1800 bits
 *   $25F6-$269E  BODY, ending in `jmp (A2)` rather than `rts`: the caller
 *                supplies its own return trampoline in A2.
 *   $2638-$269C  per-character body of that copy
 *   $264E-$2690  the glyph loop nest (D2 outer = 8 rows, D1 inner = 8 pixels)
 *   $269E        jmp (A2)
 *
 * There is a SECOND, byte-identical copy of the whole routine at $2530-$25E6:
 * the same two prologue writes at $2530/$2536, the same pointer-table lookup
 * and the same header decode at $253E-$255C, then the same glyph loop nest at
 * $2560-$25E4 -- but ending in `rts` at $25E6 instead of `jmp (A2)`. In that
 * copy the nest is at $2596-$25E6, the labels this slice's callers name.
 * Nothing here distinguishes the two except that return.
 *
 * STRING DESCRIPTOR FORMAT (established from the descriptor bytes, then
 * corroborated against two recorded MAME captures -- see below):
 *
 *   +0  x, a column byte. Used as `hdr[0] << 6`.
 *   +1  y, a row byte.    Used as `(hdr[1] * $A0) << 8`.
 *   +2  colour byte.      Written to HSTDAT as-is (D7); its `<< 8` copy (D6)
 *                         is OR-ed into HSTDAT for the second colour plane.
 *   +3  character COUNT, not a row count. The outer loop decrements this byte
 *       and re-reads the next text byte each pass, so a 30-byte string has
 *       `hdr[3] = $1E`. Confirmed: $06C53 = `01 0F DF 1E` + a 30-character
 *       string, $06BE7 = `01 00 FF 0E` + a 14-character string.
 *   +4  the text bytes. A `0x01` terminator follows the text in ROM, but
 *       THIS ROUTINE NEVER TESTS IT -- the count in +3 alone bounds the loop.
 *
 * Observed across all 321 descriptors in the ROM: +0 spans $00..$24, +1 spans
 * $00..$15, +2 takes exactly seven values ($00 $3F $5F $70 $9F $DF $FF -- a
 * palette index, not a brightness), and +3 spans 1..54, never 0. 890 of the
 * text bytes are '*'.
 *
 * CONFIRMED (by captured main-CPU host-port writes under MAME, recorded in
 * docs/mc68010-boot-findings.md section 22.2, 240/240 and 112/112 values):
 *
 *   - the coordinate word `((hdr[1] * $A0) << 8) + (hdr[0] << 6) - $800000`.
 *     Descriptor $06C53 (`01 0F DF 1E`) predicts $FF896040 and the capture's
 *     first HSTADR latch is $FF896040. Descriptor $06BE7 (`01 00 FF 0E`)
 *     predicts $FF800040 and its first latch is $FF800040.
 *   - `+$1000` per glyph row, `+$40` per character cell, 8 rows per cell.
 *   - the descriptor colour: selector $47 carries $DF and the capture shows
 *     $00DF then $DFDF written to HSTDAT; `D7 = $DF`, `D6 = $DF00`.
 *   - the glyph table address `$01FBC + 16 * (character - $2A)`, 16 bytes per
 *     glyph, one 16-bit word per row: a 4x8 font of 4 pixels per row.
 *   - the write order for the 32-bit HSTADR store: high halfword first, at
 *     the lower host offset (see HSTADR_H / HSTADR_L below).
 *
 * CONFIRMED FROM THE ROM BYTES (not visible to a $C00002/$C00004-only tap):
 *   - the two prologue writes;
 *   - the nibble rules: a font nibble is 2 bits, bit 0 selects a plain HSTDAT
 *     write of D7 and bit 2 selects a read-modify-write that OR-s in D6.
 *     Across the 45 characters the descriptors reach, only nibbles 0, 1, 4
 *     and 5 occur, so only those three cases can happen in practice;
 *   - that a zero nibble emits NO HSTDAT cycle at all but still walks HSTADR,
 *     which is why a cell is $40 = four $10 steps wide, not $80;
 *   - the two `dbra` seeds. `move.w #$7,D2` sits OUTSIDE the `dbra D2` range
 *     and `move.w #$7,D1` sits INSIDE it, so D1 is a per-row local while D2 is
 *     set once per cell; likewise `move.l D0,$c00002.l` and `move.w (A1)+,D5`
 *     are inside the D2 loop and outside the D1 loop, so D0 is re-latched once
 *     per ROW and A1 advances once per ROW -- a glyph is exactly its 16 bytes.
 *
 * INFERRED / OPEN (see docs/mc68010-boot-findings.md sections 22.6 and 22.5):
 *   - what a host READ of HSTDAT returns. The read-modify-write of D6 is
 *     modelled here as reading back the word this host port last stored, which
 *     is what the capture's $DFDF values imply, but the device-side truth
 *     (a masked read-modify-write of VRAM, `hdgsp_vram_2bpp_w`) is a separate
 *     question this module does not answer.
 *   - how $40 / $1000 / $A000 map onto visible pixels.
 *
 * No ROM contents are embedded or required. The descriptor and the glyph are
 * read through read_rom8/read_rom16, so a test supplies a synthetic image.
 */
#ifndef STUNRUN_TEXT_DRAW_H
#define STUNRUN_TEXT_DRAW_H

#include <stdint.h>

#include "board_iface.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- host-port register offsets, within $C00000-$C03FFF ----------------- */
/* HSTADR is a 32-bit device register with the high halfword at the lower
 * offset. The 68010 is big-endian on a 16-bit bus, so `move.l D0,$c00002.l`
 * stores the MOST SIGNIFICANT halfword first. This is the order the ROM
 * performs and the order the recorded captures show. */
#define STUNRUN_TEXT_HSTADR_H 0x0002u
#define STUNRUN_TEXT_HSTADR_L 0x0004u
#define STUNRUN_TEXT_HSTCTL   0x0008u
#define STUNRUN_TEXT_HSTDAT   0x000Cu

/* ---- descriptor header layout ------------------------------------------- */
#define STUNRUN_TEXT_FIELD_X      0u
#define STUNRUN_TEXT_FIELD_Y      1u
#define STUNRUN_TEXT_FIELD_COLOUR 2u
#define STUNRUN_TEXT_FIELD_COUNT  3u
#define STUNRUN_TEXT_HEADER_SIZE  4u

/* The glyph bias: `subi.b #$2A,D1`. '*' (0x2A) therefore selects glyph 0 at
 * $01FBC, whose sixteen bytes are all zero -- that is how '*' renders as a
 * space. ASCII ' ' (0x20) is NOT the blank: the byte subtraction underflows to
 * $F6 and selects glyph $F6 at $02F1C. */
#define STUNRUN_TEXT_CHAR_BIAS 0x2Au

/* ---- literal constants transcribed from the listing --------------------- */
#define STUNRUN_TEXT_Y_STRIDE   0xA0u    /* mulu.w #$A0,D0            $2618 */
#define STUNRUN_TEXT_X_SHIFT    6u       /* lsl.l #6,D1               $2624 */
#define STUNRUN_TEXT_BIAS       0x800000u /* addi.l #-$800000,D0     $2628 */
#define STUNRUN_TEXT_PIXEL_STEP 0x10u    /* addi.l #$10,$c00002       $267A */
#define STUNRUN_TEXT_ROW_STEP   0x1000u  /* addi.l #$1000,D0         $268A */
#define STUNRUN_TEXT_CELL_REWIND 0x7FC0u /* subi.l #$7FC0,D0         $2694 */
#define STUNRUN_TEXT_HSTCTL_KEEP 0xE7FFu /* andi.w #$E7FF,$c00008    $25EE */

#define STUNRUN_TEXT_ROWS       8u       /* move.w #$7,D2 / dbra D2        */
#define STUNRUN_TEXT_PIXELS     8u       /* move.w #$7,D1 / dbra D1        */

/* Draws string `index` from the ROM pointer table and returns the number of
 * character cells drawn, which is the descriptor's count field -- except that
 * a count of zero wraps the byte decrement and draws 256 cells, as the ROM
 * does. Returns 0 without touching the board for an index the pointer table
 * does not cover. */
uint32_t stunrun_text_draw(stunrun_board_t *board, uint16_t index);

#ifdef __cplusplus
}
#endif

#endif /* STUNRUN_TEXT_DRAW_H */