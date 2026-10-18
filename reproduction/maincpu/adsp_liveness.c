/* The five ADSP-2100 liveness routines. See adsp_liveness.h for provenance,
 * the confirmed/inferred split, and the address conventions.
 *
 * Each function below carries the verbatim 68010 listing it was transcribed
 * from, with the address of every instruction, so the C and the ROM can be
 * diffed by eye. Instruction widths follow the ROM exactly: every `move.w`
 * is modelled as a masked write of the low half, and every `move.l` as a full
 * 32-bit write, because that distinction is load-bearing here -- $B916's D3
 * is a long countdown and $B864's D3 is a word probe.
 */
#include <stddef.h>

#include "adsp_liveness.h"

/* `move.w SRC,Dn` / `clr.w Dn`: keep the high half, replace the low half. */
#define STUNRUN_WORD_MOVE(dst, value) \
    ((dst) = ((dst) & 0xFFFF0000u) | (uint32_t)(uint16_t)(value))

/* `move.b SRC,Dn`: keep bits 31:8, replace bits 7:0. */
#define STUNRUN_BYTE_MOVE(dst, value) \
    ((dst) = ((dst) & 0xFFFFFF00u) | (uint32_t)(uint8_t)(value))

/* `subq.w #1,Dn`: word arithmetic, so the borrow never leaves the low half. */
#define STUNRUN_SUBQ_W(reg) \
    ((reg) = ((reg) & 0xFFFF0000u) | (((reg) - 1u) & 0xFFFFu))

/* `swap Dn`: exchange the two halves. */
#define STUNRUN_SWAP(reg) ((reg) = ((reg) >> 16) | ((reg) << 16))

/* `lsl.w #8,Dn`: a WORD shift, so bits 31:16 do not move. That is what leaves
 * byte 0 at bits 23:16 and a permanent zero at bits 31:24 -- see the
 * derivation in the header. */
#define STUNRUN_LSL_W8(reg) \
    ((reg) = ((reg) & 0xFFFF0000u) | (((reg) << 8) & 0xFFFFu))

/* ------------------------------------------------------------------ $B228 */

void stunrun_adsp_program_download(stunrun_board_t *board,
                                   stunrun_adsp_regs_t *regs)
{
    if (board == NULL || regs == NULL)
        return;

    /* 0000b228: 41 f9 00 00 ae 70  lea     $ae70.l, A0 */
    regs->a0 = STUNRUN_ADSP_IMAGE_HDR;

    /* 0000b22e: 3c 18              move.w  (A0)+, D6 */
    STUNRUN_WORD_MOVE(regs->d6, board->read_rom16(board->ctx, regs->a0));
    regs->a0 += 2u;

    /* 0000b230: 43 f9 00 80 00 00  lea     $800000.l, A1 */
    regs->a1 = STUNRUN_ADSP_PROG_BASE;

    /* 0000b236: 42 80              clr.l   D0          <- loop top */
    /* 0000b238: 10 18              move.b  (A0)+, D0
     * 0000b23a: 48 40              swap    D0
     * 0000b23c: 10 18              move.b  (A0)+, D0
     * 0000b23e: e1 48              lsl.w   #8, D0
     * 0000b240: 10 18              move.b  (A0)+, D0
     * 0000b242: 22 c0              move.l  D0, (A1)+
     * 0000b244: 53 46              subq.w  #1, D6
     * 0000b246: 66 ee              bne     $b236
     *
     * `bne` tests the loop from the BOTTOM, so this is a do-while: a header
     * count of 0 wraps D6's low half to $FFFF and runs 65536 times.
     */
    do {
        /* $B236: clr.l D0 */
        regs->d0 = 0u;

        /* $B238: move.b (A0)+,D0 -- byte 0 into bits 7:0 */
        STUNRUN_BYTE_MOVE(regs->d0, board->read_rom8(board->ctx, regs->a0));
        regs->a0 += 1u;

        /* $B23A: swap D0 -- byte 0 moves into bits 23:16, and the zero that
         * was in bits 31:24 moves down into bits 31:24. */
        STUNRUN_SWAP(regs->d0);

        /* $B23C: move.b (A0)+,D0 -- byte 1 into bits 7:0 */
        STUNRUN_BYTE_MOVE(regs->d0, board->read_rom8(board->ctx, regs->a0));
        regs->a0 += 1u;

        /* $B23E: lsl.w #8,D0 -- byte 1 into bits 15:8. A word shift, so
         * byte 0 (bits 23:16) and the zero at bits 31:24 stay put. */
        STUNRUN_LSL_W8(regs->d0);

        /* $B240: move.b (A0)+,D0 -- byte 2 into bits 7:0 */
        STUNRUN_BYTE_MOVE(regs->d0, board->read_rom8(board->ctx, regs->a0));
        regs->a0 += 1u;

        /* $B242: move.l D0,(A1)+ -- ONE 32-bit store, so A1 advances by 2.
         * The 24-bit payload is in bits 23:0; bits 31:24 are always zero
         * because of the `swap`/`lsl.w` pair above. MAME's
         * adsp21xx_device::program_write (adsp2100.cpp:842) masks the store
         * with 0xffffff, so those zero bits are dropped on the way into the
         * DSP -- which is why the ROM is allowed to leave them set.
         * Consecutive stores therefore overlap in the byte-addressed window:
         * word i occupies A1(0..3) and word i+1 rewrites bytes 2..5. MAME
         * installs this window `write16s` (harddriv.cpp:4827), which splits
         * the longword into two masked word writes at offset and offset+2 --
         * so the second word's high half is what survives. That is the ROM's
         * behaviour, not a defect here. */
        board->write_adsp_prog(board->ctx, regs->a1, regs->d0);
        regs->a1 += 2u;

        /* $B244: subq.w #1,D6 -- word op */
        STUNRUN_SUBQ_W(regs->d6);

        /* $B246: bne $b236 -- loop while D6's low half is nonzero */
    } while ((regs->d6 & 0xFFFFu) != 0u);

    /* 0000b248: 4e 75              rts */
}

