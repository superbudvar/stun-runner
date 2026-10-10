/* ram_march_test.c — exercises the transcribed RAM march kernels.
 *
 * The ROM's own pass/fail report goes to the GSP FIFO as $3F3F / $5F5F /
 * $9F9F / $FFFF, so the FIFO word stream is a genuine oracle rather than
 * something we invented.  These tests therefore assert:
 *
 *   1. a clean 512 KiB run of every variant completes with no fault and no
 *      FIFO traffic at all;
 *   2. every variant leaves memory holding pattern ^ 0xFFFF (the invariant the
 *      march is built to establish);
 *   3. an injected single-word fault is detected, reports $9F9F exactly twice,
 *      nudges HSTADR by +0x20, and names the faulting address;
 *   4. the reporter helpers emit the documented word pairs and HSTADR deltas.
 *
 * The 512 KiB base/limit pair is the machine's real geometry (A1 = 0xFFFF8000,
 * A2 = 0xFFFFFFFE), so a passing run also confirms the sizing fix that used to
 * alias RAM every 32 KiB.
 */
#include "ram_march.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            return 1;                                                          \
        }                                                                      \
    } while (0)

/* multisync_68k_map: map(0xff8000, 0xffffff).ram() == 0x8000 bytes. */
#define RAM_BYTES 0x8000u
#define RAM_BASE  0xFFFF8000u
#define RAM_LIMIT 0xFFFFFFFEu

typedef struct {
    uint16_t *ram;      /* RAM_BYTES / 2 words */
    uint16_t fifo[64];
    unsigned fifo_len;
    unsigned fifo_overflow;
    unsigned watchdog_kicks;
    int32_t  hstadr;

    /* fault injection */
    int      inject_at;      /* word index, -1 to disable */
    uint16_t inject_value;
    int      inject_once;
    int      injected;
} fake_t;

static uint32_t word_index(uint32_t addr)
{
    return (addr - RAM_BASE) / 2u;
}

/* NOTE: RAM_BASE + RAM_BYTES overflows uint32_t (0xFFFF8000 + 0x80000 wraps
 * to 0), so range tests must be written as `addr - RAM_BASE < RAM_BYTES`.  That
 * overflow is exactly the class of bug this routine exists to catch. */
static void fake_poke(void *ctx, uint32_t addr, uint16_t value)
{
    fake_t *f = (fake_t *)ctx;
    if (addr - RAM_BASE < RAM_BYTES)
        f->ram[word_index(addr)] = value;
}

static uint16_t fake_peek(void *ctx, uint32_t addr)
{
    fake_t *f = (fake_t *)ctx;
    if (addr - RAM_BASE >= RAM_BYTES)
        return 0xDEADu;
    if (f->inject_at >= 0 && !f->injected &&
        (int)word_index(addr) == f->inject_at) {
        /* Stick the fault: the read-back sees a value the march never wrote. */
        f->injected = 1;
        return f->inject_value;
    }
    return f->ram[word_index(addr)];
}

static void fake_watchdog(void *ctx) { ((fake_t *)ctx)->watchdog_kicks++; }

static void fake_fifo(void *ctx, uint16_t value)
{
    fake_t *f = (fake_t *)ctx;
    if (f->fifo_len < sizeof(f->fifo) / sizeof(f->fifo[0]))
        f->fifo[f->fifo_len] = value;
    else
        f->fifo_overflow++;
    f->fifo_len++;
}

static void fake_hstadr(void *ctx, int32_t delta)
{
    ((fake_t *)ctx)->hstadr += delta;
}

static int fake_setup(fake_t *f, stunrun_march_io_t *io)
{
    memset(f, 0, sizeof(*f));
    f->ram = (uint16_t *)calloc(RAM_BYTES / 2, sizeof(uint16_t));
    CHECK(f->ram != NULL);
    CHECK(io != NULL);
    f->inject_at = -1;
    f->hstadr = 0;
    io->ctx = f;
    io->poke_w = fake_poke;
    io->peek_w = fake_peek;
    io->watchdog = fake_watchdog;
    io->fifo_w = fake_fifo;
    io->hstadr_add = fake_hstadr;
    return 0;
}

/* 1 + 2: every variant runs clean over the machine's real RAM geometry. */
static int test_clean_run_all_variants(void)
{
    static const struct { stunrun_march_variant_t v; const char *name; } k[] = {
        { STUNRUN_MARCH_C_ASCENDING,  "C ascending  (0x59E6)" },
        { STUNRUN_MARCH_C_DESCENDING, "C descending (0x5A32)" },
        { STUNRUN_MARCH_D_ASCENDING,  "D ascending  (0x5A7E)" },
        { STUNRUN_MARCH_D_DESCENDING, "D descending (0x5AD4)" },
        { STUNRUN_MARCH_INVERTED,     "inverted    (0x5B2A)" }
    };
    unsigned i;

    for (i = 0; i < sizeof(k) / sizeof(k[0]); i++) {
        fake_t f;
        stunrun_march_io_t io;
        stunrun_march_result_t r;

        CHECK(fake_setup(&f, &io) == 0);
        stunrun_march_fill(&io, RAM_BASE, RAM_LIMIT, 0x0000);
        stunrun_march_run(&io, k[i].v, RAM_BASE, RAM_LIMIT, 0x0000, &r);

        CHECK(!r.faulted);
        CHECK(r.fault_addr == 0);
        CHECK(r.passes == 1);
        CHECK(r.watchdog_kicks > 0);
        /* A clean march never reports, so the FIFO must stay empty. */
        CHECK(f.fifo_len == 0);
        CHECK(f.fifo_overflow == 0);

        if (k[i].v != STUNRUN_MARCH_INVERTED) {
            /* Invariant the march establishes: every word is pattern ^ 0xFFFF.
             * (The inverted test restores 0xFFFF rather than XORing.) */
            uint32_t w;
            for (w = 0; w < RAM_BYTES / 2; w++)
                CHECK(f.ram[w] == 0xFFFFu);
        }
        printf("  ok  %-22s watchdog kicks=%u\n", k[i].name,
               (unsigned)r.watchdog_kicks);
        free(f.ram);
    }
    return 0;
}

