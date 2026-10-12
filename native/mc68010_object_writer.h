/* MC68010 object record update routines.  See
 * docs/mc68010-boot-findings.md for provenance.
 *
 * ROM addresses:
 *   $03CF40  object_record_caller  - main entry, calls $03D120 and $03D1A0
 *   $03D120  object_update_clamp   - clamps byte fields at offsets +0, +1, +2
 *   $03D1A0  object_update_interp - advances interpolation fields at +5, +6, +B, +C
 *
 * Register names in comments are the ROM's (D0-D7, A0-A6).
 *
 * The 68010 address space is 24 bits (16 MB).  RAM is mapped at $FF8000-
 * $FFFFFF (32 KB).  The disassembly shows LEA $FFFF8000,A0, but this is
 * a 24-bit effective address: the upper byte is ignored, so $FFFF8000
 * resolves to $00FF8000 which IS the RAM base.
 *
 * The record base is $FFDD86 (within the RAM region $FF8000-$FFFFFF).
 * Each record is 16 bytes.
 *
 * Address computation (per Ghidra listing at $03CF40):
 *   ASL.W #4,D0         ; D0 = index * 16  ← stride 16, NOT 4
 *   PEA (A0,D0.W)       ; push $FFDD86 + index*16
 * Both $03D120 and $03D1A0 are called with the same record pointer.
 *
 * The index is passed on the stack at A6+$0A (4 bytes, from caller).
 * A byte is read from the input/state area at $FFFF8000 but is NOT used
 * in the address calculation (CONFIRMED from disassembly).
 */
#ifndef STUNRUN_MC68010_OBJECT_WRITER_H
#define STUNRUN_MC68010_OBJECT_WRITER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Constants ─────────────────────────────────────────────────────────── */

/* Base of 68010 work RAM in the 24-bit address space: $FF8000.
 * The 68010 ignores the upper byte of 32-bit addresses, so $FFFF8000
 * in the disassembly is effectively $00FF8000 = $FF8000. */
#define STUNRUN_RAM_BASE 0x00FF8000u

/* Base of the 32KB mapped input/state area: $FF8000. */
#define STUNRUN_INPUT_STATE_BASE STUNRUN_RAM_BASE

/* Base address of the 16-byte indexed object/animation record array.
 * This is within the RAM region: $FFDD86 = $FF8000 + 0x5D86 */
#define STUNRUN_OBJECT_RECORD_BASE 0x00FFDD86u

/* Record stride: index is multiplied by 16 in the ROM.
 * Listing at $03CF5E: ASL.W #4,D0 → index * 16.
 * This was previously documented as stride 4 (ASL.W #2), which is WRONG. */
#define STUNRUN_OBJECT_RECORD_STRIDE 16u

/* ── Record update callbacks ─────────────────────────────────────────── */

/* Callback type for the two record update routines.
 * Each receives the record pointer (16-byte record at $FFDD86 + index*16).
 *
 * $03D120 (object_update_clamp): clamps three byte fields at offsets +0, +1, +2.
 * $03D1A0 (object_update_interp): advances interpolation/counter fields at +5/+6
 *   and +B/+C, then writes smoothed values back to +2.
 */
typedef void (*stunrun_object_update_fn)(uint8_t *record);

/* ── Object record caller ($03CF40) ─────────────────────────────────── */

/* The transport seam.  Both called routines receive the same record pointer.
 *
 * `record_index` is the index from the caller's stack argument (at A6+$0A).
 * `input_byte` is the byte read from $FFFF8000 (discarded by the ROM).
 *
 * The record pointer is computed as: $FFDD86 + (record_index * 16)
 * Both $03D120 and $03D1A0 are called with this pointer.
 *
 * Callbacks are required; NULL callbacks return without calling the updaters.
 *
 * Returns 0 on success (both updaters called), -1 if callbacks are NULL
 * or if the record pointer would fall outside the 32KB RAM window.
 */
