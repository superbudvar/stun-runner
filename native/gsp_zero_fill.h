/* MC68010 zero-fill stage at ROM $0000548E-$000054C0 (maincpu program ROM).
 *
 * Source chip: the STUN Runner maincpu program ROM, assembled flat by
 * `tools/build-maincpu-image` from the six 136070-210x/300x/5xxx/6xxx pairs.
 * Image used for transcription:
 *   roms/stunrun/stunrun_maincpu.bin
 *   sha256 29c911a21d1abe780419cd3a924080756490f2d008776995dd48c7b2bedc5b6c
 * Bytes re-checked with musashi's m68kdasm (`tools/dis68k.c`); the
 * transcription is in the file comment of gsp_zero_fill.c.
 *
 * ── How it is reached: there is no xref ────────────────────────────────
 * Nothing `jsr`s or `jmp`s to $548E.  It is entered by `bra` out of a
 * continuation-address chain, which is exactly why a xref search finds
 * nothing and why this routine has to be found by reading the boot path:
 *
 *   $506C: movea.l #$ffffc000,A7
 *   $5072: lea ($6,PC),A0          ; A0 = $507A
 *   $5076: bra $53F2               ; GSP boot, terminates with jmp (A0)
 *   $507A: lea ($6,PC),A0          ; A0 = $5082
 *   $507E: bra $548E               ; <- enters here
 *   $5082: lea ($6,PC),A0
 *   $5086: bra $54C2               ; the next stage
 *
 * It returns with `jmp (A0)` at $54C0, so A0 is the caller's continuation
 * address.  The routine touches no general memory: its only stores are to the
 * GSP host window and to the watchdog.  In the boot
 * chain that continuation is $5082 -- the pattern fill at $54C2, which
 * returns to $508A, which draws selector $47
 * (`PRESS*START*TO*BYPASS*SELFTEST`).  This fill is the previous basic block
 * of the first text draw; it is not a separate phase of the boot.
 *
 * ── THE POINT: this routine writes 230,400 words to nowhere ────────────
 * It reads like a VRAM clear: enable auto-increment, latch $FF800000, then
 * push 900 x 256 words.  It is NOT one.  Under the pinned driver both of the
 * addresses that would move a byte are discarded:
 *
 *   `multisync_68k_map` maps $C00000-$C03FFF with no shift
 *     (atari/harddriv.cpp:611), so `hd68k_gsp_io_w`
 *     (atari/harddriv_m.cpp:215-221) computes `offset = (offset / 2) ^ 1`
 *     and calls `tms340x0_device::host_w(offset, data)`.
 *   `tms34010.cpp:1875` switches that value against exactly four cases --
 *     TMS34010_HOST_ADDRESS_L 0, _ADDRESS_H 1, _DATA 2, _CONTROL 3 -- and
 *     anything else falls out of the switch and is dropped with no side
 *     effect at all.
 *
 * So the window has four live words:
 *
 *     $C00000  offset 0  -> 1  HSTADRH
 *     $C00002  offset 2  -> 0  HSTADRL
 *     $C00004  offset 4  -> 3  HSTCTL
 *     $C00006  offset 6  -> 2  HSTDAT
 *     $C00008  offset 8  -> 5  discarded
 *     $C0000C  offset 12 -> 7  discarded
 *
 * Two of this routine's three data addresses decode and one does not, and the
 * distinction matters:
 *
 *   $C00002 / $C00004  DECODE.  The `move.l #$ff800000,$c00002.l` at $5496
 *                      really does reach the GSP, as HSTADRL := $FF80 and
 *                      HSTCTL := $0000.  Note which half is which: under this
 *                      decode $C00002 is HSTADRL and $C00004 is HSTCTL, so
 *                      the routine leaves $FF80 in the address latch and
 *                      $0000 in the control register.  It meant to write
 *                      $FF80:0000 to HSTADRH:HSTADRL.  Do not "correct" this
 *                      either; it is what the ROM and the driver together do.
 *   $C00008            DISCARDED.  The `ori.w #$1800,$c00008.l` at $548E goes
 *                      nowhere, so INCW is never set.
 *   $C0000C            DISCARDED.  All 230,400 `move.w D0,$c0000c.l` writes
 *                      go nowhere.
 *
 * So two bus cycles of this routine reach the GSP, both of them latches, and
 * NOT ONE reaches HSTDAT.  That is the precise content of "this loop is a
 * no-op on VRAM": it is not that nothing lands, it is that the register that
 * would have moved a byte is one of the two the driver drops.  A test asserting
 * "zero writes reach the GSP" would be wrong and would fail; the load-bearing
 * assertion is zero HSTDAT writes.
 *
 * That the data is dropped is measured, not inferred: patching
 * `move.w #$0,D0` at $54A0 to `move.w #$1a1a,D0` in a copy of the ROM set
 * yields a BYTE-IDENTICAL 4 MiB GSP share at frame 600 (0 of 4,194,304 bytes
 * differ) and an all-zero share at every frame from 48 to 360.  If even one of
 * the 230,400 writes had landed, the patched run would contain a nonzero cell.
 * It contains none.  See findings section 31.2.
 *
 * This is a live open question and it is NOT resolved here: either the ROM
 * targets a host-port register layout this driver does not implement, or the
 * driver's decode is wrong and real hardware accepts writes at $C00008/$C0000C.
 * The correct port therefore reproduces the mechanism and reports the
 * discard, rather than "correcting" the addresses until the loop does
 * something.  A port that cleared VRAM here would be asserting something the
 * oracle has already refuted.
 *
 * ── The seam ──────────────────────────────────────────────────────────
 * Like `stunrun_mc68010_text_port_t`, this is a transport seam: every 16-bit
 * program-space cycle the routine drives is reported, including the read
 * half of the `ori.w` read-modify-write at $548E.  There is deliberately NO
 * HSTDAT -> VRAM hook, and that omission is the finding, not an oversight:
 * the only writes this routine issues to the GSP window never decode to a
 * register, so there is nothing for such a hook to do.  The one write that
 * does reach the machine is the watchdog kick at $608000, which is outside
 * the window entirely and is reported as such.
 */
