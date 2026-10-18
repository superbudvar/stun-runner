/* ROM-free self-check for the 68010 text output path ($25E8).
 *
 * The observable is the GSP host-port write stream, which is what actually
 * puts pixels on the screen: the routine's only outputs are the HSTCTL clear,
 * HSTADR latches, HSTDAT writes and the watchdog kick. Every expectation below
 * is transcribed from the listing at $25E8..$269E and cross-checked against the
 * two recorded MAME captures quoted in docs/mc68010-boot-findings.md section
 * 22.2 -- never from this port's own output.
 *
 * WRITE INDEXING. Host write 0 is the prologue's HSTCTL clear, so every index
 * below is one past the first pixel write.
 *
 * Exit 0 on success.
 */
#include <stdio.h>
#include <string.h>

#include "text_draw.h"

/* ===================== recording stub board ============================= */

#define REC_ROM_SIZE 0x100000u
#define REC_LOG_MAX  0x20000u

enum {
    ACC_WATCHDOG = 0,
    ACC_ROM8,
    ACC_ROM16,
    ACC_GSP_READ,
    ACC_GSP_WRITE
};

/* The TMS34010 host registers this routine reads back. HSTADR is a 32-bit
 * register whose halves latch separately; HSTDAT holds the last word the port
 * stored; HSTCTL is a plain register. */
typedef struct {
    uint32_t hstadr;
    uint16_t hstctl;
    uint16_t hstdat;
} gsp_regs_t;

typedef struct {
    unsigned kind;
    uint32_t addr;   /* ROM address, or GSP host offset */
    uint16_t value;
} rec_access_t;

typedef struct {
    uint8_t rom[REC_ROM_SIZE];
    rec_access_t *log;
    unsigned stored;    /* accesses kept in log (capped) */
    unsigned total;     /* accesses seen, uncapped */
    gsp_regs_t regs;
    unsigned watchdog;
} rec_board_t;

static rec_access_t g_log[REC_LOG_MAX];
static rec_board_t g_board;

/* The interface the port actually calls, pointed at the recorder above. The
 * members this routine never touches are left NULL on purpose, so a port that
 * reached for one would fault rather than quietly pass. */
static stunrun_board_t g_iface;

static rec_access_t *rec_push(rec_board_t *rb, unsigned kind, uint32_t addr,
                              uint16_t value)
{
    rec_access_t slot;

    slot.kind = kind;
    slot.addr = addr;
    slot.value = value;
    rb->total++;
    if (rb->stored < REC_LOG_MAX)
        rb->log[rb->stored++] = slot;
    return &rb->log[rb->stored - 1u];
}

/* Clears the log and the device registers. The ROM image is left alone. */
static void rec_begin(rec_board_t *rb, uint16_t hstctl_init)
{
    rb->log = g_log;
    rb->stored = 0u;
    rb->total = 0u;
    rb->watchdog = 0u;
    rb->regs.hstadr = 0u;
    rb->regs.hstctl = hstctl_init;
    rb->regs.hstdat = 0u;
}

static uint8_t rec_read_rom8(void *ctx, uint32_t addr)
{
    rec_board_t *rb = (rec_board_t *)ctx;
    uint8_t v = 0u;

    if (addr < REC_ROM_SIZE)
        v = rb->rom[addr];
    rec_push(rb, ACC_ROM8, addr, v);
    return v;
}

static uint16_t rec_read_rom16(void *ctx, uint32_t addr)
{
    rec_board_t *rb = (rec_board_t *)ctx;
    uint16_t v = 0u;

    if (addr + 1u < REC_ROM_SIZE)
        v = (uint16_t)(((uint16_t)rb->rom[addr] << 8) | rb->rom[addr + 1u]);
    rec_push(rb, ACC_ROM16, addr, v);
    return v;
}

static uint16_t rec_read_gsp_control(void *ctx, uint16_t offset)
{
    rec_board_t *rb = (rec_board_t *)ctx;
    uint16_t v = 0u;

    switch (offset) {
    case STUNRUN_TEXT_HSTADR_H:
        v = (uint16_t)((rb->regs.hstadr >> 16) & 0xFFFFu);
        break;
    case STUNRUN_TEXT_HSTADR_L:
        v = (uint16_t)(rb->regs.hstadr & 0xFFFFu);
        break;
    case STUNRUN_TEXT_HSTCTL:
        v = rb->regs.hstctl;
        break;
    case STUNRUN_TEXT_HSTDAT:
        v = rb->regs.hstdat;
        break;
    default:
        break;
    }
    rec_push(rb, ACC_GSP_READ, offset, v);
    return v;
}

static void rec_write_gsp_host(void *ctx, uint16_t offset, uint16_t value)
{
    rec_board_t *rb = (rec_board_t *)ctx;

    switch (offset) {
    case STUNRUN_TEXT_HSTADR_H:
        rb->regs.hstadr = (rb->regs.hstadr & 0x0000FFFFu)
                       | ((uint32_t)value << 16);
        break;
    case STUNRUN_TEXT_HSTADR_L:
        rb->regs.hstadr = (rb->regs.hstadr & 0xFFFF0000u) | value;
        break;
    case STUNRUN_TEXT_HSTCTL:
        rb->regs.hstctl = value;
        break;
    case STUNRUN_TEXT_HSTDAT:
        rb->regs.hstdat = value;
        break;
    default:
        break;
    }
    rec_push(rb, ACC_GSP_WRITE, offset, value);
}