int stunrun_mc68010_object_record_caller(
    uint16_t record_index,
    uint8_t input_byte,
    uint8_t *ram,
    size_t ram_size,
    stunrun_object_update_fn updater_03D120,
    stunrun_object_update_fn updater_03D1A0);

/* ── Standalone record address calculator ────────────────────────────── */

/* Computes the record pointer for a given index.
 * Mirrors the ROM calculation: $FFDD86 + (index * 16).
 *
 * The `ram` pointer should represent the start of the 32KB RAM region
 * at $FF8000 (i.e., ram[0] corresponds to address $FF8000).
 *
 * Returns NULL if the computed offset exceeds ram_size.
 */
uint8_t *stunrun_object_record_ptr(uint16_t record_index,
                                   uint8_t *ram,
                                   size_t ram_size);

/* ── Object update clamp ($03D120) ─────────────────────────────────── */

/* Ghidra symbol: s_0003D120_mc68010_object_update_record
 *
 * Record field update: clamps the byte at offset +0 to the range
 * [min(offset+1, offset+2), max(offset+1, offset+2)].
 *
 * Entry: A0 = record pointer ($FFDD86 + index*16)
 *        D0 = byte at A6+$0F (discarded; the byte at $FFFF8000 is NOT used)
 *
 * Algorithm:
 *   1. Load record[0] (D0/D1), record[1] (D3), record[2] (D4)
 *   2. Determine the clamp range [lower, upper] = [min(D3,D4), max(D3,D4)]
 *   3. Clamp record[0] to that range:
 *        if record[0] < lower: record[0] = lower
 *        if record[0] > upper: record[0] = upper
 *        (if in range, record[0] passes through unchanged)
 *   4. Write clamped record[0] to record offsets +0 and +2.
 *   5. Write original record[0] to record offset +1.
 *
 * Disassembly provenance: Ghidra listing from MAME listing generation
 * at $03D120 (file offset 0x03D120), verified against 68010 opcode table.
 */
void stunrun_mc68010_object_update_record(uint8_t *record);

#ifdef __cplusplus
}
#endif

/* ── Object update interpolation ($03D1A0) ───────────────────────────── */

/* Ghidra symbol: s_0003D1A0_mc68010_object_advance_interpolation
 *
 * Record field update: advances interpolation/counter fields at offsets
 * +4/+5/+6 and +A/+B/+C, then writes smoothed values back to offset +2.
 *
 * Entry: A0 = record pointer ($FFDD86 + index*16)
 *        D0 = byte at A6+$0F (DISCARDED; matches $03D120 pattern)
 *
 * Two independent interpolation tracks share record[2] as the target:
 *
 *   Track 1 (offsets +4, +5, +6):
 *     record[5]++ while record[4] < record[2]        (bcc: branch if C=0)
 *     record[4] += (record[6] - record[4]) / 2       (when record[5] >= 6)
 *     record[6] = record[2], record[5] = 0             (when record[5] >= 6)
 *     record[4] written to record[2]
 *
 *   Track 2 (offsets +A, +B, +C):
 *     record[B]++ while record[A] > record[2]         (bls: branch if C=1 or Z=1)
 *     record[A] -= (record[C] - record[A]) / 2        (when record[B] >= 6)
 *     record[C] = record[2], record[B] = 0             (when record[B] >= 6)
 *     record[A] written to record[2]  (overwrites Track 1 result)
 *
 * IMPORTANT FIX vs prior implementation:
 *   - Prior C used stride 4 (ASL.W #2) but listing shows ASL.W #4 → stride 16
 *   - Prior Track 2 condition: record[0xA] < record[2] (WRONG — should be >)
 *   - Prior Track 2 formula: record[0xA] - record[0xC] (WRONG — should be C - A)
 *
 * Disassembly provenance: Ghidra listing from MAME listing generation
 * at $03D1A0 (file offset 0x03D1A0), verified against 68010 opcode table.
 */
void stunrun_mc68010_object_advance_interpolation(uint8_t *record);

/* ── Object record copy to buffer ($03D012) ─────────────────────────── */

