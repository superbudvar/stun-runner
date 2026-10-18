/* ROM-free self-check for the five ADSP-2100 liveness routines.
 *
 * WHAT IS ASSERTED, AND WHY THESE ARE THE RIGHT OBSERVABLES
 *
 * The 68010 side of the "ADSP-2100 DOES NOT RESPOND" verdict is nothing but
 * an ordered sequence of accesses to the ADSP windows plus one return value:
 * the caller at $5224/$5232/$5240 does `jsr`, `cmpi.w #$5555,D7`, `bne` and
 * nothing else. So the observables are exactly (a) the ordered list of board
 * accesses, and (b) D7's low word. Nothing else a slice test could reach is
 * on the decision path, and nothing on the decision path is a side effect of
 * this project's own C -- it is all the ROM's.
 *
 * The expectations below are transcribed BY HAND from the ROM listing in
 * adsp_liveness.c, not captured from this implementation's output. Each is
 * assembled entry by entry in the order the listing performs it, and then
 * compared against the whole recorded log in one pass; the first divergence
 * is printed with both sides so a failure says which access went wrong.
 *
 * Three things the tests deliberately pin down because a value-only test
 * would miss them:
 *
 *  - The countdown count. `subq.l #1,D3` sits at the TOP of the poll loop and
 *    `beq` leaves immediately, so the loop body runs 6000 / 29999 times, NOT
 *    6000 / 30000. The tests count watchdog kicks and IRQ status reads
 *    exactly.
 *  - The IRQ status POLARITY. MAME's hd68k_adsp_irq_state_r
 *    (harddriv_m.cpp:825) returns 0xFFFD, and XORs bit 0 when
 *    m_adsp_irq_state is set -- so bit 0 is ZERO while the ADSP is
 *    interrupting. `bne` back to the loop therefore means "not interrupting,
 *    keep waiting", and falling through means "the DSP answered". A stub
 *    that inverted this would still return a plausible pass/fail and would
 *    be wrong.
 *  - The packer's gap byte. The 24-bit payload sits in bits 31:8 and bits
 *    23:16 are structurally always zero (`swap` then a word-width `lsl.w`),
 *    so the tests assert that byte by byte rather than only checking totals.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "adsp_liveness.h"

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

/* ---- recorded access log --------------------------------------------------- */

enum {
    OP_DM_W = 0,
    OP_DM_R,
    OP_CTRL_W,
    OP_WDOG,
    OP_PROG_W,
    OP_ROM8,
    OP_ROM16,
    OP_IRQ_R
};

typedef struct entry {
    uint8_t  op;
    uint32_t addr;
    uint32_t value;
} entry_t;

/* The biggest log any case produces is gate 3's timeout path: 1 + 18 + 10
 * entries either side of 2 * 29999 poll-loop accesses. */
#define LOG_CAP (2u * STUNRUN_ADSP_GATE_SCRATCH_POLLS + 256u)

static const char *op_name(unsigned op)
{
    switch (op) {
    case OP_DM_W:   return "DM.W";
    case OP_DM_R:   return "DM.R";
    case OP_CTRL_W: return "CTRL";
    case OP_WDOG:   return "WDOG";
    case OP_PROG_W: return "PROG.W";
    case OP_ROM8:   return "ROM8";
    case OP_ROM16:  return "ROM16";
    case OP_IRQ_R:  return "IRQ.R";
    default:        return "?";
    }
}

/* ---- recording stub board -------------------------------------------------- */

#define ROM_SPAN 0xAE80u

typedef struct stub {
    entry_t *log;
    size_t   cap;        /* entries actually stored (may be < limit)  */
    size_t   limit;      /* store at most this many                   */
    size_t   total;      /* count every access, even past the limit    */
    uint8_t  rom[ROM_SPAN];
    uint16_t dm[STUNRUN_ADSP_DM_LEN];
    int      loopback;   /* the DSP copies DM[6] -> DM[$0A]            */
    int      fill;       /* the DSP fills the six scratch words       */
    int      irq_after;  /* polls with bit0 SET before it goes clear;
                          * -1 = never, 0 = clear from the first poll  */
    long     irq_polls;
} stub_t;

static void log_add(stub_t *s, unsigned op, uint32_t addr, uint32_t value)
{
    s->total++;
    if (s->cap < s->limit) {
        s->log[s->cap].op = (uint8_t)op;
        s->log[s->cap].addr = addr;
        s->log[s->cap].value = value;
        s->cap++;
    }
}

static uint8_t sb_read_rom8(void *ctx, uint32_t addr)
{
    stub_t *s = (stub_t *)ctx;
    uint32_t v = 0u;

    if (addr < ROM_SPAN)
        v = s->rom[addr];
    log_add(s, OP_ROM8, addr, v);
    return (uint8_t)v;
}

static uint16_t sb_read_rom16(void *ctx, uint32_t addr)
{
    stub_t *s = (stub_t *)ctx;
    uint32_t v = 0u;

    if (addr + 1u < ROM_SPAN)
        v = ((uint32_t)s->rom[addr] << 8) | s->rom[addr + 1u];
    log_add(s, OP_ROM16, addr, v);
    return (uint16_t)v;
}

static uint16_t sb_read_adsp_dm(void *ctx, uint16_t off)
{
    stub_t *s = (stub_t *)ctx;
    uint32_t v;

    if (s->loopback && off == STUNRUN_DM_READBACK)
        v = s->dm[STUNRUN_DM_TEST_WORD];   /* the DSP did the copy */
    else
        v = s->dm[off];
    log_add(s, OP_DM_R, off, v);
    return (uint16_t)v;
}

