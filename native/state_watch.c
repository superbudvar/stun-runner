/* See state_watch.h for what this log claims, what it cannot see, and why the
 * ADSP portion is transport and not execution.
 *
 * Every string in kWords[].confidence is the Confidence cell from
 * analysis/ram-map.md, copied verbatim.  The selection rule is: a row is
 * included only when that cell carries a CONFIRMED / DYNAMIC / RESOLVED token.
 * Rows excluded on purpose, so that a later reader does not "helpfully" add
 * them:
 *
 *   0xFF9544  STATIC-RESOLVED-OBJECT-FLAG            no dynamic observation
 *   0xFF956C  STATIC-RESOLVED-RECORD-CACHE           resolved as a comparison
 *                                                     cache, explicitly
 *                                                     NOT-CRAFT-STATUS; not a
 *                                                     thing the player sees
 *   0xFFDB4A  STATIC-CANDIDATE                       candidate only
 *   0xFFDAEE  OBSERVED-NEGATIVE                      recorded as not moving
 *   0xFFDD04  STATIC-CANDIDATE + OBSERVED-IN-TRACE   exact ownership unresolved
 *   0xFFDD0C  STATIC-CANDIDATE                       candidate only
 *   0xFFDD4E  STATIC-CANDIDATE + SNAPSHOT-OBSERVED    writer not isolated
 *   0xFFDD50  STATIC-CANDIDATE + OBSERVED-NEGATIVE   remained zero
 *   0xFFDD1A  OBSERVED-IN-TRACE                      axis/axis roles unresolved
 *   0xFFDD52  WRITER-TRACE-OBSERVED                   individual meanings
 *                                                     unresolved
 *
 * The ADSP upload boundaries and the control-window table are NOT restated
 * here.  They come from reproduction/adsp/adsp_control_seq.h, which is the
 * single canonical implementation for that contract; this file calls
 * stunrun_adsp_upload_state_at() and stunrun_adsp_install_frame_ready() and
 * labels each op through stunrun_adsp_control_ops().  Re-deriving them here
 * would be exactly the drift the single-canonical-implementation rule exists to
 * prevent.
 */
#include "state_watch.h"

#include "adsp_control_seq.h"
#include "fake_ports.h"

#include <stdio.h>

/* ── The watched-word table ───────────────────────────────────────────────
 * Least-expected-change FIRST, so `head` of a frame's block shows the rare
 * events instead of the two per-frame tickers (irq-tick at the end changes
 * about four times per frame; animation-counter once).  Every address is a
 * 68010 work-RAM word read big-endian through stunrun_mem_read16_be(). */
static const stunrun_state_watch_word_t kWords[] = {
    /* ram-map: `0xFF9550` broad game-state/dispatch latch */
    { 0xFF9550u, "game-state",
      "STATIC-STATE-DISPATCH-CONFIRMED; ROAD-ROLE-SECONDARY" },
    /* ram-map: `0xFF9578-0xFF9579` state/index word */
    { 0xFF9578u, "track-index",
      "DYNAMIC-WRITER-CONFIRMED; TERMINAL-CLEANUP-CONFIRMED; "
      "DISPLAYED TRACK LABEL UNPROVEN" },
    /* ram-map: `0xFF9532-0xFF9535` live score accumulator.  The low half is
     * where the observed 50/500-point writes land; the high half is carried
     * anyway so a jump past 0xFFFF cannot hide. */
    { 0xFF9534u, "score", "DYNAMIC-SCORE-CONFIRMED" },
    { 0xFF9532u, "score-hi", "DYNAMIC-SCORE-CONFIRMED" },
    /* ram-map: `0xFF9568-0xFF956B` displayed time-remaining mechanism */
    { 0xFF956Au, "time-remaining", "DYNAMIC-TIME-REMAINING-CONFIRMED" },
    { 0xFF9568u, "time-remaining-hi",
      "DYNAMIC-TIME-REMAINING-CONFIRMED" },
    /* ram-map: `0xFF957A-0xFF957B` input-state word */
    { 0xFF957Au, "input-state", "DYNAMIC-INPUT-STATE-CONFIRMED" },
    /* ram-map: `0xFFDD02` object-hit/progression counter */
    { 0xFFDD02u, "object-hits", "DYNAMIC-OBJECT-HIT-COUNTER-CONFIRMED" },
    /* ram-map: `0xFF954E` road-buffer countdown and divisor latch */
    { 0xFF954Eu, "road-countdown",
      "STATIC + DYNAMIC-TRACE; TRANSFORM-TIMER-CANDIDATE" },
    /* ram-map: `0xFF9564-0xFF9567` elapsed-update counter candidate */
    { 0xFF9566u, "elapsed-update", "STATIC + OBSERVED-IN-TRACE" },
    { 0xFF9564u, "elapsed-update-hi", "STATIC + OBSERVED-IN-TRACE" },
    /* ram-map: `0xFFDD16-0xFFDD17` speed/velocity candidate */
    { 0xFFDD16u, "speed-candidate", "STATIC + OBSERVED-IN-TRACE" },
    /* ram-map: `0xFF9582` 5-bit cyclic animation/state counter */
    { 0xFF9582u, "animation-counter", "STATIC-RESOLVED-ANIMATION-COUNTER" },
    /* ram-map: `FFFF8014` (bus-visible `0xFF8014`) interrupt-driven global
     * tick, `addq.l #1` at PC 0x0222DE.
     *
     * MEASURED 2026-10-16: it is the low HALF that moves.  A build that
     * watched 0xFF8014 alone reported zero changes over 600 frames while, in
     * the SAME process, STUNRUN_WATCH_WRITE reported 966 writes from PC
     * 0x0222DE reaching 0x03C4.  The longword at 0xFF8014 lays out
     * big-endian, so the incrementing word is 0xFF8016 and 0xFF8014 stays 0.
     * The ram-map row names the LONGWORD's address; that is not the word that
     * moves.  Same trap as score (0xFF9534) and time-remaining (0xFF956A). */
    { 0xFF8016u, "irq-tick",
      "STATIC + WRITER-TRACE-CONFIRMED; TIMING-SOURCE, NOT TRACK POSITION" },
    { 0xFF8014u, "irq-tick-hi",
      "STATIC + WRITER-TRACE-CONFIRMED; TIMING-SOURCE, NOT TRACK POSITION" }
};

