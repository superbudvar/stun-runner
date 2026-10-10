/* Transport integration plus optional differential execution of local ROMs.
 * The differential path retains the real $225E4 compiler on both sides:
 * it measures caller conversion, not parity of the C compression interpreter. */
#include "fake_ports.h"
#include "gsp_display_list.h"
#include "gsp_upload_callers.h"
#include "musashi_cpu.h"
#include "../third_party/musashi/m68k.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); \
} } while (0)

enum { RETURN_PC = 0xfffe00u, STACK = 0xfffdf0u, RAM_COMPARE = 0x7c00u };
static const uint32_t slots[] = {
    0xbfe04u, 0xbfe78u, 0xbfe80u, 0xbfe84u, 0xbfe88u, 0xbfe08u
};
static const uint32_t destinations[] = {
    0xffde0000u, 0xfff23f00u, 0xfff2b760u,
    0xfff3fd80u, 0xfff3fb20u, 0xffe00000u
};
static const unsigned small_order[] = {1u, 2u, 3u, 4u};
static const unsigned initial_order[] = {0u, 1u, 2u, 3u, 4u, 5u};
static const unsigned reload_order[] = {5u};
typedef void (*caller_fn)(stunrun_gsp_upload_callers_t *);

typedef struct {
    stunrun_fake_ports_t ports;
    musashi_cpu_t *cpu;
    stunrun_gsp_display_list_t host;
    musashi_gsp_upload_adapter_t adapter;
    stunrun_gsp_upload_callers_t io;
    const unsigned *order;
    size_t calls, expected_calls;
    int real_compiler;
} fixture_t;

static void write32(fixture_t *f, uint32_t address, uint32_t value)
{
    CHECK(stunrun_mem_write32_be(&f->ports, address, value));
}

static void execute(fixture_t *f, uint32_t entry, int has_argument,
                    uint32_t argument)
{
    /* BRA.S to itself is a synthetic sentinel, not a ROM instruction read.
     * It permits bounded batch execution without relying on disabled hooks. */
    CHECK(stunrun_mem_write16_be(&f->ports, RETURN_PC, 0x60feu));
    write32(f, STACK, RETURN_PC);
    if (has_argument) write32(f, STACK + 4u, argument);
    m68k_set_reg(M68K_REG_SR, 0x2700u);
    m68k_set_reg(M68K_REG_A7, STACK);
    m68k_set_reg(M68K_REG_PC, entry);
    unsigned cycles = 0u;
    while (musashi_cpu_get_pc(f->cpu) != RETURN_PC && cycles < 50000000u) {
        int used = musashi_cpu_run(f->cpu, 10000);
        CHECK(used > 0);
        cycles += (unsigned)used;
    }
    CHECK(musashi_cpu_get_pc(f->cpu) == RETURN_PC);
}

static void compile(void *ctx, uint32_t stream)
{
    fixture_t *f = ctx;
    if (f->real_compiler) {
        execute(f, 0x225e4u, 1, stream);
    } else {
        CHECK(f->calls < f->expected_calls);
        unsigned index = f->order[f->calls];
        uint32_t hi, lo;
        CHECK(stream == 0x10123u + index * 0x10u);
        musashi_cpu_gsp_host_addr(f->cpu, &hi, &lo);
        CHECK((hi << 16u | lo) == destinations[index]);
        f->host.write_fifo_word(f->host.ctx, (uint16_t)(0x1000u + index));
        f->host.write_fifo_word(f->host.ctx, (uint16_t)(0x2000u + index));
    }
    f->calls++;
}

static void unexpected_transfer(void *ctx, uint32_t address, uint32_t length)
{
    (void)ctx; (void)address; (void)length;
    CHECK(0); /* None of the three caller routines invokes the opaque callee. */
}

static void put32(uint8_t *rom, uint32_t address, uint32_t value)
{
    rom[address] = (uint8_t)(value >> 24u);
    rom[address + 1u] = (uint8_t)(value >> 16u);
    rom[address + 2u] = (uint8_t)(value >> 8u);
    rom[address + 3u] = (uint8_t)value;
}