static uint16_t sb_read_adsp_irq(void *ctx)
{
    stub_t *s = (stub_t *)ctx;
    /* hd68k_adsp_irq_state_r (harddriv_m.cpp:825): result = 0xfffd, then
     * `if (m_adsp_irq_state) result ^= 1`. So bit 0 is CLEAR -- 0xfffc --
     * while the ADSP is interrupting, and SET -- 0xfffd -- when it is not.
     * `bne $b8ec` therefore keeps waiting while the ADSP is NOT interrupting,
     * and falling through is the DSP answering. Only the low bit is asserted
     * on, so the stand-ins are 0x0000 and 0x0001. */
    long n = s->irq_polls++;

    if (s->irq_after >= 0 && n >= s->irq_after) {
        log_add(s, OP_IRQ_R, 0, 0x0000u);  /* interrupting: bit 0 clear */
        return 0x0000u;
    }
    log_add(s, OP_IRQ_R, 0, 0x0001u);      /* idle: bit 0 set */
    return 0x0001u;
}

static void sb_write_adsp_dm(void *ctx, uint16_t off, uint16_t value)
{
    stub_t *s = (stub_t *)ctx;

    /* Log what the 68010 WROTE, then apply the DSP's side effect separately,
     * so the log stays a record of the 68010's access sequence and the
     * stub's model of the DSP does not contaminate it. */
    log_add(s, OP_DM_W, off, value);

    if (s->fill && off >= STUNRUN_DM_SCRATCH_0
        && off < STUNRUN_DM_SCRATCH_0 + 2u * STUNRUN_DM_SCRATCH_N)
        s->dm[off] = (uint16_t)(0xC0DEu + (off - STUNRUN_DM_SCRATCH_0) / 2u);
    else
        s->dm[off] = value;
}

static void sb_write_adsp_ctrl(void *ctx, uint16_t off, uint16_t value)
{
    log_add((stub_t *)ctx, OP_CTRL_W, off, value);
}

static void sb_write_adsp_prog(void *ctx, uint32_t off, uint32_t value)
{
    log_add((stub_t *)ctx, OP_PROG_W, off, value);
}

static void sb_write_watchdog(void *ctx)
{
    log_add((stub_t *)ctx, OP_WDOG, 0, 0);
}

/* The board members these five routines never touch are left NULL on purpose:
 * a NULL call is a loud crash, not a silent no-op, so an unexpected access
 * cannot pass unnoticed. */
static stunrun_board_t stub_board(stub_t *s)
{
    stunrun_board_t b;

    memset(&b, 0, sizeof(b));
    b.ctx = s;
    b.read_rom8 = sb_read_rom8;
    b.read_rom16 = sb_read_rom16;
    b.read_adsp_dm = sb_read_adsp_dm;
    b.read_adsp_irq = sb_read_adsp_irq;
    b.write_adsp_dm = sb_write_adsp_dm;
    b.write_adsp_ctrl = sb_write_adsp_ctrl;
    b.write_adsp_prog = sb_write_adsp_prog;
    b.write_watchdog = sb_write_watchdog;
    return b;
}

/* ---- expectations, transcribed by hand from the ROM listing ---------------- */

/* Two 24-bit words. The packed values are written out as literals from the
 * $B236-$B242 derivation, not computed from the image bytes, so that the
 * comparison is against the disassembly and not against a restatement of
 * the implementation. Byte order: byte 0 -> bits 23:16, byte 1 -> bits 15:8,
 * byte 2 -> bits 7:0, bits 31:24 structurally zero. */
static const uint8_t kImage[6] = { 0x10u, 0x20u, 0x30u, 0x40u, 0x50u, 0x60u };
static const uint32_t kImageWords = 2u;
static const uint32_t kPacked[2] = { 0x00102030u, 0x00405060u };

#define ROM_IMAGE_HDR_ADDR STUNRUN_ADSP_IMAGE_HDR
#define ROM_IMAGE_FIRST     (ROM_IMAGE_HDR_ADDR + 2u)

static void want_ctrl(entry_t *e, size_t *n, uint32_t off)
{
    e[*n].op = OP_CTRL_W; e[*n].addr = off; e[*n].value = 0u; (*n)++;
}

static void want_dm_w(entry_t *e, size_t *n, uint32_t off, uint32_t v)
{
    e[*n].op = OP_DM_W; e[*n].addr = off; e[*n].value = v; (*n)++;
}

static void want_dm_r(entry_t *e, size_t *n, uint32_t off, uint32_t v)
{
    e[*n].op = OP_DM_R; e[*n].addr = off; e[*n].value = v; (*n)++;
}

static void want_wdog(entry_t *e, size_t *n)
{
    e[*n].op = OP_WDOG; e[*n].addr = 0u; e[*n].value = 0u; (*n)++;
}

static void want_irq(entry_t *e, size_t *n, uint32_t value)
{
    e[*n].op = OP_IRQ_R; e[*n].addr = 0u; e[*n].value = value; (*n)++;
}

static void want_rom8(entry_t *e, size_t *n, uint32_t addr, uint32_t v)
{
    e[*n].op = OP_ROM8; e[*n].addr = addr; e[*n].value = v; (*n)++;
}

static void want_rom16(entry_t *e, size_t *n, uint32_t addr, uint32_t v)
{
    e[*n].op = OP_ROM16; e[*n].addr = addr; e[*n].value = v; (*n)++;
}

static void want_prog(entry_t *e, size_t *n, uint32_t addr, uint32_t v)
{
    e[*n].op = OP_PROG_W; e[*n].addr = addr; e[*n].value = v; (*n)++;
}

