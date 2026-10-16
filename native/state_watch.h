/* Labelled game-state / event log for the native shell.
 *
 * WHAT THIS IS.  A frame-stamped log of named 68010 work-RAM words plus the
 * 68010-side ADSP transport, printed on CHANGE.  It exists because
 * `shell: watch-word frame=… addr=0xFF9550 value=0x0004` cannot be matched
 * against what a person sees on screen: a bare address says nothing about
 * which game quantity it is.  Every entry here carries the Confidence string
 * verbatim from analysis/ram-map.md, so no line of this log can claim more
 * than the recorded evidence supports.
 *
 * ─────────────────────────────────────────────────────────────────────────
 * THE ADSP HONESTY CONSTRAINT — READ THIS BEFORE CITING THE LOG
 * ─────────────────────────────────────────────────────────────────────────
 * WE DO NOT EXECUTE THE ADSP-2100.  There is no ADSP instruction decoder in
 * this tree, no ADSP program counter, and no ADSP bus.  `musashi_cpu.c` keeps
 * `adsp_pgm_ram`, which is the 68010's *view* of the DSP's program RAM, and
 * its control window 0x818000-0x81FFFF is DROPPED — routed, then discarded.
 * So nothing here can observe the DSP:
 *
 *   - no SOMLATCH emission, because nothing decodes ADSP instructions;
 *   - no GINT / SFLAG / SPMODE out of the DSP, because no DSP runs;
 *   - no ADSP PC, no ADSP cycle count, no ADSP register state.
 *
 * What IS observable is the *transport the 68010 performs*: its own stores into
 * the ADSP control window, the IRQ-clear pair at 0x818060, and the frame
 * boundaries of the program upload (which are recorded frame-indexed facts, not
 * measurements of a running DSP).  Every ADSP-flavoured line below is
 * `kind=adsp-control`, `kind=adsp-irq-clear`, `kind=adsp-upload` or
 * `kind=adsp-install`, and every one of them describes the 68010, never the
 * DSP.  MAME is the authority for what the DSP then did with them.
 *
 * ─────────────────────────────────────────────────────────────────────────
 * THE SAMPLING LIMITATION — WHY THE LOG CAN MISS SOMETHING YOU SAW
 * ─────────────────────────────────────────────────────────────────────────
 * The word sampler runs ONCE PER FRAME AT THE FRAME BOUNDARY.  A value that is
 * set and cleared inside a single frame is therefore INVISIBLE: the two samples
 * either side of it agree, so no change is announced.  That is not a heuristic
 * limitation of this module, it is the direct consequence of sampling once per
 * frame — and it is the reason a field here can read "never changed" over a
 * run in which the game plainly did something with it.  The
 * `STUNRUN_WATCH_WRITE` write watch is the complement: it counts EVERY store
 * with its writing PC, including inside one frame, but it prints no meaning.
 * Use both when a field here is unexpectedly flat.
 *
 * Frame 0 is the BASELINE sample.  It seeds `last_value` from live RAM and
 * prints no word lines, so every `(was …)` value in the log is a value some
 * earlier frame actually held.  The ADSP transport stream has no such seed: an
 * event observed during frame 0 is reported at frame 0, because an event is an
 * event and not a difference between two samples.
 *
 * The upload-milestone stream prints its initial state on the first sample and
 * a line per transition after that.  Those bounds come from
 * reproduction/adsp/adsp_control_seq.h and are reused here, not re-derived.
 *
 * No ROM contents are embedded or required, and nothing here writes memory,
 * registers, latches or bus events.  It reads work RAM and prints.
 */
#ifndef STUNRUN_NATIVE_STATE_WATCH_H
#define STUNRUN_NATIVE_STATE_WATCH_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Defined in fake_ports.h; forward-declared so this header does not pull the
 * whole native machine boundary in. */
typedef struct stunrun_fake_ports stunrun_fake_ports_t;

/* Upper bound on the watched-word table. The table is a compile-time constant
 * in state_watch.c; this only sizes the per-entry state arrays. */
#define STUNRUN_STATE_WATCH_MAX_WORDS 32u

