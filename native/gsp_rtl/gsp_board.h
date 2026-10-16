#ifndef STUNRUN_GSP_BOARD_H
#define STUNRUN_GSP_BOARD_H

/* STUN Runner GSP board model for the TMS34010 RTL core.
 *
 * This is the board A046901's view of the graphics processor: the
 * `multisync_gsp_map` address map from MAME `harddriv.cpp:619-630` plus the
 * four-register MC68010 host port.  It is plain C99 with no dependencies, no
 * GUI toolkit and no commercial ROM, so it is unit-testable on any machine,
 * with or without Verilator installed.
 *
 * It is a *device model*, not game logic: it holds no game state, decides no
 * policy, and is not reachable from any render loop.  It sits below the
 * toolkit-free seam in the same category as the Musashi 68010 core.
 *
 * Addresses here are 32-bit GSP PROGRAM-SPACE addresses, which are BIT
 * addresses: the TMS34010 program space is configured `datawidth 16,
 * addrshift 3`, so one address unit is one bit. Converting one to a physical
 * byte offset is `((addr - 0xFF800000) >> 3) & 0x7FFFF`, and every VRAM path
 * in gsp_board.c goes through gsp_board_vram_offset() to do it. The RTL core
 * presents a bit-addressed architectural field bus, so nothing downstream has
 * to guess the unit.
 *
 * Do not "simplify" a VRAM offset back to `addr & mask`. That byte-addressed
 * form is what this file carried until 2026-10-14, and it is the single cause
 * of the superseded sections 19, 27 and 30-33 of the findings doc. See
 * docs/mc68010-boot-findings.md section 34. */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Region sizes, shared with native/musashi_cpu.h so the two cannot drift.
 *
 * GSP_BOARD_VRAM_BYTES is the PHYSICAL share: 512 KiB. SP-338 page 10 labels
 * the silicon 'VRAM Array (16) 64K x 4' -- sixteen 64Kx4 chips are 16 x 256
 * Kbit = 4 Mbit = 512 KiB -- and MAME's own m_gsp_vram reports
 * `share bytes=524288 words=262144 vram_mask=0x003ffff`.
 *
 * It was 0x400000 (4 MiB) until 2026-10-14, which was wrong by a factor of 8
 * and produced three superseded sections of the findings doc. The cause is the
 * same in all of them: the TMS34010 program space is configured `datawidth 16,
 * addrshift 3`, so ONE PROGRAM-SPACE ADDRESS UNIT IS ONE BIT. A 4 MiB read
 * across 0xff800000-0xffffffff is 4 MiB of address span, not of physical bytes.
 *
 *     byte_offset = ((addr - 0xFF800000) >> 3) & 0x7FFFF
 *     word_index  = byte_offset >> 1
 *
 * The ADDRESS range in the map is unchanged -- MAME still maps
 * 0xff800000-0xffffffff -- it is the share behind it that is 512 KiB.
 * See docs/mc68010-boot-findings.md section 34. */
#define GSP_BOARD_VRAM_BYTES      0x80000u  /* 512 KiB share, 0x20000 words */
#define GSP_BOARD_VRAM2BPP_BYTES  0x100000u /* 0x02000000-0x020FFFFF (1 MiB, no mirroring) */
#define GSP_BOARD_PAL_BYTES       0x800u   /* 1024 entries * 2 bytes, per plane */
#define GSP_BOARD_CTRL_BYTES      0x100u   /* one 256-byte share per plane     */

/* How many distinct unmapped addresses the board remembers. */
#define GSP_BOARD_UNMAPPED_TRACK  12u

/* Host port registers, indexed by the decoded HFS1:HFS0 selector.
 *
 * `gsp_board_host_reg()` yields these indices from a host-window byte
 * offset.  The order is the TI 1988 User's Guide Table 10-1 order, and it
 * matches both the vendored RTL's `host_reg_sel_t`
 * (third_party/tms34010_sv/rtl/tms34010_pkg.sv:401-406) and the measured
 * native probe (docs/mc68010-boot-findings.md section 13, which reports MAME
 * reg 0 HSTADRL at word index 0x02 = byte offset 0x04).
 *
 * Watch the HSTADRL/HSTADRH pair: they are adjacent and are the easiest thing
 * to transpose.  Byte offset 0x00 selects HSTADRH, byte offset 0x04 selects
 * HSTADRL. */