static void init(fixture_t *f, const char *roms)
{
    memset(f, 0, sizeof(*f));
    stunrun_ports_init(&f->ports);
    f->cpu = musashi_cpu_create();
    CHECK(f->cpu != NULL);
    musashi_cpu_set_ports(f->cpu, &f->ports);
    if (roms) {
        CHECK(musashi_loader_load(f->cpu, roms) == 0);
    } else {
        uint8_t *rom = calloc(MUSASHI_ROM_TOTAL_SIZE, 1u);
        uint8_t even[0x10000], odd[0x10000];
        CHECK(rom != NULL);
        for (unsigned i = 0; i < 6u; i++)
            put32(rom, slots[i], 0x10123u + i * 0x10u);
        put32(rom, 0x47f58u, 0x89abcdefu);
        for (unsigned bank = 0; bank < 6u; bank++) {
            for (unsigned i = 0; i < 0x10000u; i++) {
                even[i] = rom[bank * 0x20000u + i * 2u];
                odd[i] = rom[bank * 0x20000u + i * 2u + 1u];
            }
            CHECK(musashi_cpu_load_rom_bank(f->cpu, bank, even, odd) == 0);
        }
        free(rom);
    }
    musashi_cpu_reset(f->cpu);
    CHECK(musashi_cpu_bind_gsp_display_list(f->cpu, &f->host) == 0);
    f->host.set_host_control(f->host.ctx, 0x0800u);
    f->adapter.cpu = f->cpu;
    f->adapter.callee_ctx = f;
    f->adapter.compile = compile;
    f->adapter.transfer = unexpected_transfer;
    CHECK(musashi_cpu_bind_gsp_upload_callers(&f->adapter, &f->io) == 0);
    write32(f, 0xff9baeu, 0x13572468u);
    CHECK(stunrun_mem_write16_be(&f->ports, 0xff9bceu, 0x5678u));
}

static void synthetic(caller_fn caller, const unsigned *order, size_t count,
                      int writes_state)
{
    fixture_t f;
    init(&f, NULL);
    f.order = order;
    f.expected_calls = count;
    caller(&f.io);
    CHECK(f.calls == count);
    CHECK(musashi_cpu_gsp_write_count(f.cpu) == count * 2u);
    for (size_t i = 0; i < count; i++) {
        for (unsigned j = 0; j < 2u; j++) {
            musashi_gsp_write_t w;
            CHECK(musashi_cpu_gsp_write_at(f.cpu, i * 2u + j, &w) == 0);
            CHECK(w.addr == destinations[order[i]] + j * 0x10u);
            CHECK(w.data == (j ? 0x2000u : 0x1000u) + order[i]);
        }
    }
    uint32_t state;
    uint16_t marker;
    CHECK(stunrun_mem_read32_be(&f.ports, 0xff9baeu, &state));
    CHECK(stunrun_mem_read16_be(&f.ports, 0xff9bceu, &marker));
    CHECK(state == (writes_state ? 0x89abcdefu : 0x13572468u));
    CHECK(marker == (writes_state ? 2u : 0x5678u));
    musashi_cpu_destroy(f.cpu);
}

static void binding_errors(void)
{
    fixture_t f;
    init(&f, NULL);
    stunrun_gsp_upload_callers_t out = f.io;
    musashi_gsp_upload_adapter_t incomplete = f.adapter;
    incomplete.compile = NULL;
    CHECK(musashi_cpu_bind_gsp_upload_callers(&incomplete, &out) == -1);
    CHECK(out.opaque == f.io.opaque && out.compile == f.io.compile);
    incomplete = f.adapter;
    incomplete.transfer = NULL;
    CHECK(musashi_cpu_bind_gsp_upload_callers(&incomplete, &out) == -1);
    musashi_cpu_set_ports(f.cpu, NULL);
    CHECK(musashi_cpu_bind_gsp_upload_callers(&f.adapter, &out) == -1);
    musashi_cpu_destroy(f.cpu);
}

