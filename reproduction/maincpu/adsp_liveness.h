/* The five ADSP-2100 routines that gate the service-mode
 * "ADSP-2100 DOES NOT RESPOND" verdict, transcribed from the main-CPU ROM.
 *
 * WHAT WAS TRANSCRIBED, AND FROM WHERE
 *
 * Source: the STUN Runner main-CPU program ROM, the MC68010, as read off a
 * de-interleaved 1 MiB flat image (file offset == 68010 address) with the
 * repository's Musashi disassembler. Chip: Motorola MC68010 (the main CPU).
 * Five routines, each read whole, past its own `rts`, so that the terminating
 * `rts` and the first instruction of the following routine were both seen.
 * No routine body runs into its successor: each ends on `4e 75` and the next
 * entry begins at the next byte.
 *
 *   $B1E8-$B227  64 bytes  ADSP restart        -> stunrun_adsp_restart()
 *   $B228-$B249  34 bytes  ADSP program loader  -> stunrun_adsp_program_download()
 *   $B864-$B8B3  80 bytes  liveness gate 1      -> stunrun_adsp_gate_loopback()
 *   $B8B4-$B915  98 bytes  liveness gate 2      -> stunrun_adsp_gate_irq()
 *   $B916-$B9AF 154 bytes  liveness gate 3      -> stunrun_adsp_gate_scratch()
 *
 * The call sites that decide the message are at $5224/$5232/$5240, each
 * `jsr` followed by `cmpi.w #$5555,D7` and `bne` to $530A, which is the index
 * 97 string "ADSP-2100 DOES NOT RESPOND". So the only return value these
 * five routines produce that the game reads is D7's low word: $5555, or 0.
 * That is why every routine here returns uint16_t and why the tests assert
 * it rather than a bool.
 *
 * WHY A REGISTER FILE IS EXPOSED
 *
 * These routines are register-to-register code: the 24-bit packer in $B228
 * lives entirely in D0, the DM countdown in D3 is itself part of the
 * observable behaviour (it is how the timeout is implemented), and $B916
 * deliberately overwrites D3 with a DM readback. Modelling them with opaque
 * locals would hide the two things a reader most needs to check: the byte
 * order of the packer, and the fact that the countdown register is reused as
 * a scratch register. `stunrun_adsp_regs_t` therefore names D0-D7, A0, A1 and
 * SR directly, and every field the ROM writes is written.
 *
 * CONFIRMED VS INFERRED
 *
 * CONFIRMED, from the ROM bytes alone:
 *  - every board address, its order, and the data written;
 *  - the pass word $5555 and its single write site per gate;
 *  - the countdown immediates $1770 (6000) and $7530 (30000);
 *  - the loop shape `subq.l #1,D3 / beq <fail>` at the TOP of the loop, so the
 *    body executes D3-1 times, not D3 (5999 and 29999 respectively);
 *  - the 24-bit packer's byte order, which follows arithmetically from the
 *    instruction sequence, is NOT ambiguous, and is independently confirmed
 *    by MAME's own write mask and decoder (see THE 24-BIT PACKER below);
 *  - that the loaded image is 295 24-bit words, so the image ends at $B1E6
 *    and A0 lands on the single pad byte at $B1E7 -- one byte before
 *    `stunrun_adsp_restart` at $B1E8. The ROM is self-consistent about its
 *    own downloader's bounds.
 *
 * INFERRED, and recorded here because the ROM does not say it:
 *  - WHAT the DSP is asked to do. Nothing in these five routines proves that
 *    the running ADSP program copies DM[6] to DM[$0A] or raises an IRQ in
 *    response to DM[2]=6; that is the natural reading of the gate's design
 *    and is consistent with the gates' structure, but it lives in the ADSP
 *    program image, not here. This port models the 68010's side only.
 *  - The TIMING of the countdown. It is a bare loop count, not a wall-clock
 *    timeout; there is no frame or vblank reference in the loop, only the
 *    watchdog kick. Calling the two constants "timeouts" describes intent.
 *  - Whether the six-word readback in $B916 is compared against anything. In
 *    the ROM it is NOT: the six words land in D0-D5 and D7 is then set to
 *    $5555 unconditionally. The comparison, if any, is the caller's -- and
 *    $524E immediately reads D4 for the checksum byte test. That is why this
 *    routine's pass path is unconditional, and why the port reproduces that
 *    rather than "fixing" it into a comparison.
 *
 * THE 24-BIT PACKER, DERIVED
 *
 * $B236-$B242, with the byte order shown as it falls out. Writing each
 * intermediate value literally, with b0/b1/b2 the three bytes read in ROM
 * order:
 *
 *     clr.l   D0            D0 = 0x00000000
 *     move.b  (A0)+, D0     D0 = 0x000000b0     b0 -> bits 7:0
 *     swap    D0            D0 = 0x00b00000     b0 -> bits 23:16, and the
 *                                             zero from bits 31:24 lands in
 *                                             bits 31:24
 *     move.b  (A0)+, D0     D0 = 0x00b000b1     b1 -> bits 7:0
 *     lsl.w   #8, D0        D0 = 0x00b0b100     b1 -> bits 15:8. A WORD
 *                                             shift, so b0 at bits 23:16
 *                                             and the zero at bits 31:24
 *                                             stay exactly where they are
 *     move.b  (A0)+, D0     D0 = 0x00b0b1b2     b2 -> bits 7:0
 *     move.l  D0, (A1)+     stores 0x00 b0 b1 b2
 *
 * So the ROM byte order is big-endian, the 24-bit payload occupies bits 23:0,
 * and bits 31:24 are ALWAYS ZERO. That is not a property of the data; it is
 * forced by the `swap` followed by a word-width `lsl.w`, and the test asserts
 * the gap byte at bits 31:24 explicitly rather than only checking totals.
 *
 * THE ROM IS CONFIRMED AGAINST ITS OWN EMULATOR, which is what makes the
 * word-width of that `lsl.w` load-bearing rather than cosmetic. MAME's
 * adsp21xx_device::program_write (adsp2100.cpp:842) stores
 * `data & 0xffffff`, and opcode_read (:849) fetches that dword back, and the
 * decoder switches on `BIT(op, 16, 8)` (:1224). So bits 23:16 must be the
 * word's most significant byte. Read the real image at $AE72 both ways:
 *
 *   bits 23:0 : 000000 000000 000000 000000 340008 340009 34000A 34000B
 *               380008 380009 38000A 38000B 800000 400024 22E00F 1800C1
 *               -- every word an ADSP-2100 opcode class, and the run
 *                  340008/340009/34000A is a coherent incrementing DAG1 write
 *
 *   bits 31:8 : 003400 003400 003400 003400 340034 340035 340036 340037
 *               -- BIT(op,16,8) is 0x00 for all of them, i.e. every one of
 *                  the 295 instructions decodes as NOP and the DSP does
 *                  nothing at all
 *
 * The first reading is the only one under which the program works, and it is
 * the one MAME's own write mask requires. Both readings are asserted in
 * adsp_liveness_test.c so a future re-derivation that lands on the other one
 * fails instead of quietly shipping.
 *
 * ADDRESS CONVENTIONS FOR write_adsp_prog
 *
 * board_iface.h documents the DM and control accesses as window offsets but
 * says nothing about the program window, which no caller had yet used. This
 * port passes A1's full 32-bit 68010 address (0x800000, 0x800002, ...) as
 * `offset`, following the header's top-level rule that ROM-side addresses are
 * 68010 addresses. This is a documented choice, not a measurement: no prior
 * caller constrained it.
 *
 * NO ROM CONTENTS ARE EMBEDDED OR REQUIRED, but $B228 reads its image through
 * read_rom16/read_rom8, so a caller must supply one. The slice test supplies a
 * synthetic image; the real ROM's word count is recorded below as a constant
 * so a caller can size a real one.
 */
