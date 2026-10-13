/* musashi_text_log_test.c -- what OBSERVABLE OUTPUT the MC68010 text log
 * produces, and -- the part that matters -- what it must NOT produce.
 *
 * The decoder in native/musashi_cpu.c hangs off gsp_host_write_word(), the one
 * choke point for 16-bit writes into the $C00000-$C03FFF GSP host window.  It
 * claims to print one line per string the MC68010 draws, and nothing else.
 *
 * Both halves of that are asserted here, and the second half is the one worth
 * having: a decoder keyed on "the host address matches a descriptor base"
 * without the $25EE prologue arm fires on the ROM's own pixel walk.  Measured
 * on the real ROM, one $25E8 call emits count*8 address compositions, three of
 * which land on other descriptors' bases and five more of which do so during
 * the `addi.l #$10` walk.  Case 5 below is exactly that situation, and it is
 * what stops a regression re-introducing a log that reports one character of
 * "PRESS START TO BYPASS SELFTEST" as if it were "RCOIN".
 *
 * No commercial ROMs.  The selector table, the descriptors and the 68010
 * program are all synthesised here, in the same shapes the real ROM uses:
 *
 *   - the 394-long pointer table at $10136, 4 bytes per selector, big-endian;
 *   - a descriptor = x, y, colour, count, then `count` bytes with '*' (0x2A)
 *     as the word separator;
 *   - base = (y * 0xA0 * 0x100) + (x * 0x40) - 0x800000, 32-bit wrap;
 *   - a draw is an HSTCTL write with 0x1800 clear, then `move.l D0,$c00002.l`
 *     for the base, then `move.w D7,$c0000c.l` for the colour -- the three
 *     things $25E8 emits in that order.
 *
 * The synthetic table reproduces the two collisions that make this feature
 * non-trivial, because both are real: colour disambiguates, and sometimes it
 * does not.
 *
 *   $FF896040  $047 colour 0xDF  PRESS START TO BYPASS SELFTEST
 *              $048 colour 0xFF  PRESS START TO CONTINUE       (colour picks)
 *              $100 colour 0xDF  INC PM ADR                    (colour cannot)
 *   $FF800040  $034 colour 0xFF   8 BITA/D
 *              $035 colour 0xFF  12 BITA/D
 *              $043 colour 0xFF  PROGRAM ROM OK                (three-way)
 *              $044 colour 0x9F  BAD PROGRAM ROM IS BLANK BOX  (colour picks)
 */
#define _POSIX_C_SOURCE 200809L   /* fileno(), dup(), dup2(): strict C99 hides them */

#include "fake_ports.h"
#include "musashi_cpu.h"
#include "../third_party/musashi/m68k.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define STUB_BASE   0x00FFF000u
#define MAX_CAPTURE 16384

static unsigned checks;
static unsigned failures;

#define CHECK(expr) do { \
    checks++; \
    if (!(expr)) { \
        fprintf(stderr, "FAIL: %s (line %d)\n", #expr, __LINE__); \
        failures++; \
    } \
} while (0)

#define CHECK_EQ(actual, expected) do { \
    long long a_ = (long long)(actual); \
    long long e_ = (long long)(expected); \
    checks++; \
    if (a_ != e_) { \
        fprintf(stderr, "FAIL: %s == %s (got %lld, want %lld; line %d)\n", \
                #actual, #expected, a_, e_, __LINE__); \
        failures++; \
    } \
} while (0)

/* ── stdout capture ──────────────────────────────────────────────────────
 * The decoded line IS this feature's output, so the test asserts on what a
 * human would see.  There is deliberately no side channel: adding a callback
 * would let a broken printer pass. */
static int saved_stdout = -1;
static FILE *capture_file;

static void capture_begin(void)
{
    fflush(stdout);
    saved_stdout = dup(1);
    capture_file = tmpfile();
    dup2(fileno(capture_file), 1);
}

static void capture_end(char *out, size_t capacity)
{
    size_t n;
    fflush(stdout);
    dup2(saved_stdout, 1);
    close(saved_stdout);
    saved_stdout = -1;
    rewind(capture_file);
    n = fread(out, 1, capacity - 1u, capture_file);
    out[n] = '\0';
    fclose(capture_file);
    capture_file = NULL;
}

static unsigned count_of(const char *haystack, const char *needle)
{
    unsigned n = 0;
    size_t len = strlen(needle);
    const char *p = haystack;
    while ((p = strstr(p, needle)) != NULL) {
        n++;
        p += len;
    }
    return n;
}

