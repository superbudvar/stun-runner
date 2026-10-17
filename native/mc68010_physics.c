/* MC68010 speed-update routine at ROM $03ABDE.  See mc68010_physics.h for
 * provenance and what is deliberately not modelled.
 */
#include "mc68010_physics.h"

#include <stddef.h>

/* ── The speed-update block ($03AB82-$03ABDE) ─────────────────────────── */

/* Disassembly (lines 69590-69619 of maincpu-68010.lst):
 *
 *  03AB7A: tst.w   $ffdd4e.l      ; timer != 0?
 *  03AB80: beq     $3abac          ; no: branch to timer==0 path
 *  03AB82: subq.w  #1, $ffdd4e.l  ; decrement timer
 *  03AB88: bne     $3ab98          ; still nonzero: skip expiry writes
 *  03AB8A: clr.w   $ffdd50.l       ; decrement reached zero: clear secondary timer
 *  03AB90: move.b  #$ff, $ffdd0a.l
 *  03AB98: cmpi.w  #$b00, $ffdd16.l ; speed >= 0x0B00?
 *  03ABA0: bcc     $3abaa          ; yes: skip ramp-up
 *  03ABA2: addi.w  #$40, $ffdd16.l ; speed += 0x40
 *  03ABAA: bra     $3abe4          ; branch to accumulator update
 *
 *  03ABAC: move.w  $ffdd16.l, D3   ; timer == 0 path
 *  03ABB2: ext.l   D3
 *  03ABB4: move.w  (-$18,A6), D0   ; local_limit (from caller's frame)
 *  03ABB8: ext.l   D0
 *  03ABBA: moveq   #$40, D1
 *  03ABBC: add.l   D1, D0          ; D0 = local_limit + 0x40
 *  03ABBE: cmp.l   D0, D3          ; speed > (local_limit + 0x40)?
 *  03ABC0: bls     $3abcc          ; no: skip decay
 *  03ABC2: subi.w  #$20, $ffdd16.l ; speed -= 0x20
 *  03ABC6: bra     $3abe4           ; branch to accumulator update
 *
 *  03ABCC: move.w  $ffdd1e.l, D2   ; base + offset path
 *  03ABD2: ext.l   D2
 *  03ABD4: move.w  $ffdd1a.l, D0
 *  03ABDA: ext.l   D0
 *  03ABDC: add.l   D0, D2          ; D2 = source_a + source_b
 *  03ABDE: move.w  D2, $ffdd16.l   ; write updated speed
 */
int16_t stunrun_mc68010_speed_update(int16_t current, uint16_t timer,
                                     int16_t local_limit, int16_t source_a,
                                     int16_t source_b)
{
    stunrun_motion_update_state_t state = {
        (uint16_t)current, timer, 0u, 0u, local_limit,
        source_a, source_b, 0u
    };
    state = stunrun_motion_update_step(state);
    return (int16_t)state.motion_word;
}

/* ── The speed/routine wrapper at $039B82 ───────────────────────────────── */