static void rec_write_watchdog(void *ctx)
{
    rec_board_t *rb = (rec_board_t *)ctx;

    rb->watchdog++;
    rec_push(rb, ACC_WATCHDOG, 0u, 0u);
}

static void rec_board_init(rec_board_t *rb)
{
    memset(rb, 0, sizeof(*rb));
    rec_begin(rb, 0xFFFFu);

    memset(&g_iface, 0, sizeof(g_iface));
    g_iface.ctx = rb;
    g_iface.read_rom8 = rec_read_rom8;
    g_iface.read_rom16 = rec_read_rom16;
    g_iface.read_gsp_control = rec_read_gsp_control;
    g_iface.write_gsp_host = rec_write_gsp_host;
    g_iface.write_watchdog = rec_write_watchdog;
}

/* ===================== synthetic ROM fixtures =========================== */

static void put_be32(uint8_t *rom, uint32_t addr, uint32_t value)
{
    rom[addr + 0u] = (uint8_t)((value >> 24) & 0xFFu);
    rom[addr + 1u] = (uint8_t)((value >> 16) & 0xFFu);
    rom[addr + 2u] = (uint8_t)((value >> 8) & 0xFFu);
    rom[addr + 3u] = (uint8_t)(value & 0xFFu);
}

/* Points string `index` at `addr` in the descriptor table. */
static void put_table_entry(uint8_t *rom, uint16_t index, uint32_t addr)
{
    put_be32(rom, STUNRUN_STRING_TABLE + (uint32_t)index * 4u, addr);
}

/* Four-byte descriptor: x, y, colour, character count, then the text bytes
 * and the `0x01` terminator that this routine never looks at. */
static void put_record(uint8_t *rom, uint32_t addr, uint8_t x, uint8_t y,
                       uint8_t colour, uint8_t count, const char *text)
{
    size_t len = strlen(text);
    size_t i;

    rom[addr + 0u] = x;
    rom[addr + 1u] = y;
    rom[addr + 2u] = colour;
    rom[addr + 3u] = count;
    for (i = 0u; i < len; i++)
        rom[addr + STUNRUN_TEXT_HEADER_SIZE + i] = (uint8_t)text[i];
    rom[addr + STUNRUN_TEXT_HEADER_SIZE + len] = 0x01u;
}

/* 16 bytes of glyph at $01FBC + 16*(ch-$2A): one 16-bit word per row. The
 * index is a byte, matching `subi.b #$2A,D1`: a character below '*' wraps. */
static void put_glyph(uint8_t *rom, uint8_t ch, const uint16_t words[8])
{
    uint32_t index = (uint32_t)(uint8_t)((uint8_t)ch
                                - (uint8_t)STUNRUN_TEXT_CHAR_BIAS);
    uint32_t addr = STUNRUN_FONT_TABLE + 16u * index;
    unsigned i;

    for (i = 0u; i < 8u; i++) {
        rom[addr + 2u * i] = (uint8_t)((words[i] >> 8) & 0xFFu);
        rom[addr + 2u * i + 1u] = (uint8_t)(words[i] & 0xFFu);
    }
}

/* 'P' as it stands at $0221C in the ROM image, eight words, one per row. */
static const uint16_t kGlyphP[8] = {
    0x0555u, 0x1405u, 0x1405u, 0x1405u, 0x0555u, 0x0005u, 0x0005u, 0x0000u
};
static const uint16_t kGlyphBlank[8] = {
    0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x0000u, 0x0000u
};
static const uint16_t kGlyphA[8] = {
    0x0150u, 0x0514u, 0x1405u, 0x1405u, 0x1555u, 0x1405u, 0x1405u, 0x0000u
};

/* ===================== log inspection helpers =========================== */

static unsigned count_writes(rec_board_t *rb, uint16_t offset)
{
    unsigned i;
    unsigned n = 0u;

    for (i = 0u; i < rb->stored; i++)
        if (rb->log[i].kind == ACC_GSP_WRITE && rb->log[i].addr == offset)
            n++;
    return n;
}

static unsigned count_writes_value(rec_board_t *rb, uint16_t offset,
                                   uint16_t value)
{
    unsigned i;
    unsigned n = 0u;

    for (i = 0u; i < rb->stored; i++)
        if (rb->log[i].kind == ACC_GSP_WRITE && rb->log[i].addr == offset
            && rb->log[i].value == value)
            n++;
    return n;
}

/* The nth host write, in order. */
static rec_access_t write_at(rec_board_t *rb, unsigned n)
{
    unsigned i;
    unsigned seen = 0u;

    for (i = 0u; i < rb->stored; i++) {
        if (rb->log[i].kind != ACC_GSP_WRITE)
            continue;
        if (seen == n)
            return rb->log[i];
        seen++;
    }
    return rb->log[rb->stored - 1u];
}

/* HSTADR latches are the halfword pair at offset $C00002 then $C00004, and
 * HSTDAT writes interleave with them, so a latch is indexed separately from
 * the raw write stream. Returns the nth latch in order. */
static uint32_t latch_at(rec_board_t *rb, unsigned n)
{
    unsigned i;
    unsigned seen = 0u;

    for (i = 0u; i + 1u < rb->stored; i++) {
        if (rb->log[i].kind != ACC_GSP_WRITE
            || rb->log[i].addr != STUNRUN_TEXT_HSTADR_H)
            continue;
        if (seen == n)
            return ((uint32_t)rb->log[i].value << 16)
                 | (uint32_t)rb->log[i + 1u].value;
        seen++;
    }
    return 0xDEADBEEFu;
}

