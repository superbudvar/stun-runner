/* 68010 reset -> ADSP-handshake boot path. Transcription; see boot_chain.h for
 * the full provenance block, the confirmed/inferred split, and the $801e ROM
 * finding that decides the $1036 branch.
 *
 * NOTHING HERE IS INVENTED. Every constant below is an immediate from the
 * instruction stream at the address named in the comment. The two places where
 * a prior board value is not observable through board_iface.h are flagged
 * inline and are the only documented deviations.
 */
#include <stddef.h>
#include <string.h>

#include "boot_chain.h"

/* ------------------------------------------------------------------------ */
/* Internal running state                                                   */
/* ------------------------------------------------------------------------ */

/* The register-thread the chain threads through (A0/A2/A6) is not modelled as
 * registers: the continuation is control flow, and each `jmp (A0)` becomes a
 * call to the next stage in sequence. This struct is the machine's working
 * state for the duration of one run: which board, which seams, where the
 * result is being written, the page counter the loop has computed, and the
 * latch set when $13ec sees Service Mode released mid-wait. */
typedef struct stunrun_boot_ctx {
    stunrun_board_t *board;
    const stunrun_boot_seams_t *seams;
    stunrun_boot_result_t *r;
    uint16_t page;
    int halted;
} stunrun_boot_ctx_t;

/* ------------------------------------------------------------------------ */
/* GSP host-port transport                                                  */
/* ------------------------------------------------------------------------ */

/* One 16-bit write. `off` is the offset the ROM names, i.e. (68010 address -
 * $c00000), passed through unmodified. MAME's hd68k_gsp_io_w folds it with
 * `(offset / 2) ^ 1` (harddriv_m.cpp:218); this slice does not apply that
 * fold, because recording what the ROM addressed is the observable worth
 * keeping. board_iface.h does not say which convention it wants, so the test
 * records raw offsets and the parent can decide at integration. */
static void gsp_w(stunrun_boot_ctx_t *c, uint16_t off, uint16_t value)
{
    c->board->write_gsp_host(c->board->ctx, off, value);
    c->r->gsp_host_writes++;
}

/* `move.l` on a 16-bit bus: high halfword first, to consecutive offsets.
 * road_fifo.h records the measurement this follows (32524 writes, 0
 * exceptions out of 16262 pairs). */
static void gsp_wl(stunrun_boot_ctx_t *c, uint16_t off, uint32_t value)
{
    gsp_w(c, off, (uint16_t)(value >> 16));
    gsp_w(c, (uint16_t)(off + 2u), (uint16_t)(value & 0xFFFFu));
}

/* `ori.w #$1800,$c00008.l` ($53f2, $548e, $54c2, $566a).
 *
 * DOCUMENTED DEVIATION #1. ORI.W is read-modify-write, and HSTCTL's prior
 * contents are not readable through board_iface.h: read_gsp_control() is the
 * GSP control window ($f4xxxxxx in the GSP's own space), not the 68010 host
 * port. The unambiguous part -- the mask, the address, the fact that four
 * separate routines do the same thing at entry -- is transcribed; the
 * preserve-the-other-bits half is not, and this writes 0x1800. Do not read a
 * prior HSTCTL value out of a test that runs this slice.
 *
 * DOCUMENTED DEVIATION #2. `clr.w $60400c.l` / `$60401c.l` ($53f2) hit
 * map(0x604000,0x607fff).w() -- the NWR latch, write-only, with no
 * board_iface.h member. Recorded as a count in nwr_latch_writes and the
 * addresses published as STUNRUN_BOOT_NWR_LATCH_A/B; no value is invented,
 * because the ROM supplies none (clr.w).
 */
#define GSP_HSTCTL_OR_MASK 0x1800u

static void gsp_hstctl_or_1800(stunrun_boot_ctx_t *c)
{
    gsp_w(c, STUNRUN_BOOT_GSP_HSTCTL, GSP_HSTCTL_OR_MASK);
}

/* A big-endian long read out of ROM through the board's 16-bit reader, so no
 * ROM bytes are embedded in this file. */
static uint32_t rom_long(const stunrun_board_t *b, uint32_t addr)
{
    uint32_t hi = (uint32_t)b->read_rom16(b->ctx, addr);
    uint32_t lo = (uint32_t)b->read_rom16(b->ctx, addr + 2u);
    return (hi << 16) | lo;
}

/* ------------------------------------------------------------------------ */
/* Seams                                                                    */
/* ------------------------------------------------------------------------ */

static void seam_call(stunrun_boot_ctx_t *c, stunrun_boot_seam_id_t id,
                      void (*fn)(void *))
{
    if (c->r->seam_calls[id] < 0xFFFFFFFFu)
        c->r->seam_calls[id]++;
    if (fn != NULL)
        fn(c->seams ? c->seams->ctx : NULL);
}