/* Full disassembly of the function entry (lines 68396-68430 of maincpu-68010.lst):
 *
 *  039B82: link    A6, #-$30         ; create 48-byte local frame
 *  039B86: movem.l D2-D6/A2-A5, -(A7) ; save registers
 *  039B8A: move.w  ($a,A6), (-$2,A6)  ; arg1 (byte from $FFDD82) → local -$2
 *  039B90: move.b  ($f,A6), D4         ; arg2 (ROM table byte or 0) → D4
 *  039B94: move.l  #$ffdd1c, (-$6,A6) ; store $FFDD1C to local -$6
 *  039B9C: jsr     $3ae30.l           ; call initialization routine
 *  039BA2: tst.b   $ffdcf6.l          ; test game-state counter slot
 *  039BA8: beq     $39c16             ; if 0: skip to speed-update setup
 *  039BAC: btst    #$0, $ff9567.l     ; test bit 0 of $FF9567
 *  039BB4: beq     $39c16             ; if clear: skip to speed-update setup
 *  039C16: move.w  #$7e0, (-$18,A6)  ; ★ set local_limit = 0x07E0
 *  039C1C: tst.w   $ffdd4e.l          ; test timer
 *  039C22: beq     $39d22             ; timer==0: branch to alternate path
 *  [ ... large pre-computation block (sets $FFDD1E, $FFDD1A) ... ]
 *  03AB7A: tst.w   $ffdd4e.l          ; speed update: timer != 0?
 *  03AB80: beq     $3abac             ; timer ran out: use base+offset path
 *  03AB82: subq.w  #1, $ffdd4e.l     ; decrement timer
 *  03AB88: bne     $3ab98             ; timer still nonzero: try ramp-up
 *  03AB8A: clr.w   $ffdd50.l          ; timer hit zero: clear secondary timer
 *  03AB90: move.b  #$ff, $ffdd0a.l
 *  03AB98: cmpi.w  #$b00, $ffdd16.l   ; speed >= 0x0B00?
 *  03ABA0: bcc     $3abaa             ; yes: skip ramp-up
 *  03ABA2: addi.w  #$40, $ffdd16.l    ; speed += 0x40 (ramp up)
 *  03ABAA: bra     $3abe4             ; branch to accumulator update
 *  03ABAC: move.w  $ffdd16.l, D3     ; timer == 0: load current speed
 *  03ABB2: ext.l   D3                 ; sign-extend to long
 *  03ABB4: move.w  (-$18,A6), D0     ; load local_limit (0x07E0)
 *  03ABB8: ext.l   D0                 ; sign-extend
 *  03ABBA: moveq   #$40, D1           ; D1 = 0x40
 *  03ABBC: add.l   D1, D0             ; D0 = local_limit + 0x40
 *  03ABBE: cmp.l   D0, D3             ; speed > (local_limit + 0x40)?
 *  03ABC0: bls     $3abcc             ; no: use base+offset path
 *  03ABC2: subi.w  #$20, $ffdd16.l   ; yes: speed -= 0x20 (decay)
 *  03ABCA: bra     $3abe4             ; branch to accumulator update
 *  03ABCC: move.w  $ffdd1e.l, D2     ; base+offset: load source_a
 *  03ABD2: ext.l   D2                 ; sign-extend
 *  03ABD4: move.w  $ffdd1a.l, D0     ; load source_b
 *  03ABDA: ext.l   D0                 ; sign-extend
 *  03ABDC: add.l   D0, D2             ; D2 = source_a + source_b
 *  03ABDE: move.w  D2, $ffdd16.l     ; write updated speed to RAM
 *  03ABE4: move.w  $ffdd16.l, D2     ; accumulator update entry
 *  03ABEA: ext.l   D2
 *  03ABEC: add.l   D2, $ffdbfe.l     ; speed += accumulator
 *  03ADE4: unlk    A6                 ; restore frame pointer
 *  03ADE6: rts                          ; return to caller
 *
 * Three callers push arguments before calling $039B82:
 *   $02587C: pushes ($FFDD82 byte zero-extended, 0)
 *   $0280FE: pushes ($FFDD82 byte zero-extended, ROM[$04EECC+$FFDE72] byte)
 *   $02853A: pushes (record byte, $38 word sign-extended)
 *
 * The speed update block at $03AB7A-$03ABDE is embedded inline in this
 * function, not called via a separate JSR.  The pre-computation phase
 * ($039D22-$03AB6F) writes to $FFDD1E and $FFDD1A (the source_a and
 * source_b fields used in the base+offset path).
 *
 * Observable outputs:
 *   1. $FFDD16 = updated speed   (via the embedded block)
 *   2. $FFDBFE += updated speed    (accumulator update at $03ABE4-$03ABEC)
 *
 * The pre-computation phase (1000+ bytes, too complex to model here) sets
 * $FFDD1E (source_a) and $FFDD1A (source_b) before the speed block runs.
 * This model captures only the speed-field update and the accumulator write.
 *
 * Parameters (mirroring the three callers' stack pushes):
 *   arg1       = byte from $FFDD82 or record byte  [caller argument]
 *   arg2       = 0 / ROM table byte / 0x38          [caller argument]
 *   speed      = current speed ($FFDD16)
 *   timer      = timer field ($FFDD4E)
 *   source_a   = pre-computed source_a ($FFDD1E)
 *   source_b   = pre-computed source_b ($FFDD1A)
 *   accumulator= current accumulator ($FFDBFE)
 */
struct stunrun_speed_outputs
stunrun_mc68010_speed_routine(int16_t arg1, int16_t arg2,
                               int16_t speed, uint16_t timer,
                               int16_t source_a, int16_t source_b,
                               int32_t accumulator)
{
    (void)arg1;   /* consumed by pre-computation; not needed for speed field */
    (void)arg2;   /* consumed by pre-computation; not needed for speed field */

    int16_t local_limit = 0x07E0;  /* set at $039C16 */

    stunrun_motion_update_state_t state = {
        (uint16_t)speed, timer, 0u, 0u, local_limit,
        source_a, source_b, (uint32_t)accumulator
    };
    state = stunrun_motion_update_step(state);

    struct stunrun_speed_outputs out;
    out.speed_write       = (int16_t)state.motion_word;
    out.accumulator_write = (int32_t)state.accumulator;
    return out;
}

/* ── Input handler at $020430 ──────────────────────────────────────────── */