/* Every decoded line starts with this, and it is not preceded by a newline on
 * the first one, so counting on a leading "\n" would silently miss line 1. */
static unsigned line_count(const char *haystack)
{
    return count_of(haystack, "text frame=");
}

static int contains(const char *haystack, const char *needle)
{
    return strstr(haystack, needle) != NULL;
}

/* ── Synthetic ROM ───────────────────────────────────────────────────────
 * musashi_cpu_load_rom_bank() takes 64 KiB even and 64 KiB odd arrays and
 * interleaves them, so flat[A] = (A & 1) ? odd[A >> 1] : even[A >> 1].  That
 * is the same interleave the board's ROM_LOAD16_BYTE pairs use -- the reason
 * `tools/build-maincpu-image` exists -- but here the bytes are ours, so it is
 * done by hand rather than by file offsets. */
static uint8_t rom_even[MUSASHI_ROM_BANK_SIZE / 2u];
static uint8_t rom_odd[MUSASHI_ROM_BANK_SIZE / 2u];

static void rom_put(uint32_t addr, uint8_t value)
{
    if ((addr & 1u) == 0u)
        rom_even[addr >> 1] = value;
    else
        rom_odd[addr >> 1] = value;
}

static void rom_put_long(uint32_t addr, uint32_t value)
{
    rom_put(addr + 0u, (uint8_t)(value >> 24));
    rom_put(addr + 1u, (uint8_t)(value >> 16));
    rom_put(addr + 2u, (uint8_t)(value >> 8));
    rom_put(addr + 3u, (uint8_t)value);
}

static void rom_descriptor(uint32_t addr, uint8_t x, uint8_t y, uint8_t colour,
                           const char *text)
{
    size_t i;
    size_t len = strlen(text);

    rom_put(addr + 0u, x);
    rom_put(addr + 1u, y);
    rom_put(addr + 2u, colour);
    rom_put(addr + 3u, (uint8_t)len);
    for (i = 0; i < len; i++)
        rom_put(addr + 4u + (uint32_t)i, (uint8_t)text[i]);
}

static int is_named_selector(unsigned i)
{
    return i == 0x034u || i == 0x035u || i == 0x043u || i == 0x044u ||
           i == 0x047u || i == 0x048u || i == 0x100u || i == 0x101u;
}

static void rom_build_table(void)
{
    uint32_t sel_addr[394];
    unsigned i;

    memset(rom_even, 0, sizeof(rom_even));
    memset(rom_odd, 0, sizeof(rom_odd));

    /* The collisions, at the same base coordinates the real ROM uses.
     * $034/$035/$043 share a base AND a colour; $044 shares only the base. */
    sel_addr[0x034] = 0x06000u;
    rom_descriptor(0x06000u, 0x01, 0x00, 0xFF, "8*BITA/D");
    sel_addr[0x035] = 0x06040u;
    rom_descriptor(0x06040u, 0x01, 0x00, 0xFF, "12*BITA/D");
    sel_addr[0x043] = 0x06080u;
    rom_descriptor(0x06080u, 0x01, 0x00, 0xFF, "PROGRAM*ROM*OK");
    sel_addr[0x044] = 0x060C0u;
    rom_descriptor(0x060C0u, 0x01, 0x00, 0x9F,
                   "BAD*PROGRAM*ROM*IS*BLANK*BOX");

    /* $047 is the one the service-mode screen really draws. */
    sel_addr[0x047] = 0x06100u;
    rom_descriptor(0x06100u, 0x01, 0x0F, 0xDF,
                   "PRESS*START*TO*BYPASS*SELFTEST");
    sel_addr[0x048] = 0x06140u;
    rom_descriptor(0x06140u, 0x01, 0x0F, 0xFF,
                   "PRESS*START*TO*CONTINUE");
    sel_addr[0x100] = 0x06180u;
    rom_descriptor(0x06180u, 0x01, 0x0F, 0xDF, "INC*PM*ADR");
    /* One column right of $047, i.e. exactly base + 0x40 -- the address the
     * ROM's `addi.l #$10` pixel walk steps onto in row 0, pixel 4. */
    sel_addr[0x101] = 0x061C0u;
    rom_descriptor(0x061C0u, 0x02, 0x0F, 0x70, "WALK*STOP");

    /* Filler so the table walk decodes the full 394 entries.  Parked at row
     * 0x7E, which no test draws at, so it can never satisfy a lookup by
     * accident.  16 bytes per descriptor fits a one-character string. */
    for (i = 0; i < 394u; i++) {
        if (is_named_selector(i))
            continue;
        sel_addr[i] = 0x06400u + 0x10u * (uint32_t)i;
        rom_descriptor(sel_addr[i], (uint8_t)(i & 0xFFu), 0x7Eu, 0x3Fu, "*");
    }
    for (i = 0; i < 394u; i++)
        rom_put_long(0x10136u + 4u * i, sel_addr[i]);
}

