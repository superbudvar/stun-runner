/* Test the decoded MC68010 -> TMS34010 host-port event fanout.  A synthetic
 * program runs through Musashi so byte lanes and longword split order are
 * asserted at the actual CPU bus seam without commercial ROMs. */
#include "fake_ports.h"
#include "musashi_cpu.h"
#include "../third_party/musashi/m68k.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define STUB_BASE 0x00FFF000u
#define STUB_SENTINEL (STUB_BASE + 90u)
#define MAX_EVENTS 16u

typedef struct event_capture {
    musashi_gsp_bus_event_t events[MAX_EVENTS];
    size_t count;
} event_capture_t;

static unsigned checks;
static unsigned failures;

#define CHECK(expr) do { \
    checks++; \
    if (!(expr)) { \
        fprintf(stderr, "FAIL: %s (line %d)\n", #expr, __LINE__); \
        failures++; \
    } \
} while (0)

#define CHECK_EQ(actual, expected) do { \
    long long actual_ = (long long)(actual); \
    long long expected_ = (long long)(expected); \
    checks++; \
    if (actual_ != expected_) { \
        fprintf(stderr, "FAIL: %s == %s (got %lld, want %lld; line %d)\n", \
                #actual, #expected, actual_, expected_, __LINE__); \
        failures++; \
    } \
} while (0)

static void capture_gsp_event(void *context,
                              const musashi_gsp_bus_event_t *event)
{
    event_capture_t *capture = (event_capture_t *)context;
    if (capture->count < MAX_EVENTS)
        capture->events[capture->count++] = *event;
}

static void write_program(stunrun_fake_ports_t *ports,
                          const uint8_t *bytes, size_t length)
{
    size_t i;
    for (i = 0; i + 1u < length; i += 2u) {
        uint16_t word = (uint16_t)(((uint16_t)bytes[i] << 8) | bytes[i + 1u]);
        CHECK(stunrun_mem_write16_be(ports, STUB_BASE + (unsigned)i, word));
    }
}

static void run_until_sentinel(musashi_cpu_t *cpu)
{
    unsigned cycles = 0;
    m68k_set_reg(M68K_REG_SR, 0x2700u); /* mask interrupts */
    m68k_set_reg(M68K_REG_A7, 0x00FFF800u);
    m68k_set_reg(M68K_REG_PC, STUB_BASE);
    while (musashi_cpu_get_pc(cpu) != STUB_SENTINEL && cycles < 10000u) {
        int used = musashi_cpu_run(cpu, 100);
        CHECK(used > 0);
        if (used <= 0)
            break;
        cycles += (unsigned)used;
    }
    CHECK_EQ(musashi_cpu_get_pc(cpu), STUB_SENTINEL);
}

static void check_event(const event_capture_t *capture, size_t index,
                        uint32_t address, uint16_t data, uint16_t lane_mask,
                        uint8_t width_bits)
{
    const musashi_gsp_bus_event_t *event;
    static const uint32_t expected_pc[] = {
        STUB_BASE + 8u, STUB_BASE + 16u, STUB_BASE + 24u,
        STUB_BASE + 32u, STUB_BASE + 40u, STUB_BASE + 48u,
        STUB_BASE + 58u, STUB_BASE + 58u, STUB_BASE + 66u
    };
    CHECK(index < capture->count);
    if (index >= capture->count)
        return;
    event = &capture->events[index];
    CHECK_EQ(event->kind, MUSASHI_GSP_BUS_HOST_WINDOW);
    CHECK_EQ(event->sequence, index);
    CHECK_EQ(event->address, address);
    CHECK_EQ(event->data, data);
    CHECK_EQ(event->lane_mask, lane_mask);
    CHECK_EQ(event->width_bits, width_bits);
    CHECK_EQ(event->pc, expected_pc[index]);
    CHECK_EQ(event->gsp_reset_released, 0u);
}

static void check_reset_event(const event_capture_t *capture, size_t index,
                              uint32_t address, uint32_t pc,
                              uint8_t released)
{
    const musashi_gsp_bus_event_t *event;
    CHECK(index < capture->count);
    if (index >= capture->count)
        return;
    event = &capture->events[index];
    CHECK_EQ(event->kind, MUSASHI_GSP_BUS_RESET_CONTROL);
    CHECK_EQ(event->sequence, index);
    CHECK_EQ(event->address, address);
    CHECK_EQ(event->pc, pc);
    CHECK_EQ(event->width_bits, 16u);
    CHECK_EQ(event->lane_mask, 0xFFFFu);
    CHECK_EQ(event->data, 0u);
    CHECK_EQ(event->gsp_reset_released, released);
}