static void seam_text(stunrun_boot_ctx_t *c, uint16_t index, uint32_t cont)
{
    unsigned n = c->r->text_draws;
    if (c->r->seam_calls[STUNRUN_BOOT_SEAM_TEXT_DRAW] < 0xFFFFFFFFu)
        c->r->seam_calls[STUNRUN_BOOT_SEAM_TEXT_DRAW]++;
    if (n < STUNRUN_BOOT_MAX_TEXT_DRAWS) {
        c->r->text_index[n] = index;
        c->r->text_cont[n] = cont;
    }
    c->r->text_draws++;
    if (c->seams != NULL && c->seams->text_draw != NULL)
        c->seams->text_draw(c->seams->ctx, index, cont);
}

static void seam_exit_page(stunrun_boot_ctx_t *c, uint16_t page,
                           uint32_t target)
{
    if (c->r->seam_calls[STUNRUN_BOOT_SEAM_EXIT_PAGE] < 0xFFFFFFFFu)
        c->r->seam_calls[STUNRUN_BOOT_SEAM_EXIT_PAGE]++;
    if (c->seams != NULL && c->seams->exit_page != NULL)
        c->seams->exit_page(c->seams->ctx, page, target);
}

/* ------------------------------------------------------------------------ */
/* $13ec-$1416 -- the two-phase /VSYNCB frame wait                          */
/* ------------------------------------------------------------------------ */

/*
 * Transcribed literally:
 *   $13ec: clr.w $608000.l                    watchdog kick
 *   $13f2: btst #$2,$60c001.l ; beq $13f2     wait while /VSYNCB is CLEAR
 *   $13fc: btst #$2,$60c001.l ; bne $13fc     wait while /VSYNCB is SET
 *   $1406: btst #$5,$60c001.l ; beq $1416
 *   $1410: jmp $260.l
 *   $1416: rts
 *
 * PORTED bit 2 of read_port0() -- /VSYNCB, PORT_BIT(0x04, IP_ACTIVE_LOW,
 * vblank) at harddriv.cpp:795. On a 68010 $60c001 is the low byte of the
 * 16-bit read, so bit 2 of the low byte is bit 2 of the value. The line is
 * active-low, so CLEAR means "inside vblank": the first loop waits for the
 * falling edge, the second for the rising edge, and the routine returns once
 * per frame just after vblank ends. That is the reading the wait pattern
 * itself establishes -- the two loops are polarity-inverted, which only makes
 * sense if bit 2 is a low-active line.
 *
 * The ROM bounds neither loop; they are bounded by the hardware line. The
 * bound here is a port artifact and is reported in out->capped, never
 * silently absorbed.
 */
void stunrun_boot_wait_vsync(stunrun_board_t *board,
                             uint32_t max_reads_per_phase,
                             stunrun_boot_vsync_t *out)
{
    stunrun_boot_vsync_t v;
    uint32_t n;

    v.reads = 0u;
    v.capped = 0u;
    v.halted = 0u;

    if (board == NULL) {
        if (out != NULL)
            *out = v;
        return;
    }
    if (max_reads_per_phase == 0u)
        max_reads_per_phase = STUNRUN_BOOT_VSYNC_MAX_READS;

    /* $13ec */
    board->write_watchdog(board->ctx);

    /* $13f2: beq back to $13f2 while bit 2 is clear. */
    for (n = 0u; ; n++) {
        uint16_t port = board->read_port0(board->ctx);
        v.reads++;
        if ((port & STUNRUN_PORT0_BIT_VSYNCB) != 0u)
            break;
        if (n + 1u >= max_reads_per_phase) {
            v.capped = 1u;
            break;
        }
    }

    /* $13fc: bne back to $13fc while bit 2 is set. */
    for (n = 0u; !v.capped; n++) {
        uint16_t port = board->read_port0(board->ctx);
        v.reads++;
        if ((port & STUNRUN_PORT0_BIT_VSYNCB) == 0u)
            break;
        if (n + 1u >= max_reads_per_phase) {
            v.capped = 1u;
            break;
        }
    }

    /* $1406: btst #$5 -- SET (Service Mode NOT pressed) jumps to $260. */
    if ((board->read_port0(board->ctx) & STUNRUN_PORT0_BIT_SELFTEST) != 0u)
        v.halted = 1u;
    v.reads++;

    if (out != NULL)
        *out = v;
}

/* The wait, with the port's counters folded into the run result. */
static void wait_vsync_in(stunrun_boot_ctx_t *c)
{
    stunrun_boot_vsync_t v;

    stunrun_boot_wait_vsync(c->board, STUNRUN_BOOT_VSYNC_MAX_READS, &v);
    c->r->watchdog_writes++;      /* $13ec's clr.w $608000.l */
    c->r->vsync_calls++;
    c->r->port0_reads += v.reads;
    if (v.halted != 0u) {
        /* $1410: jmp $260.l -- the wait abandons the chain. */
        c->r->handshake_halt++;
        c->halted = 1u;
    }
}

