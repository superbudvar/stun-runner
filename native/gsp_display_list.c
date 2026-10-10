/* The GSP display-list interpreter and the sparse fill that feeds it.
 *
 * See gsp_display_list.h for provenance and the host-window encoding.
 *
 * Transcribed from `/tmp/dis68k /tmp/maincpu_flat1m.bin` over ROM byte
 * addresses 0x000225B0-0x00022748, 0x0002418-0x0002432, 0x00000BC4-0x00000C26
 * and 0x00000C46-0x00000C62 of `136070-2102.200r`.
 *
 * Register names in the comments (D0-D5, A0-A3) are the ROM's, not the C
 * function's, and they are kept because most of what is surprising here is
 * WHICH register a value is read out of - the pending-word flag lives in D2, the
 * parked half-word in D3, the loop seeds in D0 - and none of that is visible
 * without the original instruction next to it.  Every routine is transliterated
 * instruction for instruction rather than tidied into helpers, because four of
 * the opcode handlers are near-identical and differ only in ways a helper would
 * have to hide.  That divergence is the point; see BUGS.md SR-11, SR-12 and
 * SR-13.
 *
 * Nothing here implements the transport: every effect on hardware goes through
 * the callbacks in stunrun_gsp_display_list_t.
 */
#include "gsp_display_list.h"

/* ── 68010 register-file helpers ───────────────────────────────────────── */

/* `dbra Dn,<target>`: decrement the low word, loop while it did not wrap to
 * $FFFF.  So the body runs seed + 1 times, and a seed of 0 still runs once.
 * This is why every count in this file is "off by one" relative to the value
 * you would read in the ROM's immediate. */
static int stunrun_gsp_dl_dbra(uint16_t *reg)
{
    *reg = (uint16_t)(*reg - 1u);
    return *reg != 0xFFFFu;
}

/* `subq.w #n,Dn` and `lsr.w #1,Dn` only touch D0's low word, so a following
 * `bmi` tests bit 15 of that word. */
static int stunrun_gsp_dl_word_negative(uint16_t value)
{
    return (value & 0x8000u) != 0u;
}

/* `lsr.b #1,D0`: only the low byte moves.  Kept byte-exact even though D0's high
 * byte happens to be zero everywhere it is used (every D0 value here arrives
 * via `moveq #0,D0` then `move.b (A0)+,D0`). */
static uint16_t stunrun_gsp_dl_lsr_byte(uint16_t value)
{
    return (uint16_t)((value & 0xFF00u) | ((value >> 1) & 0x00FFu));
}

/* `move.b (A0)+,Dn`: a byte-size move to a data register clears the rest of the
 * register, so D3 becomes 0x000000bb, not 0xFFFF00bb. */
static uint32_t stunrun_gsp_dl_read_byte(const uint8_t **cursor)
{
    const uint8_t *p = *cursor;

    *cursor = p + 1;
    return (uint32_t)*p;
}

