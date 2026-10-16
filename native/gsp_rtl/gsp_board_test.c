/* Tests for the STUN Runner GSP board model.
 *
 * These assert observable board behaviour -- the address map itself, the
 * host-register decode, the HSTDAT cell expansion and the HSTCTL increment --
 * not merely that the functions ran.  No commercial ROM and no Verilator
 * required: this file is why the board model is plain C99.
 */

#include "gsp_board.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
            failures++;                                                        \
        }                                                                      \
    } while (0)

#define CHECK_EQ(actual, expect)                                               \
    do {                                                                       \
        long long a_ = (long long)(actual);                                     \
        long long e_ = (long long)(expect);                                     \
        if (a_ != e_) {                                                        \
            printf("FAIL %s:%d: %s == %s (got 0x%llx, want 0x%llx)\n",         \
                   __FILE__, __LINE__, #actual, #expect,                        \
                   (unsigned long long)a_, (unsigned long long)e_);             \
            failures++;                                                        \
        }                                                                      \
    } while (0)

/* The map is the contract.  Assert every documented boundary of
 * multisync_gsp_map (harddriv.cpp:619-630) and that map.unmap_value_high()
 * really leaves everything else unmapped. */
static void test_address_map(void)
{
    /* Inside each mapped range. */
    CHECK(gsp_board_is_mapped(0x00000000u));
    CHECK(gsp_board_is_mapped(0x0000200fu));
    CHECK(gsp_board_is_mapped(0x02000000u));
    CHECK(gsp_board_is_mapped(0x020fffffu));
    CHECK(gsp_board_is_mapped(0xf4000000u));
    CHECK(gsp_board_is_mapped(0xf40000ffu));
    CHECK(gsp_board_is_mapped(0xf4800000u));
    CHECK(gsp_board_is_mapped(0xf48000ffu));
    CHECK(gsp_board_is_mapped(0xf5000000u));
    CHECK(gsp_board_is_mapped(0xf5000fffu));
    CHECK(gsp_board_is_mapped(0xf5800000u));
    CHECK(gsp_board_is_mapped(0xf5800fffu));
    CHECK(gsp_board_is_mapped(0xff800000u));
    CHECK(gsp_board_is_mapped(0xffbfffffu));

    /* Just past each range: unmap_value_high() means unmapped, not wrapped. */
    CHECK(!gsp_board_is_mapped(0x00002100u));
    CHECK(!gsp_board_is_mapped(0x02100000u));
    CHECK(!gsp_board_is_mapped(0xf4000100u));
    CHECK(!gsp_board_is_mapped(0xf4800100u));
    CHECK(!gsp_board_is_mapped(0xf5001000u));
    CHECK(!gsp_board_is_mapped(0xf5801000u));

    /* map(0xff800000, 0xffbfffff).mirror(0x0400000): mirror bit 22 doubles
     * the declared window.  The correct predicate is (addr & 0x7F800000) ==
     * 0x7F800000, which means bits 31-23 must be 0x3FF.  This is NOT the
     * same as "all 0xFFxxxxxx addresses are mapped" (that was the old bug). */
    CHECK(!gsp_board_is_mapped(0xff000000u));   /* & 0x7F800000 = 0x7F000000 */
    CHECK(!gsp_board_is_mapped(0xff7fffffu));   /* & 0x7F800000 = 0x7F000000 */
    CHECK(gsp_board_is_mapped(0xff800000u));    /* & 0x7F800000 = 0x7F800000 */
    CHECK(gsp_board_is_mapped(0xfff00000u));    /* & 0x7F800000 = 0x7F800000 */
    CHECK(gsp_board_is_mapped(0xffff0000u));    /* & 0x7F800000 = 0x7F800000 */
    CHECK(gsp_board_is_mapped(0xffffffffu));   /* & 0x7F800000 = 0x7F800000 */

    /* Bit 30 is NOT part of the mirror: 0xFC800000 sits below the expanded
     * window and must stay unmapped rather than being folded in. */
    CHECK(!gsp_board_is_mapped(0xfc800000u));
    CHECK(!gsp_board_is_mapped(0xfe000000u));
    CHECK(!gsp_board_is_mapped(0xf8000000u));
}