/* ── Synthetic 68010 program ─────────────────────────────────────────────
 * Every store is `move.l #imm,$c000xx.l` (0x23FC), which the MC68010 splits
 * into two 16-bit host writes with the HIGH half first at the lower address --
 * the same shape as the ROM's `move.l D0,$c00002.l` at $2656, and the reason
 * the decoder waits for the low half before it has a whole address.
 *
 * Word-sized `move.w` is deliberately avoided: it also emits a byte-lane write
 * at the odd address, which decodes to the same register again and would
 * double every event in these tests for no added coverage.  The real prologue
 * `andi.w #$e7ff,$c00008.l` is a read-modify-write and emits exactly one word,
 * so an equivalent single word here is what the arming rule is really being
 * tested against. */
typedef struct {
    uint8_t bytes[512];
    size_t  length;
} program_t;

static void emit_store_long(program_t *p, uint32_t value, uint32_t host_addr)
{
    p->bytes[p->length + 0u] = 0x23;
    p->bytes[p->length + 1u] = 0xfc;
    p->bytes[p->length + 2u] = (uint8_t)(value >> 24);
    p->bytes[p->length + 3u] = (uint8_t)(value >> 16);
    p->bytes[p->length + 4u] = (uint8_t)(value >> 8);
    p->bytes[p->length + 5u] = (uint8_t)value;
    p->bytes[p->length + 6u] = (uint8_t)(host_addr >> 24);
    p->bytes[p->length + 7u] = (uint8_t)(host_addr >> 16);
    p->bytes[p->length + 8u] = (uint8_t)(host_addr >> 8);
    p->bytes[p->length + 9u] = (uint8_t)host_addr;
    p->length += 10u;
}

/* Host window register addresses, as the 68010 sees them. */
#define HOST_HSTADRH 0x00C00002u
#define HOST_HSTADRL 0x00C00004u
#define HOST_HSTCTL  0x00C00008u
#define HOST_HSTDAT  0x00C0000Cu

/* The arm: an HSTCTL write with bits 11/12 clear, which is what
 * `andi.w #$e7ff,$c00008.l` at $25EE leaves behind.  Both halves of the long
 * store clear them, so the decoder is armed either way it reads the pair. */
static void emit_arm(program_t *p)
{
    emit_store_long(p, 0x00000000u, HOST_HSTCTL);
}

/* The disarm: `ori.w #$1800,$c00008.l` from the display-list compiler SETS
 * bits 11 and 12.  The decoder must treat that as "not a text draw". */
static void emit_disarm(program_t *p)
{
    emit_store_long(p, 0x18001800u, HOST_HSTCTL);
}

/* move.l D0,$c00002.l: the long store lands high half at $C00002 first and low
 * half at $C00004 second, which is the order the decoder depends on. */
static void emit_address(program_t *p, uint32_t base)
{
    emit_store_long(p, base, HOST_HSTADRH);
}

/* The ROM's two HSTDAT writes for one lit pixel:
 *   `move.w D7,$c0000c.l` -- D7 is `clr.w D7 / move.b ($2,A0),D7`, so the word
 *     is 0x00C and the descriptor colour is in the LOW byte with a zero high
 *     byte.  This is the only write the decoder acts on.
 *   `or.w D6,$c0000c.l`   -- D6 is D7 << 8, OR-ed into 0x00C read back, giving
 *     (C<<8)|C, i.e. the colour in the HIGH byte.  Verified live: selector
 *     $47's colour 0xDF produced HSTDAT 0x00DF then 0xDFDF. */
static void emit_data(program_t *p, uint8_t colour)
{
    uint16_t low_colour = colour;
    uint16_t or_ed = (uint16_t)(((uint16_t)colour << 8) | colour);
    emit_store_long(p, ((uint32_t)low_colour << 16) | or_ed, HOST_HSTDAT);
}

/* A single HSTDAT word, for a colour no descriptor at that base carries. */
static void emit_data_word(program_t *p, uint16_t value)
{
    emit_store_long(p, ((uint32_t)value << 16) | (uint32_t)value, HOST_HSTDAT);
}

