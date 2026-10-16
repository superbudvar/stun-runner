/* state_watch_test.c -- the OBSERVABLE OUTPUT of the labelled state/event log.
 *
 * The feature's product is a line a person reads next to a running game, so
 * this asserts the lines, not the bookkeeping: it captures stdout and
 * compares it against literals written out by hand from state_watch.c's format
 * strings.  There is deliberately no side channel -- a callback or a returned
 * struct would let a broken printer pass, which is the exact failure this
 * feature exists to end (`shell: watch-word addr=0xFF9550 value=0x0004` told
 * the user nothing they could match against the screen).
 *
 * WHAT IS ASSERTED, AND WHY EACH ONE IS HERE
 *
 *  A. The enable header names EVERY watched field with its ram-map confidence
 *     string, and the count of `state-log name=` lines equals the table size.
 *     This is what makes "no line for track-index later" readable as
 *     "track-index was flat" rather than "track-index was not watched".  It
 *     also fails if an entry is duplicated or dropped from the header.
 *
 *  B. Every table entry is a live word in this memory model, and inside the
 *     32 KiB mapped window.  Poking a distinct value at each address and
 *     reading it back rules out a typo'd address quietly reading open bus;
 *     the bounds check rules out one past the end of the window, where a read
 *     wraps to address 0 (fake_ports.h records that once producing 376 284
 *     bogus "RAM" bytes).
 *
 *  C. Every confidence string carries a confirmed/dynamic token.  That is the
 *     selection rule state_watch.c documents, asserted: a STATIC-CANDIDATE or
 *     OBSERVED-NEGATIVE row added to the table fails here.
 *
 *  D. The baseline frame prints no word lines, and an UNCHANGED image prints
 *     none on any later frame.  This is the "does not spam" property, and it
 *     is the one a naive implementation gets wrong by logging every field on
 *     every frame.
 *
 *  E. One synthetic change set reproduces an EXACT golden: four ADSP transport
 *     lines in insertion order, then the three changed words in table order,
 *     each carrying its previous value.  The comparison is whole-buffer
 *     equality, so a missing line, an extra line, a reordered line, a wrong
 *     label, a wrong confidence, or a wrong `(was ...)` all fail.
 *
 *  F. THE NEGATIVE CONTROLS, which are what make E mean anything.  A comparator
 *     that cannot fail is not a check, and this repository has shipped three
 *     tests that passed for the wrong reason.  So the same comparator is shown
 *     REJECTING a golden one nibble different, a golden with one line deleted
 *     and a golden with one line duplicated, before the positive result is
 *     accepted.  Every positive comparison is accompanied by an exact expected
 *     line count, and that count is non-zero, so a control which matched
 *     nothing cannot read as a pass.
 *
 *  G. The ring bound: overflowing the ADSP event buffer is counted and
 *     announced.  The 73-write recorded contract is ~55x smaller than the
 *     buffer, so an overflow in a normal run is a bug, not a tolerance.
 *
 *  H. The upload milestones agree with adsp_control_seq.c -- the single
 *     canonical implementation of those bounds -- and their transitions print
 *     at the recorded frames.
 *
 *  I. The log only reads: the whole RAM image is byte-compared before and
 *     after a sampling pass, so "observation only" is checked, not promised.
 *
 * No commercial ROMs and no MAME: this is a synthetic RAM image plus a
 * hand-fed ADSP transport stream.
 */
#define _POSIX_C_SOURCE 200809L   /* fileno(), dup(), dup2(): strict C99 hides them */

#include "state_watch.h"
#include "fake_ports.h"
#include "adsp_control_seq.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_CAPTURE 262144u

static unsigned checks;
static unsigned failures;

#define CHECK(expr) do { \
    checks++; \
    if (!(expr)) { \
        fprintf(stderr, "FAIL: %s (line %d)\n", #expr, __LINE__); \
        failures++; \
    } \
} while (0)

#define CHECK_EQ(actual, expected) do { \
    long long a_ = (long long)(actual); \
    long long e_ = (long long)(expected); \
    checks++; \
    if (a_ != e_) { \
        fprintf(stderr, "FAIL: %s == %s (got %lld, want %lld; line %d)\n", \
                #actual, #expected, a_, e_, __LINE__); \
        failures++; \
    } \
} while (0)

/* ── stdout capture ──────────────────────────────────────────────────────
 * The decoded line IS this feature's output, so the test asserts on what a
 * human would see.  No side channel: a callback would let a broken printer
 * pass. */