static unsigned count_latches(rec_board_t *rb)
{
    return count_writes(rb, STUNRUN_TEXT_HSTADR_H);
}

static int write_is(rec_board_t *rb, unsigned n, uint16_t off, uint16_t val)
{
    rec_access_t w = write_at(rb, n);

    return w.kind == ACC_GSP_WRITE && w.addr == off && w.value == val;
}

static unsigned count_reads(rec_board_t *rb, uint32_t addr)
{
    unsigned i;
    unsigned n = 0u;

    for (i = 0u; i < rb->stored; i++)
        if ((rb->log[i].kind == ACC_ROM8 || rb->log[i].kind == ACC_ROM16)
            && rb->log[i].addr == addr)
            n++;
    return n;
}

static unsigned g_failures = 0;

#define check(cond) \
    do { \
        if (cond) { \
            printf("PASS %s\n", #cond); \
        } else { \
            printf("FAIL %s\n", #cond); \
            g_failures++; \
        } \
    } while (0)

/* ===================== the transcribed write stream ===================== */

/* Host write 0 is `$25EE andi.w #$E7FF,$C00008`.
 *
 * Then descriptor `01 0F DF 01` + 'P'. Predicted base:
 *   ((0x0F * 0xA0) << 8) + (0x01 << 6) - 0x800000 = $FF896040
 * which is the base the MAME capture records for the real descriptor $06C53
 * (`01 0F DF 1E`). Glyph 'P' rows, read from $0221C:
 *   0555 1405 1405 1405 0555 0005 0005 0000
 * Nibbles LSB first, so row 0 is 5,5,5,0,0,0,0,0 -- three pixels each costing
 * two HSTDAT writes ($00DF then $DFDF) and five costing none, while all eight
 * still step HSTADR by $10. Row 1 is 5,0,4,1: the nibble 4 writes only the OR.
 *
 * Per row: 1 HSTADR latch plus 8 pixel steps, each two halfword writes, so 18
 * writes per row; plus 6+4+4+4+6+2+2+0 = 28 HSTDAT writes over the cell.
 * 172 + 1 = 173 host writes in all.
 */
typedef struct {
    uint16_t offset;
    uint16_t value;
} host_write_t;

static const host_write_t kExpectedP[173] = {
    { 0x0008u, 0xE7FFu },  { 0x0002u, 0xFF89u },  { 0x0004u, 0x6040u },  { 0x000Cu, 0x00DFu },
    { 0x000Cu, 0xDFDFu },  { 0x0002u, 0xFF89u },  { 0x0004u, 0x6050u },  { 0x000Cu, 0x00DFu },
    { 0x000Cu, 0xDFDFu },  { 0x0002u, 0xFF89u },  { 0x0004u, 0x6060u },  { 0x000Cu, 0x00DFu },
    { 0x000Cu, 0xDFDFu },  { 0x0002u, 0xFF89u },  { 0x0004u, 0x6070u },  { 0x0002u, 0xFF89u },
    { 0x0004u, 0x6080u },  { 0x0002u, 0xFF89u },  { 0x0004u, 0x6090u },  { 0x0002u, 0xFF89u },
    { 0x0004u, 0x60A0u },  { 0x0002u, 0xFF89u },  { 0x0004u, 0x60B0u },  { 0x0002u, 0xFF89u },
    { 0x0004u, 0x60C0u },  { 0x0002u, 0xFF89u },  { 0x0004u, 0x7040u },  { 0x000Cu, 0x00DFu },
    { 0x000Cu, 0xDFDFu },  { 0x0002u, 0xFF89u },  { 0x0004u, 0x7050u },  { 0x0002u, 0xFF89u },
    { 0x0004u, 0x7060u },  { 0x000Cu, 0xDFDFu },  { 0x0002u, 0xFF89u },  { 0x0004u, 0x7070u },
    { 0x000Cu, 0x00DFu },  { 0x0002u, 0xFF89u },  { 0x0004u, 0x7080u },  { 0x0002u, 0xFF89u },
    { 0x0004u, 0x7090u },  { 0x0002u, 0xFF89u },  { 0x0004u, 0x70A0u },  { 0x0002u, 0xFF89u },
    { 0x0004u, 0x70B0u },  { 0x0002u, 0xFF89u },  { 0x0004u, 0x70C0u },  { 0x0002u, 0xFF89u },
    { 0x0004u, 0x8040u },  { 0x000Cu, 0x00DFu },  { 0x000Cu, 0xDFDFu },  { 0x0002u, 0xFF89u },
    { 0x0004u, 0x8050u },  { 0x0002u, 0xFF89u },  { 0x0004u, 0x8060u },  { 0x000Cu, 0xDFDFu },
    { 0x0002u, 0xFF89u },  { 0x0004u, 0x8070u },  { 0x000Cu, 0x00DFu },  { 0x0002u, 0xFF89u },
    { 0x0004u, 0x8080u },  { 0x0002u, 0xFF89u },  { 0x0004u, 0x8090u },  { 0x0002u, 0xFF89u },
    { 0x0004u, 0x80A0u },  { 0x0002u, 0xFF89u },  { 0x0004u, 0x80B0u },  { 0x0002u, 0xFF89u },
    { 0x0004u, 0x80C0u },  { 0x0002u, 0xFF89u },  { 0x0004u, 0x9040u },  { 0x000Cu, 0x00DFu },
    { 0x000Cu, 0xDFDFu },  { 0x0002u, 0xFF89u },  { 0x0004u, 0x9050u },  { 0x0002u, 0xFF89u },
    { 0x0004u, 0x9060u },  { 0x000Cu, 0xDFDFu },  { 0x0002u, 0xFF89u },  { 0x0004u, 0x9070u },
    { 0x000Cu, 0x00DFu },  { 0x0002u, 0xFF89u },  { 0x0004u, 0x9080u },  { 0x0002u, 0xFF89u },
    { 0x0004u, 0x9090u },  { 0x0002u, 0xFF89u },  { 0x0004u, 0x90A0u },  { 0x0002u, 0xFF89u },
    { 0x0004u, 0x90B0u },  { 0x0002u, 0xFF89u },  { 0x0004u, 0x90C0u },  { 0x0002u, 0xFF89u },
    { 0x0004u, 0xA040u },  { 0x000Cu, 0x00DFu },  { 0x000Cu, 0xDFDFu },  { 0x0002u, 0xFF89u },
    { 0x0004u, 0xA050u },  { 0x000Cu, 0x00DFu },  { 0x000Cu, 0xDFDFu },  { 0x0002u, 0xFF89u },
    { 0x0004u, 0xA060u },  { 0x000Cu, 0x00DFu },  { 0x000Cu, 0xDFDFu },  { 0x0002u, 0xFF89u },
    { 0x0004u, 0xA070u },  { 0x0002u, 0xFF89u },  { 0x0004u, 0xA080u },  { 0x0002u, 0xFF89u },
    { 0x0004u, 0xA090u },  { 0x0002u, 0xFF89u },  { 0x0004u, 0xA0A0u },  { 0x0002u, 0xFF89u },
    { 0x0004u, 0xA0B0u },  { 0x0002u, 0xFF89u },  { 0x0004u, 0xA0C0u },  { 0x0002u, 0xFF89u },
    { 0x0004u, 0xB040u },  { 0x000Cu, 0x00DFu },  { 0x000Cu, 0xDFDFu },  { 0x0002u, 0xFF89u },
    { 0x0004u, 0xB050u },  { 0x0002u, 0xFF89u },  { 0x0004u, 0xB060u },  { 0x0002u, 0xFF89u },
    { 0x0004u, 0xB070u },  { 0x0002u, 0xFF89u },  { 0x0004u, 0xB080u },  { 0x0002u, 0xFF89u },
    { 0x0004u, 0xB090u },  { 0x0002u, 0xFF89u },  { 0x0004u, 0xB0A0u },  { 0x0002u, 0xFF89u },
    { 0x0004u, 0xB0B0u },  { 0x0002u, 0xFF89u },  { 0x0004u, 0xB0C0u },  { 0x0002u, 0xFF89u },
    { 0x0004u, 0xC040u },  { 0x000Cu, 0x00DFu },  { 0x000Cu, 0xDFDFu },  { 0x0002u, 0xFF89u },
    { 0x0004u, 0xC050u },  { 0x0002u, 0xFF89u },  { 0x0004u, 0xC060u },  { 0x0002u, 0xFF89u },
    { 0x0004u, 0xC070u },  { 0x0002u, 0xFF89u },  { 0x0004u, 0xC080u },  { 0x0002u, 0xFF89u },
    { 0x0004u, 0xC090u },  { 0x0002u, 0xFF89u },  { 0x0004u, 0xC0A0u },  { 0x0002u, 0xFF89u },
    { 0x0004u, 0xC0B0u },  { 0x0002u, 0xFF89u },  { 0x0004u, 0xC0C0u },  { 0x0002u, 0xFF89u },
    { 0x0004u, 0xD040u },  { 0x0002u, 0xFF89u },  { 0x0004u, 0xD050u },  { 0x0002u, 0xFF89u },
    { 0x0004u, 0xD060u },  { 0x0002u, 0xFF89u },  { 0x0004u, 0xD070u },  { 0x0002u, 0xFF89u },
    { 0x0004u, 0xD080u },  { 0x0002u, 0xFF89u },  { 0x0004u, 0xD090u },  { 0x0002u, 0xFF89u },
    { 0x0004u, 0xD0A0u },  { 0x0002u, 0xFF89u },  { 0x0004u, 0xD0B0u },  { 0x0002u, 0xFF89u },
    { 0x0004u, 0xD0C0u },
};

/* The ROM reads for the same case, in the order the routine performs them:
 * the pointer table longword, then the descriptor fields colour, y, x, count
 * (that order: $260A $2614 $2620 $262E), then the text byte, then one glyph
 * word per row. */
typedef struct {
    unsigned kind;
    uint32_t addr;
} rom_read_t;

static const rom_read_t kExpectedReadsP[15] = {
    { ACC_ROM16, 0x00010136u },
    { ACC_ROM16, 0x00010138u },
    { ACC_ROM8,  0x00008002u },
    { ACC_ROM8,  0x00008001u },
    { ACC_ROM8,  0x00008000u },
    { ACC_ROM8,  0x00008003u },
    { ACC_ROM8,  0x00008004u },
    { ACC_ROM16, 0x0000221Cu },
    { ACC_ROM16, 0x0000221Eu },
    { ACC_ROM16, 0x00002220u },
    { ACC_ROM16, 0x00002222u },
    { ACC_ROM16, 0x00002224u },
    { ACC_ROM16, 0x00002226u },
    { ACC_ROM16, 0x00002228u },
    { ACC_ROM16, 0x0000222Au }
};

/* Per row: `move.l D0,$c00002.l` latches once, then `addi.l #$10,$c00002.l`
 * latches once per pixel -- 9 HSTADR latches, 18 halfword writes, plus however
 * many HSTDAT writes the row's nibbles call for. */
#define LATCHES_PER_ROW  (1u + STUNRUN_TEXT_PIXELS)
#define LATCHES_PER_CELL (LATCHES_PER_ROW * STUNRUN_TEXT_ROWS)
#define WRITES_PER_CELL  (2u * LATCHES_PER_CELL)
#define FIRST_WRITE      1u   /* host write 0 is the HSTCTL clear */

/* ===================== tests ============================================= */

static void test_full_write_stream(void)
{
    rec_board_t *rb = &g_board;
    unsigned i;
    unsigned n = 0u;
    unsigned seen_reads = 0u;
    int same = 1;

    memset(rb->rom, 0, sizeof(rb->rom));
    put_table_entry(rb->rom, 0u, 0x00008000u);
    put_record(rb->rom, 0x00008000u, 0x01u, 0x0Fu, 0xDFu, 0x01u, "P");
    put_glyph(rb->rom, 'P', kGlyphP);
    rec_begin(rb, 0xFFFFu);

    check(stunrun_text_draw(&g_iface, 0u) == 1u);

    for (i = 0u; i < rb->stored; i++) {
        if (rb->log[i].kind != ACC_GSP_WRITE)
            continue;
        if (n >= sizeof(kExpectedP) / sizeof(kExpectedP[0])) {
            same = 0;
            break;
        }
        if (rb->log[i].addr != kExpectedP[n].offset
            || rb->log[i].value != kExpectedP[n].value) {
            printf("  write %u: got %04X=%04X want %04X=%04X\n", n,
                   (unsigned)rb->log[i].addr, (unsigned)rb->log[i].value,
                   (unsigned)kExpectedP[n].offset,
                   (unsigned)kExpectedP[n].value);
            same = 0;
            break;
        }
        n++;
    }
    check(same);
    check(n == sizeof(kExpectedP) / sizeof(kExpectedP[0]));

    /* The same stream again as spot values, so a shared misreading of the
     * table above still leaves these checked on their own. */
    check(write_is(rb, 0u, STUNRUN_TEXT_HSTCTL, 0xE7FFu));
    check(write_is(rb, 1u, STUNRUN_TEXT_HSTADR_H, 0xFF89u));
    check(write_is(rb, 2u, STUNRUN_TEXT_HSTADR_L, 0x6040u));
    check(write_is(rb, 3u, STUNRUN_TEXT_HSTDAT, 0x00DFu));
    check(write_is(rb, 4u, STUNRUN_TEXT_HSTDAT, 0xDFDFu));
    check(write_is(rb, 5u, STUNRUN_TEXT_HSTADR_H, 0xFF89u));
    check(write_is(rb, 6u, STUNRUN_TEXT_HSTADR_L, 0x6050u));
    check(write_is(rb, 171u, STUNRUN_TEXT_HSTADR_H, 0xFF89u));
    check(write_is(rb, 172u, STUNRUN_TEXT_HSTADR_L, 0xD0C0u));

    /* The ROM read order, for the same case. */
    for (i = 0u; i < rb->stored; i++) {
        if (rb->log[i].kind != ACC_ROM8 && rb->log[i].kind != ACC_ROM16)
            continue;
        if (seen_reads >= sizeof(kExpectedReadsP) / sizeof(kExpectedReadsP[0])
            || rb->log[i].kind != kExpectedReadsP[seen_reads].kind
            || rb->log[i].addr != kExpectedReadsP[seen_reads].addr) {
            printf("  read %u: got kind %u addr %06X\n", seen_reads,
                   rb->log[i].kind, (unsigned)rb->log[i].addr);
            g_failures++;
            break;
        }
        seen_reads++;
    }
    check(seen_reads == sizeof(kExpectedReadsP) / sizeof(kExpectedReadsP[0]));

    check(count_writes(rb, STUNRUN_TEXT_HSTADR_H) == LATCHES_PER_CELL);
    check(count_writes(rb, STUNRUN_TEXT_HSTADR_L) == LATCHES_PER_CELL);
    check(count_writes(rb, STUNRUN_TEXT_HSTDAT) == 28u);
    check(count_writes(rb, STUNRUN_TEXT_HSTCTL) == 1u);
}

static void test_coordinate_and_steps(void)
{
    rec_board_t *rb = &g_board;
    unsigned row;
    unsigned pixel;
    int rows_ok = 1;
    int pixels_ok = 1;
    uint32_t base = 0xFF896040u;

    rec_begin(rb, 0xFFFFu);
    (void)stunrun_text_draw(&g_iface, 0u);

    /* Row r of the cell is latched at base + r*$1000. `move.l D0,$c00002.l`
     * re-latches D0 once per row, so the walking device HSTADR from the
     * previous row's eight steps is discarded rather than added to. */
    for (row = 0u; row < STUNRUN_TEXT_ROWS; row++) {
        uint32_t want = base + (uint32_t)row * STUNRUN_TEXT_ROW_STEP;
        uint32_t got = latch_at(rb, row * LATCHES_PER_ROW);

        if (got != want) {
            printf("  row %u latch: got %08X want %08X\n", row, (unsigned)got,
                   (unsigned)want);
            rows_ok = 0;
        }
    }
    check(rows_ok);

    /* Inside a row the device HSTADR walks +$10 per pixel, eight times, from
     * the row latch -- `addi.l #$10,$c00002.l` at $267A. */
    for (row = 0u; row < STUNRUN_TEXT_ROWS; row++) {
        for (pixel = 0u; pixel <= STUNRUN_TEXT_PIXELS; pixel++) {
            uint32_t got = latch_at(rb, row * LATCHES_PER_ROW + pixel);
            uint32_t want = base + (uint32_t)row * STUNRUN_TEXT_ROW_STEP
                          + (uint32_t)pixel * STUNRUN_TEXT_PIXEL_STEP;

            if (got != want) {
                printf("  row %u pixel %u: got %08X want %08X\n", row, pixel,
                       (unsigned)got, (unsigned)want);
                pixels_ok = 0;
            }
        }
    }
    check(pixels_ok);

    /* The last latch of the cell is the eighth pixel step of row 7. The
     * `subi.l #$7FC0,D0` at $2694 that turns eight rows of stride into a $40
     * cell advance happens after it, and is asserted in test_loop_nest. */
    check(latch_at(rb, LATCHES_PER_CELL - 1u)
          == base + (uint32_t)(STUNRUN_TEXT_ROWS - 1u) * STUNRUN_TEXT_ROW_STEP
          + (uint32_t)STUNRUN_TEXT_PIXELS * STUNRUN_TEXT_PIXEL_STEP);
}

static void test_coordinate_formula(void)
{
    rec_board_t *rb = &g_board;
    unsigned before = g_failures;
    unsigned i;

    /* Descriptors taken from the ROM; the first two are the ones the recorded
     * capture resolves, with the base it records. */
    static const struct {
        uint8_t x;
        uint8_t y;
        uint32_t want;
        const char *what;
    } cases[] = {
        { 0x01u, 0x0Fu, 0xFF896040u, "$06C53, capture first latch $FF896040" },
        { 0x01u, 0x00u, 0xFF800040u, "$06BE7, capture first latch $FF800040" },
        { 0x00u, 0x00u, 0xFF800000u, "origin" },
        { 0x00u, 0x01u, 0xFF80A000u, "one row down" },
        { 0x01u, 0x00u, 0xFF800040u, "one column right" },
        { 0xFFu, 0xFFu, 0x001F9FC0u, "far corner, wrapping past $800000" }
    };

    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); i++) {
        uint32_t want = (((uint32_t)cases[i].y * STUNRUN_TEXT_Y_STRIDE) << 8)
                      + ((uint32_t)cases[i].x << STUNRUN_TEXT_X_SHIFT)
                      - STUNRUN_TEXT_BIAS;
        uint32_t got;

        put_table_entry(rb->rom, 0u, 0x00008000u);
        put_record(rb->rom, 0x00008000u, cases[i].x, cases[i].y, 0xFFu, 1u, "A");
        put_glyph(rb->rom, 'A', kGlyphA);
        rec_begin(rb, 0xFFFFu);
        (void)stunrun_text_draw(&g_iface, 0u);
        got = latch_at(rb, 0u);

        if (want != cases[i].want) {
            printf("  case %u: formula says %08X, transcribed case says %08X\n",
                   i, (unsigned)want, (unsigned)cases[i].want);
            g_failures++;
        }
        if (got != cases[i].want) {
            printf("  case %u (%s): got %08X want %08X\n", i, cases[i].what,
                   (unsigned)got, (unsigned)cases[i].want);
            g_failures++;
        }
    }
    check(g_failures == before);
}

