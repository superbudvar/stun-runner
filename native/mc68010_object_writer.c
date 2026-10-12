/* MC68010 object record update routines.  See mc68010_object_writer.h and
 * docs/mc68010-boot-findings.md for provenance.
 *
 * ROM addresses:
 *   $03CF40  object_record_caller  - main entry, calls $03D120 and $03D1A0
 *   $03D120  object_update_clamp   - clamps byte fields at offsets +0, +1, +2
 *   $03D1A0  object_update_interp - advances interpolation fields at +5, +6, +B, +C
 *
 * Disassembly of $03CF40 (Ghidra listing, file offset 0x3CF40):
 *
 *   03CF40: LINK A6,#0               ; Save old A6, set A6 = A7
 *   03CF44: MOVEM.L D2,-(A7)        ; Save D2 only
 *   03CF48: MOVE.W ($A,A6),D2        ; D2 = record_index from caller's arg
 *   03CF4C: MOVE.W D2,D1             ; D1 = record_index
 *   03CF4E: MOVEA.L #$FFFF8000,A0    ; A0 = input/state area base
 *   03CF54: MOVEQ #$0,D0             ; D0 = 0
 *   03CF56: MOVE.B (A0,D1.W),D0      ; D0 = input_byte at $FFFF8000+index (DISCARDED)
 *   03CF5A: MOVE.L D0,-(A7)          ; Push input_byte (param 1, discarded by callee)
 *   03CF5C: MOVE.W D2,D0             ; D0 = record_index (word)
 *   03CF5E: ASL.W #4,D0              ; D0 = index * 16  ← STRIDE 16 (was 4, WRONG)
 *   03CF62: MOVEA.L #$FFDD86,A0     ; A0 = record base
 *   03CF66: PEA (A0,D0.W)            ; Push record ptr = base + index*16 (param 2)
 *   03CF6A: JSR $03D120              ; Call updater: clamps +0,+1,+2
 *   03CF70: MOVE.W D2,D0             ; Recompute: D0 = record_index
 *   03CF72: ASL.W #4,D0              ; D0 = index * 16
 *   03CF76: MOVEA.L #$FFDD86,A0     ; A0 = record base
 *   03CF7A: PEA (A0,D0.W)            ; Push record ptr (param 2, again)
 *   03CF7E: JSR $03D1A0             ; Call updater: advances +5/+6/+B/+C, writes +2
 *   03CF84: MOVEM.L (A7)+,D2        ; Restore D2
 *   03CF88: UNLK A6                  ; Restore old A6
 *   03CF8A: RTS                      ; Return
 *
 * Address computation: record_ptr = $FFDD86 + (index * 16)
 * The index is scaled by 16 (ASL.W #4), NOT by 4 (ASL.W #2).
 * This was previously documented as stride 4 — that was WRONG.
 *
 * The input_byte at $FFFF8000+index is read but discarded by the callee.
 */

#include "mc68010_object_writer.h"

/* Offset from RAM base ($FF8000) to the record base ($FFDD86).
 * 0xFFDD86 - 0xFF8000 = 0x5D86 */
#define RECORD_OFFSET_FROM_RAM_BASE 0x5D86u

uint8_t *
stunrun_object_record_ptr(uint16_t record_index,
                          uint8_t *ram,
                          size_t ram_size)
{
    uint32_t ptr_offset;

    if (ram == NULL || ram_size < 0x8000u) {
        return NULL;
    }

    /* record_ptr = $FFDD86 + (record_index * 16)
     * = ($FF8000 + 0x5D86) + (index * 16)
     * = RAM_BASE + RECORD_OFFSET_FROM_RAM_BASE + index * 16 */
    ptr_offset = RECORD_OFFSET_FROM_RAM_BASE +
                 ((uint32_t)record_index * STUNRUN_OBJECT_RECORD_STRIDE);

    /* Validate the pointer stays within the 32KB RAM window.
     * ptr_offset must be < ram_size (0x8000). */
    if (ptr_offset >= ram_size) {
        return NULL;
    }

    return ram + ptr_offset;
}

/* Object record caller ($03CF40) */