/* Ghidra symbol: s_0003D012_mc68010_object_copy_to_buffer
 *
 * Copies exactly 16 bytes from the object record at
 * $FFDD86 + (record_index * 16) into the provided 16-byte buffer,
 * then calls the geometry consumer ($03D254).
 *
 * Entry conditions (arguments passed on stack by caller at $03E51C):
 *   arg1 (word at A6+$0A): record_index (0-based)
 *   arg2 (word at A6+$0E): opaque word value (passed through to $03D254)
 *
 * Sequence:
 *   1. Saves D2, D3
 *   2. Calls $2256C (SR swap stub with flag $2500) — no-op in native port
 *   3. Computes record_ptr = $FFDD86 + (record_index * 16)
 *   4. Copies exactly 16 bytes (DBRA loop, 16 iterations) to local buffer
 *   5. Calls $2256C again (SR swap with arg_word as flag) — no-op in native
 *   6. Calls $03D254 (geometry consumer) with:
 *        arg1 = sign-extended arg_word
 *        arg2 = pointer to 16-byte buffer
 *   7. Restores D2, D3 and returns
 *
 * The SR swap calls ($2256C) are pass-through stubs; the geometry consumer
 * ($03D254) is an opaque external function whose return is passed through.
 *
 * The `ram` parameter must point to the base of the 32KB emulated RAM
 * ($FF8000).  The record is read from
 *   ram + (0xFFDD86 - 0xFF8000) + record_index * 16
 * = ram + 0x5D86 + record_index * 16
 *
 * Disassembly provenance: MAME listing
 * `/tmp/opencode/stunrun-listings-current/maincpu-68010.lst`, line 72540,
 * ROM address $03D012.
 *
 * Full disassembly:
 *   03D012: link    A6,#-$10
 *   03D016: movem.l D2-D3,-(A7)
 *   03D01A: move.w  ($a,A6),D3       ; D3 = record_index
 *   03D01E: move.w  ($e,A6),D2       ; D2 = opaque arg_word (preserved)
 *   03D022: pea     $2500.w           ; Push SR swap flag word
 *   03D026: jsr     $2256c.l          ; SR swap stub (saves/restores SR)
 *   03D02C: move.w  D0,D1             ; D1 = saved SR (discarded in native)
 *   03D02E: move.w  D3,D0             ; D0 = record_index
 *   03D030: asl.w   #4,D0             ; D0 = record_index * 16
 *   03D032: movea.l #$ffdd86,A0       ; A0 = $FFDD86
 *   03D038: adda.w  D0,A0             ; A0 = record_ptr
 *   03D03A: lea     (-$10,A6),A1      ; A1 = local buffer (frame-local in ROM)
 *   03D03E: moveq   #$f,D0            ; D0 = 15 (DBRA count)
 *   03D040: move.b  (A0)+,(A1)+       ; Copy 1 byte
 *   03D042: dbra    D0,$3d040         ; Loop 16 times (DBRA: D0--, until $FFFF)
 *   03D046: moveq   #$0,D0             ; D0 = 0
 *   03D048: move.w  D1,D0              ; D0 = arg_word (sign-extended below)
 *   03D04A: move.l  D0,-(A7)           ; Push arg_word as long
 *   03D04C: jsr     $2256c.l           ; SR swap again (flag = arg_word)
 *   03D052: move.w  D2,D0              ; D0 = buffer ptr low word
 *   03D054: ext.l   D0                 ; D0 = sign-extended buffer ptr
 *   03D056: move.l  D0,-(A7)          ; Push sign-ext ptr
 *   03D058: pea     (-$10,A6)         ; Push buffer ptr
 *   03D05C: jsr     $3d254.l           ; Geometry consumer
 *   03D062: movem.l (-$18,A6),D2-D3   ; Restore D2, D3
 *   03D068: unlk    A6
 *   03D06A: rts
 *
 * The 16-byte copy is byte-at-a-time via DBRA loop (not MOVEM or MOVEP).
 * This is the only confirmed 16-byte record copy path from $FFDD86.
 */
