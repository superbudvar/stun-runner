/* Oracle-backed vector tests for the ADSP SOM span walk ($0564-$05D1).
 *
 * Inputs are the captured data-memory ring and the index registers observed at
 * $0564, from the frozen-oracle run recorded at
 *   reference/experiments/stunrun/adsp-som-transaction-f551/result.json and
 *   its register-snapshot companion adsp-som-transaction-f551-regs/.
 * Frame 551, input none, power-on, MAME sha256
 *   75b09ee422732ebce54d4c641e3bf38ddc9de4976963844691c60c361fb46b06
 *
 * Each case supplies DM[$0040..$0045] -- the six-word circular window the L
 * register selects -- plus I1 and I2 as observed at $0564. Everything the walk
 * derives (both header words, start_y, the span count, each span's AX1/AY0/AY1,
 * and the run length the emitter will latch) is computed, and compared against
 * the captured record words.
 *
 * WHAT IS TAKEN FROM THE CAPTURE RATHER THAN COMPUTED: the two 32-bit side
 * values per span. Those are what $05DD/$0607 leave in DM($003E/$003F) and
 * DM($003C/$003D); som_math.c owns that arithmetic and this test does not
 * duplicate it. The C below therefore produces a record whose header, span
 * layout, run lengths and terminator are derived, with the side words filled
 * in from the capture, and the whole 16-word record is compared word for word.
 */
#include <stdio.h>
#include <string.h>

#include "som_walk.h"

static int failures;

#define check(c) do { if (!(c)) { printf("FAIL %s\n", #c); failures++; } } while (0)

#define RECORDS 9u
#define RECORD_WORDS 16u
#define RING_BASE 0x0040u
#define RING_WORDS 6u
#define RING_LENGTH 6u /* L0 = L1 = L2 = L4 = $0006 in the capture */

typedef struct {
    uint16_t i1, i2;                         /* index registers at $0564 */
    uint16_t ring[RING_WORDS];              /* DM[$0040..$0045] */
    uint16_t expected[RECORD_WORDS];        /* the captured record words */
} walk_vector_t;

/* Records 0-8 in capture order. The control word ($0515, DM($0036)) is the
 * same $0030 for all of them and is not derived by the walk. */
static const walk_vector_t VECTORS[RECORDS] = {
    { 66, 66, { 0x00F4, 0x009E, 0x00FF, 0x0099, 0x00FB, 0x00A0 },
      { 0x0030, 0x00FF, 0x00FF, 0x0099, 0x6DB8, 0xFFFF, 0xCCDA, 0xFFFD, 0x0006,
        0x6DB8, 0xFFFF, 0x8000, 0x0003, 0x0003, 0xFFFF, 0xFFFF } },
    { 66, 66, { 0x0103, 0x00A0, 0x00FF, 0x0099, 0x010A, 0x009E },
      { 0x0030, 0x00FF, 0x00FF, 0x0099, 0x3326, 0x0002, 0x9248, 0x0000, 0x0006,
        0x8000, 0xFFFC, 0x9248, 0x0000, 0x0003, 0xFFFF, 0xFFFF } },
    { 66, 66, { 0x00FB, 0x00A0, 0x00FF, 0x0099, 0x0103, 0x00A0 },
      { 0x0030, 0x00FF, 0x00FF, 0x0099, 0x9248, 0x0000, 0x6DB8, 0xFFFF, 0x0008,
        0x0000, 0x0000, 0x0000, 0x0000, 0x0001, 0xFFFF, 0xFFFF } },
    { 64, 64, { 0x00F2, 0x0097, 0x00FF, 0x0099, 0x00F4, 0x009E },
      { 0x0030, 0x00F2, 0x00F2, 0x0097, 0x8000, 0x0006, 0x4924, 0x0000, 0x0003,
        0xCCDA, 0xFFFD, 0x4924, 0x0000, 0x0006, 0xFFFF, 0xFFFF } },
    { 68, 68, { 0x010A, 0x009E, 0x00FF, 0x0099, 0x010C, 0x0097 },
      { 0x0030, 0x010C, 0x010C, 0x0097, 0xB6DC, 0xFFFF, 0x8000, 0xFFF9, 0x0003,
        0xB6DC, 0xFFFF, 0x3326, 0x0002, 0x0006, 0xFFFF, 0xFFFF } },
    { 64, 64, { 0x00FF, 0x0091, 0x00FF, 0x0099, 0x00F6, 0x0093 },
      { 0x0030, 0x00FF, 0x00FF, 0x0091, 0x0000, 0x0000, 0x8000, 0xFFFB, 0x0003,
        0x0000, 0x0000, 0x7FFA, 0x0001, 0x0007, 0xFFFF, 0xFFFF } },
    { 68, 68, { 0x010C, 0x0097, 0x00FF, 0x0099, 0x0108, 0x0093 },
      { 0x0030, 0x0108, 0x0108, 0x0093, 0x0000, 0x0001, 0x8006, 0xFFFE, 0x0005,
        0x8000, 0xFFF9, 0x8006, 0xFFFE, 0x0003, 0xFFFF, 0xFFFF } },
    { 64, 64, { 0x00F6, 0x0093, 0x00FF, 0x0099, 0x00F2, 0x0097 },
      { 0x0030, 0x00F6, 0x00F6, 0x0093, 0x7FFA, 0x0001, 0x0000, 0xFFFF, 0x0005,
        0x7FFA, 0x0001, 0x8000, 0x0006, 0x0003, 0xFFFF, 0xFFFF } },
    { 64, 64, { 0x00FF, 0x0091, 0x0108, 0x0093, 0x00FF, 0x0099 },
      { 0x0030, 0x00FF, 0x00FF, 0x0091, 0x8000, 0x0004, 0x0000, 0x0000, 0x0003,
        0x8006, 0xFFFE, 0x0000, 0x0000, 0x0007, 0xFFFF, 0xFFFF } },
};