#ifndef STUNRUN_ADSP_LIVENESS_H
#define STUNRUN_ADSP_LIVENESS_H

#include <stddef.h>
#include <stdint.h>

#include "board_iface.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- ROM addresses, as transcribed ---------------------------------------- */

#define STUNRUN_ADSP_RESTART_ADDR        0x0000B1E8u  /* 64 bytes, rts $B226 */
#define STUNRUN_ADSP_RESTART_END         0x0000B228u
#define STUNRUN_ADSP_DOWNLOAD_ADDR       0x0000B228u  /* 34 bytes, rts $B248 */
#define STUNRUN_ADSP_DOWNLOAD_END        0x0000B24Au
#define STUNRUN_ADSP_GATE_LOOPBACK_ADDR  0x0000B864u  /* 80 bytes, rts $B8B2 */
#define STUNRUN_ADSP_GATE_LOOPBACK_END   0x0000B8B4u
#define STUNRUN_ADSP_GATE_IRQ_ADDR       0x0000B8B4u  /* 98 bytes, rts $B914 */
#define STUNRUN_ADSP_GATE_IRQ_END        0x0000B916u
#define STUNRUN_ADSP_GATE_SCRATCH_ADDR   0x0000B916u  /* 154 bytes, both
                                                       * paths end on `rts`,
                                                       * $B9AA pass / $B9AE
                                                       * fail */