static int saved_stdout = -1;
static FILE *capture_file;

static void capture_begin(void)
{
    fflush(stdout);
    saved_stdout = dup(1);
    capture_file = tmpfile();
    dup2(fileno(capture_file), 1);
}

static void capture_end(char *out, size_t capacity)
{
    size_t n;
    fflush(stdout);
    dup2(saved_stdout, 1);
    close(saved_stdout);
    saved_stdout = -1;
    rewind(capture_file);
    n = fread(out, 1, capacity - 1u, capture_file);
    out[n] = '\0';
    fclose(capture_file);
    capture_file = NULL;
}

static unsigned count_of(const char *haystack, const char *needle)
{
    unsigned n = 0;
    size_t len = strlen(needle);
    const char *p = haystack;
    while ((p = strstr(p, needle)) != NULL) {
        n++;
        p += len;
    }
    return n;
}

static int contains(const char *haystack, const char *needle)
{
    return strstr(haystack, needle) != NULL;
}

/* Whole-buffer equality.  This is the comparator the negative controls in
 * test_golden_comparator_has_teeth() are about: were it a substring search,
 * those controls would pass vacuously. */
static int exact_equals(const char *haystack, const char *golden)
{
    return strcmp(haystack, golden) == 0;
}

/* The confidence string every ADSP transport line carries, restated here so
 * the golden is written out in full rather than assembled from the module. */
#define ADSP_CONF \
    "OBSERVED-IN-TRACE: two independent 600-frame title-path runs " \
    "(no-input and coin/start identical), MAME handler semantics at " \
    "harddriv_m.cpp:685-765; this is 68010 transport, not ADSP execution"

/* ── The synthetic scenario and the golden it must produce ───────────────
 * Frame 0 seeds from an all-zero RAM image.  Frames 1 and 2 change nothing.
 * During frame 3: four ADSP transport events are fed, and three words move. */
static const char kGoldenFrame3[] =
    "shell: state frame=3 kind=adsp-control addr=0x818002 label=LEDs "
    "data=0x0000 width=8 bus-pc=0x001234 recorded-writes-in-600f=54 "
    "confidence=\"" ADSP_CONF "\"\n"
    "shell: state frame=3 kind=adsp-irq-clear addr=0x818060 data=0x0000 "
    "width=16 bus-pc=0x02C238 reference-writers=0x021426,0x02C234 "
    "note=\"bus-pc is the PC inside the bus callback, not the instruction "
    "address\" confidence=\"" ADSP_CONF "\"\n"
    "shell: state frame=3 kind=adsp-control addr=0x818012 label=unrecorded "
    "data=0x0000 width=16 bus-pc=0x0012A0 "
    "note=\"in 0x818000-0x81801F but not in adsp_control_ops()\" "
    "confidence=\"UNKNOWN\"\n"
    "shell: state frame=3 kind=adsp-window addr=0x818040 data=0x0000 width=8 "
    "bus-pc=0x0012B0 note=\"outside the recorded 0x818000-0x81801F control "
    "set and not the IRQ-clear pair\" confidence=\"UNKNOWN\"\n"
    "shell: state frame=3 kind=word name=track-index addr=0xFF9578 "
    "value=0x0003 (was 0x0000) "
    "confidence=\"DYNAMIC-WRITER-CONFIRMED; TERMINAL-CLEANUP-CONFIRMED; "
    "DISPLAYED TRACK LABEL UNPROVEN\"\n"
    "shell: state frame=3 kind=word name=score addr=0xFF9534 value=0x0032 "
    "(was 0x0000) confidence=\"DYNAMIC-SCORE-CONFIRMED\"\n"
    "shell: state frame=3 kind=word name=time-remaining addr=0xFF956A "
    "value=0x215C (was 0x0000) "
    "confidence=\"DYNAMIC-TIME-REMAINING-CONFIRMED\"\n";

/* Frame 0's upload-milestone line: printed once, because the milestone stream
 * announces its initial state rather than seeding silently. */
static const char kGoldenFrame0[] =
    "shell: state frame=0 kind=adsp-upload state=empty words=0 "
    "empty-through-frame=407 confidence=\"OBSERVED-IN-TRACE + SNAPSHOT-DIFF: "
    "program RAM empty through frame 407\"\n";

