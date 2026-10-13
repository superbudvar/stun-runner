/* MC68010 boot-chain head, ROM $506C-$5078 (maincpu program ROM).
 *
 * Source chip: the STUN Runner maincpu program ROM, assembled flat by
 * `tools/build-maincpu-image` from the six 136070-210x/300x/5xxx/6xxx pairs.
 * Image used for transcription:
 *   roms/stunrun/stunrun_maincpu.bin
 *   sha256 29c911a21d1abe780419cd3a924080756490f2d008776995dd48c7b2bedc5b6c
 *
 * Verbatim transcription ($506C-$5078, three instructions, 12 bytes):
 *
 *     0000506c: 2e 7c ff ff c0 00   movea.l #$ffffc000, A7
 *     00005072: 41 fa 00 06         lea     ($6,PC), A0        ; A0 = $507A
 *     00005076: 60 00 03 7a         bra     $53f2
 *
 * ── This is a transfer, not a function ───────────────────────────────────
 * Nothing calls $506C.  There is no `jsr $506C` anywhere in the image (see
 * the findings doc), and there is no `rts` at the end: control leaves at
 * $5076 and never comes back.  The slice is therefore modelled as "two
 * register writes and a branch", and the seam reports the three in the order
 * the 68010 performs them rather than modelling a call frame.  Modelling it as
 * a C function that returns would invent an activation record the ROM does not
 * have.
 *
 * It is nonetheless a real entry point, and not a label inside something
 * larger.  $506A is an `rts` belonging to the routine that ends there, so
 * there is no fall-through into $506C, and the only three incoming transfers in
 * the whole image are `jmp $506C.l` at $0106, $0172 and $02B2.  $0106 and
 * $0172 are two entries of the thunk table at $0100; $02B2 is reached straight
 * out of the port test at $2A0.
 *
 * `native/fake_ports.c` records the path as `$2a0 -> $506c -> $548e`.  The
 * endpoints are right and the middle step is a conditional: $2A0 is
 * `btst #$0,$60c001.l` / `bne $2b2`, and $2B2 is the `jmp $506C.l`.  The
 * sibling arm, when bit 0 is clear, is `jmp $8FEE.l` at $2AC, and $8FEE runs a
 * parallel chain ($8FF2 sets A0 = $8FFA and $8FF6 branches to the same $53F2),
 * so $548E is one of two places that chain can go.  The record is corrected
 * in the findings doc.
 *
 * ── The continuation chain ──────────────────────────────────────────────
 * $53F2 (`stunrun_gsp_boot_ready_sequence`, already transcribed) ends with
 * `jmp (A0)` at $548C.  A0 therefore is not a data pointer but the *return
 * trampoline* the boot sequence will jump back to, and $507A is the address
 * of the next link.  The full chain is
 *
 *     $506C  SP := $FFFFC000, A0 := $507A, bra $53F2
 *     $53F2  ... $548C  jmp (A0) -> $507A
 *     $507A  A0 := $5082, bra $548E        <- 230400 discarded HSTDAT writes
 *     $5082  A0 := $508A, bra $54C2        <- pattern ramps at $F5xxxxxx
 *     $508A  D0 := $0047, A2 := $5098, jmp $25E8.l   <- draws SELECTOR $47
 *
 * Each link is the same two instructions in the same order, so the shape
 * modelled here is the shape of the whole chain.  `$508A` hands $5098 to A2,
 * not to A0, because $25E8 drives A0 and A1 itself; the chain is finished
 * there and never resumes.
 *
 * ── Why both addresses are derived, not hardcoded ───────────────────────
 * On the 68010 the PC in a PC-relative operand is the address of the
 * instruction's EXTENSION WORD, not of its first byte.  For `lea ($6,PC),A0`
 * encoded at $5072 that is $5074, and $5074 + $0006 = $507A.  For
 * `bra.s $53F2` encoded at $5076 that is $5078, and $5078 + $037A = $53F2.
 * Both are exposed as base+displacement pairs and combined by
 * `stunrun_gsp_boot_entry_pc_relative` so the slice records the rule instead
 * of the two numbers that fall out of it.  Getting the base wrong by one --
 * using the opcode address, $5072 -- yields $5078, which is a byte inside the
 * `bra` and would decode as different code entirely.
 *
 * ── Why $FFFFC000 ───────────────────────────────────────────────────────
 * Two independent readings, both consistent with the bytes; neither is
 * resolved here.
 *
 * 1. It is the MC68010 Initial SSP.  The first long word of the reset vector,
 *    at ROM offset $00000000, is the bytes `ff ff c0 00`, i.e. the same
 *    $FFFFC000.  The ROM's own reset-vector PC, the second long at $00000004,
 *    is $00000216, and $0216 does `movea.l ($0,A0), A7` with A0 = 0 -- it
 *    loads A7 FROM that long.  So the ROM treats $FFFFC000 as "the stack
 *    pointer", not as an arbitrary constant.
 * 2. It is a fixed stack-frame base.  `movea.l #$ffffc000,A7` appears at 45
 *    sites in this image, and it is the first instruction of each one -- the
 *    uniform procedure prologue.  $0FBC is the same shape as $506C (set A7,
 *    `lea (d,PC),An`, tail-branch); $50B0 and $53E6 are the same too.  On that
 *    reading $506C is not "restoring" anything: nothing saves the old A7 and
 *    nothing restores it.  It installs a frame pointer and discards whatever
 *    was there.  That is safe here only because $506C is entered by `jmp` and
 *    never by `jsr`, so there is no return address to lose.
 *
 * The 24-bit bus reconciles the two numerically.  The 68010 presents 24
 * address lines, so the effective stack pointer is $FFFFC000 & $FFFFFF =
 * $FFC000 -- which is exactly 16 KiB above $FF8000, the base of the 32 KiB
 * work RAM established in findings section 9.  That leaves 16 KiB of stack
 * growth below A7 and nothing above it.  The SP therefore *corroborates*
 * section 9's 32 KiB claim rather than contradicting it.
 */
