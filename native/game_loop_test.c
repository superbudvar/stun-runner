#include "fake_ports.h"
#include "game_loop.h"
#include "../reproduction/maincpu/state_dispatch.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    stunrun_fake_ports_t ports;
    stunrun_game_loop_t loop;
    uint32_t tick = 0;
    uint16_t motion_delta = 0;
    uint16_t state_word = 1;
    int32_t steering = 0;
    int32_t button = 0;

    stunrun_ports_init(&ports);
    stunrun_game_loop_init(&loop, &ports);
    assert(stunrun_port_set_named(&ports, ":mainpcb:8BADC.0",
                                  "AD Stick X", 220));
    assert(stunrun_mem_write16_be(&ports, 0xFF9550u, state_word));
    stunrun_game_loop_step(&loop);
    assert(loop.frame == 1u);
    assert(loop.global_tick == 4u);
    assert(loop.steering_delta == 92);
    assert(loop.trajectory_coordinate == (int16_t)-0x360);
    assert(stunrun_mem_read32_be(&ports, 0xFF8014u, &tick) && tick == 4u);
    assert(loop.state_word == 1u &&
           loop.state_dispatch_target_pc == 0x02468Au);
    stunrun_game_loop_set_motion_delta(&loop, -1);
    assert(stunrun_mem_read16_be(&ports, 0xFFDD16u, &motion_delta) &&
           (int16_t)motion_delta == -1);
    assert(stunrun_mem_write16_be(&ports, 0xFF9550u, 0x46u));
    stunrun_game_loop_step(&loop);
    assert(loop.trajectory_coordinate == (int16_t)0xFC9F);
    assert(loop.state_word == 0x46u &&
           loop.state_dispatch_target_pc == STUNRUN_STATE_DISPATCH_DEFAULT_PC);
    assert(stunrun_port_read(&ports, STUNRUN_PORT_AD_STICK_X, &steering) &&
           steering == 220);
    assert(stunrun_port_set_named(&ports, ":mainpcb:a80000",
                                  "P1 Button 1", 1));
    assert(stunrun_port_get_named(&ports, ":mainpcb:a80000",
                                  "P1 Button 1", &button) && button == 1);
    puts("native frame model and fake ports: all checks passed");
    return 0;
}
