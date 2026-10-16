/* ROM-free self-check for the observed road-buffer FIFO drain.
 *
 * The captured block below is real measured data, not a hand-written example.
 * Source of record:
 *
 *   tools/mame-ram-write-trace roms OUT \
 *     --device :mainpcb:maincpu --space program \
 *     --base 0xC0000C --end 0xC0000E \
 *     --frames 1300 --start-frame 1280 --end-frame 1300 \
 *     --pc 0x02248E --input late_drive --nothrottle \
 *     --mame /mnt/build/mame-oracle/mame
 *
 *   -> 32524 writes, all from PC $02248E, strictly $C0000C-then-$C0000E,
 *      0 exceptions in 16262 pairs. Frame 1290 contributed exactly 192 pairs.
 *   tools/mame-memory-snapshot ... --base 0xFF9584 --count 0x600 \
 *     --frames 1291 --targets 1290 --input late_drive
 *      -> the base road buffer at that frame.
 *
 * All 192 packed longs of the frame-1290 burst equal the base buffer packed
 * two words per long, high halfword first: 192/192 exact match, and exactly
 * one contiguous occurrence of the full table in the 16262-pair window.
 * Full-block sha256 of the 192 packed longs:
 *   a1064bce98f3d80acfa2a83ec9df46654e7108e580b9cae5e9967fa5a256789c
 *
 * The buffer itself is commercial ROM (findings 74.1) and is deliberately not
 * committed; only these 16 derived words are.
 */
#include <stdio.h>
#include <string.h>

#include "road_fifo.h"

static int failures;

