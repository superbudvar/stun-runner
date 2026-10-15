/* C-callable interface to the GSP RTL driver.
 *
 * Design:
 *   One gsp_board_t is shared between the MC68010 (musashi) and the GSP RTL.
 *   The MC68010 host-port writes go into the shared board via
 *   musashi_cpu → gsp_board_host_write().  The GSP RTL reads/writes the
 *   same board via tick().
 *
 *   Per frame:
 *     1. gsp_rtl_driver_sync_and_step() copies musashi VRAM → shared board,
 *        steps the GSP for 664430 cycles (132886 MC68010 × 5:1 ratio),
 *        then copies shared board VRAM → musashi VRAM.
 *     2. The rasterizer reads musashi's VRAM via musashi_cpu_gsp_memory().
 *
 *   The GSP self-halts on STOP instruction; we detect this by running the
 *   full cycle budget with no host_req_o activity.
 *
 * Usage:
 *   gsp_board_t board;
 *   gsp_board_reset(&board);
 *   // MC68010 must use the shared board (musashi_cpu_set_gsp_board)
 *   gsp_rtl_driver_init(&board, NULL, 0);
 *   // After each MC68010 frame:
 *   gsp_rtl_driver_sync_and_step(musashi_vram, word_count);
 *   // OR, for manual control:
 *   if (!gsp_rtl_driver_is_halted())
 *       gsp_rtl_driver_step(664430);  // one frame of GSP cycles
 *   gsp_rtl_driver_shutdown();
 */
#ifndef STUNRUN_GSP_RTL_DRIVER_H
#define STUNRUN_GSP_RTL_DRIVER_H

#include <stddef.h>
#include <stdint.h>

/* gsp_board_t is defined in gsp_board.h; include it so we use the typedef */
#include "gsp_board.h"

#define GSP_CYCLE_RATIO 5  /* 40 MHz GSP / 8 MHz MC68010 */

