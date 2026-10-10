#ifndef STUNRUN_GSP_DIAGNOSTIC_POLL_H
#define STUNRUN_GSP_DIAGNOSTIC_POLL_H

#include <stddef.h>
#include <stdint.h>

/* MC68010 ROM 0x004F82-0x00506A.  The routine is a diagnostic/GSP
 * handshake, not a renderer.  The meanings of the three called routines
 * remain opaque; the callbacks preserve their observable ordering. */
typedef struct stunrun_gsp_diagnostic_io {
    void *ctx;
    void (*call_gsp_reset)(void *ctx);       /* jsr $003F8 */
    void (*call_10932)(void *ctx);          /* jsr $10932 */
    void (*call_4A48)(void *ctx);           /* jsr $04A48 */
    void (*set_host_address)(void *ctx, uint32_t address);
    uint16_t (*read_host_data)(void *ctx);
    uint32_t (*read_main_long)(void *ctx, uint32_t address);
    void (*write_main_long)(void *ctx, uint32_t address, uint32_t value);
    void (*write_main_byte)(void *ctx, uint32_t address, uint8_t value);
    void (*watchdog_clear)(void *ctx);
} stunrun_gsp_diagnostic_io_t;

/* Returns the number of words read from the GSP host data register (8 or 16).
 * A nonzero return is the path that reaches the 16-word diagnostic copy. */
size_t stunrun_gsp_diagnostic_poll(stunrun_gsp_diagnostic_io_t *io);

#endif