int
stunrun_mc68010_object_record_caller(
    uint16_t record_index,
    uint8_t input_byte,
    uint8_t *ram,
    size_t ram_size,
    stunrun_object_update_fn updater_03D120,
    stunrun_object_update_fn updater_03D1A0)
{
    uint8_t *record;
    (void)input_byte;  /* Read from $FFFF8000 but NOT used in address calc */

    /* Bail if no callbacks or no RAM */
    if (updater_03D120 == NULL || updater_03D1A0 == NULL) {
        return -1;
    }
    if (ram == NULL || ram_size < 0x8000u) {
        return -1;
    }

    /* Compute the record pointer: $FFDD86 + (index * 16) */
    record = stunrun_object_record_ptr(record_index, ram, ram_size);
    if (record == NULL) {
        return -1;
    }

    /* Call $03D120: clamps byte fields at offsets +0, +1, +2 */
    updater_03D120(record);

    /* Call $03D1A0: advances interpolation/counter fields at +5/+6/+B/+C,
     * writes smoothed value back to +2 */
    updater_03D1A0(record);

    return 0;
}

/* Object update clamp ($03D120) */

void __attribute__((used))
stunrun_mc68010_object_update_record(uint8_t *record)
{
    uint8_t v0;   /* D0: original record[0] (also loaded into D1) */
    uint8_t v1;   /* D3: record[1] (bound A) */
    uint8_t v2;   /* D4: record[2] (bound B) */
    uint8_t lower;
    uint8_t upper;
    uint8_t clamped;

    /* Load the three record bytes. */
    v0 = record[0];   /* MOVE.B (A0),D1 - original value */
    v1 = record[1];   /* MOVE.B $1(A0),D3 - bound A */
    v2 = record[2];   /* MOVE.B $2(A0),D4 - bound B */

    /* Compute clamp bounds: range is always [min(v1,v2), max(v1,v2)] */
    if (v1 <= v2) {
        lower = v1;
        upper = v2;
    } else {
        lower = v2;
        upper = v1;
    }

    /* Clamp v0 to the range [lower, upper] */
    if (v0 < lower) {
        clamped = lower;
    } else if (v0 > upper) {
        clamped = upper;
    } else {
        clamped = v0;   /* in range: pass through unchanged */
    }

    /* Write back: record[0] = clamped, record[1] = original v0,
     *             record[2] = clamped
     *
     * This matches the ROM's:
     *   MOVE.B D1,(A0)       -> record[0] = D1 = upper/saturation
     *   MOVE.B D3,$1(A0)     -> record[1] = D3 = original v0
     *   MOVE.B D4,$2(A0)     -> record[2] = D4 = final clamped value
     */
    record[0] = clamped;
    record[1] = v0;
    record[2] = clamped;
}