static void poke(stunrun_fake_ports_t *ports, uint32_t address, uint16_t value)
{
    CHECK(stunrun_mem_write16_be(ports, address, value) != 0);
}

/* Frames 0-3 of the scenario.  Three captures, because the silence of the
 * unchanged frames is itself an assertion and must not be clobbered by the
 * frame that changes. */
static void run_scenario(char *out0, size_t cap0, char *out12, size_t cap12,
                         char *out3, size_t cap3,
                         stunrun_state_watch_t *w, stunrun_fake_ports_t *ports)
{
    stunrun_ports_init(ports);
    stunrun_state_watch_init(w);

    memset(out0, 0, cap0);
    memset(out12, 0, cap12);
    memset(out3, 0, cap3);

    capture_begin();
    stunrun_state_watch_frame(w, ports, 0u);
    capture_end(out0, cap0);

    /* Unchanged frames.  Nothing may be printed on either. */
    capture_begin();
    stunrun_state_watch_frame(w, ports, 1u);
    stunrun_state_watch_frame(w, ports, 2u);
    capture_end(out12, cap12);

    /* The ADSP transport the 68010 performs during frame 3.  Four shapes:
     *   0x818002  a recorded control op (the LEDs)
     *   0x818060  the recorded IRQ-clear pair
     *   0x818012  inside 0x818000-0x81801F but NOT one of the eleven recorded
     *             addresses.  Printed as `unrecorded`, never swallowed: a
     *             silently dropped control write is indistinguishable from
     *             "the 68010 never did that", which is how a whole input
     *             sweep once looked like clean null results.
     *   0x818040  inside the routed window but outside the recorded set
     */
    stunrun_state_watch_note_adsp_write(w, 0x818002u, 0x0000u, 0x001234u, 8u);
    stunrun_state_watch_note_adsp_write(w, 0x818060u, 0x0000u, 0x02C238u, 16u);
    stunrun_state_watch_note_adsp_write(w, 0x818012u, 0x0000u, 0x0012A0u, 16u);
    stunrun_state_watch_note_adsp_write(w, 0x818040u, 0x0000u, 0x0012B0u, 8u);

    /* Three words change: a track index, a 50-point score award, and the
     * time-remaining LOW half at the ROM seed value 0x215C. */
    poke(ports, 0xFF9578u, 0x0003u);
    poke(ports, 0xFF9534u, 0x0032u);
    poke(ports, 0xFF956Au, 0x215Cu);

    capture_begin();
    stunrun_state_watch_frame(w, ports, 3u);
    capture_end(out3, cap3);
}