#ifndef STUNRUN_GSP_ZERO_FILL_H
#define STUNRUN_GSP_ZERO_FILL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── ROM addresses ────────────────────────────────────────────────────── */

#define STUNRUN_GSP_ZERO_FILL_PC 0x0000548Eu
/* One past the last byte of the routine; $54C2 is the next stage. */
#define STUNRUN_GSP_ZERO_FILL_PC_END 0x000054C2u
/* $507E `bra $548E` -- the only way in. */
#define STUNRUN_GSP_ZERO_FILL_CALLER_BRA 0x0000507Eu
/* $507A `lea ($6,PC),A0` -- the continuation this routine jumps to when the
 * boot chain calls it.  Supplied by the caller here, not assumed. */
#define STUNRUN_GSP_ZERO_FILL_BOOT_CONTINUATION 0x00005082u

/* ── 68010 addresses the routine writes ───────────────────────────────── */

/* ori.w #$1800,$c00008.l ($548E).  Word offset 4 -> decoded index 5 ->
 * discarded. */
#define STUNRUN_GSP_ZERO_FILL_CONTROL 0x00C00008u
/* move.l #$ff800000,$c00002.l ($5496).  A long access to a 16-bit bus is two
 * word writes, most significant half first, so the ROM's one address becomes
 * two cycles: $FF80 here, and $0000 at the next word.  Under this driver's
 * decode $C00002 is HSTADRL and $C00004 is HSTCTL, and BOTH decode -- so
 * unlike the control and data writes, this pair really does reach the GSP.
 * (It is the control word the routine then fails to OR into, so the GSP is
 * left holding $FF80:0000 in the address latches and nothing in HSTCTL.)
 * $C00004 is not a separate ROM address; the ROM never names it. */