static void bound_bus(void)
{
    fixture_t first, second;
    init(&first, NULL);
    init(&second, NULL); /* This is now the global memory-callback CPU. */
    /* fake_ports RAM is shared globally, so distinguish CPU-owned host latches
     * rather than wrongly treating two fake-port objects as separate RAM. */
    first.io.write_long(first.io.opaque, 0xc00002u, 0xff80abcdu);
    second.io.write_long(second.io.opaque, 0xc00002u, 0xff801234u);
    CHECK(first.io.read_word(first.io.opaque, 0xc00004u) == 0xabcdu);
    CHECK(second.io.read_word(second.io.opaque, 0xc00004u) == 0x1234u);
    uint32_t hi, lo;
    musashi_cpu_gsp_host_addr(first.cpu, &hi, &lo);
    CHECK(hi == 0xff80u && lo == 0xabcdu);
    musashi_cpu_gsp_host_addr(second.cpu, &hi, &lo);
    CHECK(hi == 0xff80u && lo == 0x1234u);
    musashi_cpu_destroy(first.cpu);
    musashi_cpu_destroy(second.cpu);
}

static void differential(const char *roms, uint32_t entry, caller_fn caller,
                         size_t call_count)
{
    fixture_t f;
    init(&f, roms);
    execute(&f, entry, 0, 0u);
    size_t count = musashi_cpu_gsp_write_count(f.cpu);
    CHECK(count != 0u);
    musashi_gsp_write_t *expected = malloc(count * sizeof(*expected));
    uint8_t *ram = malloc(RAM_COMPARE);
    CHECK(expected && ram);
    for (size_t i = 0; i < count; i++)
        CHECK(musashi_cpu_gsp_write_at(f.cpu, i, &expected[i]) == 0);
    for (unsigned i = 0; i < RAM_COMPARE; i++)
        CHECK(stunrun_mem_read8(&f.ports, 0xff8000u + i, ram + i));
    uint32_t expected_hi, expected_lo;
    musashi_cpu_gsp_host_addr(f.cpu, &expected_hi, &expected_lo);
    musashi_cpu_destroy(f.cpu);

    init(&f, roms);
    f.real_compiler = 1;
    caller(&f.io);
    CHECK(f.calls == call_count);
    CHECK(musashi_cpu_gsp_write_count(f.cpu) == count);
    for (size_t i = 0; i < count; i++) {
        musashi_gsp_write_t actual;
        CHECK(musashi_cpu_gsp_write_at(f.cpu, i, &actual) == 0);
        CHECK(actual.addr == expected[i].addr);
        CHECK(actual.data == expected[i].data);
    }
    for (unsigned i = 0; i < RAM_COMPARE; i++) {
        uint8_t actual;
        CHECK(stunrun_mem_read8(&f.ports, 0xff8000u + i, &actual));
        CHECK(actual == ram[i]);
    }
    uint32_t hi, lo;
    musashi_cpu_gsp_host_addr(f.cpu, &hi, &lo);
    CHECK(hi == expected_hi && lo == expected_lo);
    printf("ROM caller %06x: %zu calls, %zu address/word pairs match\n",
           entry, call_count, count);
    free(expected);
    free(ram);
    musashi_cpu_destroy(f.cpu);
}

int main(void)
{
    stunrun_gsp_upload_callers_t out;
    musashi_gsp_upload_adapter_t missing = {0};
    CHECK(musashi_cpu_bind_gsp_upload_callers(NULL, &out) == -1);
    CHECK(musashi_cpu_bind_gsp_upload_callers(&missing, &out) == -1);
    CHECK(musashi_cpu_bind_gsp_upload_callers(&missing, NULL) == -1);
    binding_errors();
    bound_bus();
    synthetic(stunrun_gsp_upload_small, small_order, 4u, 0);
    synthetic(stunrun_gsp_upload_initial, initial_order, 6u, 1);
    synthetic(stunrun_gsp_upload_reload, reload_order, 1u, 1);
    const char *roms = getenv("STUNRUN_UPLOAD_CALLERS_ROMDIR");
    if (roms && *roms) {
        differential(roms, 0x2d762u, stunrun_gsp_upload_small, 4u);
        differential(roms, 0x2db84u, stunrun_gsp_upload_reload, 1u);
        differential(roms, 0x2d7c2u, stunrun_gsp_upload_initial, 6u);
    } else {
        puts("ROM differential not run (set STUNRUN_UPLOAD_CALLERS_ROMDIR)");
    }
    puts("gsp upload callers Musashi integration: ok");
    return 0;
}