#define STUNRUN_ADSP_GATE_SCRATCH_END    0x0000B9B0u

/* ---- immediates the ROM loads -------------------------------------------- */

/* $B884: move.w #$1234,D3 -- the loopback probe value. */
#define STUNRUN_ADSP_LOOPBACK_WORD 0x1234u

/* $B8E6: move.l #$1770,D3 -- 6000. */
#define STUNRUN_ADSP_GATE_IRQ_LIMIT 0x00001770u

/* $B960: move.l #$7530,D3 -- 30000. */
#define STUNRUN_ADSP_GATE_SCRATCH_LIMIT 0x00007530u

/* Poll-loop body iterations, which is the limit MINUS ONE and not the limit.
 * The loop is laid out as
 *
 *     $B8EC: subq.l #1,D3        <- decrement at the TOP
 *     $B8EE: beq    <fail>       <- zero means the wait expired
 *     $B8F2: clr.w  $608000.l    <- body: watchdog, then poll
 *     $B8F8: btst   #$0,$838001.l
 *     $B900: bne    $B8EC
 *
 * so the body runs for D3 = 5999 .. 1, i.e. 5999 times, and the 6000th
 * decrement reaches zero and branches out WITHOUT running the body. A stub
 * that never raises the ADSP IRQ therefore produces exactly
 * STUNRUN_ADSP_GATE_IRQ_POLLS watchdog kicks and the same number of IRQ
 * status reads. Getting this wrong by one is invisible to any test that only
 * checks the return value, which is why the slice test counts them. */
#define STUNRUN_ADSP_GATE_IRQ_POLLS     5999u
#define STUNRUN_ADSP_GATE_SCRATCH_POLLS 29999u

/* $B8B6: move #$2f00,SR -- supervisor, every interrupt masked. */
#define STUNRUN_ADSP_SR_IRQ_MASKED 0x2F00u

/* $B8F8/$B972: btst #$0,$838001.l -- bit 0 of the ADSP interrupt status. */
#define STUNRUN_ADSP_IRQ_STATUS_BIT 0x0001u

/* The word count the real ROM carries at $B228's image header ($AE70): 295.
 * 295 * 3 bytes of image starting at $AE72 ends at $B1E6, and A0 is left
 * pointing at $B1E7 -- a single 0x00 pad byte -- immediately before
 * stunrun_adsp_restart() at $B1E8. Recorded from the ROM, and used by the
 * slice test as a bounds check that needs no ROM file. The pad byte is a
 * fact of this build, not something the downloader acts on: nothing reads
 * it, and the count of 295 already accounts for every image word. */
#define STUNRUN_ADSP_IMAGE_WORDS 295u
#define STUNRUN_ADSP_IMAGE_LAST  0x0000B1E6u  /* final image byte */
#define STUNRUN_ADSP_IMAGE_END   0x0000B1E7u  /* where A0 is left */

/* ---- control-register offsets this code drives --------------------------- */

/* board_iface.h names three of these. Its names are kept where they are
 * usable, but the MAME decode at harddriv_m.cpp:751
 *
 *     int val = (offset >> 3) & 1;   offset &= 7;   switch (offset) ...
 *
 * puts them on different selects than the names suggest, and the difference
 * is behavioural, so the numbers are spelled out here with their real decode:
 *
 *   $81800a  select 2, val 1   default case -- no effect ("ADSP control 02")
 *   $81800c  select 4, val 1   default case -- no effect ("ADSP control 04")
 *   $81800e  select 6, val 1   case 6: /HALT  (m_adsp_halt = !val, so a
 *                              val of 1 RELEASES the halt and spins the host)
 *   $81801a  select 2, val 1   same as $81800a
 *   $81801c  select 4, val 1   same as $81800c
 *   $81801e  select 6, val 1   same as $81800e
 *   $818060  (different MAME handler entirely: hd68k_adsp_irq_clear_w at
 *             harddriv_m.cpp:817, window 0x818060-0x81807f) clears the ADSP
 *             interrupt. board_iface.h folds this offset into write_adsp_ctrl;
 *             so does this port, because the offset is preserved either way
 *             and a caller can tell the two windows apart by the offset.
 *
 * Consequence worth stating plainly: of everything these five routines write
 * to the ADSP control window, only $81800E/$81801E (release /HALT) and
 * $818060 (IRQ clear) do anything the emulator models. The rest are log
 * lines. */
