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
#define PROLOGUE_WORDS 2u   /* PC $0035 and $0039, AR still $0000 */
#define RING_BASE 0x0040u
#define RING_WORDS 6u
#define RING_LENGTH 6u /* L0 = L1 = L2 = L4 = $0006 in the capture */

/* DM[$0030..$0045] per record, captured at $0564 from the frozen oracle:
 * control word at $0036, ring length/count at $0039, and the six-word
 * ring at $0040..$0045. The entry pointers in the comments are what
 * $052F-$0544 selects and are NOT inputs -- derive_record computes
 * them and the test asserts they match. */
#define DM_BASE 0x0030u
#define DM_WORDS 22u
static const uint16_t DM_WINDOWS[RECORDS][DM_WORDS] = {
    /* record 0: entry I1=$0042 I2=$0042 */
    {
    0x0000, 0x0001, 0x000A, 0xFFFE, 0x998B, 0x0001, 0x0030, 0x0000,
    0x0000, 0x0003, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x00F4, 0x009E, 0x00FF, 0x0099, 0x00FB, 0x00A0,





    },
    /* record 1: entry I1=$0042 I2=$0042 */
    {
    0x0000, 0x0001, 0x000A, 0xFFFE, 0x998D, 0x0001, 0x0030, 0x0000,
    0x0000, 0x0003, 0x0000, 0x0000, 0x8000, 0x0003, 0x6DB8, 0xFFFF,
    0x0103, 0x00A0, 0x00FF, 0x0099, 0x010A, 0x009E,





    },
    /* record 2: entry I1=$0042 I2=$0042 */
    {
    0x0000, 0x0001, 0x000A, 0xFFFE, 0x998F, 0x0001, 0x0030, 0x0000,
    0x0000, 0x0003, 0x0000, 0x0000, 0x9248, 0x0000, 0x8000, 0xFFFC,
    0x00FB, 0x00A0, 0x00FF, 0x0099, 0x0103, 0x00A0,





    },
    /* record 3: entry I1=$0040 I2=$0040 */
    {
    0x0000, 0x0001, 0x000A, 0xFFFE, 0x9991, 0x0001, 0x0030, 0x0000,
    0x0000, 0x0003, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x00F2, 0x0097, 0x00FF, 0x0099, 0x00F4, 0x009E,





    },
    /* record 4: entry I1=$0044 I2=$0044 */
    {
    0x0000, 0x0001, 0x000A, 0xFFFE, 0x9993, 0x0001, 0x0030, 0x0000,
    0x0000, 0x0003, 0x0000, 0x0000, 0x4924, 0x0000, 0xCCDA, 0xFFFD,
    0x010A, 0x009E, 0x00FF, 0x0099, 0x010C, 0x0097,





    },
    /* record 5: entry I1=$0040 I2=$0040 */
    {
    0x0000, 0x0001, 0x000A, 0xFFFE, 0x9995, 0x0001, 0x0030, 0x0000,
    0x0000, 0x0003, 0x0000, 0x0000, 0x3326, 0x0002, 0xB6DC, 0xFFFF,
    0x00FF, 0x0091, 0x00FF, 0x0099, 0x00F6, 0x0093,





    },
    /* record 6: entry I1=$0044 I2=$0044 */
    {
    0x0000, 0x0001, 0x000A, 0xFFFE, 0x9997, 0x0001, 0x0030, 0x0000,
    0x0000, 0x0003, 0x0000, 0x0000, 0x7FFA, 0x0001, 0x0000, 0x0000,
    0x010C, 0x0097, 0x00FF, 0x0099, 0x0108, 0x0093,





    },
    /* record 7: entry I1=$0040 I2=$0040 */
    {
    0x0000, 0x0001, 0x000A, 0xFFFE, 0x9999, 0x0001, 0x0030, 0x0000,
    0x0000, 0x0003, 0x0000, 0x0000, 0x8006, 0xFFFE, 0x8000, 0xFFF9,
    0x00F6, 0x0093, 0x00FF, 0x0099, 0x00F2, 0x0097,





    },
    /* record 8: entry I1=$0040 I2=$0040 */
    {
    0x0000, 0x0001, 0x000A, 0xFFFE, 0x999B, 0x0001, 0x0030, 0x0000,
    0x0000, 0x0003, 0x0000, 0x0000, 0x8000, 0x0006, 0x7FFA, 0x0001,
    0x00FF, 0x0091, 0x0108, 0x0093, 0x00FF, 0x0099,





    },
};