/* Object update interpolation ($03D1A0)
 *
 * Full disassembly from Ghidra listing (file offset 0x3D1A0):
 *
 * 03D1A0: link    A6, #$0
 * 03D1A4: movea.l ($8,A6), A0         ; A0 = record pointer from stack
 * 03D1A8: move.b  ($4,A0), D0         ; D0 = record[4]
 * 03D1AC: cmp.b   ($2,A0), D0         ; compare record[4] vs record[2]
 * 03D1B0: bcc     $3d1f4               ; if record[4] >= record[2] (C=0): inactive
 * 03D1B2: addq.b  #1, ($5,A0)         ; counter++
 * 03D1B6: move.b  ($2,A0), D0         ; D0 = record[2]
 * 03D1BA: cmp.b   ($6,A0), D0         ; compare record[2] vs record[6]
 * 03D1BE: bcc     $3d1c6               ; if record[2] >= record[6] (C=0): skip update
 * 03D1C0: move.b  ($2,A0), ($6,A0)   ; record[6] = record[2] (update saved target)
 * 03D1C6: cmpi.b  #$6, ($5,A0)       ; counter >= 6?
 * 03D1CC: bcs     $3d1ec               ; if counter < 6: skip interpolation
 * 03D1CE: moveq   #$0, D0             ; D0 = 0
 * 03D1D0: move.b  ($6,A0), D0         ; D0 = record[6] (saved target)
 * 03D1D4: moveq   #$0, D1             ; D1 = 0
 * 03D1D6: move.b  ($4,A0), D1         ; D1 = record[4] (current value)
 * 03D1DA: sub.l   D1, D0              ; D0 = record[6] - record[4]
 * 03D1DC: lsr.l   #1, D0               ; D0 = (record[6] - record[4]) / 2
 * 03D1DE: add.b   D0, ($4,A0)         ; record[4] += step
 * 03D1E2: move.b  #$ff, ($6,A0)       ; record[6] = 0xFF (inactive marker)
 * 03D1E8: clr.b   ($5,A0)             ; counter = 0
 * 03D1EC: move.b  ($4,A0), ($2,A0)   ; record[2] = record[4] (write back)
 * 03D1F2: bra     $3d1fe               ; → Track 2
 * 03D1F4: move.b  #$ff, ($6,A0)       ; record[6] = 0xFF (inactive)
 * 03D1FA: clr.b   ($5,A0)             ; counter = 0
 * 03D1FE: move.b  ($a,A0), D0         ; D0 = record[10]
 * 03D202: cmp.b   ($2,A0), D0         ; compare record[10] vs record[2]
 * 03D206: bls     $3d248               ; if record[10] <= record[2] (C=1 or Z=1): skip
 * 03D208: addq.b  #1, ($b,A0)         ; counter++
 * 03D20C: move.b  ($2,A0), D0         ; D0 = record[2]
 * 03D210: cmp.b   ($c,A0), D0         ; compare record[2] vs record[12]
 * 03D214: bls     $3d21c               ; if record[2] <= record[12] (C=1 or Z=1): skip
 * 03D216: move.b  ($2,A0), ($c,A0)   ; record[12] = record[2] (update saved target)
 * 03D21C: cmpi.b  #$6, ($b,A0)       ; counter >= 6?
 * 03D222: bcs     $3d240               ; if counter < 6: skip interpolation
 * 03D224: moveq   #$0, D0             ; D0 = 0
 * 03D226: move.b  ($a,A0), D0         ; D0 = record[10] (current)
 * 03D22A: moveq   #$0, D1             ; D1 = 0
 * 03D22C: move.b  ($c,A0), D1         ; D1 = record[12] (saved target)
 * 03D230: sub.l   D1, D0              ; D0 = record[10] - record[12]
 * 03D232: lsr.l   #1, D0              ; D0 = (record[10] - record[12]) / 2
 * 03D234: sub.b   D0, ($a,A0)         ; record[10] -= step
 * 03D238: clr.b   ($c,A0)             ; record[12] = 0 (inactive)
 * 03D23C: clr.b   ($b,A0)             ; counter = 0
 * 03D240: move.b  ($a,A0), ($2,A0)   ; record[2] = record[10] (write back)
 * 03D246: bra     $3d250               ; → epilogue
 * 03D248: clr.b   ($c,A0)             ; record[12] = 0 (inactive, bls branch)
 * 03D24C: clr.b   ($b,A0)             ; counter = 0
 * 03D250: unlk    A6
 * 03D252: rts
 *
 * FIXES vs prior implementation:
 *   - Prior used stride 4 (ASL.W #2); listing shows ASL.W #4 → stride 16
 *   - Prior Track 2 condition: record[0xA] < record[2] (WRONG)
 *     Listing shows: bls branches when record[10] <= record[2]
 *     → Counter increments ONLY when record[10] > record[2]
 *   - Prior Track 2 formula: record[0xA] - record[0xC] (WRONG)
 *     Listing shows: sub.l D1,D0 where D0=record[10], D1=record[12]
 *     → D0 = record[10] - record[12], step = D0/2, record[10] -= step
 *     → Correct formula: record[10] -= (record[10] - record[12]) / 2
 *     → Which is record[10] + (record[12] - record[10]) / 2 (approach saved target)
 */

