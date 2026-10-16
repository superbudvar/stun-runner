/* Road window transform (gate + paired add/subtract passes), in C.
 *
 * Literal mechanism only. This slice deliberately does NOT name any field of
 * the window; what the words MEAN remains unresolved (see
 * analysis/track-storage.md, "What is still unknown").
 *
 * WHAT THIS SLICE OWNS
 * --------------------
 * Three things, all inside the 68010 maincpu program ROM:
 *
 *   0x0298A2-0x0298DA  subtract pass: base[i] = base[i] - twin[i], 0x300 bytes
 *   0x0298DC-0x029914  add pass:      base[i] = base[i] + twin[i], 0x300 bytes
 *   0x029916..02992A   gate: subq.w #1,$FF954E ; beq expiry ; else jsr $298A2
 *   0x029ADE..029AE8   gate: subq.w #1,$FF954E ; beq expiry ; else jsr $298DC
 *
 * It also carries the loader's divisor latch store at 0x02978E/0x02988A, because
 * that store targets the SAME word the gate decrements ($FF954E) and getting
 * that wrong silently breaks the gate.
 *
 * It does NOT own: the table copy loops themselves (base store at 0x029760 and
 * twin store at 0x02977E in the single-source path; 0x0296E8/0x029726 in the
 * two-source path -- stunrun_road_buffer_copy_scaled in road_buffer_math.h
 * models their byte arithmetic; stunrun_road_window_load here wraps that
 * contract in the window state and adds the latch store), the FIFO drain at
 * 0x02248E (road_fifo.h), or anything the GSP does with the result. That last
 * one is the other side of the seam and is not ours.
 *
 * CONFIRMED (static, from the pinned listing sha256
 * 171e56ccf1a9940635a2e8e9929ab7edaa5841ac3238ef78acf0d6630f8e45e7,
 * reference/listings/stunrun.metadata.json):
 *   - both passes iterate exactly 0x300 bytes (D2 counts 0..0x2FF against D1)
 *   - the subtract pass reads the twin at 0x0298BE and updates the base at
 *     0x0298C0; the add pass reads the twin at 0x0298F8 and updates the base
 *     at 0x0298FA
 *   - neither pass writes the twin
 *   - both passes end with move.l #$FF9584,$FFDB3C
 *   - the gate is subq.w (word, wrapping) followed by beq on the RESULT, so a
 *     latch of 1 selects the expiry path and a latch of 0 (which wraps to
 *     0xFFFF) selects the pass
 *
 * CONFIRMED (dynamic, MAME oracle):
 *   - a whole-window read tap on 0xFF9584-0xFF9B83 shows 768 read events at
 *     0x0298BE and 768 at 0x0298C0 on each subtract-pass frame, and the same
 *     pair for 0x0298F8/0x0298FA on each add-pass frame
 *   - snapshot pairs across consecutive pass frames reproduce
 *     base[n+1] == base[n] - twin[n] (byte, mod 256) for all 768 bytes, and
 *     base[n+1] == base[n] + twin[n] likewise. See road_window_test.c, whose
 *     expected buffers are the emulator's, not this code's.
 *
 * INFERRED (not claimed):
 *   - that the two gate fragments sit in the same enclosing state machine and
 *     select "subtract phase" vs "add phase" of one transform. The fragments
 *     are transcribed literally; their relationship is an inference.
 *
 * $FF954E IS NOT A DEDICATED TIMER. The listing contains 68 sites that read or
 * write that word, including 0x02449E (move.w #$78), 0x029DE4 and 0x029F8A. It
 * is modelled here as ONE cell with two roles exactly as the ROM has it, and
 * the roles are not separated. Any statement of the form "the road countdown
 * is N" must name the observation window it came from.
 *
 * THE SEAM THIS SHAPE IS FOR
 * --------------------------
 * The consumer a GL/GLES backend needs is not "an array of words". It needs the
 * window as an addressable, strided, offsettable geometry source. So the window
 * is a struct holding both live buffers plus the two latch words, and
 * stunrun_road_window_geometry() hands back a pointer/count/stride/offset
 * descriptor with the bytes already decoded big-endian to host order. A GL
 * backend uploads that directly; it never needs to know that the transform was
 * a subtract, nor that the mirror is 0x300 bytes along.
 *
 * No ROM contents are embedded or required.
 */
