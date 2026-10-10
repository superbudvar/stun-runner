#ifndef STUNRUN_GSP_VRAM_CLEAR_H
#define STUNRUN_GSP_VRAM_CLEAR_H

#include <stddef.h>
#include <stdint.h>

/* Literal MC68010 routine at 0x223F4-0x22426.  It selects GSP VRAM through
 * the TMS34010 host port, enables auto-increment, and emits four passes of
 * 65536 zero longwords.  This is a boot clear, not the game's scene renderer. */
#define STUNRUN_GSP_VRAM_CLEAR_PC 0x223F4u
#define STUNRUN_GSP_VRAM_CLEAR_PASSES 4u
#define STUNRUN_GSP_VRAM_CLEAR_LONGWORDS 65536u
#define STUNRUN_GSP_VRAM_CLEAR_WORDS \
    (STUNRUN_GSP_VRAM_CLEAR_PASSES * STUNRUN_GSP_VRAM_CLEAR_LONGWORDS * 2u)

typedef struct stunrun_gsp_vram_clear_io {
    void *ctx;
    void (*set_address_high)(void *ctx, uint16_t value);
    void (*set_address_low)(void *ctx, uint16_t value);
    void (*set_control)(void *ctx, uint16_t value);
    void (*write_word)(void *ctx, uint16_t value);
    void (*watchdog_clear)(void *ctx);
} stunrun_gsp_vram_clear_io_t;

/* Reproduce the observable host-port writes of 0x223F4-0x22426. */
size_t stunrun_gsp_vram_clear(stunrun_gsp_vram_clear_io_t *io);

#endif
