/* Captured inputs for the ADSP SOM producer, in one place.
 *
 * The frame-551 SOM transaction is the only capture in this repository with
 * both halves recorded: the 146 words latched on SOMLATCH, and the data-memory
 * window plus register snapshots the ADSP held while producing them. The slice
 * tests in this directory need the inputs; the native view
 * (native/adsp_som_view.c, reachable as `./run.sh --adsp-som`) needs the same
 * ones to draw the producer's output. Before this module those inputs lived in
 * som_walk_test.c alone, which meant a second copy for the view would have been
 * free to drift from the oracle.
 *
 * These are the INPUTS, not a table of expected outputs. Expected record words
 * are exposed separately, and only so a caller can self-check what it derived;
 * nothing in reproduction/adsp/ computes geometry from them.
 *
 * See docs/mc68010-boot-findings.md sections 66, 68, 71 and 72.
 */
#ifndef STUNRUN_ADSP_SOM_FIXTURE_H
#define STUNRUN_ADSP_SOM_FIXTURE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One transaction: 9 records, 146 words, two $0000 prologue words and a $FFFF
 * $FFFF terminator per record. */
#define STUNRUN_ADSP_SOM_FIXTURE_RECORDS   9u
#define STUNRUN_ADSP_SOM_FIXTURE_REC_WORDS  16u

/* The data-memory window handed to stunrun_adsp_som_derive_record:
 * DM[$0030..$0045], addressed by absolute DM address. */
#define STUNRUN_ADSP_SOM_FIXTURE_DM_BASE   0x0030u
#define STUNRUN_ADSP_SOM_FIXTURE_DM_WORDS  22u

/* Program memory sized to the absolute PM index som_math.c uses. The
 * coefficient table occupies PM[$1037..$1076]; PM[$1036] is the zero slot. */
#define STUNRUN_ADSP_SOM_FIXTURE_PM_WORDS  0x1077u

/* DM($0039) in the capture: loop count for the scan, doubled into the ring
 * length. Every record has 3, so the ring is six words. */
#define STUNRUN_ADSP_SOM_FIXTURE_CNTR      3u
#define STUNRUN_ADSP_SOM_FIXTURE_RING_LEN  6u

/* The six-word ring, DM($0040)..DM($0045), for one record. NULL when record is
 * out of range. */
const uint16_t *stunrun_adsp_som_fixture_dm(unsigned record);

/* The 16 words that record latched on SOMLATCH. NULL when record is out of
 * range. For self-checking a derivation, never as an input to one. */
const uint16_t *stunrun_adsp_som_fixture_expected(unsigned record);

/* What $052F-$0544 selected, captured at $0564, so a caller can assert the scan
 * reproduced it. 0 when record is out of range. */
unsigned stunrun_adsp_som_fixture_entry_i1(unsigned record);
unsigned stunrun_adsp_som_fixture_entry_i2(unsigned record);

/* The coefficient table, indexed by absolute PM address. Never NULL. */
const uint32_t *stunrun_adsp_som_fixture_pm(void);

/* The table holds floor($8000/n) in bits 23:8 for n = 1..64, but the helpers
 * decline any selector above $40, so a captured run only ever selects part of
 * it. Exposed so a caller can be precise about which end it exercised. */
unsigned stunrun_adsp_som_fixture_pm_selected_max(void);

/* Whether PM[$1036] -- the n = 0 slot -- is zero, which is why the helpers
 * refuse selector 0 rather than dividing by it. 1 when it is. */
unsigned stunrun_adsp_som_fixture_pm_n0_is_zero(void);

#ifdef __cplusplus
}
#endif

#endif /* STUNRUN_ADSP_SOM_FIXTURE_H */
