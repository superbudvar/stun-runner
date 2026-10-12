/* GSP RTL driver for the native shell.
 *
 * One gsp_board_t is shared between the MC68010 (musashi) and the GSP RTL.
 * The MC68010 host-port writes go into the shared board via
 * musashi_cpu → gsp_board_host_write().  The GSP RTL reads/writes the
 * same board via tick().
 *
 * After each MC68010 frame:
 *   1. Sync MC68010 host register state into the shared board
 *   2. Step the GSP for the frame's worth of GSP cycles
 *   3. Copy the shared board VRAM back into musashi's gsp_vram
 *      (so the rasterizer sees the GSP's pixel writes)
 *
 * The rasterizer reads musashi's gsp_vram via musashi_cpu_gsp_memory().
 *
 * Clock ratio: GSP 40 MHz / MC68010 8 MHz = 5:1.
 *
 * Signal names match tms34010_sv (Verilated) as used by gsp_rtl_main.cpp.
 * The RTL uses host_req_i / host_ack_o for the host transaction handshake,
 * driven by the board model — NOT by the driver directly.  The driver only
 * calls eval() to advance the clock.
 */

#include "gsp_rtl_driver.h"

#undef GSP_CYCLE_RATIO  /* avoid conflict with constexpr below */

#include "tms34010_sv.h"
#include "verilated.h"
#include "gsp_board.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

/* gsp_board_t is shared with musashi */
static gsp_board_t           *s_board = nullptr;
static tms34010_sv           *s_top  = nullptr;
static bool                   s_init_done = false;
static bool                   s_gsp_halted = true;

/* Shadow display-register state, updated from GSP I/O register bus cycles.
 * The TMS34010 display registers (DPYSTRT, DPYCTL, DPYTAP, DPYADR) live at
 * 0xF6000010-0xF6000017 as 16-bit words.  When the GSP executes a
 * LOCAL_CYCLE_IO_WRITE to one of these addresses, we capture the value so
 * the shell can read it after a frame step.
 *
 * These are initialised to the frame-600 observed values so a frame-0 call
 * to gsp_rtl_driver_get_display_regs() returns sane data even before the GSP
 * has run any display-list microcode. */
static uint16_t s_dpy_regs[4] = {  /* [0]=DPYSTRT, [1]=DPYADR, [2]=DPYTAP, [3]=DPYCTL */
    0x00F0u,   /* dpystrt:  rowaddr=0x3C, base=0x3C<<10=0xF000 */
    0xFFC3u,   /* dpyadr:   dpyadr=rowaddr^0xFFFC=0x3C^0xFFFC=0xFFC3 */
    0x0200u,   /* dpytap:   from frame-600 capture */
    0x8201u    /* dpyctl:   ENV=1(display enable), SRE=1, ORG=0, DUDATE=0x004 */
};

/* 5:1 GSP:MC68010 cycle ratio (40 MHz / 8 MHz) */
static constexpr unsigned GSP_CYCLE_RATIO = 5;

/* Run the GSP for gsp_cycles, returning actual cycles ticked.
 * Halts early if the GSP self-halts (no host traffic for a full line).
 * Uses the harness tick() pattern from gsp_rtl_main.cpp: clock with eval(),
 * service any pending cycle_req_o, track host_ack_o for idle detection. */