/* ------------------------------------------------------------------ $B1E8 */

void stunrun_adsp_restart(stunrun_board_t *board, stunrun_adsp_regs_t *regs)
{
    if (board == NULL || regs == NULL)
        return;

    /* 0000b1e8: 42 79 00 81 80 0a  clr.w   $81800a.l    (select 2, no effect) */
    stunrun_b_ctrl(board, STUNRUN_ADSP_CTRL_NOOP_0A);
    /* 0000b1ee: 42 79 00 81 80 0c  clr.w   $81800c.l    (select 4, no effect) */
    stunrun_b_ctrl(board, STUNRUN_ADSP_CTRL_SEL_BANK);
    /* 0000b1f4: 42 79 00 81 80 0e  clr.w   $81800e.l    (select 6: /HALT) */
    stunrun_b_ctrl(board, STUNRUN_ADSP_CTRL_HALT);
    /* 0000b1fa: 42 79 00 81 80 60  clr.w   $818060.l    (ADSP IRQ clear) */
    stunrun_b_ctrl(board, STUNRUN_ADSP_CTRL_IRQCLR);

    /* 0000b200: 61 00 00 26        bsr     $b228 */
    stunrun_adsp_program_download(board, regs);

    /* 0000b204: 33 fc 00 00 00 80 80 02  move.w  #$0, $808002.l */
    stunrun_b_dm_set(board, STUNRUN_DM_DATA_REG, 0x0000u);
    /* 0000b20c: 33 fc 00 00 00 80 80 00  move.w  #$0, $808000.l */
    stunrun_b_dm_set(board, STUNRUN_DM_ADDR_REG, 0x0000u);

    /* 0000b214: 42 79 00 81 80 1a  clr.w   $81801a.l    (select 2, no effect) */
    stunrun_b_ctrl(board, STUNRUN_ADSP_CTRL_NOOP_0A);
    /* 0000b21a: 42 79 00 81 80 1c  clr.w   $81801c.l    (select 4, no effect) */
    stunrun_b_ctrl(board, STUNRUN_ADSP_CTRL_SEL_BANK);
    /* 0000b220: 42 79 00 81 80 1e  clr.w   $81801e.l    (select 6: /HALT) */
    stunrun_b_ctrl(board, STUNRUN_ADSP_CTRL_HALT);

    /* 0000b226: 4e 75              rts */
    /* No register of its own is written: every register this routine leaves
     * behind belongs to $B228 (D0, D6, A0, A1). */
}

/* ------------------------------------------------------------------ $B864 */