#define STUNRUN_GSP_ZERO_FILL_ADDRESS_HIGH 0x00C00002u
#define STUNRUN_GSP_ZERO_FILL_ADDRESS_LOW 0x00C00004u
/* move.w D0,$c0000c.l ($54AC).  Word offset 6 -> decoded index 7 ->
 * discarded.  All 230,400 data writes land here. */
#define STUNRUN_GSP_ZERO_FILL_DATA 0x00C0000Cu
/* clr.w $608000.l ($54B6) -- the watchdog kick, and the only write in this
 * routine that the board keeps.  Outside the GSP window. */
#define STUNRUN_GSP_ZERO_FILL_WATCHDOG 0x00608000u

/* ── Literals ────────────────────────────────────────────────────────── */

/* ori.w #$1800 -- 0x0800 post-increments after a HOST_DATA write, 0x1000
 * post-increments after a HOST_DATA read.  OR-ed into a discarded address. */
#define STUNRUN_GSP_ZERO_FILL_CONTROL_OR 0x1800u
/* move.l #$ff800000,$c00002.l ($5496). */
#define STUNRUN_GSP_ZERO_FILL_GSP_BASE 0xFF800000u
/* move.w #$0,D0 ($54A0) -- the word every data write pushes. */
#define STUNRUN_GSP_ZERO_FILL_SEED 0x0000u

/* A `dbra Dn,target` runs seed + 1 times.  These are the ROM's seeds, not the
 * iteration counts, and the iteration counts are derived from them so the
 * relation cannot drift:
 *
 *     $54A4 move.w #$383,D2  ->  $0384 = 900 passes
 *     $54A8 move.w #$ff,D1   ->  $0100 = 256 words per pass
 *
 * 900 x 256 = 230,400 HSTDAT cycles.  The inner `dbra` is inside the outer
 * one, and the watchdog kick at $54B6 sits between them, so it fires once per
 * 256 data words and never once per pass of something coarser. */
#define STUNRUN_GSP_ZERO_FILL_PASS_SEED 0x0383u
#define STUNRUN_GSP_ZERO_FILL_WORD_SEED 0x00FFu
#define STUNRUN_GSP_ZERO_FILL_PASSES (STUNRUN_GSP_ZERO_FILL_PASS_SEED + 1u)
#define STUNRUN_GSP_ZERO_FILL_WORDS_PER_PASS \
    (STUNRUN_GSP_ZERO_FILL_WORD_SEED + 1u)
#define STUNRUN_GSP_ZERO_FILL_DATA_WRITES \
    (STUNRUN_GSP_ZERO_FILL_PASSES * STUNRUN_GSP_ZERO_FILL_WORDS_PER_PASS)

/* ── The pinned driver's host-port decode ─────────────────────────────── */

/* The window, as installed by `multisync_68k_map` with no shift. */
#define STUNRUN_GSP_HOST_WINDOW_BASE 0x00C00000u
#define STUNRUN_GSP_HOST_WINDOW_END 0x00C04000u

/* What `(offset / 2) ^ 1` decodes to, as tms34010.cpp:1875 sees it.  The
 * enum values ARE the switch case numbers, so a caller can compare against
 * the TMS34010 constants without translating.  Negative values are the two
 * non-decodes, which are separate outcomes and must not be conflated: a
 * window write that is dropped still occupies a bus cycle, an address outside
 * the window is not part of this port at all.
 *
 * This predicate exists so that "this write was discarded" is a fact the
 * caller can query, instead of an `if (address >= 0xC00008)` sprinkled through
 * the loop.  See the "THE POINT" note above for why the answer here is what it
 * is -- and why it is not something to quietly relax.
 */
typedef enum stunrun_hostport_decode {
    /* Outside $C00000-$C03FFF: not a host-port access (the watchdog). */
    STUNRUN_HOSTPORT_OUTSIDE = -1,
    /* Inside the window, but `(offset/2)^1` is not 0-3: the driver drops it. */
    STUNRUN_HOSTPORT_DISCARDED = -2,
    STUNRUN_HOSTPORT_HSTADRL = 0, /* $C00002 */
    STUNRUN_HOSTPORT_HSTADRH = 1, /* $C00000 */
    STUNRUN_HOSTPORT_HSTDAT = 2,  /* $C00006 */
    STUNRUN_HOSTPORT_HSTCTL = 3   /* $C00004 */
} stunrun_hostport_decode_t;

