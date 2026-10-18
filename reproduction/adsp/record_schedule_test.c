/* Oracle-backed expectations for the ADSP $0174-$01A9 record schedule.
 *
 * The expected trace below is the frame-551 capture's own
 * `transaction.producer_events`, filtered to the nine PCs this slice models,
 * in `seq` order, with no value written by this implementation. Source:
 * reference/experiments/stunrun/adsp-som-pm-reads-f551/run-1/result.json
 * (sha256 52d0c8fb9e235348368c4dc4d8898565a53bb575d7a618026c7662825865c1ce,
 * matching its metadata.json), MAME /mnt/build/mame-oracle/mame
 * 75b09ee422732ebce54d4c641e3bf38ddc9de4976963844691c60c361fb46b06,
 * `input=none`, power-on with a disposable NVRAM seed, transaction at frame
 * 551. Both runs of that experiment carry the same transaction sha256.
 *
 * The ROM words the SIMBUF port serves are not taken from that capture. They
 * come from the commercial object ROM via
 * tools/read-adsp-object-rom-words (mainpcb:user1, /MP=1 bank base $10000),
 * held in the committed sparse image reproduction/adsp/som_selector_fixture.c,
 * whose 67 designated initializers `--verify-fixture` re-checks against those
 * lanes. Re-read them with that tool; do not edit the fixture by hand.
 */
#include <stdio.h>
#include <string.h>

#include "record_schedule.h"
#include "som_selector_fixture.h"
#include "som_source_select.h"

#define ROM STUNRUN_ADSP_SOM_SELECTOR_ROM_WORDS
#define ROM_BASE STUNRUN_ADSP_SOM_SELECTOR_ROM_BASE_WORD
#define WORDS STUNRUN_ADSP_SOM_SELECTOR_ROM_WORDS
#define MP1 STUNRUN_ADSP_SOM_SIMBUF_MP_WORDS

/* PC $0172 read DM($2000) and returned this; $0179 stores SI into DM($0034).
 * That read is outside this slice, so $9989 stays an input. */
#define ENTRY_DM0034 0x9989u

typedef struct expected_event {
    uint16_t pc;
    uint16_t address;
    uint16_t value;
} expected_event_t;

static int failures;

#define check(condition) do { \
    if (!(condition)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        failures++; \
    } \
} while (0)

/* The capture, verbatim, in order. 86 events: nine record passes of nine
 * accesses each, then the tenth pass's five. The tenth pass reads $0000 at both
 * $017F and $0182 and has no $019B/$01A0/$01A6/$01A7 event at all, which is
 * what the $0187 RTS produces. */