uint16_t stunrun_adsp_gate_loopback(stunrun_board_t *board,
                                    stunrun_adsp_regs_t *regs)
{
    if (board == NULL || regs == NULL)
        return 0u;

    /* 0000b864: 61 00 f9 82        bsr     $b1e8 */
    stunrun_adsp_restart(board, regs);

    /* 0000b868: 42 79 00 81 80 0a  clr.w   $81800a.l */
    stunrun_b_ctrl(board, STUNRUN_ADSP_CTRL_NOOP_0A);
    /* 0000b86e: 33 fc 00 00 00 80 80 02  move.w  #$0, $808002.l */
    stunrun_b_dm_set(board, STUNRUN_DM_DATA_REG, 0x0000u);
    /* 0000b876: 33 fc 00 02 00 80 80 00  move.w  #$2, $808000.l */
    stunrun_b_dm_set(board, STUNRUN_DM_ADDR_REG, 0x0002u);
    /* 0000b87e: 42 79 00 80 80 06  clr.w   $808006.l */
    stunrun_b_dm_set(board, STUNRUN_DM_TEST_WORD, 0x0000u);

    /* 0000b884: 36 3c 12 34        move.w  #$1234, D3   <- word move, so D3's
     * high half survives. Only the low half is ever compared or stored. */
    STUNRUN_WORD_MOVE(regs->d3, STUNRUN_ADSP_LOOPBACK_WORD);

    /* 0000b888: 33 c3 00 80 80 06  move.w  D3, $808006.l */
    stunrun_b_dm_set(board, STUNRUN_DM_TEST_WORD, (uint16_t)regs->d3);

    /* 0000b88e: 42 79 00 81 80 1a  clr.w   $81801a.l */
    stunrun_b_ctrl(board, STUNRUN_ADSP_CTRL_NOOP_0A);

    /* 0000b894: 4e 71              nop
     * 0000b896: 4e 71              nop
     * 0000b898: 4e 71              nop
     * Three NOPs, not four. Findings section 117.2 shows "nop x4"; the ROM
     * bytes at $B894-$B89B are three 4e 71. Recorded here because the count
     * is the kind of detail that gets copied forward. */

    /* 0000b89a: 42 79 00 81 80 0a  clr.w   $81800a.l */
    stunrun_b_ctrl(board, STUNRUN_ADSP_CTRL_NOOP_0A);

    /* 0000b8a0: 30 39 00 80 80 0a  move.w  $80800a.l, D0 */
    STUNRUN_WORD_MOVE(regs->d0,
                      board->read_adsp_dm(board->ctx, STUNRUN_DM_READBACK));

    /* 0000b8a6: 42 47              clr.w   D7 */
    STUNRUN_WORD_MOVE(regs->d7, 0x0000u);

    /* 0000b8a8: b6 40              cmp.w   D0, D3     (D3 - D0) */
    /* 0000b8aa: 66 00 00 06        bne     $b8b2 */
    if ((uint16_t)regs->d3 == (uint16_t)regs->d0) {
        /* 0000b8ae: 3e 3c 55 55      move.w  #$5555, D7 */
        STUNRUN_WORD_MOVE(regs->d7, STUNRUN_ADSP_PASS_WORD);
    }

    /* 0000b8b2: 4e 75              rts */
    return (uint16_t)regs->d7;
}

/* ------------------------------------------------------------------ $B8B4 */

