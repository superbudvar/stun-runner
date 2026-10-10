/* Integration check for the literal display-list slice and the live Musashi
 * GSP host model.  The slice unit test owns instruction-level oddities; this
 * test owns the subsystem boundary: emitted words must become the exact host
 * write log, address-latch walk, work-RAM table, and replicated VRAM cells.
 */
#include "fake_ports.h"
#include "gsp_display_list.h"
#include "musashi_cpu.h"
#include "../third_party/musashi/m68k.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int failures;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL: %s\n", #condition); \
        failures++; \
    } \
} while (0)

static void check_stride_blocks(musashi_cpu_t *cpu, unsigned enabled)
{
    size_t i;
    uint32_t hi, lo;
    CHECK(musashi_cpu_gsp_write_count(cpu) == 480u);
    for (i = 0; i < 480u; i++) {
        musashi_gsp_write_t write = {0};
        CHECK(musashi_cpu_gsp_write_at(cpu, i, &write) == 0);
        CHECK(write.addr == 0x02000000u + (uint32_t)(i / 32u) * 0x4000u +
                           (uint32_t)(i % 32u) * 0x20u);
        CHECK(write.data == 1u);
    }
    musashi_cpu_gsp_host_addr(cpu, &hi, &lo);
    CHECK(hi == 0x0203u && lo == (enabled ? 0x83f0u : 0x83e0u));
}

static const uint8_t stream_all[] = {
    0x00,
    0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6,
    0xA7, 0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD,
    0xAE, 0xAF,
    0x45, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
    0xC2, 0x37,
    0x84, 0x99,
    0xC0
};

static const uint16_t expected_words[] = {
    0x00A3u, 0x00A4u, 0x00A5u, 0x00A6u, 0x00A7u,
    0x1122u, 0x3344u, 0x5566u, 0x7788u,
    0x00A7u, 0x00A7u,
    0x0099u, 0x0099u
};

static void check_replicated_cell(const uint8_t *vram, size_t cell,
                                  uint16_t word)
{
    size_t base = cell * 16u;
    unsigned i;
    for (i = 0; i < 8u; i++) {
        CHECK(vram[base + i] == (uint8_t)word);
        CHECK(vram[base + 8u + i] == (uint8_t)(word >> 8));
    }
}

static void stride_blocks(const char *roms, unsigned enabled)
{
    /* Full documented caller encoding, checked against locally loaded ROMs
     * before execution, so a wrong bank/version cannot silently be our oracle. */
    static const uint8_t caller_bytes[] = {
        0x20, 0x3c, 0x02, 0x00, 0x00, 0x00,
        0x32, 0x3c, 0x00, 0x0f, 0x34, 0x3c, 0x00, 0x01,
        0x61, 0x00, 0x00, 0x0e, 0x06, 0x80, 0x00, 0x00, 0x40, 0x00,
        0x53, 0x41, 0x66, 0xf2, 0x4e, 0x75
    };
    stunrun_fake_ports_t ports;
    stunrun_gsp_display_list_t io;
    musashi_gsp_write_t expected[480];
    uint32_t expected_hi = 0u, expected_lo = 0u;
    unsigned pass, cycles;
    size_t i;

    for (pass = 0u; pass < (roms ? 2u : 1u); pass++) {
        musashi_cpu_t *cpu;
        stunrun_ports_init(&ports);
        cpu = musashi_cpu_create();
        CHECK(cpu != NULL);
        if (!cpu) return;
        musashi_cpu_set_ports(cpu, &ports);
        if (roms && musashi_loader_load(cpu, roms) != 0) {
            CHECK(0);
            musashi_cpu_destroy(cpu);
            return;
        }
        musashi_cpu_reset(cpu);
        CHECK(musashi_cpu_bind_gsp_display_list(cpu, &io) == 0);
        io.set_host_control(io.ctx, (uint16_t)(0xa000u | (enabled ? 0x0800u : 0u)));
        if (roms && pass == 0u) {
            for (i = 0u; i < sizeof(caller_bytes); i++)
                CHECK(m68k_read_memory_8(0xc28u + (unsigned)i) == caller_bytes[i]);
            /* Synthetic sentinel BRA.S to itself; do not execute boot or
             * install any live override. IRQs are masked for the isolated call. */
            CHECK(stunrun_mem_write16_be(&ports, 0xfffe00u, 0x60feu));
            CHECK(stunrun_mem_write32_be(&ports, 0xfffdf0u, 0xfffe00u));
            m68k_set_reg(M68K_REG_SR, 0x2700u);
            m68k_set_reg(M68K_REG_A7, 0xfffdf0u);
            m68k_set_reg(M68K_REG_PC, 0xc28u);
            cycles = 0u;
            while (musashi_cpu_get_pc(cpu) != 0xfffe00u && cycles < 1000000u) {
                int used = musashi_cpu_run(cpu, 1000);
                CHECK(used > 0);
                if (used <= 0) break;
                cycles += (unsigned)used;
            }
            CHECK(musashi_cpu_get_pc(cpu) == 0xfffe00u);
            CHECK(m68k_get_reg(NULL, M68K_REG_D0) == 0x0203c000u);
            CHECK((m68k_get_reg(NULL, M68K_REG_D1) & 0xffffu) == 0u);
            CHECK((m68k_get_reg(NULL, M68K_REG_D2) & 0xffffu) == 1u);
            for (i = 0u; i < 480u; i++) {
                expected[i].addr = expected[i].data = 0u;
                CHECK(musashi_cpu_gsp_write_at(cpu, i, &expected[i]) == 0);
            }
            musashi_cpu_gsp_host_addr(cpu, &expected_hi, &expected_lo);
        } else {
            stunrun_gsp_display_list_fill_stride_blocks(&io);
            if (roms) {
                uint32_t hi, lo;
                for (i = 0u; i < 480u; i++) {
                    musashi_gsp_write_t actual = {0};
                    CHECK(musashi_cpu_gsp_write_at(cpu, i, &actual) == 0);
                    CHECK(actual.addr == expected[i].addr);
                    CHECK(actual.data == expected[i].data);
                }
                musashi_cpu_gsp_host_addr(cpu, &hi, &lo);
                CHECK(hi == expected_hi && lo == expected_lo);
            }
        }
        check_stride_blocks(cpu, enabled);
        /* Neither caller nor helper changes control; read through the bus. */
        CHECK(m68k_read_memory_16(0xc00008u) == (0xa000u | (enabled ? 0x0800u : 0u)));
        musashi_cpu_destroy(cpu);
    }
    if (roms)
        printf("ROM 000c28: 480 address/word pairs and latches match (increment=%u)\n",
               enabled);
}