/* ------------------------------------------------------------------------ */
/* $53f2 -- GSP host-port init and the $02b8 register table                 */
/* ------------------------------------------------------------------------ */

/*
 *   $53f2: clr.w $60400c.l                     NWR latch (unexposed, counted)
 *   $53f8: move.l #$96,D0 / $53fe: subq.w #1,D0 / $5400: bne $53fe
 *   $5402: clr.w $60401c.l                     NWR latch (unexposed, counted)
 *   $5408: move.w #$b800,$c00008.l
 *   $5410: lea $2b8.l,A1 / $5416: move.w #$7,D0
 *   $541a: move.l (A1),$c00002.l   [8 entries of 6 bytes]
 *   $5420: move.w (4,A1),$c0000c.l
 *   $5428: adda.l #$6,A1
 *   $542e: dbra D0,$541a
 *   $5432-$5484: five fixed (address, colour) pairs
 *   $548c: jmp (A0)
 *
 * The delay: move.l #$96,D0 sets D0.l = 0x00000096 and dbra tests the LOW
 * word, so it is 150 iterations, not 0x96 * something. It issues no board
 * access at all, which is exactly why it is counted rather than observed --
 * and why a test should see gsp_host_writes unchanged across it.
 */
static void stage_gsp_init(stunrun_boot_ctx_t *c)
{
    unsigned i;

    c->r->nwr_latch_writes++;

    for (i = 0u; i < STUNRUN_BOOT_DELAY_ITERATIONS; i++)
        c->r->delay_iterations++;

    c->r->nwr_latch_writes++;

    /* $5408 */
    gsp_w(c, STUNRUN_BOOT_GSP_HSTCTL, 0xB800u);

    /* $5410-$542e: the 8-entry table, read from ROM through the board. */
    for (i = 0u; i < STUNRUN_BOOT_TABLE_ENTRIES; i++) {
        uint32_t entry = STUNRUN_BOOT_TABLE_ROM +
                         (uint32_t)i * STUNRUN_BOOT_TABLE_ENTRY_SIZE;
        gsp_wl(c, STUNRUN_BOOT_GSP_HSTADR,
               rom_long(c->board, entry));
        gsp_w(c, STUNRUN_BOOT_GSP_HSTDAT,
              c->board->read_rom16(c->board->ctx, entry + 4u));
    }

    /* $5432-$5484: five fixed (address, colour) pairs, transcribed in order. */
    gsp_wl(c, STUNRUN_BOOT_GSP_HSTADR, 0xC0000090u);
    gsp_w(c, STUNRUN_BOOT_GSP_HSTDAT, 0xFFFFu);
    gsp_wl(c, STUNRUN_BOOT_GSP_HSTADR, 0xC00000B0u);
    gsp_w(c, STUNRUN_BOOT_GSP_HSTDAT, 0x0008u);
    gsp_wl(c, STUNRUN_BOOT_GSP_HSTADR, 0xC0000080u);
    gsp_w(c, STUNRUN_BOOT_GSP_HSTDAT, 0xF010u);
    gsp_wl(c, STUNRUN_BOOT_GSP_HSTADR, 0xF4800010u);
    gsp_w(c, STUNRUN_BOOT_GSP_HSTDAT, 0x0000u);
    gsp_wl(c, STUNRUN_BOOT_GSP_HSTADR, 0xF4800090u);
    gsp_w(c, STUNRUN_BOOT_GSP_HSTDAT, 0xFFFFu);
}

/* ------------------------------------------------------------------------ */
/* $548e -- whole-screen fill                                               */
/* ------------------------------------------------------------------------ */

/*
 *   $548e: ori.w #$1800,$c00008.l
 *   $5496: move.l #$ff800000,$c00002.l
 *   $54a0: move.w #$0,D0
 *   $54a4: move.w #$383,D2
 *   $54a8: move.w #$ff,D1
 *   $54ac: move.w D0,$c0000c.l    } inner
 *   $54b2: dbra D1,$54ac           } dbra $ff = 256
 *   $54b6: clr.w $608000.l
 *   $54bc: dbra D2,$54a8           } dbra $383 = 900
 *   $54c0: jmp (A0)
 *
 * The watchdog kick sits between the inner loops, so it runs 900 times, not
 * once -- and it is the only thing keeping the board alive across 230400 host
 * writes. That ordering is asserted, because losing it is the kind of bug a
 * collapsed "just fill it" rewrite would introduce silently.
 */