uint16_t stunrun_adsp_gate_irq(stunrun_board_t *board,
                               stunrun_adsp_regs_t *regs)
{
    uint32_t sr_saved;
    int expired = 0;

    if (board == NULL || regs == NULL)
        return 0u;

    /* 0000b8b4: 40 e7              move    SR, -(A7)
     * The pushed SR is modelled by holding it in a local; A7 is never read. */
    sr_saved = regs->sr;

    /* 0000b8b6: 46 fc 2f 00        move    #$2f00, SR
     * Supervisor, all seven interrupt levels masked. That mask is what lets
     * the poll below observe the ADSP interrupt at all: the ADSP sits on
     * IPL level 2 (harddriv_m.cpp:96), so with it unmasked the autovectored
     * handler would fire, the handler would clear the interrupt, and the
     * poll would never see it. */
    regs->sr = STUNRUN_ADSP_SR_IRQ_MASKED;

    /* 0000b8ba: 42 79 00 81 80 60  clr.w   $818060.l */
    stunrun_b_ctrl(board, STUNRUN_ADSP_CTRL_IRQCLR);

    /* 0000b8c0: 61 00 f9 26        bsr     $b1e8 */
    stunrun_adsp_restart(board, regs);

    /* 0000b8c4: 42 79 00 81 80 0a  clr.w   $81800a.l */
    stunrun_b_ctrl(board, STUNRUN_ADSP_CTRL_NOOP_0A);
    /* 0000b8ca: 33 fc 00 06 00 80 80 02  move.w  #$6, $808002.l */
    stunrun_b_dm_set(board, STUNRUN_DM_DATA_REG, 0x0006u);
    /* 0000b8d2: 33 fc 00 02 00 80 80 00  move.w  #$2, $808000.l */
    stunrun_b_dm_set(board, STUNRUN_DM_ADDR_REG, 0x0002u);
    /* 0000b8da: 42 79 00 80 80 0c  clr.w   $80800c.l */
    stunrun_b_dm_set(board, STUNRUN_DM_SCRATCH_0, 0x0000u);
    /* 0000b8e0: 42 79 00 81 80 1a  clr.w   $81801a.l */
    stunrun_b_ctrl(board, STUNRUN_ADSP_CTRL_NOOP_0A);

    /* 0000b8e6: 26 3c 00 00 17 70  move.l  #$1770, D3 */
    regs->d3 = STUNRUN_ADSP_GATE_IRQ_LIMIT;

    /* 0000b8ec: 53 83              subq.l  #1, D3       <- loop top */
    /* 0000b8ee: 67 00 00 1a        beq     $b90a */
    /* 0000b8f2: 42 79 00 60 80 00  clr.w   $608000.l */
    /* 0000b8f8: 08 39 00 00 00 83 80 01  btst    #$0, $838001.l */
    /* 0000b900: 66 ea              bne     $b8ec */
    /* 0000b902: 3e 3c 55 55        move.w  #$5555, D7 */
    for (;;) {
        regs->d3 -= 1u;
        if (regs->d3 == 0u) {
            expired = 1;
            break;               /* $B8EE taken -> $B90A */
        }
        /* $B8F2: the watchdog is what the busy-wait keeps alive */
        stunrun_b_watchdog(board);
        /* $B8F8/$B900: bit 0 set means the ADSP is still asserting its
         * interrupt, so keep waiting. */
        if ((board->read_adsp_irq(board->ctx) & STUNRUN_ADSP_IRQ_STATUS_BIT)
            != 0u)
            continue;
        break;                   /* fell through to $B902 */
    }

    if (expired) {
        /* 0000b90a: 42 47           clr.w   D7 */
        STUNRUN_WORD_MOVE(regs->d7, 0x0000u);
    } else {
        /* $B902/$B906: move.w #$5555,D7 / bra $b90c */
        STUNRUN_WORD_MOVE(regs->d7, STUNRUN_ADSP_PASS_WORD);
    }

    /* 0000b90c: 42 79 00 81 80 60  clr.w   $818060.l
     * The ADSP interrupt is acknowledged BEFORE SR is restored, so the
     * interrupt that fires the instant the mask comes off has already been
     * cleared and cannot re-enter this routine. */
    stunrun_b_ctrl(board, STUNRUN_ADSP_CTRL_IRQCLR);

    /* 0000b912: 46 df              move    (A7)+, SR */
    regs->sr = sr_saved;

    /* 0000b914: 4e 75              rts */
    return (uint16_t)regs->d7;
}

/* ------------------------------------------------------------------ $B916 */