#define STUNRUN_ADSP_CTRL_SEL_BANK 0x000Cu  /* $81800c / $81801c, select 4 --
                                              * MAME default, no effect */
/* $81800a/$81801a, $81800e/$81801e and $818060 are named once, in
 * board_iface.h, as STUNRUN_ADSP_CTRL_NOOP_0A, STUNRUN_ADSP_CTRL_HALT,
 * STUNRUN_ADSP_CTRL_NOOP_1A and STUNRUN_ADSP_CTRL_IRQCLR. That header owns
 * the control-window constants; do not redefine them here. */

/* ---- the register file ----------------------------------------------------- */

/* Only D0-D7, A0, A1 and SR are read or written by these five routines; A2-A7
 * and the stack are never touched except by $B8B4's single `move SR,-(A7)` /
 * `move (A7)+,SR` pair, which is modelled by reading and writing `sr` rather
 * than by emulating a stack. Fields are named as the ROM names them so the C
 * reads like the listing. */
typedef struct stunrun_adsp_regs {
    uint32_t d0, d1, d2, d3, d4, d5, d6, d7;
    uint32_t a0, a1;
    uint32_t sr;      /* saved and restored by $B8B4 only */
} stunrun_adsp_regs_t;

/* $B228: lea $AE70.l,A0 / move.w (A0)+,D6 / lea $800000.l,A1, then for each of
 * D6 words read three ROM bytes and store one packed 24-bit long at (A1)+,
 * A1 walking the 32 KiB program window two bytes per word.
 *
 * Loop shape is a `do { } while`, because the ROM's `bne $B236` sits AFTER
 * the body: a header word count of zero does not skip the loop, it wraps D6's
 * low half from 0 to $FFFF and runs 65536 times. That is reproduced rather
 * than "fixed", and is why this is not a `for`. */
void stunrun_adsp_program_download(stunrun_board_t *board,
                                   stunrun_adsp_regs_t *regs);

/* $B1E8: clear four control registers, call $B228, write zero to DM[2] and
 * DM[0], clear three more control registers. Clobbers nothing of its own --
 * every register it changes belongs to $B228. */
void stunrun_adsp_restart(stunrun_board_t *board, stunrun_adsp_regs_t *regs);

/* $B864: restart, point the DM address/data pair at 0, clear DM[6], write
 * $1234 to it, release the ADSP, then read DM[$0A] and set D7 to $5555 if and
 * only if it equals $1234. */
uint16_t stunrun_adsp_gate_loopback(stunrun_board_t *board,
                                    stunrun_adsp_regs_t *regs);

/* $B8B4: mask interrupts, clear the ADSP IRQ, restart, point the DM pair at
 * 6, clear DM[$0C], then poll the ADSP IRQ status for up to
 * STUNRUN_ADSP_GATE_IRQ_POLLS body iterations. D7 is $5555 when the DSP
 * answered and 0 when the countdown expired. The routine restores SR and
 * clears the ADSP IRQ again before returning on both paths. */
uint16_t stunrun_adsp_gate_irq(stunrun_board_t *board,
                               stunrun_adsp_regs_t *regs);

/* $B916: clear the ADSP IRQ, restart, point the DM pair at $0A, clear the six
 * words DM[$0C]..DM[$16], then poll the ADSP IRQ status for up to
 * STUNRUN_ADSP_GATE_SCRATCH_POLLS body iterations. On expiry D7 is 0. On
 * success it reads the six words into D0-D5 -- overwriting the countdown in
 * D3 -- and sets D7 to $5555 WITHOUT comparing them, which is what the ROM
 * does; the caller at $524E is what looks at D4. */
uint16_t stunrun_adsp_gate_scratch(stunrun_board_t *board,
                                   stunrun_adsp_regs_t *regs);

#ifdef __cplusplus
}
#endif

#endif /* STUNRUN_ADSP_LIVENESS_H */