static void stage_clear_all(stunrun_boot_ctx_t *c)
{
    unsigned outer;

    gsp_hstctl_or_1800(c);
    gsp_wl(c, STUNRUN_BOOT_GSP_HSTADR, 0xFF800000u);

    for (outer = 0u; outer < STUNRUN_BOOT_CLEAR_ALL_OUTER; outer++) {
        unsigned inner;
        for (inner = 0u; inner < STUNRUN_BOOT_CLEAR_ALL_INNER; inner++)
            gsp_w(c, STUNRUN_BOOT_GSP_HSTDAT, 0u);
        /* $54b6 -- the ONLY watchdog kick inside $53f2-$552a other than $5524;
         * verified by scanning the ROM for 42 79 00 60 80 00 over that range,
         * which finds exactly these two. */
        stunrun_b_watchdog(c->board);
        c->r->watchdog_writes++;
    }
}

/* ------------------------------------------------------------------------ */
/* $54c2 -- the move.w D0,$c0000c.l double-dbra fill                        */
/* ------------------------------------------------------------------------ */

/*
 *   $54c2: ori.w #$1800,$c00008.l
 *   $54ca: move.l #$f4800020,$c00002.l ; $54d4: clr.w $c0000c.l
 *   $54da: move.l #$f4800030,$c00002.l ; $54e4: clr.w $c0000c.l
 *   $54ea: clr.w D0
 *   $54ec: move.l #$f5000000,$c00002.l
 *   $54f6: move.w #$ff,D1
 *   $54fa: move.w D0,$c0000c.l ; $5500: addi.w #$101,D0 ; dbra D1,$54fa
 *   $5508: clr.w D0
 *   $550a: move.l #$f5800000,$c00002.l
 *   $5514: move.w #$ff,D1
 *   $5518: move.w D0,$c0000c.l ; $551e: addq.w #1,D0 ; dbra D1,$5518
 *   $5524: clr.w $608000.l
 *   $552a: jmp (A0)
 *
 * WRITE-BEFORE-INCREMENT in both ramps. The increment happens after the store
 * and after dbra has not yet run, so each ramp writes 256 values and the
 * final increment is discarded: ramp one writes 0x0000, 0x0101, ... 0xFFFF
 * (k*0x101) and ramp two writes 0, 1, ... 255. Transcribing this as
 * "increment then write" would shift both ramps by one and quietly change the
 * two palette ramps the boot screen is built from, so the order is explicit.
 */
static void stage_clear_ramp(stunrun_boot_ctx_t *c)
{
    uint16_t d0;
    unsigned i;

    gsp_hstctl_or_1800(c);

    gsp_wl(c, STUNRUN_BOOT_GSP_HSTADR, 0xF4800020u);
    gsp_w(c, STUNRUN_BOOT_GSP_HSTDAT, 0u);
    gsp_wl(c, STUNRUN_BOOT_GSP_HSTADR, 0xF4800030u);
    gsp_w(c, STUNRUN_BOOT_GSP_HSTDAT, 0u);

    /* $54ea-$5504: clr.w D0, then store/increment by $0101. */
    d0 = 0u;
    gsp_wl(c, STUNRUN_BOOT_GSP_HSTADR, 0xF5000000u);
    for (i = 0u; i < STUNRUN_BOOT_CLEAR_RAMP_ENTRIES; i++) {
        gsp_w(c, STUNRUN_BOOT_GSP_HSTDAT, d0);
        d0 = (uint16_t)(d0 + 0x0101u);   /* addi.w #$101,D0 */
    }

    /* $5508-$5520: clr.w D0 again, then store/increment by 1. */
    d0 = 0u;
    gsp_wl(c, STUNRUN_BOOT_GSP_HSTADR, 0xF5800000u);
    for (i = 0u; i < STUNRUN_BOOT_CLEAR_RAMP_ENTRIES; i++) {
        gsp_w(c, STUNRUN_BOOT_GSP_HSTDAT, d0);
        d0 = (uint16_t)(d0 + 1u);       /* addq.w #1,D0 */
    }

    /* $5524 */
    stunrun_b_watchdog(c->board);
    c->r->watchdog_writes++;
}

/* ------------------------------------------------------------------------ */
/* $566a -- fill the page a string index names (also $50a0's loop-back arm)  */
/* ------------------------------------------------------------------------ */

/*
 *   $566a: ori.w #$1800,$c00008.l
 *   $5672: clr.l D1
 *   $5674: move.w D0,D1
 *   $5676: lsl.l #8,D1
 *   $5678: lsl.l #4,D1
 *   $567a: addi.l #-$800000,D1          = addi.l #$ff800000,D1
 *   $5680: move.w #$ff,D2
 *   $5684: move.l D1,$c00002.l
 *   $568a: move.w #$ffff,D3
 *   $568e: move.w D3,$c0000c.l ; dbra D2,$568e
 *   $5698: jmp (A0)
 *
 * The clr.l before move.w matters: without it the upper 16 bits of D1 would be
 * whatever D0 held. Transcribed as `d1 = (uint32_t)index` then the two shifts,
 * which is the same 32-bit arithmetic the 68010 performs.
 */