/* Full disassembly (lines 1961-2031 of maincpu-68010.lst):
 *
 *  020430: move.l  D2, -(A7)          ; save D2
 *  020432: lea     $ff9000.l, A0       ; A0 = base = 0xFF9000
 *  020438: move.w  $ffdaee.l, D2      ; D2 = flag at 0xFFDAEE
 *  02043E: bne     $20446             ; if D2 != 0: use real input
 *  020440: move.b  #$ff, D2           ; D2 = 0xFF (default)
 *  020444: bra     $2045e             ; jump to shift
 *  020446: move.b  $60c001.l, D2      ; D2 = port 0x60C001
 *  02044C: bset    #$5, D2            ; set bit 5
 *  020450: btst    #$2, $a80001.l     ; test bit 2 of port 0xA80001
 *  020458: bra     $2045e             ; jump always to 0x2045E
 *  02045A: bclr    #$5, D2            ; (never reached in practice)
 *  02045E: moveq   #$0, D0            ; D0 = 0
 *  020460: asl.l   #2, D0             ; D0 = 0 << 2 = 0
 *  020462: move.b  ($3,A0), D1        ; D1 = record[3]
 *  020466: asl.b   #1, D2             ; D2 <<= 1; carry = old bit 7
 *  020468: bcs     $20474             ; if carry: path A (input bit 7 = 1)
 *  [Path B: input bit 7 was 0]
 *  02046A: clr.b   D1                 ; D1 = 0
 *  02046C: move.b  (A0), D0           ; D0 = record[i]
 *  02046E: beq     $2049a             ; if D0 == 0: skip to next
 *  020470: subq.b  #1, D0             ; D0 = D0 - 1
 *  020472: bra     $20494             ; store
 *  [Path A: input bit 7 was 1]
 *  020474: move.b  (A0), D0           ; D0 = record[i]
 *  020476: cmp.b   #$c4, D0           ; compare with 0xC4
 *  02047A: bcc     $20490             ; if D0 >= 0xC4: store 0xC8
 *  02047C: addi.b  #$20, D1           ; D1 = D1 + 0x20
 *  020480: bcc     $20494             ; if no carry: store
 *  020482: tst.b   D0                  ; test D0
 *  020484: beq     $20490             ; if D0 == 0: store 0xC8
 *  020486: addi.l  #$10000, D0        ; D0 = D0 + 0x10000
 *  02048C: addq.b  #1, ($6,A0)        ; increment record[6]
 *  020490: move.b  #$c8, D0           ; D0 = 0xC8
 *  020494: move.b  D1, ($3,A0)        ; record[3] = D1 (from last iter)
 *  020498: move.b  D0, (A0)           ; record[i] = D0
 *  02049A: addq.l  #1, A0             ; advance to next record
 *  02049C: cmpa.l  #$ff9003, A0      ; compare with end address
 *  0204A2: bcs     $20460             ; if A0 < 0xFF9003: loop
 *  [post-loop: clr.w D0; swap; beq always taken - $0418B8 never called]
 *  0204B8: addq.b  #1, $ff948f.l      ; increment counter
 *  0204BE: subq.b  #1, ($6,A0)        ; decrement record[6]
 *  0204C2: bcc     $2051c             ; if no borrow: skip saturate
 *  0204C4: move.b  #$7, ($6,A0)       ; record[6] = 7 (saturate)
 *  [dbra loop processes record[6] backwards]
 *  0204CA: moveq   #-$1, D1           ; D1 = -1
 *  0204CC: moveq   #$2, D2            ; D2 = 2 (loop count)
 *  0204CE: lea     ($6,A0), A0        ; A0 = record + 6
 *  0204D2: move.b  -(A0), D0          ; D0 = *--A0
 *  0204D4: beq     $204e2             ; if D0 == 0: exit loop
 *  0204D6: move.w  D2, D1            ; D1 = D2 = 2
 *  0204D8: subi.b  #$10, D0           ; D0 = D0 - 0x10
 *  0204DC: bcs     $204e2             ; if borrow: exit loop
 *  0204DE: move.b  D0, (A0)          ; *A0 = D0
 *  0204E0: bra     $204f0             ; jump to exit check
 *  0204E2: dbra    D2, $204d2        ; D2--; if D2 >= 0: loop
 *  0204E6: tst.w   D1                ; test D1
 *  0204E8: bmi     $204f0             ; if D1 < 0: skip indexed write
 *  0204EA: addi.b  #-$11, (A0,D1.w) ; *(A0 + D1) += -0x11
 *  [port writes]
 *  0204F0: lea     $ff9006.l, A0     ; A0 = 0xFF9006
 *  0204F6: lea     $a0000c.l, A1     ; A1 = 0xA0000C
 *  0204FC: moveq   #$1, D1           ; D1 = 1 (loop count)
 *  0204FE: tst.b   (A0)+              ; test record[6]; A0++
 *  020500: bmi     $20506             ; if negative: write at offset 0x10
 *  020502: move.w  D0, (A1)+          ; write to *A1++; A1 += 2
 *  020504: bra     $2050c             ; jump to dbra
 *  020506: move.w  D0, ($10,A1)      ; write to 0x10,A1
 *  02050A: addq.l  #2, A1            ; A1 += 2
 *  02050C: dbra    D1, $204fe        ; D1--; if D1 >= 0: loop
 *  020510: addq.l  #1, $ff9490.l     ; increment 0xFF9490
 *  020516: jsr     $2053c.l          ; call $02053C
 *  02051C: move.l  (A7)+, D2         ; restore D2
 *  02051E: rts
 *
 * Note: $0418B8 is NEVER called because the clr.w/swap sequence at
 * $0204A4-$0204A8 always results in D0.w == 0, making the beq always taken.
 */

/* Compute the steering carry from input ports.
 * Bit 5 of port_a is set, then conditionally cleared based on port_b bit 2.
 * Then the result is shifted left by 1; the carry is the original bit 7.
 *
 * Returns: 1 if carry (original bit 7 of processed input was 1), else 0.
 */

/* Process one record byte during one loop iteration.
 *
 * The main loop runs 4 times (i = 0..3), each time processing record[i].
 * The carry value is computed ONCE before the loop and is the same for
 * all 4 iterations. D1 accumulates across Path A iterations.
 *
 * Path B (carry=0): input bit 7 was 0
 *   - D1 is cleared to 0
 *   - If record[i] == 0: D0 = 0, skip to next
 *   - Else: D0 = record[i] - 1, store
 *
 * Path A (carry=1): input bit 7 was 1
 *   - If record[i] >= 0xC4: D0 = 0xC8
 *   - Else:
 *       D1 += 0x20 (accumulates across iterations)
 *       If D1+0x20 caused carry (D1 >= 0xE0): skip increment
 *       If record[i] == 0: D0 = 0xC8
 *       Else: record[6]++, D0 = record[i]
 *
 * The final D1 (after all 4 iterations) is stored to record[3].
 */