/* Aliasing inside the GSP VRAM share.
 *
 * MAME declares a 4 MiB window with `map(0xff800000, 0xffbfffff)
 * .mirror(0x0400000)`, and the share behind it is now 4 MiB (not 512 KiB).
 * The offset formula is ((addr - 0xFF800000) % 0x400000) & 0x3FFFFF.
 *
 * Note: the predicate for MAPPED is (addr & 0x7F800000) == 0x7F800000.
 * Only addresses where bits 31-23 are 0x3FF are mapped.  Within that, the
 * formula maps different addresses to different offsets -- addresses that
 * share the same offset are true aliases (e.g. 0xFF800000 and 0xFFC00000
 * both give offset 0). */
static void test_vram_mirroring_is_one_share(void)
{
    static gsp_board_t b;   /* 4 MiB model: static, never on the stack */
    gsp_board_reset(&b);

    /* Write to 0xFF800000 (offset 0 in the 4 MiB share).  Addresses that
     * compute offset 0 will read back the same value:
     * - 0xFF800000: offset 0
     * - 0xFFC00000: offset 0 (since 0x400000 % 0x400000 = 0)
     * But 0xFFFF0000 gives offset 0x3F0000, NOT 0. */
    gsp_board_write16(&b, 0xff800000u, 0x1234u);
    CHECK_EQ(gsp_board_read16(&b, 0xff800000u), 0x1234u);
    CHECK_EQ(gsp_board_read16(&b, 0xffc00000u), 0x1234u);
    CHECK_EQ(gsp_board_read16(&b, 0xffff0000u), 0x0000u);  /* offset 0x3F0000, not 0 */

    /* 0xFF000000 is NOT mapped (bits 31-23 = 0x7F0, not 0x3FF), so writes
     * are dropped and reads return 0. */
    CHECK(!gsp_board_is_mapped(0xff000000u));
    CHECK_EQ(gsp_board_read16(&b, 0xff000000u), 0x0000u);

    /* The declared window ends at 0xFFBFFFFF, which is 2 MiB into the 4 MiB
     * share (offset 0x3FFFFF). */
    gsp_board_write16(&b, 0xffbffffeu, 0xbeefu);
    CHECK_EQ(gsp_board_read16(&b, 0xffbffffeu), 0xbeefu);
    CHECK_EQ(gsp_board_read16(&b, 0xff800000u), 0x1234u);  /* different cell */
}

/* The 2bpp window writes INTO the main VRAM share.
 *
 * This test was originally written to assert the opposite -- that the 2bpp
 * window is a separate store -- because that is what the model did.  It is
 * wrong.  hdgsp_vram_2bpp_w (harddriv_v.cpp:304-320) is a masked
 * read-modify-write of the main VRAM share, with the colour taken from
 * gsp_control_lo[0]:
 *
 *     uint32_t *dest = (uint32_t *)&m_gsp_vram[offset * 4];
 *     uint32_t color = m_gsp_control_lo[0];  color |= color << 16;
 *     *dest = (*dest & ~curmask) | (color & curmask);      // x2
 *
 * A separate buffer meant the two views silently disagreed.  Caught by
 * replaying a captured 901,354-event bus stream.  See
 * docs/mc68010-boot-findings.md section 15.4. */