void stunrun_boot_gsp_fill_page(stunrun_board_t *board, uint16_t index)
{
    uint32_t d1;
    unsigned i;

    if (board == NULL)
        return;

    board->write_gsp_host(board->ctx, STUNRUN_BOOT_GSP_HSTCTL,
                          GSP_HSTCTL_OR_MASK);

    d1 = (uint32_t)index;                 /* clr.l D1 ; move.w D0,D1 */
    d1 = d1 << 8;                         /* lsl.l #8 */
    d1 = d1 << 4;                         /* lsl.l #4 */
    d1 = d1 + 0xFF800000u;                /* addi.l #-$800000 */

    board->write_gsp_host(board->ctx, STUNRUN_BOOT_GSP_HSTADR,
                          (uint16_t)(d1 >> 16));
    board->write_gsp_host(board->ctx,
                          (uint16_t)(STUNRUN_BOOT_GSP_HSTADR + 2u),
                          (uint16_t)(d1 & 0xFFFFu));

    for (i = 0u; i < STUNRUN_BOOT_FILL_PAGE_ENTRIES; i++)
        board->write_gsp_host(board->ctx, STUNRUN_BOOT_GSP_HSTDAT, 0xFFFFu);
}

/* ------------------------------------------------------------------------ */
/* $6086-$60ac -- the boot gate                                             */
/* ------------------------------------------------------------------------ */

/*
 *   $6086: clr.w D2 / $6088: clr.w D3 / $608a: clr.w D5
 *   $608c: btst #$5,$60c001.l   ; SELF-TEST
 *   $6094: beq $609c
 *   $6096: jmp $260.l
 *   $609c: btst #$2,$a80001.l   ; IPT_START1
 *   $60a4: bne $60ac
 *   $60a6: jmp $0f94.l
 *   $60ac: lea $5ff6.l,A1 ...
 *
 * POLARITY, stated because it is inverted from the naive reading and the test
 * depends on it: $60c001 is the LOW byte of the 16-bit port read, so btst #5
 * tests PORT_SERVICE(0x20, IP_ACTIVE_LOW) (harddriv.cpp:798). CLEAR therefore
 * means Service Mode is PRESSED, and `beq` on clear CONTINUES to the START1
 * test. SET (not pressed) is the branch to $260.
 *
 * `btst #$2,$a80001.l` is a plain read of bit 2 -- no active-low inversion is
 * applied by the input table -- so SET falls through to the $60ac self-test
 * table and CLEAR takes `jmp $0f94.l`, the ADSP handshake.
 *
 * The gate NEVER returns through A0. It leaves by jmp $260, jmp $0f94, or a
 * fall-through into $60ac, so there is no continuation to record here.
 */
static void stage_gate(stunrun_boot_ctx_t *c)
{
    /* $608c reads $60c001 and $609c reads $a80001: TWO different ports, one
     * read each. Only the first is a read_port0. */
    uint16_t port0 = c->board->read_port0(c->board->ctx);
    uint16_t a800;

    c->r->port0_reads++;

    c->r->selftest_bit =
        (uint8_t)((port0 & STUNRUN_PORT0_BIT_SELFTEST) != 0u ? 1u : 0u);

    if ((port0 & STUNRUN_PORT0_BIT_SELFTEST) != 0u) {
        /* $6096: jmp $260.l */
        c->r->gate = STUNRUN_BOOT_GATE_HALT;
        return;
    }

    a800 = c->board->read_a80000(c->board->ctx);
    c->r->a800_reads++;
    c->r->start1_bit =
        (uint8_t)((a800 & STUNRUN_BOOT_A80000_BIT_START1) != 0u ? 1u : 0u);

    if ((a800 & STUNRUN_BOOT_A80000_BIT_START1) != 0u)
        c->r->gate = STUNRUN_BOOT_GATE_SELFTEST_TABLE;   /* $60ac */
    else
        c->r->gate = STUNRUN_BOOT_GATE_HANDSHAKE;        /* $60a6 */
}

/* ------------------------------------------------------------------------ */
/* $0f94-$102e -- the ADSP handshake init                                    */
/* ------------------------------------------------------------------------ */