#define check(cond) do { \
    if (!(cond)) { printf("FAIL %s\n", #cond); failures++; } \
} while (0)

/* Base road buffer at frame 1290, `--input late_drive`, first 16 words. */
static const uint16_t k_captured_base[STUNRUN_ROAD_FIFO_SOURCE_WORDS] = {
    0x0000, 0x00E0, 0x2500, 0xBD08, 0x0963, 0xE065, 0x3A00, 0xE04D,
    0x8AE0, 0x009A, 0x0002, 0x7000, 0xDF83, 0x004B, 0x0000, 0x0038
};

/* The 8 longs the first 16 source words produce, as observed on the bus. */
static const uint32_t k_captured_longs[8] = {
    0x000000E0u, 0x2500BD08u, 0x0963E065u, 0x3A00E04Du,
    0x8AE0009Au, 0x00027000u, 0xDF83004Bu, 0x00000038u
};

/* The 16 host writes, in bus order: high halfword of long 0, low halfword of
 * long 0, high of long 1, ... Because the packing is lossless and in order,
 * this is exactly the source word sequence -- which is the sharpest available
 * statement that the drain selects nothing and reorders nothing. */
static const uint16_t k_captured_host[16] = {
    0x0000, 0x00E0, 0x2500, 0xBD08, 0x0963, 0xE065, 0x3A00, 0xE04D,
    0x8AE0, 0x009A, 0x0002, 0x7000, 0xDF83, 0x004B, 0x0000, 0x0038
};

int main(void)
{
    uint16_t base[STUNRUN_ROAD_FIFO_SOURCE_WORDS];
    uint32_t fifo[STUNRUN_ROAD_FIFO_WRITES];
    uint16_t host[STUNRUN_ROAD_FIFO_WRITES * 2u];
    size_t i;

    check(STUNRUN_ROAD_FIFO_SOURCE_WORDS == 384u);
    check(STUNRUN_ROAD_FIFO_WRITES == 192u);
    check(STUNRUN_ROAD_FIFO_PC == 0x02248Eu);
    check(STUNRUN_ROAD_FIFO_DEST == 0xC0000Cu);
    check(STUNRUN_ROAD_FIFO_DEST_HIGH == 0xC0000Cu);
    check(STUNRUN_ROAD_FIFO_DEST_LOW == 0xC0000Eu);

    /* --- the captured block, asserted against measured data --------------- */
    for (i = 0; i < STUNRUN_ROAD_FIFO_SOURCE_WORDS; i++)
        base[i] = k_captured_base[i % 16u];
    memset(fifo, 0xA5, sizeof(fifo));
    check(stunrun_road_fifo_drain(base, STUNRUN_ROAD_FIFO_SOURCE_WORDS, fifo,
                                  STUNRUN_ROAD_FIFO_WRITES) ==
          STUNRUN_ROAD_FIFO_WRITES);
    for (i = 0; i < 8u; i++)
        check(fifo[i] == k_captured_longs[i]);
    /* The whole buffer is that 16-word block repeated 24 times, so the packed
     * stream is the 8 captured longs repeated 24 times. */
    for (i = 0; i < STUNRUN_ROAD_FIFO_WRITES; i++)
        check(fifo[i] == k_captured_longs[i % 8u]);

    /* --- the split into host writes --------------------------------------- */
    memset(host, 0x5A, sizeof(host));
    check(stunrun_road_fifo_host_writes(fifo, 8u, host, 16u) == 16u);
    for (i = 0; i < 16u; i++)
        check(host[i] == k_captured_host[i]);
    /* High halfword first: host[2i] is long i's high word. */
    for (i = 0; i < 8u; i++) {
        check(host[i * 2u] == (uint16_t)(fifo[i] >> 16));
        check(host[i * 2u + 1u] == (uint16_t)(fifo[i] & 0xFFFFu));
    }
    /* Over the whole burst the host writes reproduce the source words in
     * order, unmodified. */
    {
        uint16_t all[STUNRUN_ROAD_FIFO_SOURCE_WORDS];
        size_t n = stunrun_road_fifo_host_writes(
            fifo, STUNRUN_ROAD_FIFO_WRITES, all,
            STUNRUN_ROAD_FIFO_SOURCE_WORDS);
        check(n == STUNRUN_ROAD_FIFO_SOURCE_WORDS);
        for (i = 0; i < STUNRUN_ROAD_FIFO_SOURCE_WORDS; i++)
            check(all[i] == base[i]);
    }
    check(stunrun_road_fifo_host_writes(fifo, 8u, host, 15u) == 0u);
    check(stunrun_road_fifo_host_writes(NULL, 8u, host, 16u) == 0u);
    check(stunrun_road_fifo_host_writes(fifo, 8u, NULL, 16u) == 0u);

    /* --- packing is consecutive, NOT an even-lane selection -------------- */
    /* The refuted even-lane reading took base[2i] alone. It happens to equal
     * the high halfword of the true long, so an assertion of the form
     * "output == base[2i]" passes under both readings. Assert the low halfword
     * too, which only the consecutive packing produces. */
    for (i = 0; i < STUNRUN_ROAD_FIFO_WRITES; i++) {
        check(fifo[i] == (((uint32_t)base[i * 2u] << 16) |
                          (uint32_t)base[i * 2u + 1u]));
        check((uint16_t)fifo[i] == base[i * 2u + 1u]);
    }

    /* --- guards ----------------------------------------------------------- */
    check(stunrun_road_fifo_drain(base, STUNRUN_ROAD_FIFO_SOURCE_WORDS - 1u,
                                  fifo, STUNRUN_ROAD_FIFO_WRITES) == 0u);
    check(stunrun_road_fifo_drain(base, STUNRUN_ROAD_FIFO_SOURCE_WORDS, fifo,
                                  1u) == 0u);
    check(stunrun_road_fifo_drain(NULL, STUNRUN_ROAD_FIFO_SOURCE_WORDS, fifo,
                                  STUNRUN_ROAD_FIFO_WRITES) == 0u);

    /* --- bounded sub-range, same consecutive packing ---------------------- */
    {
        uint32_t split[STUNRUN_ROAD_FIFO_WRITES];
        size_t first = stunrun_road_fifo_drain_range(
            base, STUNRUN_ROAD_FIFO_SOURCE_WORDS, 0u, 128u, split,
            STUNRUN_ROAD_FIFO_WRITES);
        size_t second = stunrun_road_fifo_drain_range(
            base, STUNRUN_ROAD_FIFO_SOURCE_WORDS, 128u, 256u,
            split + first, STUNRUN_ROAD_FIFO_WRITES - first);
        check(first == 64u);
        check(second == 128u);
        check(first + second == STUNRUN_ROAD_FIFO_WRITES);
        /* Boundary word: long 63 packs source words 126 and 127, so the
         * packed stream crosses the 128-word sub-range split unbroken. */
        check(split[63u] == (((uint32_t)base[126u] << 16) | base[127u]));
        check(split[64u] == (((uint32_t)base[128u] << 16) | base[129u]));
        check(split[191u] == (((uint32_t)base[382u] << 16) | base[383u]));
        check(stunrun_road_fifo_drain_range(
                  base, STUNRUN_ROAD_FIFO_SOURCE_WORDS, 1u, 2u, split,
                  STUNRUN_ROAD_FIFO_WRITES) == 0u);
        check(stunrun_road_fifo_drain_range(
                  base, STUNRUN_ROAD_FIFO_SOURCE_WORDS, 0u, 3u, split,
                  STUNRUN_ROAD_FIFO_WRITES) == 0u);
    }

    if (failures != 0) {
        printf("road_fifo_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("road_fifo_test: ok\n");
    return 0;
}