enum {
    GSP_BOARD_HSTADRL = 0,
    GSP_BOARD_HSTADRH = 1,
    GSP_BOARD_HSTDAT  = 2,
    GSP_BOARD_HSTCTL  = 3
};

typedef struct {
    uint8_t  vram[GSP_BOARD_VRAM_BYTES];
    uint8_t  vram2bpp[GSP_BOARD_VRAM2BPP_BYTES];
    uint8_t  pal_lo[GSP_BOARD_PAL_BYTES];
    uint8_t  pal_hi[GSP_BOARD_PAL_BYTES];
    uint8_t  ctrl_lo[GSP_BOARD_CTRL_BYTES];
    uint8_t  ctrl_hi[GSP_BOARD_CTRL_BYTES];
    uint8_t  palette_bank; /* control-high write-address-selected bank */

    /* Host-port state.  HSTADR is held masked to its 16-byte cell: MAME
     * applies `addr & 0xfffffff0` before the store (tms34010.cpp:1898), and a
     * data write FILLS that whole cell.  See the note on
     * gsp_board_hstdat_store's VRAM branch. */
    uint32_t hstadr;      /* composed HSTADRH:HSTADRL, 16-byte aligned    */
    uint16_t hstdat;      /* HSTDAT write/read register          */
    uint16_t hstctl;      /* HSTCTL, including the 0x0800 increment bit */

    /* Observable counters.  These are what a test asserts on; they exist so
     * "it ran" can never be mistaken for "it did the right thing". */
    uint64_t word_reads;
    uint64_t word_writes;
    uint64_t unmapped_reads;
    uint64_t unmapped_writes;

    /* A bare unmapped counter cannot be acted on: it says the core left the
     * map but not where it went, and "117743" is not a diagnosis.  Track the
     * first few distinct addresses with their counts, which is enough to name
     * the missing region.  Bounded and fixed-size on purpose: this must not
     * grow with a 478M-cycle run. */
    struct {
        uint32_t addr;
        uint64_t count;
    } unmapped_read_at[GSP_BOARD_UNMAPPED_TRACK];

    uint64_t vram_writes;
    uint64_t host_writes;
} gsp_board_t;

/* Zero the whole model, including counters and host-port state. */
void gsp_board_reset(gsp_board_t *b);

/* Decode a host-window byte offset to a host register index.
 *
 * The transform is `(byte_offset >> 2) ^ 1`, matching
 * native/musashi_cpu.c:777.  MAME's own handler uses `(offset / 2) ^ 1`,
 * but that is the SAME access expressed in MAME's word index rather than our
 * byte offset -- see docs/mc68010-boot-findings.md section 13.  Applying
 * MAME's form to a byte offset is refuted with measurement: it routes HSTDAT
 * to an offset the ROM never writes and drops the frame-600 host-write count
 * from 372,769 to zero.  Do not "fix" this to match MAME. */
unsigned gsp_board_host_reg(uint32_t byte_offset);

/* Read/write the GSP program space at a 32-bit byte address.  Addresses
 * outside `multisync_gsp_map` are unmapped: MAME opens with
 * `map.unmap_value_high()`, so a read returns 0 and a write is discarded
 * while still being counted. */
uint16_t gsp_board_read16(gsp_board_t *b, uint32_t byte_addr);
void     gsp_board_write16(gsp_board_t *b, uint32_t byte_addr, uint16_t value);

/* One MC68010 host-port transaction.  `byte_offset` is the offset within
 * MAME's mapped $C00000-$C03FFF window. */
void gsp_board_host_write(gsp_board_t *b, uint32_t byte_offset, uint16_t data);
uint16_t gsp_board_host_read(gsp_board_t *b, uint32_t byte_offset);

/* True when the address falls in a mapped region.  Used by tests to assert
 * the map itself rather than just the effects of writes to it. */
int gsp_board_is_mapped(uint32_t byte_addr);

/* How many VRAM words are non-zero.  Used to decide when the GSP's program is
 * actually present: the entry vector lives in VRAM, so "is the program
 * uploaded yet" has no cheaper honest answer than looking at VRAM. */
unsigned gsp_board_vram_nonzero_words(const gsp_board_t *b);

#ifdef __cplusplus
}
#endif

#endif /* STUNRUN_GSP_BOARD_H */