/*
 *   $0f94: move.w #$0,D0
 *   $0f98: jsr $67a.l                    SEAM
 *   $0f9e: bra $fb2
 *   $0fb2: movea.l #$0,A0 ; movec A0,VBR
 *   $0fbc: movea.l #$ffffc000,A7         SP is set again, to the same value
 *   $0fc2: move.w $604000.l,D0           unmapped read (counted)
 *   $0fc8: move.b $600000.l,D0           unmapped read (counted)
 *   $0fce: lea ($6,PC),A2 ; A2 = $fd6
 *   $0fd2: bra $132a
 *   $132a: clr.w $608000.l ; lea $ffff8000.l,A0 ; move.w #$1fff,D1
 *          clr.l (A0)+ ; dbra D1 ; clr.w $608000.l ; jmp (A2)
 *   $0fd6: jsr $3f8.l                    SEAM
 *   $0fdc: jsr $570.l                    SEAM
 *   $0fe2: clr.w $608000.l
 *   $0fe8: move.w #$0,D0
 *   $0fec: jsr $636.l                    SEAM
 *   $0ff2: clr.w $608000.l
 *   $0ff8: clr.w $801e.w
 *   $0ffc: clr.w $8036.w
 *   $1000: clr.w $8046.w
 *   $1004: clr.w $8056.w
 *   $1008: clr.w $8066.w
 *   $100c: clr.w $8076.w
 *   $1010: clr.w $8086.w
 *   $1014: lea $135e.l,A0 ; movea.l (6,A0),A0
 *   $101e: move.w #$f,(2,A0)
 *   $1024: move.w #$ffff,(4,A0)
 *   $102a: move #$2400,SR
 *   $102e: bra $1092
 *
 * The seven clears are `clr.w $80xx.w` -- absolute-SHORT, so 68010 address
 * $0000801e..$00008086, which is inside map(0x000000,0x0fffff).rom()
 * (harddriv.cpp:602) and NOT the ADSP window at $00800000
 * (init_adsp, harddriv.cpp:4822). They are ROM. MAME installs no write
 * handler there, so each is discarded. They are modelled as write_rom16 and
 * read back through read_rom16, deliberately NOT as RAM, so that no test can
 * accidentally pass because a write stuck.
 *
 * $1014-$1024 writes MAIN RAM through a pointer read out of the ROM table at
 * $135e+6; board_iface.h exposes no main-RAM writer, so that is the
 * adsp_irq_vector_init seam rather than an invented board member.
 */
static void handshake_init(stunrun_boot_ctx_t *c)
{
    static const uint32_t flags[STUNRUN_BOOT_FLAG_COUNT] = {
        STUNRUN_BOOT_FLAG_PAGE,
        STUNRUN_BOOT_FLAG_ADVANCE,
        STUNRUN_BOOT_FLAG_BACK,
        STUNRUN_BOOT_FLAG_EXIT,
        STUNRUN_BOOT_FLAG_SPARE_1,
        STUNRUN_BOOT_FLAG_SPARE_2,
        STUNRUN_BOOT_FLAG_SPARE_3
    };
    unsigned i;

    seam_call(c, STUNRUN_BOOT_SEAM_PRE_67A,
              c->seams ? c->seams->pre_67a : NULL);

    /* $0fb2/$0fbc: VBR = 0 and SP = $ffffc000 again. Registers only; the SP
     * value is identical to $506c's, which is why result->reset_sp stands for
     * both and no second field is invented. */

    /* $0fc2 / $0fc8: read from unmapped space. Counted, not decoded. */
    c->r->probe_reads += STUNRUN_BOOT_PROBE_READS;

    /* $132a, reached by the A2 continuation. */
    stunrun_b_watchdog(c->board);
    c->r->watchdog_writes++;
    seam_call(c, STUNRUN_BOOT_SEAM_ADSP_DM_CLEAR,
              c->seams ? c->seams->adsp_dm_clear : NULL);
    stunrun_b_watchdog(c->board);
    c->r->watchdog_writes++;

    /* $0fd6-$0fec */
    seam_call(c, STUNRUN_BOOT_SEAM_PRE_3F8,
              c->seams ? c->seams->pre_3f8 : NULL);
    seam_call(c, STUNRUN_BOOT_SEAM_PRE_570,
              c->seams ? c->seams->pre_570 : NULL);
    stunrun_b_watchdog(c->board);
    c->r->watchdog_writes++;
    seam_call(c, STUNRUN_BOOT_SEAM_PRE_636,
              c->seams ? c->seams->pre_636 : NULL);
    stunrun_b_watchdog(c->board);
    c->r->watchdog_writes++;

    /* $0ff8-$1010: the seven flag clears, once each, in this order. */
    for (i = 0u; i < STUNRUN_BOOT_FLAG_COUNT; i++) {
        c->board->write_rom16(c->board->ctx, flags[i], 0u);
        if (c->r->flag_clears < STUNRUN_BOOT_FLAG_COUNT)
            c->r->flag_clear_addr[c->r->flag_clears] = flags[i];
        c->r->flag_clears++;
    }

    /* $1014-$1024: main-RAM writes behind a ROM-read pointer. */
    seam_call(c, STUNRUN_BOOT_SEAM_IRQ_VECTOR,
              c->seams ? c->seams->adsp_irq_vector_init : NULL);

    /* $102a: move #$2400,SR -- supervisor, interrupts off below level 7.
     * Register-only, no board observable. */
}

