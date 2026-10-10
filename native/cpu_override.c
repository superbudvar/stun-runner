/* cpu_override.c — MC68010 → C override system */

#include "cpu_override.h"

#include <stdio.h>
#include <string.h>

/* Musashi public API — needed for m68k_set_reg / M68K_REG_PC */
#include "../third_party/musashi/m68k.h"

/* ── Override table ── */

enum { MAX_OVERRIDES = 64 };

static struct {
    cpu_override_t entry;
    int            active;
} s_overrides[MAX_OVERRIDES];

static int s_count = 0;

/* ── Public API ── */

void cpu_override_init(void)
{
    memset(s_overrides, 0, sizeof(s_overrides));
    s_count = 0;
}

void cpu_override_shutdown(void)
{
    /* No-op for now; keep the table for debugging if needed. */
}

int cpu_override_add(const cpu_override_t *entry)
{
    if (s_count >= MAX_OVERRIDES) {
        fprintf(stderr, "cpu_override_add: table full (max %d)\n", MAX_OVERRIDES);
        return -1;
    }
    if (entry->pc_start > entry->pc_end) {
        fprintf(stderr, "cpu_override_add: invalid range 0x%X..0x%X\n",
                entry->pc_start, entry->pc_end);
        return -1;
    }
    int idx = s_count++;
    s_overrides[idx].entry   = *entry;
    s_overrides[idx].active  = 1;
    return idx;
}

void cpu_override_remove(int index)
{
    if (index < 0 || index >= MAX_OVERRIDES) return;
    s_overrides[index].active = 0;
}

size_t cpu_override_count(void)
{
    size_t n = 0;
    for (int i = 0; i < MAX_OVERRIDES; i++)
        if (s_overrides[i].active) n++;
    return n;
}

/* ── Instruction hook dispatch ── */

void cpu_override_dispatch(unsigned int pc)
{
    for (int i = 0; i < MAX_OVERRIDES; i++) {
        if (!s_overrides[i].active) continue;
        const cpu_override_t *e = &s_overrides[i].entry;
        if (pc >= e->pc_start && pc <= e->pc_end) {
            /* Read the instruction word at PC for the override function.
             * We use m68k_read_memory_16 directly; that function is declared
             * in musashi_cpu.h and implemented in musashi_cpu.c. */
            extern unsigned int m68k_read_memory_16(unsigned int address);
            uint16_t instr_word = (uint16_t)m68k_read_memory_16(pc);

            uint32_t resume_pc = e->fn(pc, instr_word, e->user);

            /* Advance PC past the original instruction so Musashi does not
             * re-execute it. If the override function returned a different PC
             * (e.g. a jump replacement), Musashi will resume there instead.
             * M68K_REG_PC = 9 (enum position, see m68k.h). */
            extern void m68k_set_reg(unsigned int reg, unsigned int value);
            m68k_set_reg((unsigned int)9, resume_pc);

#if 0
            fprintf(stderr, "cpu_override: '%s' @ PC=0x%X instr=0x%04X "
                    "→ resume=0x%X\n",
                    e->name ? e->name : "?", pc, instr_word, resume_pc);
#endif
            return;
        }
    }
}