uint16_t stunrun_adsp_gate_scratch(stunrun_board_t *board,
                                   stunrun_adsp_regs_t *regs)
{
    int expired = 0;

    if (board == NULL || regs == NULL)
        return 0u;

    /* Unlike $B8B4 this routine does NOT save, set, or restore SR. */
    /* 0000b916: 42 79 00 81 80 60  clr.w   $818060.l */
    stunrun_b_ctrl(board, STUNRUN_ADSP_CTRL_IRQCLR);

    /* 0000b91c: 61 00 f8 ca        bsr     $b1e8 */
    stunrun_adsp_restart(board, regs);

    /* 0000b920: 42 79 00 81 80 0a  clr.w   $81800a.l */
    stunrun_b_ctrl(board, STUNRUN_ADSP_CTRL_NOOP_0A);
    /* 0000b926: 33 fc 00 0a 00 80 80 02  move.w  #$a, $808002.l */
    stunrun_b_dm_set(board, STUNRUN_DM_DATA_REG, STUNRUN_DM_READBACK);
    /* 0000b92e: 33 fc 00 02 00 80 80 00  move.w  #$2, $808000.l */
    stunrun_b_dm_set(board, STUNRUN_DM_ADDR_REG, 0x0002u);

    /* 0000b936: 42 79 00 80 80 0c  clr.w   $80800c.l */
    stunrun_b_dm_set(board, STUNRUN_DM_SCRATCH_0 + 0u, 0x0000u);
    /* 0000b93c: 42 79 00 80 80 0e  clr.w   $80800e.l */
    stunrun_b_dm_set(board, STUNRUN_DM_SCRATCH_0 + 2u, 0x0000u);
    /* 0000b942: 42 79 00 80 80 10  clr.w   $808010.l */
    stunrun_b_dm_set(board, STUNRUN_DM_SCRATCH_0 + 4u, 0x0000u);
    /* 0000b948: 42 79 00 80 80 12  clr.w   $808012.l */
    stunrun_b_dm_set(board, STUNRUN_DM_SCRATCH_0 + 6u, 0x0000u);
    /* 0000b94e: 42 79 00 80 80 14  clr.w   $808014.l */
    stunrun_b_dm_set(board, STUNRUN_DM_SCRATCH_0 + 8u, 0x0000u);
    /* 0000b954: 42 79 00 80 80 16  clr.w   $808016.l */
    stunrun_b_dm_set(board, STUNRUN_DM_SCRATCH_0 + 10u, 0x0000u);

    /* 0000b95a: 42 79 00 81 80 1a  clr.w   $81801a.l */
    stunrun_b_ctrl(board, STUNRUN_ADSP_CTRL_NOOP_0A);

    /* 0000b960: 26 3c 00 00 75 30  move.l  #$7530, D3 */
    regs->d3 = STUNRUN_ADSP_GATE_SCRATCH_LIMIT;

    /* 0000b966: 53 83              subq.l  #1, D3       <- loop top */
    /* 0000b968: 67 00 00 42        beq     $b9ac */
    /* 0000b96c: 42 79 00 60 80 00  clr.w   $608000.l */
    /* 0000b972: 08 39 00 00 00 83 80 01  btst    #$0, $838001.l */
    /* 0000b97a: 66 ea              bne     $b966 */
    for (;;) {
        regs->d3 -= 1u;
        if (regs->d3 == 0u) {
            expired = 1;
            break;               /* $B968 taken -> $B9AC */
        }
        stunrun_b_watchdog(board);
        if ((board->read_adsp_irq(board->ctx) & STUNRUN_ADSP_IRQ_STATUS_BIT)
            != 0u)
            continue;
        break;
    }

    if (expired) {
        /* 0000b9ac: 42 47           clr.w   D7 */
        /* 0000b9ae: 4e 75           rts */
        STUNRUN_WORD_MOVE(regs->d7, 0x0000u);
        return (uint16_t)regs->d7;
    }

    /* 0000b97c: 42 79 00 81 80 0a  clr.w   $81800a.l */
    stunrun_b_ctrl(board, STUNRUN_ADSP_CTRL_NOOP_0A);
    /* 0000b982: 30 39 00 80 80 0c  move.w  $80800c.l, D0 */
    STUNRUN_WORD_MOVE(regs->d0,
                      board->read_adsp_dm(board->ctx, STUNRUN_DM_SCRATCH_0));
    /* 0000b988: 32 39 00 80 80 0e  move.w  $80800e.l, D1 */
    STUNRUN_WORD_MOVE(regs->d1,
                      board->read_adsp_dm(board->ctx, STUNRUN_DM_SCRATCH_0 + 2u));
    /* 0000b98e: 34 39 00 80 80 10  move.w  $808010.l, D2 */
    STUNRUN_WORD_MOVE(regs->d2,
                      board->read_adsp_dm(board->ctx, STUNRUN_DM_SCRATCH_0 + 4u));
    /* 0000b994: 36 39 00 80 80 12  move.w  $808012.l, D3
     * This DESTROYS the countdown. The ROM does it anyway, and so does this
     * port: after the loop the countdown is spent, and $B916's caller reads
     * D3 as one of six scratch words. */
    STUNRUN_WORD_MOVE(regs->d3,
                      board->read_adsp_dm(board->ctx, STUNRUN_DM_SCRATCH_0 + 6u));
    /* 0000b99a: 38 39 00 80 80 14  move.w  $808014.l, D4 */
    STUNRUN_WORD_MOVE(regs->d4,
                      board->read_adsp_dm(board->ctx, STUNRUN_DM_SCRATCH_0 + 8u));
    /* 0000b9a0: 3a 39 00 80 80 16  move.w  $808016.l, D5 */
    STUNRUN_WORD_MOVE(regs->d5,
                      board->read_adsp_dm(board->ctx, STUNRUN_DM_SCRATCH_0 + 10u));

    /* 0000b9a6: 3e 3c 55 55        move.w  #$5555, D7
     * Unconditional. The ROM compares NOTHING here. The caller at $524E does
     * `cmpi.b #$17,D4`; that is the only use of these six words anywhere in
     * the service test, and it is outside this routine. */
    STUNRUN_WORD_MOVE(regs->d7, STUNRUN_ADSP_PASS_WORD);

    /* 0000b9aa: 4e 75              rts */
    return (uint16_t)regs->d7;
}