static void test_star_is_the_blank(void)
{
    rec_board_t *rb = &g_board;
    unsigned space_writes;
    unsigned row;
    int blank_reads_ok = 1;
    int space_reads_ok = 1;

    memset(rb->rom, 0, sizeof(rb->rom));
    put_glyph(rb->rom, '*', kGlyphBlank);
    put_glyph(rb->rom, 'A', kGlyphA);
    put_glyph(rb->rom, ' ', kGlyphP);   /* anything but zero, so it can be
                                        * told apart from the blank glyph */

    /* `subi.b #$2A,D1` at $263A maps '*' to glyph index 0, whose sixteen bytes
     * are all zero -- that is how '*' renders as a space. ASCII ' ' is NOT the
     * blank: the byte subtraction underflows to $F6 and selects glyph $F6 at
     * $02F1C. */
    check((uint32_t)(uint8_t)((uint8_t)'*' - (uint8_t)STUNRUN_TEXT_CHAR_BIAS)
          == 0u);
    check((uint32_t)(uint8_t)((uint8_t)' ' - (uint8_t)STUNRUN_TEXT_CHAR_BIAS)
          == 0xF6u);
    check(STUNRUN_FONT_TABLE + 16u * 0u == 0x00001FBCu);
    check(STUNRUN_FONT_TABLE + 16u * 0xF6u == 0x00002F1Cu);

    /* A one-character record of '*': the glyph is read from $01FBC and
     * nothing at all is written to HSTDAT. */
    put_table_entry(rb->rom, 0u, 0x00008000u);
    put_record(rb->rom, 0x00008000u, 0x01u, 0x0Fu, 0xDFu, 0x01u, "*");
    rec_begin(rb, 0xFFFFu);
    check(stunrun_text_draw(&g_iface, 0u) == 1u);
    check(count_writes(rb, STUNRUN_TEXT_HSTDAT) == 0u);
    /* Still eight rows and eight steps: a blank walks HSTADR across the cell
     * and touches no pixel, which is why a cell is $40 and not $80 wide. */
    check(count_latches(rb) == LATCHES_PER_CELL);
    for (row = 0u; row < STUNRUN_TEXT_ROWS; row++)
        if (count_reads(rb, 0x00001FBCu + 2u * row) != 1u)
            blank_reads_ok = 0;
    check(blank_reads_ok);

    /* The same cell with ASCII ' ' reads a different glyph and does write. */
    put_record(rb->rom, 0x00008000u, 0x01u, 0x0Fu, 0xDFu, 0x01u, " ");
    rec_begin(rb, 0xFFFFu);
    check(stunrun_text_draw(&g_iface, 0u) == 1u);
    space_writes = count_writes(rb, STUNRUN_TEXT_HSTDAT);
    check(space_writes == 28u);              /* the 'P' words, at glyph $F6 */
    for (row = 0u; row < STUNRUN_TEXT_ROWS; row++)
        if (count_reads(rb, 0x00002F1Cu + 2u * row) != 1u)
            space_reads_ok = 0;
    check(space_reads_ok);
}

