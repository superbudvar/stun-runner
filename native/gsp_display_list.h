/* Transcription of the STUN Runner 68010 GSP display-list interpreter, the
 * sparse fill that feeds it, and the table loader it calls.
 *
 * Provenance: `136070-2102.200r`, read out of the de-interleaved 1 MiB main-CPU
 * image.  ROM ranges, per routine:
 *
 *   0x000225B0-0x000225CC  the 16-byte lookup-table loader (`bsr`/`jsr`ed)
 *   0x000225CE-0x000225E2  the terminator (dead code past the `jsr $21286`)
 *   0x000225E4-0x00022748  the byte-stream interpreter
 *   0x0002418-0x0002432   the base-address helper the fill calls
 *   0x00000BC4-0x00000C26  the sparse fill whose middle is the 0xBF4 nest
 *   0x00000C28-0x00000C44  caller: 15 blocks of the $20-strided fill
 *   0x00000C46-0x00000C62  a 32-word strided fill (NOT called by 0xBF4)
 *
 * Every routine below carries a comment giving its ROM address range.  This is
 * a decompilation, not an improvement: loop seeds, the `dbra` targets, the
 * byte-vs-word reads, the pending-word flag and the write order are transcribed
 * as the ROM performs them, and the oddities are annotated rather than
 * corrected.  See `docs/mc68010-boot-findings.md` §10 for the reading, and
 * `BUGS.md` SR-10..SR-14 for the defects this exposes in the original ROM.
 *
 * ── What the interpreter touches ────────────────────────────────────────────
 * $C0000C (HOST_DATA) for every emitted word, $608000 (the watchdog) once per
 * opcode, and two 32-byte lookup tables in 68010 RAM.  It never writes the
 * host address latches: the caller must have latched a GSP address, and every
 * word lands at the latched address plus whatever the 0x0800 HOST_DATA
 * post-increment adds.
 *
 * The two tables hold the same 16 bytes stored twice, 0x20 apart.  The loader
 * (0x225B0) writes stream byte i twice: once as a BYTE at $FF900A + 2i, and once
 * as the low byte of the WORD at $FF902A + 2i - which is to say at $FF902B + 2i,
 * with the even half left as 0x00.  Every reader loads the word at
 * $FF900A + 2*index and then immediately overwrites the low byte with the byte
 * at $FF902B + 2*index, so the word load is DEAD (the following `move.b ...,D3`
 * clears the upper 24 bits) and what actually reaches the FIFO is always table
 * byte (index / 2) as a 16-bit word.  The word load is reproduced faithfully
 * (SR-11) even though nothing observes it.
 *
 * ── Host port encoding ─────────────────────────────────────────────────────
 * Same window as `native/gsp_boot.h`: the TMS34010 host port registers are
 * ADDRESS_L = 0, ADDRESS_H = 1, DATA = 2, CONTROL = 3
 * (tms34010.h:1094-1097), reached through `hd68k_gsp_io_r/w`
 * (atari/harddriv.cpp:602) with `offset = (offset / 2) ^ 1`.  A 68010
 * `move.l #imm,$c00002.l` therefore delivers HSTADRH first and HSTADRL second,
 * and `move.w data,$c0000c.l` writes the word at the latched address and then
 * adds 0x10 to the latches if HSTCTL bit 0x0800 is set.
 *
 * Nothing here implements the transport: every effect on hardware goes through
 * the callbacks in stunrun_gsp_display_list_t.
 */
#ifndef STUNRUN_GSP_DISPLAY_LIST_H
#define STUNRUN_GSP_DISPLAY_LIST_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── 68010-side addresses this code writes ───────────────────────────────── */

#define STUNRUN_GSP_DL_WATCHDOG 0x00608000u /* clr.w $608000.l */

/* The two copies of the 16-byte lookup table, as 0x225B0 lays them out. */
#define STUNRUN_GSP_DL_TABLE_LO 0x00FF900Au /* move.b D0,(-$20,A1) with
                                              A1 = $FF902A */
#define STUNRUN_GSP_DL_TABLE_HI 0x00FF902Au /* move.w D0,(A1)+ */