#define STUNRUN_STATE_WORD_COUNT (sizeof(kWords) / sizeof(kWords[0]))

_Static_assert(STUNRUN_STATE_WORD_COUNT <= STUNRUN_STATE_WATCH_MAX_WORDS,
               "watched-word table exceeds STUNRUN_STATE_WATCH_MAX_WORDS");

/* The confidence carried on every ADSP-transport line.  This is
 * adsp_control_seq.h's own provenance, restated: MAME confirms the handler
 * semantics (harddriv_m.cpp:685-765) and the per-address counts and all-zero
 * data are OBSERVED-IN-TRACE in two independent 600-frame runs, no-input and
 * coin/start identical.  It says nothing about what the DSP did with them. */
#define STUNRUN_STATE_ADSP_CONFIDENCE                                     \
    "OBSERVED-IN-TRACE: two independent 600-frame title-path runs "     \
    "(no-input and coin/start identical), MAME handler semantics at "    \
    "harddriv_m.cpp:685-765; this is 68010 transport, not ADSP execution"

size_t stunrun_state_watch_word_count(void)
{
    return STUNRUN_STATE_WORD_COUNT;
}

const stunrun_state_watch_word_t *stunrun_state_watch_words(void)
{
    return kWords;
}

int stunrun_state_watch_find(const char *name)
{
    size_t i;
    if (name == NULL)
        return -1;
    for (i = 0; i < STUNRUN_STATE_WORD_COUNT; i++) {
        const char *a = kWords[i].name;
        const char *b = name;
        while (*a != '\0' && *a == *b) {
            a++;
            b++;
        }
        if (*a == '\0' && *b == '\0')
            return (int)i;
    }
    return -1;
}

void stunrun_state_watch_init(stunrun_state_watch_t *watch)
{
    if (watch == NULL)
        return;
    watch->words = kWords;
    watch->word_count = STUNRUN_STATE_WORD_COUNT;
    watch->reads_failed = 0;
    watch->samples = 0;
    watch->seed_frame = 0;
    watch->seeded = 0;
    watch->upload_seeded = 0;
    watch->upload_last = STUNRUN_ADSP_UPLOAD_EMPTY;
    watch->adsp_count = 0;
    watch->adsp_dropped = 0;
    watch->adsp_reported = 0;
    watch->upload_transitions = 0;
    {
        size_t i;
        for (i = 0; i < STUNRUN_STATE_WORD_COUNT; i++) {
            watch->last_value[i] = 0;
            watch->changes[i] = 0;
        }
    }
}

