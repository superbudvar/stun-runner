/* Oracle-backed vector tests for the ADSP SOM record emitter.
 *
 * Every expected word here is transcribed from one frozen-oracle capture:
 *   reference/experiments/stunrun/adsp-som-transaction-f551/result.json
 *   frame 551, input none, power-on, MAME sha256
 *   75b09ee422732ebce54d4c641e3bf38ddc9de4976963844691c60c361fb46b06
 * transaction sha256 fcd8807f5eccca5f37d5829654169466fa4de6a9a5fc2db9ae3c5ffd8cba3c3f
 *
 * The capture records, per emitted word, the ADSP program counter that latched
 * it, so the record layout below is the observed $0515/$0565/$0567/$056B
 * header, repeated $0580|$05A0|$05BA five-word spans, and the $05D2/$05D4
 * terminator -- not a shape chosen to make this test pass.
 *
 * The 146 captured words are: two $0000 prologue words (PC $0035/$0039), then
 * nine 16-word records (PC $0515..$05D4), then GINT at PC $004C.
 */
#include <stdio.h>
#include <string.h>

#include "som_record.h"

static int failures;

#define check(c) do { if (!(c)) { printf("FAIL %s\n", #c); failures++; } } while (0)

/* The complete captured SOMLATCH stream, in emission order. */
static const uint16_t CAPTURED_SOM[146] = {
    /* prologue, PC $0035/$0039 with AR still $0000 */
    0x0000, 0x0000,
    /* record 0: control 0030, start_x 00FF, delta_x 00FF, start_y 0099 */
    0x0030, 0x00FF, 0x00FF, 0x0099,
    0x6DB8, 0xFFFF, 0xCCDA, 0xFFFD, 0x0006,
    0x6DB8, 0xFFFF, 0x8000, 0x0003, 0x0003,
    0xFFFF, 0xFFFF,
    /* record 1 */
    0x0030, 0x00FF, 0x00FF, 0x0099,
    0x3326, 0x0002, 0x9248, 0x0000, 0x0006,
    0x8000, 0xFFFC, 0x9248, 0x0000, 0x0003,
    0xFFFF, 0xFFFF,
    /* record 2 */
    0x0030, 0x00FF, 0x00FF, 0x0099,
    0x9248, 0x0000, 0x6DB8, 0xFFFF, 0x0008,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0001,
    0xFFFF, 0xFFFF,
    /* record 3 */
    0x0030, 0x00F2, 0x00F2, 0x0097,
    0x8000, 0x0006, 0x4924, 0x0000, 0x0003,
    0xCCDA, 0xFFFD, 0x4924, 0x0000, 0x0006,
    0xFFFF, 0xFFFF,
    /* record 4 */
    0x0030, 0x010C, 0x010C, 0x0097,
    0xB6DC, 0xFFFF, 0x8000, 0xFFF9, 0x0003,
    0xB6DC, 0xFFFF, 0x3326, 0x0002, 0x0006,
    0xFFFF, 0xFFFF,
    /* record 5 */
    0x0030, 0x00FF, 0x00FF, 0x0091,
    0x0000, 0x0000, 0x8000, 0xFFFB, 0x0003,
    0x0000, 0x0000, 0x7FFA, 0x0001, 0x0007,
    0xFFFF, 0xFFFF,
    /* record 6 */
    0x0030, 0x0108, 0x0108, 0x0093,
    0x0000, 0x0001, 0x8006, 0xFFFE, 0x0005,
    0x8000, 0xFFF9, 0x8006, 0xFFFE, 0x0003,
    0xFFFF, 0xFFFF,
    /* record 7 */
    0x0030, 0x00F6, 0x00F6, 0x0093,
    0x7FFA, 0x0001, 0x0000, 0xFFFF, 0x0005,
    0x7FFA, 0x0001, 0x8000, 0x0006, 0x0003,
    0xFFFF, 0xFFFF,
    /* record 8 */
    0x0030, 0x00FF, 0x00FF, 0x0091,
    0x8000, 0x0004, 0x0000, 0x0000, 0x0003,
    0x8006, 0xFFFE, 0x0000, 0x0000, 0x0007,
    0xFFFF, 0xFFFF,
};