/* $B228 alone: 9 accesses for a two-word image. */
static void want_download(entry_t *e, size_t *n)
{
    uint32_t i;

    want_rom16(e, n, ROM_IMAGE_HDR_ADDR, kImageWords);
    for (i = 0u; i < kImageWords; i++) {
        want_rom8(e, n, ROM_IMAGE_FIRST + 3u * i + 0u, kImage[3u * i + 0u]);
        want_rom8(e, n, ROM_IMAGE_FIRST + 3u * i + 1u, kImage[3u * i + 1u]);
        want_rom8(e, n, ROM_IMAGE_FIRST + 3u * i + 2u, kImage[3u * i + 2u]);
        want_prog(e, n, STUNRUN_ADSP_PROG_BASE + 2u * i, kPacked[i]);
    }
}

/* $B1E8: four clears, the download, two DM writes, three more clears. */
static void want_restart(entry_t *e, size_t *n)
{
    want_ctrl(e, n, 0x000Au);
    want_ctrl(e, n, 0x000Cu);
    want_ctrl(e, n, 0x000Eu);
    want_ctrl(e, n, STUNRUN_ADSP_CTRL_IRQCLR);
    want_download(e, n);
    want_dm_w(e, n, STUNRUN_DM_DATA_REG, 0x0000u);
    want_dm_w(e, n, STUNRUN_DM_ADDR_REG, 0x0000u);
    want_ctrl(e, n, 0x000Au);
    want_ctrl(e, n, 0x000Cu);
    want_ctrl(e, n, 0x000Eu);
}

/* $B8EC/$B966: one body iteration = watchdog kick, then status read. The ROM
 * order is `clr.w $608000` THEN `btst #$0,$838001`, and that order is part of
 * what is asserted. `status` is the bit-0 value every poll in this run sees;
 * on the timeout path the DSP never interrupts, so it is 1 throughout. */
static void want_polls(entry_t *e, size_t *n, uint32_t iterations,
                       uint32_t status)
{
    uint32_t i;

    for (i = 0u; i < iterations; i++) {
        want_wdog(e, n);
        want_irq(e, n, status);
    }
}

/* ---- log comparison -------------------------------------------------------- */

static int log_matches(const entry_t *want, size_t want_n,
                       const entry_t *got, size_t got_n,
                       const char *label)
{
    size_t i;
    size_t n = want_n < got_n ? want_n : got_n;

    if (want_n != got_n) {
        printf("FAIL %s: access count %u want %u\n", label,
               (unsigned)got_n, (unsigned)want_n);
        g_failures++;
        return 0;
    }
    for (i = 0u; i < n; i++) {
        if (got[i].op == want[i].op && got[i].addr == want[i].addr
            && got[i].value == want[i].value)
            continue;
        printf("FAIL %s: access %u differs -- got  %-6s addr=0x%08X val=0x%08X"
               " / want %-6s addr=0x%08X val=0x%08X\n",
               label, (unsigned)i,
               op_name(got[i].op), got[i].addr, got[i].value,
               op_name(want[i].op), want[i].addr, want[i].value);
        g_failures++;
        return 0;
    }
    printf("PASS %s: %u board accesses match the listing\n", label,
           (unsigned)got_n);
    return 1;
}

/* ---- the synthetic image --------------------------------------------------- */

static void stub_load_image(stub_t *s)
{
    size_t i;

    memset(s->rom, 0, sizeof(s->rom));
    s->rom[ROM_IMAGE_HDR_ADDR] = (uint8_t)(kImageWords >> 8);
    s->rom[ROM_IMAGE_HDR_ADDR + 1u] = (uint8_t)(kImageWords & 0xFFu);
    for (i = 0u; i < sizeof(kImage); i++)
        s->rom[ROM_IMAGE_FIRST + i] = kImage[i];
}

/* ---- cases ----------------------------------------------------------------- */

static void test_constants(void)
{
    /* Every routine ends on its own `rts`, and the next entry begins at the
     * next byte: the transcribed ranges are contiguous and ordered. */
    check(STUNRUN_ADSP_RESTART_ADDR == 0x0000B1E8u);
    check(STUNRUN_ADSP_RESTART_END == STUNRUN_ADSP_DOWNLOAD_ADDR);
    check(STUNRUN_ADSP_DOWNLOAD_END == 0x0000B24Au);
    check(STUNRUN_ADSP_GATE_LOOPBACK_ADDR == 0x0000B864u);
    check(STUNRUN_ADSP_GATE_LOOPBACK_END == STUNRUN_ADSP_GATE_IRQ_ADDR);
    check(STUNRUN_ADSP_GATE_IRQ_ADDR == 0x0000B8B4u);
    check(STUNRUN_ADSP_GATE_IRQ_END == STUNRUN_ADSP_GATE_SCRATCH_ADDR);
    check(STUNRUN_ADSP_GATE_SCRATCH_ADDR == 0x0000B916u);

    /* The pass word all three gates return, and the word the service test
     * compares against at $522a/$5238/$5246. */
    check(STUNRUN_ADSP_PASS_WORD == 0x5555u);

    /* The countdown immediates and the derived body counts. */
    check(STUNRUN_ADSP_GATE_IRQ_LIMIT == 0x00001770u);
    check(STUNRUN_ADSP_GATE_SCRATCH_LIMIT == 0x00007530u);
    check(STUNRUN_ADSP_GATE_IRQ_POLLS == 5999u);
    check(STUNRUN_ADSP_GATE_SCRATCH_POLLS == 29999u);
    check(STUNRUN_ADSP_GATE_IRQ_POLLS
          == (unsigned)(STUNRUN_ADSP_GATE_IRQ_LIMIT - 1u));
    check(STUNRUN_ADSP_GATE_SCRATCH_POLLS
          == (unsigned)(STUNRUN_ADSP_GATE_SCRATCH_LIMIT - 1u));

    /* The real ROM's header says 295 words, so the image ends at $B1E6 and A0 is
     * left on the single pad byte at $B1E7, immediately before the restart
     * routine. This needs no ROM file. */
    check(STUNRUN_ADSP_IMAGE_WORDS == 295u);
    check(ROM_IMAGE_HDR_ADDR + 2u + 3u * STUNRUN_ADSP_IMAGE_WORDS
          == STUNRUN_ADSP_IMAGE_END);
    check(STUNRUN_ADSP_IMAGE_END - 1u == STUNRUN_ADSP_IMAGE_LAST);
    check(STUNRUN_ADSP_IMAGE_END + 1u == STUNRUN_ADSP_RESTART_ADDR);
}