static void emit_stop(program_t *p)
{
    p->bytes[p->length + 0u] = 0x60;
    p->bytes[p->length + 1u] = 0xfe;
    p->length += 2u;
}

static void write_program(stunrun_fake_ports_t *ports, const uint8_t *bytes,
                          size_t length)
{
    size_t i;
    for (i = 0; i + 1u < length; i += 2u) {
        uint16_t word = (uint16_t)(((uint16_t)bytes[i] << 8) | bytes[i + 1u]);
        CHECK(stunrun_mem_write16_be(ports, STUB_BASE + (uint32_t)i, word));
    }
}

static void run_to_spin(musashi_cpu_t *cpu, uint32_t spin_pc)
{
    unsigned cycles = 0;
    m68k_set_reg(M68K_REG_SR, 0x2700u);   /* mask interrupts */
    m68k_set_reg(M68K_REG_A7, 0x00FFF800u);
    m68k_set_reg(M68K_REG_PC, STUB_BASE);
    while (musashi_cpu_get_pc(cpu) != spin_pc && cycles < 100000u) {
        int used = musashi_cpu_run(cpu, 100);
        if (used <= 0)
            break;
        cycles += (unsigned)used;
    }
}

static musashi_cpu_t *make_cpu(stunrun_fake_ports_t *ports,
                               const program_t *p)
{
    musashi_cpu_t *cpu = musashi_cpu_create();
    CHECK(cpu != NULL);
    if (cpu == NULL)
        return NULL;
    stunrun_ports_init(ports);
    musashi_cpu_set_ports(cpu, ports);
    rom_build_table();
    musashi_cpu_reset(cpu);
    write_program(ports, p->bytes, p->length);
    CHECK_EQ(musashi_cpu_load_rom_bank(cpu, 0u, rom_even, rom_odd), 0);
    return cpu;
}

/* Run `p` with the text log on and return what it printed. */
static void run_case(const program_t *p, char *out, size_t capacity,
                     unsigned *descriptors)
{
    stunrun_fake_ports_t ports;
    musashi_cpu_t *cpu = make_cpu(&ports, p);

    memset(out, 0, capacity);
    if (cpu == NULL)
        return;
    capture_begin();
    *descriptors = musashi_cpu_text_log_enable(cpu);
    run_to_spin(cpu, STUB_BASE + (uint32_t)p->length);
    capture_end(out, capacity);
    musashi_cpu_text_log_disable();
    musashi_cpu_destroy(cpu);
}

/* The ROM's HSTADR is a TMS34010 BIT address, not a byte address. With
 * addrshift=3, $FF896040 maps to physical share byte offset $12C08. Assert the
 * actual stored word there so the text-log test also guards the share decode
 * that used to put the correct glyph data at the wrong 4 MiB-file offset. */
static void test_text_word_in_physical_share(void)
{
    stunrun_fake_ports_t ports;
    musashi_gsp_memory_t memory;
    musashi_cpu_t *cpu;
    program_t p;
    const uint8_t *bytes;

    memset(&p, 0, sizeof(p));
    emit_arm(&p);
    emit_address(&p, 0xFF896040u);
    emit_data(&p, 0xDFu);
    emit_stop(&p);
    cpu = make_cpu(&ports, &p);
    CHECK(cpu != NULL);
    if (cpu == NULL)
        return;

    run_to_spin(cpu, STUB_BASE + (uint32_t)p.length);
    CHECK(musashi_cpu_gsp_memory(cpu, &memory) == 0);
    bytes = (const uint8_t *)memory.vram;
    CHECK_EQ(MUSASHI_GSP_VRAM_BYTES, 0x80000u);
    CHECK_EQ((0xFF896040u - 0xFF800000u) >> 3u, 0x12C08u);
    /* The 68010 sends 0x00DF then 0xDFDF to the same cell. On the little-endian
     * 16-bit share, the final physical word is bytes DF DF. */
    CHECK(bytes[0x12C08u] == 0xDFu);
    CHECK(bytes[0x12C09u] == 0xDFu);
    /* The old byte-address interpretation targeted 0x96040, beyond this
     * 512 KiB share. The capacity assertion above makes that invalid address
     * visible without reading out of bounds. */
    CHECK(0x96042u >= MUSASHI_GSP_VRAM_BYTES);

    musashi_cpu_destroy(cpu);
}

