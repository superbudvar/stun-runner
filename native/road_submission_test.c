#include "fake_ports.h"
#include "road_submission.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    stunrun_fake_ports_t ports;
    stunrun_road_submission_t submission;
    uint16_t table[STUNRUN_ROAD_SUBMISSION_SOURCE_WORDS];
    size_t i;

    stunrun_ports_init(&ports);
    stunrun_road_submission_init(&submission, &ports);
    assert(stunrun_road_submission_submit(&submission) == 0u);

    for (i = 0u; i < STUNRUN_ROAD_SUBMISSION_SOURCE_WORDS; i++)
        table[i] = (uint16_t)(0x1000u + i);
    assert(stunrun_road_submission_load_table(&submission, table));
    assert(submission.road_fifo_count == 0u);
    assert(submission.road_ready);

    /* 192 packed longs ride the host port as 384 16-bit writes, preserving
     * every source word in order. The refuted even-lane-only result is 382. */
    assert(stunrun_road_submission_submit(&submission) ==
           2u * STUNRUN_ROAD_SUBMISSION_FIFO_LONGS);
    assert(ports.gsp_fifo_count ==
           2u * STUNRUN_ROAD_SUBMISSION_FIFO_LONGS);
    for (i = 0u; i < STUNRUN_ROAD_SUBMISSION_SOURCE_WORDS; i++)
        assert(ports.gsp_fifo[i] == table[i]);
    puts("native road table submission and FIFO: all checks passed");
    return 0;
}