#ifdef __cplusplus
extern "C" {
#endif

/* Initialise the GSP RTL and bind it to the shared board.
 * vram_image: optional seed VRAM image (may be NULL).
 * vram_image_words: number of 16-bit words in vram_image. */
void gsp_rtl_driver_init(gsp_board_t *board,
                         const uint8_t *vram_image,
                         size_t vram_image_words);

/* Replay-only initialization: attach the RTL and leave it held in reset. The
 * capture's own reset/host events then establish the state; do not run the
 * driver's normal empty-board boot loop first. */
void gsp_rtl_driver_init_replay(gsp_board_t *board);

/* Full per-frame sync: copy musashi VRAM → shared board, step GSP, copy back.
 * musashi_gsp_vram: musashi_cpu's gsp_vram, exposed as 16-bit words.
 * word_count: MUSASHI_GSP_VRAM_WORDS, i.e. the whole 512 KiB share.
 *
 * Both sides are the same 512 KiB byte array since the unit correction
 * (findings section 34). The previous de-interleave loop byte-swapped every
 * word on the way through, which belonged to the refuted 4 MiB packed model:
 * musashi reinterprets its uint8_t share as uint16_t words, and the board is
 * byte-addressed, so the words are ALREADY in order and swapping them corrupted
 * every pixel. A straight memcpy is the correct transfer. */
void gsp_rtl_driver_sync_and_step(const uint16_t *musashi_gsp_vram,
                                   size_t word_count);

/* Copy the core's VRAM back out, so the rasteriser reads what the GSP produced
 * rather than what the MC68010 uploaded.  Same convention as sync_and_step,
 * for the same reason. */
void gsp_rtl_driver_sync_to_musashi(uint16_t *musashi_gsp_vram,
                                    size_t word_count);

/* Copy the BOARD's colour planes back into musashi's, for the same reason as
 * the VRAM above and with the same history.
 *
 * The palette lives at 0xF5000000-0xF50007FF, which is NOT the VRAM share, so
 * neither of the VRAM sync functions ever touched it: the MC68010's palette
 * writes went into gsp_board_t::pal_lo/pal_hi and were then read by nobody.
 * The scanout kept rendering musashi's own planes, which no RTL-path writer
 * ever populated -- so every pixel came out through an all-zero palette.
 *
 * Both are 0x800 bytes of packed little-endian 16-bit entries (8 bits of
 * intensity plus 3 bits of entry select); there is nothing to convert, so
 * this is a plain memcpy like its VRAM counterpart. */
void gsp_rtl_driver_sync_palette_to_musashi(uint16_t *musashi_pal_lo,
                                            uint16_t *musashi_pal_hi);

/* Returns non-zero if the GSP is halted and should not be stepped. */
int gsp_rtl_driver_is_halted(void);

/* Step the GSP for gsp_cycles clock ticks.  Returns the number actually
 * ticked (may be less if the GSP self-halts). */
uint64_t gsp_rtl_driver_step(uint64_t gsp_cycles);

/* Inject a host-port write from the MC68010 into the RTL.
 * reg: 0=HSTADRL, 1=HSTADRH, 2=HSTDAT, 3=HSTCTL. */
int gsp_rtl_driver_host_write(unsigned reg, uint16_t data);

/* Apply an observed active-low /GSPRES transition from an oracle capture. */
int gsp_rtl_driver_set_reset(int released);

/* Read the GSP's current display-register state after a GSP frame step.
 * Returns the four live display registers (DPYSTRT, DPYADR, DPYTAP, DPYCTL)
 * from the RTL model.  The RTL must have been stepped since the last MC68010
 * frame for these to be meaningful.
 *
 * The RTL currently does NOT expose these as top-level signals; this function
 * tracks GSP bus cycles that write to the display-register address range and
 * maintains shadow state.  RTL integration TODO: add debug ports to
 * tms34010_system.sv that expose io_reg[IO_IDX_DPY*] directly. */
void gsp_rtl_driver_get_display_regs(uint16_t *dpystrt,
                                     uint16_t *dpyadr,
                                     uint16_t *dpytap,
                                     uint16_t *dpyctl);

/* Provenance for the values above: bit n set (bit 0 DPYSTRT, 1 DPYADR,
 * 2 DPYTAP, 3 DPYCTL) means that register is the CORE's own value. A clear bit
 * means it is still the initialiser this driver was built with. Print it
 * next to the values -- an unmarked register is a guess wearing a number. */
uint32_t gsp_rtl_driver_display_regs_seen(void);

/* Count of GSP-local INTENB I/O writes and observed bit-7 edge transitions.
 * Observation only; these are not wired to the MC68010 interrupt input. */
void gsp_rtl_driver_intenb_observation(uint32_t *writes, uint32_t *rises,
                                       uint32_t *falls, uint16_t *last);
/* Active-low HINT output, driven by HSTCTLL.INTOUT; observation only.
 *
 * DO NOT WIRE THIS TO IPL2.  HINT is the host-side "hint" pin; on the real
 * TMS34010 it is a different signal from INT, and MAME's external INT output
 * follows HSTCTLL.INTOUT through tms34010.cpp:1827-1830 (processor side) and
 * :1682-1685 (host side).  The RTL exposes only the HINT pin
 * (tms34010_io_regs.sv:492); there is no INT output pin at all.  Use
 * gsp_rtl_driver_intout_level() below for IPL2. */
void gsp_rtl_driver_hint_observation(uint32_t *asserts, uint32_t *clears,
                                     int *asserted);

/* ---- HSTCTLL.INTOUT: the GSP-to-host interrupt, the IPL2 source -------
 *
 * MAME asserts the GSP's INT pin on a RISING EDGE of HSTCTLL bit 7 (INTOUT)
 * and deasserts it on the falling edge:
 *
 *   tms34010.cpp:1661-1678  (both arms of `case REG_HSTCTLL`, selected by
 *                            m_external_host_access; host_control_w sets it for
 *                            one write at :2039-2042)
 *   tms34010.cpp:1682-1685  (the edge)
 *       newreg = (oldreg & 0xfff8) | (data & 0x0007);   // host: MSGIN
 *       newreg &= data | ~0x0080;                      // host: INTOUT clear-only
 *       newreg |= data & 0x0008;                       // host: INTIN set-only
 *   or, processor side:
 *       newreg  = (oldreg & 0xff8f) | (data & 0x0070); // MSGOUT
 *       newreg |= data & 0x0080;                       // INTOUT set-only
 *       newreg &= data | ~0x0008;                      // INTIN clear-only
 *       if (!(oldreg & 0x0080) && (newreg & 0x0080)) m_output_int_cb(1);
 *       else if ((oldreg & 0x0080) && !(newreg & 0x0080)) m_output_int_cb(0);
 *
 * harddriv.cpp:1489 binds output_int to hdgsp_irq_gen, and
 * harddriv_m.cpp:97 drives m_maincpu->set_input_line(3, ...), i.e. IPL2.
 *
 * The level reported here is a LEVEL, not an event: callers should apply it on
 * a change, which is exactly what m_output_int_cb's caller does. */
int gsp_rtl_driver_intout_level(void);

/* Everything measured about INTOUT, for the report.  `mismatch` is the count of
 * settled-state comparisons between this driver's HSTCTLL shadow and the RTL's
 * own io_reg[IO_IDX_HSTCTLL] that disagreed; it must be 0 for the shadow to be
 * a faithful account of the RTL, and is how we would notice a write path the
 * bus trace cannot see (a host-indirect HSTCTLL access, say). */
#define GSP_RTL_INTOUT_IO_MAX 48
typedef struct gsp_rtl_intout_obs {
    uint32_t proc_hstctll_writes;  /* processor-side writes the RTL decodes as HSTCTLL */
    uint32_t host_hstctl_writes;   /* MC68010 HSTCTL writes that reach HSTCTLL's low byte */
    /* Processor-side writes at 0xC000000F, which is the address MAME decodes as
     * HSTCTLL (offset 15 in internal_regs_map) while the RTL decodes 0xC00000F0
     * (index = addr[8:4]).  Reported so "which decode can reach HSTCTLL" is a
     * measurement rather than an assumption; see gsp_rtl_driver.cpp. */
    uint32_t mame_rule_hstctll_writes;
    uint32_t rises;                /* INTOUT 0 -> 1 */
    uint32_t falls;                /* INTOUT 1 -> 0 */
    int      level;                /* current INTOUT, 0 or 1 */
    /* Who moved the bit last: 0 none, 1 processor-side local-bus write,
     * 2 MC68010 host HSTCTL write, 3 adopted from the RTL's own io_reg.
     * `set_data`/`clear_data` are the word that moved it. */
    uint32_t set_writer, clear_writer;
    uint16_t set_data, clear_data;
    uint16_t shadow;               /* this driver's HSTCTLL shadow */
    uint16_t rtl_io_reg;           /* the RTL's own io_reg[HSTCTLL], when exposed */
    uint32_t rtl_available;        /* 0 = Verilator did not expose io_reg */
    uint32_t mismatch;             /* settled comparisons that disagreed */
    uint32_t io_writes;            /* all processor-side I/O-space writes seen */
    uint32_t io_distinct;          /* distinct I/O addresses in `io_addr` (capped) */
    uint32_t io_overflow;          /* distinct addresses dropped past the cap */
    uint32_t io_addr[GSP_RTL_INTOUT_IO_MAX];      /* distinct addresses */
    uint32_t io_count[GSP_RTL_INTOUT_IO_MAX];     /* how many times each */
} gsp_rtl_intout_obs_t;

void gsp_rtl_driver_intout_observation(gsp_rtl_intout_obs_t *out);

/* Shutdown and free the RTL. */
void gsp_rtl_driver_shutdown(void);

#ifdef __cplusplus
}  /* end extern "C" */
#endif

#endif  /* STUNRUN_GSP_RTL_DRIVER_H */