#define RECORDS 9u
#define RECORD_WORDS 16u
#define PROLOGUE_WORDS 2u

/* Measured per-span register state, in the order the capture emitted it.
 * Branch is not a field: $05A0 means AY1 > AY0, $05BA means AY1 < AY0, $0580
 * means AY1 == AY0. The three cases are covered by the captured data. */
static const stunrun_adsp_som_span_t SPANS[RECORDS][2] = {
    /* record 0: $05A0 (AY1 > AY0), then $0580 (AY1 == AY0) */
    { { 0x0099, 0x009E, 0x00A0, { 0x6DB8, 0xFFFF }, { 0xCCDA, 0xFFFD } },
      { 0x009E, 0x00A0, 0x00A0, { 0x6DB8, 0xFFFF }, { 0x8000, 0x0003 } } },
    /* record 1: $05BA (AY1 < AY0), then $0580 */
    { { 0x0099, 0x00A0, 0x009E, { 0x3326, 0x0002 }, { 0x9248, 0x0000 } },
      { 0x009E, 0x00A0, 0x00A0, { 0x8000, 0xFFFC }, { 0x9248, 0x0000 } } },
    /* record 2: $0580 twice; the second span has AY0 == AY1 == AX1 */
    { { 0x0099, 0x00A0, 0x00A0, { 0x9248, 0x0000 }, { 0x6DB8, 0xFFFF } },
      { 0x00A0, 0x00A0, 0x00A0, { 0x0000, 0x0000 }, { 0x0000, 0x0000 } } },
    /* record 3: $05BA, then $0580 */
    { { 0x0097, 0x009E, 0x0099, { 0x8000, 0x0006 }, { 0x4924, 0x0000 } },
      { 0x0099, 0x009E, 0x009E, { 0xCCDA, 0xFFFD }, { 0x4924, 0x0000 } } },
    /* record 4: $05A0, then $0580 */
    { { 0x0097, 0x0099, 0x009E, { 0xB6DC, 0xFFFF }, { 0x8000, 0xFFF9 } },
      { 0x0099, 0x009E, 0x009E, { 0xB6DC, 0xFFFF }, { 0x3326, 0x0002 } } },
    /* record 5: $05A0, then $0580 */
    { { 0x0091, 0x0093, 0x0099, { 0x0000, 0x0000 }, { 0x8000, 0xFFFB } },
      { 0x0093, 0x0099, 0x0099, { 0x0000, 0x0000 }, { 0x7FFA, 0x0001 } } },
    /* record 6: $05BA, then $0580 */
    { { 0x0093, 0x0099, 0x0097, { 0x0000, 0x0001 }, { 0x8006, 0xFFFE } },
      { 0x0097, 0x0099, 0x0099, { 0x8000, 0xFFF9 }, { 0x8006, 0xFFFE } } },
    /* record 7: $05A0, then $0580 */
    { { 0x0093, 0x0097, 0x0099, { 0x7FFA, 0x0001 }, { 0x0000, 0xFFFF } },
      { 0x0097, 0x0099, 0x0099, { 0x7FFA, 0x0001 }, { 0x8000, 0x0006 } } },
    /* record 8: $05BA, then $0580 */
    { { 0x0091, 0x0099, 0x0093, { 0x8000, 0x0004 }, { 0x0000, 0x0000 } },
      { 0x0093, 0x0099, 0x0099, { 0x8006, 0xFFFE }, { 0x0000, 0x0000 } } },
};