static void test_2bpp_window_writes_main_vram(void)
{
    static gsp_board_t b;   /* 512 KiB model: static, never on the stack */
    gsp_board_reset(&b);

    /* The 2bpp colour source is the low control WORD, byte 1.  This model
     * used ctrl_lo[0]; native/musashi_cpu.c uses ctrl_lo[1] and is the copy
     * validated against MAME's direct palette-share capture (findings 34.3). */
    b.ctrl_lo[1] = 0x5a;

    /* Data 0x0001 selects MASK(0), a full BYTE lane, in the low halfword only.
     * The share is little-endian, so lane 0 is vram[0] and the colour byte
     * lands whole -- not folded into two bits of vram[3].  HSTADR 0x02000000
     * is a bit address: window offset (0 >> 4) = 0, share byte 0. */
    gsp_board_write16(&b, 0x02000000u, 0x0001u);

    CHECK_EQ(b.vram[0], 0x5au);         /* whole colour byte in lane 0 */
    CHECK_EQ(b.vram[1], 0x00u);
    CHECK_EQ(b.vram[2], 0x00u);
    CHECK_EQ(b.vram[3], 0x00u);

    /* An all-zero data word writes nothing at all. */
    uint64_t before = b.vram_writes;
    gsp_board_write16(&b, 0x02000000u, 0x0000u);
    CHECK_EQ(b.vram_writes, before);
}

/* Unmapped space reads zero and is counted, so a harness that walks off the
 * end of the map cannot look like a working one. */
static void test_unmapped_reads_zero_and_count(void)
{
    static gsp_board_t b;   /* 1.5 MiB model: static, never on the stack */
    gsp_board_reset(&b);

    CHECK_EQ(gsp_board_read16(&b, 0x12345678u), 0);
    CHECK_EQ(b.unmapped_reads, 1u);
    CHECK_EQ(b.word_reads, 1u);

    gsp_board_write16(&b, 0x12345678u, 0xffffu);
    CHECK_EQ(b.unmapped_writes, 1u);

    /* The self-test window is .noprw(): it is mapped, but reads nothing and
     * stores nothing. */
    CHECK(gsp_board_is_mapped(0x00000000u));
    CHECK_EQ(gsp_board_read16(&b, 0x00000000u), 0);
    gsp_board_write16(&b, 0x00000000u, 0xffffu);
    CHECK_EQ(gsp_board_read16(&b, 0x00000000u), 0);
}

/* The host-register decode.
 *
 * The load-bearing negative control is that HSTDAT is register 2 when the
 * offset is a byte offset.  docs/mc68010-boot-findings.md section 13 records
 * the refuted alternative: using MAME's `(offset / 2) ^ 1` on a byte offset
 * sends HSTDAT to an offset the ROM never writes and drops the measured
 * frame-600 host-write count from 372,769 to zero.  If someone "fixes" this
 * decode to agree with MAME, this test must fail. */
static void test_host_register_decode(void)
{
    /* Byte offsets from the measured board map (musashi_cpu.c:749-750):
     *   0xC00000-0xC00003 -> HSTADRH   0xC00008-0xC0000B -> HSTCTL
     *   0xC00004-0xC00007 -> HSTADRL   0xC0000C-0xC0000F -> HSTDATA
     * HSTADRL and HSTADRH are adjacent and are the easiest pair to transpose;
     * the RTL's own host_reg_sel_t orders them HSTADRL=0, HSTADRH=1. */
    CHECK_EQ(gsp_board_host_reg(0x00u), GSP_BOARD_HSTADRH);
    CHECK_EQ(gsp_board_host_reg(0x04u), GSP_BOARD_HSTADRL);
    CHECK_EQ(gsp_board_host_reg(0x08u), GSP_BOARD_HSTCTL);
    CHECK_EQ(gsp_board_host_reg(0x0cu), GSP_BOARD_HSTDAT);

    /* MAME's word index 6 is byte offset 0x0C, and both decode to HSTDAT. */
    CHECK_EQ(6u * 2u, 0x0cu);
    CHECK_EQ(gsp_board_host_reg(6u * 2u), GSP_BOARD_HSTDAT);

    /* The indices must equal the vendored RTL's host_reg_sel_t values, since
     * the harness passes this index straight to host_reg_i.  Spelling them
     * out here means a transpose fails here rather than in the simulator. */
    CHECK_EQ(GSP_BOARD_HSTADRL, 0);
    CHECK_EQ(GSP_BOARD_HSTADRH, 1);
    CHECK_EQ(GSP_BOARD_HSTDAT, 2);
    CHECK_EQ(GSP_BOARD_HSTCTL, 3);

    /* The transform differs from MAME's beyond the first mapped offsets;
     * larger offsets must not silently wrap onto one of the four registers. */
    CHECK(gsp_board_host_reg(0x20u) > 3u);
}