#ifndef STUNRUN_ROAD_WINDOW_H
#define STUNRUN_ROAD_WINDOW_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Window geometry, all OBSERVED-IN-TRACE. */
#define STUNRUN_ROAD_WINDOW_BYTES 0x300u          /* 768 */
#define STUNRUN_ROAD_WINDOW_WORDS 384u            /* 768 / 2, big-endian words */
#define STUNRUN_ROAD_WINDOW_TWIN_STRIDE 0x300u    /* +0x300 bytes = +384 words */

/* Observed maincpu addresses. The window is RAM; these are the literals the
 * passes move into A0/A1 and the literals they store back. */
#define STUNRUN_ROAD_WINDOW_BASE_ADDR 0xFF9584u
#define STUNRUN_ROAD_WINDOW_TWIN_ADDR 0xFF9884u
#define STUNRUN_ROAD_WINDOW_LATCH_ADDR 0xFF954Eu /* countdown AND divisor */
#define STUNRUN_ROAD_WINDOW_ACTIVE_ADDR 0xFFDB3Cu /* active road-buffer pointer */

/* Which buffer a geometry view reads. The transform only ever writes BASE. */
typedef enum {
    STUNRUN_ROAD_BUFFER_BASE = 0,
    STUNRUN_ROAD_BUFFER_TWIN = 1
} stunrun_road_buffer_id;

/* What a gate fragment selected. NONE means the beq took the expiry path and
 * no pass ran -- that is an outcome, not an error. */
typedef enum {
    STUNRUN_ROAD_PASS_NONE = 0,
    STUNRUN_ROAD_PASS_SUBTRACT,
    STUNRUN_ROAD_PASS_ADD
} stunrun_road_pass;

/* The window. Pointers, not storage, so a test or a renderer can point the
 * window at a fixture or at mapped RAM. */
typedef struct {
    uint8_t *base;  /* $FF9584, the pass destination and the FIFO drain source */
    uint8_t *twin;  /* $FF9884, read-only as far as these two passes are
                     * concerned; written only by the table-copy loops */
    uint16_t latch; /* $FF954E */
    uint32_t active;/* $FFDB3C */
} stunrun_road_window;

/* A geometry view over one buffer, shaped for a GL consumer: a contiguous
 * host-order word array plus the count/stride/offset triple that describes
 * where it sits inside the window. `words` is only valid until the next call
 * that writes the same buffer. */
typedef struct {
    uint16_t *words;
    size_t count;         /* words in this view, = count - offset_words */
    size_t stride_words;  /* 384: base -> mirror distance in words */
    size_t offset_words;  /* where this view starts inside its buffer */
} stunrun_road_geometry;

/* Bind both buffers and clear the two latches. NULL arguments are tolerated
 * (the window is then inert and every pass returns 0). */
void stunrun_road_window_init(stunrun_road_window *w, uint8_t *base,
                              uint8_t *twin);

/* Raw access to $FF954E. Store models any of the 68 listing sites that write
 * the word; no range or role checking is performed because the ROM does none. */
uint16_t stunrun_road_window_latch(const stunrun_road_window *w);
void stunrun_road_window_latch_store(stunrun_road_window *w, uint16_t value);

/* 0x02978E / 0x02988A: move.w D0,$FF954E with D0 = the loader's divisor
 * argument (a byte, zero-extended by `moveq #0,D0; move.b`). This OVERWRITES
 * whatever countdown was in the cell -- that is the ROM's behaviour, not a bug
 * in the model, and it is why a settled window can be observed with $FF954E==1
 * while an animating one shows a countdown. */
void stunrun_road_window_latch_load_divisor(stunrun_road_window *w,
                                            uint8_t divisor);

/* 0xFF954E read back as the loader's divisor byte. Meaningful only right after
 * a load; this is a view, not a second storage location. */