/* Header words, measured at PC $0515/$0565/$0567/$056B. */
static const struct {
    uint16_t control, start_x, delta_x, start_y;
} HEADERS[RECORDS] = {
    { 0x0030, 0x00FF, 0x00FF, 0x0099 }, /* 0 */
    { 0x0030, 0x00FF, 0x00FF, 0x0099 }, /* 1 */
    { 0x0030, 0x00FF, 0x00FF, 0x0099 }, /* 2 */
    { 0x0030, 0x00F2, 0x00F2, 0x0097 }, /* 3 */
    { 0x0030, 0x010C, 0x010C, 0x0097 }, /* 4 */
    { 0x0030, 0x00FF, 0x00FF, 0x0091 }, /* 5 */
    { 0x0030, 0x0108, 0x0108, 0x0093 }, /* 6 */
    { 0x0030, 0x00F6, 0x00F6, 0x0093 }, /* 7 */
    { 0x0030, 0x00FF, 0x00FF, 0x0091 }, /* 8 */
};

static void expect_record(size_t index, const uint16_t *got, size_t got_words)
{
    const uint16_t *want = &CAPTURED_SOM[PROLOGUE_WORDS + index * RECORD_WORDS];
    size_t i;

    check(got_words == RECORD_WORDS);
    if (got_words != RECORD_WORDS)
        return;
    for (i = 0; i < RECORD_WORDS; i++) {
        if (got[i] != want[i]) {
            printf("FAIL record %zu word %zu: got %04X want %04X\n",
                   index, i, got[i], want[i]);
            failures++;
            return;
        }
    }
}

