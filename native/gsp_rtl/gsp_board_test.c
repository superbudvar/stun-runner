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
     * the declared window, so 0xFF000000-0xFFFFFFFF is mapped.  This is the
     * same predicate native/musashi_cpu.c:347 uses for this share. */
    CHECK(gsp_board_is_mapped(0xff000000u));
    CHECK(gsp_board_is_mapped(0xff7fffffu));
    CHECK(gsp_board_is_mapped(0xff800000u));
    CHECK(gsp_board_is_mapped(0xfff00000u));
    CHECK(gsp_board_is_mapped(0xffff0000u));
    CHECK(gsp_board_is_mapped(0xffffffffu));

    /* Bit 30 is NOT part of the mirror: 0xFC800000 sits below the expanded
     * window and must stay unmapped rather than being folded in. */
    CHECK(!gsp_board_is_mapped(0xfc800000u));
    CHECK(!gsp_board_is_mapped(0xfe000000u));
    CHECK(!gsp_board_is_mapped(0xf8000000u));
}

/* Aliasing inside the GSP VRAM share.
 *
 * MAME declares a 4 MiB window with `map(0xff800000, 0xffbfffff)
 * .mirror(0x0400000)`, but the share behind it is only 0x80000 bytes.  That
 * mismatch is the whole point of findings section 12.1 ("The `gsp_vram` share
 * is 512 KiB, not 4 MiB"): the window wraps within 512 KiB.  So the physical
 * offset is `addr & 0x7FFFF`, and several high addresses legitimately share
 * offset 0. */
static void test_vram_mirroring_is_one_share(void)
{
    static gsp_board_t b;   /* 1.5 MiB model: static, never on the stack */
    gsp_board_reset(&b);

    gsp_board_write16(&b, 0xff800000u, 0x1234u);

    /* Every one of these resolves to physical offset 0. */
    CHECK_EQ(gsp_board_read16(&b, 0xff800000u), 0x1234u);
    CHECK_EQ(gsp_board_read16(&b, 0xff000000u), 0x1234u);
    CHECK_EQ(gsp_board_read16(&b, 0xff400000u), 0x1234u);
    CHECK_EQ(gsp_board_read16(&b, 0xffc00000u), 0x1234u);
    CHECK_EQ(gsp_board_read16(&b, 0xfff00000u), 0x1234u);

    /* 0xFFFF0000 is mapped but lands on offset 0x70000, NOT offset 0.  This
     * is the distinction the earlier assumption got wrong: "high address"
     * is not the same as "aliases the window base". */
    CHECK(gsp_board_is_mapped(0xffff0000u));
    CHECK_EQ(gsp_board_read16(&b, 0xffff0000u), 0x0000u);

    /* The declared window ends at 0xFFBFFFFF, which is the last word of the
     * 512 KiB share.  It is not a wrap back to offset 0. */
    gsp_board_write16(&b, 0xffbffffeu, 0xbeefu);
    CHECK_EQ(gsp_board_read16(&b, 0xffbffffeu), 0xbeefu);
    CHECK_EQ(gsp_board_read16(&b, 0xff800000u), 0x1234u);
}

/* The 2bpp window is a distinct store from the vram share.  Keeping them
 * apart is why native/musashi_cpu.c allocates two arrays. */
static void test_2bpp_window_is_separate(void)
{
    static gsp_board_t b;   /* 1.5 MiB model: static, never on the stack */
    gsp_board_reset(&b);

    gsp_board_write16(&b, 0x02000000u, 0xaaaau);
    gsp_board_write16(&b, 0xff800000u, 0x5555u);

    CHECK_EQ(gsp_board_read16(&b, 0x02000000u), 0xaaaau);
    CHECK_EQ(gsp_board_read16(&b, 0xff800000u), 0x5555u);
    /* Only the vram share counts as a vram write; the 2bpp window is a
     * separate store. */
    CHECK_EQ(b.vram_writes, 1u);
    CHECK_EQ(b.word_writes, 2u);
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
static void test_hstdat_cell_expansion(void)
{
    static gsp_board_t b;   /* 1.5 MiB model: static, never on the stack */
    unsigned i;
    gsp_board_reset(&b);

    gsp_board_host_write(&b, 0x00u, 0x0000u); /* HSTADRH = 0      */
    gsp_board_host_write(&b, 0x04u, 0x0100u); /* HSTADRL = 0x0100 */
    gsp_board_host_write(&b, 0x0cu, 0xc37fu); /* HSTDAT           */

    CHECK_EQ(b.hstadr, 0x0100u);
    for (i = 0; i < 8u; i++) {
        CHECK_EQ(b.vram[0x0100u + i], 0x7fu);
        CHECK_EQ(b.vram[0x0108u + i], 0xc3u);
    }
    CHECK_EQ(b.vram[0x0110u], 0x00u); /* cell is exactly 16 bytes */
    CHECK_EQ(b.vram_writes, 16u);
}

/* HSTCTL bit 0x0800 post-increments the address latches after a host data
 * write, by one 16-byte cell.  The increment is POST: the store lands at the
 * pre-increment address, which is why the visible data is at 0x0200 and not
 * at 0x0210. */
static void test_hstctl_increment(void)
{
    static gsp_board_t b;   /* 1.5 MiB model: static, never on the stack */
    gsp_board_reset(&b);

    gsp_board_host_write(&b, 0x00u, 0x0000u);
    gsp_board_host_write(&b, 0x04u, 0x0200u);

    /* Increment clear: address must not move. */
    gsp_board_host_write(&b, 0x08u, 0x0000u); /* HSTCTL = 0 */
    gsp_board_host_write(&b, 0x0cu, 0x1111u);
    CHECK_EQ(b.hstadr, 0x0200u);

    /* Increment set: one cell forward, after the store. */
    gsp_board_host_write(&b, 0x08u, 0x0800u); /* HSTCTL = 0x0800 */
    gsp_board_host_write(&b, 0x0cu, 0x2222u);
    CHECK_EQ(b.hstadr, 0x0210u);
    CHECK_EQ(b.vram[0x0200u], 0x22u); /* stored at the OLD address */
    CHECK_EQ(b.vram[0x0210u], 0x00u); /* the new address is not yet written */

    /* HSTADRH/HSTADRL compose into one address without disturbing the other
     * half. */
    gsp_board_host_write(&b, 0x00u, 0x0007u);
    CHECK_EQ(b.hstadr, 0x00070210u);
    gsp_board_host_write(&b, 0x04u, 0x0000u);
    CHECK_EQ(b.hstadr, 0x00070000u);
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
        { 0x02000010u, 0xa5a5u },   /* 2bpp window        */
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
    test_2bpp_window_is_separate();
    test_unmapped_reads_zero_and_count();
    test_host_register_decode();
    test_hstdat_cell_expansion();
    test_hstctl_increment();
    test_host_readback();
    test_region_roundtrip();

    if (failures) {
        printf("gsp_board_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("gsp_board_test: PASS\n");
    return 0;
}