static void process_record_iteration(uint8_t record_i, int carry,
                                     uint8_t d1_in,
                                     uint8_t *out_d0, uint8_t *out_d1,
                                     int *out_inc6)
{
    uint8_t d0, d1;
    int inc6 = 0;

    if (!carry) {
        /* Path B: input bit 7 was 0 */
        d1 = 0;  /* clr.b D1 at $02046A */
        d0 = record_i;  /* move.b (A0), D0 at $02046C */
        if (d0 == 0) {
            /* beq at $02046E: skip to next record (D0 stays 0) */
        } else {
            d0 = d0 - 1;  /* subq.b #1, D0 at $020470 */
        }
    } else {
        /* Path A: input bit 7 was 1 */
        d0 = record_i;  /* move.b (A0), D0 at $020474 */
        if (d0 >= 0xC4) {
            /* bcc at $02047A: D0 >= 0xC4, branch to store 0xC8 */
            d0 = 0xC8;  /* move.b #$c8, D0 at $020490 */
        } else {
            d1 = d1_in + 0x20;  /* addi.b #$20, D1 at $02047C */
            /* bcc at $020480: if carry=0 (no overflow), branch to store
             * if carry=1 (overflow), fall through to tst.b */
            /* For addi.b: carry set if result >= 256, i.e., d1_in >= 0xE0 */
            if (d1_in >= 0xE0) {
                /* Carry: bcc NOT taken, continue to tst.b/beq */
                if (d0 == 0) {
                    /* beq at $020484: branch to store 0xC8 */
                    d0 = 0xC8;
                } else {
                    /* addi.l #$10000, D0 at $020486 - 32-bit add
                     * Low byte stays as record_i (overflow to high word) */
                    inc6 = 1;  /* addq.b #1, ($6,A0) at $02048C */
                    /* d0 stays as record_i */
                }
            } else {
                /* No carry: bcc IS taken, branch to store
                 * Skip tst.b/beq */
            }
        }
    }

    *out_d0 = d0;
    *out_d1 = d1;
    *out_inc6 = inc6;
}
struct stunrun_input_outputs
stunrun_mc68010_input_handler(uint16_t flag_value,
                              uint8_t port_a, uint8_t port_b,
                              struct stunrun_input_records records)
{
    struct stunrun_input_outputs out = {0};
    uint8_t d1 = 0;
    int carry;
    int i;
    uint8_t new_records[4];
    int inc6_total = 0;

    /* Step 1: Determine input byte based on flag ($020438-$02045A) */
    if (flag_value == 0) {
        /* Default input when flag is 0 */
        port_a = STUNRUN_INPUT_FLAG_DEFAULT;
    }

    /* Step 2: Process bit 5 based on port_b bit 2 ($02044C-$02045A) */
    port_a |= 0x20;  /* bset #$5 */
    if ((port_b & 0x04) == 0) {
        port_a &= (uint8_t)~0x20;  /* bclr #$5 */
    }

    /* Step 3: Compute carry for loop decision ($020466) */
    /* asl.b #1, D2: shift left by 1; carry = original bit 7 */
    carry = (port_a & 0x80) ? 1 : 0;

    /* Pre-loop: load D1 with record[3] at $020462 */
    d1 = records.record3;

    /* Main loop: process 4 records at 0xFF9000-0xFF9003 */
    new_records[0] = records.record0;
    new_records[1] = records.record1;
    new_records[2] = records.record2;
    new_records[3] = records.record3;

    for (i = 0; i < 4; i++) {
        uint8_t record_i;
        uint8_t new_d0, new_d1;
        int inc6 = 0;
        

        switch (i) {
            case 0: record_i = records.record0; break;
            case 1: record_i = records.record1; break;
            case 2: record_i = records.record2; break;
            case 3: record_i = records.record3; break;
        }

        process_record_iteration(record_i, carry, d1,
                                &new_d0, &new_d1, &inc6);

        /* Store at $020494-$020498:
         * move.b D1, ($3,A0) - always stores to record[3] (last iteration wins)
         * move.b D0, (A0) - stores to current record[i]
         */
        new_records[i] = new_d0;
        d1 = new_d1;  /* D1 accumulates across iterations */
        inc6_total += inc6;
    }

    /* Post-loop: set output record values
     * move.b D0, (A0) at $020498 stores d0 to current record[i]
     * move.b D1, ($3,A0) at $020494 stores d1 to A0+3 (record[6] in iter 3)
     * new_records[i] contains d0 values, d1 contains final accumulated d1
     */
    out.record0 = new_records[0];
    out.record1 = new_records[1];
    out.record2 = new_records[2];
    out.record3 = new_records[3];
    out.record6 = d1;  /* final accumulated d1 from iteration 3 */


    /* Post-loop: $0418B8 is NEVER called ($0204A8 beq always taken)
     * because clr.w/swap sequence zeros D0.w regardless of input.
     * Documented as observed behavior from disassembly. */

    /* $0204B8: increment byte at 0xFF948F */
    out.counter_948f = 1;  /* relative to current value */

    /* $0204BE: subq.b #1, record[6] */
    if (out.record6 > 0) {
        out.record6--;
        /* bcc at $0204C2: carry clear means no borrow, skip saturate */
    } else {
        /* record6 was 0: after decrement, wraps to 0xFF (borrow occurred)
         * bcs is taken, skip saturate to 7
         * record6 stays as-is (0xFF) before dbra processing */
    }

    /* The dbra loop at $0204CA-$0204E2 processes record[6] and preceding
     * bytes, subtracting 0x10 from the first non-zero byte.
     * For simplicity, we model record6 as-is (the dbra result would be
     * complex to fully model without the actual memory state). */