uint16_t stunrun_mc68010_object_copy_to_buffer(
    uint16_t record_index,
    uint16_t arg_word,
    uint8_t *ram,
    size_t ram_size,
    uint8_t buffer[32]);

/* ── Geometry consumer stub ($03D254) ─────────────────────────────── */

/* Ghidra symbol: s_0003D254_mc68010_object_geometry_consumer
 *
 * Called by $03D012 after the record copy. Receives the 16-byte buffer
 * and an argument word. The buffer content drives geometry submission.
 *
 * Entry (at $03D254):
 *   arg1 (long at A6+$08): sign-extended arg_word
 *   arg2 (long at A6+$0C): pointer to 16-byte buffer
 *
 * Disassembly:
 *   03D254: link    A6,#$0
 *   03D258: movem.l D2,-(A7)
 *   03D25C: move.l  ($8,A6),D1       ; D1 = arg_word (sign-extended long)
 *   03D260: move.w  ($e,A6),D2       ; D2 = buffer ptr low word
 *   03D264: tst.w   D2               ; D2 >= 0?
 *   03D266: bge     $3d282            ; Skip negation if D2 >= 0
 *   03D268: move.w  D2,D0
 *   03D26A: ext.l   D0
 *   03D26C: neg.l   D0
 *   03D26E: add.l   D0,D0
 *   03D270: move.l  D0,-(A7)
 *   03D272: move.l  D1,-(A7)
 *   03D274: jsr     $3d29a.l
 *   03D27A: move.w  D2,D1
 *   03D27C: ext.l   D1
 *   03D27E: add.l   D1,D0
 *   03D280: bra     $3d290
 *   03D282: move.l  D1,D0             ; D0 = arg_word (no negation)
 *   03D290: unlk    A6
 *   03D292: rts
 *
 * The return value in D0 is the geometry result consumed by $03E51C.
 * Stub implementation: returns 0 (consumed by caller, exact value not critical).
 */
int32_t stunrun_mc68010_object_geometry_consumer(int32_t arg_word_sext,
                                                  const uint8_t buffer[32]);

/* ── Object build geometry ($03E3FC) ──────────────────────────────────── */