uint8_t stunrun_road_window_divisor(const stunrun_road_window *w);

/* Table-copy contract as seen through the window: base <- source verbatim,
 * twin <- source/divisor, both 0x300 bytes, then the divisor is latched.
 * (The byte arithmetic itself belongs to stunrun_road_buffer_copy_scaled; this
 * only places it in the window and does the latch store.) Returns 0 and
 * changes nothing if the divisor is 0 -- the ROM would take a divide-by-zero
 * exception there and modelling an exception is not this slice's job. */
int stunrun_road_window_load(stunrun_road_window *w, const uint8_t *source,
                             uint8_t divisor, size_t bytes);

/* 0x029916 / 0x029ADE gate fragment: subq.w #1,$FF954E ; beq -> NONE.
 *
 * The two fragments are byte-identical apart from the jsr target
 * (0x02992C -> $298A2, 0x029AEA -> $298DC), so the instruction stream itself
 * cannot say which pass it is gating. This function therefore always reports
 * STUNRUN_ROAD_PASS_SUBTRACT when it selects a pass; stunrun_road_window_step_add()
 * uses it purely as "a pass was selected" and then runs the add body. Read the
 * return value as "was a pass selected", not as "which one this caller wants".
 *
 * Note the branch tests the RESULT: pre-decrement 1 -> 0 -> NONE; every other
 * value (including 0, which wraps to 0xFFFF) -> the pass. */
stunrun_road_pass stunrun_road_window_gate(stunrun_road_window *w);

/* The two full-buffer passes. Each walks exactly STUNRUN_ROAD_WINDOW_BYTES
 * bytes, updates base only, and on success stores
 * STUNRUN_ROAD_WINDOW_BASE_ADDR into the active-pointer cell (0x0298C8 /
 * 0x029902). Returns 1 when the pass ran, 0 when the window is unusable. */
int stunrun_road_window_subtract(stunrun_road_window *w);
int stunrun_road_window_add(stunrun_road_window *w);

/* Gate + pass, i.e. one execution of the enclosing state machine fragment at
 * 0x029916 (subtract) or 0x029ADE (add). Returns 1 if a pass ran -- so a 0
 * return is the ROM's expiry path, not a failure. */
int stunrun_road_window_step_subtract(stunrun_road_window *w);
int stunrun_road_window_step_add(stunrun_road_window *w);

/* $FFDB3C as the ROM stored it. */
uint32_t stunrun_road_window_active(const stunrun_road_window *w);

/* Byte view helpers. Index 0..767, one byte, big-endian storage order. */
int stunrun_road_window_byte(const stunrun_road_window *w,
                             stunrun_road_buffer_id which, size_t index,
                             uint8_t *out);

/* Geometry view. Decodes big-endian bytes to host-order words and fills *view.
 * `which` selects base or the mirror; `offset_words` slices from the start of
 * that buffer. Returns 0 (and leaves *view zeroed) on a bad argument, an
 * offset past the end, or a count that would run past the buffer. */
int stunrun_road_window_geometry(stunrun_road_window *w,
                                 stunrun_road_buffer_id which,
                                 size_t offset_words, size_t count_words,
                                 uint16_t *words_out, size_t words_cap,
                                 stunrun_road_geometry *view);

/* Fill *view with the whole 384-word base window, offset 0. The common case for
 * a renderer: one call, then upload view->words with GL_STATIC_DRAW. */
int stunrun_road_window_geometry_base(stunrun_road_window *w, uint16_t *words_out,
                                      size_t words_cap,
                                      stunrun_road_geometry *view);

/* How far base and mirror have drifted apart: the count of differing byte
 * offsets, plus the lowest such offset. A settled just-loaded window reports 0
 * differing bytes (divisor 1); an animating window reports a large count.
 * Returns the differing byte count; *first_offset receives the lowest
 * differing offset (STUNRUN_ROAD_WINDOW_BYTES when none differ). */
size_t stunrun_road_window_divergence(const stunrun_road_window *w,
                                      size_t *first_offset);

#ifdef __cplusplus
}
#endif

#endif /* STUNRUN_ROAD_WINDOW_H */