int main(void)
{
    stunrun_adsp_som_span_t spans[8];
    uint16_t out[RECORD_WORDS];
    size_t i, k;

    for (i = 0; i < RECORDS; i++) {
        stunrun_adsp_som_walk_t walk;
        stunrun_adsp_som_record_t record;

        memset(spans, 0, sizeof(spans));
        check(stunrun_adsp_som_walk_record(
            VECTORS[i].ring, RING_WORDS, RING_BASE,
            VECTORS[i].i1, VECTORS[i].i2, RING_LENGTH,
            spans, 8u, &walk));

        /* Derived, not copied: these must equal the captured header words. */
        check(walk.start_x == VECTORS[i].expected[1]);
        check(walk.delta_x == VECTORS[i].expected[2]);
        check(walk.start_y == VECTORS[i].expected[3]);
        check(walk.terminated == 1);
        /* Two spans in every captured record; 4 + 2*5 + 2 == 16 words. */
        check(walk.span_count == 2u);
        check(stunrun_adsp_som_record_words(walk.span_count) == RECORD_WORDS);

        /* Fill in the side values $05DD/$0607 own, then emit and compare. */
        for (k = 0; k < walk.span_count; k++) {
            spans[k].dm_3e.lo = VECTORS[i].expected[4 + k * 5 + 0];
            spans[k].dm_3e.hi = VECTORS[i].expected[4 + k * 5 + 1];
            spans[k].dm_3c.lo = VECTORS[i].expected[4 + k * 5 + 2];
            spans[k].dm_3c.hi = VECTORS[i].expected[4 + k * 5 + 3];
        }
        record.control = VECTORS[i].expected[0];
        record.start_x = walk.start_x;
        record.delta_x = walk.delta_x;
        record.start_y = walk.start_y;
        record.spans = spans;
        record.span_count = walk.span_count;

        check(stunrun_adsp_som_emit_record(&record, out, RECORD_WORDS)
              == RECORD_WORDS);
        if (memcmp(out, VECTORS[i].expected, sizeof(VECTORS[i].expected)) != 0) {
            printf("FAIL record %lu word stream\n", (unsigned long)i);
            for (k = 0; k < RECORD_WORDS; k++)
                if (out[k] != VECTORS[i].expected[k])
                    printf("   word %lu got %04X want %04X\n",
                           (unsigned long)k, out[k], VECTORS[i].expected[k]);
            failures++;
        }
    }

    /* The walk's own arithmetic, isolated from the emitter. Every captured
     * span is (AX1, AY0, AY1) with AY1/AY0 straddling AX1; these three cases
     * pin which endpoint each branch promotes. */
    for (i = 0; i < RECORDS; i++) {
        stunrun_adsp_som_walk_t walk;
        memset(spans, 0, sizeof(spans));
        check(stunrun_adsp_som_walk_record(
            VECTORS[i].ring, RING_WORDS, RING_BASE,
            VECTORS[i].i1, VECTORS[i].i2, RING_LENGTH, spans, 8u, &walk));
        check(spans[0].ax1 == walk.start_y);
        check(spans[0].ay1 > spans[0].ax1);
        check(spans[0].ay0 > spans[0].ax1);
    }

    /* Prologue guard strictness. The $0574/$0577 prologue uses IF LE, so a
     * first span with AY0 == AY1 == AX1 must produce NO spans and leave the
     * terminator as the only output. None of the nine captured records is
     * degenerate in the prologue position, so this is a synthetic vector
     * derived from the disassembly, not from a captured measurement. */
    {
        static const uint16_t degenerate[RING_WORDS] = {
            0x0100, 0x0100, 0x0101, 0x0100, 0x0101, 0x0100
        };
        stunrun_adsp_som_walk_t walk;

        /* DM[$41] = DM[$43] = DM[$45] = $0100, so AY0 == AX1 == AY1. */
        memset(spans, 0, sizeof(spans));
        check(stunrun_adsp_som_walk_record(
            degenerate, RING_WORDS, RING_BASE, 0x0042u, 0x0042u, RING_LENGTH,
            spans, 8u, &walk));
        check(walk.start_y == 0x0100u);
        check(walk.span_count == 0u);
        check(walk.terminated == 0);
        /* Header plus terminator only: 4 + 0 + 2 words. */
        check(stunrun_adsp_som_record_words(walk.span_count) == 6u);
    }

    /* Circular addressing: this is what keeps the walk inside the six-word
     * ring, and what makes records 4 and 6 take the back edge. Walking back
     * from $0041 lands below the window base and is lifted by L; walking
     * forward off the top is dropped by L. Both directions are exercised by
     * the captured records (record 0's I4 goes $0043 -> $0041 -> $0045). */
    check(stunrun_adsp_lmask(6u) == 0x3ff8u);
    check(stunrun_adsp_lmask(0x0011u) == 0x3fe0u);
    check(stunrun_adsp_lmask(1u) == 0x3fffu);
    {
        stunrun_adsp_index_t reg;
        reg.length = RING_LENGTH;
        stunrun_adsp_index_write(&reg, 0x0043u);
        check(reg.index == 0x0043u);
        check(reg.base == 0x0040u);      /* 0x43 & 0x3ff8 */
        stunrun_adsp_index_modify(&reg, STUNRUN_ADSP_M_BACK);
        check(reg.index == 0x0041u);      /* still inside the ring */
        stunrun_adsp_index_modify(&reg, STUNRUN_ADSP_M_BACK);
        check(reg.index == 0x0045u);      /* 0x3f < base, so += 6 */
        stunrun_adsp_index_modify(&reg, STUNRUN_ADSP_M_FWD);
        check(reg.index == 0x0041u);      /* 0x47 >= base+6, so -= 6 */
        stunrun_adsp_index_modify(&reg, STUNRUN_ADSP_M_FWD);
        check(reg.index == 0x0043u);
    }

    /* A truncated capture must fail loudly rather than read past the window. */
    {
        stunrun_adsp_som_walk_t walk;
        memset(spans, 0, sizeof(spans));
        check(!stunrun_adsp_som_walk_record(
            VECTORS[0].ring, 4u, RING_BASE, VECTORS[0].i1, VECTORS[0].i2,
            RING_LENGTH, spans, 8u, &walk));
        check(!stunrun_adsp_som_walk_record(
            NULL, RING_WORDS, RING_BASE, VECTORS[0].i1, VECTORS[0].i2,
            RING_LENGTH, spans, 8u, &walk));
        check(!stunrun_adsp_som_walk_record(
            VECTORS[0].ring, RING_WORDS, RING_BASE, VECTORS[0].i1,
            VECTORS[0].i2, RING_LENGTH, spans, 8u, NULL));
        /* Too little room for the walk it actually needs: fail, do not
         * truncate the record. */
        check(!stunrun_adsp_som_walk_record(
            VECTORS[0].ring, RING_WORDS, RING_BASE, VECTORS[0].i1,
            VECTORS[0].i2, RING_LENGTH, spans, 1u, &walk));
    }

    if (failures == 0)
        puts("PASS");
    return failures == 0 ? 0 : 1;
}