/* ── A/B/C: the enable header and the table itself ──────────────────────── */
static void test_enable_header(void)
{
    const stunrun_state_watch_word_t *words = stunrun_state_watch_words();
    const size_t n = stunrun_state_watch_word_count();
    stunrun_state_watch_t w;
    stunrun_fake_ports_t ports;
    char out[MAX_CAPTURE];
    char line[512];
    size_t i;

    CHECK(n > 0u);
    CHECK(n <= STUNRUN_STATE_WATCH_MAX_WORDS);
    /* Pinned, because every "nothing was printed" check below is vacuous
     * against an empty table.  A new confirmed row must come with a new
     * number here and a new golden. */
    CHECK_EQ(n, 15u);

    stunrun_ports_init(&ports);
    stunrun_state_watch_init(&w);
    /* init() binds the built-in table, so a context the caller did not wire
     * up cannot sample zero words and make every silence check vacuous. */
    CHECK(w.words == words);
    CHECK_EQ(w.word_count, n);
    CHECK_EQ(w.seeded, 0);

    memset(out, 0, sizeof(out));
    capture_begin();
    stunrun_state_watch_enable(&w);
    capture_end(out, sizeof(out));

    CHECK(contains(out,
          "shell: state-log=1 words=15 sample=frame-boundary "
          "seed-frame-prints=none order=rarest-change-first\n"));
    /* The honesty constraint has to be in the log the user reads, not only
     * in the header comment they will not open. */
    CHECK(contains(out,
          "shell: state-log note=adsp-lines-are-68010-transport-only "
          "no-adsp-2100-is-executed\n"));
    /* Exactly one header line per table entry: a duplicated or dropped entry
     * fails here rather than silently downstream. */
    CHECK_EQ(count_of(out, "shell: state-log name="), n);
    CHECK_EQ(count_of(out, "shell: state-log=1 "), 1u);
    CHECK_EQ(count_of(out, "shell: state-log note="), 1u);

    for (i = 0; i < n; i++) {
        snprintf(line, sizeof(line),
                 "shell: state-log name=%s addr=0x%06X confidence=\"%s\"\n",
                 words[i].name, words[i].address, words[i].confidence);
        CHECK(contains(out, line));
        CHECK_EQ(stunrun_state_watch_find(words[i].name), (int)i);
        CHECK(words[i].name[0] != '\0');
        CHECK(words[i].confidence[0] != '\0');

        /* B: live word in this memory model.  A typo'd address would
         * otherwise read open bus and print a plausible constant. */
        {
            uint16_t probe = (uint16_t)(0xA100u + i);
            uint16_t back = 0;
            poke(&ports, words[i].address, probe);
            CHECK_EQ(stunrun_mem_read16_be(&ports, words[i].address, &back), 1);
            CHECK_EQ(back, probe);
            /* And inside the mapped window, not one past its end, where a
             * read wraps to address 0. */
            CHECK(words[i].address >= STUNRUN_MAIN_RAM_BASE);
            CHECK(words[i].address <
                  STUNRUN_MAIN_RAM_BASE + STUNRUN_MAIN_RAM_BYTES);
        }
        /* C: the selection rule.  STATIC-CANDIDATE / OBSERVED-NEGATIVE rows
         * carry none of these tokens and must not reach the table. */
        CHECK(contains(words[i].confidence, "CONFIRMED") ||
              contains(words[i].confidence, "DYNAMIC") ||
              contains(words[i].confidence, "RESOLVED") ||
              contains(words[i].confidence, "OBSERVED-IN-TRACE"));
    }
    CHECK_EQ(stunrun_state_watch_find("no-such-field"), -1);
    CHECK_EQ(stunrun_state_watch_find(NULL), -1);

    /* NULL context is a no-op rather than a crash: the shell holds a static
     * context, but the API is reachable from elsewhere. */
    capture_begin();
    stunrun_state_watch_enable(NULL);
    stunrun_state_watch_summary(NULL);
    stunrun_state_watch_frame(NULL, &ports, 0u);
    stunrun_state_watch_note_adsp_write(NULL, 0x818002u, 0u, 0u, 16u);
    capture_end(out, sizeof(out));
    CHECK_EQ(count_of(out, "shell:"), 0u);
}

/* ── D: seeding and the no-spam property ────────────────────────────────── */
static void test_baseline_and_silence(void)
{
    stunrun_state_watch_t w;
    stunrun_fake_ports_t ports;
    char out0[MAX_CAPTURE];
    char out12[MAX_CAPTURE];
    char out3[MAX_CAPTURE];

    run_scenario(out0, sizeof(out0), out12, sizeof(out12), out3, sizeof(out3),
                 &w, &ports);

    /* Frame 0: the milestone stream announces its initial state, and the
     * word stream seeds SILENTLY -- so every `(was ...)` in the log is a
     * value some earlier frame really held. */
    CHECK_EQ(count_of(out0, "kind=word"), 0u);
    CHECK_EQ(count_of(out0, "shell:"), 1u);
    CHECK(exact_equals(out0, kGoldenFrame0));

    /* Frames 1 and 2: nothing moved and no ADSP event arrived, so the log is
     * byte-for-byte empty.  This is the property that makes the log usable
     * next to a running game. */
    CHECK_EQ(strlen(out12), 0u);
    CHECK_EQ(count_of(out12, "kind=word"), 0u);

    /* The sampled image really did contain the seeded values, so the silence
     * above is "unchanged" and not "never read".  reads_failed is the field
     * that distinguishes the two. */
    CHECK_EQ(w.reads_failed, 0u);
    CHECK_EQ(w.samples, 4u);
}