/* `move.w (A0)+,Dn` and `move.w (A0),Dn`. */
static uint16_t stunrun_gsp_dl_read_word_at(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static uint16_t stunrun_gsp_dl_read_word(const uint8_t **cursor)
{
    const uint8_t *p = *cursor;

    *cursor = p + 2;
    return stunrun_gsp_dl_read_word_at(p);
}

/* `move.l <imm>,$c00002.l`: a 68010 long store to the host window lands on
 * HSTADRH first and HSTADRL second, because `hd68k_gsp_io_w` maps offset / 2 to
 * the host-port word index (atari/harddriv.cpp:602) and tms34010_device's
 * register order puts HOST_ADDRESS_H at index 1 and HOST_ADDRESS_L at index 0
 * (tms34010.h:1094-1097). */
static void stunrun_gsp_dl_latch_address(stunrun_gsp_display_list_t *io,
                                         uint32_t address)
{
    io->set_host_address_hi(io->ctx, (uint16_t)(address >> 16));
    io->set_host_address_lo(io->ctx, (uint16_t)(address & 0xFFFFu));
}

/* ── 0x225B0: the 16-byte lookup-table loader ───────────────────────────── */

void stunrun_gsp_display_list_load_tables(stunrun_gsp_display_list_t *io,
                                          const uint8_t *stream,
                                          uint32_t *consumed)
{
    const uint8_t *a0 = stream;
    uint16_t d1 = 0x000Fu; /* 000225b8: moveq #$f,D1 */
    unsigned i = 0u;

    if (io == NULL || stream == NULL) {
        if (consumed != NULL) {
            *consumed = 0u;
        }
        return;
    }

    /* 000225b0: lea $ff902a.l,A1   - A1 is the anchor of every table access
     *                                      below, and neither interpreter sets
     *                                      it: they inherit it from this routine.
     * 000225b6: moveq #0,D0
     * 000225ba: move.b (A0)+,D0
     * 000225bc: move.b D0,(-$20,A1)   -> $FF900A + 2i, one BYTE
     * 000225c0: move.w D0,(A1)+       -> $FF902A + 2i, one WORD
     * 000225c2: dbra   D1,$225ba      - 16 iterations
     * 000225c6: lea $ff902a.l,A1      - restored, because the loop moved it
     * 000225cc: rts
     *
     * Each stream byte therefore lands twice, 0x20 apart: once as the low byte
     * of a word in the FIRST block (so $FF900A + 2i is the byte and
     * $FF900B + 2i is untouched) and once as a whole word in the second (so
     * $FF902A + 2i = $00bb and $FF902B + 2i = the byte).  `dbra` with seed
     * $000F at the bottom of the body gives exactly 16 iterations: the 16th
     * decrement takes $0000 to $FFFF and ends the loop, so bytes 0..15 of the
     * stream are consumed. */
    /* `dbra` sits at the BOTTOM of the body here (the branch target is $225ba,
     * the `move.b`), so the body is entered once before the first decrement and
     * the count is seed + 1: 16 passes, consuming stream bytes 0..15. */
    do {
        uint32_t value = stunrun_gsp_dl_read_byte(&a0);

        io->poke_byte(io->ctx, STUNRUN_GSP_DL_TABLE_LO + 2u * i,
                      (uint8_t)value);
        io->poke_word(io->ctx, STUNRUN_GSP_DL_TABLE_HI + 2u * i,
                      (uint16_t)value);
        i++;
    } while (stunrun_gsp_dl_dbra(&d1));

    if (consumed != NULL) {
        *consumed = (uint32_t)(a0 - stream);
    }
}

/* ── 0x225E4: the interpreter ───────────────────────────────────────────── */

typedef struct {
    stunrun_gsp_display_list_t *io;
    const uint8_t *a0;
    uint16_t d0;
    uint16_t d1;
    uint16_t d2; /* the pending-word flag */
    uint32_t d3; /* 32 bits: `lsl.l #8,D3` at $22650 puts a byte in the HIGH
                  * word, and `move.w D3,(A2)` then drops it */
    uint16_t d4;
    uint16_t d5;
    unsigned words; /* FIFO words emitted */
} stunrun_gsp_dl_regs_t;

static void stunrun_gsp_dl_emit(stunrun_gsp_dl_regs_t *r, uint16_t word)
{
    r->io->write_fifo_word(r->io->ctx, word);
    r->words++;
}

/* $22690, $226b2, $2271c, $22726, $2273e: `move.b ($1,A1,Dn.w),D3` - the BYTE at
 * $FF902B + index, i.e. lookup-table byte index/2.  0x225B0's
 * `move.w D0,(A1)+` writes two bytes per iteration, $FF902A + 2i = 0x00 and
 * $FF902B + 2i = the stream's table byte, so this read does reach the stream's
 * own table.  The indices are masked even, so the +1 always lands on an odd
 * address.
 *
 * `move.b` into D3 clears D3's upper three bytes, so D3.w is exactly this byte:
 * no high half ever survives to the `move.w D3,(A2)` that follows, which is
 * what makes the `move.w (-$20,A1,D4.w),D3` before it dead.  BUGS.md SR-11. */
static uint32_t stunrun_gsp_dl_table_read(stunrun_gsp_display_list_t *io,
                                          uint16_t index)
{
    return (uint32_t)io->peek_byte(io->ctx, STUNRUN_GSP_DL_TABLE_HI + 1u + index);
}

/* $2261E-$22670: opcode 0x01-0x3F - copy `count` units out of the stream.
 *
 * D0 is the opcode masked to 6 bits, 1..0x3F.  D2 is the pending-word flag left
 * behind by an odd-length copy; when set, one FRESH byte is pulled out of the
 * stream and emitted - the parked half-word is dropped, not emitted.  (The
 * other flush site, $226de, also drops it and instead emits the fill's own
 * operand byte.  BUGS.md SR-13.)
 *
 * The copy then splits on the parity of the stream pointer ($2262c), and the two
 * halves do NOT park the same thing for an odd count.  BUGS.md SR-12. */
static void stunrun_gsp_dl_op_copy(stunrun_gsp_dl_regs_t *r)
{
    /* 0002261e: tst.w D2 / 00022620: beq $2262c */
    if (r->d2 != 0u) {
        /* 00022622: move.b (A0)+,D3
         * 00022624: move.w D3,(A2)      - the fresh byte, not the parked word
         * 00022626: moveq #0,D2
         * 00022628: subq.w #1,D0
         * 0002262a: beq $22606 */
        r->d3 = stunrun_gsp_dl_read_byte(&r->a0);
        stunrun_gsp_dl_emit(r, (uint16_t)r->d3);
        r->d2 = 0u;
        r->d0 = (uint16_t)(r->d0 - 1u);
        if (r->d0 == 0u) {
            return;
        }
    }

    /* 0002262c: move.w A0,D1 / 0002262e: andi.w #$1,D1 / 00022632: beq $22656
     *
     * The stream pointer's low bit selects the copy width, so the same opcode
     * encodes two different things depending on where in the stream it lands.
     * The test cases pin both parities explicitly.  D1 is overwritten at $22634
     * or $22656, so only the branch survives. */
    if (((uintptr_t)r->a0 & 1u) != 0u) {
        /* ODD pointer: consume byte pairs, but only the SECOND byte of each
         * pair reaches the FIFO.
         *
         *   00022634: move.b D0,D1        - the count, low byte only
         *   00022636: lsr.b #1,D0
         *   00022638: subq.w #1,D0
         *   0002263a: bmi $22648          - a count under 2 skips the loop
         *   0002263c: move.b (A0)+,D3     - the first byte ...
         *   0002263e: lsl.w #8,D3         - ... shifted into the high half
         *   00022640: move.b (A0)+,D3     - and then OVERWRITTEN by the second
         *   00022642: move.w D3,(A2)        byte, which a byte-sized move
         *                                      zeroes D3's upper three bytes
         *   00022644: dbra D0,$2263c      - count / 2 iterations
         *   00022648: btst #0,D1          - an odd count leaves a byte
         *   0002264c: beq $22606
         *   0002264e: move.b (A0)+,D3
         *   00022650: lsl.l #8,D3         - a LONG shift: into bits 23..16,
         *                                    so D3.w is $0000
         *   00022652: not.w D2            - arm the pending flag
         *   00022654: bra $22606
         *
         * So on this path the assembled "word" is just its low byte.  That is
         * the same class of dead-code defect as the table-run's dead word load
         * below, and the same mistake as writing `lsl` in C and assuming the
         * high half survives the next byte read: on a 68010 a byte-sized move
         * to a data register clears bits 31..8, so $2263e cannot.  The shift is
         * transcribed and then immediately discarded.  BUGS.md SR-11. */
        r->d1 = (uint16_t)(uint8_t)r->d0; /* move.b D0,D1 */
        r->d0 = stunrun_gsp_dl_lsr_byte(r->d0);
        r->d0 = (uint16_t)(r->d0 - 1u);

        if (!stunrun_gsp_dl_word_negative(r->d0)) {
            do {
                r->d3 = stunrun_gsp_dl_read_byte(&r->a0);
                r->d3 = (r->d3 << 8) & 0xFFFFFFFFu;
                r->d3 = stunrun_gsp_dl_read_byte(&r->a0);
                stunrun_gsp_dl_emit(r, (uint16_t)r->d3);
            } while (stunrun_gsp_dl_dbra(&r->d0));
        }

        if ((r->d1 & 1u) != 0u) {
            r->d3 = stunrun_gsp_dl_read_byte(&r->a0);
            r->d3 = (r->d3 << 8) & 0xFFFFFFFFu;
            r->d2 = (uint16_t)~r->d2;
        }
        return;
    }

    /* EVEN pointer: copy whole words straight to the FIFO.
     *
     *   00022656: move.b D0,D1
     *   00022658: lsr.b #1,D0
     *   0002265a: subq.w #1,D0
     *   0002265c: bmi $2266a
     *   0002265e: move.w (A0)+,(A2)
     *   00022660: dbra D0,$2265e
     *   00022664: btst #0,D1
     *   00022668: beq $22606
     *   0002266a: move.w (A0),D3        - a WORD, and NO post-increment
     *   0002266c: addq.l #1,A0          - yet A0 advances ONE byte
     *   0002266e: not.w D2
     *   00022670: bra $22606
     *
     * The leftover is a single byte, but the ROM reads a whole word from it and
     * then steps over one byte, so the second byte of that word is parked in D3
     * AND left in the stream, and the pointer ends up ODD - which flips the
     * parity branch for the NEXT opcode.  BUGS.md SR-12. */
    r->d1 = (uint16_t)(uint8_t)r->d0; /* move.b D0,D1 */
    r->d0 = stunrun_gsp_dl_lsr_byte(r->d0);
    r->d0 = (uint16_t)(r->d0 - 1u);

    if (!stunrun_gsp_dl_word_negative(r->d0)) {
        do {
            stunrun_gsp_dl_emit(r, stunrun_gsp_dl_read_word(&r->a0));
        } while (stunrun_gsp_dl_dbra(&r->d0));
    }

    if ((r->d1 & 1u) != 0u) {
        r->d3 = (uint32_t)stunrun_gsp_dl_read_word_at(r->a0);
        r->a0 += 1;
        r->d2 = (uint16_t)~r->d2;
    }
}

/* $22672-$226c6: opcode 0x40-0x7F - read `count` operand bytes and emit one
 * table word each.
 *
 *   00022672: subq.w #1,D0 / 00022674: bmi $225d4   - only opcode 0x40
 *   00022678: tst.w D2 / 0002267a: bne $226a8
 *   0002267c: move.b (A0)+,D4
 *   0002267e: move.w D4,D5
 *   00022680: lsr.b #3,D4
 *   00022682: andi.w #$1e,D4        - high nibble, kept even
 *   00022686: andi.w #$f,D5
 *   0002268a: add.w D5,D5           - low nibble, doubled
 *   0002268c: move.w (-$20,A1,D4.w),D3   - DEAD, see below
 *   00022690: move.b ($1,A1,D5.w),D3
 *   00022694: move.w D3,(A2)
 *   00022696: dbra D0,$2267c
 *   0002269a: bra $22606
 *
 * The D2-set path ($226a8) reads a different register for the index (D5 = high
 * nibble rather than D4) and, unlike $2267c, never clears D2 - so the flag
 * survives the opcode and is still pending for the next one.  The
 * `move.w (-$20,A1,D4.w),D3` at $226a4 is DEAD: the `move.b` at $226b2 clears
 * the register it had just filled.  Transcribed as dead rather than dropped.
 *
 * Returns non-zero for opcode 0x40, which branches to the dead epilogue at
 * $225d4 without the `jsr`. */
static int stunrun_gsp_dl_op_table_run(stunrun_gsp_dl_regs_t *r)
{
    uint16_t low_nibble;

    /* 00022672: subq.w #1,D0 / 00022674: bmi $225d4 */
    r->d0 = (uint16_t)(r->d0 - 1u);
    if (stunrun_gsp_dl_word_negative(r->d0)) {
        return 1;
    }

    /* 00022678: tst.w D2 / 0002267a: bne $226a8
     *
     * WHICH NIBBLE of the operand byte selects the table entry is decided once,
     * here, and holds for the whole opcode - not per iteration:
     *
     *   D2 clear -> enter at $2267c, whose `dbra D0,$2267c` loops back to the
     *              byte read, so every pass indexes on the LOW nibble (D5).
     *   D2 set   -> enter at $226a8, whose `dbra D0,$2269e` loops back to a
     *              block that only masks D4 (never read) and then falls into
     *              $226a8 again, so every pass indexes on the HIGH nibble (D5)
     *              and D2 is never cleared.
     *
     * Getting this per-iteration instead of per-opcode silently emits table
     * byte 0 for every pass after the first - which is exactly what the test
     * caught the first time round. */
    low_nibble = (r->d2 == 0u) ? 1u : 0u;

    for (;;) {
        uint32_t byte = stunrun_gsp_dl_read_byte(&r->a0);
        uint16_t index;

        if (low_nibble) {
            /* 0002267c: move.b (A0)+,D4 / 0002267e: move.w D4,D5 /
             * 00022680: lsr.b #3,D4 / 00022682: andi.w #$1e,D4 - dead, see below
             * 00022686: andi.w #$f,D5 / 0002268a: add.w D5,D5
             * 0002268c: move.w (-$20,A1,D4.w),D3  - DEAD, see below */
            r->d4 = (uint16_t)((byte >> 3) & 0x1Eu);
            r->d5 = (uint16_t)((uint16_t)(byte & 0x0Fu) * 2u);
            index = r->d5;
        } else {
            /* 000226a8: move.b (A0)+,D4 / 000226aa: move.w D4,D5 /
             * 000226ac: lsr.b #3,D5 / 000226ae: andi.w #$1e,D5
             *   - indexes on the HIGH nibble of the current byte */
            r->d4 = (uint16_t)byte;
            r->d5 = (uint16_t)((byte >> 3) & 0x1Eu);
            index = r->d5;
        }

        /* 00022690 / 000226b2: move.b ($1,A1,D5.w),D3 */
        r->d3 = stunrun_gsp_dl_table_read(r->io, index);
        stunrun_gsp_dl_emit(r, (uint16_t)r->d3);

        if (!stunrun_gsp_dl_dbra(&r->d0)) {
            break;
        }
    }

    /* 000226bc: andi.w #$f,D4 / 000226c0: add.w D4,D4 /
     * 000226c2: move.w (-$20,A1,D4.w),D3 / 000226c6: bra $22606
     *   - the load is dead again; D3 is overwritten by the next opcode. */
    return 0;
}

/* $226d8-$226fe: opcode 0x81-0xBF - consume ONE operand byte and emit
 * floor(count / 2) copies of it as a 16-bit word.
 *
 *   000226d8: move.b (A0)+,D3
 *   000226da: tst.w D2 / 000226dc: beq $226e8
 *   000226de: move.w D3,(A2)        - the operand, not the parked word
 *   000226e0: moveq #0,D2
 *   000226e2: subq.w #1,D0 / 000226e4: beq $22606
 *   000226e8: lsr.w #1,D0           - X gets the count's low bit
 *   000226ea: subx.w D2,D2          - D2 - D2 - X; D2 is 0 here, so $0000/$FFFF
 *   000226ec: move.w D3,D5
 *   000226ee: lsl.w #8,D3
 *   000226f0: move.b D5,D3          - a byte round trip: D3 is unchanged
 *   000226f2: subq.w #1,D0
 *   000226f4: bmi $22606            - a count of 1 emits nothing at all
 *   000226f8: move.w D3,(A2)
 *   000226fa: dbra D0,$226f8
 *   000226fe: bra $22606
 *
 * With D2 set the total is 1 + floor((count - 1) / 2) words; with D2 clear it is
 * floor(count / 2).  The `subx` re-arms D2 from the count's low bit, so an odd
 * count leaves a pending flag that fires on the NEXT opcode. */
static void stunrun_gsp_dl_op_fill(stunrun_gsp_dl_regs_t *r)
{
    uint16_t extend;

    /* 000226d8: move.b (A0)+,D3 - note this happens BEFORE the D2 test, so the
     * "flush" below is the operand byte of THIS opcode, and whatever the
     * previous opcode parked in D3 is simply overwritten and lost. */
    r->d3 = stunrun_gsp_dl_read_byte(&r->a0);

    /* 000226da: tst.w D2 / 000226dc: beq $226e8 */
    if (r->d2 != 0u) {
        /* 000226de: move.w D3,(A2) - the operand byte just read, already
         * zero-extended to 16 bits.  $22622, the other flush site, instead
         * reads a byte it has not consumed yet.  Neither emits the parked word.
         * BUGS.md SR-13. */
        stunrun_gsp_dl_emit(r, (uint16_t)r->d3);
        r->d2 = 0u;
        r->d0 = (uint16_t)(r->d0 - 1u);
        if (r->d0 == 0u) {
            return;
        }
    }

    /* 000226e8: lsr.w #1,D0 */
    extend = (uint16_t)(r->d0 & 1u);
    r->d0 = (uint16_t)(r->d0 >> 1);

    /* 000226ea: subx.w D2,D2 */
    r->d2 = (uint16_t)(0u - (uint16_t)0u - extend);

    /* 000226ec: move.w D3,D5 / 000226ee: lsl.w #8,D3 / 000226f0: move.b D5,D3 */
    r->d5 = (uint16_t)r->d3;
    r->d3 = (r->d3 & 0xFFFF0000u) | ((uint32_t)(uint16_t)r->d3 << 8);
    r->d3 = (r->d3 & 0xFFFF0000u) | (r->d5 & 0xFFu);

    /* 000226f2: subq.w #1,D0 / 000226f4: bmi $22606 */
    r->d0 = (uint16_t)(r->d0 - 1u);
    if (stunrun_gsp_dl_word_negative(r->d0)) {
        return;
    }

    /* 000226f8: move.w D3,(A2) / 000226fa: dbra D0,$226f8 */
    do {
        stunrun_gsp_dl_emit(r, (uint16_t)r->d3);
    } while (stunrun_gsp_dl_dbra(&r->d0));
}

/* $22702-$22746: opcode 0xC0-0xFF - consume ONE operand byte, exactly like
 * $226d8, but repeat the TABLE word for that byte instead of the byte itself.
 *
 *   00022708: move.b (A0)+,D4
 *   0002270a: move.w D4,D5
 *   0002270c: lsr.b #3,D4 / 0002270e: andi.w #$1e,D4   - D4 = high nibble
 *   00022712: andi.w #$f,D5 / 00022716: add.w D5,D5     - D5 = low nibble
 *   00022718: tst.w D2 / 0002271a: beq $2273a
 *   0002271c: move.b ($1,A1,D4.w),D3   - HIGH nibble
 *   00022720: move.w D3,(A2)           - emitted unconditionally
 *   00022722: move.w (-$20,A1,D5.w),D3
 *   00022726: move.b ($1,A1,D4.w),D3   - HIGH nibble again, not D5; the word
 *   0002272a: subq.w #2,D0              load at $22722 is dead
 *   0002272c: bmi $22606
 *   00022730: move.w D3,(A2) / 00022732: dbra D0,$22730 / 00022736: bra
 *   0002273a: move.w (-$20,A1,D4.w),D3
 *   0002273e: move.b ($1,A1,D5.w),D3   - LOW nibble
 *   00022742: subq.w #1,D0 / 00022744: bpl $22730
 *
 * So within this one opcode the pending path and the clear path index different
 * nibbles of the same operand byte: HIGH when D2 is set, LOW when it is clear.
 * The two halves of $22730 are shared, and both paths emit `count` words. */
static void stunrun_gsp_dl_op_table_fill(stunrun_gsp_dl_regs_t *r)
{
    uint32_t byte;
    uint16_t d4;
    uint16_t d5;

    /* 00022708: move.b (A0)+,D4 / 0002270a: move.w D4,D5 /
     * 0002270c: lsr.b #3,D4 / 0002270e: andi.w #$1e,D4 /
     * 00022712: andi.w #$f,D5 / 00022716: add.w D5,D5 */
    byte = stunrun_gsp_dl_read_byte(&r->a0);
    d4 = (uint16_t)((byte >> 3) & 0x1Eu);
    d5 = (uint16_t)((uint16_t)(byte & 0x0Fu) * 2u);

    /* 00022718: tst.w D2 / 0002271a: beq $2273a */
    if (r->d2 != 0u) {
        /* 0002271c: move.b ($1,A1,D4.w),D3 - the HIGH nibble */
        r->d3 = stunrun_gsp_dl_table_read(r->io, d4);
        /* 00022720: move.w D3,(A2) - emitted unconditionally */
        stunrun_gsp_dl_emit(r, (uint16_t)r->d3);
        /* 00022722: move.w (-$20,A1,D5.w),D3 - dead, and indexed on the other
         * nibble than the byte read that follows.  Kept as written.
         * 00022726: move.b ($1,A1,D4.w),D3 - the HIGH nibble again, NOT D5 */
        r->d3 = stunrun_gsp_dl_table_read(r->io, d4);

        /* 0002272a: subq.w #2,D0 / 0002272c: bmi $22606 */
        r->d0 = (uint16_t)(r->d0 - 2u);
        if (stunrun_gsp_dl_word_negative(r->d0)) {
            return;
        }
        /* 00022730 / 00022732: shared with the D2-clear path below */
        do {
            stunrun_gsp_dl_emit(r, (uint16_t)r->d3);
        } while (stunrun_gsp_dl_dbra(&r->d0));
        return;
    }

    /* 0002273a: move.w (-$20,A1,D4.w),D3 - dead
     * 0002273e: move.b ($1,A1,D5.w),D3 - the LOW nibble this time */
    r->d3 = stunrun_gsp_dl_table_read(r->io, d5);

    /* 00022742: subq.w #1,D0 / 00022744: bpl $22730 */
    r->d0 = (uint16_t)(r->d0 - 1u);
    if (stunrun_gsp_dl_word_negative(r->d0)) {
        return;
    }
    do {
        stunrun_gsp_dl_emit(r, (uint16_t)r->d3);
    } while (stunrun_gsp_dl_dbra(&r->d0));
}

void stunrun_gsp_display_list_run(stunrun_gsp_display_list_t *io,
                                  const uint8_t *stream,
                                  stunrun_gsp_display_list_result_t *result)
{
    stunrun_gsp_dl_regs_t r;

    if (result != NULL) {
        result->stream_end = 0u;
        result->words_emitted = 0u;
        result->end = STUNRUN_GSP_DL_END_RESET;
    }
    if (io == NULL || stream == NULL || result == NULL) {
        return;
    }

    /* 000225e4: movea.l ($4,A7),A0      - the stream pointer is the argument
     * 000225e8: movem.l D2-D5/A2-A3,-(A7)
     * 000225ec: lea $c0000c.l,A2        - HSTDAT; A2 is the FIFO alias for the
     *                                     whole routine, which is why every
     *                                     emit below reads as `move.w Dn,(A2)`
     * 000225f2: lea $608000.l,A3        - the watchdog
     * 000225f8: moveq #0,D3
     * 000225fa: moveq #0,D5
     * 000225fc: moveq #0,D4
     * 000225fe: moveq #0,D2
     *
     * A1 is NOT set: the two interpreters inherit it from 0x225b0, which
     * leaves it at $FF902A.  Both call 0x225b0 before the first table read
     * (the entry byte is consumed first), so the invariant holds. */
    r.io = io;
    r.a0 = stream;
    r.d0 = 0u;
    r.d1 = 0u;
    r.d2 = 0u;
    r.d3 = 0u;
    r.d4 = 0u;
    r.d5 = 0u;
    r.words = 0u;

    /* 00022600: move.b (A0)+,D0 / 00022602: bne $225ce
     *   A nonzero FIRST byte ends the list before anything is compiled.  It is
     *   still consumed. */
    if (stunrun_gsp_dl_read_byte(&r.a0) != 0u) {
        goto terminator;
    }

    /* 00022604: bsr $225b0 */
    {
        uint32_t consumed = 0u;

        stunrun_gsp_display_list_load_tables(io, &r.a0[0], &consumed);
        r.a0 += consumed;
    }

    for (;;) {
        /* 00022606: moveq #0,D0
         * 00022608: clr.w (A3)          - the watchdog is kicked once per
         *                                  opcode, before it is decoded
         * 0002260a: move.b (A0)+,D0 */
        r.d0 = 0u;
        io->poke_word(io->ctx, STUNRUN_GSP_DL_WATCHDOG, 0u);
        r.d0 = (uint16_t)(uint8_t)stunrun_gsp_dl_read_byte(&r.a0);

        /* 0002260c: bclr #$7,D0 / 00022610: bne $226ca */
        if ((r.d0 & 0x80u) != 0u) {
            r.d0 = (uint16_t)(r.d0 & 0x7Fu);
            /* 000226ca: bclr #$6,D0 / 000226ce: bne $22702 */
            if ((r.d0 & 0x40u) != 0u) {
                r.d0 = (uint16_t)(r.d0 & 0x3Fu);
                /* 00022702: tst.w D0 / 00022704: beq $225ce - opcode 0xC0 */
                if (r.d0 == 0u) {
                    goto terminator;
                }
                stunrun_gsp_dl_op_table_fill(&r);
            } else {
                r.d0 = (uint16_t)(r.d0 & 0x3Fu);
                /* 000226d2: tst.w D0 / 000226d4: beq $225ce - opcode 0x80 */
                if (r.d0 == 0u) {
                    goto terminator;
                }
                stunrun_gsp_dl_op_fill(&r);
            }
            continue;
        }

        /* 00022614: bclr #$6,D0 / 00022618: bne $22672 */
        if ((r.d0 & 0x40u) != 0u) {
            r.d0 = (uint16_t)(r.d0 & 0x3Fu);
            if (stunrun_gsp_dl_op_table_run(&r)) {
                /* opcode 0x40 only: $225d4, the dead epilogue WITHOUT the jsr */
                result->stream_end = (uint32_t)(r.a0 - stream);
                result->words_emitted = r.words;
                result->end = STUNRUN_GSP_DL_END_OPCODE_40;
                return;
            }
            continue;
        }

        /* 0002261a: tst.b D0 / 0002261c: beq $22604 - opcode 0x00 is a table
         * reload, not a stop: control falls back into 0x225b0 and then reads
         * the next opcode. */
        if ((uint8_t)r.d0 == 0x00u) {
            uint32_t consumed = 0u;

            stunrun_gsp_display_list_load_tables(io, &r.a0[0], &consumed);
            r.a0 += consumed;
            continue;
        }

        /* 0002261e: everything else in 0x01-0x3F */
        stunrun_gsp_dl_op_copy(&r);
    }

terminator:
    /* 000225ce: jsr $21286.l
     *
     * $21286 is the SSP reset entry: `stop #$2700` / `movea.l $0.w,A7` /
     * `move #$2700,SR` / `movea.l #$0,A0` / `movec A0,VBR` /
     * `move.w #$4,$818040.l` / `clr.l $8018.w` / `movea.l $4.w,A0` /
     * `jmp (A0)`.  It never returns, so the `move.l A0,D0` at $225dc and the
     * pending-word flush at $225d4-$225da never execute - which is why
     * `stream_end` is only meaningful as "how far this C read", and why the C
     * does not emit the $0000 flush.
     *
     * So the interpreter has no clean exit: reaching the end of a display list
     * resets the 68010.  BUGS.md SR-14. */
    result->stream_end = (uint32_t)(r.a0 - stream);
    result->words_emitted = r.words;
    result->end = STUNRUN_GSP_DL_END_RESET;
}

/* ── 0x0BC4: the sparse fill ────────────────────────────────────────────── */

/* 0x2418: turn a record's tile and row into a GSP 2bpp VRAM address.
 *
 *   00002418: andi.l #$ff,D0
 *   0000241e: mulu.w #$2800,D0
 *   00002422: andi.l #$ff,D1
 *   00002428: lsl.l #4,D1
 *   0000242a: add.l D1,D0
 *   0000242c: addi.l #$2000000,D0
 *   00002432: rts
 *
 * Pure arithmetic, no port side effects - so inlining it here is not a
 * structural claim, just avoidance of a five-call stub. */
static uint32_t stunrun_gsp_dl_base_address(uint32_t tile, uint32_t row)
{
    uint64_t d0 = tile & 0xFFu;
    uint64_t d1 = row & 0xFFu;

    d0 = d0 * 0x2800u;   /* mulu.w #$2800,D0 - 16x16 -> 32 bits */
    d1 = d1 * 16u;       /* lsl.l #4,D1 */
    d0 = d0 + d1;        /* add.l D1,D0 */
    return (uint32_t)(d0 + 0x02000000u); /* addi.l #$2000000,D0 */
}

void stunrun_gsp_display_list_sparse_fill(stunrun_gsp_display_list_t *io,
                                          const uint8_t *stream)
{
    const uint8_t *a0 = stream;
    uint16_t d5;

    if (io == NULL || stream == NULL) {
        return;
    }

    /* 00000bc4: ori.w #$1800,$c00008.l - turn on the HOST_DATA post-increment
     * so the pairs of `move.w D3,(A1)` below land on consecutive addresses. */
    io->or_host_control_mask(io->ctx, STUNRUN_GSP_DL_HOSTCTL_AUTO_INCREMENT);

    /* 00000bcc: move.w (A0)+,D5 - a 2-byte record COUNT, consumed with a
     * post-increment.  `dbra` at $0c22 runs it seed + 1 times, and the ROM's
     * own table at 0xB9A holds $0004 followed by exactly five 8-byte records
     * ending at 0xBC4 - the first byte of this routine.  Confirms both the
     * count and the stride. */
    d5 = stunrun_gsp_dl_read_word(&a0);

    do {
        uint32_t d0;
        uint16_t d1;
        uint16_t d4;

        /* 00000bce: move.w (A0),D1        - record + 0
         * 00000bd0: move.w (2,A0),D0      - record + 2, into D0's low word */
        d1 = stunrun_gsp_dl_read_word_at(a0);
        d0 = (uint32_t)stunrun_gsp_dl_read_word_at(a0 + 2);

        /* 00000bd4: jsr $2418.l */
        d0 = stunrun_gsp_dl_base_address(d0, d1);

        /* 00000bda: move.l #$f4000000,$c00002.l */
        stunrun_gsp_dl_latch_address(io, STUNRUN_GSP_DL_CONTROL_LO);

        /* 00000be4: move.w (6,A0),$c0000c.l - the one control word per record */
        io->write_fifo_word(io->ctx, stunrun_gsp_dl_read_word_at(a0 + 6));

        /* 00000bec: move.w (4,A0),D1 - record + 4 REPLACES the value $2418 used
         * for the address, so from here on D1 is only the inner loop seed. */
        d1 = stunrun_gsp_dl_read_word_at(a0 + 4);

        /* 00000bf0: move.w #$ffff,D3
         * 00000bf4: lea $c0000c.l,A1       - A1 aliases the FIFO for the nest
         * 00000bfa: move.l D0,$c00002.l    - the 2bpp VRAM base for this row
         * 00000c00: move.w D1,D2
         * 00000c02: move.w #$7,D4          - 8 rows, via `dbra` */
        stunrun_gsp_dl_latch_address(io, d0);
        d4 = 0x0007u;

        do {
            uint16_t d2 = d1;

            /* 00000c06: move.w D3,(A1)     - two words per pass; the 0x0800
             * 00000c08: move.w D3,(A1)       post-increment makes them adjacent
             * 00000c0a: dbra D2,$c06         - (seed + 1) pairs
             * 00000c0e: addi.l #$400,D0      - the next row, 0x400 further on
             * 00000c14: move.l D0,$c00002.l  - re-latch it
             * 00000c1a: move.w D1,D2         - the seed is reloaded, so the
             * 00000c1c: dbra D4,$c06           same column count repeats */
            do {
                io->write_fifo_word(io->ctx, 0xFFFFu);
                io->write_fifo_word(io->ctx, 0xFFFFu);
            } while (stunrun_gsp_dl_dbra(&d2));

            d0 += 0x400u;
            stunrun_gsp_dl_latch_address(io, d0);
        } while (stunrun_gsp_dl_dbra(&d4));

        /* 00000c20: addq.l #8,A0 */
        a0 += 8;
        /* 00000c22: dbra D5,$bce - back to the record head, not to $0bc4 */
    } while (stunrun_gsp_dl_dbra(&d5));

    /* 00000c26: rts */
}

/* ── 0x0C46: the $20-stride fill ────────────────────────────────────────── */

void stunrun_gsp_display_list_fill_stride_0x20(stunrun_gsp_display_list_t *io,
                                               uint32_t base, uint16_t word)
{
    uint32_t d3;
    uint16_t d4 = 0x0020u;

    if (io == NULL) {
        return;
    }

    /* 00000c46: move.l D0,D3
     * 00000c48: move.w #$20,D4      - 32 passes
     * 00000c4c: move.l D3,$c00002.l
     * 00000c52: move.w D2,$c0000c.l
     * 00000c58: addi.l #$20,D3
     * 00000c5e: subq.w #1,D4
     * 00000c60: bne $c4c
     * 00000c62: rts
     *
     * NOT reachable from 0x0BC4: nothing in the nest calls out.  The recorded
     * `bsr $c46` is at 0x0C36 (inside 0x0C28); 0x0C7E is a watchdog write,
     * NOT a second call.  Transcribed because it is the closest thing to the
     * 0xBF4 nest and the comparison is the point.
     *
     * Note the stride is $20 to the ADDRESS while the post-increment moves it a
     * further $10 per word, so the 32 writes land on 32 addresses $20 apart,
     * one word each, interleaved with the gaps the post-increment opened.
     *
     * $0c5e/$0c60 are `subq.w`/`bne`, NOT `dbra`, so the count is the literal
     * 0x20 passes rather than seed + 1.  That is the whole difference between
     * this and every other loop in the file, and getting it wrong is a 33rd
     * write at base + $400. */
    d3 = base;
    for (;;) {
        stunrun_gsp_dl_latch_address(io, d3);
        io->write_fifo_word(io->ctx, word);
        d3 += 0x20u;

        /* 00000c5e: subq.w #1,D4 / 00000c60: bne $c4c */
        d4 = (uint16_t)(d4 - 1u);
        if (d4 != 0u) {
            continue;
        }
        break;
    }
}

void stunrun_gsp_display_list_fill_stride_blocks(stunrun_gsp_display_list_t *io)
{
    uint32_t d0 = 0x02000000u;
    uint16_t d1 = 0x000fu;

    if (io == NULL)
        return;

    /* ROM 0x0C28-0x0C44, findings log (stride-fill caller):
     * 0C28 move.l #$02000000,D0
     * 0C2E move.w #$000f,D1
     * 0C32 move.w #$0001,D2
     * 0C36 bsr $c46           (helper preserves D0/D1/D2)
     * 0C3A addi.l #$4000,D0
     * 0C40 subq.w #1,D1
     * 0C42 bne $c36
     * 0C44 rts
     * SUBQ/BNE executes 15 times, not DBRA's seed+1.  No HSTCTL write
     * appears here or in $c46; keep the caller's state, including bit $0800.
     * This models bus effects only, not register/CCR/cycle timing. */
    do {
        stunrun_gsp_display_list_fill_stride_0x20(io, d0, 1u);
        d0 += 0x4000u;
        d1 = (uint16_t)(d1 - 1u);
    } while (d1 != 0u);
}