int main(void)
{
    stunrun_fake_ports_t ports;
    musashi_cpu_t *cpu;
    stunrun_gsp_display_list_t io;
    stunrun_gsp_display_list_result_t result;
    musashi_gsp_region_counts_t counts;
    musashi_gsp_memory_t memory;
    uint32_t address_hi = 0u;
    uint32_t address_lo = 0u;
    uint8_t byte = 0u;
    size_t i;

    stunrun_ports_init(&ports);
    cpu = musashi_cpu_create();
    CHECK(cpu != NULL);
    if (cpu == NULL)
        return 1;

    CHECK(musashi_cpu_bind_gsp_display_list(NULL, &io) == -1);
    CHECK(musashi_cpu_bind_gsp_display_list(cpu, NULL) == -1);
    CHECK(musashi_cpu_bind_gsp_display_list(cpu, &io) == -1);
    musashi_cpu_set_ports(cpu, &ports);
    CHECK(musashi_cpu_bind_gsp_display_list(cpu, &io) == 0);

    io.set_host_address_hi(io.ctx, 0xFF80u);
    io.set_host_address_lo(io.ctx, 0x0000u);
    io.set_host_control(io.ctx, 0x0000u);
    io.or_host_control_mask(io.ctx, 0x0800u);
    stunrun_gsp_display_list_run(&io, stream_all, &result);

    CHECK(result.words_emitted == sizeof(expected_words) / sizeof(expected_words[0]));
    CHECK(result.stream_end == sizeof(stream_all));
    CHECK(result.end == STUNRUN_GSP_DL_END_RESET);
    CHECK(musashi_cpu_gsp_write_count(cpu) ==
          sizeof(expected_words) / sizeof(expected_words[0]));

    for (i = 0; i < sizeof(expected_words) / sizeof(expected_words[0]); i++) {
        musashi_gsp_write_t write;
        CHECK(musashi_cpu_gsp_write_at(cpu, i, &write) == 0);
        CHECK(write.addr == 0xFF800000u + (uint32_t)(16u * i));
        CHECK(write.data == expected_words[i]);
    }

    musashi_cpu_gsp_host_addr(cpu, &address_hi, &address_lo);
    CHECK(address_hi == 0xFF80u);
    CHECK(address_lo == 0x00D0u);

    musashi_cpu_gsp_region_counts(cpu, &counts);
    CHECK(counts.vram == 13u);
    CHECK(counts.vram_2bpp == 0u);
    CHECK(counts.pal_lo == 0u);
    CHECK(counts.pal_hi == 0u);
    CHECK(counts.control == 0u);
    CHECK(counts.unmapped == 0u);

    CHECK(musashi_cpu_gsp_memory(cpu, &memory) == 0);
    for (i = 0; i < sizeof(expected_words) / sizeof(expected_words[0]); i++)
        check_replicated_cell((const uint8_t *)memory.vram, i,
                              expected_words[i]);

    CHECK(stunrun_mem_read8(&ports, STUNRUN_GSP_DL_TABLE_LO, &byte));
    CHECK(byte == 0xA0u);
    CHECK(stunrun_mem_read8(&ports, STUNRUN_GSP_DL_TABLE_LO + 30u, &byte));
    CHECK(byte == 0xAFu);
    CHECK(stunrun_mem_read8(&ports, STUNRUN_GSP_DL_TABLE_HI + 1u, &byte));
    CHECK(byte == 0xA0u);
    CHECK(stunrun_mem_read8(&ports, STUNRUN_GSP_DL_TABLE_HI + 31u, &byte));
    CHECK(byte == 0xAFu);

    musashi_cpu_destroy(cpu);
    stride_blocks(NULL, 0u);
    stride_blocks(NULL, 1u);
    {
        const char *roms = getenv("STUNRUN_UPLOAD_CALLERS_ROMDIR");
        if (roms && *roms) {
            stride_blocks(roms, 0u);
            stride_blocks(roms, 1u);
        } else {
            puts("0x0C28 ROM differential not run (set STUNRUN_UPLOAD_CALLERS_ROMDIR)");
        }
    }
    if (failures != 0) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    puts("gsp display-list Musashi integration: ok");
    return 0;
}
