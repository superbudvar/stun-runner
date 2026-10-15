/* Observable self-check for the GSP payload codec.
 *
 * The vectors are REAL, lifted from the committed oracle capture
 * reference/experiments/stunrun/display-list-service-f2-60-180/mame.log,
 * M1_GSP_DL_CALL id=1 stream=00060002 (64 words = 8 whole records). Nothing
 * here is invented geometry: if the codec cannot carry these back out
 * byte-for-byte, the codec is wrong.
 *
 * Two negative-control lessons are applied deliberately (findings 45.1 and
 * 48.3): this file was verified to FAIL under four deliberate breaks, and the
 * reject cases are asserted rather than assumed.
 */
#include <stdio.h>
#include <string.h>

#include "payload_codec.h"

static int failures;

#define check(cond) do { \
    if (!(cond)) { printf("FAIL %s\n", #cond); failures++; } \
} while (0)

/* All 64 words of stream 00060002, verbatim from the committed MAME log. */
static const uint16_t k_real64[64] = {
    0x0014u, 0x1615u, 0x171Fu, 0x1013u, 0x0000u, 0x0000u, 0x0000u, 0x0000u,
    0x00FFu, 0x11FFu, 0x11FFu, 0x11FFu, 0x11C4u, 0x11FFu, 0x22D9u, 0x2243u,
    0x1211u, 0x23D0u, 0x2246u, 0x1312u, 0x1111u, 0x4312u, 0xCB11u, 0x4812u,
    0x1111u, 0x4312u, 0x1111u, 0x43CCu, 0x0045u, 0x1211u, 0x1143u, 0x31D0u,
    0x2243u, 0x3211u, 0x41FFu, 0x22D9u, 0x227Eu, 0x3234u, 0x1131u, 0x3234u,
    0x1131u, 0x3234u, 0x1131u, 0x3234u, 0x1131u, 0x3234u, 0x1131u, 0x3234u,
    0x1131u, 0x3234u, 0x1131u, 0x3234u, 0x1131u, 0x3234u, 0x1131u, 0x3234u,
    0x1131u, 0x3234u, 0x1131u, 0x3234u, 0x1131u, 0x3234u, 0x1131u, 0x3234u
};

int main(void)
{
    stunrun_gsp_payload_record_t rec[8];
    stunrun_gsp_payload_control_t ctl;
    stunrun_gsp_fill_xy_setup_t fill;
    uint16_t back[64];
    size_t n = 0u, w = 0u;

    /* --- decode of real data ------------------------------------------------ */
    check(stunrun_gsp_payload_decode(k_real64, 64u, rec, 8u, &n) == 8u);
    check(n == 8u);
    check(stunrun_gsp_payload_decode(k_real64, 64u, rec, 7u, &n) == 0u);
    check(n == 0u);                       /* capacity must not be exceeded */

    check(stunrun_gsp_payload_control_word(&rec[0]) == 0x0014u);
    check(stunrun_gsp_payload_field_a8_initial(&rec[0]) == 0x1615u);
    check(stunrun_gsp_payload_field_a6(&rec[0]) == 0x171Fu);
    check(stunrun_gsp_payload_control_word(&rec[1]) == 0x00FFu);
    check(stunrun_gsp_payload_field_a8_initial(&rec[1]) == 0x11FFu);
    check(stunrun_gsp_payload_field_a6(&rec[1]) == 0x11FFu);

    /* The header record's tail words are zero and must survive untouched. */
    check(stunrun_gsp_payload_unmapped(&rec[0], 3u) == 0x1013u);
    check(stunrun_gsp_payload_unmapped(&rec[0], 7u) == 0x0000u);

    /* --- round trip --------------------------------------------------------- */
    check(stunrun_gsp_payload_encode(rec, 8u, back, 64u, &w) == 64u);
    check(w == 64u);
    check(memcmp(back, k_real64, sizeof(k_real64)) == 0);

    /* --- control-word selects, on each bit independently -------------------- */
    /* 0x0014 from the real capture: no flag bits set, low byte 0x14. */
    ctl = stunrun_gsp_payload_control(&rec[0]);
    check(ctl.word == 0x0014u);
    check(ctl.skip == 0);
    check(ctl.mode == 0);
    check(ctl.expand == 0x0000u);
    check(ctl.low8 == 0x0014u);

    /* Each bit is asserted on its own, so a bit mask that silently covered a
     * neighbour cannot pass. */
    rec[0].raw[0] = 0x8000u;
    check(stunrun_gsp_payload_control(&rec[0]).skip == 1);
    check(stunrun_gsp_payload_control(&rec[0]).mode == 0);
    rec[0].raw[0] = 0x1000u;
    check(stunrun_gsp_payload_control(&rec[0]).skip == 0);
    check(stunrun_gsp_payload_control(&rec[0]).mode == 1);
    rec[0].raw[0] = 0x0C00u;
    check(stunrun_gsp_payload_control(&rec[0]).expand == 0x0C00u);
    rec[0].raw[0] = 0x0800u;
    check(stunrun_gsp_payload_control(&rec[0]).expand == 0x0800u);
    rec[0].raw[0] = 0x0004u;
    check(stunrun_gsp_payload_control(&rec[0]).low8 == 0x0004u);
    rec[0].raw[0] = 0x0005u;
    check(stunrun_gsp_payload_control(&rec[0]).low8 == 0x0005u);

    /* Bit 15 and bit 12 together, which no single-bit case produces. The
     * outer branch priority is also asserted in color_action below. */
    rec[0].raw[0] = 0x9000u;
    ctl = stunrun_gsp_payload_control(&rec[0]);
    check(ctl.skip == 1 && ctl.mode == 1);

    /* --- B9 setup branch, directly from $FFF45160-$FFF452A0 ------------------ */
    rec[0].raw[0] = 0x0014u; /* C00=0, low8!=4: retain B9=A12 */
    check(stunrun_gsp_payload_control(&rec[0]).color_action ==
          STUNRUN_GSP_PAYLOAD_COLOR_ACTION_INHERIT);
    rec[0].raw[0] = 0x0004u;
    check(stunrun_gsp_payload_control(&rec[0]).color_action ==
          STUNRUN_GSP_PAYLOAD_COLOR_ACTION_SET_B);
    rec[0].raw[0] = 0x0400u;
    check(stunrun_gsp_payload_control(&rec[0]).color_action ==
          STUNRUN_GSP_PAYLOAD_COLOR_ACTION_SET_B);
    rec[0].raw[0] = 0x0800u;
    check(stunrun_gsp_payload_control(&rec[0]).color_action ==
          STUNRUN_GSP_PAYLOAD_COLOR_ACTION_SET_A);
    rec[0].raw[0] = 0x0C00u;
    check(stunrun_gsp_payload_control(&rec[0]).color_action ==
          STUNRUN_GSP_PAYLOAD_COLOR_ACTION_SELECTOR_ALT);
    rec[0].raw[0] = 0x1000u;
    check(stunrun_gsp_payload_control(&rec[0]).color_action ==
          STUNRUN_GSP_PAYLOAD_COLOR_ACTION_MODE_ALT);
    rec[0].raw[0] = 0x8000u;
    check(stunrun_gsp_payload_control(&rec[0]).color_action ==
          STUNRUN_GSP_PAYLOAD_COLOR_ACTION_SKIP);

    /* Restore the actual first captured record and run the primary worker's
     * register mapping. A11 is supplied by the caller and A13 bit 3 is clear.
     * These are register-level XY/DYDX values, not claimed visible pixels. */
    rec[0].raw[0] = 0x0014u;
    check(stunrun_gsp_payload_primary_fill_xy(&rec[0], 0x00020000u, 0u,
                                               &fill) == 1);
    check(fill.destination_x == 0x1615u);
    check(fill.destination_y == 0x1013u);
    check(fill.dx == 0x171Fu);
    check(fill.dy == 0x0002u);

    /* Caller A13 bit 3, bit-15 skip, bit-12 mode, and selector-C00 divert
     * before the primary FILL. Every diversion must leave output untouched. */
    memset(&fill, 0xA5, sizeof(fill));
    check(stunrun_gsp_payload_primary_fill_xy(&rec[0], 0x00020000u, 0x8u,
                                               &fill) == 0);
    check(fill.destination_x == 0xA5A5u);
    rec[0].raw[0] = 0x8000u;
    check(stunrun_gsp_payload_primary_fill_xy(&rec[0], 0u, 0u, &fill) == 0);
    rec[0].raw[0] = 0x1000u;
    check(stunrun_gsp_payload_primary_fill_xy(&rec[0], 0u, 0u, &fill) == 0);
    rec[0].raw[0] = 0x0C00u;
    check(stunrun_gsp_payload_primary_fill_xy(&rec[0], 0u, 0u, &fill) == 0);
    check(stunrun_gsp_payload_primary_fill_xy(NULL, 0u, 0u, &fill) == 0);
    check(stunrun_gsp_payload_primary_fill_xy(&rec[0], 0u, 0u, NULL) == 0);
    check(STUNRUN_GSP_PAYLOAD_COLOR1_A == 0x01010101u);
    check(STUNRUN_GSP_PAYLOAD_COLOR1_B == 0x15151515u);

    /* --- unmapped accessor bounds ------------------------------------------ */
    check(stunrun_gsp_payload_unmapped(&rec[0], 2u) == 0u);   /* mapped word */
    check(stunrun_gsp_payload_unmapped(&rec[0], 8u) == 0u);   /* past the end */
    check(stunrun_gsp_payload_unmapped(NULL, 3u) == 0u);
    check(stunrun_gsp_payload_control_word(NULL) == 0u);
    check(stunrun_gsp_payload_field_a8_initial(NULL) == 0u);

    /* --- malformed input is rejected loudly --------------------------------- */
    check(stunrun_gsp_payload_decode(k_real64, 63u, rec, 8u, &n) == 0u);
    check(n == 0u);                        /* not a whole number of records */
    check(stunrun_gsp_payload_decode(k_real64, 7u, rec, 8u, &n) == 0u);
    check(stunrun_gsp_payload_decode(NULL, 64u, rec, 8u, &n) == 0u);
    check(stunrun_gsp_payload_decode(k_real64, 64u, NULL, 8u, &n) == 0u);
    check(stunrun_gsp_payload_decode(k_real64, 64u, rec, 8u, NULL) == 0u);
    check(stunrun_gsp_payload_encode(rec, 8u, back, 63u, &w) == 0u);
    check(w == 0u);
    check(stunrun_gsp_payload_encode(rec, 0u, back, 64u, &w) == 0u);
    check(stunrun_gsp_payload_encode(NULL, 8u, back, 64u, &w) == 0u);

    /* The captured 64 words must decode to exactly 8 records, not 64 and not 1. */
    check(stunrun_gsp_payload_decode(k_real64, 64u, rec, 8u, &n) == 8u);

    if (failures == 0)
        printf("PASS\n");
    return failures == 0 ? 0 : 1;
}
