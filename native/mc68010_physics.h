/* MC68010 motion-field update block at ROM $03AB7A-$03ABEC.
 *
 * Source chip: STUN Runner maincpu program ROM, assembled flat by
 * `tools/build-maincpu-image` from the six 136070-210x/300x/5xxx/6xxx pairs.
 * Image used for transcription:
 *   roms/stunrun/stunrun_maincpu.bin
 *   sha256 29c911a21d1abe780419cd3a924080756490f2d008776995dd48c7b2bedc5b6c
 *
 * This routine is embedded inside the larger function that starts at $039B82.
 * Three callers invoke $039B82 with different argument sets:
 *   $02587C: pushes (0, byte from $FFDD82)
 *   $0280FE: pushes (byte from ROM table $04EECC[$FFDE72], $FFDD82)
 *   $02853A: pushes (byte from record, 0x38)
 *
 * The block also adds the sign-extended updated word to $FFDBFE.
 * The value-only adapter below calls the canonical stateful implementation in
 * reproduction/maincpu/motion_update.c. It returns the motion word but not the
 * timer/flag side effects; the reproduction slice exposes all outputs.
 *
 * Provenance: /tmp/opencode/stunrun-listings-current/maincpu-68010.lst,
 * lines 69590-69619.
 */
#ifndef STUNRUN_MC68010_PHYSICS_H
#define STUNRUN_MC68010_PHYSICS_H

#include <stdint.h>
#include "../reproduction/maincpu/motion_update.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Constants from the ROM listing ──────────────────────────────────── */

/* ── Mechanism-level speed update ─────────────────────────────────────── */

/* Convenience adapter over the canonical motion-update slice. The caller's timer,
 * local limit, and source words have not been assigned gameplay semantics;
 * they are mechanism-level inputs only.
 *
 * Parameters:
 *   current    - current motion word (written to 0xFFDD16 by the ROM)
 *   timer      - timer field (0xFFDD4E); nonzero enables ramp-up
 *   local_limit - caller-supplied limit; compared as (local_limit + 0x40)
 *   source_a   - added to source_b when neither ramp applies (0xFFDD1E)
 *   source_b   - added to source_a when neither ramp applies (0xFFDD1A)
 *
 * Returns the updated motion word. The field's gameplay semantics are not
 * assigned by this mechanism-level adapter.
 */
int16_t stunrun_mc68010_speed_update(int16_t current, uint16_t timer,
                                     int16_t local_limit, int16_t source_a,
                                     int16_t source_b);

/* ── The speed/routine wrapper at $039B82 ───────────────────────────────── */

/* Full function: ROM $039B82-$03ADE6 (over 2000 bytes).
 * Three callers:
 *   $02587C: pushes ($FFDD82 byte, 0)
 *   $0280FE: pushes ($FFDD82 byte, ROM[$04EECC+$FFDE72] byte)
 *   $02853A: pushes (record byte, $38)
 *
 * The speed update block at $03AB7A-$03ABDE is embedded inline.
 */

/* Observable outputs from the wrapper function. */
struct stunrun_speed_outputs {
    int16_t  speed_write;        /* new value for $FFDD16 */
    int32_t  accumulator_write;  /* new value for $FFDBFE */
};

/* Model of the speed/routine wrapper at $039B82.
 *
 * Entry conditions (from stack setup by callers):
 *   arg1  = byte from $FFDD82 (caller 1 & 2) or record byte (caller 3)
 *   arg2  = 0 (caller 1), ROM table byte (caller 2), or 0x38 (caller 3)
 *
 * The caller pre-computation is not modelled; the parameters source_a and
 * source_b are the values that the pre-computation writes to RAM before the
 * speed update block runs.
 *
 * local_limit is set to 0x07E0 at $039C16.
 *
 * Returns speed_write (= updated $FFDD16) and accumulator_write (= new $FFDBFE).
 */
struct stunrun_speed_outputs
stunrun_mc68010_speed_routine(int16_t arg1, int16_t arg2,
                               int16_t speed, uint16_t timer,
                               int16_t source_a, int16_t source_b,
                               int32_t accumulator);

/* ── Input handler at $020430 ──────────────────────────────────────────── */

