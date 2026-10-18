/* Sparse evidence-only MP=1 SIMBUF words for the nine captured selector runs. */
#ifndef STUNRUN_ADSP_SOM_SELECTOR_FIXTURE_H
#define STUNRUN_ADSP_SOM_SELECTOR_FIXTURE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STUNRUN_ADSP_SOM_SELECTOR_ROM_BASE_WORD 0u
#define STUNRUN_ADSP_SOM_SELECTOR_ROM_WORDS 0x199cau
#define STUNRUN_ADSP_SOM_SELECTOR_RECORDS 9u

/* Sparse synthetic word image containing only observed selectors and the two
 * observed $04B2 SIMBUF coefficients; no commercial ROM image is embedded. */
const uint16_t *stunrun_adsp_som_selector_fixture_rom(void);
const uint16_t *stunrun_adsp_som_selector_fixture_simclk_starts(void);

#ifdef __cplusplus
}
#endif

#endif /* STUNRUN_ADSP_SOM_SELECTOR_FIXTURE_H */
