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
    /* map(0xff800000, 0xffbfffff).mirror(0x0400000)  [harddriv.cpp:628]
     *
     * The declared window is 0xFF800000-0xFFBFFFFF, which is 0x400000 bytes --
     * 4 MiB, not the 512 KiB that GSP_BOARD_VRAM_BYTES below assumes.  Mirror
     * mask bit 22 then makes that whole window reachable a second time at
     * 0xFFC00000-0xFFFFFFFF, so the measured decode is:
     *
     *     mapped iff (addr & 0x7f800000) == 0x7f800000
     *     offset      =  addr & 0x003fffff
     *
     * Measured rather than inferred: tools/mame-gsp-mirror-probe writes markers
     * into the live machine and reports where they land.  See
     * docs/mc68010-boot-findings.md section 19.
     *
     * Two separate bugs live here.  The predicate below accepts all of
     * 0xFF000000-0xFFFFFFFF where MAME maps only 0xFF800000-0xFFFFFFFF -- too
     * wide, though the capture happens never to address below 0xFF800000.  The
     * offset path masks with (GSP_BOARD_VRAM_BYTES - 1) = 0x7FFFF, a 512 KiB
     * period, where the share is 4 MiB -- too narrow, and it disagrees for
     * 745,384 of the 843,688 captured 0xFFxxxxxx writes.
     *
     * Both native/musashi_cpu.c and this file carry the same 0x80000, so they
     * agree with each other by construction and tools/check-gsp-replay cannot
     * see either bug.  That is why section 15.4's byte-for-byte VRAM match is
     * not evidence about MAME.
     *
     * Do not fix the offset here.  The constant is also the size of
     * gsp_board.vram[], of the --vram-out dump and of the frame-600 oracle
     * fixture, so growing it to 0x400000 is its own slice (stunrun-2ua) with
     * the oracle re-captured at the new extent first -- moving the model and
     * the fixture together would give a green diff that means nothing. */
    if ((a & 0xff000000u) == 0xff000000u) return GSP_REGION_VRAM;
    return GSP_REGION_UNMAPPED;
}

int gsp_board_is_mapped(uint32_t byte_addr)
{
    return gsp_region(byte_addr) != GSP_REGION_UNMAPPED;
}

unsigned gsp_board_vram_nonzero_words(const gsp_board_t *b)
{
    unsigned n = 0, i;
    for (i = 0; i < GSP_BOARD_VRAM_BYTES; i += 2)
        if (b->vram[i] || b->vram[i + 1]) n++;
    return n;
}

void gsp_board_reset(gsp_board_t *b)
{
    memset(b, 0, sizeof(*b));
}

/* m_mask_table[data * 2 + half], for the multisync board.
 *
 * harddriv_v.cpp:28 defines  MASK(n) = 0x000000ff << (n * 8)  -- a full BYTE
 * lane, not a 2-bit pixel.  harddriv_v.cpp:43-61 then builds two masks per
 * data word: the low one tests data bits 0x0001/0x0004/0x0010/0x0040 and the
 * high one tests 0x0100/0x0400/0x1000/0x4000, and BOTH map onto the same byte
 * lanes 0..3.
 *
 * Two ways to get this wrong, both of which were written here first:
 *  - using a 2-bit pixel mask (0x3 << n*2) instead of a byte lane, and
 *  - shifting the lane by 8 for the high half instead of switching which data
 *    bits are tested.
 *
 * Caught by replaying the captured bus stream: under the wrong mask every
 * diverging 4-byte word in the residue had a 2bpp expander as its last writer,
 * and not one had a plain VRAM write. */
static uint32_t gsp_2bpp_mask_word(uint16_t data, unsigned half)
{
    static const uint16_t bits[2][4] = {
        { 0x0001u, 0x0004u, 0x0010u, 0x0040u },
        { 0x0100u, 0x0400u, 0x1000u, 0x4000u }
    };
    uint32_t mask = 0;
    unsigned n;
    for (n = 0; n < 4u; n++)
        if (data & bits[half][n])
            mask |= 0x000000ffu << (n * 8u);
    return mask;
}

/* Palette entry index.  hdgsp_control_hi_w (harddriv_v.cpp:211-256) selects
 * the bank from the low three bits of control_high, and only the first 256
 * entries of each plane are addressable. */