/* ------------------------------------------------------------------------ */
/* $1092-$10ba -- the wait loop                                              */
/* ------------------------------------------------------------------------ */

/*
 * $1092: move.w #$10,D1 ; $1096: move.w #$6,D2 ; bsr $145e   (SEAM)
 * $109e: bsr $10bc                                        (SEAM)
 * $10a2: bsr $13ec
 * $10a6: bra $1032
 * $1032: bsr $1368                                        (SEAM)
 * $1036: tst.w $8036.w ; $103a: beq $105a
 * $103e: clr.w $8036.w
 * $1042: move.w $801e.w,D0
 * $1046: addq.w #1,D0
 * $1048: cmpi.w #$4,D0 ; $104c: bne $1052
 * $1050: clr.w D0
 * $1052: move.w D0,$801e.w
 * $1056: bra $1092
 * $105a: tst.w $8046.w ; $105e: beq $107c
 * $1062: clr.w $8046.w
 * $1066: move.w $801e.w,D0
 * $106a: subq.w #1,D0
 * $106c: bpl $1074
 * $1070: move.w #$3,D0
 * $1074: move.w D0,$801e.w
 * $1078: bra $1092
 * $107c: tst.w $8056.w ; $1080: beq $108c
 * $1084: clr.w $8056.w
 * $1088: bra $10a8
 * $108c: bsr $13ec
 * $1090: bra $1032
 * $10a8: lea $fa2.l,A0
 * $10ae: move.w $801e.w,D0
 * $10b2: lsl.w #2,D0
 * $10b4: movea.l (A0,D0.w),A0
 * $10b8: jsr (A0)                        (SEAM)
 * $10ba: bra $1092
 *
 * The flag priority is the code order, not a table: $8036 is tested first and
 * wins over both others; $8046 only if $8036 is clear; $8056 only if both are.
 *
 * `bpl $1074` tests the SIGN of the 16-bit subq.w result, so $801e = 0 gives
 * 0xffff (negative -> reload $0003) while $801e = $8000 gives $7fff (positive,
 * stored as-is). Modelled as a sign test on the wrapped word, not as "if zero"
 * and not as a saturating decrement -- both would differ on $8000.
 *
 * $10b2's lsl.w #2 is on the LOW word of D0 only, so the table index is
 * (page * 4) and a page above $3fff wraps the word to 0.
 */
static void handshake_block_1092(stunrun_boot_ctx_t *c)
{
    seam_call(c, STUNRUN_BOOT_SEAM_LED_BAR,
              c->seams ? c->seams->led_bar : NULL);
    seam_call(c, STUNRUN_BOOT_SEAM_PAGE_DRAW,
              c->seams ? c->seams->page_draw : NULL);
    wait_vsync_in(c);
}

static void handshake_loop(stunrun_boot_ctx_t *c, uint32_t max_iterations)
{
    stunrun_board_t *b = c->board;
    stunrun_boot_result_t *r = c->r;

    /* $102e: bra $1092 -- the $1092 block runs once BEFORE the first pass of
     * $1032, so the block count equals the iteration count and max == 0 still
     * performs it. */
    handshake_block_1092(c);
    if (c->halted)
        return;

    for (;;) {
        uint16_t w;

        if (r->handshake_iterations >= max_iterations) {
            r->handshake_cap_hit = 1u;
            return;
        }

        /* $1032: bsr $1368 */
        seam_call(c, STUNRUN_BOOT_SEAM_INPUT_SCAN,
                  c->seams ? c->seams->input_scan : NULL);
        r->handshake_iterations++;

        /* $1036: tst.w $8036.w */
        w = b->read_rom16(b->ctx, STUNRUN_BOOT_FLAG_ADVANCE);
        if (w != 0u) {
            /* $103e-$1056: advance the page counter, mod 4. */
            uint16_t d0;
            r->handshake_advance++;
            b->write_rom16(b->ctx, STUNRUN_BOOT_FLAG_ADVANCE, 0u);
            d0 = b->read_rom16(b->ctx, STUNRUN_BOOT_FLAG_PAGE);
            d0 = (uint16_t)(d0 + 1u);
            if (d0 == STUNRUN_BOOT_PAGE_MODULUS)
                d0 = 0u;
            b->write_rom16(b->ctx, STUNRUN_BOOT_FLAG_PAGE, d0);
            c->page = d0;
            handshake_block_1092(c);
            if (c->halted)
                return;
            continue;
        }

        /* $105a: tst.w $8046.w */
        w = b->read_rom16(b->ctx, STUNRUN_BOOT_FLAG_BACK);
        if (w != 0u) {
            uint16_t d0;
            r->handshake_back++;
            b->write_rom16(b->ctx, STUNRUN_BOOT_FLAG_BACK, 0u);
            d0 = b->read_rom16(b->ctx, STUNRUN_BOOT_FLAG_PAGE);
            d0 = (uint16_t)(d0 - 1u);          /* subq.w #1 */
            if ((d0 & 0x8000u) != 0u)          /* bpl -> negative */
                d0 = STUNRUN_BOOT_PAGE_WRAP;
            b->write_rom16(b->ctx, STUNRUN_BOOT_FLAG_PAGE, d0);
            c->page = d0;
            handshake_block_1092(c);
            if (c->halted)
                return;
            continue;
        }

        /* $107c: tst.w $8056.w */
        w = b->read_rom16(b->ctx, STUNRUN_BOOT_FLAG_EXIT);
        if (w != 0u) {
            uint16_t d0;
            uint32_t target;
            r->handshake_exit++;
            b->write_rom16(b->ctx, STUNRUN_BOOT_FLAG_EXIT, 0u);
            /* $10a8: dispatch through the $fa2 table. */
            d0 = b->read_rom16(b->ctx, STUNRUN_BOOT_FLAG_PAGE);
            target = rom_long(b, STUNRUN_BOOT_DISPATCH_TABLE_ROM +
                                   (uint32_t)((uint16_t)(d0 << 2)));
            r->handshake_dispatch++;
            seam_exit_page(c, d0, target);
            handshake_block_1092(c);
            if (c->halted)
                return;
            continue;
        }

        /* $108c: bsr $13ec ; $1090: bra $1032 */
        wait_vsync_in(c);
        if (c->halted)
            return;
    }
}