/* Ghidra symbol: s_0003E3FC_mc68010_object_build_geometry
 *
 * Builds geometry primitives from a 16-byte record buffer by making 7 calls
 * to the shared geometry FIFO emitter ($02F470, geometry_emit_fifo).
 *
 * Entry conditions (called from $03E51C):
 *   Stack arguments (at caller's A6 frame, pushed right-to-left):
 *     arg1 (long at A6+$08): sign-extended record_index word
 *     arg2 (long at A6+$0C): sign-extended (geometry_result + 8) word
 *   The 16-byte buffer is a local in the caller's frame at A6-$10.
 *
 * Inside $03E3FC:
 *   D3 = record_index (word loaded from A6+$0A = caller's buffer+0x0A)
 *   D2 = geometry_result + 8 (word loaded from A6+$0E = caller's buffer+0x0E)
 *
 * The function then makes 7 calls to geometry_emit_fifo, each emitting 5 words
 * to the GSP FIFO at $C0000C. The parameters for each call are computed by
 * reading specific offsets from the 16-byte buffer (at D3 = buffer+0x0A and
 * D2 = buffer+0x0E as base words) and applying arithmetic transformations.
 *
 * Full disassembly from MAME listing (file offset 0x3E3FC):
 *
 * 03E3FC: link    A6,#$0
 * 03E400: movem.l D2-D3/A2,-(A7)
 * 03E404: move.w  ($a,A6),D3          ; D3 = record_index word
 * 03E408: move.w  ($e,A6),D2          ; D2 = geometry_result+8 word
 * 03E40C: movea.l #$2f470,A2          ; A2 = $02F470
 * 03E412: pea     $10.w               ; Push $10 (constant selector/flag)
 *
 *   ;; Call 1: D3-3, D2+6, D3, D2+6
 * 03E416: move.w  D2,D0               ; D0 = D2 (geometry_result+8)
 * 03E418: ext.l   D0                  ; D0 = sign-ext(D2)
 * 03E41A: subq.l  #3,D0               ; D0 = D2 - 3
 * 03E41C: move.l  D0,-(A7)            ; arg1 = D2 - 3
 * 03E41E: move.w  D3,D0               ; D0 = D3 (record_index)
 * 03E420: ext.l   D0                  ; D0 = sign-ext(D3)
 * 03E422: addq.l  #6,D0               ; D0 = D3 + 6
 * 03E424: move.l  D0,-(A7)            ; arg2 = D3 + 6
 * 03E426: move.w  D2,D0               ; D0 = D2
 * 03E428: ext.l   D0                  ; D0 = sign-ext(D2)
 * 03E42A: move.l  D0,-(A7)            ; arg3 = D2
 * 03E42C: move.w  D3,D0               ; D0 = D3
 * 03E42E: ext.l   D0                  ; D0 = sign-ext(D3)
 * 03E430: move.l  D0,-(A7)            ; arg4 = D3
 * 03E432: jsr     (A2)                ; Call $02F470
 *
 *   ;; Call 2: D3-3, D2+3, D3-3, D2+6
 * 03E434: pea     $10.w
 * 03E438: move.w  D2,D0
 * 03E43A: ext.l   D0
 * 03E43C: subq.l  #3,D0
 * 03E43E: move.l  D0,-(A7)
 * 03E440: move.w  D3,D0
 * 03E442: ext.l   D0
 * 03E444: addq.l  #3,D0
 * 03E446: move.l  D0,-(A7)
 * 03E448: move.w  D2,D0
 * 03E44A: ext.l   D0
 * 03E44C: subq.l  #3,D0
 * 03E44E: move.l  D0,-(A7)
 * 03E450: move.w  D3,D0
 * 03E452: ext.l   D0
 * 03E454: addq.l  #6,D0
 * 03E456: move.l  D0,-(A7)
 * 03E458: jsr     (A2)
 *
 *   ;; Call 3: D3-6, D2+3, D3-3, D2+3
 * 03E45A: pea     $10.w
 * 03E45E: move.w  D2,D0
 * 03E460: ext.l   D0
 * 03E462: subq.l  #6,D0
 * 03E464: move.l  D0,-(A7)
 * 03E466: move.w  D3,D0
 * 03E468: ext.l   D0
 * 03E46A: addq.l  #3,D0
 * 03E46C: move.l  D0,-(A7)
 * 03E46E: move.w  D2,D0
 * 03E470: ext.l   D0
 * 03E472: subq.l  #3,D0
 * 03E474: move.l  D0,-(A7)
 * 03E476: move.w  D3,D0
 * 03E478: ext.l   D0
 * 03E47A: addq.l  #3,D0
 * 03E47C: move.l  D0,-(A7)
 * 03E47E: jsr     (A2)
 *
 *   ;; Call 4: D3-6, D2-3, D3-6, D2+3
 * 03E480: pea     $10.w
 * 03E484: move.w  D2,D0
 * 03E486: ext.l   D0
 * 03E488: subq.l  #6,D0
 * 03E48A: move.l  D0,-(A7)
 * 03E48C: move.w  D3,D0
 * 03E48E: ext.l   D0
 * 03E490: subq.l  #3,D0
 * 03E492: move.l  D0,-(A7)
 * 03E494: move.w  D2,D0
 * 03E496: ext.l   D0
 * 03E498: subq.l  #6,D0
 * 03E49A: move.l  D0,-(A7)
 * 03E49C: move.w  D3,D0
 * 03E49E: ext.l   D0
 * 03E4A0: addq.l  #3,D0
 * 03E4A2: move.l  D0,-(A7)
 * 03E4A4: jsr     (A2)
 *
 *   ;; Call 5: D3-3, D2-3, D3-3, D2-6
 * 03E4A6: pea     $10.w
 * 03E4AA: move.w  D2,D0
 * 03E4AC: ext.l   D0
 * 03E4AE: subq.l  #3,D0
 * 03E4B0: move.l  D0,-(A7)
 * 03E4B2: move.w  D3,D0
 * 03E4B4: ext.l   D0
 * 03E4B6: subq.l  #3,D0
 * 03E4B8: move.l  D0,-(A7)
 * 03E4BA: move.w  D2,D0
 * 03E4BC: ext.l   D0
 * 03E4BE: subq.l  #6,D0
 * 03E4C0: move.l  D0,-(A7)
 * 03E4C2: move.w  D3,D0
 * 03E4C4: ext.l   D0
 * 03E4C6: subq.l  #3,D0
 * 03E4C8: move.l  D0,-(A7)
 * 03E4CA: jsr     (A2)
 *
 *   ;; Call 6: D3-3, D2-6, D3-3, D2-3
 * 03E4CC: pea     $10.w
 * 03E4D0: move.w  D2,D0
 * 03E4D2: ext.l   D0
 * 03E4D4: subq.l  #3,D0
 * 03E4D6: move.l  D0,-(A7)
 * 03E4D8: move.w  D3,D0
 * 03E4DA: ext.l   D0
 * 03E4DC: subq.l  #6,D0
 * 03E4DE: move.l  D0,-(A7)
 * 03E4E0: move.w  D2,D0
 * 03E4E2: ext.l   D0
 * 03E4E4: subq.l  #3,D0
 * 03E4E6: move.l  D0,-(A7)
 * 03E4E8: move.w  D3,D0
 * 03E4EA: ext.l   D0
 * 03E4EC: subq.l  #3,D0
 * 03E4EE: move.l  D0,-(A7)
 * 03E4F0: jsr     (A2)
 *
 *   ;; Call 7: D2, (D3-6), D2-3, D3-6 (different pattern)
 * 03E4F2: pea     $10.w
 * 03E4F6: move.w  D2,D0               ; D0 = D2 (unextended for arg1)
 * 03E4F8: ext.l   D0
 * 03E4FA: move.l  D0,-(A7)            ; arg1 = D2 (unextended word)
 * 03E4FC: move.w  D3,D0               ; D0 = D3
 * 03E4FE: ext.l   D0                  ; D0 = sign-ext(D3)
 * 03E500: move.l  D0,-(A7)            ; arg2 = sign-ext(D3)
 * 03E502: move.w  D2,D0
 * 03E504: ext.l   D0
 * 03E506: subq.l  #3,D0
 * 03E508: move.l  D0,-(A7)            ; arg3 = D2 - 3
 * 03E50A: ext.l   D3                  ; D3 = sign-ext(D3)
 * 03E50C: subq.l  #6,D3               ; D3 = D3 - 6
 * 03E50E: move.l  D3,-(A7)            ; arg4 = D3 - 6
 * 03E510: jsr     (A2)
 *
 * 03E512: movem.l (-$c,A6),D2-D3/A2  ; Restore
 * 03E518: unlk    A6
 * 03E51A: rts
 *
 * The geometry_emit_fifo ($02F470) writes 5 words to $C0000C:
 *   {0x2000 + selector, arg1, arg2, arg3, arg4}
 * where selector = byte at stack offset +$1B (from the 5th argument slot).
 *
 * Disassembly provenance: MAME listing
 * `/tmp/opencode/stunrun-listings-current/maincpu-68010.lst`, line 74087,
 * ROM address $03E3FC.
 *
 * The 7 calls each emit 5 words → 35 total FIFO words.
 */
void stunrun_mc68010_object_build_geometry(
    uint16_t record_index,
    uint16_t arg_word,
    const uint8_t buffer[32],
    void *fifo_context,
    void (*write_fifo_word)(void *ctx, uint16_t word));
#endif /* STUNRUN_MC68010_OBJECT_WRITER_H */