static const expected_event_t EXPECTED_TRACE[] = {
    {0x017A, 0x0034, 0x9989}, {0x017B, 0x2001, 0x9989},
    {0x017E, 0x0038, 0x0000}, {0x017F, 0x2000, 0x999D},
    {0x0182, 0x2000, 0xF002}, {0x019B, 0x0034, 0x998B},
    {0x01A0, 0x0036, 0x0002}, {0x01A6, 0x0035, 0x0001},
    {0x01A7, 0x2001, 0x999D},
    {0x017A, 0x0034, 0x998B}, {0x017B, 0x2001, 0x998B},
    {0x017E, 0x0038, 0x0000}, {0x017F, 0x2000, 0x99A2},
    {0x0182, 0x2000, 0xF002}, {0x019B, 0x0034, 0x998D},
    {0x01A0, 0x0036, 0x0002}, {0x01A6, 0x0035, 0x0001},
    {0x01A7, 0x2001, 0x99A2},
    {0x017A, 0x0034, 0x998D}, {0x017B, 0x2001, 0x998D},
    {0x017E, 0x0038, 0x0000}, {0x017F, 0x2000, 0x99A7},
    {0x0182, 0x2000, 0xF002}, {0x019B, 0x0034, 0x998F},
    {0x01A0, 0x0036, 0x0002}, {0x01A6, 0x0035, 0x0001},
    {0x01A7, 0x2001, 0x99A7},
    {0x017A, 0x0034, 0x998F}, {0x017B, 0x2001, 0x998F},
    {0x017E, 0x0038, 0x0000}, {0x017F, 0x2000, 0x99AC},
    {0x0182, 0x2000, 0xF002}, {0x019B, 0x0034, 0x9991},
    {0x01A0, 0x0036, 0x0002}, {0x01A6, 0x0035, 0x0001},
    {0x01A7, 0x2001, 0x99AC},
    {0x017A, 0x0034, 0x9991}, {0x017B, 0x2001, 0x9991},
    {0x017E, 0x0038, 0x0000}, {0x017F, 0x2000, 0x99B1},
    {0x0182, 0x2000, 0xF002}, {0x019B, 0x0034, 0x9993},
    {0x01A0, 0x0036, 0x0002}, {0x01A6, 0x0035, 0x0001},
    {0x01A7, 0x2001, 0x99B1},
    {0x017A, 0x0034, 0x9993}, {0x017B, 0x2001, 0x9993},
    {0x017E, 0x0038, 0x0000}, {0x017F, 0x2000, 0x99B6},
    {0x0182, 0x2000, 0xF002}, {0x019B, 0x0034, 0x9995},
    {0x01A0, 0x0036, 0x0002}, {0x01A6, 0x0035, 0x0001},
    {0x01A7, 0x2001, 0x99B6},
    {0x017A, 0x0034, 0x9995}, {0x017B, 0x2001, 0x9995},
    {0x017E, 0x0038, 0x0000}, {0x017F, 0x2000, 0x99BB},
    {0x0182, 0x2000, 0xF002}, {0x019B, 0x0034, 0x9997},
    {0x01A0, 0x0036, 0x0002}, {0x01A6, 0x0035, 0x0001},
    {0x01A7, 0x2001, 0x99BB},
    {0x017A, 0x0034, 0x9997}, {0x017B, 0x2001, 0x9997},
    {0x017E, 0x0038, 0x0000}, {0x017F, 0x2000, 0x99C0},
    {0x0182, 0x2000, 0xF002}, {0x019B, 0x0034, 0x9999},
    {0x01A0, 0x0036, 0x0002}, {0x01A6, 0x0035, 0x0001},
    {0x01A7, 0x2001, 0x99C0},
    {0x017A, 0x0034, 0x9999}, {0x017B, 0x2001, 0x9999},
    {0x017E, 0x0038, 0x0000}, {0x017F, 0x2000, 0x99C5},
    {0x0182, 0x2000, 0xF002}, {0x019B, 0x0034, 0x999B},
    {0x01A0, 0x0036, 0x0002}, {0x01A6, 0x0035, 0x0001},
    {0x01A7, 0x2001, 0x99C5},
    {0x017A, 0x0034, 0x999B}, {0x017B, 0x2001, 0x999B},
    {0x017E, 0x0038, 0x0000}, {0x017F, 0x2000, 0x0000},
    {0x0182, 0x2000, 0x0000},
};

#define EXPECTED_EVENTS \
    (sizeof(EXPECTED_TRACE) / sizeof(EXPECTED_TRACE[0]))

/* Flatten a run into the same (pc, address, value) stream the capture holds, so
 * the comparison is of the ordered sequence of DM accesses and not of a
 * summary count. */
static size_t flatten(const stunrun_adsp_record_schedule_t *schedule,
                      expected_event_t *out, size_t capacity)
{
    size_t produced = 0u;
    size_t pass;

    for (pass = 0; pass < schedule->pass_count; ++pass) {
        const stunrun_adsp_record_schedule_pass_t *entry =
            &schedule->passes[pass];
        if (produced + 9u > capacity)
            return 0u;
        out[produced].pc = 0x017Au;
        out[produced].address = 0x0034u;
        out[produced].value = entry->dm2001_latched;
        produced++;
        out[produced].pc = 0x017Bu;
        out[produced].address = 0x2001u;
        out[produced].value = entry->dm2001_latched;
        produced++;
        out[produced].pc = 0x017Eu;
        out[produced].address = 0x0038u;
        out[produced].value = entry->dm0038;
        produced++;
        out[produced].pc = 0x017Fu;
        out[produced].address = 0x2000u;
        out[produced].value = entry->start;
        produced++;
        out[produced].pc = 0x0182u;
        out[produced].address = 0x2000u;
        out[produced].value = entry->command;
        produced++;
        if (!entry->produced_record)
            continue;
        out[produced].pc = 0x019Bu;
        out[produced].address = 0x0034u;
        out[produced].value = entry->dm0034_after;
        produced++;
        out[produced].pc = 0x01A0u;
        out[produced].address = 0x0036u;
        out[produced].value = entry->dm0036;
        produced++;
        out[produced].pc = 0x01A6u;
        out[produced].address = 0x0035u;
        out[produced].value = entry->dm0035;
        produced++;
        out[produced].pc = 0x01A7u;
        out[produced].address = 0x2001u;
        out[produced].value = entry->record_start;
        produced++;
    }
    return produced;
}