static void test_download_packing(void)
{
    /* A second, hand-picked image whose packed words are written out from
     * the derivation rather than from kImage. Bytes chosen so that every one
     * of the three lands in a different part of the longword, including the
     * cases that distinguish the two readings of the packer: b0 = 0x00 and
     * b0 = 0xFF. */
    static const uint8_t img[12] = {
        0x11u, 0x22u, 0x33u,
        0xAAu, 0xBBu, 0xCCu,
        0x00u, 0x7Fu, 0x01u,
        0xFFu, 0xEEu, 0xDDu
    };
    static const uint32_t packed[4] = {
        0x00112233u, 0x00AABBCCu, 0x00007F01u, 0x00FFEEDDu
    };
    entry_t want[64];
    entry_t *got;
    stub_t *s;
    stunrun_board_t b;
    stunrun_adsp_regs_t r;
    size_t n = 0u;
    size_t i;

    got = (entry_t *)malloc(sizeof(entry_t) * LOG_CAP);
    if (got == NULL) {
        printf("FAIL test_download_packing: out of memory\n");
        g_failures++;
        return;
    }
    s = (stub_t *)malloc(sizeof(*s));
    if (s == NULL) {
        printf("FAIL test_download_packing: out of memory\n");
        free(got);
        g_failures++;
        return;
    }
    memset(s, 0, sizeof(*s));
    s->log = got;
    s->limit = LOG_CAP;
    s->irq_after = -1;

    s->rom[0xAE70] = 0x00u;
    s->rom[0xAE71] = 0x04u;                 /* four words */
    for (i = 0u; i < sizeof(img); i++)
        s->rom[0xAE72u + i] = img[i];

    b = stub_board(s);
    memset(&r, 0, sizeof(r));
    r.d0 = 0xDEADBEEFu; r.a0 = 0x11111111u; /* prove they are overwritten */
    r.a1 = 0x22222222u;

    /* $B228: the header word, then 3 ROM reads and 1 program write per word. */
    want_rom16(want, &n, 0xAE70u, 4u);
    for (i = 0u; i < 4u; i++) {
        want_rom8(want, &n, 0xAE72u + 3u * i + 0u, img[3u * i + 0u]);
        want_rom8(want, &n, 0xAE72u + 3u * i + 1u, img[3u * i + 1u]);
        want_rom8(want, &n, 0xAE72u + 3u * i + 2u, img[3u * i + 2u]);
        want_prog(want, &n, STUNRUN_ADSP_PROG_BASE + 2u * i, packed[i]);
    }

    stunrun_adsp_program_download(&b, &r);
    log_matches(want, n, got, s->cap, "download 24-bit packing");

    /* A0 lands on the first byte past the image; A1 has advanced two bytes
     * per word. */
    check(r.a0 == 0xAE72u + 12u);
    check(r.a1 == STUNRUN_ADSP_PROG_BASE + 8u);
    /* D6 counts down to zero in its low half only. */
    check((r.d6 & 0xFFFFu) == 0u);

    /* Byte order, asserted position by position rather than as a total: byte 0
     * is bits 23:16, byte 1 is bits 15:8, byte 2 is bits 7:0, and bits 31:24
     * are structurally always zero because the word-width `lsl.w` at $B23E
     * cannot reach them. The b0 = 0x00 and b0 = 0xFF words below exist to
     * catch the opposite reading of the packer (byte 0 into bits 31:24), which
     * would turn every one of the ROM's 295 instructions into an ADSP NOP. */
    check((packed[0] & 0xFF000000u) == 0x00000000u);
    check((packed[0] & 0x00FF0000u) == 0x00110000u);
    check((packed[0] & 0x0000FF00u) == 0x00002200u);
    check((packed[0] & 0x000000FFu) == 0x00000033u);
    check(got[4].value == packed[0] && got[4].addr == STUNRUN_ADSP_PROG_BASE);
    check((got[4].value & 0xFF000000u) == 0u);
    /* The same, straight off the recorded log, for the last word -- whose
     * first byte is 0xFF, so it also pins the sign/zero behaviour. */
    check(got[n - 1u].value == packed[3]);
    check((got[n - 1u].value & 0xFF000000u) == 0u);
    check((got[n - 1u].value & 0x00FF0000u) == 0x00FF0000u);
    /* And the word whose first byte is zero is NOT all-zero, which the
     * opposite reading would also have got wrong. */
    check(got[12].value == packed[2]);
    check(got[12].value != 0x00000000u);

    free(s);
    free(got);
}