/* HSTDAT expands to a 16-byte cell: eight low-byte copies then eight
 * high-byte copies.  This is the board's observed expansion, recorded in
 * docs/mc68010-boot-findings.md section 12.2. */
/* HSTDAT to the VRAM share stores a PACKED 16-bit word at a physical offset
 * CONVERTED from the GSP bit address.
 *
 *     byte_offset = ((addr - 0xFF800000) >> 3) & 0x7FFFF
 *
 * Both halves of that are the whole correction. The share is 512 KiB, not the
 * 4 MiB the address window spans, and the shift is because the TMS34010 program
 * space is configured `datawidth 16, addrshift 3` -- one address unit is one
 * BIT. HSTADR $FF801000 is therefore physical byte $200, not $1000.
 *
 * Verified end to end against MAME's direct share dump: the 68010's own text
 * line at HSTADR $FF896040 lands at physical $12C08, and native/musashi_cpu.c
 * puts `df df df df df df 00 00 ...` there, byte-identical. See
 * docs/mc68010-boot-findings.md section 34.3.
 *
 * This test previously asserted a 16-byte replicated cell. That came from
 * reading the share through the GSP program space and stepping one address at a
 * time -- which walks BIT addresses, so sixteen consecutive program-space reads
 * are not sixteen consecutive physical bytes. The measurement is withdrawn in
 * section 34.2; the expectations below are the corrected ones. */
static void test_hstdat_stores_packed_word_at_converted_offset(void)
{
    static gsp_board_t b;   /* 512 KiB model: static, never on the stack */
    unsigned i;
    gsp_board_reset(&b);

    /* HSTADR must select the VRAM share.  The ROM's own boot fill uses
     * `move.l #$FF800000,$C00002.l` (findings 13.1), and an address outside
     * 0xFF000000-0xFFFFFFFF lands in no region at all -- so a test that uses
     * a small HSTADR like 0x0100 is not testing the board, it is testing
     * an address the board never sees. */
    gsp_board_host_write(&b, 0x00u, 0xff80u); /* HSTADRH */
    gsp_board_host_write(&b, 0x04u, 0x1000u); /* HSTADRL -> 0xFF801000 */
    gsp_board_host_write(&b, 0x0cu, 0xc37fu); /* HSTDAT                 */

    CHECK_EQ(b.hstadr, 0xff801000u);

    /* (0xFF801000 - 0xFF800000) >> 3 == 0x200.  Packed, little-endian. */
    CHECK_EQ(b.vram[0x0200u], 0x7fu);
    CHECK_EQ(b.vram[0x0201u], 0xc3u);

    /* Nothing else was written.  A byte-addressed model puts the word at
     * 0x1000 instead, and a replicating model fills 0x1000-0x100f, so all
     * three disagree here. */
    for (i = 0; i < 16u; i++) {
        CHECK_EQ(b.vram[0x0202u + i], 0x00u);
    }
    CHECK_EQ(b.vram[0x1000u], 0x00u);
    CHECK_EQ(b.vram_writes, 2u);
}

/* The conversion is the whole point, so pin it against a value the ROM
 * actually uses.  HSTADR $FF896040 is the 68010's first text line; findings
 * 34.3 records it landing at physical share byte $12C08.  0x96040 >> 3 is
 * 0x12C08, so this asserts the shift rather than restating the constant. */
