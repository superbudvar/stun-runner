/* See text_draw.h for the ROM address ranges, the descriptor format, and the
 * confirmed/inferred split. Every line below carries the address of the
 * instruction it stands for. */
#include <stddef.h>

#include "text_draw.h"

/* The 68010 is big-endian on a 16-bit bus: a longword read fetches the most
 * significant halfword first. Used for `movea.l (A0,D0.l),A0` at $2604. */
static uint32_t text_rom32(stunrun_board_t *b, uint32_t addr)
{
    uint32_t hi = (uint32_t)b->read_rom16(b->ctx, addr);
    uint32_t lo = (uint32_t)b->read_rom16(b->ctx, addr + 2u);
    return (hi << 16) | lo;
}

/* $2656 `move.l D0,$c00002.l`. HSTADR is a 32-bit device register whose high
 * halfword is at the LOWER host offset ($C00002 = HSTADRH, $C00004 =
 * HSTADRL), and the store delivers the high halfword first. That order is what
 * the recorded captures show: HSTADR = (tap@$C00002 << 16) | tap@$C00004. */
static void text_latch_hstadr(stunrun_board_t *b, uint32_t value)
{
    b->write_gsp_host(b->ctx, STUNRUN_TEXT_HSTADR_H,
                      (uint16_t)((value >> 16) & 0xFFFFu));
    b->write_gsp_host(b->ctx, STUNRUN_TEXT_HSTADR_L,
                      (uint16_t)(value & 0xFFFFu));
}

/* $267A `addi.l #$10,$c00002.l`. This is a read-modify-write of the DEVICE's
 * HSTADR, not of D0 -- which is why the eight increments of a row are
 * discarded by the next row's `move.l D0,$c00002.l`. Read order is
 * HSTADRH then HSTADRL, matching the longword RMW; the write order is the
 * same as text_latch_hstadr(). */
static void text_step_hstadr(stunrun_board_t *b)
{
    uint32_t adr = ((uint32_t)b->read_gsp_control(b->ctx, STUNRUN_TEXT_HSTADR_H) << 16)
                 | (uint32_t)b->read_gsp_control(b->ctx, STUNRUN_TEXT_HSTADR_L);
    text_latch_hstadr(b, adr + STUNRUN_TEXT_PIXEL_STEP);
}