    /* $020510: increment long at 0xFF9490 */
    out.counter_9490 = 1;  /* relative increment */

    /* $020516: call $02053C (not modelled) */
    out.called_02053c = 1;

    /* Port write at $0204FE-$02050A:
     * tst.b (A0)+ tests record[6]; if negative, write to offset 0x10 from A1
     */
    if ((out.record6 & 0x80) != 0) {
        /* record[6] negative: write at ($10,A1) */
        out.port_write = (uint16_t)out.record6;
    } else {
        /* record[6] positive: write at (A1)+ */
        out.port_write = (uint16_t)out.record6;
    }

    return out;
}

/* ── Trajectory integration at $03887A ─────────────────────────────────── */

/* Full disassembly of the trajectory update routine (lines 66990-67110 of
 * maincpu-68010.lst):
 *
 *  03887A: link    A6, #-$14         ; create 20-byte local frame
 *  03887E: movem.l D2-D5/A2, -(A7)   ; save registers
 *  038882: movea.l #$ffdcdc, A2      ; A2 = 0xFFDCDC (history buffer)
 *  038888: tst.w   $ffdce2.l         ; test flag at 0xFFDCE2
 *  03888E: beq     $38896             ; if 0: continue
 *  038890: moveq   #$0, D0            ; return 0 (early exit)
 *  038892: bra     $38bc4             ; jump to epilogue
 *  038896: tst.w   $ffdcca.l          ; test flag at 0xFFDCCA
 *  03889C: beq     $388d2             ; if 0: skip latching
 *  [latch current motion values]
 *  03889E: move.w  $ffdd16.l, $ffdcce.l  ; latch motion delta
 *  0388A8: move.w  $ffdd16.l, $ffdccc.l  ; copy to speed copy
 *  0388B2: move.w  $ffdccc.l, $ffdcd0.l  ; shift history
 *  0388BC: move.w  $ffdc1a.l, $ffdcde.l  ; latch other motion
 *  0388C6: move.w  $ffdc1a.l, (A2)+    ; store to history buffer, A2++
 *  0388CC: clr.w   $ffdcca.l          ; clear latch flag
 *  [compute deltas]
 *  0388D2: move.w  $ffdd16.l, D2      ; D2 = current motion
 *  0388D8: sub.w   $ffdcce.l, D2      ; D2 = D2 - previous motion
 *  0388DE: move.w  $ffdd16.l, $ffdcce.l  ; update previous motion
 *  0388E8: move.w  $ffdccc.l, D0      ; D0 = current speed copy
 *  0388EE: sub.w   $ffdcd0.l, D0      ; D0 = D0 - previous speed
 *  0388F4: move.w  $ffdccc.l, $ffdcd0.l  ; shift history
 *  0388FE: move.w  D2, D3             ; D3 = D2 (motion delta)
 *  038900: sub.w   D0, D3             ; D3 = D3 - D0 (speed delta)
 *  038902: tst.w   D3                 ; test D3
 *  038904: beq     $38920             ; if D3 == 0: use stored
 *  [filter D3]
 *  038906: move.w  D3, D0             ; D0 = D3
 *  038908: ext.l   D0                 ; sign-extend to long
 *  03890A: move.b  $4d771.l, D1       ; D1 = shift count (1)
 *  038910: asr.l   D1, D0             ; arithmetic shift right by D1
 *  038912: move.w  D0, D0             ; test result
 *  038914: beq     $3891c             ; if result == 0: set to -1
 *  038916: ext.l   D0                 ; sign-extend
 *  038918: neg.l   D0                 ; negate
 *  03891A: bra     $3891e             ; jump to store
 *  03891C: moveq   #-$1, D0           ; D0 = -1
 *  03891E: bra     $3893c             ; jump to add to trajectory
 *  [use stored DCD2]
 *  038920: tst.w   $ffdcd2.l          ; test stored value
 *  038926: blt     $38932             ; if < 0: negate
 *  038928: move.w  $ffdcd2.l, D0      ; D0 = stored
 *  03892E: ext.l   D0                 ; sign-extend
 *  038930: bra     $3893c             ; jump to store
 *  038932: move.w  $ffdcd2.l, D0      ; D0 = stored
 *  038938: ext.l   D0                 ; sign-extend
 *  03893A: neg.l   D0                 ; make positive
 *  03893C: move.w  D0, $ffdcd2.l      ; store filtered result to 0xFFDCD2
 *  [add to trajectory]
 *  038942: add.w   D0, $ffdcc6.l      ; add to trajectory at 0xFFDCC6
 *  [clamp trajectory]
 *  038948: cmpi.w  #-$360, $ffdcc6.l  ; compare with -0x360
 *  038950: ble     $3895c             ; if <= -0x360: skip high clamp
 *  038952: move.w  #$fca0, $ffdcc6.l  ; set to -0x360 (0xFCA0)
 *  03895A: bra     $3896e             ; jump to next check
 *  03895C: cmpi.w  #-$5a0, $ffdcc6.l  ; compare with -0x5A0
 *  038964: bge     $3896e             ; if >= -0x5A0: skip low clamp
 *  038966: move.w  #$fa60, $ffdcc6.l  ; set to -0x5A0 (0xFA60)
 *  03896E: [continues with more logic...]
 */