static void test_download_zero_count(void)
{
    /* `bne` sits AFTER the body, so a header count of zero does not skip the
     * loop: D6's low half wraps 0 -> $FFFF and the body runs 65536 times.
     * Counting only (the log is capped) so this stays cheap. */
    entry_t *got;
    stub_t *s;
    stunrun_board_t b;
    stunrun_adsp_regs_t r;

    got = (entry_t *)malloc(sizeof(entry_t) * 16u);
    if (got == NULL || (s = (stub_t *)malloc(sizeof(*s))) == NULL) {
        printf("FAIL test_download_zero_count: out of memory\n");
        free(got);
        g_failures++;
        return;
    }
    memset(s, 0, sizeof(*s));
    s->log = got;
    s->limit = 16u;
    s->rom[0xAE70] = 0x00u;
    s->rom[0xAE71] = 0x00u;                 /* zero words */

    b = stub_board(s);
    memset(&r, 0, sizeof(r));
    stunrun_adsp_program_download(&b, &r);

    check(s->total == 1u + 65536u * 4u);
    check(s->cap == 16u);                   /* the cap held, nothing lost */
    check((r.d6 & 0xFFFFu) == 0u);          /* it wrapped all the way round */
    check(r.a1 == STUNRUN_ADSP_PROG_BASE + 2u * 65536u);

    free(s);
    free(got);
}

static void test_restart(void)
{
    entry_t want[64];
    entry_t *got;
    stub_t *s;
    stunrun_board_t b;
    stunrun_adsp_regs_t r;
    size_t n = 0u;

    got = (entry_t *)malloc(sizeof(entry_t) * LOG_CAP);
    s = (stub_t *)malloc(sizeof(*s));
    if (got == NULL || s == NULL) {
        printf("FAIL test_restart: out of memory\n");
        free(got); free(s);
        g_failures++;
        return;
    }
    memset(s, 0, sizeof(*s));
    s->log = got;
    s->limit = LOG_CAP;
    s->irq_after = -1;
    stub_load_image(s);

    b = stub_board(s);
    memset(&r, 0, sizeof(r));
    want_restart(want, &n);
    check(n == 18u);

    stunrun_adsp_restart(&b, &r);
    log_matches(want, n, got, s->cap, "restart $B1E8");
    /* $B1E8 writes no register of its own; D6/A0/A1 are $B228's residue. */
    check(r.a1 == STUNRUN_ADSP_PROG_BASE + 2u * kImageWords);
    check(r.a0 == ROM_IMAGE_FIRST + 3u * kImageWords);

    free(s);
    free(got);
}

static void test_gate_loopback(void)
{
    entry_t want[64];
    entry_t *got;
    stub_t *s;
    stunrun_board_t b;
    stunrun_adsp_regs_t r;
    size_t n = 0u;
    uint16_t d7;

    got = (entry_t *)malloc(sizeof(entry_t) * LOG_CAP);
    s = (stub_t *)malloc(sizeof(*s));
    if (got == NULL || s == NULL) {
        printf("FAIL test_gate_loopback: out of memory\n");
        free(got); free(s);
        g_failures++;
        return;
    }

    /* --- pass: the DSP copied DM[6] into DM[$0A]. --- */
    memset(s, 0, sizeof(*s));
    s->log = got;
    s->limit = LOG_CAP;
    s->irq_after = -1;
    s->loopback = 1;
    stub_load_image(s);
    b = stub_board(s);
    memset(&r, 0, sizeof(r));

    want_restart(want, &n);
    want_ctrl(want, &n, 0x000Au);                            /* $B868 */
    want_dm_w(want, &n, STUNRUN_DM_DATA_REG, 0x0000u);        /* $B86E */
    want_dm_w(want, &n, STUNRUN_DM_ADDR_REG, 0x0002u);        /* $B876 */
    want_dm_w(want, &n, STUNRUN_DM_TEST_WORD, 0x0000u);       /* $B87E */
    want_dm_w(want, &n, STUNRUN_DM_TEST_WORD,
              STUNRUN_ADSP_LOOPBACK_WORD);                    /* $B888 */
    want_ctrl(want, &n, 0x000Au);                            /* $B88E */
    want_ctrl(want, &n, 0x000Au);                            /* $B89A */
    want_dm_r(want, &n, STUNRUN_DM_READBACK,
              STUNRUN_ADSP_LOOPBACK_WORD);                    /* $B8A0 */
    check(n == 26u);

    d7 = stunrun_adsp_gate_loopback(&b, &r);
    log_matches(want, n, got, s->cap, "gate 1 loopback, pass");
    check(d7 == STUNRUN_ADSP_PASS_WORD);
    check((uint16_t)r.d7 == 0x5555u);
    /* The probe word is a `move.w`, so D3 keeps whatever high half it had. */
    check((r.d3 & 0xFFFFu) == STUNRUN_ADSP_LOOPBACK_WORD);
    check((uint16_t)r.d0 == STUNRUN_ADSP_LOOPBACK_WORD);

    /* --- fail: DM[6] holds $1234 but nothing copied it. --- */
    memset(s, 0, sizeof(*s));
    s->log = got;
    s->limit = LOG_CAP;
    s->irq_after = -1;
    s->loopback = 0;
    stub_load_image(s);
    b = stub_board(s);
    memset(&r, 0, sizeof(r));
    /* A non-zero HIGH half in D7, so `clr.w D7` at $B8A6 has to be seen to
     * clear only the low half. */
    r.d7 = 0xAAAA5555u;

    /* Same access sequence as the pass path; the only difference is the value
     * the readback returns, which is exactly the thing being judged. */
    want[n - 1u].value = 0x0000u;

    d7 = stunrun_adsp_gate_loopback(&b, &r);
    check(d7 == 0x0000u);
    check((r.d7 & 0xFFFFu) == 0x0000u);
    check((r.d7 & 0xFFFF0000u) == 0xAAAA0000u);
    /* The write to DM[6] still happened -- the gate failed on the READBACK,
     * not because the probe was never sent. */
    check(s->dm[STUNRUN_DM_TEST_WORD] == STUNRUN_ADSP_LOOPBACK_WORD);
    check(s->dm[STUNRUN_DM_READBACK] == 0x0000u);
    log_matches(want, n, got, s->cap, "gate 1 loopback, fail");

    free(s);
    free(got);
}

