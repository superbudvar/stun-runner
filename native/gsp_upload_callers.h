#ifndef STUNRUN_GSP_UPLOAD_CALLERS_H
#define STUNRUN_GSP_UPLOAD_CALLERS_H

#include <stdint.h>

/* 68010 caller-side effects only. All callbacks must be supplied. ROM pointers
 * are read through the seam, not replaced by the currently observed assets.
 * compile invokes the existing $225E4 operation; transfer represents $2247A,
 * whose body and data interpretation are deliberately outside this slice.
 * Host destination writes are the literal 68010 long write to $C00002. */
typedef struct {
    void *opaque;
    uint16_t (*read_word)(void *, uint32_t);
    uint32_t (*read_long)(void *, uint32_t);
    void (*write_word)(void *, uint32_t, uint16_t);
    void (*write_long)(void *, uint32_t, uint32_t);
    void (*compile)(void *, uint32_t);
    void (*transfer)(void *, uint32_t, uint32_t);
} stunrun_gsp_upload_callers_t;

void stunrun_gsp_upload_small(stunrun_gsp_upload_callers_t *io); /* $2D762 */
void stunrun_gsp_upload_initial(stunrun_gsp_upload_callers_t *io); /* $2D7C2 */
void stunrun_gsp_upload_reload(stunrun_gsp_upload_callers_t *io); /* $2DB84 */
void stunrun_gsp_upload_chunks(stunrun_gsp_upload_callers_t *io,
                               uint32_t address, uint32_t length); /* $2D80E */
void stunrun_gsp_upload_emit_byte(stunrun_gsp_upload_callers_t *io,
                                  uint32_t value); /* $2D858 */

#endif