/* MC68010 input handler at ROM $020430.
 *
 * Called from $02FFF0 as part of main state dispatch (after $023D8C).
 * Reads input ports 0x60C001 and 0xA80001.
 * Updates records at 0xFF9000..0xFF9007.
 *
 * Full disassembly (lines 1961-2031 of maincpu-68010.lst):
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
 *  02046A: clr.b   D1                 ; path B: D1 = 0
 *  02046C: move.b  (A0), D0           ; D0 = record[i]
 *  02046E: beq     $2049a             ; if D0 == 0: skip to next
 *  020470: subq.b  #1, D0             ; D0 = D0 - 1
 *  020472: bra     $20494             ; store
 *  020474: move.b  (A0), D0           ; path A: D0 = record[i]
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
 *  [post-loop: clr.w/swap zeroes D0.w; $0418B8 never called]
 *  0204B8: addq.b  #1, $ff948f.l      ; increment counter
 *  0204BE: subq.b  #1, ($6,A0)        ; decrement record[6]
 *  [dbra loop + port writes + call $02053C]
 *  02051E: rts
 *
 * Note: $0418B8 is NEVER called because the clr.w/swap sequence at
 * $0204A4-$0204A8 always results in D0.w == 0, making the beq always taken.
 *
 * Provenance: /tmp/opencode/stunrun-listings-current/maincpu-68010.lst,
 * lines 1961-2031.
 */

/* Flag at 0xFFDAEE - determines whether to use real input or default */
#define STUNRUN_INPUT_FLAG_ADDR   0xFFDAEEu
#define STUNRUN_INPUT_FLAG_DEFAULT 0xFFu  /* used when flag == 0 */

/* Input record area base */
#define STUNRUN_INPUT_RECORD_BASE 0xFF9000u

/* Observable outputs from the input handler. */
struct stunrun_input_outputs {
    uint8_t  record0;      /* new value for 0xFF9000 */
    uint8_t  record1;      /* new value for 0xFF9001 */
    uint8_t  record2;      /* new value for 0xFF9002 */
    uint8_t  record3;      /* new value for 0xFF9003 */
    uint8_t  record6;      /* new value for 0xFF9006 */
    uint8_t  counter_948f; /* new value for 0xFF948F (incremented) */
    uint32_t counter_9490; /* new value for 0xFF9490 (incremented) */
    uint16_t port_write;   /* value written to 0xA0000C */
    int      called_02053c; /* non-zero if $02053C was called */
};

/* Input records state - what the function reads from 0xFF9000-0xFF9006 */
struct stunrun_input_records {
    uint8_t record0;   /* value at 0xFF9000 */
    uint8_t record1;   /* value at 0xFF9001 */
    uint8_t record2;   /* value at 0xFF9002 */
    uint8_t record3;   /* value at 0xFF9003 */
    uint8_t record6;   /* value at 0xFF9006 */
};

/* Model of the input handler at $020430.
 *
 * Entry conditions (called from $02FFF0):
 *   A0 = 0xFF9000 (input record base)
 *   0xFFDAEE contains the flag value
 *
 * Parameters:
 *   flag_value  - current value at 0xFFDAEE (0 = use default 0xFF)
 *   port_a      - value read from 0x60C001 (main input)
 *   port_b      - value read from 0xA80001 (only bit 2 tested)
 *   records     - current values at 0xFF9000-0xFF9006
 *
 * The subroutines $0418B8 and $02053C are not modelled; the call to
 * $02053C is indicated in the output struct.
 *
 * Returns observable outputs from the input handler.
 */
struct stunrun_input_outputs
stunrun_mc68010_input_handler(uint16_t flag_value,
                               uint8_t port_a, uint8_t port_b,
                               struct stunrun_input_records records);

#ifdef __cplusplus
}
#endif

#endif /* STUNRUN_MC68010_PHYSICS_H */

/* ── Trajectory integration at $03887A ─────────────────────────────────── */

/* Trajectory state cluster addresses (literal RAM addresses, not semantics) */
#define STUNRUN_TRAJ_MIRROR       0xFFDCC0u
#define STUNRUN_TRAJ_COORD        0xFFDCC6u
#define STUNRUN_TRAJ_SPEED_COPY   0xFFDCCCu
#define STUNRUN_TRAJ_SPEED_PREV   0xFFDCD0u
#define STUNRUN_TRAJ_DELTA_PREV   0xFFDCCEu
#define STUNRUN_TRAJ_DELTA_STORED 0xFFDCD2u
#define STUNRUN_TRAJ_MOTION_PREV  0xFFDCDEu
#define STUNRUN_TRAJ_FLAG_LATCH   0xFFDCCAu
#define STUNRUN_TRAJ_FLAG_EXIT    0xFFDCE2u
#define STUNRUN_TRAJ_MOTION       0xFFDD16u
#define STUNRUN_TRAJ_MOTION_2     0xFFDC1Au

/* Trajectory bounds (literal values from ROM listing) */
#define STUNRUN_TRAJ_BOUND_UPPER  ((int16_t)-0x360)  /* 0xFCA0 */
#define STUNRUN_TRAJ_BOUND_LOWER  ((int16_t)-0x5A0)  /* 0xFA60 */

