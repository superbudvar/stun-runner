/* Tests for the bounded primary FILL XY prefix decoder.
 *
 * The first four words are taken verbatim from the M1_GSP_DL_CALL capture
 * stream=00060002. No boundary is assumed for the F1 tail after these fields. */
#include <stdio.h>
#include <string.h>

#include "payload_codec.h"

static int failures;

#define check(cond) do { \
    if (!(cond)) { printf("FAIL %s\n", #cond); failures++; } \
} while (0)

static const uint16_t k_capture_prefix[4] = {
    0x0014u, 0x1615u, 0x171Fu, 0x1013u
};

int main(void)
{
    stunrun_gsp_payload_prefix_t p;
    stunrun_gsp_payload_prefix_t before;
    stunrun_gsp_payload_control_t c;
    stunrun_gsp_fill_xy_setup_t fill;
    uint16_t encoded[4] = {0u, 0u, 0u, 0u};

    /* Decode the actual first F0 prefix from the committed 64-word capture. */
    check(stunrun_gsp_payload_decode_prefix(k_capture_prefix, 4u, 0u, &p));
    check(p.control == 0x0014u);
    check(p.x == 0x1615u);
    check(p.dx == 0x171Fu);
    check(p.y == 0x1013u);
    check(stunrun_gsp_payload_encode_prefix(&p, encoded, 4u) == 4u);
    check(memcmp(encoded, k_capture_prefix, sizeof(k_capture_prefix)) == 0);

    /* Offset support is for a larger raw stream; it does not establish record
     * boundaries. A second vector is placed at word 8 only as an explicit test
     * input, not as a claim about the captured stream's entry stride. */
    {
        uint16_t larger[12] = {0u};
        larger[8] = 0x00FFu; larger[9] = 0x11FFu;
        larger[10] = 0x11FFu; larger[11] = 0x11FFu;
        check(stunrun_gsp_payload_decode_prefix(larger, 12u, 8u, &p));
        check(p.control == 0x00FFu && p.x == 0x11FFu &&
              p.dx == 0x11FFu && p.y == 0x11FFu);
    }

    /* Failures leave the output untouched. */
    before = p;
    check(!stunrun_gsp_payload_decode_prefix(k_capture_prefix, 3u, 0u, &p));
    check(memcmp(&p, &before, sizeof(p)) == 0);
    check(!stunrun_gsp_payload_decode_prefix(k_capture_prefix, 4u, 2u, &p));
    check(!stunrun_gsp_payload_decode_prefix(NULL, 4u, 0u, &p));
    check(!stunrun_gsp_payload_decode_prefix(k_capture_prefix, 4u, 0u, NULL));
    check(stunrun_gsp_payload_encode_prefix(&p, encoded, 3u) == 0u);
    check(stunrun_gsp_payload_encode_prefix(NULL, encoded, 4u) == 0u);

    /* Selector paths from the disassembly. */
    p.control = 0x0014u;
    c = stunrun_gsp_payload_control(&p);
    check(c.color_action == STUNRUN_GSP_PAYLOAD_COLOR_ACTION_INHERIT);
    p.control = 0x0004u;
    check(stunrun_gsp_payload_control(&p).color_action ==
          STUNRUN_GSP_PAYLOAD_COLOR_ACTION_SET_B);
    p.control = 0x0400u;
    check(stunrun_gsp_payload_control(&p).color_action ==
          STUNRUN_GSP_PAYLOAD_COLOR_ACTION_SET_B);
    p.control = 0x0800u;
    check(stunrun_gsp_payload_control(&p).color_action ==
          STUNRUN_GSP_PAYLOAD_COLOR_ACTION_SET_A);
    p.control = 0x0C00u;
    check(stunrun_gsp_payload_control(&p).color_action ==
          STUNRUN_GSP_PAYLOAD_COLOR_ACTION_SELECTOR_ALT);
    p.control = 0x1000u;
    check(stunrun_gsp_payload_control(&p).color_action ==
          STUNRUN_GSP_PAYLOAD_COLOR_ACTION_MODE_ALT);
    p.control = 0x8000u;
    check(stunrun_gsp_payload_control(&p).color_action ==
          STUNRUN_GSP_PAYLOAD_COLOR_ACTION_SKIP);

    /* Actual captured prefix -> register-level FILL XY operands. A11 is
     * deliberately synthetic; its value was not in the capture. */
    check(stunrun_gsp_payload_decode_prefix(k_capture_prefix, 4u, 0u, &p));
    check(stunrun_gsp_payload_primary_fill_xy(&p, 0x00020000u, 0u, &fill));
    check(fill.destination_x == 0x1615u);
    check(fill.destination_y == 0x1013u);
    check(fill.dx == 0x171Fu);
    check(fill.dy == 0x0002u);

    memset(&fill, 0xA5, sizeof(fill));
    check(!stunrun_gsp_payload_primary_fill_xy(&p, 0x00020000u, 0x8u, &fill));
    check(fill.destination_x == 0xA5A5u);
    p.control = 0x8000u;
    check(!stunrun_gsp_payload_primary_fill_xy(&p, 0u, 0u, &fill));
    p.control = 0x1000u;
    check(!stunrun_gsp_payload_primary_fill_xy(&p, 0u, 0u, &fill));
    p.control = 0x0C00u;
    check(!stunrun_gsp_payload_primary_fill_xy(&p, 0u, 0u, &fill));
    check(!stunrun_gsp_payload_primary_fill_xy(NULL, 0u, 0u, &fill));
    check(!stunrun_gsp_payload_primary_fill_xy(&p, 0u, 0u, NULL));

    check(STUNRUN_GSP_PAYLOAD_COLOR1_A == 0x01010101u);
    check(STUNRUN_GSP_PAYLOAD_COLOR1_B == 0x15151515u);

    if (failures == 0)
        puts("PASS");
    return failures == 0 ? 0 : 1;
}