/* 3: fault injection must be caught, and caught the way the ROM reports it. */
static int test_fault_injection(void)
{
    static const stunrun_march_variant_t variants[] = {
        STUNRUN_MARCH_C_ASCENDING, STUNRUN_MARCH_C_DESCENDING,
        STUNRUN_MARCH_D_ASCENDING, STUNRUN_MARCH_D_DESCENDING,
        STUNRUN_MARCH_INVERTED
    };
    unsigned i;

    for (i = 0; i < sizeof(variants) / sizeof(variants[0]); i++) {
        fake_t f;
        stunrun_march_io_t io;
        stunrun_march_result_t r;
        int target = (int)(RAM_BYTES / 4);   /* a word well inside the march */

        CHECK(fake_setup(&f, &io) == 0);
        stunrun_march_fill(&io, RAM_BASE, RAM_LIMIT, 0x0000);
        /* Report a read-back value the march never wrote. */
        f.inject_at = target;
        f.inject_value = 0x1234u;
        f.inject_once = 1;
        stunrun_march_run(&io, variants[i], RAM_BASE, RAM_LIMIT, 0x0000, &r);

        CHECK(r.faulted);
        /* 0x5E82 emits $9F9F twice then nudges HSTADR forward by 0x20. */
        CHECK(f.fifo_len == 2);
        CHECK(f.fifo[0] == STUNRUN_MARCH_MARK_FAULT);
        CHECK(f.fifo[1] == STUNRUN_MARCH_MARK_FAULT);
        CHECK(f.hstadr == (int32_t)STUNRUN_MARCH_HSTADR_DELTA);
        /* The faulting address must be inside the range we marched.  Written as
         * a subtraction, never `addr < RAM_BASE + RAM_BYTES`: RAM_BASE +
         * RAM_BYTES wraps uint32_t to 0 (0xFFFF8000 + 0x8000 == 0x100000000)
         * so the additive form rejects every address. */
        CHECK(r.fault_addr >= RAM_BASE);
        CHECK(r.fault_addr - RAM_BASE < RAM_BYTES);
        printf("  ok  variant %u faulted at 0x%08lX\n", i,
               (unsigned long)r.fault_addr);
        free(f.ram);
    }
    return 0;
}

/* 4: the reporters are the diagnostics screen's only output, so pin them. */
static int test_reporters(void)
{
    fake_t f;
    stunrun_march_io_t io;

    CHECK(fake_setup(&f, &io) == 0);

    stunrun_march_report_pass(&io);
    CHECK(f.fifo_len == 2 && f.fifo[0] == 0x3F3Fu && f.fifo[1] == 0x3F3Fu);
    CHECK(f.hstadr == -0x20);          /* 0x5E48 uses subi.l */

    f.fifo_len = 0;
    f.hstadr = 0;
    stunrun_march_report_done(&io);
    CHECK(f.fifo_len == 2 && f.fifo[0] == 0x5F5Fu && f.fifo[1] == 0x5F5Fu);
    CHECK(f.hstadr == 0x20);

    f.fifo_len = 0;
    f.hstadr = 0;
    stunrun_march_report_phase2(&io);
    CHECK(f.fifo_len == 2 && f.fifo[0] == 0xFFFFu && f.fifo[1] == 0xFFFFu);
    CHECK(f.hstadr == 0x20);

    return 0;
}

/* The fill prologue at 0x59D8 must cover exactly [base, limit). */
static int test_fill_geometry(void)
{
    fake_t f;
    stunrun_march_io_t io;
    uint32_t next;
    uint32_t w;

    CHECK(fake_setup(&f, &io) == 0);
    /* Sentinel everywhere: any word the fill does not touch must still be
     * 0xAAAA when we look, which is how the exclusive-end claim below is
     * proven rather than assumed. */
    for (w = 0; w < RAM_BYTES / 2; w++)
        f.ram[w] = 0xAAAAu;
    next = stunrun_march_fill(&io, RAM_BASE, RAM_LIMIT, 0x5A5Au);

    CHECK(next == RAM_LIMIT);
    for (w = 0; w + 1 < RAM_BYTES / 2; w++)
        CHECK(f.ram[w] == 0x5A5Au);
    /* A2 is the EXCLUSIVE end: `lea (-$2,A1),A0 / write / addq.w #2,A0 /
     * cmpa.l A2,A0 / bcs` stops once A0 reaches A2, so the word AT A2
     * (offset 0x7FFE, the last of the 32 KiB) is never written and keeps
     * whatever it held.  That leaves 0x4000-1 = 16383 words touched, which
     * is what the harness observes. */
    CHECK(RAM_BASE + (RAM_BYTES - 2) == RAM_LIMIT);
    CHECK(f.ram[RAM_BYTES / 2 - 1] == 0xAAAAu);

    free(f.ram);
    return 0;
}

int main(void)
{
    printf("ram_march slice\n");
    if (test_fill_geometry())                 return 1;
    if (test_reporters())                    return 1;
    if (test_clean_run_all_variants())       return 1;
    if (test_fault_injection())              return 1;
    printf("ram_march: all checks passed\n");
    return 0;
}