/* ── GSP-side destination address the sparse fill latches once per record ── */

#define STUNRUN_GSP_DL_CONTROL_LO 0xF4000000u
#define STUNRUN_GSP_DL_2BPP_VRAM_BASE 0x02000000u

/* ── Host port control mask ──────────────────────────────────────────────── */

#define STUNRUN_GSP_DL_HOSTCTL_AUTO_INCREMENT 0x1800u /* ori.w #$1800,$c00008 */

/* ── Host port seam ──────────────────────────────────────────────────────── */

/* Deliberately the same field set and order as `stunrun_gsp_host_t` in
 * gsp_boot.h, plus `poke_byte`: 0x225B0 stores the lookup table a BYTE at a
 * time (`move.b D0,(-$20,A1)`), and the odd half of each 16-bit word is read
 * back by the interpreters' `move.w (-$20,A1,D4.w)`.  A word-granular store
 * would silently change what those reads return, so the seam is byte-wide
 * here.  All callbacks must be non-NULL. */
typedef struct stunrun_gsp_display_list {
    void *ctx;

    /* HSTADRL (ROM `move.l #imm,$c00002.l`, high half first). */
    void (*set_host_address_lo)(void *ctx, uint16_t value);
    /* HSTADRH. */
    void (*set_host_address_hi)(void *ctx, uint16_t value);
    /* HSTCTL, plain write.  Not used by any routine here; present so this
     * v-table stays interchangeable with stunrun_gsp_host_t. */
    void (*set_host_control)(void *ctx, uint16_t value);
    /* HSTCTL |= mask (ROM `ori.w #$1800,$c00008.l`). */
    void (*or_host_control_mask)(void *ctx, uint16_t mask);
    /* HSTDAT: write `value` at the latched GSP address, honouring the 0x0800
     * post-increment.  ROM `move.w <data>,$c0000c.l`. */
    void (*write_fifo_word)(void *ctx, uint16_t value);

    /* Plain 68010 memory: the lookup tables. */
    uint16_t (*peek_word)(void *ctx, uint32_t address);
    uint8_t (*peek_byte)(void *ctx, uint32_t address);
    /* 68010 stores: the watchdog kick and the lookup-table loader. */
    void (*poke_word)(void *ctx, uint32_t address, uint16_t value);
    void (*poke_byte)(void *ctx, uint32_t address, uint8_t value);
} stunrun_gsp_display_list_t;

/* ── 0x225E4: the byte-stream interpreter ────────────────────────────────── */

/* How the interpreter stopped.  Both values end the ROM's loop; see the
 * header note about why neither runs the ROM's own epilogue. */
typedef enum {
    /* Reached `jsr $21286.l` at $225CE: a nonzero first stream byte, opcode
     * 0x80, or opcode 0xC0.  $21286 is the SSP reset entry and never returns,
     * so the instruction that follows the jsr does not execute. */
    STUNRUN_GSP_DL_END_RESET = 0,
    /* Opcode 0x40 only.  $22672 does `subq.w #1,D0` / `bmi $225d4`, so it lands
     * in the same epilogue WITHOUT the jsr.  Still dead on real hardware (the
     * epilogue's `movem.l (A7)+,D2-D5/A2-A3` pops a frame the prologue never
     * pushed here).  This is the one terminator that WOULD have run
     * $225d4-$225da, i.e. emitted a `clr.b D3` / `move.w D3,(A2)` flush word -
     * which this slice does not emit, because $225ce's jsr never returns and
     * the two paths are indistinguishable to a caller that never gets there. */
    STUNRUN_GSP_DL_END_OPCODE_40 = 1
} stunrun_gsp_display_list_end_t;

typedef struct stunrun_gsp_display_list_result {
    /* The ROM returns its final stream pointer in D0 (`move.l A0,D0` at
     * $225DC).  On every real terminator this is meaningless, because $21286
     * never returns; it is reported because it is the only handle the C has on
     * how much stream was consumed. */
    uint32_t stream_end;
    /* Words handed to write_fifo_word.  The ROM has no such counter; it exists
     * so a test can assert the emission count without re-deriving it from the
     * port log. */
    unsigned words_emitted;
    stunrun_gsp_display_list_end_t end;
} stunrun_gsp_display_list_result_t;