static void test_captured_schedule(void)
{
    stunrun_adsp_record_schedule_t schedule;
    expected_event_t trace[EXPECTED_EVENTS + 4u];
    size_t produced, event;
    size_t pass;

    check(stunrun_adsp_record_schedule_run(
        stunrun_adsp_som_selector_fixture_rom(), ROM, ROM_BASE, 1u,
        ENTRY_DM0034, &schedule));

    /* The observable the whole slice exists for: every latched DM value, in
     * the order the ADSP produced it. */
    produced = flatten(&schedule, trace, sizeof(trace) / sizeof(trace[0]));
    check(produced == EXPECTED_EVENTS);
    if (produced != EXPECTED_EVENTS)
        return;
    for (event = 0; event < EXPECTED_EVENTS; ++event) {
        if (trace[event].pc != EXPECTED_TRACE[event].pc ||
            trace[event].address != EXPECTED_TRACE[event].address ||
            trace[event].value != EXPECTED_TRACE[event].value) {
            printf("event %zu: got pc=$%04X addr=$%04X val=$%04X "
                   "expected pc=$%04X addr=$%04X val=$%04X\n",
                   event, trace[event].pc, trace[event].address,
                   trace[event].value, EXPECTED_TRACE[event].pc,
                   EXPECTED_TRACE[event].address, EXPECTED_TRACE[event].value);
            failures++;
        }
    }

    /* Nine records, ten passes: the tenth is what proves the exit. */
    check(schedule.entry_dm0034 == ENTRY_DM0034);
    check(schedule.pass_count == 10u);
    check(schedule.record_count == 9u);

    /* Each $017F read must have come from the object ROM word the chain holds
     * at that address, and $0182 from the next one. These are ROM word
     * indices (SIM address + the /MP=1 bank base $10000), not SIM addresses. */
    for (pass = 0; pass < schedule.pass_count; ++pass) {
        const stunrun_adsp_record_schedule_pass_t *entry =
            &schedule.passes[pass];
        check(entry->start_word_index == MP1 + 0x9989u + 2u * pass);
        check(entry->command_word_index == MP1 + 0x998au + 2u * pass);
        check(entry->start == stunrun_adsp_som_selector_fixture_rom()[
            entry->start_word_index - ROM_BASE]);
        check(entry->command == stunrun_adsp_som_selector_fixture_rom()[
            entry->command_word_index - ROM_BASE]);
    }
    check(schedule.passes[9].jump_index ==
          STUNRUN_ADSP_RECORD_SCHEDULE_EXIT_INDEX);
    for (pass = 0; pass < schedule.record_count; ++pass)
        check(schedule.passes[pass].jump_index ==
              STUNRUN_ADSP_RECORD_SCHEDULE_CONTINUE_INDEX);
    /* $017E writes DM($0038) on all ten passes, the terminating one included. */
    for (pass = 0; pass < schedule.pass_count; ++pass)
        check(schedule.passes[pass].dm0038 == 0x0000u);

    /* $0187's RTS leaves DM($2001) holding the pointer $017B latched, NOT a
     * record start -- the tenth pass read $0000 and latched nothing.
     *
     * dm2001_final and dm0034_final are EQUAL BY CONSTRUCTION on every path
     * this slice accepts: $017B copies DM($0034) into DM($2001) at the top of
     * each pass, and the only exit is index 0, which returns before $019B could
     * advance DM($0034). A mutant that swaps the two therefore SURVIVES, and
     * that is not a hole a stronger assertion can close -- no input separates
     * them. What the pair does pin is that the register was left on the
     * terminating pointer rather than on the last record's start, so assert
     * that instead, which the two equal values do decide. */
    check(schedule.dm2001_final == 0x999Bu);
    check(schedule.dm0034_final == 0x999Bu);
    check(schedule.dm2001_final == schedule.dm0034_final);
    check(schedule.dm2001_final !=
          schedule.passes[schedule.record_count - 1u].record_start);
    check(schedule.dm0036_final == 0x0002u);
    check(schedule.dm0035_final == 0x0001u);
    check(schedule.dm0038_final == 0x0000u);
    check(schedule.next_simbuf_word_index == MP1 + 0x999bu + 2u);
}