static void test_loop_nest(void)
{
    rec_board_t *rb = &g_board;
    unsigned before = g_failures;
    unsigned cell;
    unsigned row;
    int glyph_reads_ok;
    uint32_t base = (((uint32_t)0x03u * STUNRUN_TEXT_Y_STRIDE) << 8)
                  + ((uint32_t)0x02u << STUNRUN_TEXT_X_SHIFT)
                  - STUNRUN_TEXT_BIAS;

    /* Three characters, three cells; each cell eight rows of eight pixel
     * steps, and the glyph word read once per ROW. */
    put_table_entry(rb->rom, 0u, 0x00008000u);
    put_record(rb->rom, 0x00008000u, 0x02u, 0x03u, 0x7Fu, 0x03u, "APA");
    put_glyph(rb->rom, 'A', kGlyphA);
    put_glyph(rb->rom, 'P', kGlyphP);
    rec_begin(rb, 0xFFFFu);

    check(stunrun_text_draw(&g_iface, 0u) == 3u);
    check(count_latches(rb) == 3u * LATCHES_PER_CELL);
    /* `move.w (A1)+,D5` is outside the pixel loop: one glyph word per ROW, so
     * 'P' at $0221C is read once per row of each of its cells and 'A' at
     * $0212C once per row of its two. */
    /* `move.w (A1)+,D5` at $265C is OUTSIDE the pixel loop: one glyph word per
     * ROW, at $01FBC + 16*(ch-$2A) + 2*row. 'P' appears in one cell, so its
     * eight words are read once each; 'A' appears in two, so twice each. */
    glyph_reads_ok = 1;
    for (row = 0u; row < STUNRUN_TEXT_ROWS; row++) {
        if (count_reads(rb, 0x0000221Cu + 2u * row) != 1u)
            glyph_reads_ok = 0;
        if (count_reads(rb, 0x0000212Cu + 2u * row) != 2u)
            glyph_reads_ok = 0;
    }
    check(glyph_reads_ok);

    /* `subi.l #$7FC0,D0` at $2694 undoes the eight rows of stride ($8000) and adds
     * one four-pixel cell, so the net step to the next cell is $8000-$7FC0 =
     * $40 -- one row base to the right for every character. */
    for (cell = 0u; cell < 3u; cell++) {
        for (row = 0u; row < STUNRUN_TEXT_ROWS; row++) {
            uint32_t got = latch_at(rb, cell * LATCHES_PER_CELL
                                        + row * LATCHES_PER_ROW);
            uint32_t want = base
                          + (uint32_t)cell
                            * ((uint32_t)STUNRUN_TEXT_ROWS
                               * STUNRUN_TEXT_ROW_STEP
                               - STUNRUN_TEXT_CELL_REWIND)
                          + (uint32_t)row * STUNRUN_TEXT_ROW_STEP;

            if (got != want) {
                printf("  cell %u row %u: got %08X want %08X\n", cell, row,
                       (unsigned)got, (unsigned)want);
                g_failures++;
            }
        }
    }
    check(g_failures == before);
}