static void test_hstdat_rom_text_address_lands_at_12c08(void)
{
    static gsp_board_t b;   /* 512 KiB model: static, never on the stack */
    gsp_board_reset(&b);

    gsp_board_host_write(&b, 0x00u, 0xff89u);
    gsp_board_host_write(&b, 0x04u, 0x6040u); /* -> 0xFF896040 */
    gsp_board_host_write(&b, 0x0cu, 0xdfdfu);

    CHECK_EQ(b.vram[0x12c08u], 0xdfu);
    CHECK_EQ(b.vram[0x12c09u], 0xdfu);
    /* And not at the byte-addressed offset the old model would have used. */
    CHECK_EQ(b.vram[0x096040u & (GSP_BOARD_VRAM_BYTES - 1u)], 0x00u);
}

/* HSTADR's low four bits are DISCARDED.  MAME masks the assembled address with
 * 0xfffffff0 before the store (tms34010.cpp:1898).  That mask aligns to a
 * 16-bit WORD -- it is not a 16-byte cell size, which is the reading that
 * produced the withdrawn replication.
 *
 * HSTADR $FF802008 therefore stores at $FF802000, i.e. physical byte 0x400,
 * not at 0x408. */
static void test_hstdat_discards_low_address_bits(void)
{
    static gsp_board_t b;   /* 512 KiB model: static, never on the stack */
    gsp_board_reset(&b);

    gsp_board_host_write(&b, 0x00u, 0xff80u);
    gsp_board_host_write(&b, 0x04u, 0x2008u); /* -> 0xFF802008 */
    gsp_board_host_write(&b, 0x0cu, 0x5678u);

    /* (0x2000 >> 3) == 0x400, packed little-endian. */
    CHECK_EQ(b.vram[0x0400u], 0x78u);
    CHECK_EQ(b.vram[0x0401u], 0x56u);
    /* 0xFF802008 unmasked would be 0x401, and the byte-addressed reading would
     * be 0x2008.  Neither was written. */
    CHECK_EQ(b.vram[0x0402u], 0x00u);
    CHECK_EQ(b.vram[0x2008u], 0x00u);
}

/* HSTCTL bit 0x0800 post-increments the address latches after a host data
 * write, by one 16-byte cell.  The increment is POST: the store lands at the
 * pre-increment address, which is why the visible data is at 0x0200 and not
 * at 0x0210. */
static void test_hstctl_increment(void)
{
    static gsp_board_t b;   /* 1.5 MiB model: static, never on the stack */
    gsp_board_reset(&b);

    /* HSTADRH is loaded with the real 0xFF8x high half: HSTADR is masked by
     * the region decode, so an address like 0x0200 would select no region at
     * all and the store would be discarded. */
    gsp_board_host_write(&b, 0x00u, 0xff80u);
    gsp_board_host_write(&b, 0x04u, 0x0200u); /* -> 0xFF800200, physical 0x40 */

    /* Increment clear: address must not move. */
    gsp_board_host_write(&b, 0x08u, 0x0000u); /* HSTCTL = 0 */
    gsp_board_host_write(&b, 0x0cu, 0x1111u);
    CHECK_EQ(b.hstadr, 0xff800200u);
    CHECK_EQ(b.vram[0x0040u], 0x11u);
    CHECK_EQ(b.vram[0x0041u], 0x11u);

    /* Increment set: one 16-bit WORD forward (0x10 in GSP address units,
     * which is TWO physical bytes), after the store. */
    gsp_board_host_write(&b, 0x08u, 0x0800u); /* HSTCTL = 0x0800 */
    gsp_board_host_write(&b, 0x0cu, 0x2222u);
    CHECK_EQ(b.hstadr, 0xff800210u);
    CHECK_EQ(b.vram[0x0040u], 0x22u); /* stored at the OLD address */
    CHECK_EQ(b.vram[0x0042u], 0x00u); /* the new address is not yet written */

    /* HSTADRH/HSTADRL compose into one address without disturbing the other
     * half. */
    gsp_board_host_write(&b, 0x00u, 0x0007u);
    CHECK_EQ(b.hstadr, 0x00070210u);
    gsp_board_host_write(&b, 0x04u, 0x0000u);
    CHECK_EQ(b.hstadr, 0x00070000u);
}