/* A chain whose words are not in the captured fixture, so a lookup table keyed
 * on the captured values cannot answer this. The captured field word is always
 * $F002, which pins DM($0036) at $0002 and DM($0035) at $0001 and therefore
 * cannot tell $003F from $000F; these four are chosen so every bit the two masks
 * select is set by at least one of them, and so the $01A4 "+1 when zero" rule is
 * exercised outside the captured chain too:
 *
 *   $F508 -> DM($0036)=$0008 (bit 3)         DM($0035)=$0014 (bits 2 and 4)
 *   $F820 -> DM($0036)=$0020 (bit 5)         DM($0035)=$0020 (bit 5)
 *   $F710 -> DM($0036)=$0010 (bit 4)         DM($0035)=$001C (bits 2, 3, 4)
 *   $F002 -> DM($0036)=$0002 (bits 0 and 1) DM($0035)=$0001 ($01A4 adds 1)
 */
#define SYNTH_BASE_WORD (MP1 + 0x4000u)
static const uint16_t SYNTH_CHAIN[] = {
    0x2222u, 0xF508u,
    0x3333u, 0xF820u,
    0x4444u, 0xF710u,
    0x5555u, 0xF002u,
    0x0000u, 0x0000u,   /* terminates the walk at $0187 */
};
#define SYNTH_WORDS (sizeof(SYNTH_CHAIN) / sizeof(SYNTH_CHAIN[0]))
#define SYNTH_ENTRY 0x4000u

static void test_other_rom_words_give_other_results(void)
{
    stunrun_adsp_record_schedule_t schedule;
    uint16_t altered[SYNTH_WORDS];

    check(stunrun_adsp_record_schedule_run(
        SYNTH_CHAIN, SYNTH_WORDS, SYNTH_BASE_WORD, 1u, SYNTH_ENTRY,
        &schedule));
    check(schedule.pass_count == 5u);
    check(schedule.record_count == 4u);
    check(schedule.passes[0].dm2001_latched == 0x4000u);
    check(schedule.passes[0].start == 0x2222u);
    check(schedule.passes[0].dm0034_after == 0x4002u);
    check(schedule.passes[0].dm0036 == 0x0008u);
    check(schedule.passes[0].dm0035 == 0x0014u);
    check(schedule.passes[0].record_start == 0x2222u);
    check(schedule.passes[1].dm2001_latched == 0x4002u);
    check(schedule.passes[1].start == 0x3333u);
    check(schedule.passes[1].dm0034_after == 0x4004u);
    check(schedule.passes[1].dm0036 == 0x0020u);
    check(schedule.passes[1].dm0035 == 0x0020u);
    check(schedule.passes[1].record_start == 0x3333u);
    check(schedule.passes[2].dm2001_latched == 0x4004u);
    check(schedule.passes[2].start == 0x4444u);
    check(schedule.passes[2].dm0034_after == 0x4006u);
    check(schedule.passes[2].dm0036 == 0x0010u);
    check(schedule.passes[2].dm0035 == 0x001Cu);
    check(schedule.passes[2].record_start == 0x4444u);
    check(schedule.passes[3].dm2001_latched == 0x4006u);
    check(schedule.passes[3].start == 0x5555u);
    check(schedule.passes[3].dm0034_after == 0x4008u);
    check(schedule.passes[3].dm0036 == 0x0002u);
    check(schedule.passes[3].dm0035 == 0x0001u);
    check(schedule.passes[3].record_start == 0x5555u);
    check(schedule.passes[4].produced_record == 0);
    check(schedule.dm2001_final == 0x4008u);
    check(schedule.dm0034_final == 0x4008u);
    check(schedule.dm0036_final == 0x0002u);
    check(schedule.dm0035_final == 0x0001u);
    check(schedule.next_simbuf_word_index == MP1 + 0x4008u + 2u);

    /* One field word changed by one bit moves the derived value with it. */
    memcpy(altered, SYNTH_CHAIN, sizeof(altered));
    altered[1] = 0xF509u;
    check(stunrun_adsp_record_schedule_run(
        altered, SYNTH_WORDS, SYNTH_BASE_WORD, 1u, SYNTH_ENTRY, &schedule));
    check(schedule.passes[0].dm0036 == 0x0009u);

    /* And a start word that is not in the captured fixture survives as itself. */
    memcpy(altered, SYNTH_CHAIN, sizeof(altered));
    altered[0] = 0xbeefu;
    check(stunrun_adsp_record_schedule_run(
        altered, SYNTH_WORDS, SYNTH_BASE_WORD, 1u, SYNTH_ENTRY, &schedule));
    check(schedule.passes[0].record_start == 0xbeefu);
}

