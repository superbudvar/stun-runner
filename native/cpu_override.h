/* cpu_override.h — MC68010 → C override system via Musashi instruction hook */

#ifndef STUNRUN_CPU_OVERRIDE_H
#define STUNRUN_CPU_OVERRIDE_H

#include <stdint.h>
#include <stddef.h>

/* Override function signature: receives the PC that triggered the override
 * and the original instruction word at that PC. Must return the PC to
 * resume at (typically pc + instruction_size, or a completely different PC
 * for jump replacements). */
typedef uint32_t (*cpu_override_fn)(uint32_t pc, uint16_t instr_word, void *user);

/* One override entry: [pc_start, pc_end] inclusive range. */
typedef struct cpu_override {
    uint32_t          pc_start;
    uint32_t          pc_end;   /* inclusive */
    cpu_override_fn   fn;
    void             *user;
    const char       *name;     /* human-readable label, may be NULL */
} cpu_override_t;

/* Init / shutdown. Must be called BEFORE musashi_cpu_reset(). */
void cpu_override_init(void);
void cpu_override_shutdown(void);

/* Register an override. Returns its index (for later removal) or -1 on error.
 * Overrides are checked in registration order; first match wins. */
int  cpu_override_add(const cpu_override_t *entry);

/* Remove a previously registered override by index returned from _add.
 * Removal does not compact the table; slots are marked inactive. */
void cpu_override_remove(int index);

/* Called by the Musashi instruction hook (m68k_set_instr_hook_callback).
 * Do not call this directly. */
void cpu_override_dispatch(unsigned int pc);

/* Return the number of active overrides currently registered. */
size_t cpu_override_count(void);

#endif /* STUNRUN_CPU_OVERRIDE_H */
