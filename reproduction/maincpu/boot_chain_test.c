/* ROM-free self-check for the 68010 reset -> ADSP-handshake boot path.
 *
 * EXPECTATIONS ARE TRANSCRIBED FROM THE DISASSEMBLY, not from this port's own
 * output. Every number below is an immediate, a dbra count, or a branch target
 * that appears in the instruction stream at the address named beside it, or a
 * ROM word read out of the shipped 1 MiB main-CPU image with the command
 * printed in boot_chain.h. Where a number is a choice rather than a
 * transcription, the comment says so and gives the reason.
 *
 * The stub RECORDS rather than merely counting, because the ordered sequence
 * of board accesses IS the observable: the repo rule is that a read
 * disassembly is not a deliverable until its observable output is asserted.
 *
 * Three logging limits, all forced by the code being tested:
 *   - $548e issues 230400 host writes, so a full stream log is impossible. A
 *     bounded first-N log covers the ordering claims (they are all at the head
 *     of the run), and a per-offset TALLY covers the counts.
 *   - $54c2's ramps sit behind those 230400 writes, so a bounded WINDOW keyed on
 *     the host-write ordinal is used to reach them. The ordinals are derived
 *     from the dbra counts, not guessed.
 */
#include <stdio.h>
#include <string.h>

#include "boot_chain.h"

static int g_failures = 0;