static void test_count_field_and_quirk(void)
{
    rec_board_t *rb = &g_board;

    /* hdr[3] is the CHARACTER COUNT, not a row count: two characters, two
     * cells. */
    put_table_entry(rb->rom, 0u, 0x00008000u);
    put_record(rb->rom, 0x00008000u, 0x02u, 0x03u, 0x7Fu, 0x02u, "AP");
    rec_begin(rb, 0xFFFFu);
    check(stunrun_text_draw(&g_iface, 0u) == 2u);
    check(count_latches(rb) == 2u * LATCHES_PER_CELL);
    /* The `0x01` terminator is never read: two cells, two text bytes. */
    check(count_reads(rb, 0x00008006u) == 0u);

    /* A count of zero: `subq.b #1,D3` at $269A wraps $00 to $FF and the cell
     * loop runs 256 times. Faithful to the ROM, and asserted here so the port
     * cannot quietly "fix" it. */
    memset(rb->rom + 0x8004u, 'P', 512u);
    put_record(rb->rom, 0x00008000u, 0x02u, 0x03u, 0x7Fu, 0x00u, "");
    rec_begin(rb, 0xFFFFu);
    check(stunrun_text_draw(&g_iface, 0u) == 256u);
    check(count_latches(rb) == 256u * LATCHES_PER_CELL);

    /* An index the pointer table does not cover touches the board not at all. */
    rec_begin(rb, 0xFFFFu);
    check(stunrun_text_draw(&g_iface, (uint16_t)STUNRUN_STRING_COUNT) == 0u);
    check(rb->total == 0u);
    check(stunrun_text_draw(NULL, 0u) == 0u);

    /* The last covered index still resolves. */
    put_table_entry(rb->rom, (uint16_t)(STUNRUN_STRING_COUNT - 1u), 0x00008000u);
    put_record(rb->rom, 0x00008000u, 0x02u, 0x03u, 0x7Fu, 0x01u, "P");
    rec_begin(rb, 0xFFFFu);
    check(stunrun_text_draw(&g_iface,
                            (uint16_t)(STUNRUN_STRING_COUNT - 1u)) == 1u);
    check(count_reads(rb, STUNRUN_STRING_TABLE
                         + (uint32_t)(STUNRUN_STRING_COUNT - 1u) * 4u) == 1u);
}