static uint64_t run_gsp(uint64_t gsp_cycles)
{
    uint64_t ticked = 0;
    bool idle_line = true;

    for (uint64_t i = 0; i < gsp_cycles; i++) {
        bool had_req = (s_top->host_req_i != 0);
        bool cycle_pending = false;

        /* Service any request the core raised on the previous cycle.
         * The bus arbiter holds cycle_req_o until it sees cycle_ack_i. */
        if (cycle_pending) {
            const unsigned kind = s_top->cycle_kind_o & 0xf;
            const uint32_t a = static_cast<uint32_t>(s_top->cycle_addr_o);

            if (kind == 1) {  /* KIND_WORD_WRITE */
                gsp_board_write16(s_board, a,
                                  static_cast<uint16_t>(s_top->cycle_wdata_o));
            }
            s_top->cycle_ack_i = 1;
            cycle_pending = false;
        } else {
            s_top->cycle_ack_i = 0;
        }

        if (!cycle_pending && s_top->cycle_req_o) {
            cycle_pending = true;
        }

        /* Rising edge */
        s_top->clk = 1;
        s_top->eval();

        /* Track GSP display-register writes from the I/O bus.
         * LOCAL_CYCLE_IO_WRITE (kind==6) to 0xF6000010-0xF6000017 updates
         * the shadow display-register state. */
        if (cycle_pending && s_top->cycle_kind_o == 6) {
            uint32_t addr = static_cast<uint32_t>(s_top->cycle_addr_o);
            uint16_t wdata = static_cast<uint16_t>(s_top->cycle_wdata_o);
            if (addr == 0xF6000010u)       s_dpy_regs[3] = wdata;  /* DPYCTL */
            else if (addr == 0xF6000012u) s_dpy_regs[0] = wdata;  /* DPYSTRT */
            else if (addr == 0xF6000014u) s_dpy_regs[2] = wdata;  /* DPYTAP */
            else if (addr == 0xF6000016u) s_dpy_regs[1] = wdata;  /* DPYADR */
        }

        /* Falling edge */
        s_top->clk = 0;
        s_top->eval();

        ticked++;
        if (s_top->host_ack_o || had_req) {
            idle_line = false;
            s_gsp_halted = false;
        }
        /* If we've been idle for a full scanline (2535 cycles), assume
         * the GSP has hit a STOP and is waiting. */
        if (idle_line && ticked >= 2535) {
            s_gsp_halted = true;
            break;
        }
    }
    return ticked;
}

}  // anonymous namespace