void stunrun_state_watch_enable(const stunrun_state_watch_t *watch)
{
    size_t i;
    if (watch == NULL)
        return;
    printf("shell: state-log=1 words=%u sample=frame-boundary "
           "seed-frame-prints=none order=rarest-change-first\n",
           (unsigned)watch->word_count);
    /* Printed so that a word which never produces a line afterwards is
     * readable as "watched and flat" rather than "not watched". */
    for (i = 0; i < watch->word_count; i++) {
        /* %06X, not %08X: the shell's own watch-word pads to eight digits and
         * prints 0x00FF9550, which matches neither analysis/ram-map.md nor the
         * 68010's 24-bit bus.  These addresses are copied out of ram-map, so
         * they print the way ram-map writes them and grep the same. */
        printf("shell: state-log name=%s addr=0x%06X confidence=\"%s\"\n",
               watch->words[i].name, watch->words[i].address,
               watch->words[i].confidence);
    }
    printf("shell: state-log note=adsp-lines-are-68010-transport-only "
           "no-adsp-2100-is-executed\n");
}

void stunrun_state_watch_note_adsp_write(stunrun_state_watch_t *watch,
                                         uint32_t address, uint16_t data,
                                         uint32_t pc, unsigned width_bits)
{
    stunrun_state_watch_adsp_event_t *ev;
    if (watch == NULL)
        return;
    if (watch->adsp_count >= STUNRUN_STATE_WATCH_ADSP_RING) {
        watch->adsp_dropped++;
        return;
    }
    ev = &watch->adsp[watch->adsp_count++];
    ev->address = address;
    ev->data = data;
    ev->pc = pc;
    ev->width_bits = width_bits;
}

/* ── ADSP transport classification ───────────────────────────────────────
 * Every branch here describes something the 68010 did.  The three outcomes
 * are: a recorded control op (labelled from adsp_control_seq.h), the recorded
 * IRQ-clear address, or something NOT in the recorded set — which is printed
 * rather than dropped, because a silently swallowed control-window write is
 * indistinguishable from "the 68010 never did that". */
static void report_adsp_event(const stunrun_state_watch_adsp_event_t *ev,
                              unsigned frame)
{
    int op;

    if (ev->address == STUNRUN_ADSP_IRQ_CLEAR_ADDR) {
        const uint32_t *pcs = stunrun_adsp_irq_clear_pcs();
        printf("shell: state frame=%u kind=adsp-irq-clear addr=0x%06X "
               "data=0x%04X width=%u bus-pc=0x%06X reference-writers="
               "0x%06X,0x%06X note=\"bus-pc is the PC inside the bus "
               "callback, not the instruction address\" confidence=\"%s\"\n",
               frame, ev->address, ev->data, ev->width_bits, ev->pc,
               pcs[0], pcs[1], STUNRUN_STATE_ADSP_CONFIDENCE);
        return;
    }
    op = stunrun_adsp_control_lookup(ev->address);
    if (op >= 0) {
        const stunrun_adsp_control_op_t *t = stunrun_adsp_control_ops();
        printf("shell: state frame=%u kind=adsp-control addr=0x%06X "
               "label=%s data=0x%04X width=%u bus-pc=0x%06X "
               "recorded-writes-in-600f=%u confidence=\"%s\"\n",
               frame, ev->address, t[op].handler, ev->data, ev->width_bits,
               ev->pc, t[op].expected_writes, STUNRUN_STATE_ADSP_CONFIDENCE);
        return;
    }
    if (ev->address >= 0x818000u && ev->address <= 0x81801Fu) {
        /* Inside the recorded control window but not one of its eleven
         * addresses.  Named `unrecorded`, never `unknown`: we know exactly
         * what happened, and it is not something the MAME tap recorded. */
        printf("shell: state frame=%u kind=adsp-control addr=0x%06X "
               "label=unrecorded data=0x%04X width=%u bus-pc=0x%06X "
               "note=\"in 0x818000-0x81801F but not in "
               "adsp_control_ops()\" confidence=\"UNKNOWN\"\n",
               frame, ev->address, ev->data, ev->width_bits, ev->pc);
        return;
    }
    printf("shell: state frame=%u kind=adsp-window addr=0x%06X data=0x%04X "
           "width=%u bus-pc=0x%06X note=\"outside the recorded "
           "0x818000-0x81801F control set and not the IRQ-clear pair\" "
           "confidence=\"UNKNOWN\"\n",
           frame, ev->address, ev->data, ev->width_bits, ev->pc);
}