void __attribute__((used))
stunrun_mc68010_object_advance_interpolation(uint8_t *record)
{
    /* ── Track 1: advance record[4] toward record[2] ─────────────────── */
    /* record[4]: current smoothed value (advances toward record[2])
     * record[5]: frame counter
     * record[6]: record[2] value saved at start of interpolation cycle
     *
     * Condition: bcc branches (inactive) when record[4] >= record[2] (C=0).
     * Counter increments BEFORE the cmpi check each frame.
     * Interpolation runs ONCE when counter reaches 6, then counter resets. */

    /* record[4] >= record[2]? (bcc: branch if C=0, i.e., unsigned >=) */
    if (record[4] >= record[2]) {
        /* record[4] has reached or exceeded target: go inactive */
        record[6] = 0xFF;  /* inactive marker */
        record[5] = 0;
    } else {
        /* record[4] is below target: advance interpolation */
        record[5]++;  /* counter++ */

        /* Read record[6] BEFORE any conditional update (saved target) */
        uint8_t saved_target = record[6];

        /* If record[2] has dropped below the saved target,
         * update the saved target so interpolation moves in the right direction. */
        if (record[2] < saved_target) {
            record[6] = record[2];
        }

        /* Interpolate: advance record[4] by half the remaining distance.
         * ROM: cmpi.b #$6, counter; bcs → branches when counter < 6.
         * So interpolation runs when counter >= 6.
         * Uses the SAVED target value (record[6] before the conditional update). */
        if (record[5] >= 6) {
            uint8_t diff = (uint8_t)(record[6] - record[4]);
            uint8_t step = diff / 2u;
            record[4] = (uint8_t)(record[4] + step);
            record[6] = record[2];
            record[5] = 0;
        }
    }

    /* Write smoothed value back to record[2] */
    record[2] = record[4];

    /* ── Track 2: advance record[10] toward record[2] ─────────────────── */
    /* record[10]: second smoothed value
     * record[11]: frame counter
     * record[12]: record[2] value saved at start of interpolation cycle
     *
     * Condition: bls branches (skip counter++) when record[10] <= record[2].
     * → Counter increments ONLY when record[10] > record[2].
     *
     * Reads record[2] AFTER Track 1 has already written its result there.
     *
     * FIX vs prior: prior C used record[0xA] < record[2] (OPPOSITE).
     * The ROM's bls instruction branches when unsigned <= (C=1 or Z=1),
     * which means it skips the increment when record[10] <= record[2].
     */

    /* record[10] > record[2]? (bls: branch if C=1 or Z=1, i.e., unsigned <=) */
    if (record[0xA] > record[2]) {
        /* record[10] is above target: advance interpolation (decreasing toward target) */
        record[0xB]++;  /* counter++ */

        /* Read record[12] BEFORE any conditional update (saved target) */
        uint8_t saved_target = record[0xC];

        /* If record[2] has dropped below saved target, update saved target */
        if (record[2] < saved_target) {
            record[0xC] = record[2];
        }

        /* Interpolate: advance record[10] toward record[12] by half the distance.
         * ROM: sub.l D1,D0 where D0=record[10], D1=record[12]
         *       lsr.l #1, D0
         *       sub.b D0,($a,A0)  → record[10] -= step
         * step = (record[10] - record[12]) / 2 (unsigned, logical shift)
         * record[10] = record[10] - step
         *
         * Prior C: step = (record[0xA] - record[0xC]) / 2  ← SAME formula!
         * So the formula was correct by coincidence. The bug was ONLY the condition.
         */
        if (record[0xB] >= 6) {
            /* step = (record[10] - record[12]) / 2, unsigned */
            uint8_t step = (uint8_t)((uint8_t)(record[0xA] - record[0xC]) / 2u);
            record[0xA] = (uint8_t)(record[0xA] - step);
            record[0xC] = record[2];
            record[0xB] = 0;
        }
    } else {
        /* record[10] has reached or dropped below target: reset counters */
        record[0xC] = 0;
        record[0xB] = 0;
    }

    /* Write second track result to record[2] (overwrites Track 1's write) */
    record[2] = record[0xA];
}

/* ── Geometry consumer stub ($03D254) ─────────────────────────────── */

