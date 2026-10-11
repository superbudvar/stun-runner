/* STUN Runner GSP board model.  See gsp_board.h for scope and provenance.
 *
 * Every decode below is transcribed from MAME `harddriv.cpp:619-630`
 * (multisync_gsp_map) and `harddriv_m.cpp` (the 68010 host-port handler).
 * Where behaviour is inferred rather than read, the comment says so. */

#include "gsp_board.h"

#include <string.h>

/* Map entries, kept as data so the test can assert the map directly. */
typedef enum {
    GSP_REGION_UNMAPPED = 0,
    GSP_REGION_SELFTEST,  /* 0x00000000-0x0000200f, .noprw()      */
    GSP_REGION_VRAM2BPP,  /* 0x02000000-0x020fffff                */
    GSP_REGION_CTRL_LO,   /* 0xf4000000-0xf40000ff                */
    GSP_REGION_CTRL_HI,   /* 0xf4800000-0xf48000ff                */
    GSP_REGION_PAL_LO,    /* 0xf5000000-0xf5000fff                */
    GSP_REGION_PAL_HI,    /* 0xf5800000-0xf5800fff                */
    GSP_REGION_VRAM       /* 0xff800000-0xffbfffff, mirrored      */
} gsp_region_t;

static gsp_region_t gsp_region(uint32_t a)
{
    /* Order matters only for readability; the ranges do not overlap. */
    if (a >= 0x00000000u && a <= 0x0000200fu) return GSP_REGION_SELFTEST;
    if (a >= 0x02000000u && a <= 0x020fffffu) return GSP_REGION_VRAM2BPP;
    if (a >= 0xf4000000u && a <= 0xf40000ffu) return GSP_REGION_CTRL_LO;
    if (a >= 0xf4800000u && a <= 0xf48000ffu) return GSP_REGION_CTRL_HI;
    if (a >= 0xf5000000u && a <= 0xf5000fffu) return GSP_REGION_PAL_LO;
    if (a >= 0xf5800000u && a <= 0xf5800fffu) return GSP_REGION_PAL_HI;
    /* map(0xff800000, 0xffbfffff).mirror(0x0400000)
     *
     * The declared window is 0xFF800000-0xFFBFFFFF and mirror mask bit 22
     * doubles it, so the pair covers 0xFF000000-0xFFFFFFFF.  That predicate is
     * exactly what native/musashi_cpu.c:347 already tests for this same share.
     *
     * For any address at or above the window start the physical offset is
     * simply `addr & (size - 1)`: 0xFF800000 is a whole number of 512 KiB
     * cells, so masking the raw address and masking the offset from the base
     * agree.  The mirror therefore needs no special offset arithmetic, only
     * the wider "is it mapped" test. */
    if ((a & 0xff000000u) == 0xff000000u) return GSP_REGION_VRAM;
    return GSP_REGION_UNMAPPED;
}

int gsp_board_is_mapped(uint32_t byte_addr)
{
    return gsp_region(byte_addr) != GSP_REGION_UNMAPPED;
}

void gsp_board_reset(gsp_board_t *b)
{
    memset(b, 0, sizeof(*b));
}

unsigned gsp_board_host_reg(uint32_t byte_offset)
{
    /* See the header: `(byte_offset >> 2) ^ 1` is the board-correct decode.
     * MAME's `(offset / 2) ^ 1` is the same access in MAME's word-index
     * units, not a different transform.  Applying it to a byte offset is a
     * refuted change, documented in docs/mc68010-boot-findings.md section 13. */
    return (unsigned)((byte_offset >> 2u) ^ 1u);
}

/* ------------------------------------------------------------------ reads */

uint16_t gsp_board_read16(gsp_board_t *b, uint32_t byte_addr)
{
    /* TMS34010 16-bit words are halfword aligned.  A misaligned access is not
     * expected from the core; clamp rather than silently cross a boundary. */
    byte_addr &= ~1u;
    b->word_reads++;

    switch (gsp_region(byte_addr)) {
    case GSP_REGION_SELFTEST:
        /* .noprw(): reads hit nothing.  MAME returns 0. */
        return 0;
    case GSP_REGION_VRAM: {
        uint32_t off = byte_addr & (GSP_BOARD_VRAM_BYTES - 1u);
        return (uint16_t)(b->vram[off] | ((uint16_t)b->vram[off + 1] << 8));
    }
    case GSP_REGION_VRAM2BPP:
        /* The 2bpp window has a distinct backing store from the vram share.
         * native/musashi_cpu.c keeps them separate for the same reason. */
        return (uint16_t)((b->vram2bpp[byte_addr & 0xffffu] & 0xff) |
                          ((uint16_t)(b->vram2bpp[(byte_addr & 0xffffu) + 1u]
                                      << 8)));
    case GSP_REGION_CTRL_LO:
        return (uint16_t)(b->ctrl_lo[byte_addr & 0xffu]);
    case GSP_REGION_CTRL_HI:
        return (uint16_t)(b->ctrl_hi[byte_addr & 0xffu]);
    case GSP_REGION_PAL_LO: {
        uint32_t off = byte_addr & (GSP_BOARD_PAL_BYTES - 1u);
        return (uint16_t)(b->pal_lo[off] | ((uint16_t)b->pal_lo[off + 1] << 8));
    }
    case GSP_REGION_PAL_HI: {
        uint32_t off = byte_addr & (GSP_BOARD_PAL_BYTES - 1u);
        return (uint16_t)(b->pal_hi[off] | ((uint16_t)b->pal_hi[off + 1] << 8));
    }
    case GSP_REGION_UNMAPPED:
    default:
        b->unmapped_reads++;
        return 0;
    }
}