int main(void)
{
    uint16_t stream[PROLOGUE_WORDS + RECORDS * RECORD_WORDS];
    size_t cursor = 0;
    size_t i;

    /* Reassemble the complete captured transaction from C output.
     * The first two words are the $0034-$0039 prologue (SOMCLK, then AR=$0000
     * latched twice); the record emitter does not produce them, so they are
     * copied from the capture and only the records are generated here. */
    stream[cursor++] = CAPTURED_SOM[0];
    stream[cursor++] = CAPTURED_SOM[1];

    for (i = 0; i < RECORDS; i++) {
        stunrun_adsp_som_record_t record;
        uint16_t out[RECORD_WORDS];
        size_t produced;

        record.control = HEADERS[i].control;
        record.start_x = HEADERS[i].start_x;
        record.delta_x = HEADERS[i].delta_x;
        record.start_y = HEADERS[i].start_y;
        record.spans = SPANS[i];
        record.span_count = 2;

        produced = stunrun_adsp_som_emit_record(&record, out, RECORD_WORDS);
        expect_record(i, out, produced);
        memcpy(&stream[cursor], out,
               produced * sizeof(uint16_t));
        cursor += produced;
    }

    check(cursor == PROLOGUE_WORDS + RECORDS * RECORD_WORDS);
    check(cursor == sizeof(CAPTURED_SOM) / sizeof(CAPTURED_SOM[0]));
    check(memcmp(stream, CAPTURED_SOM,
                 sizeof(CAPTURED_SOM)) == 0);

    /* Branch coverage: the captured data exercises $05A0, $05BA and $0580, and
     * each must produce the run length the ADSP actually latched. */
    check(stunrun_adsp_som_span_height(0x0099, 0x009E, 0x00A0) == 0x0006); /* > */
    check(stunrun_adsp_som_span_height(0x0099, 0x00A0, 0x009E) == 0x0006); /* < */
    check(stunrun_adsp_som_span_height(0x0099, 0x00A0, 0x00A0) == 0x0008); /* = */
    check(stunrun_adsp_som_span_height(0x00A0, 0x00A0, 0x00A0) == 0x0001);

    /* Guard levels differ by path. SPANS[2][1] is the captured span with
     * AY0 == AY1 == AX1; it is legal in loop position but the $0574/$0577
     * prologue uses IF LE, so it cannot be the first span. */
    {
        stunrun_adsp_som_record_t record;
        uint16_t out[RECORD_WORDS];

        record.control = 0x0030;
        record.start_x = 0x00FF;
        record.delta_x = 0x00FF;
        record.start_y = 0x0099;

        /* Degenerate span promoted to the prologue position: rejected. */
        record.spans = &SPANS[2][1];
        record.span_count = 1;
        check(stunrun_adsp_som_emit_record(&record, out, RECORD_WORDS) == 0);

        /* Same span in loop position, behind a valid first span: accepted,
         * and its run length lands in word 13. */
        record.spans = SPANS[2];
        record.span_count = 2;
        check(stunrun_adsp_som_emit_record(&record, out, RECORD_WORDS)
              == RECORD_WORDS);
        check(out[13] == 0x0001);
    }

    /* Header word order is NOT discriminated by the capture. All nine captured
     * records latch DM(I1,M1) and DM(I2,M2) to equal values ($00FF/$00FF,
     * $00F2/$00F2, ...), so no value-based expectation can tell words 1 and 2
     * apart. What the capture does pin is the attribution: word 1 was latched
     * at PC $0565 (AR = DM(I1,M1)) and word 2 at PC $0567 (AR = DM(I2,M2)).
     * The order therefore rests on the disassembly, and the synthetic record
     * below exists to keep a swap from passing silently. */
    {
        stunrun_adsp_som_record_t record;
        stunrun_adsp_som_span_t span;
        uint16_t out[RECORD_WORDS];

        span.ax1 = 0x0091; span.ay0 = 0x0093; span.ay1 = 0x0099;
        span.dm_3e.lo = 0x0000; span.dm_3e.hi = 0x0000;
        span.dm_3c.lo = 0x0000; span.dm_3c.hi = 0x0000;
        record.control = 0x0030;
        record.start_x = 0x00AA;  /* DM(I1,M1), latched first at $0565 */
        record.delta_x = 0x0055;  /* DM(I2,M2), latched second at $0567 */
        record.start_y = 0x0099;
        record.spans = &span;
        record.span_count = 1;

        check(stunrun_adsp_som_emit_record(&record, out, RECORD_WORDS) == 11u);
        check(out[0] == 0x0030);
        check(out[1] == 0x00AA);
        check(out[2] == 0x0055);
        check(out[3] == 0x0099);
        check(out[4] == 0x0000);
        check(out[5] == 0x0000);
        check(out[6] == 0x0000);
        check(out[7] == 0x0000);
        check(out[8] == 0x0003);  /* AY1 > AY0, so base = AY0: 0x0093-0x0091+1 */
        check(out[9] == 0xFFFF);
        check(out[10] == 0xFFFF);
    }

    /* Rejections must not write a partial record: the documented contract is
     * that a rejected emit leaves `out` untouched, so fill it with a poison
     * pattern and prove nothing was overwritten. */
    {
        stunrun_adsp_som_record_t record;
        uint16_t out[RECORD_WORDS];
        uint16_t poison[RECORD_WORDS];
        size_t i;
        int untouched;

        record.control = 0x0030;
        record.start_x = 0x00FF;
        record.delta_x = 0x00FF;
        record.start_y = 0x0099;
        record.spans = SPANS[0];
        record.span_count = 2;

        check(stunrun_adsp_som_emit_record(NULL, out, RECORD_WORDS) == 0);
        check(stunrun_adsp_som_emit_record(&record, NULL, RECORD_WORDS) == 0);
        check(stunrun_adsp_som_emit_record(&record, out, RECORD_WORDS - 1) == 0);
        record.spans = NULL;
        check(stunrun_adsp_som_emit_record(&record, out, RECORD_WORDS) == 0);

        /* A guard failure after the header words would have been emitted. */
        record.spans = &SPANS[2][1];
        record.span_count = 1;
        for (i = 0; i < RECORD_WORDS; i++) {
            out[i] = 0x5A5Au;
            poison[i] = 0x5A5Au;
        }
        check(stunrun_adsp_som_emit_record(&record, out, RECORD_WORDS) == 0);
        untouched = 1;
        for (i = 0; i < RECORD_WORDS; i++)
            if (out[i] != poison[i])
                untouched = 0;
        check(untouched);
    }

    /* Word count must match what is actually emitted. */
    check(stunrun_adsp_som_record_words(0) == 6u);
    check(stunrun_adsp_som_record_words(1) == 11u);
    check(stunrun_adsp_som_record_words(2) == RECORD_WORDS);
    check(stunrun_adsp_som_record_words((size_t)-1) == 0u);

    if (failures == 0)
        puts("PASS");
    return failures == 0 ? 0 : 1;
}