/* ── E: the exact golden for a synthetic change set ─────────────────────── */
static void test_exact_golden(void)
{
    stunrun_state_watch_t w;
    stunrun_fake_ports_t ports;
    char out0[MAX_CAPTURE];
    char out12[MAX_CAPTURE];
    char out3[MAX_CAPTURE];

    run_scenario(out0, sizeof(out0), out12, sizeof(out12), out3, sizeof(out3),
                 &w, &ports);

    /* Exact line count first.  Non-zero, so this cannot pass by matching
     * nothing. */
    CHECK_EQ(count_of(out3, "shell: state frame=3 "), 7u);
    CHECK_EQ(count_of(out3, "kind=adsp-"), 4u);
    CHECK_EQ(count_of(out3, "kind=word"), 3u);

    /* Whole-buffer equality. */
    if (!exact_equals(out3, kGoldenFrame3)) {
        fputs("--- actual frame 3 ---\n", stderr);
        fputs(out3, stderr);
        fputs("--- end actual ---\n", stderr);
    }
    CHECK(exact_equals(out3, kGoldenFrame3));

    /* The three word lines carry the labels and the previous values a reader
     * needs, spelled out so a change to any one of them is named here. */
    CHECK(contains(out3,
          "shell: state frame=3 kind=word name=score addr=0xFF9534 "
          "value=0x0032 (was 0x0000) confidence=\"DYNAMIC-SCORE-CONFIRMED\"\n"));
    CHECK(contains(out3,
          "shell: state frame=3 kind=word name=track-index addr=0xFF9578 "
          "value=0x0003 (was 0x0000) confidence=\"DYNAMIC-WRITER-CONFIRMED; "
          "TERMINAL-CLEANUP-CONFIRMED; DISPLAYED TRACK LABEL UNPROVEN\"\n"));
    CHECK(contains(out3,
          "shell: state frame=3 kind=word name=time-remaining addr=0xFF956A "
          "value=0x215C (was 0x0000) "
          "confidence=\"DYNAMIC-TIME-REMAINING-CONFIRMED\"\n"));

    /* The `(was ...)` chain: a second change reports the value the previous
     * frame actually held, not a constant zero. */
    poke(&ports, 0xFF9578u, 0x0004u);
    memset(out3, 0, sizeof(out3));
    capture_begin();
    stunrun_state_watch_frame(&w, &ports, 4u);
    capture_end(out3, sizeof(out3));
    CHECK_EQ(count_of(out3, "shell: state frame=4 "), 1u);
    CHECK(contains(out3,
          "shell: state frame=4 kind=word name=track-index addr=0xFF9578 "
          "value=0x0004 (was 0x0003)"));

    /* Word lines come out in TABLE order, not address order: the module
     * orders rarest-change-first so `head` shows the rare events. */
    poke(&ports, 0xFF9578u, 0x0005u);
    poke(&ports, 0xFF9550u, 0x0027u);   /* a recorded state-dispatch constant */
    memset(out3, 0, sizeof(out3));
    capture_begin();
    stunrun_state_watch_frame(&w, &ports, 5u);
    capture_end(out3, sizeof(out3));
    CHECK_EQ(count_of(out3, "shell: state frame=5 "), 2u);
    CHECK(contains(out3, "name=game-state addr=0xFF9550"));
    CHECK(contains(out3, "name=track-index addr=0xFF9578"));
    {
        const char *a = strstr(out3, "name=game-state");
        const char *b = strstr(out3, "name=track-index");
        CHECK(a != NULL && b != NULL && a < b);
    }

    /* The summary turns "this field never moved" into a number. */
    {
        char sums[MAX_CAPTURE];
        memset(sums, 0, sizeof(sums));
        capture_begin();
        stunrun_state_watch_summary(&w);
        capture_end(sums, sizeof(sums));
        CHECK_EQ(count_of(sums, "shell: state-log summary name="),
                 stunrun_state_watch_word_count());
        CHECK_EQ(count_of(sums, "shell: state-log summary words="), 1u);
        /* track-index 0->3->4->5 is three changes; score, time-remaining and
         * game-state one each. */
        CHECK(contains(sums,
              "shell: state-log summary name=track-index addr=0xFF9578 "
              "changes=3\n"));
        CHECK(contains(sums,
              "shell: state-log summary name=score addr=0xFF9534 changes=1\n"));
        CHECK(contains(sums,
              "shell: state-log summary name=score-hi addr=0xFF9532 "
              "changes=0\n"));
        CHECK(contains(sums,
              "shell: state-log summary name=time-remaining addr=0xFF956A "
              "changes=1\n"));
        /* The longword's low half is the one that moves; the high half at
         * 0xFF8014 never does.  Both are watched precisely so that
         * distinction is observable rather than assumed -- see the note on
         * the table entry in state_watch.c. */
        CHECK(contains(sums,
              "shell: state-log summary name=irq-tick addr=0xFF8016 "
              "changes=0\n"));
        CHECK(contains(sums,
              "shell: state-log summary name=irq-tick-hi addr=0xFF8014 "
              "changes=0\n"));
        CHECK(contains(sums,
              "shell: state-log summary name=game-state addr=0xFF9550 "
              "changes=1\n"));
        CHECK(contains(sums, "adsp-events=4 adsp-dropped=0 "
                             "upload-transitions=1 ram-read-failures=0"));
    }
}

