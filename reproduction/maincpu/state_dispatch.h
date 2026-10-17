/* MC68010 main-loop state dispatch at ROM $02452C. */
#ifndef STUNRUN_MAINCPU_STATE_DISPATCH_H
#define STUNRUN_MAINCPU_STATE_DISPATCH_H

#include <stdint.h>

#define STUNRUN_STATE_DISPATCH_MAX 0x45u
#define STUNRUN_STATE_DISPATCH_DEFAULT_PC 0x025436u
#define STUNRUN_STATE_DISPATCH_WORD_ADDR 0xFF9550u

/* Return the ROM branch target for the unsigned state word.  This ports the
 * bounds check and relative jump-table lookup only; the target state handlers
 * remain separate routines and are not executed by this function. */
uint32_t stunrun_maincpu_state_dispatch_target(uint16_t state);

#endif
