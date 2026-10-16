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
 * Nothing in the emitted record is copied from the capture. Given the DM ring,
 * the coefficient table and the entry pointers, the walk derives the header
 * words, the span count, each span's AX1/AY0/AY1, both interpolated 32-bit
 * side values, and the run length; the whole 16-word record is then compared
 * word for word against the capture.
 */
#include <stdio.h>
#include <string.h>

#include "som_walk.h"

static int failures;

#define check(c) do { if (!(c)) { printf("FAIL %s\n", #c); failures++; } } while (0)

/* The coefficient table the two helpers index at $1036, captured from
 * the frame-551 ADSP program image (byte-identical at frame 600).
 * MAME stores 24-bit PM values in 32-bit containers; $05DD and $0607
 * take bits 23:8. The values are exactly floor($8000/n) for n = 1..$40
 * and zero at n = 0 -- see docs/mc68010-boot-findings.md section 68.
 *
 * Indexed by absolute PM address, matching som_math.c. The words not
 * listed here are instructions and are never read by these helpers. */
#define PM_PROGRAM_WORDS 0x1077u
static const uint32_t PM_PROGRAM[PM_PROGRAM_WORDS] = {
    [0x1037] = 0x800000,
    [0x1038] = 0x400000,
    [0x1039] = 0x2AAA00,
    [0x103A] = 0x200000,
    [0x103B] = 0x199900,
    [0x103C] = 0x155500,
    [0x103D] = 0x124900,
    [0x103E] = 0x100000,
    [0x103F] = 0x0E3800,
    [0x1040] = 0x0CCC00,
    [0x1041] = 0x0BA200,
    [0x1042] = 0x0AAA00,
    [0x1043] = 0x09D800,
    [0x1044] = 0x092400,
    [0x1045] = 0x088800,
    [0x1046] = 0x080000,
    [0x1047] = 0x078700,
    [0x1048] = 0x071C00,
    [0x1049] = 0x06BC00,
    [0x104A] = 0x066600,
    [0x104B] = 0x061800,
    [0x104C] = 0x05D100,
    [0x104D] = 0x059000,
    [0x104E] = 0x055500,
    [0x104F] = 0x051E00,
    [0x1050] = 0x04EC00,
    [0x1051] = 0x04BD00,
    [0x1052] = 0x049200,
    [0x1053] = 0x046900,
    [0x1054] = 0x044400,
    [0x1055] = 0x042100,
    [0x1056] = 0x040000,
    [0x1057] = 0x03E000,
    [0x1058] = 0x03C300,
    [0x1059] = 0x03A800,
    [0x105A] = 0x038E00,
    [0x105B] = 0x037500,
    [0x105C] = 0x035E00,
    [0x105D] = 0x034800,
    [0x105E] = 0x033300,
    [0x105F] = 0x031F00,
    [0x1060] = 0x030C00,
    [0x1061] = 0x02FA00,
    [0x1062] = 0x02E800,
    [0x1063] = 0x02D800,
    [0x1064] = 0x02C800,
    [0x1065] = 0x02B900,
    [0x1066] = 0x02AA00,
    [0x1067] = 0x029C00,
    [0x1068] = 0x028F00,
    [0x1069] = 0x028200,
    [0x106A] = 0x027600,
    [0x106B] = 0x026A00,
    [0x106C] = 0x025E00,
    [0x106D] = 0x025300,
    [0x106E] = 0x024900,
    [0x106F] = 0x023E00,
    [0x1070] = 0x023400,
    [0x1071] = 0x022B00,
    [0x1072] = 0x022200,
    [0x1073] = 0x021900,
    [0x1074] = 0x021000,
    [0x1075] = 0x020800,
    [0x1076] = 0x020000,
};

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
            PM_PROGRAM, PM_PROGRAM_WORDS,
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

        /* The two interpolated side values per span are computed by the walk
         * now, so nothing is copied in before emitting. */
        for (k = 0; k < walk.span_count; k++) {
            check(spans[k].dm_3e.lo == VECTORS[i].expected[4 + k * 5 + 0]);
            check(spans[k].dm_3e.hi == VECTORS[i].expected[4 + k * 5 + 1]);
            check(spans[k].dm_3c.lo == VECTORS[i].expected[4 + k * 5 + 2]);
            check(spans[k].dm_3c.hi == VECTORS[i].expected[4 + k * 5 + 3]);
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
            PM_PROGRAM, PM_PROGRAM_WORDS,
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
            degenerate, RING_WORDS, RING_BASE,
            PM_PROGRAM, PM_PROGRAM_WORDS,
            0x0042u, 0x0042u, RING_LENGTH, spans, 8u, &walk));
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
            VECTORS[0].ring, 4u, RING_BASE,
            PM_PROGRAM, PM_PROGRAM_WORDS,
            VECTORS[0].i1, VECTORS[0].i2, RING_LENGTH, spans, 8u, &walk));
        check(!stunrun_adsp_som_walk_record(
            NULL, RING_WORDS, RING_BASE,
            PM_PROGRAM, PM_PROGRAM_WORDS,
            VECTORS[0].i1, VECTORS[0].i2, RING_LENGTH, spans, 8u, &walk));
        check(!stunrun_adsp_som_walk_record(
            VECTORS[0].ring, RING_WORDS, RING_BASE,
            PM_PROGRAM, PM_PROGRAM_WORDS,
            VECTORS[0].i1, VECTORS[0].i2, RING_LENGTH, spans, 8u, NULL));
        /* A coefficient table too short for the index the helper computes:
         * refuse rather than read past it. */
        check(!stunrun_adsp_som_walk_record(
            VECTORS[0].ring, RING_WORDS, RING_BASE,
            PM_PROGRAM, 2u,
            VECTORS[0].i1, VECTORS[0].i2, RING_LENGTH, spans, 8u, &walk));
        /* Too little room for the walk it actually needs: fail, do not
         * truncate the record. */
        check(!stunrun_adsp_som_walk_record(
            VECTORS[0].ring, RING_WORDS, RING_BASE,
            PM_PROGRAM, PM_PROGRAM_WORDS,
            VECTORS[0].i1, VECTORS[0].i2, RING_LENGTH, spans, 1u, &walk));
    }

    if (failures == 0)
        puts("PASS");
    return failures == 0 ? 0 : 1;
}