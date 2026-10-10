/* musashi_loader.c — Load STUN Runner ROM files into Musashi MC68010
 *
 * Callable API (no main): musashi_loader_load(cpu, roms_dir).
 * Returns 0 on success, -1 on failure.
 *
 * ROM layout from MAME multisync_68k_map:
 *   0x000000–0x0FFFFF: flat 1MB ROM (NO banking)
 *   6 × 64KB banks loaded at offsets 0, 0x20000, 0x40000, 0x60000, 0x80000, 0xA0000
 *
 * ROM_LOAD16_BYTE with ROM_SKIP(1):
 *   even file bytes → addresses 0, 2, 4, 6, ...  (MSB of each 16-bit word)
 *   odd file bytes  → addresses 1, 3, 5, 7, ...  (LSB of each 16-bit word)
 * Result: big-endian 16-bit words, CPU address N = high byte at N, low byte at N+1
 *
 * Bank files (MAME ROM_LOAD16_BYTE definitions):
 *   Bank 0: 136070-2102.200r (even, MSB) + 136070-2101.210r (odd, LSB) → 0x00000
 *   Bank 1: 136070-6104.200s (even)        + 136070-6103.210s (odd)        → 0x20000
 *   Bank 2: 136070-6106.200t (even)        + 136070-6105.210t (odd)        → 0x40000
 *   Bank 3: 136070-2108.200u (even)        + 136070-2107.210u (odd)        → 0x60000
 *   Bank 4: 136070-2110.200v (even)        + 136070-2109.210v (odd)        → 0x80000
 *   Bank 5: 136070-2112.200w (even)        + 136070-2111.210w (odd)        → 0xA0000
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "musashi_cpu.h"

/* 64KB per even/odd file pair */
#define BANK_SIZE 0x10000

typedef struct {
    const char *even;   /* .200r/.200s/.200t/.200u/.200v/.200w */
    const char *odd;    /* .210r/.210s/.210t/.210u/.210v/.210w */
    unsigned    offset; /* byte offset in 68010 address space */
} bank_info_t;

static const bank_info_t BANKS[6] = {
    { "136070-2102.200r", "136070-2101.210r", 0x00000 },
    { "136070-6104.200s", "136070-6103.210s", 0x20000 },
    { "136070-6106.200t", "136070-6105.210t", 0x40000 },
    { "136070-2108.200u", "136070-2107.210u", 0x60000 },
    { "136070-2110.200v", "136070-2109.210v", 0x80000 },
    { "136070-2112.200w", "136070-2111.210w", 0xA0000 },
};

/* Load one even/odd file pair into the given buffers (64KB each). */
static int load_pair(const char *roms_dir, const bank_info_t *bi,
                     uint8_t *even_out, uint8_t *odd_out)
{
    char path[512];

    snprintf(path, sizeof(path), "%s/%s", roms_dir, bi->even);
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "ERROR: cannot open %s\n", path); return -1; }
    size_t n = fread(even_out, 1, BANK_SIZE, f);
    fclose(f);
    if (n != BANK_SIZE) {
        fprintf(stderr, "ERROR: short read from %s (%zu bytes)\n", path, n);
        return -1;
    }

    snprintf(path, sizeof(path), "%s/%s", roms_dir, bi->odd);
    f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "ERROR: cannot open %s\n", path); return -1; }
    n = fread(odd_out, 1, BANK_SIZE, f);
    fclose(f);
    if (n != BANK_SIZE) {
        fprintf(stderr, "ERROR: short read from %s (%zu bytes)\n", path, n);
        return -1;
    }

    return 0;
}

/* Load all 6 STUN Runner ROM banks into the Musashi MC68010 instance.
 * Returns 0 on success, -1 on any file I/O failure. */
int musashi_loader_load(musashi_cpu_t *cpu, const char *roms_dir)
{
    uint8_t even[BANK_SIZE];
    uint8_t odd[BANK_SIZE];
    unsigned bank;

    if (!cpu || !roms_dir) return -1;

    for (bank = 0; bank < 6; bank++) {
        if (load_pair(roms_dir, &BANKS[bank], even, odd) != 0)
            return -1;

        if (musashi_cpu_load_rom_bank(cpu, bank, even, odd) != 0) {
            fprintf(stderr, "ERROR: musashi_cpu_load_rom_bank failed for bank %u\n", bank);
            return -1;
        }
        printf("musashi_loader: bank %u loaded from offset 0x%05X\n",
               bank, BANKS[bank].offset);
    }

    /* Reset vector: word at 68010 address 0x000000 (even[0]=MSB, odd[0]=LSB).
     * Initial PC = 0xFFFF (execution starts near top of bank 0, after vector table).
     * This is a valid 24-bit address in the ROM region 0x000000–0x0FFFFF. */
    {
        uint16_t reset_vec = (uint16_t)((even[0] << 8) | odd[0]);
        printf("musashi_loader: reset vector = 0x%04X (initial PC will be 0x%04X)\n",
               reset_vec, reset_vec);
    }

    return 0;
}