/* ── F: the comparator must have teeth ──────────────────────────────────── */
static void test_golden_comparator_has_teeth(void)
{
    stunrun_state_watch_t w;
    stunrun_fake_ports_t ports;
    char out0[MAX_CAPTURE];
    char out12[MAX_CAPTURE];
    char out3[MAX_CAPTURE];
    size_t len;

    run_scenario(out0, sizeof(out0), out12, sizeof(out12), out3, sizeof(out3),
                 &w, &ports);
    CHECK(exact_equals(out3, kGoldenFrame3));
    CHECK_EQ(count_of(out3, "shell: state frame=3 "), 7u);

    len = strlen(kGoldenFrame3);

    /* 1. One nibble different in a value. */
    {
        char bad[sizeof(kGoldenFrame3) + 16u];
        memcpy(bad, kGoldenFrame3, len + 1u);
        bad[0] = '\0';
        snprintf(bad, sizeof(bad), "%s", kGoldenFrame3);
        /* value=0x0032 -> value=0x0033 */
        {
            char *at = strstr(bad, "value=0x0032");
            CHECK(at != NULL);
            if (at != NULL)
                at[11] = '3';
        }
        CHECK(!exact_equals(out3, bad));
    }

    /* 2. One line deleted. */
    {
        char bad[sizeof(kGoldenFrame3) + 16u];
        const char *score = strstr(kGoldenFrame3, "name=score ");
        size_t cut = strlen(score);
        size_t head = (size_t)(score - kGoldenFrame3);
        memcpy(bad, kGoldenFrame3, head);
        memcpy(bad + head, score + cut, len - head - cut + 1u);
        CHECK(!exact_equals(out3, bad));
    }

    /* 3. One line duplicated -- an emitter that fires twice on a change. */
    {
        char bad[sizeof(kGoldenFrame3) * 2u];
        const char *score = strstr(kGoldenFrame3, "name=score ");
        size_t linelen = strlen(score);
        size_t head = (size_t)(score - kGoldenFrame3);
        memcpy(bad, kGoldenFrame3, head);
        memcpy(bad + head, score, linelen);
        memcpy(bad + head + linelen, score, linelen);
        memcpy(bad + head + 2u * linelen, score, len - head + 1u);
        CHECK(!exact_equals(out3, bad));
    }

    /* 4. A wrong confidence string: the whole point of carrying it. */
    {
        char bad[sizeof(kGoldenFrame3) + 16u];
        char *at;
        snprintf(bad, sizeof(bad), "%s", kGoldenFrame3);
        at = strstr(bad, "DYNAMIC-SCORE-CONFIRMED");
        CHECK(at != NULL);
        if (at != NULL)
            memcpy(at, "SPECULATIVE-SCORE-HOPEFULLY", 27u);
        CHECK(!exact_equals(out3, bad));
    }

    /* 5. The matching count the positive case relies on is not zero, so a
     * control that matched nothing could not have been read as a pass. */
    CHECK(count_of(out3, "shell: state frame=3 ") > 0u);
    CHECK(count_of(out3, "kind=word") > 0u);
}

