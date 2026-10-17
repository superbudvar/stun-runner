/* Evidence-shaped native loop. Unresolved fields remain named by mechanism. */
#include "game_loop.h"

#include "trajectory_state.h"
#include "../reproduction/maincpu/state_dispatch.h"

static void service_interrupt_tick(stunrun_game_loop_t *loop)
{
    loop->global_tick++;
    (void)stunrun_mem_write32_be(loop->ports, 0xFF8014u,
                                 loop->global_tick);
}

static void sample_inputs(stunrun_game_loop_t *loop)
{
    int32_t stick_x = 128;
    (void)stunrun_port_read(loop->ports, STUNRUN_PORT_AD_STICK_X, &stick_x);
    loop->steering_delta = stick_x - 128;
}

static void update_trajectory_state(stunrun_game_loop_t *loop)
{
    uint16_t raw_motion_delta;
    if (stunrun_mem_read16_be(loop->ports, 0xFFDD16u,
                              &raw_motion_delta))
        loop->motion_delta = (int16_t)raw_motion_delta;
    loop->trajectory_coordinate = stunrun_trajectory_step(
        loop->trajectory_coordinate, loop->motion_delta);
    /* The field is a literal 16-bit big-endian trajectory boundary. Its
     * physical axis and producer are intentionally not named here. */
    (void)stunrun_mem_write16_be(loop->ports, 0xFFDCC6u,
                                 (uint16_t)loop->trajectory_coordinate);
}

void stunrun_game_loop_init(stunrun_game_loop_t *loop,
                            stunrun_fake_ports_t *ports)
{
    if (loop == NULL)
        return;
    loop->ports = ports;
    loop->frame = 0u;
    loop->global_tick = 0u;
    loop->state_word = 0u;
    loop->state_dispatch_target_pc = 0u;
    loop->steering_delta = 0;
    loop->trajectory_coordinate = stunrun_trajectory_initial();
    loop->motion_delta = 0;
    (void)stunrun_mem_write16_be(ports, 0xFFDD16u, 0u);
}

void stunrun_game_loop_step(stunrun_game_loop_t *loop)
{
    unsigned tick;
    uint16_t state_word;
    if (loop == NULL || loop->ports == NULL)
        return;
    for (tick = 0u; tick < STUNRUN_INTERRUPT_TICKS_PER_FRAME; tick++)
        service_interrupt_tick(loop);
    sample_inputs(loop);
    update_trajectory_state(loop);
    if (stunrun_mem_read16_be(loop->ports,
                              STUNRUN_STATE_DISPATCH_WORD_ADDR,
                              &state_word)) {
        loop->state_word = state_word;
        loop->state_dispatch_target_pc =
            stunrun_maincpu_state_dispatch_target(state_word);
    }
    loop->frame++;
}

void stunrun_game_loop_set_motion_delta(stunrun_game_loop_t *loop,
                                        int16_t motion_delta)
{
    if (loop != NULL) {
        loop->motion_delta = motion_delta;
        (void)stunrun_mem_write16_be(loop->ports, 0xFFDD16u,
                                     (uint16_t)motion_delta);
    }
}