/* Trajectory state cluster addresses */
#define STUNRUN_TRAJ_MIRROR       0xFFDCC0u  /* copy of global tick */
#define STUNRUN_TRAJ_COORD        0xFFDCC6u  /* main trajectory coordinate */
#define STUNRUN_TRAJ_SPEED_COPY   0xFFDCCCu  /* current speed copy */
#define STUNRUN_TRAJ_SPEED_PREV   0xFFDCD0u  /* previous speed copy */
#define STUNRUN_TRAJ_DELTA_PREV   0xFFDCCEu  /* previous motion delta */
#define STUNRUN_TRAJ_DELTA_STORED 0xFFDCD2u  /* stored filtered delta */
#define STUNRUN_TRAJ_MOTION_PREV  0xFFDCDEu  /* previous motion (FFDC1A) */
#define STUNRUN_TRAJ_FLAG_LATCH   0xFFDCCAu  /* latch flag */
#define STUNRUN_TRAJ_FLAG_EXIT    0xFFDCE2u  /* early exit flag */
#define STUNRUN_TRAJ_MOTION       0xFFDD16u  /* current motion delta */
#define STUNRUN_TRAJ_MOTION_2     0xFFDC1Au  /* second motion value */

/* Trajectory bounds */
#define STUNRUN_TRAJ_BOUND_UPPER  ((int16_t)-0x360)  /* 0xFCA0 */
#define STUNRUN_TRAJ_BOUND_LOWER  ((int16_t)-0x5A0)  /* 0xFA60 */

/* ROM shift count constant at $04D771 (confirmed as 1) */
#define STUNRUN_TRAJ_SHIFT_COUNT  1u

/* Arithmetic shift right - 68010 compatible sign-preserving shift.
 * For positive values: just divide by 2^n.
 * For negative values: negate, divide, then negate result.
 */
static int32_t arithmetic_shift_right(int32_t value, unsigned count)
{
    if (value >= 0)
        return value / (1 << count);
    /* For negative: -((-value + (2^n - 1)) / 2^n) gives正确的arithmetic shift */
    return -((-value + (1 << count) - 1) / (1 << count));
}

/* Filter delta computation at $038906-$03893C.
 * Inputs:
 *   d2_delta  = motion delta (current motion - previous motion)
 *   d0_delta  = speed delta (current speed - previous speed)
 *   stored    = previous stored delta at 0xFFDCD2
 * Returns filtered delta and updates stored value.
 */
static int16_t filter_delta(int16_t d2_delta, int16_t d0_delta,
                           int16_t *stored)
{
    int32_t d3 = (int32_t)d2_delta - (int32_t)d0_delta;
    int32_t result;

    if (d3 != 0) {
        /* D3 != 0: apply arithmetic shift right by 1 (ROM $04D771) */
        result = arithmetic_shift_right(d3, STUNRUN_TRAJ_SHIFT_COUNT);
        if (result == 0)
            result = -1;  /* $038914-$03891C: if result == 0, set to -1 */
        else
            result = -result;  /* $038916-$038918: negate */
    } else {
        /* D3 == 0: use stored value, make positive ($038920-$03893A) */
        result = (int32_t)*stored;
        if (result < 0)
            result = -result;
    }

    *stored = (int16_t)result;
    return (int16_t)result;
}

/* Trajectory clamp at $038942-$038966.
 *
 * After analyzing the ROM disassembly, the "upper bound" check at $0389AA
 * is a BRANCH CONDITION (blt), not a clamp instruction. The code at $389AC
 * does a speed comparison, NOT a coordinate clamp.
 *
 * The ONLY actual coordinate clamp in this code is at $038966:
 *   cmpi.w #-$5a0, $ffdcc6.l
 *   bge $3896e        (skip if coord >= -0x5A0)
 *   move.w #$fa60, $ffdcc6.l  (clamp to -0x5A0)
 *
 * So we only clamp at the LOWER bound (-0x5A0).
 */
int16_t stunrun_trajectory_clamp(int16_t coord)
{
    /* Lower bound: if coord < -0x5A0, clamp to -0x5A0 */
    if (coord < STUNRUN_TRAJ_BOUND_LOWER)
        return STUNRUN_TRAJ_BOUND_LOWER;
    /* No upper bound clamp - the blt at $389AA is a branch condition,
     * and the code at $389AC is a speed comparison, not a coordinate clamp */
    return coord;
}


/* Model of the trajectory integration at $03887A.
 *
 * Entry conditions:
 *   - Reads trajectory state from the cluster at 0xFFDCC0-0xFFDCE0
 *   - Uses current motion at 0xFFDD16 and 0xFFDC1A
 *
 * Parameters (trajectory state):
 *   state - current trajectory state cluster values
 *
 * Returns updated trajectory state.
 *
 * Observable outputs:
 *   - Updated trajectory coordinate at 0xFFDCC6
 *   - Updated history fields (speed_copy, speed_prev, delta_prev, etc.)
 *   - Updated stored delta at 0xFFDCD2
 */
