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

/* Returns non-zero if the GSP is halted and should not be stepped. */
int gsp_rtl_driver_is_halted(void);

/* Step the GSP for gsp_cycles clock ticks.  Returns the number actually
 * ticked (may be less if the GSP self-halts). */
uint64_t gsp_rtl_driver_step(uint64_t gsp_cycles);

/* Inject a host-port write from the MC68010 into the RTL.
 * reg: 0=HSTADRL, 1=HSTADRH, 2=HSTDAT, 3=HSTCTL. */
int gsp_rtl_driver_host_write(unsigned reg, uint16_t data);

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

/* Shutdown and free the RTL. */
void gsp_rtl_driver_shutdown(void);

#ifdef __cplusplus
}  /* end extern "C" */
#endif

#endif  /* STUNRUN_GSP_RTL_DRIVER_H */