static unsigned gsp_board_palette_index(const gsp_board_t *b, uint32_t addr)
{
    unsigned bank = (unsigned)(b->ctrl_hi[0] & 0x07u);
    unsigned off = (unsigned)(addr & 0x0fffu) & 0xffu;
    return bank * 0x100u + off;
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
        /* REFUTED MODEL, corrected here: the 2bpp window is NOT a separate
         * store.  hdgsp_vram_2bpp_r (harddriv_v.cpp) reads the MAIN vram share
         * at `offset * 4`, so writes must land there too or the two views
         * silently disagree.  Getting this wrong showed up as 23,979 of
         * 524,288 bytes differing when replaying a captured 901,354-event
         * bus stream, concentrated in the 2bpp range with the reference
         * holding data where this model still held its 0xFF fill. */
        {
            uint32_t off = (byte_addr & 0xffffu) * 4u;
            off &= (GSP_BOARD_VRAM_BYTES - 1u) & ~3u;
            return (uint32_t)b->vram[off] | ((uint32_t)b->vram[off + 1] << 8) |
                   ((uint32_t)b->vram[off + 2] << 16) |
                   ((uint32_t)b->vram[off + 3] << 24);
        }
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
        {
            /* Record the first few distinct unmapped addresses so a runaway
             * PC names the region it left by, instead of reporting a bare
             * count.  Bounded: once the table is full, later addresses are
             * still counted by unmapped_reads but not tracked individually. */
            unsigned i;
            for (i = 0; i < GSP_BOARD_UNMAPPED_TRACK; i++) {
                if (b->unmapped_read_at[i].count != 0 &&
                    b->unmapped_read_at[i].addr == byte_addr) {
                    b->unmapped_read_at[i].count++;
                    break;
                }
                if (b->unmapped_read_at[i].count == 0) {
                    b->unmapped_read_at[i].addr = byte_addr;
                    b->unmapped_read_at[i].count = 1;
                    break;
                }
            }
        }
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
    case GSP_REGION_VRAM2BPP:
        /* hdgsp_vram_2bpp_w (harddriv_v.cpp:304-320), transcribed literally:
         *
         *     uint32_t *dest = (uint32_t *)&m_gsp_vram[offset * 4];
         *     uint32_t *mask = &m_mask_table[data * 2];
         *     uint32_t color = m_gsp_control_lo[0];
         *     color |= color << 16;
         *     *dest = (*dest & ~curmask) | (color & curmask);   // x2
         *
         * A MASKED read-modify-write of the MAIN VRAM share using a colour
         * from the low control byte -- not a plain store, and not a separate
         * buffer.  Two details are easy to get wrong and both cost real
         * fidelity, so they are called out:
         *
         *  - The stride is `offset * 8` BYTES, not `offset * 4`.  MAME casts
         *    to `uint32_t*` over a share declared `uint16_t`, so one element
         *    is two bytes and the cast advances 4 elements = 8 bytes.  See the
         *    identical note in native/musashi_cpu.c.
         *  - The masked word is assembled BIG-endian from the share and stored
         *    back big-endian, again because the share is uint16_t and MAME's
         *    uint32_t view is the opposite endianness.
         *
         * The mask table (harddriv_v.cpp:43-61, multisync branch) packs bits
         * 0x0001/0x0004/0x0010/0x0040 into the low halfword and the same four
         * bits shifted by 8 into the high halfword, each driving one 2-bit
         * pixel. */
        {
            uint32_t offset = (byte_addr & 0x000fffffu) >> 1;
            uint32_t colour = b->ctrl_lo[0];
            unsigned half;
            colour |= colour << 16;
            for (half = 0; half < 2u; half++) {
                uint32_t mask = gsp_2bpp_mask_word(value, half);
                uint32_t o, cur;
                if (mask == 0u) continue;
                o = (offset * 8u + half * 4u);
                o &= (GSP_BOARD_VRAM_BYTES - 1u) & ~3u;
                cur = ((uint32_t)b->vram[o] << 24) |
                      ((uint32_t)b->vram[o + 1] << 16) |
                      ((uint32_t)b->vram[o + 2] << 8) |
                      (uint32_t)b->vram[o + 3];
                cur = (cur & ~mask) | (colour & mask);
                b->vram[o]     = (uint8_t)(cur >> 24);
                b->vram[o + 1] = (uint8_t)(cur >> 16);
                b->vram[o + 2] = (uint8_t)(cur >> 8);
                b->vram[o + 3] = (uint8_t)cur;
                b->vram_writes += 4u;
            }
        }
        return;
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

/* One HSTDAT write, dispatched on the effective HSTADR address.
 *
 * REFUTED MODEL, corrected here.  This used to expand every HSTDAT into a
 * 16-byte VRAM cell unconditionally, which silently ignored the other
 * regions the address can select.  Replaying the captured 901,354-event bus
 * stream showed 1,658 cells whose last writer was a plain VRAM write and 404
 * whose last writer was the 2bpp expander, with our values looking like cell
 * indices rather than data -- the signature of a write landing in the wrong
 * store.
 *
 * The dispatch below mirrors gsp_mem_write_word in native/musashi_cpu.c
 * region for region, and MAME's host_w() (tms34010.cpp:1875) for the
 * expansion itself. */
static void gsp_board_hstdat_store(gsp_board_t *b, uint16_t data)
{
    const uint32_t addr = b->hstadr;

    /* 0xFF000000-0xFFFFFFFF: the VRAM share.  A host word write does
     * NOT store two bytes -- MAME's program space is a 32-bit view over a
     * 16-bit share, so one 16-bit write covers a 16-byte cell with each
     * source byte replicated eight times.
     *
     * The offset below masks with (size - 1) = 0x7FFFF, a 512 KiB period.
     * The share is 4 MiB: MAME's window 0xff800000-0xffbfffff is 0x400000
     * bytes, mirrored through bit 22, so the measured offset is
     * `addr & 0x3fffff`.  The two disagree from 0xff880000 upward -- inside
     * the primary window, not merely in the alias -- and across the capture
     * that is 745,384 of 843,688 0xFFxxxxxx writes.  CONFIRMED DIVERGENCE,
     * unfixed; see the note on gsp_region() above and
     * docs/mc68010-boot-findings.md section 19. */
    if ((addr & 0xff000000u) == 0xff000000u) {
        uint32_t off = ((addr - 0xff800000u) &
                        (GSP_BOARD_VRAM_BYTES - 1u)) & ~0xfu;
        uint8_t lo = (uint8_t)(data & 0xffu);
        uint8_t hi = (uint8_t)(data >> 8);
        unsigned k;
        for (k = 0; k < 8u; k++) {
            b->vram[off + k]      = lo;
            b->vram[off + 8u + k] = hi;
        }
        b->vram_writes += 16u;
        return;
    }

    /* 0x02000000-0x020FFFFF: masked expander over the SAME VRAM share, with
     * the colour taken from the low control byte.  The loop covers EIGHT
     * four-byte words -- 32 bytes -- alternating the low and high halfword
     * mask, not just the two words a first reading suggests. */
    if ((addr & 0xfff00000u) == 0x02000000u) {
        uint32_t offset = (addr & 0x000fffffu) >> 1;
        uint32_t colour = b->ctrl_lo[0];
        unsigned w;
        colour |= colour << 16;
        for (w = 0; w < 8u; w++) {
            uint32_t mask = gsp_2bpp_mask_word(data, w & 1u);
            uint32_t o, cur;
            if (mask == 0u) continue;
            o = (offset * 8u + w * 4u) &
                (GSP_BOARD_VRAM_BYTES - 1u) & ~3u;
            cur = ((uint32_t)b->vram[o] << 24) |
                  ((uint32_t)b->vram[o + 1] << 16) |
                  ((uint32_t)b->vram[o + 2] << 8) |
                  (uint32_t)b->vram[o + 3];
            cur = (cur & ~mask) | (colour & mask);
            b->vram[o]     = (uint8_t)(cur >> 24);
            b->vram[o + 1] = (uint8_t)(cur >> 16);
            b->vram[o + 2] = (uint8_t)(cur >> 8);
            b->vram[o + 3] = (uint8_t)cur;
            b->vram_writes += 4u;
        }
        return;
    }

    /* Palette and control shares.  The control shares are byte-wide; the
     * palette planes are indexed by the bank selected through control_high. */
    if ((addr & 0xfffff000u) == 0xf5000000u ||
        (addr & 0xfffff000u) == 0xf5800000u) {
        uint8_t *plane = ((addr & 0xfffff000u) == 0xf5000000u)
                       ? b->pal_lo : b->pal_hi;
        unsigned idx = gsp_board_palette_index(b, addr) * 2u;
        if (idx + 1u < GSP_BOARD_PAL_BYTES) {
            plane[idx]     = (uint8_t)(data >> 8);
            plane[idx + 1] = (uint8_t)(data & 0xffu);
        }
        return;
    }
    if ((addr & 0xfffff000u) == 0xf4000000u) {
        b->ctrl_lo[addr & 0xffu] = (uint8_t)(data & 0xffu);
        return;
    }
    if ((addr & 0xfffff000u) == 0xf4800000u) {
        b->ctrl_hi[addr & 0xffu] = (uint8_t)(data & 0xffu);
        return;
    }
    /* Anything else is unmapped; the native model counts and drops it. */
    b->unmapped_writes++;
}

void gsp_board_host_write(gsp_board_t *b, uint32_t byte_offset, uint16_t data)
{
    b->host_writes++;
    switch (gsp_board_host_reg(byte_offset)) {
    case GSP_BOARD_HSTADRH:
        /* All 16 bits are significant.  Masking to 8 bits here silently
         * discarded the 0xC000 the ROM writes, which turned address 0xC0000100
         * into 0x00000100 -- the two agree after the 512 KiB mask for some
         * addresses and disagree for others, which is exactly the sort of
         * partial failure that looks like a rendering bug rather than a decode
         * bug.  Found by replaying a captured 901,354-event bus stream:
         * 2,062 of 32,768 cells differed.
         * MAME: host_w() case TMS34010_HOST_ADDRESS_H stores `data` whole. */
        b->hstadr = (b->hstadr & 0x0000ffffu) | ((uint32_t)data << 16);
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