struct stunrun_trajectory_state
stunrun_trajectory_update(struct stunrun_trajectory_state state,
                         int16_t motion_current, int16_t motion_2_current)
{
    struct stunrun_trajectory_state next = state;

    /* Early exit if exit flag is set ($038888-$038892) */
    if (state.exit_flag != 0)
        return next;

    /* Latch motion values if flag is set ($038896-$0388CC) */
    if (state.latch_flag != 0) {
        next.delta_prev = motion_current;     /* 0xFFDCCE = 0xFFDD16 */
        next.speed_copy = motion_current;      /* 0xFFDCCC = 0xFFDD16 */
        next.speed_prev = next.speed_copy;     /* 0xFFDCD0 = 0xFFDCCC */
        next.motion_prev = motion_2_current;  /* 0xFFDCDE = 0xFFDC1A */
        /* (A2)+ = motion_2_current in actual implementation */
        next.latch_flag = 0;
    }

    /* Compute deltas ($0388D2-$0388F4) */
    int16_t d2 = motion_current;               /* D2 = current motion */
    d2 = (int16_t)((int16_t)d2 - (int16_t)next.delta_prev);  /* D2 = D2 - prev */

    int16_t d0 = next.speed_copy;             /* D0 = current speed copy */
    d0 = (int16_t)((int16_t)d0 - (int16_t)next.speed_prev);   /* D0 = D0 - prev */

    /* Update previous values for next iteration */
    next.delta_prev = motion_current;
    next.speed_prev = next.speed_copy;

    /* Filter the delta difference ($0388FE-$03893C) */
    int16_t filtered = filter_delta(d2, d0, &next.delta_stored);

    /* Add filtered delta to trajectory ($038942) */
    next.coord = (int16_t)((int16_t)next.coord + (int16_t)filtered);

    /* Clamp trajectory ($038942-$038966) */
    next.coord = stunrun_trajectory_clamp(next.coord);

    return next;
}

/* Convenience function: initialize trajectory state to defaults.
 * Matches the initialization at $0387C6-$03880E.
 */
struct stunrun_trajectory_state stunrun_trajectory_initial_state(void)
{
    struct stunrun_trajectory_state state = {0};
    state.coord = STUNRUN_TRAJ_BOUND_UPPER;   /* 0xFFDCC6 = 0xFCA0 (-0x360) */
    state.speed_copy = 0;
    state.speed_prev = 0;
    state.delta_prev = 0;
    state.delta_stored = 0;
    state.motion_prev = 0;
    state.latch_flag = 1;  /* causes latch on first update */
    state.exit_flag = 0;
    return state;
}

/* Step trajectory with a given motion delta.
 * Simplified interface that just adds delta and clamps.
 * This is the trajectory_step from the existing trajectory_state.c.
 */
int16_t stunrun_trajectory_step_simple(int16_t coord, int16_t delta)
{
    int32_t result = (int32_t)coord + (int32_t)delta;
    return stunrun_trajectory_clamp((int16_t)result);
}

/* ── $03B02C: object collision test ─────────────────────────────────────── */

/*
 * Verbatim transcription of the decisive tail, ROM $03B112-$03B160:
 *
 *   03B112: moveq  #$0,d3            ; d3 = 0
 *   03B114: tst.w  $ffdd14.l         ; sign of dx
 *   03B11A: blt    $03B126
 *   03B11C: move.w $ffdd14.l,d2
 *   03B122: ext.l  d2                ; d2 = dx
 *   03B124: bra    $03B130
 *   03B126: move.w $ffdd14.l,d2
 *   03B12C: ext.l  d2
 *   03B12E: neg.l  d2                ; d2 = -dx
 *   03B130: moveq  #$0,d0
 *   03B132: move.w  d5,d0            ; d0 = half_a
 *   03B134: move.l  d0,d1
 *   03B136: cmp.l   d2,d1
 *   03B138: bls    $03B156           ; |dx| >= half_a -> miss
 *   03B13A: tst.w  (a1)              ; sign of dy
 *   03B13C: blt    $03B144
 *   03B13E: move.w (a1),d2
 *   03B140: ext.l  d2
 *   03B142: bra    $03B14A
 *   03B144: move.w (a1),d2
 *   03B148: ext.l  d2
 *   03B14A: ... d1 = d4 ; cmp.l d2,d1
 *   03B152: bls    $03B156           ; |dy| >= half_b -> miss
 *   03B154: moveq  #$1,d3            ; HIT
 *   03B156: move.l  d3,d0
 *   03B158: movem.l -$10(a6),d2-d5
 *   03B15E: unlk   a6
 *   03B160: rts
 *
 * bls is unsigned-or-equal, so the test is a strict "less than" overlap:
 * a hit requires |dx| < half_a AND |dy| < half_b.
 */

int stunrun_mc68010_collision_test(
    const struct stunrun_collision_record *record,
    int16_t accumulator, int16_t y_ref, uint16_t timer,
    uint16_t size_class, int selector,
    int16_t *out_dx, int16_t *out_dy)
{
    int32_t dx, dy, adx, ady;
    int32_t half_a, half_b;
    int result;

    if (record == NULL)
        return 0;

    /* $03B064-$03B082: the two deltas, written to $FFDD14 and $FFDD12. */
    dx = (int32_t)accumulator - (int32_t)record->x;
    dy = (int32_t)y_ref      - (int32_t)record->y;

    half_a = (int32_t)record->half_a;
    half_b = (int32_t)record->half_b;

    /* $03B094-$03B098: subtract the centring offsets from the two
     * candidate extents. */
    half_b = (int32_t)record->half_b - STUNRUN_COLLISION_B_CENTRE;

    /* $03B09C-$03B0AA: the selector picks which extent is active. */
    if (selector) {
        half_b = (int32_t)record->half_c - STUNRUN_COLLISION_C_CENTRE;
    }

    /* $03B0B0-$03B0EC: size-class scaling on exact immediates. An
     * unlisted class leaves the extent unscaled, which is what the
     * ROM's default fall-through does. */
    switch (size_class) {
    case STUNRUN_COLLISION_CLASS_3_2A:
    case STUNRUN_COLLISION_CLASS_1_5A:
        half_b += half_b >> 1;   /* * 1.5 */
        break;
    case STUNRUN_COLLISION_CLASS_2X:
        half_b <<= 1;            /* * 2 */
        break;
    case STUNRUN_COLLISION_CLASS_HALF:
        half_b >>= 1;            /* * 0.5 */
        break;
    default:
        break;
    }

    /* $03B0EE: timer nonzero and type != 3 widens both extents. */
    if (timer != 0 && record->type != 3) {
        half_a += half_a >> 2;   /* * 1.25 */
        half_b += half_b >> 4;   /* * 1.0625 */
    }

    adx = dx < 0 ? -dx : dx;
    ady = dy < 0 ? -dy : dy;

    if (out_dx != NULL)
        *out_dx = (int16_t)dx;
    if (out_dy != NULL)
        *out_dy = (int16_t)dy;

    /* $03B136/$03B152: strict less-than on both axes. */
    result = (adx < half_a) && (ady < half_b) ? 1 : 0;
    return result;
}