/* The captured record words, which the C must reproduce. */
static const uint16_t EXPECTED[RECORDS][RECORD_WORDS] = {
    /* record 0 */
    {
    0x0030, 0x00FF, 0x00FF, 0x0099, 0x6DB8, 0xFFFF, 0xCCDA, 0xFFFD,
    0x0006, 0x6DB8, 0xFFFF, 0x8000, 0x0003, 0x0003, 0xFFFF, 0xFFFF,
    },
    /* record 1 */
    {
    0x0030, 0x00FF, 0x00FF, 0x0099, 0x3326, 0x0002, 0x9248, 0x0000,
    0x0006, 0x8000, 0xFFFC, 0x9248, 0x0000, 0x0003, 0xFFFF, 0xFFFF,
    },
    /* record 2 */
    {
    0x0030, 0x00FF, 0x00FF, 0x0099, 0x9248, 0x0000, 0x6DB8, 0xFFFF,
    0x0008, 0x0000, 0x0000, 0x0000, 0x0000, 0x0001, 0xFFFF, 0xFFFF,
    },
    /* record 3 */
    {
    0x0030, 0x00F2, 0x00F2, 0x0097, 0x8000, 0x0006, 0x4924, 0x0000,
    0x0003, 0xCCDA, 0xFFFD, 0x4924, 0x0000, 0x0006, 0xFFFF, 0xFFFF,
    },
    /* record 4 */
    {
    0x0030, 0x010C, 0x010C, 0x0097, 0xB6DC, 0xFFFF, 0x8000, 0xFFF9,
    0x0003, 0xB6DC, 0xFFFF, 0x3326, 0x0002, 0x0006, 0xFFFF, 0xFFFF,
    },
    /* record 5 */
    {
    0x0030, 0x00FF, 0x00FF, 0x0091, 0x0000, 0x0000, 0x8000, 0xFFFB,
    0x0003, 0x0000, 0x0000, 0x7FFA, 0x0001, 0x0007, 0xFFFF, 0xFFFF,
    },
    /* record 6 */
    {
    0x0030, 0x0108, 0x0108, 0x0093, 0x0000, 0x0001, 0x8006, 0xFFFE,
    0x0005, 0x8000, 0xFFF9, 0x8006, 0xFFFE, 0x0003, 0xFFFF, 0xFFFF,
    },
    /* record 7 */
    {
    0x0030, 0x00F6, 0x00F6, 0x0093, 0x7FFA, 0x0001, 0x0000, 0xFFFF,
    0x0005, 0x7FFA, 0x0001, 0x8000, 0x0006, 0x0003, 0xFFFF, 0xFFFF,
    },
    /* record 8 */
    {
    0x0030, 0x00FF, 0x00FF, 0x0091, 0x8000, 0x0004, 0x0000, 0x0000,
    0x0003, 0x8006, 0xFFFE, 0x0000, 0x0000, 0x0007, 0xFFFF, 0xFFFF,
    },
};