#define check(cond) \
    do { \
        if (cond) { \
            printf("PASS %s\n", #cond); \
        } else { \
            printf("FAIL %s\n", #cond); \
            g_failures++; \
        } \
    } while (0)

/* ------------------------------------------------------------------------ */
/* A synthetic ROM, so the port never needs the commercial image             */
/* ------------------------------------------------------------------------ */

/* The $02b8 register table as the shipped image holds it (read back with the
 * same command shape used on /tmp/maincpu_flat1m.bin). Transcribed so $53f2's
 * table loop has something to read; the mechanics are what is under test, not
 * the values, but using the real ones means the test cannot drift from the
 * image. */
static const uint16_t kTableRom[8 * 3] = {
    /* $02b8 */ 0xC000u, 0x0070u, 0x0105u,
    /* $02be */ 0xC000u, 0x0060u, 0x00F8u,
    /* $02c4 */ 0xC000u, 0x0050u, 0x0013u,
    /* $02ca */ 0xC000u, 0x0040u, 0x0004u,
    /* $02d0 */ 0xC000u, 0x0030u, 0x013Cu,
    /* $02d6 */ 0xC000u, 0x0020u, 0x0137u,
    /* $02dc */ 0xC000u, 0x0010u, 0x0037u,
    /* $02e2 */ 0xC000u, 0x0000u, 0x0013u
};

/* The $801e family as the shipped image holds it: ASCII from the self-test
 * message blob at $8000. These are why $1036's `beq $105a` can never be
 * taken, and why the loop collapses to one arm. */
#define ROM_801E 0x4552u
#define ROM_8036 0x4154u
#define ROM_8046 0x4255u
#define ROM_8056 0x3638u

/* Host-write ordinal at which $54c2's first $c0000c write lands. Counts EVERY
 * write_gsp_host before it, not just the HSTDAT ones, because the window is
 * keyed on the raw write ordinal:
 *   $53f2  1 ($b800 to HSTCTL) + 8 table entries x 3 (a `move.l` splits into
 *          two writes, plus the colour word) + 5 fixed pairs x 3   ->  40
 *   $548e  1 (ori to HSTCTL) + 1 long x 2 + 256 * 900              -> 230403
 */
#define HOST_WRITES_AT_RAMP                                            \
    (1u + STUNRUN_BOOT_TABLE_ENTRIES * 3u + 5u * 3u + 1u + 2u +        \
     STUNRUN_BOOT_CLEAR_ALL_INNER * STUNRUN_BOOT_CLEAR_ALL_OUTER)

/* Offset of ramp one's first store inside that window. The $54c2 prologue
 * between its entry and ramp one is, in order:
 *   ori HSTCTL        1 write
 *   move.l $f4800020  2 writes ($54ca)
 *   clr.w  $c0000c    1 write  ($54d4)
 *   move.l $f4800030  2 writes ($54da)
 *   clr.w  $c0000c    1 write  ($54e4)
 *   move.l $f5000000  2 writes ($54ec)
 * so ramp one starts 9 writes in. */
#define RAMP1_START (HOST_WRITES_AT_RAMP + 9u)

/* ------------------------------------------------------------------------ */
/* Recording stub                                                            */
/* ------------------------------------------------------------------------ */

#define MAX_EVENTS 64u
#define WIN_MAX 16u

enum {
    EV_ROM_W = 0,
    EV_GSP_W,
    EV_WATCHDOG,
    EV_TEXT
};

typedef struct event {
    unsigned kind;
    uint32_t a;
    uint16_t b;
} event_t;

typedef struct stub {
    /* port0. The first `gate_reads` reads return `port0_gate` (the two btst at
     * $608c/$609c, and the $1406 read inside $13ec); every later read returns
     * a two-cycle vblank script, so one $13ec call costs two reads and the
     * minimum-wait path is distinguishable from a spinning one. */
    uint16_t port0_gate;
    unsigned gate_reads;
    unsigned gate_seen;
    unsigned vsync_seen;
    uint16_t vsync_high;      /* value returned on the odd (line-high) read */
    int vsync_never_high;     /* hold the line low: force the phase-1 cap */

    uint16_t a800;

    /* ROM words at the $801e family. */
    uint16_t rom_801e, rom_8036, rom_8046, rom_8056;

    unsigned rom_writes;
    unsigned gsp_writes;
    unsigned watchdog;
    unsigned port0_reads;
    unsigned a800_reads;
    unsigned rom16_reads;

    unsigned ev_count;
    event_t ev[MAX_EVENTS];

    /* A ROM-write-only ordered log. The mixed stream is dominated by the
     * 230400 host writes and 901 watchdog kicks, so "the seven clears happen
     * once, in order" cannot be asked of it. */
    unsigned rom_ev_n;
    event_t rom_ev[16];

    unsigned host_by_offset[16];

    /* Ordinal windowed capture of (offset, value), for reaches that the head
     * log cannot. */
    unsigned win_from;
    unsigned win_count;
    unsigned win_n;
    uint16_t win_off[WIN_MAX];
    uint16_t win_val[WIN_MAX];
} stub_t;

static void ev_push(stub_t *s, unsigned kind, uint32_t a, uint16_t b)
{
    if (s->ev_count < MAX_EVENTS) {
        s->ev[s->ev_count].kind = kind;
        s->ev[s->ev_count].a = a;
        s->ev[s->ev_count].b = b;
    }
    s->ev_count++;
}

/* ROM-write-only equivalent of ev_is. */
static int rom_ev_is(const stub_t *s, unsigned i, uint32_t a, uint16_t b)
{
    if (i >= 16u || i >= s->rom_ev_n)
        return 0;
    if (s->rom_ev[i].kind != EV_ROM_W || s->rom_ev[i].a != a)
        return 0;
    if (s->rom_ev[i].b != b)
        return 0;
    return 1;
}

/* Bounded by MAX_EVENTS as well as ev_count: ev_count keeps counting past the
 * log, so indexing it alone would read off the end. */
static int ev_is(const stub_t *s, unsigned i, unsigned kind, uint32_t a,
                 uint16_t b)
{
    if (i >= MAX_EVENTS || i >= s->ev_count)
        return 0;
    if (s->ev[i].kind != kind || s->ev[i].a != a || s->ev[i].b != b)
        return 0;
    return 1;
}

/* $13ec's minimum path is 2 phase reads plus the $1406 read: read A must have
 * bit 2 SET to break `beq $13f2`, and read B must have bit 2 CLEAR to break
 * `bne $13fc`. The script therefore starts HIGH. Polarity is transcribed from
 * the ROM's two inverted tests and from PORT_BIT(0x04, IP_ACTIVE_LOW, vblank):
 * clear is "inside vblank", so phase 1 waits for the rising edge and phase 2
 * for the falling one. */
static uint16_t stub_port0_read(stub_t *s)
{
    uint16_t v;

    if (s->gate_seen < s->gate_reads) {
        v = s->port0_gate;
        s->gate_seen++;
    } else if (s->vsync_never_high) {
        v = 0u;                      /* /VSYNCB stays low: phase 1 never ends */
    } else if ((s->vsync_seen % 2u) == 0u) {
        v = s->vsync_high;           /* /VSYNCB high: ends `beq $13f2` */
    } else {
        v = 0u;                      /* /VSYNCB low: ends `bne $13fc` */
    }
    s->vsync_seen++;
    s->port0_reads++;
    return v;
}

static uint8_t stub_rom8(void *ctx, uint32_t addr)
{
    (void)ctx; (void)addr;
    return 0u;
}

static uint16_t stub_rom16(void *ctx, uint32_t addr)
{
    stub_t *s = (stub_t *)ctx;

    s->rom16_reads++;
    if (addr == STUNRUN_BOOT_FLAG_PAGE)    return s->rom_801e;
    if (addr == STUNRUN_BOOT_FLAG_ADVANCE) return s->rom_8036;
    if (addr == STUNRUN_BOOT_FLAG_BACK)    return s->rom_8046;
    if (addr == STUNRUN_BOOT_FLAG_EXIT)    return s->rom_8056;
    if (addr >= STUNRUN_BOOT_TABLE_ROM &&
        addr < STUNRUN_BOOT_TABLE_ROM + (uint32_t)(sizeof kTableRom))
        return kTableRom[(addr - STUNRUN_BOOT_TABLE_ROM) / 2u];
    return 0u;
}

static uint16_t stub_port0(void *ctx) { return stub_port0_read((stub_t *)ctx); }

static uint16_t stub_a80000(void *ctx)
{
    stub_t *s = (stub_t *)ctx;
    s->a800_reads++;
    return s->a800;
}

static uint16_t stub_dm(void *ctx, uint16_t off)
{ (void)ctx; (void)off; return 0u; }
static uint16_t stub_irq(void *ctx) { (void)ctx; return 0u; }

/* Must never be called: board_iface.h has no host-port reader, which is
 * exactly why `ori.w #$1800,$c00008.l` is a documented deviation. If the port
 * ever grew one, it would show up here. */
static uint16_t stub_gsp_control(void *ctx, uint16_t off)
{ (void)ctx; (void)off; return 0u; }
static uint16_t stub_gsp_font(void *ctx, uint16_t off)
{ (void)ctx; (void)off; return 0u; }

static void stub_write_rom16(void *ctx, uint32_t addr, uint16_t value)
{
    stub_t *s = (stub_t *)ctx;
    s->rom_writes++;
    if (s->rom_ev_n < 16u) {
        s->rom_ev[s->rom_ev_n].kind = EV_ROM_W;
        s->rom_ev[s->rom_ev_n].a = addr;
        s->rom_ev[s->rom_ev_n].b = value;
    }
    s->rom_ev_n++;
    ev_push(s, EV_ROM_W, addr, value);
}

static void stub_watchdog(void *ctx)
{
    stub_t *s = (stub_t *)ctx;
    s->watchdog++;
    /* $13ec's FIRST action is `clr.w $608000.l`, so the watchdog kick is what
     * marks the start of a wait. Re-phasing the vblank script here keeps every
     * call to $13ec on the same two-read minimum, instead of letting the $1406
     * read at the end of one wait shift the parity of the next. */
    s->vsync_seen = 0u;
    ev_push(s, EV_WATCHDOG, 0u, 0u);
}

static void stub_write_gsp_host(void *ctx, uint16_t off, uint16_t value)
{
    stub_t *s = (stub_t *)ctx;
    unsigned ord = s->gsp_writes;

    if (off < 16u)
        s->host_by_offset[off]++;
    if (s->win_n < s->win_count && s->win_n < WIN_MAX &&
        ord >= s->win_from) {
        s->win_off[s->win_n] = off;
        s->win_val[s->win_n] = value;
        s->win_n++;
    }
    s->gsp_writes++;
    ev_push(s, EV_GSP_W, off, value);
}

static void stub_dm_set(void *ctx, uint16_t off, uint16_t v)
{ (void)ctx; (void)off; (void)v; }
static void stub_ctrl(void *ctx, uint16_t off, uint16_t v)
{ (void)ctx; (void)off; (void)v; }
static void stub_prog(void *ctx, uint32_t off, uint32_t v)
{ (void)ctx; (void)off; (void)v; }

/* ------------------------------------------------------------------------ */
/* Seam recorders: assert the CALL ORDER, not merely that calls happened     */
/* ------------------------------------------------------------------------ */

typedef struct seams_state {
    unsigned n;
    unsigned id[24];
    uint32_t arg[24];
    uint16_t page[24];
    uint32_t target[24];
} seams_state_t;

static void ss_push(seams_state_t *ss, unsigned id)
{
    if (ss->n < 24u) {
        ss->id[ss->n] = id;
        ss->arg[ss->n] = 0u;
        ss->page[ss->n] = 0u;
        ss->target[ss->n] = 0u;
    }
    ss->n++;
}

static void st_text(void *ctx, uint16_t index, uint32_t cont)
{
    seams_state_t *ss = (seams_state_t *)ctx;
    ss_push(ss, STUNRUN_BOOT_SEAM_TEXT_DRAW);
    if (ss->n >= 1u && ss->n <= 24u) {
        ss->arg[ss->n - 1u] = index;
        ss->target[ss->n - 1u] = cont;
    }
}

#define SEAM_VOID(NAME, ID)                                    \
    static void NAME(void *ctx)                                \
    {                                                         \
        ss_push((seams_state_t *)ctx, (ID));                   \
    }

SEAM_VOID(st_pre_67a,   STUNRUN_BOOT_SEAM_PRE_67A)
SEAM_VOID(st_pre_3f8,   STUNRUN_BOOT_SEAM_PRE_3F8)
SEAM_VOID(st_pre_570,   STUNRUN_BOOT_SEAM_PRE_570)
SEAM_VOID(st_pre_636,   STUNRUN_BOOT_SEAM_PRE_636)
SEAM_VOID(st_dm_clear,  STUNRUN_BOOT_SEAM_ADSP_DM_CLEAR)
SEAM_VOID(st_irq_vec,   STUNRUN_BOOT_SEAM_IRQ_VECTOR)
SEAM_VOID(st_input,     STUNRUN_BOOT_SEAM_INPUT_SCAN)
SEAM_VOID(st_led,       STUNRUN_BOOT_SEAM_LED_BAR)
SEAM_VOID(st_page_draw, STUNRUN_BOOT_SEAM_PAGE_DRAW)

static void st_exit_page(void *ctx, uint16_t page, uint32_t target)
{
    seams_state_t *ss = (seams_state_t *)ctx;
    ss_push(ss, STUNRUN_BOOT_SEAM_EXIT_PAGE);
    if (ss->n >= 1u && ss->n <= 24u) {
        ss->page[ss->n - 1u] = page;
        ss->target[ss->n - 1u] = target;
    }
}

/* ------------------------------------------------------------------------ */
/* Fixture                                                                   */
/* ------------------------------------------------------------------------ */

static void stub_init(stub_t *s)
{
    memset(s, 0, sizeof *s);
    /* $608c is the gate's ONLY read_port0: $609c reads $a80001, a different
     * port. One, not two -- getting this wrong makes the first $13ec start
     * mid-script and silently inflates its read count. */
    s->gate_reads = 1u;
    s->vsync_high = (uint16_t)STUNRUN_PORT0_BIT_VSYNCB;
    /* Drive the board with the words the shipped ROM image holds. */
    s->rom_801e = ROM_801E;
    s->rom_8036 = ROM_8036;
    s->rom_8046 = ROM_8046;
    s->rom_8056 = ROM_8056;
}

static void board_init(stunrun_board_t *b, void *ctx)
{
    memset(b, 0, sizeof *b);
    b->ctx = ctx;
    b->read_rom8 = stub_rom8;
    b->read_rom16 = stub_rom16;
    b->read_port0 = stub_port0;
    b->read_a80000 = stub_a80000;
    b->read_adsp_dm = stub_dm;
    b->read_adsp_irq = stub_irq;
    b->read_gsp_control = stub_gsp_control;
    b->read_gsp_font = stub_gsp_font;
    b->write_rom16 = stub_write_rom16;
    b->write_watchdog = stub_watchdog;
    b->write_adsp_dm = stub_dm_set;
    b->write_adsp_ctrl = stub_ctrl;
    b->write_adsp_prog = stub_prog;
    b->write_gsp_host = stub_write_gsp_host;
}

static void seams_init(stunrun_boot_seams_t *sm, seams_state_t *ss)
{
    memset(sm, 0, sizeof *sm);
    memset(ss, 0, sizeof *ss);
    sm->ctx = ss;
    sm->text_draw = st_text;
    sm->pre_67a = st_pre_67a;
    sm->pre_3f8 = st_pre_3f8;
    sm->pre_570 = st_pre_570;
    sm->pre_636 = st_pre_636;
    sm->adsp_dm_clear = st_dm_clear;
    sm->adsp_irq_vector_init = st_irq_vec;
    sm->input_scan = st_input;
    sm->led_bar = st_led;
    sm->page_draw = st_page_draw;
    sm->exit_page = st_exit_page;
}

/* The --service path: SELF-TEST clear (pressed), START1 clear. */
static void service_inputs(stub_t *s)
{
    s->port0_gate = 0u;
    s->a800 = 0u;
}

/* ------------------------------------------------------------------------ */
/* 1. the chain, in order                                                    */
/* ------------------------------------------------------------------------ */

/*
 * Transcribed by hand from the `lea ($6,PC),A0` / `bra` / `jmp (A0)` pairs:
 *
 *   $506c entry -> $53f2 -> $507a -> $548e -> $5082 -> $54c2 -> $508a
 *   $508a -> $5098 -> $6086 -> ($0f94 -> $1032 ...)
 *
 * $507a, $5082, $5098 and $50a0 are the `lea` stubs themselves, not routines
 * entered by `bra`, so they are not stage entries. $50a0 is not visited at all
 * -- it is the loop-back arm $56e6 returns to through A6.
 */
static void test_chain_order(void)
{
    stub_t s;
    stunrun_board_t b;
    stunrun_boot_seams_t sm;
    seams_state_t ss;
    stunrun_boot_result_t r;
    unsigned i;

    stub_init(&s);
    board_init(&b, &s);
    seams_init(&sm, &ss);
    service_inputs(&s);

    r = stunrun_boot_chain_run(&b, &sm, 1u);

    /* $506c: movea.l #$ffffc000,A7 */
    check(r.reset_sp == 0xFFFFC000u);

    check(r.stage_count == 7u);
    if (r.stage_count == 7u) {
        check(r.stages[0] == 0x0000506Cu);
        check(r.stages[1] == 0x000053F2u);
        check(r.stages[2] == 0x0000548Eu);
        check(r.stages[3] == 0x000054C2u);
        check(r.stages[4] == 0x0000508Au);
        check(r.stages[5] == 0x00006086u);
        check(r.stages[6] == 0x00000F94u);
    }
    /* $50a0 is the loop-back arm and must not appear. */
    for (i = 0u; i < r.stage_count && i < STUNRUN_BOOT_MAX_STAGES; i++)
        check(r.stages[i] != 0x000050A0u);

    /* $508a: move.w #$47,D0 ; $508e: lea ($8,PC),A2 -> $5098 ;
     * $5092: jmp $25e8.l */
    check(r.text_draws == 1u);
    if (r.text_draws == 1u) {
        check(r.text_index[0] == 0x0047u);
        check(r.text_cont[0] == 0x00005098u);
    }

    /* $53f2: two NWR-latch writes ($60400c, $60401c) and the $53fe settle spin.
     * The spin issues no board access at all, so it is the one thing at $53f2
     * a board stub cannot observe; count it. probe_reads is 2 because this run
     * reaches $0f94 and $0fc2/$0fc8 read two unmapped locations. */
    check(r.nwr_latch_writes == 2u);
    check(r.delay_iterations == 150u);
    check(r.probe_reads == STUNRUN_BOOT_PROBE_READS);
}

/* ------------------------------------------------------------------------ */
/* 2. the three clear stages                                                */
/* ------------------------------------------------------------------------ */

/*
 * All counts are the dbras, read off the immediates:
 *   $5486  clr.w $608000.l                       1 watchdog kick
 *   $548e  dbra $ff inside dbra $383        256*900 = 230400 host writes
 *   $54c2  two single clr.w $c0000c.l, then two dbra $ff ramps of 256
 *   $548c/$54c0/$552a  jmp (A0)
 *   $566a  dbra $ff                            256 host writes of $ffff
 */
static void test_clear_stages(void)
{
    stub_t s;
    stunrun_board_t b;
    stunrun_boot_seams_t sm;
    seams_state_t ss;
    stunrun_boot_result_t r;

    stub_init(&s);
    board_init(&b, &s);
    seams_init(&sm, &ss);
    service_inputs(&s);

    /* max 0: the chain and the three clears only, no handshake. */
    r = stunrun_boot_chain_run(&b, &sm, 0u);

    /* HSTDAT, offset 0xc:
     *   $53f2  8 table + 5 fixed                                  13
     *   $548e  256 * 900                                       230400
     *   $54c2  2 (the clr.w pair) + 256 + 256                     514 */
    check(s.host_by_offset[STUNRUN_BOOT_GSP_HSTDAT] ==
          STUNRUN_BOOT_TABLE_ENTRIES + 5u +
          STUNRUN_BOOT_CLEAR_ALL_INNER * STUNRUN_BOOT_CLEAR_ALL_OUTER +
          2u + 2u * STUNRUN_BOOT_CLEAR_RAMP_ENTRIES);

    /* HSTCTL, offset 8: $53f2's move.w #$b800, then the ori at $548e and
     * $54c2. Three, not two -- the $b800 write is easy to forget. */
    check(s.host_by_offset[STUNRUN_BOOT_GSP_HSTCTL] == 3u);

    /* HSTADR, offset 2: every `move.l` splits into one write here and one at
     * offset 4, so the two tallies are equal. $53f2 8+5, $548e 1, $54c2 4. */
    check(s.host_by_offset[STUNRUN_BOOT_GSP_HSTADR] ==
          STUNRUN_BOOT_TABLE_ENTRIES + 5u + 1u + 4u);
    check(s.host_by_offset[STUNRUN_BOOT_GSP_HSTADR + 2u] ==
          STUNRUN_BOOT_TABLE_ENTRIES + 5u + 1u + 4u);

    /* Watchdog: $54b6 inside the $548e outer loop, 900 times, and $5524 once.
     * Verified against the ROM by scanning $53f2-$552a for the clr.w $608000
     * encoding, which yields exactly those two sites -- the ordering matters,
     * because the kick inside the loop is what keeps the board alive across
     * 230400 host writes.
     *
     * The gate is armed to HALT here so the handshake is skipped, which
     * isolates the chain's own total. The handshake adds five more: $132a x2,
     * $0fe2, $0ff2, and $13ec's in the one pre-loop $1092 block. */
    stub_init(&s);
    board_init(&b, &s);
    seams_init(&sm, &ss);
    s.port0_gate = (uint16_t)STUNRUN_PORT0_BIT_SELFTEST;
    r = stunrun_boot_chain_run(&b, &sm, 0u);
    check(r.gate == STUNRUN_BOOT_GATE_HALT);
    check(r.watchdog_writes == STUNRUN_BOOT_CLEAR_ALL_OUTER + 1u);
    check(s.watchdog == STUNRUN_BOOT_CLEAR_ALL_OUTER + 1u);
    check(s.watchdog == r.watchdog_writes);

    /* And the full --service run, which reaches $0f94. */
    stub_init(&s);
    board_init(&b, &s);
    seams_init(&sm, &ss);
    service_inputs(&s);
    r = stunrun_boot_chain_run(&b, &sm, 0u);
    check(r.gate == STUNRUN_BOOT_GATE_HANDSHAKE);
    check(r.watchdog_writes == STUNRUN_BOOT_CLEAR_ALL_OUTER + 1u + 5u);
    check(s.watchdog == r.watchdog_writes);
}

/*
 * $54c2's ramps are store-BEFORE-increment. Assert the exact values, not only
 * the total: an increment-before-store rewrite would keep 256 writes per ramp
 * and still be wrong, because the ramp would start at 0x0101 instead of 0x0000
 * and end at 0x0000 instead of 0xffff. Transcribed:
 *   ramp one: addi.w #$101, so k*0x101 -> 0x0000, 0x0101, ... 0xFFFF
 *   ramp two: addq.w #1,   so k        -> 0x0000, 0x0001, ... 0x00FF
 */
static void test_ramp_values(void)
{
    stub_t s;
    stunrun_board_t b;
    stunrun_boot_seams_t sm;
    seams_state_t ss;
    stunrun_boot_result_t r;
    /* The entire $54c2 prologue and the first five ramp-one stores, in order,
     * transcribed instruction by instruction. This is a whole-stream
     * assertion, not just the HSTDAT column, so it also pins the interleaving
     * of the `move.l` address writes with the `clr.w $c0000c.l` data writes --
     * an ordering a column-count rewrite would silently change. */
    static const uint16_t prologue_off[14] = {
        0x0008u, 0x0002u, 0x0004u, 0x000Cu,          /* ori, $f4800020, clr  */
        0x0002u, 0x0004u, 0x000Cu,                   /* $f4800030, clr       */
        0x0002u, 0x0004u,                            /* $f5000000            */
        0x000Cu, 0x000Cu, 0x000Cu, 0x000Cu, 0x000Cu  /* ramp one, k = 0..4  */
    };
    static const uint16_t prologue_val[14] = {
        0x1800u, 0xF480u, 0x0020u, 0x0000u,
        0xF480u, 0x0030u, 0x0000u,
        0xF500u, 0x0000u,
        0x0000u, 0x0101u, 0x0202u, 0x0303u, 0x0404u
    };
    /* Ramp one's last five stores (251*257 = $fbfb ... 255*257 = $ffff), then
     * $550a's move.l #$f5800000, then ramp two's first three. */
    static const uint16_t tail_off[10] = {
        0x000Cu, 0x000Cu, 0x000Cu, 0x000Cu, 0x000Cu,
        0x0002u, 0x0004u,
        0x000Cu, 0x000Cu, 0x000Cu
    };
    static const uint16_t tail_val[10] = {
        0xFBFBu, 0xFCFCu, 0xFDFDu, 0xFEFEu, 0xFFFFu,
        0xF580u, 0x0000u,
        0x0000u, 0x0001u, 0x0002u
    };
    unsigned i;

    stub_init(&s);
    board_init(&b, &s);
    seams_init(&sm, &ss);
    service_inputs(&s);
    s.win_from = HOST_WRITES_AT_RAMP;
    s.win_count = 14u;

    r = stunrun_boot_chain_run(&b, &sm, 0u);
    check(r.handshake_iterations == 0u);

    check(s.win_n == 14u);
    for (i = 0u; i < s.win_n && i < 14u; i++) {
        check(s.win_off[i] == prologue_off[i]);
        check(s.win_val[i] == prologue_val[i]);
    }

    /* Same run, windowed at the ramp's tail. */
    stub_init(&s);
    board_init(&b, &s);
    seams_init(&sm, &ss);
    service_inputs(&s);
    s.win_from = RAMP1_START + 251u;
    s.win_count = 10u;

    r = stunrun_boot_chain_run(&b, &sm, 0u);

    check(s.win_n == 10u);
    for (i = 0u; i < s.win_n && i < 10u; i++) {
        check(s.win_off[i] == tail_off[i]);
        check(s.win_val[i] == tail_val[i]);
    }

    /* The window is only meaningful if the derived ordinal is right: the first
     * captured write must be $54c2's ori and the last must still be inside ramp
     * two, which ends 256 stores after ramp one starts. */
    check(s.win_from < RAMP1_START + 256u + 256u);
    check(s.win_from == RAMP1_START + 251u);
}

/*
 * $566a is not on the reset path -- it is $50a8's target, reached from the
 * game loop's return. Exercised directly. Its address arithmetic is
 * (index << 12) - 0x80000, from the clr.l / move.w / lsl.l / lsl.l / addi.l at
 * $5672-$567a. With D0 = $0054: (0x54 << 12) = 0x54000 and
 * 0x54000 + 0xff800000 = 0xff854000, so the two 16-bit writes at offsets 2
 * and 4 carry 0xff85 and 0x4000.
 */
static void test_fill_page(void)
{
    stub_t s;
    stunrun_board_t b;

    memset(&s, 0, sizeof s);
    board_init(&b, &s);

    stunrun_boot_gsp_fill_page(&b, 0x0054u);

    check(s.host_by_offset[STUNRUN_BOOT_GSP_HSTDAT] ==
          STUNRUN_BOOT_FILL_PAGE_ENTRIES);
    /* The clr.l before move.w matters: without it the upper half of D1 would be
     * leftover, so the offset-2 word would not be 0xff85. */
    check(ev_is(&s, 0u, EV_GSP_W, STUNRUN_BOOT_GSP_HSTCTL, 0x1800u));
    check(ev_is(&s, 1u, EV_GSP_W, STUNRUN_BOOT_GSP_HSTADR, 0xFF85u));
    check(ev_is(&s, 2u, EV_GSP_W, STUNRUN_BOOT_GSP_HSTADR + 2u, 0x4000u));
    check(ev_is(&s, 3u, EV_GSP_W, STUNRUN_BOOT_GSP_HSTDAT, 0xFFFFu));

    /* $50a0's index $54 and $508a's index $47, to pin the formula's low bits:
     * 0x47 -> (0x47000 + 0xff800000) = 0xff847000. */
    memset(&s, 0, sizeof s);
    board_init(&b, &s);
    stunrun_boot_gsp_fill_page(&b, 0x0047u);
    check(ev_is(&s, 1u, EV_GSP_W, STUNRUN_BOOT_GSP_HSTADR, 0xFF84u));
    check(ev_is(&s, 2u, EV_GSP_W, STUNRUN_BOOT_GSP_HSTADR + 2u, 0x7000u));
}

/* ------------------------------------------------------------------------ */
/* 3. the $6086 gate, all three arms                                          */
/* ------------------------------------------------------------------------ */

/*
 * POLARITY, restated because the point is that it is inverted: SELF-TEST is
 * PORT_SERVICE(0x20, IP_ACTIVE_LOW) (harddriv.cpp:798), so a CLEAR bit 5 means
 * Service Mode is PRESSED, and `beq $609c` CONTINUES on clear. $60c001 is the
 * LOW byte of the 16-bit port read, so btst #5 tests bit 5 of the value.
 * START1 (`btst #$2,$a80001.l`) has no active-low inversion in the input
 * table, so `bne $60ac` continues on SET.
 *
 *   SELF-TEST SET   (not pressed)            -> jmp $260.l    (HALT)
 *   SELF-TEST CLEAR (pressed), START1 CLEAR  -> jmp $0f94.l   (HANDSHAKE)
 *   SELF-TEST CLEAR (pressed), START1 SET    -> $60ac         (SELFTEST_TABLE)
 */
static void test_gate_arms(void)
{
    stub_t s;
    stunrun_board_t b;
    stunrun_boot_seams_t sm;
    seams_state_t ss;
    stunrun_boot_result_t r;

    /* --- SELF-TEST SET (Service Mode NOT pressed) -> jmp $260.l --- */
    stub_init(&s);
    board_init(&b, &s);
    seams_init(&sm, &ss);
    s.port0_gate = (uint16_t)STUNRUN_PORT0_BIT_SELFTEST;
    s.a800 = 0u;
    r = stunrun_boot_chain_run(&b, &sm, 4u);
    check(r.selftest_bit == 1u);
    check(r.gate == STUNRUN_BOOT_GATE_HALT);
    /* It never reaches the START1 read and never reaches $0f94, so $0f94 is
     * not a stage and none of its seven clears happen. */
    check(s.a800_reads == 0u);
    check(r.flag_clears == 0u);
    check(r.handshake_iterations == 0u);
    check(r.stage_count == 6u);

    /* --- SELF-TEST CLEAR (pressed) + START1 SET -> $60ac --- */
    stub_init(&s);
    board_init(&b, &s);
    seams_init(&sm, &ss);
    s.port0_gate = 0u;
    s.a800 = (uint16_t)STUNRUN_BOOT_A80000_BIT_START1;
    r = stunrun_boot_chain_run(&b, &sm, 4u);
    check(r.selftest_bit == 0u);
    check(r.start1_bit == 1u);
    check(r.gate == STUNRUN_BOOT_GATE_SELFTEST_TABLE);
    check(r.flag_clears == 0u);
    check(r.handshake_iterations == 0u);
    check(r.stage_count == 6u);

    /* --- SELF-TEST CLEAR (pressed) + START1 CLEAR -> jmp $0f94.l --- */
    stub_init(&s);
    board_init(&b, &s);
    seams_init(&sm, &ss);
    service_inputs(&s);
    r = stunrun_boot_chain_run(&b, &sm, 4u);
    check(r.selftest_bit == 0u);
    check(r.start1_bit == 0u);
    check(r.gate == STUNRUN_BOOT_GATE_HANDSHAKE);
    check(r.stage_count == 7u);
    check(r.flag_clears == 7u);
    check(r.handshake_iterations == 4u);

    /* Unrelated bits in the same port word must not disturb the arms: bit 2
     * (VSYNCB), bits 7 and 8. Only bit 5 is consulted at $608c. */
    stub_init(&s);
    board_init(&b, &s);
    seams_init(&sm, &ss);
    s.port0_gate = (uint16_t)(STUNRUN_PORT0_BIT_SELFTEST |
                              STUNRUN_PORT0_BIT_VSYNCB | 0x0180u);
    s.a800 = 0u;
    r = stunrun_boot_chain_run(&b, &sm, 1u);
    check(r.selftest_bit == 1u);
    check(r.gate == STUNRUN_BOOT_GATE_HALT);

    /* And the mirror: bit 2 set but bit 5 clear must NOT halt. */
    stub_init(&s);
    board_init(&b, &s);
    seams_init(&sm, &ss);
    s.port0_gate = (uint16_t)(STUNRUN_PORT0_BIT_VSYNCB | 0x0180u);
    s.a800 = 0u;
    r = stunrun_boot_chain_run(&b, &sm, 1u);
    check(r.selftest_bit == 0u);
    check(r.gate == STUNRUN_BOOT_GATE_HANDSHAKE);
}

/* ------------------------------------------------------------------------ */
/* 4. $0f94: the seven ROM flag clears                                       */
/* ------------------------------------------------------------------------ */

/*
 * Transcribed from $0ff8-$1010:
 *   clr.w $801e.w / $8036.w / $8046.w / $8056.w / $8066.w / $8076.w / $8086.w
 *
 * Asserted two independent ways, because either alone is weak: the PORT's own
 * flag_clear_addr[] log, and the STUB's recorded write stream, which also
 * proves nothing else wrote a ROM word in between.
 *
 * These are absolute-SHORT, so the 68010 addresses are $0000801e..$00008086,
 * inside map(0x000000,0x0fffff).rom() (harddriv.cpp:602) and NOT the ADSP
 * window at $00800000 (init_adsp, harddriv.cpp:4822). They are modelled as ROM
 * writes and read back as ROM, so no test can pass because a write stuck.
 */
static void test_flag_clears(void)
{
    stub_t s;
    stunrun_board_t b;
    stunrun_boot_seams_t sm;
    seams_state_t ss;
    stunrun_boot_result_t r;
    static const uint32_t want[STUNRUN_BOOT_FLAG_COUNT] = {
        0x0000801Eu, 0x00008036u, 0x00008046u, 0x00008056u,
        0x00008066u, 0x00008076u, 0x00008086u
    };
    unsigned i;

    stub_init(&s);
    board_init(&b, &s);
    seams_init(&sm, &ss);
    service_inputs(&s);

    r = stunrun_boot_chain_run(&b, &sm, 1u);

    check(r.flag_clears == STUNRUN_BOOT_FLAG_COUNT);
    for (i = 0u; i < STUNRUN_BOOT_FLAG_COUNT && i < r.flag_clears; i++)
        check(r.flag_clear_addr[i] == want[i]);

    /* Seven flag clears, then one iteration's worth of $8036/$801e traffic: the
     * seven init clears plus $103e's clr.w $8036.w and $1052's
     * move.w D0,$801e.w. */
    check(s.rom_writes == 7u + 2u);
    for (i = 0u; i < STUNRUN_BOOT_FLAG_COUNT && i < s.rom_ev_n; i++)
        check(rom_ev_is(&s, i, want[i], 0u));

    /* $0fc2/$0fc8 probe two unmapped locations; counted, never decoded. */
    check(r.probe_reads == STUNRUN_BOOT_PROBE_READS);

    /* $102e branches to $1092, NOT to $1032, so the $1092 block runs before the
     * first pass of $1032: LED bar, page draw, $13ec, then the poll. Index 0
     * is $508a's text draw, which happens before the gate. */
    check(ss.id[0] == STUNRUN_BOOT_SEAM_TEXT_DRAW);      /* $508a */
    check(ss.id[1] == STUNRUN_BOOT_SEAM_PRE_67A);        /* $0f98 */
    check(ss.id[2] == STUNRUN_BOOT_SEAM_ADSP_DM_CLEAR);  /* $132a */
    check(ss.id[3] == STUNRUN_BOOT_SEAM_PRE_3F8);        /* $0fd6 */
    check(ss.id[4] == STUNRUN_BOOT_SEAM_PRE_570);        /* $0fdc */
    check(ss.id[5] == STUNRUN_BOOT_SEAM_PRE_636);        /* $0fec */
    check(ss.id[6] == STUNRUN_BOOT_SEAM_IRQ_VECTOR);     /* $1014-$1024 */
    check(ss.id[7] == STUNRUN_BOOT_SEAM_LED_BAR);        /* $1092 */
    check(ss.id[8] == STUNRUN_BOOT_SEAM_PAGE_DRAW);      /* $109e */
    check(ss.id[9] == STUNRUN_BOOT_SEAM_INPUT_SCAN);     /* $1032 */
}

/* ------------------------------------------------------------------------ */
/* 5. $1032/$1036: the wait loop                                             */
/* ------------------------------------------------------------------------ */

/*
 * THE MEASURED ARM. With the words the shipped ROM image holds, $8036 reads
 * 0x4154 ('AT'), so `tst.w $8036.w` is never zero and `beq $105a` is never
 * taken. That makes $8046 and $8056 unreachable, $10a8 unreachable, and the
 * only reachable path the advance arm followed by the $1092 block. Asserting
 * the arm COUNTS is the point: back, exit and dispatch must all be 0 for a
 * board that behaves like this ROM.
 */
static void test_loop_with_real_rom_words(void)
{
    stub_t s;
    stunrun_board_t b;
    stunrun_boot_seams_t sm;
    seams_state_t ss;
    stunrun_boot_result_t r;

    stub_init(&s);
    board_init(&b, &s);
    seams_init(&sm, &ss);
    service_inputs(&s);

    r = stunrun_boot_chain_run(&b, &sm, 5u);

    check(r.handshake_iterations == 5u);
    check(r.handshake_cap_hit == 1u);   /* the ROM never exits; we stopped it */
    check(r.handshake_advance == 5u);
    check(r.handshake_back == 0u);
    check(r.handshake_exit == 0u);
    check(r.handshake_dispatch == 0u);
    check(r.handshake_halt == 0u);

    /* $1042/$1046: the page counter reads $4552 every time, because the store
     * at $1052 was discarded. So the final value is the ROM word plus one on
     * every iteration, not a running sequence. Asserting the constant is how
     * the write-discard is checked. */
    check(r.page == (uint16_t)(ROM_801E + 1u));
    check(r.page == 0x4553u);

    /* $102e's `bra $1092` runs one block, then each of the 5 iterations ends
     * with another: 6 blocks, 6 $13ec calls, 3 port0 reads each, plus the 2
     * gate reads at $608c/$609c. */
    check(r.vsync_calls == 6u);
    check(r.port0_reads == 1u + 6u * 3u);
    check(r.seam_calls[STUNRUN_BOOT_SEAM_INPUT_SCAN] == 5u);
    check(r.seam_calls[STUNRUN_BOOT_SEAM_LED_BAR] == 6u);
    check(r.seam_calls[STUNRUN_BOOT_SEAM_PAGE_DRAW] == 6u);
}

/*
 * THE "NEVER SET" CASE. A board whose three flags all read zero, so every
 * poll takes `beq $105a`, `beq $107c`, `beq $108c` and lands on
 * `bsr $13ec ; bra $1032` with no $1092 block in between.
 *
 * Bound: the explicit max_iterations argument. 8 is chosen because it is
 * instant to run and large enough that a loop which iterated once, or not at
 * all, fails the count. The port's own default is
 * STUNRUN_BOOT_HANDSHAKE_MAX_ITERATIONS (64) -- see boot_chain.h for why a
 * bound has to exist at all, since on the real path $1032 has no exit.
 */
static void test_loop_never_set(void)
{
    stub_t s;
    stunrun_board_t b;
    stunrun_boot_seams_t sm;
    seams_state_t ss;
    stunrun_boot_result_t r;

    stub_init(&s);
    board_init(&b, &s);
    seams_init(&sm, &ss);
    service_inputs(&s);
    s.rom_8036 = 0u;      /* tst.w reads zero -> beq $105a taken */
    s.rom_8046 = 0u;      /* -> tst.w $8046 reads zero -> beq $107c taken */
    s.rom_8056 = 0u;      /* -> tst.w $8056 reads zero -> beq $108c taken */

    r = stunrun_boot_chain_run(&b, &sm, 8u);

    check(r.handshake_iterations == 8u);
    check(r.handshake_cap_hit == 1u);
    check(r.handshake_advance == 0u);
    check(r.handshake_back == 0u);
    check(r.handshake_exit == 0u);

    /* One $1092 block before the loop, then $108c's $13ec on every poll. */
    check(r.vsync_calls == 9u);
    check(r.port0_reads == 1u + 9u * 3u);
    check(s.rom_writes == 7u);   /* the seven init clears; nothing else */

    /* After the pre-loop block, every entry is an input scan: no LED bar, no
     * page draw, because $1092 is never reached on this path. Index 0 is the
     * $508a text draw. */
    check(ss.id[9] == STUNRUN_BOOT_SEAM_INPUT_SCAN);
    check(ss.id[10] == STUNRUN_BOOT_SEAM_INPUT_SCAN);
    check(ss.id[11] == STUNRUN_BOOT_SEAM_INPUT_SCAN);
    check(r.seam_calls[STUNRUN_BOOT_SEAM_LED_BAR] == 1u);
    check(r.seam_calls[STUNRUN_BOOT_SEAM_PAGE_DRAW] == 1u);
}

/*
 * THE "SET IMMEDIATELY" CASE. $8036 reads non-zero on the very first test, so
 * the loop takes the advance arm on iteration 1 and never tests $8046 or
 * $8056 at all.
 */
static void test_loop_set_immediately(void)
{
    stub_t s;
    stunrun_board_t b;
    stunrun_boot_seams_t sm;
    seams_state_t ss;
    stunrun_boot_result_t r;

    stub_init(&s);
    board_init(&b, &s);
    seams_init(&sm, &ss);
    service_inputs(&s);
    s.rom_8036 = 0x0001u;    /* tst.w -> non-zero on the first read */
    s.rom_801e = 0x0000u;    /* so the counter advances 0 -> 1 */

    r = stunrun_boot_chain_run(&b, &sm, 1u);

    check(r.handshake_iterations == 1u);
    check(r.handshake_advance == 1u);
    check(r.handshake_back == 0u);
    check(r.handshake_exit == 0u);
    check(r.handshake_dispatch == 0u);
    check(r.handshake_cap_hit == 1u);   /* the cap is what stopped it */
    /* $1048: cmpi.w #$4,D0 -- 0+1 is 1, so `bne $1052` stores 1. */
    check(r.page == 1u);
    check(r.vsync_calls == 2u);         /* pre-loop block + this iteration */

    /* $103e clr.w $8036.w, then $1052 move.w D0,$801e.w: two more ROM writes,
     * in that order, after the seven init clears. */
    check(s.rom_writes == 9u);
    check(rom_ev_is(&s, 7u, STUNRUN_BOOT_FLAG_ADVANCE, 0u));
    check(rom_ev_is(&s, 8u, STUNRUN_BOOT_FLAG_PAGE, 1u));
}

/*
 * The page counter's modulus, from $1048/$1050. `cmpi.w #$4,D0` with `bne
 * $1052` and $1050 `clr.w D0` makes the sequence 1, 2, 3, 0.
 *
 * Four separate runs rather than one four-iteration run, and deliberately so:
 * a board that PERSISTED the store would be a board the real machine cannot
 * produce, because $801e is ROM and the store at $1052 is discarded. Four
 * single-advance runs from 0, 1, 2 and 3 pin all four values of the modulus
 * without pretending the hardware behaves differently.
 */
static void test_page_counter_modulus(void)
{
    static const uint16_t start[4] = { 0u, 1u, 2u, 3u };
    static const uint16_t want[4] = { 1u, 2u, 3u, 0u };
    unsigned k;

    for (k = 0u; k < 4u; k++) {
        stub_t s;
        stunrun_board_t b;
        stunrun_boot_seams_t sm;
        seams_state_t ss;
        stunrun_boot_result_t r;

        stub_init(&s);
        board_init(&b, &s);
        seams_init(&sm, &ss);
        service_inputs(&s);
        s.rom_8036 = 0x0001u;
        s.rom_801e = start[k];

        r = stunrun_boot_chain_run(&b, &sm, 1u);

        check(r.handshake_iterations == 1u);
        check(r.handshake_advance == 1u);
        check(r.page == want[k]);
        /* And the value actually stored at $1052, not just the register. */
        check(rom_ev_is(&s, 8u, STUNRUN_BOOT_FLAG_PAGE, want[k]));
    }
}

/*
 * The $8046 arm, which the shipped ROM image cannot reach. Asserted with the
 * stub explicitly saying so. $106a `subq.w #1,D0` on 0 gives 0xffff, `bpl
 * $1074` is not taken, and $1070 loads $0003 -- transcribed from the
 * immediates. A version that tested "if zero" instead of the sign would agree
 * on 0 and disagree on $8000, which is why $8000 is checked too.
 */
static void test_back_arm(void)
{
    stub_t s;
    stunrun_board_t b;
    stunrun_boot_seams_t sm;
    seams_state_t ss;
    stunrun_boot_result_t r;

    stub_init(&s);
    board_init(&b, &s);
    seams_init(&sm, &ss);
    service_inputs(&s);
    s.rom_8036 = 0u;
    s.rom_8046 = 0x0001u;
    s.rom_801e = 0u;           /* 0 - 1 wraps negative -> reload $0003 */

    r = stunrun_boot_chain_run(&b, &sm, 1u);

    check(r.handshake_back == 1u);
    check(r.handshake_advance == 0u);
    check(r.page == 3u);
    check(s.rom_writes == 9u);
    check(rom_ev_is(&s, 7u, STUNRUN_BOOT_FLAG_BACK, 0u));
    check(rom_ev_is(&s, 8u, STUNRUN_BOOT_FLAG_PAGE, 3u));

    /* $8000: subq.w #1 gives $7fff, positive, so `bpl $1074` IS taken and the
     * decremented value is stored unmodified -- no reload. */
    stub_init(&s);
    board_init(&b, &s);
    seams_init(&sm, &ss);
    service_inputs(&s);
    s.rom_8036 = 0u;
    s.rom_8046 = 0x0001u;
    s.rom_801e = 0x8000u;

    r = stunrun_boot_chain_run(&b, &sm, 1u);
    check(r.page == 0x7FFFu);
    check(rom_ev_is(&s, 8u, STUNRUN_BOOT_FLAG_PAGE, 0x7FFFu));
}

/*
 * The $8056 arm and the $10a8 dispatch. $10b2's lsl.w #2 is on the LOW word of
 * D0, so the table index is (page * 4). The target long comes from the ROM
 * table at $fa2; the synthetic image holds zeros there, so the observables
 * that matter are that the seam was reached with the right page and that the
 * table was read.
 */
static void test_exit_arm(void)
{
    stub_t s;
    stunrun_board_t b;
    stunrun_boot_seams_t sm;
    seams_state_t ss;
    stunrun_boot_result_t r;
    unsigned i;
    int saw_exit = 0;

    stub_init(&s);
    board_init(&b, &s);
    seams_init(&sm, &ss);
    service_inputs(&s);
    s.rom_8036 = 0u;
    s.rom_8046 = 0u;
    s.rom_8056 = 0x0001u;
    s.rom_801e = 2u;

    r = stunrun_boot_chain_run(&b, &sm, 1u);

    check(r.handshake_exit == 1u);
    check(r.handshake_dispatch == 1u);
    check(r.handshake_advance == 0u);
    check(r.handshake_back == 0u);
    for (i = 0u; i < ss.n && i < 24u; i++)
        if (ss.id[i] == STUNRUN_BOOT_SEAM_EXIT_PAGE) {
            check(ss.page[i] == 2u);
            check(ss.target[i] == 0u);   /* synthetic table is all zeros */
            saw_exit = 1;
        }
    check(saw_exit == 1);
    /* $1084: clr.w $8056.w, then $1088's `bra $10a8`, then $10ba's `bra $1092`.
     * Only ONE ROM write: the $8056 arm stores nothing to $801e. */
    check(s.rom_writes == 8u);
    check(rom_ev_is(&s, 7u, STUNRUN_BOOT_FLAG_EXIT, 0u));
}

/* ------------------------------------------------------------------------ */
/* 6. $13ec: the vsync wait                                                  */
/* ------------------------------------------------------------------------ */

/*
 * Two phases on /VSYNCB, PORT_BIT(0x04, IP_ACTIVE_LOW, vblank)
 * (harddriv.cpp:795), plus the $1406 SELF-TEST check. On the minimum path that
 * is 3 reads: one that satisfies `beq $13f2`, one that satisfies `bne $13fc`,
 * one for the SELF-TEST test. With the line held low, phase 1 never terminates
 * and the port's own bound stops it -- which is what makes the bound
 * observable rather than decorative.
 */
static void test_vsync(void)
{
    stub_t s;
    stunrun_board_t b;
    stunrun_boot_vsync_t v;

    /* Minimum path: 2 phase reads + the $1406 read. */
    memset(&s, 0, sizeof s);
    board_init(&b, &s);
    s.gate_reads = 0u;
    s.vsync_high = (uint16_t)STUNRUN_PORT0_BIT_VSYNCB;

    stunrun_boot_wait_vsync(&b, 512u, &v);
    check(v.reads == 3u);
    check(v.capped == 0u);
    check(v.halted == 0u);
    check(s.watchdog == 1u);          /* $13ec's clr.w $608000.l */

    /* Line held low: phase 1 spins to the bound, and the result says so. */
    memset(&s, 0, sizeof s);
    board_init(&b, &s);
    s.vsync_never_high = 1;
    s.gate_reads = 0u;
    stunrun_boot_wait_vsync(&b, 64u, &v);
    check(v.capped == 1u);
    check(v.reads >= 64u);
    check(v.halted == 0u);

    /* $1406: SELF-TEST SET -> jmp $260.l, i.e. halted. Bit 5 rides on the
     * line-high value, which is also what ends phase 1, so no extra plumbing
     * is needed: this is the $1406 read landing on a line-high sample. */
    memset(&s, 0, sizeof s);
    board_init(&b, &s);
    s.gate_reads = 0u;
    s.vsync_high = (uint16_t)(STUNRUN_PORT0_BIT_VSYNCB |
                              STUNRUN_PORT0_BIT_SELFTEST);
    stunrun_boot_wait_vsync(&b, 512u, &v);
    check(v.reads == 3u);
    check(v.halted == 1u);
    check(v.capped == 0u);

    /* Bit 5 CLEAR on the same sample: no halt. This is the pair that makes the
     * $1406 test distinguishable from "any line-high read halts". */
    memset(&s, 0, sizeof s);
    board_init(&b, &s);
    s.gate_reads = 0u;
    s.vsync_high = (uint16_t)STUNRUN_PORT0_BIT_VSYNCB;
    stunrun_boot_wait_vsync(&b, 512u, &v);
    check(v.reads == 3u);
    check(v.halted == 0u);
}

/*
 * A halt observed INSIDE the loop: $1406's `jmp $260.l` abandons the chain
 * mid-handshake, so the gate arm is rewritten to HALT and iteration stops.
 * gate_reads 5 covers the two gate btsts plus the $1406 read of the first
 * $13ec (the pre-loop $1092 block), which is where the wait first sees SELF-TEST
 * set.
 */
static void test_halt_inside_loop(void)
{
    stub_t s;
    stunrun_board_t b;
    stunrun_boot_seams_t sm;
    seams_state_t ss;
    stunrun_boot_result_t r;

    stub_init(&s);
    board_init(&b, &s);
    seams_init(&sm, &ss);
    service_inputs(&s);
    s.gate_reads = 1u;      /* $608c is the gate's only read_port0 */
    s.vsync_high = (uint16_t)(STUNRUN_PORT0_BIT_VSYNCB |
                              STUNRUN_PORT0_BIT_SELFTEST);

    r = stunrun_boot_chain_run(&b, &sm, 4u);

    check(r.gate == STUNRUN_BOOT_GATE_HALT);
    check(r.handshake_halt == 1u);
    check(r.handshake_iterations == 0u);
    /* The seven flag clears still happened: $1406 is inside $13ec, which is
     * reached only after $0ff8-$1010. */
    check(r.flag_clears == STUNRUN_BOOT_FLAG_COUNT);
    /* And the halt was seen inside the pre-loop $1092 block, before any pass of
     * $1032 -- so the LED bar and page draw ran but no input scan did. */
    check(r.seam_calls[STUNRUN_BOOT_SEAM_INPUT_SCAN] == 0u);
    check(r.seam_calls[STUNRUN_BOOT_SEAM_LED_BAR] == 1u);
}

/* ------------------------------------------------------------------------ */
/* 7. NULL and empty-seam safety                                             */
/* ------------------------------------------------------------------------ */

static void test_robustness(void)
{
    stub_t s;
    stunrun_board_t b;
    stunrun_boot_result_t r;

    memset(&s, 0, sizeof s);

    /* A NULL board returns a zeroed result rather than dereferencing. */
    r = stunrun_boot_chain_run(NULL, NULL, 4u);
    check(r.stage_count == 0u);
    check(r.reset_sp == 0u);

    /* NULL seams: every out-of-scope call is skipped and counted, so a caller
     * can tell "not called" from "not wired". */
    board_init(&b, &s);
    service_inputs(&s);
    r = stunrun_boot_chain_run(&b, NULL, 2u);
    check(r.gate == STUNRUN_BOOT_GATE_HANDSHAKE);
    check(r.flag_clears == 7u);
    check(r.seam_calls[STUNRUN_BOOT_SEAM_PRE_67A] == 1u);
    check(r.seam_calls[STUNRUN_BOOT_SEAM_TEXT_DRAW] == 1u);
    check(r.seam_calls[STUNRUN_BOOT_SEAM_INPUT_SCAN] == 2u);
    check(r.handshake_iterations == 2u);
    check(r.text_draws == 1u);   /* recorded even with no seam wired */

    /* max == 0 is defined, not an error: the $1092 block runs once, no pass of
     * $1032 happens, and the cap is reported. */
    stub_init(&s);
    board_init(&b, &s);
    service_inputs(&s);
    r = stunrun_boot_chain_run(&b, NULL, 0u);
    check(r.handshake_iterations == 0u);
    check(r.handshake_cap_hit == 1u);
    check(r.vsync_calls == 1u);   /* the $1092 block still ran */

    /* NULL tolerance on the two standalone entry points. */
    stunrun_boot_wait_vsync(NULL, 0u, NULL);
    stunrun_boot_gsp_fill_page(NULL, 0x0054u);
    check(1);
}

int main(void)
{
    test_chain_order();
    test_clear_stages();
    test_ramp_values();
    test_fill_page();
    test_gate_arms();
    test_flag_clears();
    test_loop_with_real_rom_words();
    test_loop_never_set();
    test_loop_set_immediately();
    test_page_counter_modulus();
    test_back_arm();
    test_exit_arm();
    test_vsync();
    test_halt_inside_loop();
    test_robustness();

    if (g_failures == 0)
        printf("boot chain C port: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}