/* ── G: the ADSP event ring bound ───────────────────────────────────────── */
static void test_adsp_ring_bound(void)
{
    stunrun_state_watch_t w;
    stunrun_fake_ports_t ports;
    char out[MAX_CAPTURE];
    char *big;
    unsigned i;

    stunrun_ports_init(&ports);
    stunrun_state_watch_init(&w);
    memset(out, 0, sizeof(out));
    capture_begin();
    stunrun_state_watch_frame(&w, &ports, 0u);   /* seed: milestone line only */
    capture_end(out, sizeof(out));
    CHECK_EQ(count_of(out, "kind=adsp-upload"), 1u);

    /* Overflow the ring by three: the buffer is 4096 events, which is ~55x
     * the 73 control writes a 600-frame title-path run performs, so a real
     * run must never get here.  Drops are counted and announced, never
     * silently truncating. */
    for (i = 0; i < STUNRUN_STATE_WATCH_ADSP_RING + 3u; i++)
        stunrun_state_watch_note_adsp_write(&w, 0x818002u, 0x0000u,
                                            0x020000u + i, 16u);
    CHECK_EQ(w.adsp_dropped, 3u);
    CHECK_EQ(w.adsp_count, STUNRUN_STATE_WATCH_ADSP_RING);

    /* 4096 lines of ~250 bytes, so this one case needs its own heap buffer
     * rather than the shared MAX_CAPTURE capture. */
    big = (char *)malloc(2u * 1024u * 1024u);
    CHECK(big != NULL);
    if (big == NULL)
        return;
    memset(big, 0, 2u * 1024u * 1024u);
    capture_begin();
    stunrun_state_watch_frame(&w, &ports, 1u);
    stunrun_state_watch_summary(&w);
    capture_end(big, 2u * 1024u * 1024u);

    CHECK_EQ(count_of(big, "kind=adsp-control"), STUNRUN_STATE_WATCH_ADSP_RING);
    CHECK_EQ(w.adsp_reported, STUNRUN_STATE_WATCH_ADSP_RING);
    CHECK(contains(big, "adsp-events=4096 adsp-dropped=3 "));
    CHECK(contains(big, " WARNING=events-lost"));

    /* The buffer is empty again, so the next frame is silent. */
    memset(out, 0, sizeof(out));
    capture_begin();
    stunrun_state_watch_frame(&w, &ports, 2u);
    capture_end(out, sizeof(out));
    CHECK_EQ(strlen(out), 0u);

    free(big);
}

/* ── H: upload milestones come from adsp_control_seq.c ──────────────────── */
static void test_upload_milestones(void)
{
    stunrun_state_watch_t w;
    stunrun_fake_ports_t ports;
    char out[MAX_CAPTURE];

    stunrun_ports_init(&ports);
    stunrun_state_watch_init(&w);

    memset(out, 0, sizeof(out));
    capture_begin();
    stunrun_state_watch_frame(&w, &ports, 407u);
    stunrun_state_watch_frame(&w, &ports, 408u);
    stunrun_state_watch_frame(&w, &ports, 409u);
    stunrun_state_watch_frame(&w, &ports, 410u);
    stunrun_state_watch_frame(&w, &ports, 411u);
    stunrun_state_watch_frame(&w, &ports, 412u);
    stunrun_state_watch_frame(&w, &ports, 500u);
    capture_end(out, sizeof(out));

    /* Non-zero, so this block cannot pass vacuously. */
    CHECK_EQ(count_of(out, "kind=adsp-upload"), 3u);
    /* The initial state plus two transitions; 409/410/500 add nothing. */
    CHECK_EQ(count_of(out, "shell: state frame=407 "), 1u);
    CHECK(contains(out,
          "shell: state frame=407 kind=adsp-upload state=empty words=0 "
          "empty-through-frame=407 confidence=\"OBSERVED-IN-TRACE + "
          "SNAPSHOT-DIFF: program RAM empty through frame 407\"\n"));
    CHECK(contains(out,
          "shell: state frame=408 kind=adsp-upload state=populating "
          "words=293 first-populated-frame=408 complete-frame=411 "
          "confidence=\"OBSERVED-IN-TRACE + SNAPSHOT-DIFF\"\n"));
    CHECK(contains(out,
          "shell: state frame=411 kind=adsp-upload state=complete words=2718 "
          "complete-frame=411 confidence=\"OBSERVED-IN-TRACE + SNAPSHOT-DIFF: "
          "2718-word image present\"\n"));
    CHECK(!contains(out, "frame=409 "));
    CHECK(!contains(out, "frame=410 "));
    CHECK(!contains(out, "frame=500 "));

    /* Cross-check the names against the single canonical implementation the
     * module calls.  If adsp_control_seq.c's bounds move, this is where the
     * log's text is caught disagreeing with them. */
    CHECK_EQ(stunrun_adsp_upload_state_at(407),
             STUNRUN_ADSP_UPLOAD_EMPTY);
    CHECK_EQ(stunrun_adsp_upload_state_at(408),
             STUNRUN_ADSP_UPLOAD_POPULATING);
    CHECK_EQ(stunrun_adsp_upload_state_at(410),
             STUNRUN_ADSP_UPLOAD_POPULATING);
    CHECK_EQ(stunrun_adsp_upload_state_at(411),
             STUNRUN_ADSP_UPLOAD_COMPLETE);
    CHECK_EQ(stunrun_adsp_install_frame_ready(411), 0);
    CHECK_EQ(stunrun_adsp_install_frame_ready(412), 1);

    /* Every recorded control op is reachable by address, so the label the log
     * prints is the one adsp_control_seq.c records and not a re-derivation. */
    {
        size_t i;
        for (i = 0; i < stunrun_adsp_control_op_count(); i++) {
            const stunrun_adsp_control_op_t *ops = stunrun_adsp_control_ops();
            char want[MAX_CAPTURE];
            snprintf(want, sizeof(want),
                     "kind=adsp-control addr=0x%06X label=%s",
                     ops[i].address, ops[i].handler);
            static stunrun_state_watch_t w2;
            char got[MAX_CAPTURE];
            stunrun_state_watch_init(&w2);
            memset(got, 0, sizeof(got));
            capture_begin();
            /* Seed inside the capture, so the milestone line does not leak
             * onto the real stdout of the CTest run. */
            stunrun_state_watch_frame(&w2, &ports, 0u);
            stunrun_state_watch_note_adsp_write(&w2, ops[i].address, 0x0000u,
                                                0x030000u, 16u);
            stunrun_state_watch_frame(&w2, &ports, 1u);
            capture_end(got, sizeof(got));
            CHECK(contains(got, want));
            CHECK(contains(got, "recorded-writes-in-600f="));
        }
    }
}