static void test_gate_irq_timeout(void)
{
    entry_t want[LOG_CAP];
    entry_t *got;
    stub_t *s;
    stunrun_board_t b;
    stunrun_adsp_regs_t r;
    size_t n = 0u;
    uint16_t d7;

    got = (entry_t *)malloc(sizeof(entry_t) * LOG_CAP);
    s = (stub_t *)malloc(sizeof(*s));
    if (got == NULL || s == NULL) {
        printf("FAIL test_gate_irq_timeout: out of memory\n");
        free(got); free(s);
        g_failures++;
        return;
    }
    memset(s, 0, sizeof(*s));
    s->log = got;
    s->limit = LOG_CAP;
    s->irq_after = -1;                    /* never interrupts */
    stub_load_image(s);
    b = stub_board(s);
    memset(&r, 0, sizeof(r));
    r.sr = 0x00002000u;                   /* $B8B4 must put this back */

    want_ctrl(want, &n, STUNRUN_ADSP_CTRL_IRQCLR);                      /* $B8BA */
    want_restart(want, &n);
    want_ctrl(want, &n, 0x000Au);                             /* $B8C4 */
    want_dm_w(want, &n, STUNRUN_DM_DATA_REG, 0x0006u);         /* $B8CA */
    want_dm_w(want, &n, STUNRUN_DM_ADDR_REG, 0x0002u);         /* $B8D2 */
    want_dm_w(want, &n, STUNRUN_DM_SCRATCH_0, 0x0000u);        /* $B8DA */
    want_ctrl(want, &n, 0x000Au);                             /* $B8E0 */
    want_polls(want, &n, STUNRUN_ADSP_GATE_IRQ_POLLS, 1u);
    want_ctrl(want, &n, STUNRUN_ADSP_CTRL_IRQCLR);             /* $B90C */
    check(n == 12023u);

    d7 = stunrun_adsp_gate_irq(&b, &r);
    log_matches(want, n, got, s->cap, "gate 2 IRQ, timeout");

    check(d7 == 0x0000u);
    /* 6000 watchdog kicks and 6000 status reads -- NOT 6000. */
    check(s->irq_polls == (long)STUNRUN_ADSP_GATE_IRQ_POLLS);
    check(r.d3 == 0u);                          /* the countdown ended at zero */
    /* SR is masked for the duration and restored before the rts. */
    check(r.sr == 0x00002000u);

    free(s);
    free(got);
}

static void test_gate_irq_pass(void)
{
    entry_t want[64];
    entry_t *got;
    stub_t *s;
    stunrun_board_t b;
    stunrun_adsp_regs_t r;
    size_t n = 0u;
    uint16_t d7;

    got = (entry_t *)malloc(sizeof(entry_t) * LOG_CAP);
    s = (stub_t *)malloc(sizeof(*s));
    if (got == NULL || s == NULL) {
        printf("FAIL test_gate_irq_pass: out of memory\n");
        free(got); free(s);
        g_failures++;
        return;
    }
    memset(s, 0, sizeof(*s));
    s->log = got;
    s->limit = LOG_CAP;
    s->irq_after = 0;                     /* interrupting from the first poll */
    stub_load_image(s);
    b = stub_board(s);
    memset(&r, 0, sizeof(r));
    r.sr = 0x00002000u;

    want_ctrl(want, &n, STUNRUN_ADSP_CTRL_IRQCLR);                      /* $B8BA */
    want_restart(want, &n);
    want_ctrl(want, &n, 0x000Au);
    want_dm_w(want, &n, STUNRUN_DM_DATA_REG, 0x0006u);
    want_dm_w(want, &n, STUNRUN_DM_ADDR_REG, 0x0002u);
    want_dm_w(want, &n, STUNRUN_DM_SCRATCH_0, 0x0000u);
    want_ctrl(want, &n, 0x000Au);
    want_polls(want, &n, 1u, 0u);             /* exactly one body iteration */
    want_ctrl(want, &n, STUNRUN_ADSP_CTRL_IRQCLR);
    check(n == 27u);

    d7 = stunrun_adsp_gate_irq(&b, &r);
    log_matches(want, n, got, s->cap, "gate 2 IRQ, immediate");
    check(d7 == STUNRUN_ADSP_PASS_WORD);
    check((uint16_t)r.d7 == 0x5555u);
    check(s->irq_polls == 1);
    /* The loop decremented once before polling, so D3 is left at limit-1.
     * On the timeout path D3 was zero -- the two are distinguishable from
     * the register file alone. */
    check(r.d3 == STUNRUN_ADSP_GATE_IRQ_LIMIT - 1u);
    check(r.sr == 0x00002000u);

    free(s);
    free(got);
}