/* Everything the capture never exercised is refused, and refusal leaves the
 * caller's buffer exactly as it was. */
static void check_rejected(const uint16_t *rom, size_t word_count,
                           uint32_t base_word, uint16_t mp, uint32_t entry,
                           const char *what)
{
    stunrun_adsp_record_schedule_t schedule;
    size_t i;

    memset(&schedule, 0xa5, sizeof(schedule));
    if (stunrun_adsp_record_schedule_run(rom, word_count, base_word, mp, entry,
                                         &schedule)) {
        printf("FAIL %s was accepted\n", what);
        failures++;
    }
    for (i = 0; i < sizeof(schedule) / sizeof(uint16_t); ++i) {
        const uint8_t *bytes = (const uint8_t *)&schedule;
        if (bytes[i] != 0xa5u) {
            printf("FAIL %s published partial output at byte %zu\n", what, i);
            failures++;
            break;
        }
    }
}

static void test_rejects_without_partial_output(void)
{
    uint16_t chain[SYNTH_WORDS];
    uint32_t index;

    check_rejected(NULL, SYNTH_WORDS, SYNTH_BASE_WORD, 1u, SYNTH_ENTRY,
                   "NULL rom");
    check_rejected(SYNTH_CHAIN, SYNTH_WORDS, SYNTH_BASE_WORD, 0u, SYNTH_ENTRY,
                   "mp != 1");
    check_rejected(SYNTH_CHAIN, 0u, SYNTH_BASE_WORD, 1u, SYNTH_ENTRY,
                   "zero-length ROM window");
    check_rejected(SYNTH_CHAIN, SYNTH_WORDS, SYNTH_BASE_WORD, 1u, 0x10000u,
                   "entry outside the /MP=1 bank");
    check_rejected(SYNTH_CHAIN, SYNTH_WORDS, SYNTH_BASE_WORD + 1u, 1u,
                   SYNTH_ENTRY, "ROM base above the entry");
    check_rejected(SYNTH_CHAIN, 1u, SYNTH_BASE_WORD, 1u, SYNTH_ENTRY,
                   "ROM window too short for the first pair");
    check_rejected(SYNTH_CHAIN, 3u, SYNTH_BASE_WORD, 1u, SYNTH_ENTRY,
                   "ROM window cut inside the chain");

    /* $0188 (index 1) and the seven NOPs at $0189-$0195 (indices 2..14) were
     * never executed; so was the $019F exit $01E4 drain. */
    for (index = 0u; index < 4u; ++index) {
        static const uint16_t rejected_commands[] = {
            0x1001u, /* index 1  -> $0188, would write DM($0038) */
            0xA041u, /* index 10 -> $0192 NOP */
            0x2041u, /* index 2  -> $0189 NOP */
            0xF000u, /* index 15 but $019F IF EQ -> $01E4 drain */
        };
        memcpy(chain, SYNTH_CHAIN, sizeof(chain));
        chain[1] = rejected_commands[index];
        check_rejected(chain, SYNTH_WORDS, SYNTH_BASE_WORD, 1u, SYNTH_ENTRY,
                       "unobserved branch");
    }
}

int main(void)
{
    test_captured_schedule();
    test_other_rom_words_give_other_results();
    test_rejects_without_partial_output();
    if (failures == 0)
        puts("PASS");
    return failures == 0 ? 0 : 1;
}