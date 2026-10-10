#include "gsp_diagnostic_poll.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct fixture {
    uint32_t addresses[32];
    uint16_t data[32];
    uint32_t main_long_address[32];
    uint32_t main_long_value[32];
    uint32_t main_long_count;
    uint32_t address_count;
    uint32_t read_count;
    uint32_t status_reads;
    uint32_t reset_count;
    uint32_t call_10932_count;
    uint32_t call_4a48_count;
    uint32_t watchdog_count;
    uint32_t byte_count;
} fixture_t;

static void reset(void *ctx) { ((fixture_t *)ctx)->reset_count++; }
static void call_10932(void *ctx) { ((fixture_t *)ctx)->call_10932_count++; }
static void call_4a48(void *ctx) { ((fixture_t *)ctx)->call_4a48_count++; }
static void address(void *ctx, uint32_t value)
{
    fixture_t *f = ctx;
    f->addresses[f->address_count++] = value;
}
static uint16_t read_data(void *ctx)
{
    fixture_t *f = ctx;
    return f->data[f->read_count++];
}
static uint32_t read_main(void *ctx, uint32_t address)
{
    fixture_t *f = ctx;
    assert(address == 0xFF9042u);
    f->status_reads++;
    return 0x1D4Cu;
}
static void write_long(void *ctx, uint32_t address, uint32_t value)
{
    fixture_t *f = ctx;
    f->main_long_address[f->main_long_count] = address;
    f->main_long_value[f->main_long_count++] = value;
}
static void write_byte(void *ctx, uint32_t address, uint8_t value)
{
    (void)address; (void)value;
    ((fixture_t *)ctx)->byte_count++;
}
static void watchdog(void *ctx) { ((fixture_t *)ctx)->watchdog_count++; }

int main(void)
{
    fixture_t f;
    stunrun_gsp_diagnostic_io_t io;
    size_t words;
    memset(&f, 0, sizeof(f));
    for (unsigned i = 0; i < 7; ++i)
        f.data[i] = (uint16_t)(2u + i);
    for (unsigned i = 0; i < 16; ++i)
        f.data[7 + i] = (uint16_t)(0x1000u + i);
    memset(&io, 0, sizeof(io));
    io.ctx = &f; io.call_gsp_reset = reset; io.call_10932 = call_10932;
    io.call_4A48 = call_4a48; io.set_host_address = address;
    io.read_host_data = read_data; io.read_main_long = read_main;
    io.write_main_long = write_long; io.write_main_byte = write_byte;
    io.watchdog_clear = watchdog;

    /* The fixture returns the observed progression 2..8, then 16 status
     * words, which takes the captured ROM's 16-word branch. */
    words = stunrun_gsp_diagnostic_poll(&io);
    assert(words == 16);
    assert(f.reset_count == 2);
    assert(f.call_10932_count == 1);
    assert(f.call_4a48_count == 2);
    assert(f.status_reads == 1);
    assert(f.address_count == 23);
    assert(f.addresses[1] == 0xFFC00000u);
    assert(f.addresses[6] == 0xFFC00000u);
    assert(f.addresses[7] == 0xFFC00020u);
    assert(f.addresses[22] == 0xFFC00110u);
    assert(f.main_long_count == 24);
    assert(f.main_long_address[8] == 0xFF9048u);
    assert(f.main_long_value[8] == 0x1000u);
    assert(f.byte_count == 32);
    puts("gsp diagnostic poll slice: ok");
    return 0;
}