/* Run one display list.  `stream` is the raw byte stream the ROM would have
 * had in A0; it is read, never written, and the low bit of its address is
 * significant (the ROM branches on it - see the notes in the .c).
 *
 * Stops on opcode 0x80, on opcode 0xC0, and on opcode 0x40.  A nonzero FIRST
 * byte also ends it (at $22602, before any opcode is decoded).  There is no
 * other exit - in particular opcode 0x00 is a table reload, not a stop. */
void stunrun_gsp_display_list_run(stunrun_gsp_display_list_t *io,
                                  const uint8_t *stream,
                                  stunrun_gsp_display_list_result_t *result);

/* 0x225B0: load the 16-byte lookup table out of the stream.  Stores byte i at
 * $FF900A + 2i and word i at $FF902A + 2i, then leaves A1 = $FF902A.  Consumes
 * exactly 16 bytes and clobbers the ROM's D0, D1 and A1 (which the
 * interpreters rely on: neither of them sets A1 itself). */
void stunrun_gsp_display_list_load_tables(stunrun_gsp_display_list_t *io,
                                          const uint8_t *stream,
                                          uint32_t *consumed);

/* ── 0x0BC4: the sparse fill ─────────────────────────────────────────────── */

/* One 8-byte record of the fill's table, exactly as the ROM addresses it.
 *
 *   0x0BCC: `move.w (A0)+,D5` takes the record COUNT (seed + 1 passes) from a
 *           2-byte word that precedes the records, and post-increments A0.
 *   0x0BCE: `move.w (A0),D1`   -> record.row_term, folded into the base
 *                                address by $2418.
 *   0x0BD0: `move.w (2,A0),D0`  -> record.tile.
 *   0x0BD4: `jsr $2418`         -> D0 = $02000000 + tile * $2800 + (row_term
 *                                & $FF) * 16.
 *   0x0BE4: `move.w (6,A0),$c0000c.l` -> record.control, the ONE control word
 *                                pushed per record, to $F4000000.
 *   0x0BEC: `move.w (4,A0),D1`  -> record.columns, which REPLACES the value
 *                                $2418 used, so it only sets the inner loop
 *                                seed and never the address.
 *   0x0C20: `addq.l #8,A0`      -> the record stride.
 */
typedef struct stunrun_gsp_display_list_record {
    uint16_t row_term; /* (A0)+0 -> D1 for the base address */
    uint16_t tile;     /* (A0)+2 -> D0.w */
    uint16_t columns;  /* (A0)+4 -> D1, the inner `dbra` seed (seed + 1) */
    uint16_t control;  /* (A0)+6 -> the word pushed to $F4000000 */
} stunrun_gsp_display_list_record_t;

/* Run the fill: `stream` is the 2-byte count word followed by
 * count+1 8-byte records, i.e. the byte layout the ROM sees, not a struct
 * array.  Every record latches $F4000000 and pushes its control word, then
 * runs the 0xBF4 nest. */
void stunrun_gsp_display_list_sparse_fill(stunrun_gsp_display_list_t *io,
                                          const uint8_t *stream);

/* 0x0C46: push `word` to 32 addresses strided $20 apart starting at `base`.
 *
 * NOT reachable from the sparse fill - `0x0BF4` calls nothing.  The recorded
 * direct caller is `bsr $c46` at 0x0C36 inside 0x0C28. */
void stunrun_gsp_display_list_fill_stride_0x20(stunrun_gsp_display_list_t *io,
                                                uint32_t base, uint16_t word);

/* 0x0C28-0x0C44: 15 blocks, each 32 writes of 1 at $20-strided addresses,
 * starting at $02000000 and advancing block bases by $4000.  This routine
 * does NOT set HSTCTL; preserve the caller's auto-increment state.  Effect
 * slice only: not a register/CCR/cycle-compatible live CPU replacement. */
void stunrun_gsp_display_list_fill_stride_blocks(stunrun_gsp_display_list_t *io);

#ifdef __cplusplus
}
#endif

#endif /* STUNRUN_GSP_DISPLAY_LIST_H */
