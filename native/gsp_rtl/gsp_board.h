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
 * Addresses here are 32-bit BYTE addresses as MAME's program space sees
 * them.  The RTL core presents a bit-addressed architectural field bus and
 * performs the bit-to-physical-word narrowing itself inside
 * tms34010_field_sequencer, so the harness converts to byte address before
 * calling into this model. */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Region sizes, shared with native/musashi_cpu.h so the two cannot drift. */
#define GSP_BOARD_VRAM_BYTES      0x80000u /* 512 KiB share, 0x40000 words   */
#define GSP_BOARD_VRAM2BPP_BYTES  0x100000u /* 0x02000000-0x020FFFFF          */
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

    /* Host-port state.  Addresses are 16-byte cells, per the TI guide and
     * the board's observed host-write expansion. */
    uint32_t hstadr;      /* composed HSTADRH:HSTADRL, in cells */
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