static void test_gate_irq_polarity(void)
{
    /* The loop waits while bit 0 is SET and falls through when it is CLEAR,
     * because MAME clears bit 0 of the status word while the ADSP is
     * interrupting. A stub that inverts this would still produce a plausible
     * pass/fail; assert both directions explicitly. */
    entry_t want[64];
    entry_t *got;
    stub_t *s;
    stunrun_board_t b;
    stunrun_adsp_regs_t r;
    size_t n = 0u;
    int delay;

    got = (entry_t *)malloc(sizeof(entry_t) * LOG_CAP);
    s = (stub_t *)malloc(sizeof(*s));
    if (got == NULL || s == NULL) {
        printf("FAIL test_gate_irq_polarity: out of memory\n");
        free(got); free(s);
        g_failures++;
        return;
    }

    for (delay = 0; delay <= 3; delay++) {
        memset(s, 0, sizeof(*s));
        s->log = got;
        s->limit = LOG_CAP;
        s->irq_after = delay;              /* `delay` polls, then interrupt */
        stub_load_image(s);
        b = stub_board(s);
        memset(&r, 0, sizeof(r));

        n = 0u;
        want_ctrl(want, &n, STUNRUN_ADSP_CTRL_IRQCLR);
        want_restart(want, &n);
        want_ctrl(want, &n, 0x000Au);
        want_dm_w(want, &n, STUNRUN_DM_DATA_REG, 0x0006u);
        want_dm_w(want, &n, STUNRUN_DM_ADDR_REG, 0x0002u);
        want_dm_w(want, &n, STUNRUN_DM_SCRATCH_0, 0x0000u);
        want_ctrl(want, &n, 0x000Au);
        /* `delay` iterations see bit 0 SET (idle) and loop again; the one
         * after them sees it CLEAR, which is what ends the wait. */
        want_polls(want, &n, (uint32_t)delay, 1u);
        want_wdog(want, &n);
        want_irq(want, &n, 0u);
        want_ctrl(want, &n, STUNRUN_ADSP_CTRL_IRQCLR);

        check(stunrun_adsp_gate_irq(&b, &r) == STUNRUN_ADSP_PASS_WORD);
        log_matches(want, n, got, s->cap, "gate 2 IRQ polarity");
        check(s->irq_polls == (long)delay + 1);
    }

    free(s);
    free(got);
}

static void test_gate_scratch_timeout(void)
{
    entry_t want[LOG_CAP];
    entry_t *got;
    stub_t *s;
    stunrun_board_t b;
    stunrun_adsp_regs_t r;
    size_t n = 0u;
    uint16_t d7;

    got = (entry_t *)malloc(sizeof(entry_t) * LOG_CAP);
    s = (stub_t *)malloc(sizeof(*s));
    if (got == NULL || s == NULL) {
        printf("FAIL test_gate_scratch_timeout: out of memory\n");
        free(got); free(s);
        g_failures++;
        return;
    }
    memset(s, 0, sizeof(*s));
    s->log = got;
    s->limit = LOG_CAP;
    s->irq_after = -1;
    stub_load_image(s);
    b = stub_board(s);
    memset(&r, 0, sizeof(r));
    r.sr = 0x00002700u;   /* $B916 does NOT touch SR at all */

    want_ctrl(want, &n, STUNRUN_ADSP_CTRL_IRQCLR);                  /* $B916 */
    want_restart(want, &n);
    want_ctrl(want, &n, 0x000Au);                                   /* $B920 */
    want_dm_w(want, &n, STUNRUN_DM_DATA_REG, STUNRUN_DM_READBACK);  /* $B926 */
    want_dm_w(want, &n, STUNRUN_DM_ADDR_REG, 0x0002u);             /* $B92E */
    want_dm_w(want, &n, STUNRUN_DM_SCRATCH_0 + 0u,  0x0000u);       /* $B936 */
    want_dm_w(want, &n, STUNRUN_DM_SCRATCH_0 + 2u,  0x0000u);       /* $B93C */
    want_dm_w(want, &n, STUNRUN_DM_SCRATCH_0 + 4u,  0x0000u);       /* $B942 */
    want_dm_w(want, &n, STUNRUN_DM_SCRATCH_0 + 6u,  0x0000u);       /* $B948 */
    want_dm_w(want, &n, STUNRUN_DM_SCRATCH_0 + 8u,  0x0000u);       /* $B94E */
    want_dm_w(want, &n, STUNRUN_DM_SCRATCH_0 + 10u, 0x0000u);       /* $B954 */
    want_ctrl(want, &n, 0x000Au);                                   /* $B95A */
    want_polls(want, &n, STUNRUN_ADSP_GATE_SCRATCH_POLLS, 1u);
    check(n == 60027u);

    d7 = stunrun_adsp_gate_scratch(&b, &r);
    log_matches(want, n, got, s->cap, "gate 3 scratch, timeout");
    check(d7 == 0x0000u);
    check(s->irq_polls == (long)STUNRUN_ADSP_GATE_SCRATCH_POLLS);
    check(r.d3 == 0u);
    check(r.sr == 0x00002700u);

    free(s);
    free(got);
}

