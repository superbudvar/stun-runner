/* Bounded C transcription of the captured ADSP SOM source/ring routine.
 *
 * The implemented path is ADSP PC $04B2-$04DF at power-on frame 551. It reads
 * three (signed x, signed y, SIMBUF address) tuples from DM, fetches the
 * coefficient through the MP=1 object-ROM window, performs both observed
 * passes, and returns the six-word DM($0040..$0045) ring after pass 2. This is
 * not a general ADSP core or a geometry-semantic claim.
 */
#ifndef STUNRUN_ADSP_SOM_PRODUCER_H
#define STUNRUN_ADSP_SOM_PRODUCER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STUNRUN_ADSP_SOM_SOURCE_TUPLES 3u
#define STUNRUN_ADSP_SOM_SOURCE_WORDS 10u
#define STUNRUN_ADSP_SOM_RING_WORDS 6u
#define STUNRUN_ADSP_SOM_MP1_BASE_WORDS 0x10000u
#define STUNRUN_ADSP_SOM_PASS2_GUARD 0x0200u
#define STUNRUN_ADSP_SOM_PASS2_Y_MULTIPLIER 0x0140u

/* Execute the measured $04B2-$04DF path.
 *
 * source_dm is the DM window containing DM(I0,M1) reads beginning at start_i0;
 * only the captured I0=DM($0928)=$0040 path is accepted. It consumes ten words (three
 * tuples plus the final unused x prefetch). object_rom_words is the MAME-style
 * word-addressed SIMBUF image; this routine sets MP=1, so each SIMCLK address
 * reads object_rom_words[0x10000 + simclk]. The captured path exercises the
 * $04C3 greater-than branch only, with DM($1FE4)=$0200,
 * DM($1FE5)=$0140, and SE=-10.
 *
 * pass1_out receives the six words written at $04C8/$04CA. ring_out receives
 * the six words after the $04D6-$04DC pass. With a zero pass2_guard, the
 * disassembly's $04CD branch skips pass 2 and ring_out equals pass1_out; that
 * branch is implemented from static disassembly but was not exercised by the
 * captured transaction. Other pass-2 constants and the $04C3 alternate scale
 * branch are rejected rather than guessed.
 *
 * Returns 1 on success and 0 on NULL, truncated DM/object-ROM windows,
 * unsupported tuple count, alternate $04C3 path, or unsupported pass-2
 * constants. Output arrays are left unchanged on failure.
 */
int stunrun_adsp_som_produce_ring(
    const uint16_t *source_dm, size_t source_dm_word_count,
    uint16_t source_dm_base, uint16_t start_i0,
    const uint16_t *object_rom_words, size_t object_rom_word_count,
    uint16_t pass2_guard, uint16_t pass2_y_multiplier,
    uint16_t pass1_out[STUNRUN_ADSP_SOM_RING_WORDS],
    uint16_t ring_out[STUNRUN_ADSP_SOM_RING_WORDS]);

#ifdef __cplusplus
}
#endif

#endif /* STUNRUN_ADSP_SOM_PRODUCER_H */
