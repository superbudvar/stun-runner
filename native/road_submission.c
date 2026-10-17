/* Native road-table fixture loading and GSP FIFO transport. */
#include "road_submission.h"

#include "../reproduction/maincpu/geom_upload.h"
#include "../reproduction/maincpu/road_fifo.h"

void stunrun_road_submission_init(stunrun_road_submission_t *submission,
                                  stunrun_fake_ports_t *ports)
{
    if (submission == NULL)
        return;
    submission->ports = ports;
    submission->road_fifo_count = 0u;
    submission->road_ready = 0;
}

int stunrun_road_submission_load_table(stunrun_road_submission_t *submission,
                                       const uint16_t *table)
{
    if (submission == NULL || table == NULL ||
        stunrun_geom_upload(STUNRUN_GEOM_COPY_COUNT, table,
                            submission->road_base, submission->road_twin) !=
            STUNRUN_GEOM_COPY_COUNT)
        return 0;
    submission->road_fifo_count = 0u;
    submission->road_ready = 1;
    return 1;
}

size_t stunrun_road_submission_submit(stunrun_road_submission_t *submission)
{
    size_t i;
    if (submission == NULL || submission->ports == NULL ||
        !submission->road_ready)
        return 0u;
    submission->road_fifo_count = stunrun_road_fifo_drain(
        submission->road_base, STUNRUN_ROAD_SUBMISSION_SOURCE_WORDS,
        submission->road_fifo, STUNRUN_ROAD_SUBMISSION_FIFO_LONGS);
    /* Each datum is a 32-bit long carried by two 16-bit host writes, the high
     * halfword to $C0000C first and the low halfword to $C0000E second
     * (findings 75). */
    for (i = 0u; i < submission->road_fifo_count; i++) {
        uint16_t half[2];
        size_t n;

        if (stunrun_road_fifo_host_writes(&submission->road_fifo[i], 1u,
                                         half, 2u) != 2u)
            return 0u;
        for (n = 0u; n < 2u; n++)
            if (!stunrun_gsp_fifo_push(submission->ports, half[n]))
                return i;
    }
    return submission->road_fifo_count * 2u;
}