static void test_gate_scratch_pass(void)
{
    entry_t want[64];
    entry_t *got;
    stub_t *s;
    stunrun_board_t b;
    stunrun_adsp_regs_t r;
    size_t n = 0u;
    uint16_t d7;

    got = (entry_t *)malloc(sizeof(entry_t) * LOG_CAP);
    s = (stub_t *)malloc(sizeof(*s));
    if (got == NULL || s == NULL) {
        printf("FAIL test_gate_scratch_pass: out of memory\n");
        free(got); free(s);
        g_failures++;
        return;
    }
    memset(s, 0, sizeof(*s));
    s->log = got;
    s->limit = LOG_CAP;
    s->irq_after = 0;
    s->fill = 1;                 /* the DSP fills the six scratch words */
    stub_load_image(s);
    b = stub_board(s);
    memset(&r, 0, sizeof(r));

    want_ctrl(want, &n, STUNRUN_ADSP_CTRL_IRQCLR);
    want_restart(want, &n);
    want_ctrl(want, &n, 0x000Au);
    want_dm_w(want, &n, STUNRUN_DM_DATA_REG, STUNRUN_DM_READBACK);
    want_dm_w(want, &n, STUNRUN_DM_ADDR_REG, 0x0002u);
    want_dm_w(want, &n, STUNRUN_DM_SCRATCH_0 + 0u,  0x0000u);
    want_dm_w(want, &n, STUNRUN_DM_SCRATCH_0 + 2u,  0x0000u);
    want_dm_w(want, &n, STUNRUN_DM_SCRATCH_0 + 4u,  0x0000u);
    want_dm_w(want, &n, STUNRUN_DM_SCRATCH_0 + 6u,  0x0000u);
    want_dm_w(want, &n, STUNRUN_DM_SCRATCH_0 + 8u,  0x0000u);
    want_dm_w(want, &n, STUNRUN_DM_SCRATCH_0 + 10u, 0x0000u);
    want_ctrl(want, &n, 0x000Au);
    want_polls(want, &n, 1u, 0u);
    want_ctrl(want, &n, 0x000Au);                                  /* $B97C */
    want_dm_r(want, &n, STUNRUN_DM_SCRATCH_0 + 0u,  0xC0DEu);       /* $B982 */
    want_dm_r(want, &n, STUNRUN_DM_SCRATCH_0 + 2u,  0xC0DFu);       /* $B988 */
    want_dm_r(want, &n, STUNRUN_DM_SCRATCH_0 + 4u,  0xC0E0u);       /* $B98E */
    want_dm_r(want, &n, STUNRUN_DM_SCRATCH_0 + 6u,  0xC0E1u);       /* $B994 */
    want_dm_r(want, &n, STUNRUN_DM_SCRATCH_0 + 8u,  0xC0E2u);       /* $B99A */
    want_dm_r(want, &n, STUNRUN_DM_SCRATCH_0 + 10u, 0xC0E3u);       /* $B9A0 */
    check(n == 38u);
    /* Unlike $B8B4, $B916 does NOT clear $818060 on the way out: the last
     * control write in the sequence is $B97C. There is deliberately no
     * trailing want_ctrl(IRQCLR) here. */

    d7 = stunrun_adsp_gate_scratch(&b, &r);
    log_matches(want, n, got, s->cap, "gate 3 scratch, immediate");

    /* $B9A6 sets D7 to $5555 with NO comparison at all -- the ROM reads the
     * six words and passes regardless of their values. Assert that, so a
     * future "fix" that compares them fails here instead of silently
     * agreeing with the ROM. */
    check(d7 == STUNRUN_ADSP_PASS_WORD);
    check((uint16_t)r.d0 == 0xC0DEu);
    check((uint16_t)r.d1 == 0xC0DFu);
    check((uint16_t)r.d2 == 0xC0E0u);
    /* The countdown register is destroyed by the fourth readback. */
    check((r.d3 & 0xFFFFu) == 0xC0E1u);
    check(r.d3 != (STUNRUN_ADSP_GATE_SCRATCH_LIMIT - 1u));
    check((uint16_t)r.d4 == 0xC0E2u);
    check((uint16_t)r.d5 == 0xC0E3u);

    /* Same again with the DSP silent: still passes. */
    memset(s, 0, sizeof(*s));
    s->log = got;
    s->limit = LOG_CAP;
    s->irq_after = 0;
    stub_load_image(s);
    b = stub_board(s);
    memset(&r, 0, sizeof(r));
    check(stunrun_adsp_gate_scratch(&b, &r) == STUNRUN_ADSP_PASS_WORD);
    check((uint16_t)r.d0 == 0x0000u);
    check((uint16_t)r.d4 == 0x0000u);

    free(s);
    free(got);
}

static void test_null_safety(void)
{
    stub_t s;
    stunrun_board_t b;
    stunrun_adsp_regs_t r;

    memset(&s, 0, sizeof(s));
    s.irq_after = -1;
    stub_load_image(&s);
    b = stub_board(&s);
    memset(&r, 0, sizeof(r));

    /* Documented behaviour, not an accident: a NULL board or register file
     * is a no-op / a zero verdict, and it must not touch the board. */
    stunrun_adsp_program_download(NULL, &r);
    stunrun_adsp_restart(NULL, &r);
    check(stunrun_adsp_gate_loopback(NULL, &r) == 0u);
    check(stunrun_adsp_gate_irq(NULL, &r) == 0u);
    check(stunrun_adsp_gate_scratch(NULL, &r) == 0u);
    stunrun_adsp_program_download(&b, NULL);
    stunrun_adsp_restart(&b, NULL);
    check(stunrun_adsp_gate_loopback(&b, NULL) == 0u);
    check(stunrun_adsp_gate_irq(&b, NULL) == 0u);
    check(stunrun_adsp_gate_scratch(&b, NULL) == 0u);
    check(s.total == 0u);
}

int main(void)
{
    test_constants();
    test_download_packing();
    test_download_zero_count();
    test_restart();
    test_gate_loopback();
    test_gate_irq_timeout();
    test_gate_irq_pass();
    test_gate_irq_polarity();
    test_gate_scratch_timeout();
    test_gate_scratch_pass();
    test_null_safety();

    if (g_failures == 0)
        printf("adsp liveness C port: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}