/* HSTDAT must be DISPATCHED on the effective HSTADR address.
 *
 * This is the regression test for the bug that made the 901,354-event replay
 * diverge on 2,062 cells: every HSTDAT was expanded into a 16-byte VRAM cell
 * unconditionally, so the 2bpp window, the palette planes and the control
 * shares were silently ignored.  The failure looked like arithmetic drift --
 * our values came out looking like cell indices -- rather than a missing
 * branch, which is exactly why it survived the earlier fixes.
 *
 * Counter-check for the dispatch itself: the same register index reaches a
 * different store purely because HSTADR differs. */
static void test_hstdat_dispatches_on_address(void)
{
    static gsp_board_t b;   /* 1.5 MiB model: static, never on the stack */
    gsp_board_reset(&b);
    b.ctrl_lo[1] = 0x5a;   /* the 2bpp colour source (low control word, byte 1) */

    /* HSTDAT -> palette low, entry 0 of bank 0. */
    gsp_board_host_write(&b, 0x00u, 0xf500u);
    gsp_board_host_write(&b, 0x04u, 0x0000u);
    gsp_board_host_write(&b, 0x0cu, 0x1234u);
    /* Little-endian: the share stores the low byte first. */
    CHECK_EQ(b.pal_lo[0], 0x34u);
    CHECK_EQ(b.pal_lo[1], 0x12u);
    /* And nothing landed in VRAM. */
    CHECK_EQ(b.vram_writes, 0u);

    /* The SAME HSTDAT, with HSTADR in the 2bpp window, must reach the VRAM
     * share through the masked expander instead -- not the palette. */
    uint64_t pal_before = b.vram_writes;
    gsp_board_host_write(&b, 0x00u, 0x0200u);
    gsp_board_host_write(&b, 0x04u, 0x0010u);
    gsp_board_host_write(&b, 0x0cu, 0x0001u); /* one byte lane, low half */
    CHECK(b.vram_writes > pal_before);
    /* Bit 0 of the data selects MASK(0), which is a full BYTE lane
     * (0x000000ff << 0), not a 2-bit pixel.
     *
     * HSTADR 0x02000010 is a BIT address, so the window offset is
     * (0x10 >> 4) = 1 and the share byte offset is offset * 8 = 8.  The share
     * is little-endian, so lane 0 is the first byte.  The colour source is
     * ctrl_lo[1] (validated against MAME's direct palette capture, 34.3). */
    CHECK_EQ(b.vram[0x08u], 0x5au);
    CHECK_EQ(b.vram[0x09u], 0x00u); /* other lanes untouched by this mask */
    CHECK_EQ(b.vram[0x0au], 0x00u);
    CHECK_EQ(b.vram[0x0bu], 0x00u);

    /* HSTDAT -> control low: byte-wide, and it must feed the 2bpp colour. */
    gsp_board_host_write(&b, 0x00u, 0xf400u);
    gsp_board_host_write(&b, 0x04u, 0x0000u);
    gsp_board_host_write(&b, 0x0cu, 0x00c3u);
    CHECK_EQ(b.ctrl_lo[0], 0xc3u);

    /* An HSTADR outside every mapped region is dropped and counted, not
     * silently turned into a VRAM write. */
    uint64_t unmapped_before = b.unmapped_writes;
    uint64_t vram_before = b.vram_writes;
    gsp_board_host_write(&b, 0x00u, 0x1234u);
    gsp_board_host_write(&b, 0x04u, 0x5678u);
    gsp_board_host_write(&b, 0x0cu, 0xffffu);
    CHECK(b.unmapped_writes > unmapped_before);
    CHECK_EQ(b.vram_writes, vram_before);
}

/* MAME selects palette banks from the control-high WRITE ADDRESS, not from
 * the value stored in control_hi[0].  Exercise the address-derived bank via
 * the same mapped write path the GSP core uses. */