/* Classify one 68010 program-space address.  Returns one of the enum values. */
stunrun_hostport_decode_t stunrun_gsp_zero_fill_classify(uint32_t address);

/* "HSTADRL" / "HSTDAT" / "discarded" / "outside" -- for failure messages, so
 * a diagnostic names the decode and not just a number. */
const char *stunrun_gsp_zero_fill_decode_name(int decode);

/* ── Transport seam ──────────────────────────────────────────────────── */

/* 68010 program-space bus observer.  Every cycle is reported, INCLUDING the
 * read half of the `ori.w` read-modify-write, because that read is what makes
 * the routine's control-word handling a read-modify-write rather than a plain
 * store.  Either callback may be NULL; the routine then runs with no observer
 * and still reports its own counts.
 */
typedef struct stunrun_gsp_zero_fill_port {
    void *ctx;
    void (*bus_read)(void *ctx, uint32_t address, uint16_t value);
    void (*bus_write)(void *ctx, uint32_t address, uint16_t value);

    /* Caller-preset value returned by the read half of the `ori.w` at $548E.
     * $C00008 is a discarded address, so what the driver would hand back
     * there is not modelled and this is a stand-in; it exists so the OR is
     * visible in the trace as a read-modify-write.  Nothing downstream
     * depends on it, because the write half is discarded too. */
    uint16_t hstctl;
} stunrun_gsp_zero_fill_port_t;

/* What the routine did, all of it observable bus traffic plus the arithmetic
 * the caller would otherwise have to trust. */
typedef struct stunrun_gsp_zero_fill_result {
    /* Cycle census.  `writes` is the total number of 16-bit bus writes the
     * routine drives; the rest partition it. */
    uint32_t reads;
    uint32_t writes;
    uint32_t control_reads;     /* 1  */
    uint32_t control_writes;    /* 1  */
    uint32_t address_writes;    /* 2  */
    uint32_t data_writes;       /* 230400 */
    uint32_t watchdog_writes;   /* 900 */

    /* Every write classified through stunrun_gsp_zero_fill_classify().  These
     * three always sum to `writes`.  `live_writes` counts the cycles that
     * reach a decoded host-port register -- which is 2, both latches, NOT 0;
     * see the decode table above. */
    uint32_t live_writes;
    uint32_t discarded_writes;
    uint32_t outside_writes;
    /* The subset of `live_writes` that reached HSTDAT, i.e. that could have
     * altered GSP memory and therefore VRAM.  This is the no-op claim, and it
     * is the number that must be zero. */
    uint32_t live_data_writes;

    /* Register values as the trace sees them, whether or not the board keeps
     * them. */
    uint16_t control_before;
    uint16_t control_after;
    uint32_t start_address; /* $FF800000 */
    uint32_t data_address;  /* HSTADR the data writes nominally target */
    uint16_t data_value;    /* D0 at every data write */

    /* `jmp (A0)` at $54C0: the continuation the routine returns to. */
    uint32_t continuation;
} stunrun_gsp_zero_fill_result_t;

/* ROM $548E-$54C0, whole routine.
 *
 * `continuation` is the value the caller has in A0 and becomes the `jmp (A0)`
 * target; the routine passes it through unchanged and only reports it.
 *
 * Returns 1 if the routine ran, 0 if the arguments were unusable.  The result
 * struct is fully populated on success. */
int stunrun_gsp_zero_fill(uint32_t continuation,
                          stunrun_gsp_zero_fill_port_t *port,
                          stunrun_gsp_zero_fill_result_t *result);

#ifdef __cplusplus
}
#endif

#endif /* STUNRUN_GSP_ZERO_FILL_H */