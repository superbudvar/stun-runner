#include "state_dispatch.h"

#include <assert.h>
#include <stdio.h>

/* Expected branch targets independently transcribed from the ROM table. */
static const uint32_t expected_targets[STUNRUN_STATE_DISPATCH_MAX + 1u] = {
    0x024546, 0x02468a, 0x024bd6, 0x024cce, 0x024df2, 0x024dfc,
    0x025436, 0x024782, 0x0248b6, 0x024b5c, 0x024b66, 0x024b70,
    0x024b7a, 0x024bfc, 0x024c6e, 0x0245d0, 0x0248c0, 0x024b8e,
    0x024be0, 0x024c64, 0x024cb4, 0x024b84, 0x0248ca, 0x0249cc,
    0x024b52, 0x024e06, 0x024e10, 0x024e1a, 0x024e24, 0x024e3e,
    0x024eca, 0x024cd8, 0x024da0, 0x024c96, 0x024ca0, 0x024c82,
    0x024c8c, 0x024ed4, 0x024ed8, 0x024ee2, 0x025010, 0x02501a,
    0x0247ce, 0x0246d6, 0x0246e0, 0x025024, 0x0252f6, 0x02538c,
    0x0253d8, 0x0253e2, 0x0253dc, 0x0253f8, 0x025400, 0x025408,
    0x025410, 0x025418, 0x025420, 0x025428, 0x025430, 0x02472c,
    0x024736, 0x024864, 0x024916, 0x024920, 0x02496c, 0x024976,
    0x0249c2, 0x024a14, 0x025396, 0x0253a0
};

int main(void)
{
    unsigned state;
    for (state = 0; state <= STUNRUN_STATE_DISPATCH_MAX; ++state)
        assert(stunrun_maincpu_state_dispatch_target((uint16_t)state) ==
               expected_targets[state]);
    assert(stunrun_maincpu_state_dispatch_target(0x46u) ==
           STUNRUN_STATE_DISPATCH_DEFAULT_PC);
    assert(stunrun_maincpu_state_dispatch_target(0xffffu) ==
           STUNRUN_STATE_DISPATCH_DEFAULT_PC);
    puts("68010 ROM state-dispatch table: all targets match");
    return 0;
}