#ifndef STUNRUN_GSP_BOOT_ENTRY_H
#define STUNRUN_GSP_BOOT_ENTRY_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Values read straight out of the encoding ──────────────────────────── */

/* movea.l #$ffffc000,A7 ($506C).  The full 32-bit literal, as written. */
#define STUNRUN_GSP_BOOT_ENTRY_SP 0xFFFFC000u

/* The same value after the 68010's 24-bit address mask, i.e. the first stack
 * address the ROM can actually reach.  $FFFFC000 is above $FFFFFF as a
 * 24-bit address and is not in the RAM window until it is masked. */
#define STUNRUN_GSP_BOOT_ENTRY_ADDRESS_MASK 0x00FFFFFFu
#define STUNRUN_GSP_BOOT_ENTRY_SP_EFFECTIVE \
    (STUNRUN_GSP_BOOT_ENTRY_SP & STUNRUN_GSP_BOOT_ENTRY_ADDRESS_MASK)

/* Base of the work RAM: map(0xff8000, 0xffffff).ram(), mame-ref
 * src/mame/atari/harddriv.cpp:615.  32 KiB, per findings section 9. */
#define STUNRUN_GSP_BOOT_ENTRY_RAM_BASE 0xFF8000u

/* lea ($6,PC),A0 ($5072).  The base is the extension-word address $5074,
 * which is instruction address + 2 on the 68010. */
#define STUNRUN_GSP_BOOT_ENTRY_LEA_INSN 0x5072u
#define STUNRUN_GSP_BOOT_ENTRY_LEA_PC_BASE 0x5074u
#define STUNRUN_GSP_BOOT_ENTRY_LEA_DISP 0x0006

/* bra.s $53f2 ($5076).  Base is likewise the extension-word address $5078. */
#define STUNRUN_GSP_BOOT_ENTRY_BRA_INSN 0x5076u
#define STUNRUN_GSP_BOOT_ENTRY_BRA_PC_BASE 0x5078u
#define STUNRUN_GSP_BOOT_ENTRY_BRA_DISP 0x037A

/* ── What the routine produces ─────────────────────────────────────────── */

/* The link that $53F2's `jmp (A0)` at $548C will return to.  Named here as a
 * constant only so the test can state the expected chain; nothing in the slice
 * uses it, because the slice derives the same value from the LEA pair. */
#define STUNRUN_GSP_BOOT_ENTRY_CONTINUATION 0x507Au

/* ── The PC-relative rule ──────────────────────────────────────────────── */

/* The MC68010 forms `(d,PC)` from the address of the operand's extension
 * word.  `pc_base` is therefore the extension-word address, NOT the address of
 * the instruction's first byte.  `displacement` is the raw 16-bit extension
 * word and is sign-extended, because `(d,PC)` is a signed 16-bit displacement
 * addressing form. */
uint32_t stunrun_gsp_boot_entry_pc_relative(uint32_t pc_base,
                                            int16_t displacement);

/* ── The three instructions, as observable events ──────────────────────── */

/* In the order the 68010 performs them.  The order is the point: the slice
 * exists to show that SP is set before A0 is loaded and that control leaves
 * last, and a trace that only recorded final register values could not tell
 * the chain from any other ordering of the same three results. */
typedef enum stunrun_gsp_boot_entry_event {
    STUNRUN_GSP_BOOT_ENTRY_SET_SP = 0,
    STUNRUN_GSP_BOOT_ENTRY_SET_CONTINUATION = 1,
    STUNRUN_GSP_BOOT_ENTRY_TRANSFER = 2
} stunrun_gsp_boot_entry_event_t;

/* Register and block state.  `a7` and `a0` are seeded by the caller so a test
 * can show that $506C REPLACES both registers rather than adding to them --
 * the ROM has no caller whose SP it is meant to extend. */
typedef struct stunrun_gsp_boot_entry_state {
    void *ctx;
    void (*emit)(void *ctx, stunrun_gsp_boot_entry_event_t event, uint32_t value);

    uint32_t a7;
    uint32_t a0;

    /* Inputs to the two PC-relative instructions, kept in the struct rather
     * than folded into the code so the arithmetic is inspectable and can be
     * driven off a shifted base. */
    uint32_t lea_pc_base;
    int16_t lea_displacement;
    uint32_t bra_pc_base;
    int16_t bra_displacement;

    /* Outputs. */
    uint32_t continuation; /* the value materialised into A0 */
    uint32_t transfer_pc;  /* where control actually leaves for */
} stunrun_gsp_boot_entry_state_t;

/* Seed `state` with the real bases and displacements of $5072 and $5076, a
 * sentinel caller A7/A0, and no emitter.  A NULL `state` is a no-op. */
void stunrun_gsp_boot_entry_init(stunrun_gsp_boot_entry_state_t *state);

/* Execute $506C-$5078 against `state`.  No-op if `state` or its emitter is
 * NULL; the ROM has no way to run these three instructions and fail to. */
void stunrun_gsp_boot_entry_step(stunrun_gsp_boot_entry_state_t *state);

#ifdef __cplusplus
}
#endif

#endif