/* ADSP transport events buffered between one frame boundary and the next.
 * 4096 is ~55x the 73 control writes a 600-frame title-path run performs, so
 * the bound is not reached by the recorded contract; if it ever IS reached the
 * overflow is counted and reported rather than silently truncating. */
#define STUNRUN_STATE_WATCH_ADSP_RING 4096u

/* One watched work-RAM word.
 *
 * `name` is the label used in the log.  `confidence` is the Confidence cell
 * from analysis/ram-map.md, copied character for character -- including its
 * "; NOT-TRACK-POSITION" style negations, which are the reason the log is
 * allowed to exist at all.  A longword row in ram-map.md contributes TWO
 * entries, `-hi` and the bare low half, because which half carries the value
 * is a measurement (see docs/mc68010-boot-findings.md section 77.5) and
 * watching both costs nothing: a half that never changes prints no lines. */
typedef struct stunrun_state_watch_word {
    uint32_t address;
    const char *name;
    const char *confidence;
} stunrun_state_watch_word_t;

/* The table, least-expected-change first so that `head` of a frame's block
 * shows the rare events rather than the two per-frame tickers. */
size_t stunrun_state_watch_word_count(void);
const stunrun_state_watch_word_t *stunrun_state_watch_words(void);

/* Index of the entry named `name`, or -1. */
int stunrun_state_watch_find(const char *name);

/* One observed MC68010 store into the ADSP window (0x818000-0x81FFFF).  Fed
 * from native/musashi_cpu.c's observer; `pc` is the PC as seen INSIDE the bus
 * callback, which is the instruction address plus the already-consumed
 * prefetch/extension words — NOT the instruction address.  See
 * musashi_cpu.h. */
typedef struct stunrun_state_watch_adsp_event {
    uint32_t address;
    uint16_t data;
    uint32_t pc;
    unsigned width_bits;
} stunrun_state_watch_adsp_event_t;

typedef struct stunrun_state_watch {
    const stunrun_state_watch_word_t *words;
    size_t word_count;
    uint16_t last_value[STUNRUN_STATE_WATCH_MAX_WORDS];
    unsigned long changes[STUNRUN_STATE_WATCH_MAX_WORDS];
    unsigned long reads_failed;   /* addresses the RAM model refused */
    unsigned long samples;
    unsigned long seed_frame;
    int seeded;

    int upload_seeded;
    int upload_last;              /* stunrun_adsp_upload_state_t */

    stunrun_state_watch_adsp_event_t adsp[STUNRUN_STATE_WATCH_ADSP_RING];
    size_t adsp_count;            /* buffered, drained at the next frame */
    unsigned long adsp_dropped;   /* lost to a full ring */
    unsigned long adsp_reported;  /* printed */

    unsigned long upload_transitions;
} stunrun_state_watch_t;

/* Zero the context and bind it to the built-in table. */
void stunrun_state_watch_init(stunrun_state_watch_t *watch);

/* Print the enable header and one line per watched word, each with its
 * confidence string.  This is what makes a field with no later lines
 * interpretable as "never changed" rather than "never watched". */
void stunrun_state_watch_enable(const stunrun_state_watch_t *watch);

/* Record one observed ADSP-window store.  `watch` may be NULL (no-op). */
void stunrun_state_watch_note_adsp_write(stunrun_state_watch_t *watch,
                                         uint32_t address, uint16_t data,
                                         uint32_t pc, unsigned width_bits);

/* Sample at the frame boundary.  Prints, in this order: the upload-milestone
 * transition (if any), every ADSP transport event observed since the previous
 * call, then every watched word whose value changed. */
void stunrun_state_watch_frame(stunrun_state_watch_t *watch,
                               const stunrun_fake_ports_t *ports,
                               unsigned frame);

/* Per-word change counts plus the transport tallies, so "this field never
 * changed" is a number rather than an absence. */
void stunrun_state_watch_summary(const stunrun_state_watch_t *watch);

#ifdef __cplusplus
}
#endif

#endif /* STUNRUN_NATIVE_STATE_WATCH_H */