int main(void)
{
    /* MOVE.W HSTADRH; MOVE.B low lane of HSTADRH; MOVE.W HSTADRL/HSTCTL/
     * HSTDAT; write another raw address within MAME's mapped host window;
     * MOVE.L to HSTADRH/HSTADRL (two 16-bit writes); MOVE.B high lane of
     * HSTCTL; write beyond MAME's mapped host-port window; issue /GSPRES
     * assert/release through the driver's two decoded NWR addresses; BRA. */
    static const uint8_t program[] = {
        0x33, 0xfc, 0xab, 0xcd, 0x00, 0xc0, 0x00, 0x00,
        0x13, 0xfc, 0x00, 0x12, 0x00, 0xc0, 0x00, 0x01,
        0x33, 0xfc, 0x00, 0x10, 0x00, 0xc0, 0x00, 0x04,
        0x33, 0xfc, 0x08, 0x00, 0x00, 0xc0, 0x00, 0x08,
        0x33, 0xfc, 0xca, 0xfe, 0x00, 0xc0, 0x00, 0x0c,
        0x33, 0xfc, 0xaa, 0xaa, 0x00, 0xc0, 0x00, 0x20,
        0x23, 0xfc, 0x11, 0x22, 0x33, 0x44, 0x00, 0xc0, 0x00, 0x02,
        0x13, 0xfc, 0x00, 0x77, 0x00, 0xc0, 0x00, 0x08,
        0x33, 0xfc, 0xde, 0xad, 0x00, 0xc0, 0x40, 0x00,
        0x33, 0xfc, 0x00, 0x00, 0x00, 0x60, 0x40, 0x0c,
        0x33, 0xfc, 0x00, 0x00, 0x00, 0x60, 0x40, 0x1c,
        0x60, 0xfe
    };
    stunrun_fake_ports_t ports;
    musashi_cpu_t *cpu;
    event_capture_t capture;
    uint32_t hstadrh = 0;
    uint32_t hstadrl = 0;
    musashi_gsp_write_t memory_write;

    memset(&capture, 0, sizeof(capture));
    stunrun_ports_init(&ports);
    cpu = musashi_cpu_create();
    CHECK(cpu != NULL);
    if (cpu == NULL)
        return 1;

    musashi_cpu_set_ports(cpu, &ports);
    musashi_cpu_reset(cpu);
    write_program(&ports, program, sizeof(program));
    musashi_cpu_set_gsp_bus_observer(cpu, capture_gsp_event, &capture);
    run_until_sentinel(cpu);

    CHECK_EQ(capture.count, 11u);
    check_event(&capture, 0, 0x00C00000u, 0xABCDu, 0xFFFFu, 16u);
    check_event(&capture, 1, 0x00C00001u, 0xAB12u, 0x00FFu, 8u);
    check_event(&capture, 2, 0x00C00004u, 0x0010u, 0xFFFFu, 16u);
    check_event(&capture, 3, 0x00C00008u, 0x0800u, 0xFFFFu, 16u);
    check_event(&capture, 4, 0x00C0000Cu, 0xCAFEu, 0xFFFFu, 16u);
    check_event(&capture, 5, 0x00C00020u, 0xAAAAu, 0xFFFFu, 16u);
    check_event(&capture, 6, 0x00C00002u, 0x1122u, 0xFFFFu, 16u);
    check_event(&capture, 7, 0x00C00004u, 0x3344u, 0xFFFFu, 16u);
    check_event(&capture, 8, 0x00C00008u, 0x7700u, 0xFF00u, 8u);
    check_reset_event(&capture, 9, 0x0060400Cu, STUB_BASE + 82u, 0u);
    check_reset_event(&capture, 10, 0x0060401Cu, STUB_BASE + 90u, 1u);
    CHECK_EQ(musashi_cpu_gsp_reset_released(cpu), 1);

    musashi_cpu_gsp_host_addr(cpu, &hstadrh, &hstadrl);
    CHECK_EQ(hstadrh, 0x1122u);
    CHECK_EQ(hstadrl, 0x3344u);
    CHECK_EQ(musashi_cpu_gsp_write_count(cpu), 1u);
    CHECK_EQ(musashi_cpu_gsp_write_at(cpu, 0, &memory_write), 0);
    CHECK_EQ(memory_write.addr, 0xAB120010u);
    CHECK_EQ(memory_write.data, 0xCAFEu);

    musashi_cpu_destroy(cpu);
    if (failures != 0) {
        fprintf(stderr, "%u/%u checks failed\n", failures, checks);
        return 1;
    }
    printf("MC68010 GSP bus observer: %u checks passed\n", checks);
    return 0;
}