int main(void)
{
    program_t p;
    char out[MAX_CAPTURE];
    unsigned descriptors = 0;
    musashi_text_log_stats_t stats;

    test_text_word_in_physical_share();

    /* ── Case 1: colour picks a pair out of everything at that base ───────
     * The real base 0xFF896040 carries 18 descriptors across five colours.
     * A 0xDF HSTDAT must select the 0xDF pair and no other, and because two
     * of them share the colour the line has to SAY so rather than pick one. */
    memset(&p, 0, sizeof(p));
    emit_arm(&p);
    emit_address(&p, 0xFF896040u);
    emit_data(&p, 0xDFu);
    emit_stop(&p);
    run_case(&p, out, sizeof(out), &descriptors);

    CHECK_EQ(descriptors, 394u);
    /* '*' renders as a space, because that is what it draws. */
    CHECK(contains(out, "sel=$047 x=01 y=0F colour=0xDF base=0xFF896040 "
                        "chars=30 \"PRESS START TO BYPASS SELFTEST\"\n"));
    CHECK(contains(out, "colour=0xDF is shared by 2 descriptors"));
    CHECK(contains(out, "$100 \"INC PM ADR\""));
    /* The other colours at that base are absent: colour really did filter. */
    CHECK(!contains(out, "PRESS START TO CONTINUE"));
    CHECK(!contains(out, "BAD PROGRAM ROM"));

    /* ── Case 2: colour disambiguates cleanly, so no ambiguity line ──────── */
    memset(&p, 0, sizeof(p));
    emit_arm(&p);
    emit_address(&p, 0xFF800040u);
    emit_data(&p, 0x9Fu);
    emit_stop(&p);
    run_case(&p, out, sizeof(out), &descriptors);
    CHECK(contains(out, "sel=$044 x=01 y=00 colour=0x9F base=0xFF800040 "
                        "chars=28 \"BAD PROGRAM ROM IS BLANK BOX\"\n"));
    CHECK(!contains(out, "is shared by"));

    /* ── Case 3: colour does NOT disambiguate -- three-way ───────────────
     * $034, $035 and $043 are all at 0xFF800040 in colour 0xFF.  The lowest
     * selector is printed and the other two are named.  This is the case a
     * decoder that "just takes the first match" quietly hides. */
    memset(&p, 0, sizeof(p));
    emit_arm(&p);
    emit_address(&p, 0xFF800040u);
    emit_data(&p, 0xFFu);
    emit_stop(&p);
    run_case(&p, out, sizeof(out), &descriptors);
    CHECK(contains(out, "sel=$034 x=01 y=00 colour=0xFF base=0xFF800040 "
                        "chars=8 \"8 BITA/D\"\n"));
    CHECK(contains(out, "colour=0xFF is shared by 3 descriptors"));
    CHECK(contains(out, "$035 \"12 BITA/D\""));
    CHECK(contains(out, "$043 \"PROGRAM ROM OK\""));

    /* ── Case 4: a disarm is not an arm ──────────────────────────────────
     * `ori.w #$1800,$c00008.l` sets bits 11/12, so the same address and the
     * same colour print nothing.  Dropping the bits-11/12 test from the
     * decoder fails here. */
    memset(&p, 0, sizeof(p));
    emit_disarm(&p);
    emit_address(&p, 0xFF896040u);
    emit_data(&p, 0xDFu);
    emit_stop(&p);
    run_case(&p, out, sizeof(out), &descriptors);
    CHECK_EQ(line_count(out), 0u);
    CHECK(!contains(out, "PRESS START TO BYPASS SELFTEST"));
    musashi_cpu_text_log_stats(&stats);
    CHECK_EQ(stats.arms, 0u);
    CHECK_EQ(stats.matched, 0u);
    CHECK_EQ(stats.fired, 0u);

    /* ── Case 5: no prologue arm at all ──────────────────────────────────
     * Without the `andi.w #$e7ff` the same address and colour print nothing.
     * Deleting the arming entirely fails here and at case 1. */
    memset(&p, 0, sizeof(p));
    emit_address(&p, 0xFF896040u);
    emit_data(&p, 0xDFu);
    emit_stop(&p);
    run_case(&p, out, sizeof(out), &descriptors);
    CHECK_EQ(line_count(out), 0u);
    CHECK(!contains(out, "PRESS START TO BYPASS SELFTEST"));
    CHECK_EQ(descriptors, 394u);

    /* ── Case 6: the pixel walk must not re-fire ──────────────────────────
     * One $25E8 call draws 30 characters and advances the address by +0x40 per
     * character, and 0xFF896080 is a real descriptor base one column right.
     * Exactly one line may come out of the pair below. */
    memset(&p, 0, sizeof(p));
    emit_arm(&p);
    emit_address(&p, 0xFF896040u);
    emit_data(&p, 0xDFu);
    emit_address(&p, 0xFF896080u);   /* the walk stepping onto a real base */
    emit_data(&p, 0x70u);
    emit_stop(&p);
    run_case(&p, out, sizeof(out), &descriptors);
    CHECK_EQ(count_of(out, "sel=$047"), 1u);
    CHECK_EQ(count_of(out, "sel=$101"), 0u);
    CHECK(!contains(out, "WALK STOP"));
    musashi_cpu_text_log_stats(&stats);
    CHECK_EQ(stats.arms, 1u);
    CHECK_EQ(stats.matched, 1u);
    CHECK_EQ(stats.fired, 1u);

    /* ── Case 7: a colour no descriptor at that base carries ──────────────
     * Counted, not printed.  A nonzero colour_miss is how an arming rule that
     * admits non-text traffic announces itself, so it has to be observable. */
    memset(&p, 0, sizeof(p));
    emit_arm(&p);
    emit_address(&p, 0xFF896080u);
    emit_data_word(&p, 0x0033u);
    emit_stop(&p);
    run_case(&p, out, sizeof(out), &descriptors);
    CHECK_EQ(line_count(out), 0u);
    musashi_cpu_text_log_stats(&stats);
    CHECK_EQ(stats.colour_miss, 1u);
    CHECK_EQ(stats.fired, 0u);

    /* ── Case 8: the counters, on a run that is three well-formed draws ── */
    memset(&p, 0, sizeof(p));
    emit_arm(&p);
    emit_address(&p, 0xFF896040u);
    emit_data(&p, 0xDFu);
    emit_arm(&p);
    emit_address(&p, 0xFF800040u);
    emit_data(&p, 0xFFu);
    emit_arm(&p);
    emit_address(&p, 0xFF800040u);
    emit_data(&p, 0x9Fu);
    emit_stop(&p);
    run_case(&p, out, sizeof(out), &descriptors);
    CHECK_EQ(line_count(out), 3u);
    musashi_cpu_text_log_stats(&stats);
    CHECK_EQ(stats.arms, 3u);
    CHECK_EQ(stats.matched, 3u);
    CHECK_EQ(stats.fired, 3u);
    CHECK_EQ(stats.colour_miss, 0u);
    CHECK_EQ(stats.ambiguous, 2u);

    /* ── Case 9: disabled is genuinely off, and changes nothing ───────────
     * The same program with the log never enabled: no lines, and the machine
     * still took every bus write.  The observation-only claim, in miniature;
     * the real proof is the byte-identical 600-frame dump. */
    {
        stunrun_fake_ports_t ports;
        musashi_cpu_t *cpu = make_cpu(&ports, &p);
        if (cpu != NULL) {
            size_t before = musashi_cpu_gsp_bus_event_count(cpu);
            capture_begin();
            run_to_spin(cpu, STUB_BASE + (uint32_t)p.length);
            capture_end(out, sizeof(out));
            CHECK_EQ(line_count(out), 0u);
            /* 3 arms x 2 words + 3 addresses x 2 words + 3 data x 2 words
             * = 18 host-window events; the `bra.s *` touches no device. */
            CHECK_EQ(musashi_cpu_gsp_bus_event_count(cpu) - before, 18u);
            musashi_cpu_destroy(cpu);
        }
    }

    /* ── Case 10: the same program, log on, produces the same bus stream ──
     * This is the observation-only property stated as a test: enabling the
     * decoder must not cost or add a single bus event. */
    {
        stunrun_fake_ports_t ports;
        musashi_cpu_t *cpu = make_cpu(&ports, &p);
        if (cpu != NULL) {
            size_t before = musashi_cpu_gsp_bus_event_count(cpu);
            unsigned n;
            capture_begin();
            n = musashi_cpu_text_log_enable(cpu);
            run_to_spin(cpu, STUB_BASE + (uint32_t)p.length);
            capture_end(out, sizeof(out));
            CHECK_EQ(n, 394u);
            CHECK_EQ(musashi_cpu_gsp_bus_event_count(cpu) - before, 18u);
            CHECK_EQ(line_count(out), 3u);
            musashi_cpu_text_log_disable();
            musashi_cpu_destroy(cpu);
        }
    }

    printf("musashi_text_log_test: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
