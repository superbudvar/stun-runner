/* Sparse evidence-only MP=1 SIMBUF words for the frame-551 producer run.
 *
 * Every word below was READ FROM THE OBJECT ROM, never copied from what the
 * ADSP did with it. `tools/read-adsp-object-rom-words roms --index <sim addr>`
 * composes the six `mainpcb:user1` byte lanes into words, verifies each lane
 * against `roms/stunrun.manifest.json`, and adds the /MP=1 bank base $10000;
 * with `--verify-capture` it also checks the words against the words the pinned
 * MAME build latched at PC $017B/$01A7, which reported
 * `verified_pointer_chain_words: 9` for the chain below. If these values are
 * ever in doubt, re-read them with that tool; do not edit them by hand.
 *
 * Three groups live here, all from the same frame-551 capture:
 *   $19989-$1999C  the stride-2 record pointer chain and its command words,
 *                  read by $017F/$0182 to produce each record's start and the
 *                  loop-exit test;
 *   $1999D-$199C9  the per-record selector tables read at $01EC/$01FC/$01B1;
 *   $1208C/$123AC  the two $04B2 SIMBUF coefficients.
 *
 * No commercial ROM image is embedded: everything outside these addresses is
 * zero, and a zero here is a real ROM zero rather than a missing word.
 */
#ifndef STUNRUN_ADSP_SOM_SELECTOR_FIXTURE_H
#define STUNRUN_ADSP_SOM_SELECTOR_FIXTURE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STUNRUN_ADSP_SOM_SELECTOR_ROM_BASE_WORD 0u
#define STUNRUN_ADSP_SOM_SELECTOR_ROM_WORDS 0x199cau
#define STUNRUN_ADSP_SOM_SELECTOR_RECORDS 9u
#define STUNRUN_ADSP_SOM_SELECTOR_CHAIN_FIRST_WORD 0x19989u
#define STUNRUN_ADSP_SOM_SELECTOR_CHAIN_WORDS 0x14u

/* Sparse synthetic word image containing only the words listed above. */
const uint16_t *stunrun_adsp_som_selector_fixture_rom(void);
const uint16_t *stunrun_adsp_som_selector_fixture_simclk_starts(void);

#ifdef __cplusplus
}
#endif

#endif /* STUNRUN_ADSP_SOM_SELECTOR_FIXTURE_H */