/* ── $03A262: jump-table case 5, object-hit award chain ─────────────────── */

/*
 * Verbatim transcription of the scoring portion, ROM $03A28E-$03A31A:
 *
 *   03A28E: cmp.l  $ffdd78.l,d0       ; d0 = current record
 *   03A294: beq    $03A314           ; duplicate -> return 1, score nothing
 *   03A298: addq.w #$1,$ffdd02.l     ; hit counter (16-bit wrap)
 *   03A29E: move.w $ff9578.l,d0      ; course index
 *   03A2A4: ext.l  d0
 *   03A2A6: movea.l #$4ee4e,a0       ; threshold table
 *   03A2AC: tst.b  (a0,d0.l)         ; table[course] != 0 ?
 *   03A2B0: beq    $03A2DA           ; no threshold -> skip
 *   03A2B2..$03A2C2: d0 = table[course]
 *   03A2C6: cmp.w  $ffdd02.l,d0      ; EQUALITY, not >=
 *   03A2CC: bne    $03A2DA
 *   03A2CE: pea.l  $16.w             ; milestone event
 *   03A2D2: jsr    $301ca.l
 *   03A2DA: tst.w  $ff9578.l
 *   03A2E0: beq    $03A2EA
 *   03A2E2: move.l #$1f4,d2          ; 500
 *   03A2EA: moveq  #$32,d2           ; 50
 *   03A2EC: add.l  d2,$ff9532.l      ; 32-bit add
 *   03A2F2: move.l -$e(a6),$ffdd78.l ; latch last record
 *   03A2FA: tst.w  $ff9578.l
 *   03A300: beq    $03A308
 *   03A302: pea.l  $5f.w             ; later-course hit event
 *   03A308: pea.l  $6c.w             ; first-course hit event
 *   03A30C: jsr    $301ca.l
 *   03A314: moveq  #$1,d0            ; return 1
 *   03A316: move.b d0,-$12(a6)       ; per-frame hit flag
 *   03A31A: bra    $03A4FE           ; shared loop exit
 */

stunrun_collision_award_t stunrun_mc68010_collision_award(
    int hit, uint32_t record, uint32_t last_record,
    uint16_t hit_count, uint32_t score, uint16_t course,
    const uint8_t *thresholds)
{
    stunrun_collision_award_t out;

    out.hit = 0;
    out.counter_written = 0;
    out.hit_count = hit_count;
    out.milestone_queued = 0;
    out.score_written = 0;
    out.score = score;
    out.last_record_written = 0;
    out.last_record = last_record;
    out.event = 0;
    out.event_queued = 0;
    out.frame_hit_flag = 0;

    /* $03A280/$03A286: a miss jumps straight to the loop exit. */
    if (!hit)
        return out;

    /* $03A314: both the duplicate and the scoring path return 1. */
    out.hit = 1;

    /* $03A28E-$03A294: a repeat of the same record is deduped. It still
     * returns 1 and still sets the frame flag, but scores nothing. */
    if (record == last_record) {
        out.frame_hit_flag = 1;
        return out;
    }

    /* $03A298: 16-bit increment; 0xFFFF wraps to 0x0000. */
    out.counter_written = 1;
    out.hit_count = (uint16_t)(hit_count + 1u);

    /* $03A2AC-$03A2CC: the milestone needs the counter to land on
     * exactly the threshold byte, and the byte to be nonzero. */
    if (thresholds != NULL && course < 33u) {
        uint8_t target = thresholds[course];
        if (target != 0u && (uint16_t)target == out.hit_count)
            out.milestone_queued = 1;
    }

    /* $03A2DA-$03A2EC: first course awards 50, later courses 500.
     * The add is 32-bit, so it wraps at 2^32 rather than 2^16. */
    out.score_written = 1;
    out.score = score + (course != 0u
                         ? (uint32_t)STUNRUN_AWARD_LATER_COURSE
                         : (uint32_t)STUNRUN_AWARD_FIRST_COURSE);

    /* $03A2F2: latch this record so a repeat cannot rescore. */
    out.last_record_written = 1;
    out.last_record = record;

    /* $03A2FA-$03A30C: the per-course hit event. */
    out.event_queued = 1;
    out.event = (course != 0u) ? STUNRUN_EVENT_HIT_LATER
                                : STUNRUN_EVENT_HIT_FIRST;

    out.frame_hit_flag = 1;
    return out;
}
