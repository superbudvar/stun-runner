/* Board interface for the transcribed 68010 boot and service-mode routines.
 *
 * WHY THIS EXISTS. The routines in reproduction/maincpu/{boot_chain,
 * adsp_liveness,adsp_board_test,text_draw}.c are ports of real 68010 code
 * read off the main-CPU ROM. Each one talks to the board purely through
 * address accesses: a read of $60c001, a write to $808006, a store into the
 * GSP host port. To port them as readable C -- and to unit-test them without
 * an emulator -- those accesses have to become a contract rather than inline
 * memory poking. That contract is this header.
 *
 * It is deliberately a plain struct of function pointers with a `void *ctx`,
 * not a bus object, a device class, or a global. A slice test supplies a
 * recording stub and asserts the *sequence of board accesses the ROM code
 * performs*; that sequence is the observable behaviour, per the repo rule
 * that a read disassembly is not a deliverable until its observable output is
 * asserted.
 *
 * ADDRESS CONVENTIONS
 *
 *  - ROM accesses take the 68010 address as a 32-bit value ($00800000 etc.),
 *    matching the ROM's own absolute-long addressing.
 *  - ADSP data-RAM accesses take the *window offset* (0..0x1fff), not the
 *    68010 address. MAME's hd68k_adsp_data_r/w (harddriv_m.cpp:651,657) are a
 *    flat indexed array -- `m_adsp_data_memory[offset]` -- so the offset IS
 *    the DM word index. The ROM relies on this: $b864 writes $1234 to $808006
 *    and then reads $80800a expecting the DSP to have copied it.
 *  - ADSP control accesses likewise take the window offset into the
 *    0x818000-0x81801f control register (hd68k_adsp_control_w:751). The ROM
 *    uses offsets $0a, $0c, $0e, $60, $1a, $1c, $1e.
 *  - GSP host-port accesses take the offset within $c00000-$c03fff.
 *
 * NO ROM CONTENTS ARE EMBEDDED OR REQUIRED by this header, and none of the
 * routines that use it require a ROM at run time except the ADSP program
 * downloader, which reads through read_rom8/read_rom16 and therefore needs a
 * test to supply a synthetic program image.
 */
#ifndef STUNRUN_BOARD_IFACE_H
#define STUNRUN_BOARD_IFACE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 68010-visible windows these routines touch ------------------------- */

/* Main-CPU ROM. The 68010 sees 1 MiB at $000000-$0fffff plus banks at
 * $020000; the routines here address the fixed region only. */
#define STUNRUN_ROM_BASE      0x00000000u
#define STUNRUN_ROM_LAST      0x000FFFFFu

/* $60c000-0x60ffff, hd68k_port0_r (harddriv_m.cpp:267).
 *   bit1 /HSYNCB  bit2 /VSYNCB  bit3 EOC12  bit5 SELF-TEST
 * SELF-TEST is PORT_SERVICE(0x20, IP_ACTIVE_LOW) (harddriv.cpp:798), so a
 * CLEAR bit 5 means Service Mode is *pressed*. */
#define STUNRUN_PORT0_BASE    0x0060C000u
#define STUNRUN_PORT0_BIT_HSYNCB 0x0002u
#define STUNRUN_PORT0_BIT_VSYNCB 0x0004u
#define STUNRUN_PORT0_BIT_EOC12  0x0008u
#define STUNRUN_PORT0_BIT_SELFTEST 0x0020u

/* $a80000-0xafffff, hd68k_a80000_r: the shifter/button port. harddriv.cpp
 * gives bit0 BUTTON1, bit1 BUTTON2, bit2 IPT_START1. */
#define STUNRUN_A80000_BASE   0x00A80000u

/* $604000-0x607fff, hd68k_nwr_w (harddriv_m.cpp:466): bit3 is the value,
 * offset&7 the function (4 = ZP1, 5 = ZP2). */
#define STUNRUN_NWR_BASE      0x00604000u

/* $608000-0x60bfff, write-only, harddriv.cpp:603 maps this to the watchdog.
 * clr.w $608000 is the service kick. */
#define STUNRUN_WATCHDOG_ADDR 0x00608000u

/* $c00000-0xc03fff, the GSP host port. The 68010 draws ALL text through it;
 * see text_draw.c. */
#define STUNRUN_GSP_HOST_BASE 0x00C00000u

/* ADSP windows, init_adsp() at harddriv.cpp:4822-4836. */
#define STUNRUN_ADSP_PROG_BASE 0x00800000u   /* program RAM, 32 KiB window   */
#define STUNRUN_ADSP_DM_BASE   0x00808000u   /* data RAM, flat 16 KiB window  */
#define STUNRUN_ADSP_CTRL_BASE 0x00818000u   /* control, 0x818000-0x81801f   */
#define STUNRUN_ADSP_IRQ_ADDR  0x00838000u   /* hd68k_adsp_irq_state_r       */

