/* Tests for the MC68010 text-draw slice at ROM $25E8-$269F.
 *
 * The headline assertion is that the ordered sequence of 240 HSTADR latches
 * the slice produces for selector $47 matches, value for value and in order,
 * the 240 latches recorded in
 *
 *   reference/experiments/stunrun/service-f2-text-host-writes/result.json
 *
 * (service-mode MAME capture, PC $2656 filtered, 1040 taps = 520 long writes;
 * the 240 covered here are draw 0, frames 214-216, descriptor $06C53).  That
 * is what pins the loop nesting: eight latches per character, thirty
 * characters, +$1000 per row, +$40 per cell, $FF896040 for the first.  Nothing
 * weaker would do.  A routine that drew the right number of glyphs at the
 * wrong stride, or the right pixels in the wrong order, would still "work", and
 * a check that only counted writes could not tell them apart.
 *
 * The capture taps only $C00002/$C00004 and only at PC $2656, so it cannot see
 * HSTDAT, and cannot see the read half of the `addi.l` at $267A.  The rest of
 * the stream is pinned by a literal 83-cycle golden (rows 0-1 of the first
 * character, which between them contain every HSTDAT shape the ROM can
 * produce -- nibble 5, nibble 4, nibble 1 and nibble 0), by a lockstep walk of
 * all 9154 cycles against the nibble rule derived from the embedded font, and
 * by an exact census.
 *
 * Fixture bytes are transcribed from
 *   roms/stunrun/stunrun_maincpu.bin
 *   sha256 29c911a21d1abe780419cd3a924080756490f2d008776995dd48c7b2bedc5b6c
 * No ROM is needed to build or run this test.
 */
#include "mc68010_text_writer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x)                                                            \
    do {                                                                    \
        if (!(x)) {                                                         \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__,\
                    #x);                                                   \
            return 1;                                                       \
        }                                                                   \
    } while (0)

static int check_eq(unsigned long long got, unsigned long long want,
                    const char *what, int line)
{
    if (got != want) {
        fprintf(stderr, "%s:%d: %s = %llu (0x%llX), expected %llu (0x%llX)\n",
                __FILE__, line, what, got, got, want, want);
        return 0;
    }
    return 1;
}