static void test_prologue(void)
{
    rec_board_t *rb = &g_board;
    unsigned i;
    int ctl_read_seen = 0;
    int ctl_write_seen_after_read = 0;

    put_table_entry(rb->rom, 0u, 0x00008000u);
    put_record(rb->rom, 0x00008000u, 0x01u, 0x0Fu, 0xDFu, 0x01u, "P");
    rec_begin(rb, 0xFFFFu);
    (void)stunrun_text_draw(&g_iface, 0u);

    /* $25E8 clr.w $608000 -- one kick per call, before anything else. */
    check(rb->watchdog == 1u);
    check(rb->log[0].kind == ACC_WATCHDOG);

    /* $25EE andi.w #$E7FF,$C00008 -- a read-modify-write of HSTCTL, so the
     * device value must be read before it is written. */
    for (i = 0u; i < rb->stored; i++) {
        if (rb->log[i].kind == ACC_GSP_READ
            && rb->log[i].addr == STUNRUN_TEXT_HSTCTL)
            ctl_read_seen = 1;
        if (ctl_read_seen && rb->log[i].kind == ACC_GSP_WRITE
            && rb->log[i].addr == STUNRUN_TEXT_HSTCTL)
            ctl_write_seen_after_read = 1;
    }
    check(ctl_read_seen);
    check(ctl_write_seen_after_read);
    check(write_is(rb, 0u, STUNRUN_TEXT_HSTCTL, 0xE7FFu));
    check(rb->regs.hstctl == 0xE7FFu);

    /* Only HSTCTL bits $1800 are cleared; anything else survives. */
    rec_begin(rb, 0x07FFu);
    (void)stunrun_text_draw(&g_iface, 0u);
    check(write_is(rb, 0u, STUNRUN_TEXT_HSTCTL, 0x07FFu));
    rec_begin(rb, 0xFFFFu);
    (void)stunrun_text_draw(&g_iface, 0u);
    check(write_is(rb, 0u, STUNRUN_TEXT_HSTCTL, 0xE7FFu));
    rec_begin(rb, 0x0000u);
    (void)stunrun_text_draw(&g_iface, 0u);
    check(write_is(rb, 0u, STUNRUN_TEXT_HSTCTL, 0x0000u));
}