/* ------------------------------------------------------------------------ */
/* the chain                                                                */
/* ------------------------------------------------------------------------ */

static void stage_push(stunrun_boot_result_t *r, uint32_t addr)
{
    if (r->stage_count < STUNRUN_BOOT_MAX_STAGES)
        r->stages[r->stage_count] = addr;
    r->stage_count++;
}

stunrun_boot_result_t stunrun_boot_chain_run(
    stunrun_board_t *board, const stunrun_boot_seams_t *seams,
    uint32_t max_handshake_iterations)
{
    stunrun_boot_result_t r;
    stunrun_boot_ctx_t c;

    memset(&r, 0, sizeof r);
    memset(&c, 0, sizeof c);
    c.board = board;
    c.seams = seams;
    c.r = &r;
    c.page = 0u;
    c.halted = 0u;

    if (board == NULL)
        return r;

    /* $506c: movea.l #$ffffc000,A7. Both $506c and $0fbc load this value; the
     * field records it once and the second site is called out in handshake_init
     * rather than given a field of its own. */
    r.reset_sp = STUNRUN_BOOT_RESET_SP;
    stage_push(&r, STUNRUN_BOOT_ENTRY);

    /* $5072: lea ($6,PC),A0 -> $507a ; $5076: bra $53f2 */
    stage_push(&r, STUNRUN_BOOT_STAGE_GSP_INIT);
    stage_gsp_init(&c);

    /* $507a: lea ($6,PC),A0 -> $5082 ; $507e: bra $548e */
    stage_push(&r, STUNRUN_BOOT_STAGE_CLEAR_ALL);
    stage_clear_all(&c);

    /* $5082: lea ($6,PC),A0 -> $508a ; $5086: bra $54c2 */
    stage_push(&r, STUNRUN_BOOT_STAGE_CLEAR_RAMP);
    stage_clear_ramp(&c);

    /* $508a: move.w #$47,D0 ; $508e: lea ($8,PC),A2 -> $5098 ;
     * $5092: jmp $25e8.l -- no `lea ($6,PC),A0` first, because the
     * continuation goes in A2 and the drawer is reached with jmp. */
    stage_push(&r, STUNRUN_BOOT_STAGE_DRAW_BANNER);
    seam_text(&c, STUNRUN_BOOT_TEXT_BANNER, STUNRUN_BOOT_CONT_BANNER);

    /* $5098: lea ($6,PC),A6 -> $50a0 ; $509c: bra $6086.
     * A6 is the loop-back continuation for $56e6, not for anything on this
     * path, so it is loaded and discarded. $50a0 is NOT visited here. */
    stage_push(&r, STUNRUN_BOOT_STAGE_GATE);
    stage_gate(&c);

    if (r.gate != STUNRUN_BOOT_GATE_HANDSHAKE)
        return r;

    /* $60a6: jmp $0f94.l */
    stage_push(&r, STUNRUN_BOOT_STAGE_HANDSHAKE);
    handshake_init(&c);

    /* $102e: bra $1092 */
    handshake_loop(&c, max_handshake_iterations);

    if (c.halted)
        r.gate = STUNRUN_BOOT_GATE_HALT;

    r.page = c.page;
    return r;
}