static void report_upload_state(stunrun_state_watch_t *watch, unsigned frame)
{
    int state = stunrun_adsp_upload_state_at(frame);

    if (watch->upload_seeded && state == watch->upload_last)
        return;
    watch->upload_transitions++;
    switch (state) {
    case STUNRUN_ADSP_UPLOAD_EMPTY:
        printf("shell: state frame=%u kind=adsp-upload state=empty words=0 "
               "empty-through-frame=%u confidence=\"OBSERVED-IN-TRACE + "
               "SNAPSHOT-DIFF: program RAM empty through frame %u\"\n",
               frame, STUNRUN_ADSP_UPLOAD_LAST_ZERO_FRAME,
               STUNRUN_ADSP_UPLOAD_LAST_ZERO_FRAME);
        break;
    case STUNRUN_ADSP_UPLOAD_POPULATING:
        printf("shell: state frame=%u kind=adsp-upload state=populating "
               "words=%u first-populated-frame=%u complete-frame=%u "
               "confidence=\"OBSERVED-IN-TRACE + SNAPSHOT-DIFF\"\n",
               frame, STUNRUN_ADSP_UPLOAD_FIRST_POPULATED_WORDS,
               STUNRUN_ADSP_UPLOAD_FIRST_POPULATED_FRAME,
               STUNRUN_ADSP_UPLOAD_COMPLETE_FRAME);
        break;
    case STUNRUN_ADSP_UPLOAD_COMPLETE:
        printf("shell: state frame=%u kind=adsp-upload state=complete words=%u "
               "complete-frame=%u confidence=\"OBSERVED-IN-TRACE + "
               "SNAPSHOT-DIFF: 2718-word image present\"\n",
               frame, STUNRUN_ADSP_UPLOAD_COMPLETE_WORDS,
               STUNRUN_ADSP_UPLOAD_COMPLETE_FRAME);
        break;
    default:
        printf("shell: state frame=%u kind=adsp-upload state=unknown-%d "
               "confidence=\"UNKNOWN\"\n", frame, state);
        break;
    }
    watch->upload_last = state;
    watch->upload_seeded = 1;
}

void stunrun_state_watch_frame(stunrun_state_watch_t *watch,
                               const stunrun_fake_ports_t *ports,
                               unsigned frame)
{
    size_t i;

    if (watch == NULL)
        return;

    report_upload_state(watch, frame);

    /* Drain the transport buffer first: these happened DURING the frame the
     * loop just ran, so they belong to `frame`, not to the next one. */
    for (i = 0; i < watch->adsp_count; i++) {
        report_adsp_event(&watch->adsp[i], frame);
        watch->adsp_reported++;
    }
    watch->adsp_count = 0;

    if (ports == NULL)
        return;

    watch->samples++;
    for (i = 0; i < watch->word_count; i++) {
        uint16_t value = 0;
        /* stunrun_mem_read16_be() returns NON-ZERO ON SUCCESS (fake_ports.c).
         * Getting this backwards is not hypothetical: the shell's own
         * watch-word readback had `!= 0 -> continue`, which skips the word
         * exactly when the read worked, so the feature was dead.  See
         * docs/mc68010-boot-findings.md section 77.4. */
        if (stunrun_mem_read16_be(ports, watch->words[i].address,
                                  &value) == 0) {
            watch->reads_failed++;
            continue;
        }
        if (!watch->seeded) {
            /* Baseline: adopt whatever is there and say nothing.  Every
             * `(was ...)` in the log is then a value an earlier frame held,
             * which is what makes the first real line readable. */
            watch->last_value[i] = value;
            continue;
        }
        if (value == watch->last_value[i])
            continue;
        printf("shell: state frame=%u kind=word name=%s addr=0x%06X "
               "value=0x%04X (was 0x%04X) confidence=\"%s\"\n",
               frame, watch->words[i].name, watch->words[i].address,
               (unsigned)value, (unsigned)watch->last_value[i],
               watch->words[i].confidence);
        watch->last_value[i] = value;
        watch->changes[i]++;
    }
    if (!watch->seeded) {
        watch->seeded = 1;
        watch->seed_frame = frame;
    }
}

void stunrun_state_watch_summary(const stunrun_state_watch_t *watch)
{
    size_t i;
    unsigned long total = 0;

    if (watch == NULL)
        return;
    for (i = 0; i < watch->word_count; i++) {
        printf("shell: state-log summary name=%s addr=0x%06X changes=%lu\n",
               watch->words[i].name, watch->words[i].address,
               watch->changes[i]);
        total += watch->changes[i];
    }
    printf("shell: state-log summary words=%u changes=%lu "
           "adsp-events=%lu adsp-dropped=%lu upload-transitions=%lu "
           "ram-read-failures=%lu%s\n",
           (unsigned)watch->word_count, total, watch->adsp_reported,
           watch->adsp_dropped, watch->upload_transitions,
           watch->reads_failed,
           watch->adsp_dropped != 0 ? " WARNING=events-lost" : "");
}