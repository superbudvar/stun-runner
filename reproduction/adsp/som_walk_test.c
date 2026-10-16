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

#include "som_fixture.h"
#include "som_walk.h"

static int failures;

#define check(c) do { if (!(c)) { printf("FAIL %s\n", #c); failures++; } } while (0)


#define RECORDS     STUNRUN_ADSP_SOM_FIXTURE_RECORDS
#define RECORD_WORDS STUNRUN_ADSP_SOM_FIXTURE_REC_WORDS
#define DM_BASE     STUNRUN_ADSP_SOM_FIXTURE_DM_BASE
#define DM_WORDS    STUNRUN_ADSP_SOM_FIXTURE_DM_WORDS
#define PROLOGUE_WORDS 2u   /* PC $0035 and $0039, AR still $0000 */
#define RING_BASE 0x0040u
#define RING_WORDS 6u
#define RING_LENGTH 6u /* L0 = L1 = L2 = L4 = $0006 in the capture */



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
            stunrun_adsp_som_fixture_dm(i), DM_WORDS, DM_BASE,
            stunrun_adsp_som_fixture_pm(), STUNRUN_ADSP_SOM_FIXTURE_PM_WORDS, spans, 8u, &record));
        check(record.control == stunrun_adsp_som_fixture_expected(i)[0]);
        check(record.span_count == 2u);
        check(stunrun_adsp_som_record_words(record.span_count) == RECORD_WORDS);
        check(stunrun_adsp_som_emit_record(&record, out, RECORD_WORDS)
              == RECORD_WORDS);
        if (memcmp(out, stunrun_adsp_som_fixture_expected(i), sizeof(stunrun_adsp_som_fixture_expected(i))) != 0) {
            printf("FAIL record %lu word stream\n", (unsigned long)i);
            for (k = 0; k < RECORD_WORDS; k++)
                if (out[k] != stunrun_adsp_som_fixture_expected(i)[k])
                    printf("   word %lu got %04X want %04X\n",
                           (unsigned long)k, out[k], stunrun_adsp_som_fixture_expected(i)[k]);
            failures++;
        }
    }

    /* The scan in isolation: the entry pointers it selects must equal the ones
     * observed at $0564, and the ring length must be the doubled counter.
     * DM($0039) is $0003 in all nine records, so L is $0006. */
    for (i = 0; i < RECORDS; i++) {
        uint16_t cntr = stunrun_adsp_som_fixture_dm(i)[STUNRUN_ADSP_SOM_CNTR_ADDR - DM_BASE];
        check(cntr == 3u);
        check(stunrun_adsp_som_scan_select(
            stunrun_adsp_som_fixture_dm(i), DM_WORDS, DM_BASE, cntr,
            &entry_i1[i], &entry_i2[i], &ring_length[i]));
        check(ring_length[i] == 6u);
    }
    /* Expected entry pointers, captured at $0564. The values live in the
     * fixture so the native view asserts against the same ones; the assertion
     * that the scan REPRODUCED them stays here. */
    for (i = 0; i < RECORDS; i++) {
        check(entry_i1[i] == stunrun_adsp_som_fixture_entry_i1(i));
        check(entry_i2[i] == stunrun_adsp_som_fixture_entry_i2(i));
    }

    /* The walk's own arithmetic, isolated from the scan: every captured span
     * straddles AX1 strictly. */
    for (i = 0; i < RECORDS; i++) {
        stunrun_adsp_som_walk_t walk;
        memset(spans, 0, sizeof(spans));
        check(stunrun_adsp_som_walk_record(
            stunrun_adsp_som_fixture_dm(i) + (STUNRUN_ADSP_SOM_SCAN_BASE - DM_BASE),
            DM_WORDS - (STUNRUN_ADSP_SOM_SCAN_BASE - DM_BASE),
            STUNRUN_ADSP_SOM_SCAN_BASE,
            stunrun_adsp_som_fixture_pm(), STUNRUN_ADSP_SOM_FIXTURE_PM_WORDS,
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
            stunrun_adsp_som_fixture_pm(), STUNRUN_ADSP_SOM_FIXTURE_PM_WORDS,
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
            stunrun_adsp_som_fixture_dm(0), 4u, DM_BASE,
            stunrun_adsp_som_fixture_pm(), STUNRUN_ADSP_SOM_FIXTURE_PM_WORDS,
            66, 66, RING_LENGTH, spans, 8u, &walk));
        check(!stunrun_adsp_som_walk_record(
            NULL, DM_WORDS, DM_BASE, stunrun_adsp_som_fixture_pm(), STUNRUN_ADSP_SOM_FIXTURE_PM_WORDS,
            66, 66, RING_LENGTH, spans, 8u, &walk));
        check(!stunrun_adsp_som_walk_record(
            stunrun_adsp_som_fixture_dm(0), DM_WORDS, DM_BASE, stunrun_adsp_som_fixture_pm(), STUNRUN_ADSP_SOM_FIXTURE_PM_WORDS,
            66, 66, RING_LENGTH, spans, 8u, NULL));
        /* A coefficient table too short for the index the helper computes:
         * refuse rather than read past it. */
        check(!stunrun_adsp_som_walk_record(
            stunrun_adsp_som_fixture_dm(0), DM_WORDS, DM_BASE, stunrun_adsp_som_fixture_pm(), 2u,
            66, 66, RING_LENGTH, spans, 8u, &walk));
        /* Too little room for the walk it actually needs: fail, do not
         * truncate the record. */
        check(!stunrun_adsp_som_walk_record(
            stunrun_adsp_som_fixture_dm(0), DM_WORDS, DM_BASE, stunrun_adsp_som_fixture_pm(), STUNRUN_ADSP_SOM_FIXTURE_PM_WORDS,
            66, 66, RING_LENGTH, spans, 1u, &walk));

        /* The same rejections through the composed entry point. */
        check(!stunrun_adsp_som_derive_record(
            stunrun_adsp_som_fixture_dm(0), DM_WORDS, DM_BASE, stunrun_adsp_som_fixture_pm(), STUNRUN_ADSP_SOM_FIXTURE_PM_WORDS,
            spans, 8u, NULL));
        check(!stunrun_adsp_som_derive_record(
            NULL, DM_WORDS, DM_BASE, stunrun_adsp_som_fixture_pm(), STUNRUN_ADSP_SOM_FIXTURE_PM_WORDS,
            spans, 8u, &record));
        check(!stunrun_adsp_som_derive_record(
            stunrun_adsp_som_fixture_dm(0), DM_WORDS, DM_BASE, stunrun_adsp_som_fixture_pm(), STUNRUN_ADSP_SOM_FIXTURE_PM_WORDS,
            spans, 1u, &record));
        /* A window that does not reach DM($0036) cannot supply the control
         * word. */
        check(!stunrun_adsp_som_derive_record(
            stunrun_adsp_som_fixture_dm(0) + 8, DM_WORDS - 8, DM_BASE + 8,
            stunrun_adsp_som_fixture_pm(), STUNRUN_ADSP_SOM_FIXTURE_PM_WORDS, spans, 8u, &record));

        /* The scan rejects a ring too short to carry both chains, and NULL
         * outputs, rather than inventing pointers. */
        check(!stunrun_adsp_som_scan_select(
            stunrun_adsp_som_fixture_dm(0), DM_WORDS, DM_BASE, 1u, &junk, &junk, &junk));
        check(!stunrun_adsp_som_scan_select(
            stunrun_adsp_som_fixture_dm(0), DM_WORDS, DM_BASE, 0u, &junk, &junk, &junk));
        check(!stunrun_adsp_som_scan_select(
            stunrun_adsp_som_fixture_dm(0), DM_WORDS, DM_BASE, 3u, NULL, &junk, &junk));
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
                stunrun_adsp_som_fixture_dm(i), DM_WORDS, DM_BASE,
                stunrun_adsp_som_fixture_pm(), STUNRUN_ADSP_SOM_FIXTURE_PM_WORDS, spans, 8u, &record));
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
                         stunrun_adsp_som_fixture_expected(i), sizeof(stunrun_adsp_som_fixture_expected(i))) == 0);
    }

    if (failures == 0)
        puts("PASS");
    return failures == 0 ? 0 : 1;
}