/* Internal trajectory state for the integration step.
 * Represents the cluster at 0xFFDCC0-0xFFDCE0.
 */
struct stunrun_trajectory_state {
    int16_t  coord;        /* trajectory coordinate (0xFFDCC6) */
    int16_t  speed_copy;   /* current speed copy (0xFFDCCC) */
    int16_t  speed_prev;   /* previous speed copy (0xFFDCD0) */
    int16_t  delta_prev;   /* previous motion delta (0xFFDCCE) */
    int16_t  delta_stored; /* stored filtered delta (0xFFDCD2) */
    int16_t  motion_prev;  /* previous motion (0xFFDCDE) */
    uint16_t latch_flag;   /* latch flag (0xFFDCCA) */
    uint16_t exit_flag;    /* exit flag (0xFFDCE2) */
};

/* Clamp trajectory coordinate to bounds [-0x5A0, -0x360].
 * Transcribed from $038948-$038966.
 */
int16_t stunrun_trajectory_clamp(int16_t coord);

/* Model of the trajectory integration at $03887A.
 *
 * Full disassembly (lines 66990-67110 of maincpu-68010.lst):
 * - Early exit if exit_flag (0xFFDCE2) != 0
 * - Latch current motion values if latch_flag (0xFFDCCA) != 0
 * - Compute D2 = current_motion - previous_delta
 * - Compute D0 = current_speed_copy - previous_speed_copy
 * - D3 = D2 - D0
 * - Filter D3 with arithmetic shift right by 1 (ROM $04D771)
 * - Add filtered result to trajectory at 0xFFDCC6
 * - Clamp to [-0x5A0, -0x360]
 *
 * Parameters:
 *   state           - current trajectory state cluster values
 *   motion_current  - current motion delta (0xFFDD16)
 *   motion_2_current - current secondary motion (0xFFDC1A)
 *
 * Returns updated trajectory state with all fields.
 */
struct stunrun_trajectory_state
stunrun_trajectory_update(struct stunrun_trajectory_state state,
                         int16_t motion_current, int16_t motion_2_current);

/* Initialize trajectory state to default values.
 * Matches initialization at $0387C6-$03880E:
 * - coord = -0x360 (0xFCA0)
 * - All history fields cleared
 * - latch_flag = 1 (triggers latch on first update)
 * - exit_flag = 0
 */
struct stunrun_trajectory_state stunrun_trajectory_initial_state(void);

/* Simplified trajectory step: add delta and clamp.
 * This is the mechanism-level trajectory integration that the
 * game_loop.c scaffolding uses.
 */
int16_t stunrun_trajectory_step_simple(int16_t coord, int16_t delta);

/* ── Object collision test at $03B02C ───────────────────────────────────── */

/* RAM landmarks used by the collision helper and its caller.
 * These are addresses in the 32 KiB work RAM at $FF8000. */
#define STUNRUN_COLLISION_ACCUM      0xFFDBFEu  /* LONG, d1 base */
#define STUNRUN_COLLISION_REC_X      0xFFDD14u  /* WORD, |dx| out */
#define STUNRUN_COLLISION_REC_Y      0xFFDD12u  /* WORD, |dy| out */
#define STUNRUN_COLLISION_Y_REF      0xFFDC1Au  /* WORD, dy base   */
#define STUNRUN_COLLISION_TIMER      0xFFDD4Eu  /* WORD, size adj  */

/* Record half-extent fields (signed 16-bit words). */
#define STUNRUN_COLLISION_HALF_A     0x0Au     /* record +$0A */
#define STUNRUN_COLLISION_HALF_B     0x0Cu     /* record +$0C */
#define STUNRUN_COLLISION_HALF_C     0x0Eu     /* record +$0E */
#define STUNRUN_COLLISION_REC_XPOS   0x02u     /* record +$02 */
#define STUNRUN_COLLISION_REC_YPOS   0x06u     /* record +$06 */
#define STUNRUN_COLLISION_REC_TYPE   0x10u     /* record +$10 */

/* Centring offsets subtracted from the half-extents at $03B094/$03B098. */
#define STUNRUN_COLLISION_B_CENTRE   0x400     /* $400, from +$0C */
#define STUNRUN_COLLISION_C_CENTRE   0xC0      /* $C0,  from +$0E */

/* Collision-size class constants dispatched at $03B0B0 on the owner's
 * record +$0A. These are exact immediates, not a range. */
#define STUNRUN_COLLISION_CLASS_1_5A 0x4B0     /* half * 1.5      */
#define STUNRUN_COLLISION_CLASS_2X   0x320     /* half << 1        */
#define STUNRUN_COLLISION_CLASS_3_2A 0x3E8     /* half + (half>>1) */
#define STUNRUN_COLLISION_CLASS_HALF 0xC80     /* half >> 1        */