extern "C" {

void gsp_rtl_driver_init(gsp_board_t *board,
                         const uint8_t *vram_image,
                         size_t vram_image_words)
{
    if (s_init_done) return;
    s_board = board;

    s_top = new tms34010_sv;

    /* Default inputs — matching gsp_rtl_main.cpp initialisation */
    s_top->run_emu_n_i = 1;
    s_top->lint1_n_i = 1;
    s_top->lint2_n_i = 1;
    s_top->hold_req_i = 0;
    s_top->dpyint_set_i = 0;
    s_top->video_hsync_n_i = 1;
    s_top->video_vsync_n_i = 1;
    s_top->host_be_i = 3;    /* 16-bit */
    s_top->host_req_i = 0;
    s_top->host_we_i  = 0;
    s_top->host_reg_i = 0;
    s_top->host_wdata_i = 0;
    s_top->hcs_n_i = 1;     /* held in reset */
    s_top->rst = 1;
    s_top->vclk_rst_i = 1;
    s_top->clk = 0;
    s_top->eval();

    /* Seed VRAM if provided.
     * vram_image is packed uint8_t (byte-offset), one byte per address.
     * gsp_board.vram is also packed uint8_t. */
    if (vram_image && vram_image_words > 0) {
        size_t words_to_copy = vram_image_words;
        if (words_to_copy > GSP_BOARD_VRAM_BYTES / 2)
            words_to_copy = GSP_BOARD_VRAM_BYTES / 2;
        for (size_t i = 0; i < words_to_copy; i++) {
            uint32_t off = (uint32_t)i * 2;
            s_board->vram[off + 0] = vram_image[i * 2 + 0];
            s_board->vram[off + 1] = vram_image[i * 2 + 1];
        }
    }

    /* Release reset: GSP fetches its boot vector from VRAM.
     * The MC68010 has already uploaded the GSP program at this point. */
    s_top->rst = 0;
    s_top->vclk_rst_i = 0;
    s_top->hcs_n_i = 0;     /* /GSPRES released — ROM-less boot */
    s_top->eval();

    /* Boot: run until the GSP halts at its first STOP instruction.
     * In the real boot sequence this is $0216 (stop #$2700).
     * Allow up to 200k cycles for boot — should halt well before that. */
    s_gsp_halted = false;
    for (unsigned i = 0; i < 200000; i++) {
        /* One clock tick: rising + falling edge with eval() */
        s_top->clk = 1;
        s_top->eval();
        s_top->clk = 0;
        s_top->eval();
        if (i > 1000 && !s_top->host_req_i && !s_top->host_ack_o) {
            s_gsp_halted = true;
            break;
        }
    }

    s_init_done = true;
    printf("gsp_rtl_driver: init done, halted=%d\n", s_gsp_halted ? 1 : 0);
}

/* Full per-frame sync: copy musashi VRAM → shared board, step GSP, copy back.
 * musashi_gsp_vram: musashi_cpu's gsp_vram (uint16_t packed).
 * word_count: number of uint16_t words. */
void gsp_rtl_driver_sync_and_step(const uint16_t *musashi_gsp_vram,
                                  size_t word_count)
{
    if (!s_init_done || !s_board) return;

    /* Copy musashi VRAM → shared board VRAM.
     * The MC68010 writes pixel data to musashi's gsp_vram via the host port.
     * We need that data in s_board->vram so the GSP RTL can see it. */
    if (musashi_gsp_vram && word_count > 0) {
        size_t limit = word_count < GSP_BOARD_VRAM_BYTES / 2
                           ? word_count : GSP_BOARD_VRAM_BYTES / 2;
        for (size_t i = 0; i < limit; i++) {
            uint32_t off = (uint32_t)i * 2;
            uint16_t w = musashi_gsp_vram[i];
            s_board->vram[off + 0] = (uint8_t)(w & 0xFFu);
            s_board->vram[off + 1] = (uint8_t)(w >> 8);
        }
    }

    /* Step the GSP for one frame's worth of cycles.
     * MC68010 frame = 132886 cycles × 5 = 664430 GSP cycles. */
    if (!s_gsp_halted) {
        uint64_t gsp_frame_cycles = 132886ULL * GSP_CYCLE_RATIO;
        run_gsp(gsp_frame_cycles);
    }
}

/* Copy shared board VRAM back into musashi VRAM for the rasterizer. */
void gsp_rtl_driver_sync_to_musashi(uint16_t *musashi_gsp_vram,
                                    size_t word_count)
{
    if (!s_init_done || !s_board || !musashi_gsp_vram) return;

    size_t limit = word_count < GSP_BOARD_VRAM_BYTES / 2
                       ? word_count : GSP_BOARD_VRAM_BYTES / 2;
    for (size_t i = 0; i < limit; i++) {
        uint32_t off = (uint32_t)i * 2;
        uint16_t w = (uint16_t)(s_board->vram[off + 0]) |
                    ((uint16_t)(s_board->vram[off + 1]) << 8);
        musashi_gsp_vram[i] = w;
    }
}

int gsp_rtl_driver_is_halted(void)
{
    return s_init_done ? (s_gsp_halted ? 1 : 0) : 1;
}

uint64_t gsp_rtl_driver_step(uint64_t gsp_cycles)
{
    if (!s_init_done || s_gsp_halted) return 0;
    return run_gsp(gsp_cycles);
}

/* Inject a host-port write from the MC68010 into the RTL.
 * reg: 0=HSTADRL, 1=HSTADRH, 2=HSTDAT, 3=HSTCTL.
 *
 * NOTE: the RTL's host_req_i / host_ack_o handshake is driven by the board
 * model inside run_gsp().  This function only injects a write into the
 * board model's register state — the next GSP step will present it. */
int gsp_rtl_driver_host_write(unsigned reg, uint16_t data)
{
    if (!s_init_done || !s_board) return -1;
    /* Map reg (0=HSTADRL, 1=HSTADRH, 2=HSTDAT, 3=HSTCTL) to byte offset.
     * Matches gsp_rtl_main.cpp host_txn() lambda and gsp_board.c */
    uint32_t offsets[4] = { 0x04, 0x00, 0x0C, 0x08 };
    if (reg > 3) return -1;
    gsp_board_host_write(s_board, offsets[reg], data);
    return 0;
}

/* Read the GSP's current display-register state after a GSP frame step.
 * Returns the four live display registers (DPYSTRT, DPYADR, DPYTAP, DPYCTL)
 * from the shadow state maintained during run_gsp().
 *
 * The RTL does not currently expose these as top-level signals; this function
 * tracks GSP bus cycles that write to the display-register address range and
 * maintains shadow state. */
void gsp_rtl_driver_get_display_regs(uint16_t *dpystrt,
                                     uint16_t *dpyadr,
                                     uint16_t *dpytap,
                                     uint16_t *dpyctl)
{
    if (!s_init_done) {
        if (dpystrt) *dpystrt = 0x00F0u;
        if (dpyadr)  *dpyadr  = 0xFFC3u;
        if (dpytap)  *dpytap  = 0x0200u;
        if (dpyctl)   *dpyctl  = 0x8201u;
        return;
    }
    if (dpystrt) *dpystrt = s_dpy_regs[0];
    if (dpyadr)  *dpyadr  = s_dpy_regs[1];
    if (dpytap)  *dpytap  = s_dpy_regs[2];
    if (dpyctl)   *dpyctl  = s_dpy_regs[3];
}

void gsp_rtl_driver_shutdown(void)
{
    if (s_top) { delete s_top; s_top = nullptr; }
    s_board = nullptr;
    s_init_done = false;
}

}  // extern "C"