uint32_t stunrun_text_draw(stunrun_board_t *board, uint16_t index)
{
    stunrun_board_t *b = board;
    uint32_t record;
    uint32_t text_addr;
    uint32_t d0;
    uint32_t d1;
    uint32_t d3;
    uint32_t cells = 0u;
    uint16_t d2;
    uint16_t d5;
    uint16_t d6;
    uint16_t d7;

    if (b == NULL)
        return 0u;
    /* DOCUMENTED CHOICE. The ROM does not bound the index: $25FC masks the
     * caller's D0 to 16 bits, $2602 scales it by four and $2604 loads
     * whatever longword sits there. Callers only ever pass 0..320, and the
     * descriptor table is exactly STUNRUN_STRING_COUNT entries, so out of
     * range is rejected here rather than reading past the table. */
    if (index >= STUNRUN_STRING_COUNT)
        return 0u;

    /* $25E8 `clr.w $608000.l` -- the watchdog kick every call opens with. */
    stunrun_b_watchdog(b);

    /* $25EE `andi.w #$E7FF,$c00008.l`. Read-modify-write of HSTCTL: clears
     * the TMS34010's post-increment-on-HSTDAT-write and -read bits $1800, so
     * the hand-rolled `addi.l #$10,$c00002.l` does not double-step the
     * address. Without it every write above would also move HSTADR. */
    b->write_gsp_host(b->ctx, STUNRUN_TEXT_HSTCTL,
                      (uint16_t)(b->read_gsp_control(b->ctx, STUNRUN_TEXT_HSTCTL)
                                 & STUNRUN_TEXT_HSTCTL_KEEP));

    /* $25F6 `lea $10136.l,A0` / $25FC `andi.l #$FFFF,D0` / $2602 `lsl.l #2,D0`
     * $2604 `movea.l (A0,D0.l),A0` */
    record = text_rom32(b, STUNRUN_STRING_TABLE + (uint32_t)index * 4u);

    /* $2608 `clr.w D7` / $260A `move.b ($2,A0),D7` -- the colour byte. */
    d7 = (uint16_t)b->read_rom8(b->ctx, record + STUNRUN_TEXT_FIELD_COLOUR);
    /* $260E `move.w D7,D6` / $2610 `lsl.w #8,D6` -- the second colour plane,
     * OR-ed into HSTDAT rather than stored over it. */
    d6 = (uint16_t)((d7 << 8) & 0xFFFFu);

    /* $2612 `clr.l D0` / $2614 `move.b ($1,A0),D0` / $2618 `mulu.w #$A0,D0`
     * $261C `lsl.l #8,D0`. The mulu is a word multiply of a byte, so its high
     * half is zero: D0 = hdr[1] * $A000. */
    d0 = (uint32_t)b->read_rom8(b->ctx, record + STUNRUN_TEXT_FIELD_Y);
    d0 *= STUNRUN_TEXT_Y_STRIDE;
    d0 <<= 8;

    /* $261E `clr.l D1` / $2620 `move.b ($0,A0),D1` / $2624 `lsl.l #6,D1`
     * $2626 `add.l D1,D0` / $2628 `addi.l #-$800000,D0`. */
    d1 = (uint32_t)b->read_rom8(b->ctx, record + STUNRUN_TEXT_FIELD_X);
    d0 += (d1 << STUNRUN_TEXT_X_SHIFT);
    d0 -= STUNRUN_TEXT_BIAS;

    /* $262E `move.b ($3,A0),D3` -- the CHARACTER COUNT for the whole cell
     * loop, not a row count. */
    d3 = (uint32_t)b->read_rom8(b->ctx, record + STUNRUN_TEXT_FIELD_COUNT);

    /* $2632 `adda.l #$4,A0` -- text starts past the header. The `0x01` that
     * follows the text in ROM is never tested by this routine. */
    text_addr = record + STUNRUN_TEXT_HEADER_SIZE;

    for (;;) {
        uint32_t a1;

        /* $2638 `move.b (A0)+,D1` */
        d1 = (uint32_t)b->read_rom8(b->ctx, text_addr);
        text_addr += 1u;
        /* $263A `subi.b #$2A,D1` -- byte arithmetic, so a character below
         * '*' underflows and simply selects a low glyph index. */
        d1 = (d1 - STUNRUN_TEXT_CHAR_BIAS) & 0xFFu;
        /* $263E `andi.l #$FF,D1` / $2644 `lsl.w #4,D1` */
        d1 = (d1 << 4) & 0xFFFFu;
        /* $2646 `lea $01FBC.l,A1` / $264C `adda.w D1,A1` */
        a1 = (uint32_t)(STUNRUN_FONT_TABLE + (int32_t)(int16_t)(uint16_t)d1);

        /* $264E `move.w #$7,D2` -- OUTSIDE the `dbra D2` range, so it is set
         * once per cell. `dbra` runs the counter down to $FFFF, so both nests
         * are eight passes, not seven. */
        d2 = 7u;
        do {
            /* $2652 `move.w #$7,D1` -- inside the `dbra D2` range: a per-row
             * local. */
            uint16_t pixels = 7u;

            /* $2656 `move.l D0,$c00002.l` -- re-latched once per ROW. */
            text_latch_hstadr(b, d0);
            /* $265C `move.w (A1)+,D5` -- outside the `dbra D1` range, so A1
             * advances once per ROW. Eight rows x two bytes is exactly the
             * sixteen bytes of one glyph. */
            d5 = b->read_rom16(b->ctx, a1);
            a1 += 2u;

            for (;;) {
                /* $265E `btst #0,D5` / $2666 `move.w D7,$c0000c.l` -- nibble
                 * bit 0 is the first colour plane; a clear nibble skips the
                 * HSTDAT cycle entirely. */
                if ((d5 & 0x0001u) != 0u)
                    b->write_gsp_host(b->ctx, STUNRUN_TEXT_HSTDAT, d7);
                /* $266C `btst #2,D5` / $2674 `or.w D6,$c0000c.l` -- nibble
                 * bit 2 is the second plane, OR-ed into whatever HSTDAT read
                 * back, so the first plane of the previous write survives. */
                if ((d5 & 0x0004u) != 0u)
                    b->write_gsp_host(b->ctx, STUNRUN_TEXT_HSTDAT,
                                      (uint16_t)(b->read_gsp_control(b->ctx,
                                                                    STUNRUN_TEXT_HSTDAT)
                                                 | d6));
                /* $267A `addi.l #$10,$c00002.l` -- on the device, not D0. */
                text_step_hstadr(b);
                /* $2684 `lsr.w #4,D5` -- the next nibble. After four shifts
                 * the word is empty, so pixels five to seven are silent. */
                d5 = (uint16_t)(d5 >> 4);
                /* $2686 `dbra D1,$265E` */
                pixels = (uint16_t)(pixels - 1u);
                if (pixels == 0xFFFFu)
                    break;
            }
            /* $268A `addi.l #$1000,D0` */
            d0 += STUNRUN_TEXT_ROW_STEP;
            /* $2690 `dbra D2,$2652` */
            d2 = (uint16_t)(d2 - 1u);
        } while (d2 != 0xFFFFu);

        /* $2694 `subi.l #$7FC0,D0` -- undo the eight rows of stride
         * ($8000) and add one four-pixel cell ($40). */
        d0 -= STUNRUN_TEXT_CELL_REWIND;

        cells++;
        /* $269A `subq.b #1,D3` / $269C `bne $2638`. A count of zero wraps to
         * $FF and runs 256 cells, which is what the ROM does. */
        d3 = (d3 - 1u) & 0xFFu;
        if (d3 == 0u)
            break;
    }

    /* $269E is `jmp (A2)`, not `rts`: the caller installed its own return
     * trampoline. Here the routine simply returns the cell count. */
    return cells;
}