#define CHECK_EQ(got, want)                                                 \
    do {                                                                    \
        if (!check_eq((unsigned long long)(got), (unsigned long long)(want), \
                      #got, __LINE__))                                      \
            return 1;                                                       \
    } while (0)

/* ── Fixtures, transcribed from the ROM ────────────────────────────────── */

/* Selector $47's descriptor, at its real ROM address $06C53:
 *   01 0F DF 1E  "PRESS*START*TO*BYPASS*SELFTEST"
 * x=1, y=$0F, colour=$DF, count=$1E=30, so the base HSTADR is
 * $0F*$A000 + 1*$40 - $800000 = $FF896040 -- the first value in the capture. */
#define FIXTURE_DESCRIPTOR 0x06C53u
/* A second descriptor, different in every field, wired to selector $46 so that
 * resolving the wrong table slot is visible. */
#define FIXTURE_DECOY 0x06C40u
/* And one wired to selector $45 whose pointer is outside the image, so that a
 * missing bounds check reads past the buffer instead of failing. */
#define FIXTURE_BAD_SELECTOR 0x45u
/* `lea $10136.l,A0` ($25F6), as a LITERAL.  Deliberately not
 * STUNRUN_TEXT_SELECTOR_TABLE: a fixture placed at whatever the header says
 * moves with a wrong constant and the test still passes, which is the trap
 * findings section 15.6 describes.  The one equality check below is what makes
 * the header's copy auditable. */
#define FIXTURE_SELECTOR_TABLE 0x10136u

static const uint8_t k_descriptor[] = {
    0x01, 0x0F, 0xDF, 0x1E, 0x50, 0x52, 0x45, 0x53, 0x53, 0x2A, 0x53, 0x54,
    0x41, 0x52, 0x54, 0x2A, 0x54, 0x4F, 0x2A, 0x42, 0x59, 0x50, 0x41, 0x53,
    0x53, 0x2A, 0x53, 0x45, 0x4C, 0x46, 0x54, 0x45, 0x53, 0x54
};
/* x=3, y=2, colour $3F, count 2, "AB".  Base $02*$A000 + 3*$40 - $800000. */
static const uint8_t k_decoy[] = { 0x03, 0x02, 0x3F, 0x02, 0x41, 0x42 };

/* The 16 bytes of every glyph the fixture text uses, at the addresses
 * `lea $1fbc.l,A1` computes for them.  Each row is one 16-bit word holding
 * FOUR pixels, one per nibble; and across all 300 glyphs of this font the
 * nibbles only ever take the values 0, 1, 4 and 5, which is exactly why the
 * ROM tests bit 0 and bit 2 of a nibble and nothing else. */
static const struct {
    uint8_t glyph;
    uint32_t address;
    uint8_t bytes[16];
} k_glyphs[] = {
    { 0x2A, 0x1FBC, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } }, /* * */
    { 0x41, 0x212C, { 0x01, 0x50, 0x05, 0x14, 0x14, 0x05, 0x14, 0x05,
                      0x15, 0x55, 0x14, 0x05, 0x14, 0x05, 0x00, 0x00 } }, /* A */
    { 0x42, 0x213C, { 0x05, 0x55, 0x14, 0x05, 0x14, 0x05, 0x05, 0x55,
                      0x14, 0x05, 0x14, 0x05, 0x05, 0x55, 0x00, 0x00 } }, /* B */
    { 0x45, 0x216C, { 0x15, 0x55, 0x00, 0x05, 0x00, 0x05, 0x01, 0x55,
                      0x00, 0x05, 0x00, 0x05, 0x15, 0x55, 0x00, 0x00 } }, /* E */
    { 0x46, 0x217C, { 0x15, 0x55, 0x00, 0x05, 0x00, 0x05, 0x01, 0x55,
                      0x00, 0x05, 0x00, 0x05, 0x00, 0x05, 0x00, 0x00 } }, /* F */
    { 0x4C, 0x21DC, { 0x00, 0x14, 0x00, 0x14, 0x00, 0x14, 0x00, 0x14,
                      0x00, 0x14, 0x00, 0x14, 0x15, 0x54, 0x00, 0x00 } }, /* L */
    { 0x4F, 0x220C, { 0x05, 0x54, 0x14, 0x05, 0x14, 0x05, 0x14, 0x05,
                      0x14, 0x05, 0x14, 0x05, 0x05, 0x54, 0x00, 0x00 } }, /* O */
    { 0x50, 0x221C, { 0x05, 0x55, 0x14, 0x05, 0x14, 0x05, 0x14, 0x05,
                      0x05, 0x55, 0x00, 0x05, 0x00, 0x05, 0x00, 0x00 } }, /* P */
    { 0x52, 0x223C, { 0x05, 0x55, 0x14, 0x05, 0x14, 0x05, 0x05, 0x05,
                      0x01, 0x55, 0x05, 0x45, 0x15, 0x05, 0x00, 0x00 } }, /* R */
    { 0x53, 0x224C, { 0x01, 0x54, 0x05, 0x05, 0x00, 0x05, 0x05, 0x54,
                      0x14, 0x00, 0x14, 0x05, 0x05, 0x54, 0x00, 0x00 } }, /* S */
    { 0x54, 0x225C, { 0x15, 0x54, 0x01, 0x40, 0x01, 0x40, 0x01, 0x40,
                      0x01, 0x40, 0x01, 0x40, 0x01, 0x40, 0x00, 0x00 } }, /* T */
    { 0x59, 0x22AC, { 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x05, 0x50,
                      0x01, 0x40, 0x01, 0x40, 0x01, 0x40, 0x00, 0x00 } }, /* Y */
};

/* The image is 66 KiB only because the selector table lives at $10136. */
#define FIXTURE_ROM_SIZE 0x10300u
static uint8_t g_rom[FIXTURE_ROM_SIZE];

static void store_long(uint8_t *rom, uint32_t address, uint32_t value)
{
    rom[address + 0u] = (uint8_t)(value >> 24);
    rom[address + 1u] = (uint8_t)(value >> 16);
    rom[address + 2u] = (uint8_t)(value >> 8);
    rom[address + 3u] = (uint8_t)value;
}

static void build_rom(void)
{
    size_t i;

    memset(g_rom, 0, sizeof(g_rom));
    memcpy(g_rom + FIXTURE_DESCRIPTOR, k_descriptor, sizeof(k_descriptor));
    memcpy(g_rom + FIXTURE_DECOY, k_decoy, sizeof(k_decoy));
    for (i = 0u; i < sizeof(k_glyphs) / sizeof(k_glyphs[0]); i++)
        memcpy(g_rom + k_glyphs[i].address, k_glyphs[i].bytes, 16u);
    /* $25F6 lea $10136.l,A0 / $2604 movea.l (A0,D0.l),A0 */
    store_long(g_rom, FIXTURE_SELECTOR_TABLE + 4u * 0x47u,
               FIXTURE_DESCRIPTOR);
    store_long(g_rom, FIXTURE_SELECTOR_TABLE + 4u * 0x46u, FIXTURE_DECOY);
    store_long(g_rom, FIXTURE_SELECTOR_TABLE + 4u * FIXTURE_BAD_SELECTOR,
               FIXTURE_ROM_SIZE + 0x1000u);
}

/* ── The machine the slice drives ─────────────────────────────────────── */

#define LOG_CAPACITY 16384u
#define WORD_SLOTS 4096u

typedef struct {
    int is_write;
    uint32_t address;
    uint16_t value;
} cycle_t;

static cycle_t g_cycle[LOG_CAPACITY];
static size_t g_count;
static size_t g_writes;
static size_t g_reads;

/* Sparse GSP word memory keyed on HSTADR >> 4.  Flat 16-bit words: this slice
 * deliberately stops at the host port, before `hdgsp_vram_2bpp_w`. */
static struct {
    uint32_t key;
    uint16_t value;
    int used;
} g_word[WORD_SLOTS];

static size_t word_slot(uint32_t address, int create)
{
    uint32_t key = (address >> 4) & (WORD_SLOTS - 1u);
    size_t probe;

    for (probe = 0u; probe < WORD_SLOTS; probe++) {
        size_t i = (size_t)((key + probe) & (WORD_SLOTS - 1u));
        if (!g_word[i].used) {
            if (!create)
                return WORD_SLOTS;
            g_word[i].used = 1;
            g_word[i].key = key;
            g_word[i].value = 0u;
            return i;
        }
        if (g_word[i].key == key)
            return i;
    }
    fprintf(stderr, "word table full at $%08X\n", address);
    exit(2);
    return WORD_SLOTS;
}

static uint16_t gsp_read(void *ctx, uint32_t address)
{
    size_t i = word_slot(address, 0);
    (void)ctx;
    return i == WORD_SLOTS ? 0u : g_word[i].value;
}

static int gsp_write(void *ctx, uint32_t address, uint16_t value)
{
    size_t i = word_slot(address, 1);
    (void)ctx;
    if (i == WORD_SLOTS)
        return 0;
    g_word[i].value = value;
    return 1;
}

static void bus_read(void *ctx, uint32_t address, uint16_t value)
{
    (void)ctx;
    if (g_count == LOG_CAPACITY) {
        fprintf(stderr, "bus log overflow\n");
        exit(2);
    }
    g_cycle[g_count].is_write = 0;
    g_cycle[g_count].address = address;
    g_cycle[g_count].value = value;
    g_count++;
    g_reads++;
}

static void bus_write(void *ctx, uint32_t address, uint16_t value)
{
    (void)ctx;
    if (g_count == LOG_CAPACITY) {
        fprintf(stderr, "bus log overflow\n");
        exit(2);
    }
    g_cycle[g_count].is_write = 1;
    g_cycle[g_count].address = address;
    g_cycle[g_count].value = value;
    g_count++;
    g_writes++;
}

static void machine_reset(void)
{
    memset(g_cycle, 0, sizeof(g_cycle));
    memset(g_word, 0, sizeof(g_word));
    g_count = 0u;
    g_writes = 0u;
    g_reads = 0u;
}

static size_t draw(uint16_t selector)
{
    stunrun_mc68010_text_port_t port;

    memset(&port, 0, sizeof(port));
    /* The ROM's `ori.w #$1800` sites leave both post-increment bits latched, so
     * a real caller arrives with 0x1800 already set.  Starting there is what
     * makes the prologue's `andi.w #$e7ff` observable rather than a no-op. */
    port.hstctl = 0x1800u;
    port.gsp_read = gsp_read;
    port.gsp_write = gsp_write;
    port.bus_read = bus_read;
    port.bus_write = bus_write;
    return stunrun_mc68010_text_draw(selector, g_rom, sizeof(g_rom), &port);
}

static void seed_words(uint32_t base, uint32_t top, uint16_t value)
{
    uint32_t address;
    for (address = base; address < top; address += 0x10u)
        (void)gsp_write(NULL, address, value);
}

/* ── Goldens ──────────────────────────────────────────────────────────── */

/* The 240 HSTADR latches the committed capture recorded for selector $47, in
 * order, reassembled from the 480 taps of result.json belonging to draw 0.
 * A 32-bit `move.l D0,$c00002.l` arrives as two 16-bit taps, most significant
 * half first at $C00002 and least significant at $C00004, so the value is
 * (tap@$C00002 << 16) | tap@$C00004.  Reading that the other way round gives
 * $6040FF89 and silently fails to match anything. */
static const uint32_t k_capture_latches[] = {
    0xFF896040u, 0xFF897040u, 0xFF898040u, 0xFF899040u, 0xFF89A040u,
    0xFF89B040u, 0xFF89C040u, 0xFF89D040u, 0xFF896080u, 0xFF897080u,
    0xFF898080u, 0xFF899080u, 0xFF89A080u, 0xFF89B080u, 0xFF89C080u,
    0xFF89D080u, 0xFF8960C0u, 0xFF8970C0u, 0xFF8980C0u, 0xFF8990C0u,
    0xFF89A0C0u, 0xFF89B0C0u, 0xFF89C0C0u, 0xFF89D0C0u, 0xFF896100u,
    0xFF897100u, 0xFF898100u, 0xFF899100u, 0xFF89A100u, 0xFF89B100u,
    0xFF89C100u, 0xFF89D100u, 0xFF896140u, 0xFF897140u, 0xFF898140u,
    0xFF899140u, 0xFF89A140u, 0xFF89B140u, 0xFF89C140u, 0xFF89D140u,
    0xFF896180u, 0xFF897180u, 0xFF898180u, 0xFF899180u, 0xFF89A180u,
    0xFF89B180u, 0xFF89C180u, 0xFF89D180u, 0xFF8961C0u, 0xFF8971C0u,
    0xFF8981C0u, 0xFF8991C0u, 0xFF89A1C0u, 0xFF89B1C0u, 0xFF89C1C0u,
    0xFF89D1C0u, 0xFF896200u, 0xFF897200u, 0xFF898200u, 0xFF899200u,
    0xFF89A200u, 0xFF89B200u, 0xFF89C200u, 0xFF89D200u, 0xFF896240u,
    0xFF897240u, 0xFF898240u, 0xFF899240u, 0xFF89A240u, 0xFF89B240u,
    0xFF89C240u, 0xFF89D240u, 0xFF896280u, 0xFF897280u, 0xFF898280u,
    0xFF899280u, 0xFF89A280u, 0xFF89B280u, 0xFF89C280u, 0xFF89D280u,
    0xFF8962C0u, 0xFF8972C0u, 0xFF8982C0u, 0xFF8992C0u, 0xFF89A2C0u,
    0xFF89B2C0u, 0xFF89C2C0u, 0xFF89D2C0u, 0xFF896300u, 0xFF897300u,
    0xFF898300u, 0xFF899300u, 0xFF89A300u, 0xFF89B300u, 0xFF89C300u,
    0xFF89D300u, 0xFF896340u, 0xFF897340u, 0xFF898340u, 0xFF899340u,
    0xFF89A340u, 0xFF89B340u, 0xFF89C340u, 0xFF89D340u, 0xFF896380u,
    0xFF897380u, 0xFF898380u, 0xFF899380u, 0xFF89A380u, 0xFF89B380u,
    0xFF89C380u, 0xFF89D380u, 0xFF8963C0u, 0xFF8973C0u, 0xFF8983C0u,
    0xFF8993C0u, 0xFF89A3C0u, 0xFF89B3C0u, 0xFF89C3C0u, 0xFF89D3C0u,
    0xFF896400u, 0xFF897400u, 0xFF898400u, 0xFF899400u, 0xFF89A400u,
    0xFF89B400u, 0xFF89C400u, 0xFF89D400u, 0xFF896440u, 0xFF897440u,
    0xFF898440u, 0xFF899440u, 0xFF89A440u, 0xFF89B440u, 0xFF89C440u,
    0xFF89D440u, 0xFF896480u, 0xFF897480u, 0xFF898480u, 0xFF899480u,
    0xFF89A480u, 0xFF89B480u, 0xFF89C480u, 0xFF89D480u, 0xFF8964C0u,
    0xFF8974C0u, 0xFF8984C0u, 0xFF8994C0u, 0xFF89A4C0u, 0xFF89B4C0u,
    0xFF89C4C0u, 0xFF89D4C0u, 0xFF896500u, 0xFF897500u, 0xFF898500u,
    0xFF899500u, 0xFF89A500u, 0xFF89B500u, 0xFF89C500u, 0xFF89D500u,
    0xFF896540u, 0xFF897540u, 0xFF898540u, 0xFF899540u, 0xFF89A540u,
    0xFF89B540u, 0xFF89C540u, 0xFF89D540u, 0xFF896580u, 0xFF897580u,
    0xFF898580u, 0xFF899580u, 0xFF89A580u, 0xFF89B580u, 0xFF89C580u,
    0xFF89D580u, 0xFF8965C0u, 0xFF8975C0u, 0xFF8985C0u, 0xFF8995C0u,
    0xFF89A5C0u, 0xFF89B5C0u, 0xFF89C5C0u, 0xFF89D5C0u, 0xFF896600u,
    0xFF897600u, 0xFF898600u, 0xFF899600u, 0xFF89A600u, 0xFF89B600u,
    0xFF89C600u, 0xFF89D600u, 0xFF896640u, 0xFF897640u, 0xFF898640u,
    0xFF899640u, 0xFF89A640u, 0xFF89B640u, 0xFF89C640u, 0xFF89D640u,
    0xFF896680u, 0xFF897680u, 0xFF898680u, 0xFF899680u, 0xFF89A680u,
    0xFF89B680u, 0xFF89C680u, 0xFF89D680u, 0xFF8966C0u, 0xFF8976C0u,
    0xFF8986C0u, 0xFF8996C0u, 0xFF89A6C0u, 0xFF89B6C0u, 0xFF89C6C0u,
    0xFF89D6C0u, 0xFF896700u, 0xFF897700u, 0xFF898700u, 0xFF899700u,
    0xFF89A700u, 0xFF89B700u, 0xFF89C700u, 0xFF89D700u, 0xFF896740u,
    0xFF897740u, 0xFF898740u, 0xFF899740u, 0xFF89A740u, 0xFF89B740u,
    0xFF89C740u, 0xFF89D740u, 0xFF896780u, 0xFF897780u, 0xFF898780u,
    0xFF899780u, 0xFF89A780u, 0xFF89B780u, 0xFF89C780u, 0xFF89D780u
};

/* Every 16-bit bus cycle the routine drives for selector $47, from the first
 * glyph row onwards, capped at rows 0-1 of the first character.
 *
 * Reading it: $C00002/$C00004 written with NO reads in front is
 * `move.l D0,$c00002.l` latching a row base; the same pair PRECEDED by a read
 * of each is `addi.l #$10,$c00002.l` stepping one pixel.  $C0000C is HSTDAT and
 * $C00002/$C00004 are HSTADRH/HSTADRL.
 *
 * Row 0 of 'P' is the font word $0555 -- four nibbles of 5 -- so four
 * write / read-back / write triples, then four pixels with no HSTDAT cycle at
 * all because the `beq` at $2662 and the `beq` at $2670 are both taken.  That
 * is how the ROM leaves the background alone.
 *
 * Row 1 is $1405 -- nibbles 5, 0, 4, 1.  Pixel 2 carries only bit 2, so its
 * `or.w D6,$c0000c.l` reads the untouched VRAM word (0) and writes $DF00 on its
 * own; pixel 3 carries only bit 0 and overwrites with $00DF. */
static const cycle_t k_golden[] = {
    { 1, 0x00C00008u, 0x0000u },     { 1, 0x00C00002u, 0xFF89u },     { 1, 0x00C00004u, 0x6040u },
    { 1, 0x00C0000Cu, 0x00DFu },     { 0, 0x00C0000Cu, 0x00DFu },     { 1, 0x00C0000Cu, 0xDFDFu },
    { 0, 0x00C00002u, 0xFF89u },     { 0, 0x00C00004u, 0x6040u },     { 1, 0x00C00002u, 0xFF89u },
    { 1, 0x00C00004u, 0x6050u },     { 1, 0x00C0000Cu, 0x00DFu },     { 0, 0x00C0000Cu, 0x00DFu },
    { 1, 0x00C0000Cu, 0xDFDFu },     { 0, 0x00C00002u, 0xFF89u },     { 0, 0x00C00004u, 0x6050u },
    { 1, 0x00C00002u, 0xFF89u },     { 1, 0x00C00004u, 0x6060u },     { 1, 0x00C0000Cu, 0x00DFu },
    { 0, 0x00C0000Cu, 0x00DFu },     { 1, 0x00C0000Cu, 0xDFDFu },     { 0, 0x00C00002u, 0xFF89u },
    { 0, 0x00C00004u, 0x6060u },     { 1, 0x00C00002u, 0xFF89u },     { 1, 0x00C00004u, 0x6070u },
    { 0, 0x00C00002u, 0xFF89u },     { 0, 0x00C00004u, 0x6070u },     { 1, 0x00C00002u, 0xFF89u },
    { 1, 0x00C00004u, 0x6080u },     { 0, 0x00C00002u, 0xFF89u },     { 0, 0x00C00004u, 0x6080u },
    { 1, 0x00C00002u, 0xFF89u },     { 1, 0x00C00004u, 0x6090u },     { 0, 0x00C00002u, 0xFF89u },
    { 0, 0x00C00004u, 0x6090u },     { 1, 0x00C00002u, 0xFF89u },     { 1, 0x00C00004u, 0x60A0u },
    { 0, 0x00C00002u, 0xFF89u },     { 0, 0x00C00004u, 0x60A0u },     { 1, 0x00C00002u, 0xFF89u },
    { 1, 0x00C00004u, 0x60B0u },     { 0, 0x00C00002u, 0xFF89u },     { 0, 0x00C00004u, 0x60B0u },
    { 1, 0x00C00002u, 0xFF89u },     { 1, 0x00C00004u, 0x60C0u },     { 1, 0x00C00002u, 0xFF89u },
    { 1, 0x00C00004u, 0x7040u },     { 1, 0x00C0000Cu, 0x00DFu },     { 0, 0x00C0000Cu, 0x00DFu },
    { 1, 0x00C0000Cu, 0xDFDFu },     { 0, 0x00C00002u, 0xFF89u },     { 0, 0x00C00004u, 0x7040u },
    { 1, 0x00C00002u, 0xFF89u },     { 1, 0x00C00004u, 0x7050u },     { 0, 0x00C00002u, 0xFF89u },
    { 0, 0x00C00004u, 0x7050u },     { 1, 0x00C00002u, 0xFF89u },     { 1, 0x00C00004u, 0x7060u },
    { 0, 0x00C0000Cu, 0x0000u },     { 1, 0x00C0000Cu, 0xDF00u },     { 0, 0x00C00002u, 0xFF89u },
    { 0, 0x00C00004u, 0x7060u },     { 1, 0x00C00002u, 0xFF89u },     { 1, 0x00C00004u, 0x7070u },
    { 1, 0x00C0000Cu, 0x00DFu },     { 0, 0x00C00002u, 0xFF89u },     { 0, 0x00C00004u, 0x7070u },
    { 1, 0x00C00002u, 0xFF89u },     { 1, 0x00C00004u, 0x7080u },     { 0, 0x00C00002u, 0xFF89u },
    { 0, 0x00C00004u, 0x7080u },     { 1, 0x00C00002u, 0xFF89u },     { 1, 0x00C00004u, 0x7090u },
    { 0, 0x00C00002u, 0xFF89u },     { 0, 0x00C00004u, 0x7090u },     { 1, 0x00C00002u, 0xFF89u },
    { 1, 0x00C00004u, 0x70A0u },     { 0, 0x00C00002u, 0xFF89u },     { 0, 0x00C00004u, 0x70A0u },
    { 1, 0x00C00002u, 0xFF89u },     { 1, 0x00C00004u, 0x70B0u },     { 0, 0x00C00002u, 0xFF89u },
    { 0, 0x00C00004u, 0x70B0u },     { 1, 0x00C00002u, 0xFF89u },     { 1, 0x00C00004u, 0x70C0u }
};

/* ── Helpers ──────────────────────────────────────────────────────────── */

/* Is the write pair starting at cycle `i` a `move.l D0,$c00002.l` rather than
 * the write half of an `addi.l #$10,$c00002.l`?  The `addi` is a long
 * read-modify-write, so it always arrives preceded by a read of $C00002 and
 * then a read of $C00004; the `move.l` has no reads in front of it.  That
 * distinction is the only way to separate the two in a write-only trace, which
 * is what the committed capture is. */
static int is_row_latch(size_t i)
{
    int addi_reads = i >= 2u && !g_cycle[i - 1u].is_write &&
                     !g_cycle[i - 2u].is_write &&
                     g_cycle[i - 1u].address == STUNRUN_TEXT_HSTADR_LO &&
                     g_cycle[i - 2u].address == STUNRUN_TEXT_HSTADR_HI;
    return g_cycle[i].is_write && !addi_reads &&
           g_cycle[i].address == STUNRUN_TEXT_HSTADR_HI &&
           g_cycle[i + 1u].is_write &&
           g_cycle[i + 1u].address == STUNRUN_TEXT_HSTADR_LO;
}

/* Cycle index at which the k-th `move.l D0,$c00002.l` starts, or g_count. */
static size_t latch_cycle_index(size_t k)
{
    size_t i;
    size_t seen = 0u;

    for (i = 0u; i + 1u < g_count; i++) {
        if (is_row_latch(i)) {
            if (seen == k)
                return i;
            seen++;
            i++;
        }
    }
    return g_count;
}

/* Collect the value of every `move.l D0,$c00002.l` in the log.  Returns how
 * many there were. */
static size_t collect_latches(uint32_t *out, size_t capacity)
{
    size_t i;
    size_t found = 0u;

    for (i = 0u; i + 1u < g_count; i++) {
        if (is_row_latch(i)) {
            if (found < capacity)
                out[found] = ((uint32_t)g_cycle[i].value << 16) |
                             (uint32_t)g_cycle[i + 1u].value;
            found++;
            i++;
        }
    }
    return found;
}

static const uint8_t *glyph_bytes(uint8_t character)
{
    size_t i;
    for (i = 0u; i < sizeof(k_glyphs) / sizeof(k_glyphs[0]); i++)
        if (k_glyphs[i].glyph == character)
            return k_glyphs[i].bytes;
    fprintf(stderr, "no fixture glyph for 0x%02X\n", (unsigned)character);
    exit(2);
    return NULL;
}

static uint16_t glyph_row(uint8_t character, unsigned row)
{
    const uint8_t *bytes = glyph_bytes(character);
    return (uint16_t)((bytes[2u * row] << 8) | bytes[2u * row + 1u]);
}

/* ── Tests ────────────────────────────────────────────────────────────── */

/* The oracle check: 240 latches, in order, against the capture. */
static int test_selector_47_matches_capture(void)
{
    static uint32_t latches[512];
    size_t returned;
    size_t count;
    size_t i;

    machine_reset();
    /* Separated: the operands of CHECK_EQ have unspecified evaluation order,
     * so reading g_writes in the same expression as the call would compare
     * the count from the *previous* draw against this one. */
    returned = draw(0x47);
    CHECK_EQ(returned, g_writes);
    CHECK_EQ(returned, 4983u);
    count = collect_latches(latches, sizeof(latches) / sizeof(latches[0]));
    CHECK_EQ(count, sizeof(k_capture_latches) / sizeof(k_capture_latches[0]));
    for (i = 0u; i < count; i++)
        CHECK_EQ(latches[i], k_capture_latches[i]);
    return 0;
}

/* The literal cycle-by-cycle golden for the first two glyph rows. */
static int test_first_character_cycles(void)
{
    size_t i;

    machine_reset();
    CHECK(draw(0x47) != 0u);
    /* g_cycle[0] and g_cycle[1] are the prologue's $608000 write and the HSTCTL
     * write-back; k_golden starts after them. */
    CHECK(g_count > sizeof(k_golden) / sizeof(k_golden[0]) + 2u);
    for (i = 0u; i < sizeof(k_golden) / sizeof(k_golden[0]); i++) {
        CHECK_EQ(g_cycle[i + 2u].is_write, (unsigned)k_golden[i].is_write);
        CHECK_EQ(g_cycle[i + 2u].address, k_golden[i].address);
        CHECK_EQ(g_cycle[i + 2u].value, k_golden[i].value);
    }
    return 0;
}

/* The prologue is part of the observable effect, not decoration: the watchdog
 * write is the first word on the bus, and `andi.w #$e7ff,$c00008.l` clears both
 * HSTCTL post-increment bits that the ROM's own `ori.w #$1800` sites set. */
static int test_prologue_is_observable(void)
{
    machine_reset();
    CHECK(draw(0x47) != 0u);
    CHECK_EQ(g_cycle[0].is_write, 1);
    CHECK_EQ(g_cycle[0].address, 0x00608000u);
    CHECK_EQ(g_cycle[0].value, 0u);
    CHECK_EQ(g_cycle[1].is_write, 0);
    CHECK_EQ(g_cycle[1].address, 0x00C00008u);
    CHECK_EQ(g_cycle[1].value, 0x1800u);
    CHECK_EQ(g_cycle[2].is_write, 1);
    CHECK_EQ(g_cycle[2].address, 0x00C00008u);
    CHECK_EQ(g_cycle[2].value, 0x0000u);
    return 0;
}

static int test_prologue_clears_post_increment(void)
{
    stunrun_mc68010_text_port_t port;

    machine_reset();
    memset(&port, 0, sizeof(port));
    port.hstctl = 0x1800u;
    port.gsp_read = gsp_read;
    port.gsp_write = gsp_write;
    port.bus_read = bus_read;
    port.bus_write = bus_write;
    CHECK(stunrun_mc68010_text_draw(0x47, g_rom, sizeof(g_rom), &port) != 0u);
    /* Both post-increment bits cleared, or every HSTDAT access would move
     * HSTADR as well and the hand-rolled `addi.l #$10` would double-step. */
    CHECK_EQ(port.hstctl, 0x0000u);
    /* Last row latches at $FF89D780 and then takes eight +$10 steps. */
    CHECK_EQ(port.hstadr, 0xFF89D800u);
    return 0;
}

/* Walk every cycle of the selector-$47 call in lockstep against the nibble
 * rule, derived here from the embedded font rather than from the slice.  This
 * is the assertion that covers the 29 characters the literal golden does not
 * spell out, and it pins the order as well as the values. */
static int test_every_cycle_follows_the_font(void)
{
    const uint16_t low = 0x00DFu;
    const uint16_t high = 0xDF00u;
    size_t i = 3u;
    size_t ci;

    machine_reset();
    CHECK(draw(0x47) != 0u);

    for (ci = 0u; ci < 30u; ci++) {
        uint8_t character = k_descriptor[4u + ci];
        unsigned row;
        for (row = 0u; row < 8u; row++) {
            uint16_t bits = glyph_row(character, row);
            uint32_t latch = k_capture_latches[ci * 8u + row];
            unsigned pixel;

            /* $2656 move.l D0,$c00002.l */
            CHECK(i + 1u < g_count);
            CHECK_EQ(g_cycle[i].is_write, 1);
            CHECK_EQ(g_cycle[i].address, STUNRUN_TEXT_HSTADR_HI);
            CHECK_EQ(((uint32_t)g_cycle[i].value << 16) |
                     (uint32_t)g_cycle[i + 1u].value, latch);
            CHECK_EQ(g_cycle[i + 1u].is_write, 1);
            CHECK_EQ(g_cycle[i + 1u].address, STUNRUN_TEXT_HSTADR_LO);
            i += 2u;

            for (pixel = 0u; pixel < 8u; pixel++) {
                uint32_t here = latch + 0x10u * pixel;
                uint32_t stepped = here + 0x10u;
                /* $2666 move.w D7,$c0000c.l: a clean nibble overwrites. */
                if ((bits & 1u) != 0u) {
                    CHECK(i < g_count);
                    CHECK_EQ(g_cycle[i].is_write, 1);
                    CHECK_EQ(g_cycle[i].address, STUNRUN_TEXT_HSTDAT);
                    CHECK_EQ(g_cycle[i].value, low);
                    i++;
                }
                /* $2674 or.w D6,$c0000c.l: a read-modify-write of HSTDAT, so
                 * there is always a read cycle first.  For nibble 5 the read
                 * returns the $00DF `$2666` just wrote; for nibble 4 it
                 * returns the untouched background, 0 in a clean VRAM. */
                if ((bits & 4u) != 0u) {
                    uint16_t base = (bits & 1u) ? low : 0u;
                    CHECK(i < g_count);
                    CHECK_EQ(g_cycle[i].is_write, 0);
                    CHECK_EQ(g_cycle[i].address, STUNRUN_TEXT_HSTDAT);
                    CHECK_EQ(g_cycle[i].value, base);
                    i++;
                    CHECK(i < g_count);
                    CHECK_EQ(g_cycle[i].is_write, 1);
                    CHECK_EQ(g_cycle[i].address, STUNRUN_TEXT_HSTDAT);
                    CHECK_EQ(g_cycle[i].value, base | high);
                    i++;
                }
                /* $267A addi.l #$10,$c00002.l: read both halves of the
                 * DEVICE address, add $10, write both halves back.  The two
                 * reads therefore return the address as it stands BEFORE the
                 * step, and the two writes the address after it. */
                CHECK(i + 3u < g_count);
                CHECK_EQ(g_cycle[i].is_write, 0);
                CHECK_EQ(g_cycle[i].address, STUNRUN_TEXT_HSTADR_HI);
                CHECK_EQ(g_cycle[i].value, (uint16_t)(here >> 16));
                CHECK_EQ(g_cycle[i + 1u].is_write, 0);
                CHECK_EQ(g_cycle[i + 1u].address, STUNRUN_TEXT_HSTADR_LO);
                CHECK_EQ(g_cycle[i + 1u].value, (uint16_t)here);
                CHECK_EQ(g_cycle[i + 2u].is_write, 1);
                CHECK_EQ(g_cycle[i + 2u].address, STUNRUN_TEXT_HSTADR_HI);
                CHECK_EQ(g_cycle[i + 2u].value, (uint16_t)(stepped >> 16));
                CHECK_EQ(g_cycle[i + 3u].is_write, 1);
                CHECK_EQ(g_cycle[i + 3u].address, STUNRUN_TEXT_HSTADR_LO);
                CHECK_EQ(g_cycle[i + 3u].value, (uint16_t)stepped);
                i += 4u;
                bits = (uint16_t)(bits >> 4);
            }
        }
    }
    CHECK_EQ(i, g_count);
    return 0;
}

/* The `or.w` really is a read-modify-write of HSTDAT, so a pixel that carries
 * only bit 2 preserves whatever was already in VRAM.  Seed the first
 * character's eight rows with $0030 and check, inside that character only, that
 * every nibble-4 pixel writes $DF30 and none writes $DF00.  This is the
 * behaviour the older, narrower slice in this file does not model. */
static int test_or_read_modify_write_preserves_background(void)
{
    size_t boundary;
    size_t i;
    size_t df30 = 0u;
    size_t df00 = 0u;

    machine_reset();
    seed_words(0xFF896040u, 0xFF896040u + 0x8000u, 0x0030u);
    CHECK(draw(0x47) != 0u);

    /* The first latch of the SECOND character: the ninth row latch, which is
     * character 0's eight rows done.  Located by latch ordinal, not by value,
     * because the `addi` write half at pixel 3 of character 0 row 0 carries
     * the same $FF896080. */
    boundary = latch_cycle_index(8u);
    CHECK(boundary < g_count);

    for (i = 0u; i < boundary; i++) {
        if (!g_cycle[i].is_write || g_cycle[i].address != STUNRUN_TEXT_HSTDAT)
            continue;
        if (g_cycle[i].value == 0xDF30u)
            df30++;
        if (g_cycle[i].value == 0xDF00u)
            df00++;
    }
    /* 'P' has nibble-4 pixels; none of them may write plain $DF00, because
     * $0030 is still there to be OR-ed with. */
    CHECK(df30 > 0u);
    CHECK_EQ(df00, 0u);
    return 0;
}

/* Exact census of the whole call.  30 characters x 8 rows x
 * (2 latch + 64 addi) bus writes, plus the two prologue writes, plus one or
 * two HSTDAT writes per lit pixel. */
static int test_census(void)
{
    machine_reset();
    CHECK_EQ(draw(0x47), 4983u);
    CHECK_EQ(g_writes, 4983u);
    CHECK_EQ(g_reads, 4171u);
    CHECK_EQ(g_count, 9154u);
    return 0;
}

/* Resolution really goes through the table: selector $46 points at a different
 * descriptor, at a different place, in a different colour. */
static int test_selector_is_an_index(void)
{
    static uint32_t latches[512];
    size_t count;
    size_t i;
    size_t hstdat = 0u;

    machine_reset();
    CHECK(draw(0x46) != 0u);
    count = collect_latches(latches, sizeof(latches) / sizeof(latches[0]));
    /* x=3, y=2, count 2: two characters, eight rows each. */
    CHECK_EQ(count, 16u);
    CHECK_EQ(latches[0], 0xFF8140C0u);
    CHECK_EQ(latches[1], 0xFF8150C0u);
    CHECK_EQ(latches[7], 0xFF81B0C0u);
    CHECK_EQ(latches[8], 0xFF814100u);
    CHECK_EQ(latches[15], 0xFF81B100u);
    for (i = 0u; i < g_count; i++)
        if (g_cycle[i].is_write && g_cycle[i].address == STUNRUN_TEXT_HSTDAT) {
            hstdat++;
            /* Colour $3F, so $003F / $3F00 -- never selector $47's $DF. */
            CHECK(g_cycle[i].value != 0x00DFu && g_cycle[i].value != 0xDFDFu);
            CHECK(g_cycle[i].value != 0xDF00u);
        }
    CHECK_EQ(hstdat, 64u);
    return 0;
}

/* The ROM constants the header exports are the layout facts the slice reads.
 * Assert them against this file's own literals, so a constant that moves
 * without the ROM moving cannot quietly move the fixture with it. */
static int test_rom_constants(void)
{
    uint32_t read_long;

    CHECK_EQ(STUNRUN_TEXT_SELECTOR_TABLE, FIXTURE_SELECTOR_TABLE);
    CHECK_EQ(STUNRUN_TEXT_FONT_BASE, 0x1FBCu);
    CHECK_EQ(STUNRUN_TEXT_GLYPH_STRIDE, 16u);
    CHECK_EQ(STUNRUN_TEXT_GLYPH_BIAS, 0x2Au);
    CHECK_EQ(STUNRUN_TEXT_WATCHDOG, 0x00608000u);
    CHECK_EQ(STUNRUN_TEXT_HSTADR_HI, 0x00C00002u);
    CHECK_EQ(STUNRUN_TEXT_HSTADR_LO, 0x00C00004u);
    CHECK_EQ(STUNRUN_TEXT_HSTCTL, 0x00C00008u);
    CHECK_EQ(STUNRUN_TEXT_HSTDAT, 0x00C0000Cu);
    CHECK_EQ(STUNRUN_TEXT_HSTCTL_KEEP, 0xE7FFu);
    /* And the fixture image really is where this file says it is. */
    read_long = ((uint32_t)g_rom[FIXTURE_SELECTOR_TABLE + 4u * 0x47u + 0u] << 24) |
                ((uint32_t)g_rom[FIXTURE_SELECTOR_TABLE + 4u * 0x47u + 1u] << 16) |
                ((uint32_t)g_rom[FIXTURE_SELECTOR_TABLE + 4u * 0x47u + 2u] << 8) |
                (uint32_t)g_rom[FIXTURE_SELECTOR_TABLE + 4u * 0x47u + 3u];
    CHECK_EQ(read_long, FIXTURE_DESCRIPTOR);
    CHECK_EQ(memcmp(g_rom + FIXTURE_DESCRIPTOR, k_descriptor,
                    sizeof(k_descriptor)), 0);
    return 0;
}

/* The routine refuses to read outside the ROM image it was handed. */
static int test_rom_bounds(void)
{
    stunrun_mc68010_text_port_t port;

    memset(&port, 0, sizeof(port));
    port.hstctl = 0x1800u;
    port.gsp_read = gsp_read;
    port.gsp_write = gsp_write;
    port.bus_read = bus_read;
    port.bus_write = bus_write;

    CHECK_EQ(stunrun_mc68010_text_draw(0x47, NULL, sizeof(g_rom), &port), 0u);
    CHECK_EQ(stunrun_mc68010_text_draw(0x47, g_rom, sizeof(g_rom), NULL), 0u);
    /* $45's table entry points outside the image. */
    CHECK_EQ(stunrun_mc68010_text_draw(FIXTURE_BAD_SELECTOR, g_rom,
                                       sizeof(g_rom), &port), 0u);
    /* A selector whose table slot falls outside a truncated image. */
    CHECK_EQ(stunrun_mc68010_text_draw(0x0189u, g_rom, sizeof(g_rom), &port),
             0u);
    CHECK_EQ(stunrun_mc68010_text_draw(0x047u, g_rom,
                                       FIXTURE_SELECTOR_TABLE + 4u * 0x47u + 3u,
                                       &port), 0u);
    return 0;
}

/* ── The earlier, narrower slice, kept alive ──────────────────────────── */

typedef struct {
    uint32_t address[128];
    uint16_t value[128];
    size_t count;
} legacy_log_t;

static int legacy_capture(void *ctx, uint32_t address, uint16_t value)
{
    legacy_log_t *log = (legacy_log_t *)ctx;
    if (log->count == 128u)
        return 0;
    log->address[log->count] = address;
    log->value[log->count] = value;
    log->count++;
    return 1;
}

/* Kept so that CTest mc68010-text-writer-slice-c does not silently change
 * meaning while the faithful model above is the one to believe.  Note what it
 * asserts: 128 writes for a 2-character draw, eight per glyph row.  The ROM
 * emits one cycle per lit nibble and none at all for a blank one, so this
 * count is neither a pixel count nor a bus-cycle count.  See the header. */
static int test_legacy_slice(void)
{
    static const uint8_t descriptor[] = { 1u, 15u, 0xDFu, 2u, 'A', 'A' };
    uint16_t font[24u * 8u];
    legacy_log_t log;

    memset(font, 0, sizeof(font));
    memset(&log, 0, sizeof(log));
    font[23u * 8u] = 0x0005u; /* 'A' - the ROM's $2A glyph bias */
    CHECK_EQ(stunrun_mc68010_text_write(descriptor, sizeof(descriptor), font,
                                        sizeof(font) / sizeof(font[0]),
                                        legacy_capture, &log), 128u);
    CHECK_EQ(log.count, 128u);
    CHECK_EQ(log.address[0], 0xFF896040u);
    CHECK_EQ(log.value[0], 0xDFDFu);
    CHECK_EQ(log.address[1], 0xFF896050u);
    CHECK_EQ(log.value[1], 0u);
    CHECK_EQ(log.address[7], 0xFF8960B0u);
    CHECK_EQ(log.address[8], 0xFF897040u);
    CHECK_EQ(log.address[63], 0xFF89D0B0u);
    CHECK_EQ(log.address[64], 0xFF896080u);
    return 0;
}

int main(void)
{
    static const struct {
        const char *name;
        int (*fn)(void);
    } tests[] = {
        { "selector_47_matches_capture", test_selector_47_matches_capture },
        { "first_character_cycles", test_first_character_cycles },
        { "prologue_is_observable", test_prologue_is_observable },
        { "prologue_clears_post_increment", test_prologue_clears_post_increment },
        { "every_cycle_follows_the_font", test_every_cycle_follows_the_font },
        { "or_preserves_background", test_or_read_modify_write_preserves_background },
        { "census", test_census },
        { "selector_is_an_index", test_selector_is_an_index },
        { "rom_constants", test_rom_constants },
        { "rom_bounds", test_rom_bounds },
        { "legacy_slice", test_legacy_slice },
    };
    size_t i;
    unsigned failed = 0u;

    build_rom();
    for (i = 0u; i < sizeof(tests) / sizeof(tests[0]); i++) {
        int rc = tests[i].fn();
        printf("%-34s %s\n", tests[i].name, rc == 0 ? "ok" : "FAILED");
        if (rc != 0)
            failed++;
    }
    if (failed != 0u) {
        fprintf(stderr, "%u of %u tests failed\n", failed,
                (unsigned)(sizeof(tests) / sizeof(tests[0])));
        return 1;
    }
    printf("mc68010-text-writer: %u tests ok\n",
           (unsigned)(sizeof(tests) / sizeof(tests[0])));
    return 0;
}