int32_t
stunrun_mc68010_object_geometry_consumer(int32_t arg_word_sext,
                                         const uint8_t buffer[32])
{
    /* Stub: $03D254 returns arg_word (or modified) in D0.
     * The caller at $03E51C uses the return for geometry computation.
     * Stub returns 0 so callers that only check for error (D0 != 0) remain safe.
     */
    (void)arg_word_sext;
    (void)buffer;
    return 0;
}

/* ── Object record copy to buffer ($03D012) ───────────────────────── */

uint16_t
stunrun_mc68010_object_copy_to_buffer(uint16_t record_index,
                                       uint16_t arg_word,
                                       uint8_t *ram,
                                       size_t ram_size,
                                       uint8_t buffer[32])
{
    uint32_t ptr_offset;
    uint8_t *record;
    uint16_t i;

    /* DBRA copy loop: exactly 16 bytes, one byte at a time.
     *   moveq   #$f,D0   ; D0 = 15 (DBRA: iterates 16 times, stops at -1)
     *   move.b  (A0)+,(A1)+ ; copy 1 byte, post-increment both
     *   dbra    D0,$3d040  ; D0--, loop until D0 = $FFFF
     *
     * DBRA (decrement and branch if not result -1) loops while D0 != $FFFF.
     * With D0 = 15, it runs 16 iterations: 15,14,...,0,-1($FFFF) → exit.
     * The copy is confirmed byte-at-a-time (not MOVEM), matching the listing.
     */

    /* Validate inputs */
    if (buffer == NULL) {
        return 0;
    }
    if (ram == NULL || ram_size < 0x8000u) {
        return 0;
    }

    /* Compute record pointer: $FFDD86 + (record_index * 16)
     * = RAM_BASE + 0x5D86 + record_index * 16
     * record_ptr_offset = RECORD_OFFSET_FROM_RAM_BASE + index * 16 */
    ptr_offset = RECORD_OFFSET_FROM_RAM_BASE +
                 ((uint32_t)record_index * STUNRUN_OBJECT_RECORD_STRIDE);

    if (ptr_offset + 16 > ram_size) {
        /* Record would extend past RAM boundary — return 0 (no geometry) */
        return 0;
    }

    record = ram + ptr_offset;

    /* Copy exactly 16 bytes from record to buffer, byte at a time.
     * The DBRA loop is the confirmed copy mechanism in the ROM. */
    for (i = 0; i < 16; i++) {
        buffer[i] = record[i];
    }

    /* After copy: call $2256C twice (SR swap stubs — no-ops in native).
     * Then call $03D254 (geometry consumer) with:
     *   arg1 = sign-extended arg_word (D0 low word, extended to long)
     *   arg2 = pointer to buffer
     *
     * In the ROM:
     *   move.w  D2,D0   ; D0 = buffer ptr low word (from saved arg_word)
     *   ext.l   D0       ; sign-extend to 32-bit
     *   move.l  D0,-(A7) ; push as arg1
     *   pea     (-$10,A6) ; push buffer ptr as arg2
     *   jsr     $3d254.l  ; geometry consumer
     *
     * The stub calls the real consumer; the arg_word is opaque from our view.
     */
    (void)arg_word;  /* Arg word is preserved through SR swap but opaque here */

    return (uint16_t)(stunrun_mc68010_object_geometry_consumer(
        (int32_t)(int16_t)arg_word,  /* sign-extended from 16-bit */
        buffer) & 0xFFFFu);
}

/* ── Object build geometry ($03E3FC) ──────────────────────────────────── */

/* Helper: emit a geometry primitive via the FIFO callback.
 * Writes 5 words to the GSP FIFO:
 *   word 0: 0x2000 + selector (selector = buffer[0x1B])
 *   word 1-4: the four geometry arguments
 */
static void
emit_geometry_packet(void *fifo_ctx,
                      void (*write_fifo_word)(void *, uint16_t),
                      int16_t arg1, int16_t arg2,
                      int16_t arg3, int16_t arg4,
                      uint8_t selector)
{
    write_fifo_word(fifo_ctx, (uint16_t)(0x2000u + selector));
    write_fifo_word(fifo_ctx, (uint16_t)arg1);
    write_fifo_word(fifo_ctx, (uint16_t)arg2);
    write_fifo_word(fifo_ctx, (uint16_t)arg3);
    write_fifo_word(fifo_ctx, (uint16_t)arg4);
}