/* ── I: observation only ────────────────────────────────────────────────── */
static void test_reads_only(void)
{
    stunrun_state_watch_t w;
    stunrun_fake_ports_t ports;
    uint8_t *before;
    uint8_t *after;
    /* fake_ports.c allocates main_ram[STUNRUN_MAIN_RAM_BYTES] -- 32 KiB -- even
     * though the header comments on both files still say "512 KiB".  Size the
     * copy from the macro, not from the comment. */
    size_t n = STUNRUN_MAIN_RAM_BYTES;
    char out[MAX_CAPTURE];
    unsigned i;

    before = (uint8_t *)malloc(n);
    after = (uint8_t *)malloc(n);
    CHECK(before != NULL && after != NULL);
    if (before == NULL || after == NULL) {
        free(before);
        free(after);
        return;
    }

    stunrun_ports_init(&ports);
    stunrun_state_watch_init(&w);
    memset(out, 0, sizeof(out));
    capture_begin();
    stunrun_state_watch_frame(&w, &ports, 0u);   /* seed: milestone line only */
    capture_end(out, sizeof(out));
    for (i = 0; i < stunrun_state_watch_word_count(); i++)
        poke(&ports, stunrun_state_watch_words()[i].address,
             (uint16_t)(0x3000u + i));
    stunrun_state_watch_note_adsp_write(&w, 0x81800Eu, 0x0000u, 0x021234u, 16u);
    stunrun_state_watch_note_adsp_write(&w, 0x818060u, 0x0000u, 0x021426u, 16u);

    memcpy(before, stunrun_ports_main_ram(), n);

    capture_begin();
    stunrun_state_watch_frame(&w, &ports, 1u);
    stunrun_state_watch_frame(&w, &ports, 2u);
    stunrun_state_watch_summary(&w);
    capture_end(out, sizeof(out));

    memcpy(after, stunrun_ports_main_ram(), n);
    CHECK_EQ(memcmp(before, after, n), 0);

    /* The whole point of the log: an ADSP reset assert and an IRQ clear both
     * became a line.  Non-zero match count, so this half cannot pass by
     * printing nothing. */
    CHECK_EQ(count_of(out, "kind=adsp-control"), 1u);
    CHECK(contains(out, "kind=adsp-control addr=0x81800E label=ADSP reset "
                        "assert"));
    CHECK_EQ(count_of(out, "kind=adsp-irq-clear"), 1u);
    CHECK(contains(out, "kind=adsp-irq-clear addr=0x818060"));
    /* The two recorded writer PCs come from adsp_control_seq.c, not from a
     * literal in this test. */
    CHECK(contains(out, "reference-writers=0x021426,0x02C234"));

    free(before);
    free(after);
}

int main(void)
{
    test_enable_header();
    test_baseline_and_silence();
    test_exact_golden();
    test_golden_comparator_has_teeth();
    test_adsp_ring_bound();
    test_upload_milestones();
    test_reads_only();

    printf("state_watch_test: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}