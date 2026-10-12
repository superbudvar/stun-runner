/* Test for mc68010_text_selector_writer.c
 *
 * Covers: ROM $196 (selector dispatch) and $2580 (text draw from descriptor).
 *
 * Observable output: the sequence of HSTADR (address latch) and HSTDAT
 * (data write) bus operations.  We capture them in a ring buffer.
 */
#include "mc68010_text_selector_writer.h"

#include <stdio.h>
#include <string.h>

/* ── Capture log ─────────────────────────────────────────────────────── */

#define MAX_WRITES 512

typedef struct {
    uint32_t addr;
    uint16_t dat;
    int is_dat;  /* 1 = HSTDAT write, 0 = HSTADR write */
} write_record_t;

static write_record_t writes[MAX_WRITES];
static unsigned write_count;

static void clear_log(void)
{
    write_count = 0;
}

static void log_hstadr(void *ctx, uint32_t addr, uint16_t value)
{
    (void)ctx; (void)value;
    if (write_count < MAX_WRITES) {
        writes[write_count].addr = addr;
        writes[write_count].dat = 0;
        writes[write_count].is_dat = 0;
        write_count++;
    }
}

static void log_hstdat(void *ctx, uint16_t value)
{
    (void)ctx;
    if (write_count < MAX_WRITES) {
        writes[write_count].addr = 0;
        writes[write_count].dat = value;
        writes[write_count].is_dat = 1;
        write_count++;
    }
}

static void log_bus_write(void *ctx, uint32_t addr, uint16_t value)
{
    if (addr == 0x00C00002u || addr == 0x00C00004u) {
        log_hstadr(ctx, addr, value);
    } else if (addr == 0x00C0000Cu) {
        log_hstdat(ctx, value);
    }
}

/* ── Selector table for tests ─────────────────────────────────────────── */

/* Build a minimal ROM with a selector table entry and a glyph.
 * Selector $01: VRAM base 0x2000000, colour 0xDF, 1 char = 0x41 ('A')
 * Glyph 0x41: simple cross pattern (bits 4-7 = 0xF at row 4, else 0) */
static uint8_t test_rom[0x20000];

static void build_test_rom(void)
{
    uint32_t sel_entry;
    uint32_t glyph_base;
    unsigned i;

    memset(test_rom, 0, sizeof(test_rom));

    /* Selector $01 entry at TABLE + 4 = 0x1013A */
    sel_entry = 0x10136u + 4u;
    test_rom[sel_entry + 0] = 0x00;   /* VRAM base byte 0 */
    test_rom[sel_entry + 1] = 0x00;   /* VRAM base byte 1 → base = 0x0000*0x40 = 0 */
    test_rom[sel_entry + 2] = 0xDF;   /* colour */
    test_rom[sel_entry + 3] = 0x01;   /* count = 1 */
    test_rom[sel_entry + 4] = 0x41;  /* char 'A' */

    /* VRAM base computation: byte1*0xA0*0x100 + byte0*0x40 - 0x800000
     * = 0*0xA000 + 0*0x40 - 0x800000 = 0x800000 (unsigned) */
    /* Actually base = 0x00 * 0xA000 + 0x00 * 0x40 - 0x800000 = 0xFF800000 (signed → uint)
     * Let's set byte0=0x00, byte1=0x80 → base = 0x80*0xA000 + 0*0x40 - 0x800000
     * = 0x5000000 + 0 - 0x800000 = 0x4800000... let me use a simpler base.
     * Actually let's just use byte0=0x00, byte1=0x00 and accept 0xFF800000 as base. */

    /* Use the correct base for testing: byte1=0x00, byte0=0x00
     * base = 0x0000*0xA000 + 0x0000*0x40 - 0x800000 = 0xFF800000 */
    /* Actually the formula is: ((byte1 * 0xA0) << 8) + (byte0 * 0x40) - 0x800000
     * byte1=0, byte0=0 → 0 + 0 - 0x800000 = 0xFF800000 (wraps to 24-bit)
     * But VRAM for text is at 0x16000-0x1FFFF in the actual game.
     * For test purposes, any base is fine. */
    sel_entry = 0x10136u + 4u;
    /* Change base bytes to give us a workable address in main VRAM:
     * byte1=0xFF, byte0=0x00 → base = 0xFF*0xA000 + 0 - 0x800000
     *   = 0xFF0000 - 0x800000 = 0x7F0000 (still wrong)
     * Use: byte1=0x80, byte0=0x00 → base = 0x80*0xA000 + 0 - 0x800000
     *   = 0x5000000 - 0x800000 = 0x4800000. */
    test_rom[sel_entry + 0] = 0x00;
    test_rom[sel_entry + 1] = 0x80;   /* → base = 0x4800000 */

    /* Glyph for 0x41: row 4 = 0x00F0 (centre row of 'A') */
    glyph_base = STUNRUN_TEXT_FONT_BASE +
                 ((uint32_t)(0x41 - STUNRUN_TEXT_GLYPH_BIAS) *
                  STUNRUN_TEXT_GLYPH_STRIDE);
    for (i = 0; i < 8; i++) {
        uint16_t row_word = (i == 4) ? 0x00F0 : 0x0000;
        test_rom[glyph_base + i * 2]     = (uint8_t)(row_word >> 8);
        test_rom[glyph_base + i * 2 + 1] = (uint8_t)(row_word & 0xFF);
    }
}

/* ── Selector dispatch ($196) test ────────────────────────────────────── */

static void test_selector_dispatch(void)
{
    stunrun_mc68010_text_port_t port = {
        .ctx = NULL,
        .bus_write = log_bus_write,
        .hstadr = 0,
    };

    build_test_rom();

    clear_log();
    stunrun_mc68010_text_selector_dispatch(&port, test_rom, 0x01);

    printf("  selector_dispatch (selector $01): %u writes\n", write_count);
    printf("  Expect 16+ writes (8 rows × 2 ops) — %s\n",
           write_count >= 16 ? "OK" : "FAIL");
}

/* ── Text from descriptor ($2580) test ───────────────────────────────── */

static void test_text_draw_from_descriptor(void)
{
    stunrun_mc68010_text_port_t port = {
        .ctx = NULL,
        .bus_write = log_bus_write,
        .hstadr = 0,
    };
    const uint8_t chars[1] = { 0x41 };
    uint32_t vram_base = 0x2000000u;
    uint16_t colour = 0xDF00u;  /* byte-replicated: 0xDF low, 0x00 high */

    /* Patch glyph into ROM first */
    build_test_rom();

    clear_log();
    stunrun_mc68010_text_draw_from_descriptor(&port, test_rom, vram_base,
                                               chars, 1, colour);

    printf("  text_draw_from_descriptor: %u writes\n", write_count);
    printf("  Expect 16+ writes — %s\n",
           write_count >= 16 ? "OK" : "FAIL");
}

/* ── main ────────────────────────────────────────────────────────────── */

int main(void)
{
    printf("mc68010_text_selector_writer tests\n");
    printf("=================================\n\n");

    test_selector_dispatch();
    printf("\n");
    test_text_draw_from_descriptor();
    printf("\nDone.\n");
    return 0;
}
