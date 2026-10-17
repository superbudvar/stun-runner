/* Native fixture path for loading a road table and submitting it to the fake
 * GSP FIFO. This is separate from the per-frame native model. */
#ifndef STUNRUN_NATIVE_ROAD_SUBMISSION_H
#define STUNRUN_NATIVE_ROAD_SUBMISSION_H

#include <stddef.h>
#include <stdint.h>

#include "fake_ports.h"

#define STUNRUN_ROAD_SUBMISSION_SOURCE_WORDS 384u
#define STUNRUN_ROAD_SUBMISSION_FIFO_LONGS 192u

typedef struct stunrun_road_submission {
    stunrun_fake_ports_t *ports;
    uint16_t road_base[STUNRUN_ROAD_SUBMISSION_SOURCE_WORDS];
    uint16_t road_twin[STUNRUN_ROAD_SUBMISSION_SOURCE_WORDS];
    uint32_t road_fifo[STUNRUN_ROAD_SUBMISSION_FIFO_LONGS];
    size_t road_fifo_count;
    int road_ready;
} stunrun_road_submission_t;

void stunrun_road_submission_init(stunrun_road_submission_t *submission,
                                  stunrun_fake_ports_t *ports);
int stunrun_road_submission_load_table(stunrun_road_submission_t *submission,
                                       const uint16_t *table);
size_t stunrun_road_submission_submit(stunrun_road_submission_t *submission);

#endif