static void test_palette_bank_comes_from_control_hi_address(void)
{
    static gsp_board_t b;
    gsp_board_reset(&b);

    /* F48000A0 is control-high offset 10: offset&7 == 2 and offset>>3 == 1,
     * so MAME selects palette-bank bit 0 even though the written data is zero. */
    gsp_board_write16(&b, 0xf48000a0u, 0x0000u);
    gsp_board_write16(&b, 0xf5000000u, 0x1234u);

    CHECK_EQ(b.pal_lo[0], 0u);
    CHECK_EQ(b.pal_lo[1], 0u);
    CHECK_EQ(b.pal_lo[0x200u], 0x34u);
    CHECK_EQ(b.pal_lo[0x201u], 0x12u);
    CHECK_EQ(gsp_board_read16(&b, 0xf5000000u), 0x1234u);

    /* Offset 0x20 has bit value zero and must return to bank zero even when
     * its register data is nonzero. */
    gsp_board_write16(&b, 0xf4800020u, 0xffffu);
    gsp_board_write16(&b, 0xf5000000u, 0x5678u);
    CHECK_EQ(b.pal_lo[0], 0x78u);
    CHECK_EQ(b.pal_lo[1], 0x56u);
    CHECK_EQ(b.pal_lo[0x200u], 0x34u);
    CHECK_EQ(b.pal_lo[0x201u], 0x12u);
    CHECK_EQ(gsp_board_read16(&b, 0xf5000000u), 0x5678u);
}

static void test_host_readback(void)
{
    static gsp_board_t b;   /* 1.5 MiB model: static, never on the stack */
    gsp_board_reset(&b);

    gsp_board_host_write(&b, 0x00u, 0x0001u); /* HSTADRH */
    gsp_board_host_write(&b, 0x04u, 0x2345u); /* HSTADRL */
    CHECK_EQ(gsp_board_host_read(&b, 0x00u), 0x0001u);
    CHECK_EQ(gsp_board_host_read(&b, 0x04u), 0x2345u);

    gsp_board_host_write(&b, 0x0cu, 0xbeefu); /* HSTDAT */
    CHECK_EQ(gsp_board_host_read(&b, 0x0cu), 0xbeefu);
}

/* Every mapped region's storage must round-trip.  The two control shares are
 * byte-addressed 256-byte regions, so only the low byte of a 16-bit write
 * persists there; the rest of the map is genuinely 16 bits wide. */
static void test_region_roundtrip(void)
{
    static gsp_board_t b;   /* 1.5 MiB model: static, never on the stack */
    static const struct { uint32_t addr; uint16_t expect; } cases[] = {
        { 0xf5000800u, 0xa5a5u },   /* palette low        */
        { 0xf5800800u, 0xa5a5u },   /* palette high       */
        { 0xff801000u, 0xa5a5u },   /* vram share         */
        { 0xf4000080u, 0x00a5u },   /* control lo: byte-wide */
        { 0xf4800040u, 0x00a5u }    /* control hi: byte-wide */
    };
    size_t i;
    gsp_board_reset(&b);

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        gsp_board_write16(&b, cases[i].addr, 0xa5a5u);
        CHECK_EQ(gsp_board_read16(&b, cases[i].addr), cases[i].expect);
    }
}

int main(void)
{
    test_address_map();
    test_vram_mirroring_is_one_share();
    test_2bpp_window_writes_main_vram();
    test_unmapped_reads_zero_and_count();
    test_host_register_decode();
    test_hstdat_stores_packed_word_at_converted_offset();
    test_hstdat_rom_text_address_lands_at_12c08();
    test_hstdat_discards_low_address_bits();
    test_hstctl_increment();
    test_hstdat_dispatches_on_address();
    test_palette_bank_comes_from_control_hi_address();
    test_host_readback();
    test_region_roundtrip();

    if (failures) {
        printf("gsp_board_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("gsp_board_test: PASS\n");
    return 0;
}