/* DM offsets the ROM's liveness tests use. */
#define STUNRUN_ADSP_DM_LEN   0x2000u
#define STUNRUN_DM_ADDR_REG   0x0000u        /* $808000 */
#define STUNRUN_DM_DATA_REG   0x0002u        /* $808002 */
#define STUNRUN_DM_TEST_WORD  0x0006u        /* $808006 */
#define STUNRUN_DM_READBACK   0x000Au        /* $80800a */
#define STUNRUN_DM_SCRATCH_0  0x000Cu        /* $80800c, first of six       */
#define STUNRUN_DM_SCRATCH_N  6u

/* Control-register offsets the ROM drives.
 *
 * NAMES CORRECTED 2026-10-18. These were first written as LEDS/BR/FLAG by
 * analogy with hd68k_adsp_control_w's comment block without checking the
 * decode, and two of the four were wrong. hd68k_adsp_control_w
 * (harddriv_m.cpp:751) computes `val = (offset >> 3) & 1` and selects on
 * `offset & 7`, so what a given offset means is decided by its low three
 * bits, not by the offset itself:
 *
 *   offset&7 == 0,1  LEDs                (no-op)
 *   offset&7 == 2    default             -- logs only, nothing happens
 *   offset&7 == 3    SOM bank (deferred)
 *   offset&7 == 5    /BR   bus request   -- halts the ADSP
 *   offset&7 == 6    /HALT               -- halts the ADSP
 *   offset&7 == 7    reset
 *
 * So $81800a is select 2 (a no-op, NOT an LED write) and $81800e is select 6
 * (/HALT, NOT /BR). $818060 is not this handler at all: harddriv.cpp:4835 maps
 * 0x818060-0x81807f to hd68k_adsp_irq_clear_w.
 *
 * Only $81801e (release /HALT) and $818060 (IRQ clear) do anything MAME
 * models. The rest are log lines. */
#define STUNRUN_ADSP_CTRL_NOOP_0A    0x0Au  /* select 2 -- no-op in MAME    */
#define STUNRUN_ADSP_CTRL_HALT       0x0Eu  /* select 6 -- /HALT asserted   */
#define STUNRUN_ADSP_CTRL_NOOP_1A    0x1Au  /* select 2 -- no-op in MAME    */
#define STUNRUN_ADSP_CTRL_HALT_REL   0x1Eu  /* select 6 -- /HALT released   */
#define STUNRUN_ADSP_CTRL_IRQCLR     0x60u  /* separate handler, not this  */

/* The pass word all three liveness gates return in D7. */
#define STUNRUN_ADSP_PASS_WORD 0x5555u

/* ROM pointer table of 321 string records, indexed by the drawer's D0. */
#define STUNRUN_STRING_TABLE  0x00010136u
#define STUNRUN_STRING_COUNT  321u
#define STUNRUN_FONT_TABLE    0x00001FBCu

/* Where $b228 finds the ADSP program image: a word count, then 3 bytes per
 * 24-bit word. */
#define STUNRUN_ADSP_IMAGE_HDR 0x0000AE70u

/* ---- the contract ------------------------------------------------------- */

typedef struct stunrun_board {
    void *ctx;

    /* Reads. */
    uint8_t  (*read_rom8)(void *ctx, uint32_t addr);
    uint16_t (*read_rom16)(void *ctx, uint32_t addr);
    uint16_t (*read_port0)(void *ctx);
    uint16_t (*read_a80000)(void *ctx);
    uint16_t (*read_adsp_dm)(void *ctx, uint16_t offset);
    uint16_t (*read_adsp_irq)(void *ctx);
    uint16_t (*read_gsp_control)(void *ctx, uint16_t offset);
    uint16_t (*read_gsp_font)(void *ctx, uint16_t offset);

    /* Writes. */
    void (*write_rom16)(void *ctx, uint32_t addr, uint16_t value);
    void (*write_watchdog)(void *ctx);
    void (*write_adsp_dm)(void *ctx, uint16_t offset, uint16_t value);
    void (*write_adsp_ctrl)(void *ctx, uint16_t offset, uint16_t value);
    void (*write_adsp_prog)(void *ctx, uint32_t offset, uint32_t value);
    void (*write_gsp_host)(void *ctx, uint16_t offset, uint16_t value);
} stunrun_board_t;

/* Convenience wrappers, so the transcribed routines read like the ROM does.
 * Each is a one-line forward to the corresponding member. */

static inline uint16_t stunrun_b_rom16(const stunrun_board_t *b, uint32_t addr)
{
    return b->read_rom16(b->ctx, addr);
}

static inline uint16_t stunrun_b_dm(const stunrun_board_t *b, uint16_t off)
{
    return b->read_adsp_dm(b->ctx, off);
}

static inline void stunrun_b_dm_set(stunrun_board_t *b, uint16_t off, uint16_t v)
{
    b->write_adsp_dm(b->ctx, off, v);
}

static inline void stunrun_b_ctrl(stunrun_board_t *b, uint16_t off)
{
    /* clr.w -- the data is ignored by hd68k_adsp_control_w, which takes the
     * value from bit 3 of the offset, so a clear is written as the offset
     * itself with no data. */
    b->write_adsp_ctrl(b->ctx, off, 0);
}

static inline void stunrun_b_watchdog(stunrun_board_t *b)
{
    b->write_watchdog(b->ctx);
}

#ifdef __cplusplus
}
#endif

#endif /* STUNRUN_BOARD_IFACE_H */