/* The object/collision state consumed by the $03B02C helper. */
struct stunrun_collision_record {
    int16_t x;        /* record +$02, signed */
    int16_t y;        /* record +$06, signed */
    int16_t half_a;   /* record +$0A        */
    int16_t half_b;   /* record +$0C        */
    int16_t half_c;   /* record +$0E        */
    uint16_t type;    /* record +$10        */
};

/* Strict two-axis AABB overlap test, transcribed from ROM $03B02C.
 *
 * Returns 1 only when |dx| < half_a AND |dy| < half_b, where the deltas
 * are computed against the live accumulator/motion landmarks:
 *   dx = accumulator - record.x
 *   dy = y_ref      - record.y
 * and half_b is selected from record +$0C or +$0E by `selector`, then
 * scaled by the collision-size class `size_class` from the owner record.
 * When the timer landmark is nonzero and the record type is not 3, both
 * extents are widened: half_a += half_a>>2, half_b += half_b>>4.
 *
 * `out_dx` and `out_dy` receive the signed deltas, which the ROM writes
 * to 0xFFDD14 and 0xFFDD12 as a side effect of the test.
 */
int stunrun_mc68010_collision_test(
    const struct stunrun_collision_record *record,
    int16_t accumulator, int16_t y_ref, uint16_t timer,
    uint16_t size_class, int selector,
    int16_t *out_dx, int16_t *out_dy);

/* ── Object-hit award chain at $03A262 (jump-table case 5) ──────────────── */

/* $03A298 is the middle of jump-table case 5 inside the $039B82 routine,
 * not a routine entry. Case 5 is entered at $03A262; $03A298 is reachable
 * only as a fall-through from $03A294, i.e. after a non-duplicate hit.
 *
 * RAM landmarks for the case-5 chain. */
#define STUNRUN_AWARD_HIT_COUNT   0xFFDD02u  /* WORD, addq.w #$1        */
#define STUNRUN_AWARD_COURSE      0xFF9578u  /* WORD, course index      */
#define STUNRUN_AWARD_SCORE       0xFF9532u  /* LONG, add.l d2          */
#define STUNRUN_AWARD_LAST_RECORD 0xFFDD78u  /* LONG, dedupe latch      */
#define STUNRUN_AWARD_THRESHOLDS  0x4EE4Eu   /* ROM byte table, 33 bytes */

/* Award immediates at $03A2E2 / $03A2EA. $FF9578 is the 22-value course
 * counter, so this is a first-course-versus-later tier, not a per-track
 * lookup. */
#define STUNRUN_AWARD_FIRST_COURSE 0x0032u   /* 50  */
#define STUNRUN_AWARD_LATER_COURSE 0x01F4u   /* 500 */

/* Event bytes pushed to the $00301CA staging queue. */
#define STUNRUN_EVENT_MILESTONE       0x16u
#define STUNRUN_EVENT_HIT_LATER       0x5Fu
#define STUNRUN_EVENT_HIT_FIRST       0x6Cu

/* Observable effects of one pass through case 5.
 *
 * Every field is a "was this written" pair so a caller can distinguish a
 * miss, a duplicate (dedupe), and a scoring hit without inspecting
 * internals. */
typedef struct stunrun_collision_award {
    int      hit;                 /* d0 left in the register            */
    int      counter_written;     /* 0 when $03A298 was not reached     */
    uint16_t hit_count;           /* new (uint16_t)$FFDD02               */
    int      milestone_queued;    /* 0x16 pushed to $00301CA            */
    int      score_written;       /* 0 when $03A2EC was not reached     */
    uint32_t score;               /* new $FF9532                         */
    int      last_record_written; /* 0 when $03A2F2 was not reached     */
    uint32_t last_record;         /* new $FFDD78                         */
    uint16_t event;               /* 0x5F or 0x6C, 0 when none queued   */
    int      event_queued;
    uint8_t  frame_hit_flag;      /* -$12(a6), set to 1 on a hit         */
} stunrun_collision_award_t;

/* Run one pass of the $03A262-$03A31E case-5 chain.
 *
 * `hit` is the value $03B02C left in D0. `record` is the current record
 * pointer compared against the $FFDD78 latch at $03A28E; a duplicate hit
 * returns 1 but writes nothing except the frame flag. `thresholds` is the
 * 33-byte ROM table at $4EE4E indexed by course; pass NULL to model a
 * table of zeros, which suppresses the milestone event.
 */
stunrun_collision_award_t stunrun_mc68010_collision_award(
    int hit, uint32_t record, uint32_t last_record,
    uint16_t hit_count, uint32_t score, uint16_t course,
    const uint8_t *thresholds);