void
stunrun_mc68010_object_build_geometry(uint16_t record_index,
                                      uint16_t arg_word,
                                      const uint8_t buffer[32],
                                      void *fifo_context,
                                      void (*write_fifo_word)(void *, uint16_t))
{
    /* D3 = record_index (word at buffer+0x0A in caller's frame)
     * D2 = (int16_t)arg_word + 8  (word at buffer+0x0E in caller's frame)
     *
     * The buffer is at the caller's frame at offset A6-$10.
     * Accessing +$0A from the caller's A6 gives the record_index word.
     * Accessing +$0E from the caller's A6 gives the arg_word word.
     * Both are 16-bit values in the MC68010's big-endian byte order.
     *
     * NOTE: The 16-byte record is copied by $03D012, but $03E3FC reads
     * up to buffer[0x1B] (selector byte). This exceeds the 16-byte copy.
     * The full local frame in $03E51C is 32 bytes (A6-$10 to A6+$10),
     * so a 32-byte buffer is sufficient for all accesses.
     */
    int16_t D3 = (int16_t)record_index;
    int16_t D2 = (int16_t)(int16_t)arg_word + 8;

    /* The selector byte is buffer[0x1B] (5th argument slot at +$1B in frame).
     * Confirmed from $02F470: move.b ($1b,A6),D0 followed by addi.l #$2000,D0.
     * This byte is at buffer offset 0x1B within the caller's local buffer. */
    uint8_t selector = buffer[0x1B];

    /* 7 calls to geometry_emit_fifo, each emitting 5 words.
     *
     * Call 1: args = D2-3, D3+6, D2, D3
     *   D0 = D2; D0.sext -= 3 → arg1 = D2 - 3
     *   D0 = D3; D0.sext += 6 → arg2 = D3 + 6
     *   D0 = D2; D0.sext      → arg3 = D2
     *   D0 = D3; D0.sext      → arg4 = D3
     */
    emit_geometry_packet(fifo_context, write_fifo_word,
                         D2 - 3, D3 + 6, D2, D3, selector);

    /* Call 2: args = D2-3, D3+3, D2-3, D3+6 */
    emit_geometry_packet(fifo_context, write_fifo_word,
                         D2 - 3, D3 + 3, D2 - 3, D3 + 6, selector);

    /* Call 3: args = D2-6, D3+3, D2-3, D3+3 */
    emit_geometry_packet(fifo_context, write_fifo_word,
                         D2 - 6, D3 + 3, D2 - 3, D3 + 3, selector);

    /* Call 4: args = D2-6, D3-3, D2-6, D3+3 */
    emit_geometry_packet(fifo_context, write_fifo_word,
                         D2 - 6, D3 - 3, D2 - 6, D3 + 3, selector);

    /* Call 5: args = D2-3, D3-3, D2-3, D3-6 */
    emit_geometry_packet(fifo_context, write_fifo_word,
                         D2 - 3, D3 - 3, D2 - 3, D3 - 6, selector);

    /* Call 6: args = D2-3, D3-6, D2-3, D3-3 */
    emit_geometry_packet(fifo_context, write_fifo_word,
                         D2 - 3, D3 - 6, D2 - 3, D3 - 3, selector);

    /* Call 7: args = D2 (unextended), D3.sext, D2-3, (D3.sext - 6)
     * Last call uses sign-extended D3 for arg2 and D3-6 for arg4.
     * arg1 uses D2's unextended word value (not sign-extended).
     */
    emit_geometry_packet(fifo_context, write_fifo_word,
                         D2,                     /* arg1: D2 unextended word */
                         D3,                     /* arg2: D3 sign-extended */
                         D2 - 3,                 /* arg3: D2 - 3 */
                         D3 - 6,                 /* arg4: D3 - 6 */
                         selector);
}