static void test_colour_planes(void)
{
    rec_board_t *rb = &g_board;

    /* D7 is the descriptor colour byte; D6 is that byte shifted left eight.
     * Nibble bit 0 writes D7; nibble bit 2 OR-s D6 into the read-back, so the
     * only two values that can reach HSTDAT are D7 and D7|D6. 'P' has eleven
     * nibbles of 5 (both planes, two writes each), three of 4 (the OR alone)
     * and three of 1 (the first plane alone): 14 writes of D7 and 14 of
     * D7|D6. */
    put_table_entry(rb->rom, 0u, 0x00008000u);
    put_record(rb->rom, 0x00008000u, 0x01u, 0x0Fu, 0x9Fu, 0x01u, "P");
    rec_begin(rb, 0xFFFFu);
    check(stunrun_text_draw(&g_iface, 0u) == 1u);

    check(write_is(rb, 3u, STUNRUN_TEXT_HSTDAT, 0x009Fu));
    check(write_is(rb, 4u, STUNRUN_TEXT_HSTDAT, 0x9F9Fu));
    check(count_writes(rb, STUNRUN_TEXT_HSTDAT) == 28u);
    check(count_writes_value(rb, STUNRUN_TEXT_HSTDAT, 0x009Fu) == 14u);
    check(count_writes_value(rb, STUNRUN_TEXT_HSTDAT, 0x9F9Fu) == 14u);
}

int main(void)
{
    rec_board_init(&g_board);
    memset(g_board.rom, 0, sizeof(g_board.rom));

    test_full_write_stream();
    test_coordinate_and_steps();
    test_coordinate_formula();
    test_star_is_the_blank();
    test_loop_nest();
    test_count_field_and_quirk();
    test_prologue();
    test_colour_planes();

    if (g_failures == 0)
        printf("text draw C port: all checks passed\n");
    return g_failures == 0 ? 0 : 1;
}