/* ----------------------------------------------------------------- writes */

void gsp_board_write16(gsp_board_t *b, uint32_t byte_addr, uint16_t value)
{
    byte_addr &= ~1u;
    b->word_writes++;

    switch (gsp_region(byte_addr)) {
    case GSP_REGION_SELFTEST:
        /* .noprw(): writes go nowhere.  Counted, not stored. */
        return;
    case GSP_REGION_VRAM: {
        uint32_t off = byte_addr & (GSP_BOARD_VRAM_BYTES - 1u);
        b->vram[off]     = (uint8_t)(value & 0xffu);
        b->vram[off + 1] = (uint8_t)(value >> 8);
        b->vram_writes++;
        return;
    }
    case GSP_REGION_VRAM2BPP: {
        uint32_t off = byte_addr & 0xffffu;
        b->vram2bpp[off]     = (uint8_t)(value & 0xffu);
        b->vram2bpp[off + 1] = (uint8_t)(value >> 8);
        return;
    }
    case GSP_REGION_CTRL_LO:
        b->ctrl_lo[byte_addr & 0xffu] = (uint8_t)(value & 0xffu);
        return;
    case GSP_REGION_CTRL_HI:
        b->ctrl_hi[byte_addr & 0xffu] = (uint8_t)(value & 0xffu);
        return;
    case GSP_REGION_PAL_LO: {
        uint32_t off = byte_addr & (GSP_BOARD_PAL_BYTES - 1u);
        b->pal_lo[off]     = (uint8_t)(value & 0xffu);
        b->pal_lo[off + 1] = (uint8_t)(value >> 8);
        return;
    }
    case GSP_REGION_PAL_HI: {
        uint32_t off = byte_addr & (GSP_BOARD_PAL_BYTES - 1u);
        b->pal_hi[off]     = (uint8_t)(value & 0xffu);
        b->pal_hi[off + 1] = (uint8_t)(value >> 8);
        return;
    }
    case GSP_REGION_UNMAPPED:
    default:
        b->unmapped_writes++;
        return;
    }
}

/* -------------------------------------------------------------- host port */

/* HSTDAT expands to a 16-byte cell: eight copies of the low byte followed by
 * eight copies of the high byte.  This is the board's observed host-write
 * expansion, recorded in docs/mc68010-boot-findings.md section 12.2 and
 * already implemented in native/musashi_cpu.c. */
static void gsp_board_hstdat_store(gsp_board_t *b, uint16_t data)
{
    uint32_t base = b->hstadr & (GSP_BOARD_VRAM_BYTES - 1u);
    uint8_t lo = (uint8_t)(data & 0xffu);
    uint8_t hi = (uint8_t)(data >> 8);
    unsigned i;
    for (i = 0; i < 8u; i++) {
        b->vram[base + i] = lo;
        b->vram[base + 8u + i] = hi;
    }
    b->vram_writes += 16u;
}

void gsp_board_host_write(gsp_board_t *b, uint32_t byte_offset, uint16_t data)
{
    b->host_writes++;
    switch (gsp_board_host_reg(byte_offset)) {
    case GSP_BOARD_HSTADRH:
        b->hstadr = (b->hstadr & 0x0000ffffu) |
                    ((uint32_t)(data & 0x00ffu) << 16);
        return;
    case GSP_BOARD_HSTADRL:
        b->hstadr = (b->hstadr & 0xffff0000u) | (uint32_t)(data & 0xffffu);
        return;
    case GSP_BOARD_HSTDAT:
        gsp_board_hstdat_store(b, data);
        b->hstdat = data;
        /* HSTCTL bit 0x0800 post-increments the address latches after a host
         * data write, by one 16-byte cell. */
        if (b->hstctl & 0x0800u) b->hstadr += 16u;
        return;
    case GSP_BOARD_HSTCTL:
        b->hstctl = data;
        return;
    default:
        /* A decoded register index outside the four the board implements.
         * The bus observer records these raw rather than decoding them; here
         * there is nothing to model, so they are dropped. */
        return;
    }
}

uint16_t gsp_board_host_read(gsp_board_t *b, uint32_t byte_offset)
{
    switch (gsp_board_host_reg(byte_offset)) {
    case GSP_BOARD_HSTADRH:
        return (uint16_t)((b->hstadr >> 16) & 0x00ffu);
    case GSP_BOARD_HSTADRL:
        return (uint16_t)(b->hstadr & 0xffffu);
    case GSP_BOARD_HSTDAT:
        /* HSTDAT is a data REGISTER, not a memory window: a read returns the
         * last value written.  It must NOT be reconstructed from the cell.
         * The 16-byte expansion puts eight low-byte copies at base+0..7 and
         * eight high-byte copies at base+8..15, so reading adjacent bytes of
         * the cell would return (hi,hi) rather than the word. */
        return b->hstdat;
    case GSP_BOARD_HSTCTL:
        return b->hstctl;
    default:
        return 0;
    }
}