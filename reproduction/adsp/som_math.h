/* Bounded ADSP-2100 interpolation helper used by the SOM producer.
 *
 * This is not an ADSP core and not the whole PC $05DD routine. It implements
 * its captured low-index path through the DM sample reads, PM coefficient
 * lookup, and `MR = AR * MY0 (SU)`. The >$40 scaling branch is unsupported.
 * See docs/mc68010-boot-findings.md §64.
 */
#ifndef STUNRUN_ADSP_SOM_MATH_H
#define STUNRUN_ADSP_SOM_MATH_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STUNRUN_ADSP_SOM_DATA_WORDS 0x2000u
#define STUNRUN_ADSP_SOM_PROGRAM_WORDS 0x2000u
#define STUNRUN_ADSP_SOM_COEFF_BASE 0x1036u
#define STUNRUN_ADSP_SOM_COEFF_LIMIT 0x0040u

typedef struct stunrun_adsp_mac_pair {
    uint16_t mr0;
    uint16_t mr1;
} stunrun_adsp_mac_pair_t;

/* Execute the measured low-index branch of ADSP subroutines $05DD/$0607.
 *
 * lookup_index is the subroutine-entry AR: it indexes PM[$1036 + AR] for MY0
 * and selects the fast branch through 0x40. i0/i2 are the caller's index
 * registers; the code reads DM[i0-1] and DM[i2-1], subtracts them as 16-bit
 * signed values, then multiplies the difference by unsigned MY0. The captured
 * MAME vectors establish that MR1:MR0 is the low 32 bits of 2 * AR * MY0.
 *
 * data_words is addressed by absolute DM address, as a window: DM(a) is
 * data_words[a - data_base]. A read outside the window is rejected, so a
 * truncated capture fails instead of returning plausible garbage.
 *
 * Program words are 32-bit MAME containers for 24-bit PM data, indexed by
 * absolute PM address; MY0 is bits 23:8. Returns 1 on the implemented
 * low-index path. Rejects selector >0x40, invalid pointers, or out-of-range
 * addresses without touching output. */
int stunrun_adsp_som_interpolation_low_branch(
    uint16_t lookup_index, uint16_t i0, uint16_t i2,
    const uint16_t *data_words, size_t data_word_count, uint16_t data_base,
    const uint32_t *program_words, size_t program_word_count,
    stunrun_adsp_mac_pair_t *out);

#ifdef __cplusplus
}
#endif

#endif /* STUNRUN_ADSP_SOM_MATH_H */