int main(void)
{
    stunrun_adsp_som_span_t spans[8];
    uint16_t out[RECORD_WORDS];
    uint16_t entry_i1[RECORDS], entry_i2[RECORDS], ring_length[RECORDS];
    size_t i, k;

    /* The headline assertion. For each record the C is given only the
     * data-memory window and the coefficient table -- no pointer, no span
     * list, no side value -- and must produce all sixteen captured words. */
    for (i = 0; i < RECORDS; i++) {
        stunrun_adsp_som_record_t record;

        memset(spans, 0, sizeof(spans));
        check(stunrun_adsp_som_derive_record(
            DM_WINDOWS[i], DM_WORDS, DM_BASE,
            PM_PROGRAM, PM_PROGRAM_WORDS, spans, 8u, &record));
        check(record.control == EXPECTED[i][0]);
        check(record.span_count == 2u);
        check(stunrun_adsp_som_record_words(record.span_count) == RECORD_WORDS);
        check(stunrun_adsp_som_emit_record(&record, out, RECORD_WORDS)
              == RECORD_WORDS);
        if (memcmp(out, EXPECTED[i], sizeof(EXPECTED[i])) != 0) {
            printf("FAIL record %lu word stream\n", (unsigned long)i);
            for (k = 0; k < RECORD_WORDS; k++)
                if (out[k] != EXPECTED[i][k])
                    printf("   word %lu got %04X want %04X\n",
                           (unsigned long)k, out[k], EXPECTED[i][k]);
            failures++;
        }
    }

    /* The scan in isolation: the entry pointers it selects must equal the ones
     * observed at $0564, and the ring length must be the doubled counter.
     * DM($0039) is $0003 in all nine records, so L is $0006. */
    for (i = 0; i < RECORDS; i++) {
        uint16_t cntr = DM_WINDOWS[i][STUNRUN_ADSP_SOM_CNTR_ADDR - DM_BASE];
        check(cntr == 3u);
        check(stunrun_adsp_som_scan_select(
            DM_WINDOWS[i], DM_WORDS, DM_BASE, cntr,
            &entry_i1[i], &entry_i2[i], &ring_length[i]));
        check(ring_length[i] == 6u);
    }
    /* Expected entry pointers, captured at $0564 in record order. */
    {
        static const uint16_t WANT_I1[RECORDS] = {
            66, 66, 66, 64, 68, 64, 68, 64, 64
        };
        static const uint16_t WANT_I2[RECORDS] = {
            66, 66, 66, 64, 68, 64, 68, 64, 64
        };
        for (i = 0; i < RECORDS; i++) {
            check(entry_i1[i] == WANT_I1[i]);
            check(entry_i2[i] == WANT_I2[i]);
        }
    }

    /* The walk's own arithmetic, isolated from the scan: every captured span
     * straddles AX1 strictly. */
    for (i = 0; i < RECORDS; i++) {
        stunrun_adsp_som_walk_t walk;
        memset(spans, 0, sizeof(spans));
        check(stunrun_adsp_som_walk_record(
            DM_WINDOWS[i] + (STUNRUN_ADSP_SOM_SCAN_BASE - DM_BASE),
            DM_WORDS - (STUNRUN_ADSP_SOM_SCAN_BASE - DM_BASE),
            STUNRUN_ADSP_SOM_SCAN_BASE,
            PM_PROGRAM, PM_PROGRAM_WORDS,
            entry_i1[i], entry_i2[i], ring_length[i], spans, 8u, &walk));
        check(walk.terminated == 1);
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
        stunrun_adsp_som_record_t record;
        uint16_t junk;

        memset(spans, 0, sizeof(spans));
        check(!stunrun_adsp_som_walk_record(
            DM_WINDOWS[0], 4u, DM_BASE,
            PM_PROGRAM, PM_PROGRAM_WORDS,
            66, 66, RING_LENGTH, spans, 8u, &walk));
        check(!stunrun_adsp_som_walk_record(
            NULL, DM_WORDS, DM_BASE, PM_PROGRAM, PM_PROGRAM_WORDS,
            66, 66, RING_LENGTH, spans, 8u, &walk));
        check(!stunrun_adsp_som_walk_record(
            DM_WINDOWS[0], DM_WORDS, DM_BASE, PM_PROGRAM, PM_PROGRAM_WORDS,
            66, 66, RING_LENGTH, spans, 8u, NULL));
        /* A coefficient table too short for the index the helper computes:
         * refuse rather than read past it. */
        check(!stunrun_adsp_som_walk_record(
            DM_WINDOWS[0], DM_WORDS, DM_BASE, PM_PROGRAM, 2u,
            66, 66, RING_LENGTH, spans, 8u, &walk));
        /* Too little room for the walk it actually needs: fail, do not
         * truncate the record. */
        check(!stunrun_adsp_som_walk_record(
            DM_WINDOWS[0], DM_WORDS, DM_BASE, PM_PROGRAM, PM_PROGRAM_WORDS,
            66, 66, RING_LENGTH, spans, 1u, &walk));

        /* The same rejections through the composed entry point. */
        check(!stunrun_adsp_som_derive_record(
            DM_WINDOWS[0], DM_WORDS, DM_BASE, PM_PROGRAM, PM_PROGRAM_WORDS,
            spans, 8u, NULL));
        check(!stunrun_adsp_som_derive_record(
            NULL, DM_WORDS, DM_BASE, PM_PROGRAM, PM_PROGRAM_WORDS,
            spans, 8u, &record));
        check(!stunrun_adsp_som_derive_record(
            DM_WINDOWS[0], DM_WORDS, DM_BASE, PM_PROGRAM, PM_PROGRAM_WORDS,
            spans, 1u, &record));
        /* A window that does not reach DM($0036) cannot supply the control
         * word. */
        check(!stunrun_adsp_som_derive_record(
            DM_WINDOWS[0] + 8, DM_WORDS - 8, DM_BASE + 8,
            PM_PROGRAM, PM_PROGRAM_WORDS, spans, 8u, &record));

        /* The scan rejects a ring too short to carry both chains, and NULL
         * outputs, rather than inventing pointers. */
        check(!stunrun_adsp_som_scan_select(
            DM_WINDOWS[0], DM_WORDS, DM_BASE, 1u, &junk, &junk, &junk));
        check(!stunrun_adsp_som_scan_select(
            DM_WINDOWS[0], DM_WORDS, DM_BASE, 0u, &junk, &junk, &junk));
        check(!stunrun_adsp_som_scan_select(
            DM_WINDOWS[0], DM_WORDS, DM_BASE, 3u, NULL, &junk, &junk));
        check(!stunrun_adsp_som_scan_select(
            NULL, DM_WORDS, DM_BASE, 3u, &junk, &junk, &junk));
    }

    /* The complete captured transaction. The two leading words are the
     * $0034-$0039 prologue -- SOMCLK, then AR still $0000 latched twice -- which
     * this slice does not produce, so they are written as the captured zeros
     * and only the nine records are generated. 2 + 9 * 16 == 146, which is
     * exactly the captured SOMLATCH stream length. */
    {
        uint16_t stream[PROLOGUE_WORDS + RECORDS * RECORD_WORDS];
        size_t cursor = 0;

        stream[cursor++] = 0x0000;   /* PC $0035 */
        stream[cursor++] = 0x0000;   /* PC $0039 */

        for (i = 0; i < RECORDS; i++) {
            stunrun_adsp_som_record_t record;
            memset(spans, 0, sizeof(spans));
            check(stunrun_adsp_som_derive_record(
                DM_WINDOWS[i], DM_WORDS, DM_BASE,
                PM_PROGRAM, PM_PROGRAM_WORDS, spans, 8u, &record));
            {
                size_t produced = stunrun_adsp_som_emit_record(
                    &record, &stream[cursor],
                    sizeof(stream) / sizeof(stream[0]) - cursor);
                check(produced == RECORD_WORDS);
                cursor += produced;
            }
        }

        check(cursor == sizeof(stream) / sizeof(stream[0]));
        for (i = 0; i < RECORDS; i++)
            check(memcmp(&stream[PROLOGUE_WORDS + i * RECORD_WORDS],
                         EXPECTED[i], sizeof(EXPECTED[i])) == 0);
    }

    if (failures == 0)
        puts("PASS");
    return failures == 0 ? 0 : 1;
}
