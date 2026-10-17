/* Deterministic native game-loop scaffold over the fake machine ports. */
#ifndef STUNRUN_NATIVE_GAME_LOOP_H
#define STUNRUN_NATIVE_GAME_LOOP_H

#include <stdint.h>

#include "fake_ports.h"

#define STUNRUN_INTERRUPT_TICKS_PER_FRAME 4u
typedef struct stunrun_game_loop {
    stunrun_fake_ports_t *ports;
    unsigned frame;
    uint32_t global_tick;
    uint16_t state_word;
    uint32_t state_dispatch_target_pc;
    int32_t steering_delta;
    int16_t trajectory_coordinate;
    int16_t motion_delta;
} stunrun_game_loop_t;

void stunrun_game_loop_init(stunrun_game_loop_t *loop,
                            stunrun_fake_ports_t *ports);
void stunrun_game_loop_step(stunrun_game_loop_t *loop);
void stunrun_game_loop_set_motion_delta(stunrun_game_loop_t *loop,
                                        int16_t motion_delta);
#endif
