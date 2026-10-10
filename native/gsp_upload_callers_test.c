#include "gsp_upload_callers.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

typedef struct { char kind; uint32_t address, value; } event_t;
typedef struct {
    event_t events[64];
    size_t count;
    uint32_t streams[6], marker;
    uint16_t previous, pending, remaining;
} fixture_t;

static void record(fixture_t *f, char kind, uint32_t address, uint32_t value)
{
    assert(f->count < 64u);
    f->events[f->count++] = (event_t){kind, address, value};
}

static uint32_t read_long(void *opaque, uint32_t address)
{
    fixture_t *f = opaque;
    static const uint32_t slots[] = {
        0xbfe04u, 0xbfe78u, 0xbfe80u, 0xbfe84u, 0xbfe88u, 0xbfe08u
    };
    uint32_t value = f->marker;
    size_t i;
    for (i = 0; i < 6u; ++i)
        if (address == slots[i]) { value = f->streams[i]; break; }
    assert(i < 6u || address == 0x47f58u);
    record(f, 'R', address, value);
    return value;
}

static uint16_t read_word(void *opaque, uint32_t address)
{
    fixture_t *f = opaque;
    uint16_t value;
    if (address == 0xff9c64u) value = f->previous;
    else if (address == 0xff9c66u) value = f->pending;
    else { assert(address == 0xff9c68u); value = f->remaining; }
    record(f, 'r', address, value);
    return value;
}

static void write_long(void *opaque, uint32_t address, uint32_t value)
{
    record(opaque, 'W', address, value);
}

static void write_word(void *opaque, uint32_t address, uint16_t value)
{
    fixture_t *f = opaque;
    record(f, 'w', address, value);
    if (address == 0xff9c64u) f->previous = value;
    else if (address == 0xff9c66u) f->pending = value;
    else if (address == 0xff9c68u) f->remaining = value;
}

static void compile(void *opaque, uint32_t stream) { record(opaque, 'C', stream, 0u); }
static void transfer(void *opaque, uint32_t address, uint32_t length)
{
    record(opaque, 'T', address, length);
}

static stunrun_gsp_upload_callers_t init(fixture_t *f)
{
    static const uint32_t streams[] = {
        0x60002u, 0x94d09u, 0x97f4bu, 0x9841cu, 0x98457u, 0x6118fu
    };
    memset(f, 0, sizeof(*f));
    memcpy(f->streams, streams, sizeof(streams));
    f->marker = 0xf0u;
    return (stunrun_gsp_upload_callers_t){
        f, read_word, read_long, write_word, write_long, compile, transfer
    };
}

static void expect(fixture_t *f, const event_t *events, size_t count)
{
    size_t i;
    assert(f->count == count);
    for (i = 0; i < count; ++i) {
        assert(f->events[i].kind == events[i].kind);
        assert(f->events[i].address == events[i].address);
        assert(f->events[i].value == events[i].value);
    }
}

#define UPLOAD(dest, slot, stream) {'W', 0xc00002u, dest}, {'R', slot, stream}, {'C', stream, 0u}
#define CHECK(f, events) expect(f, events, sizeof(events) / sizeof((events)[0]))

static void test_uploads(void)
{
    fixture_t f;
    stunrun_gsp_upload_callers_t io = init(&f);
    /* Expected destinations/arguments independently observed in the complete
     * MAME producer trace. ROM slot addresses and state writes from assembly. */
    const event_t initial[] = {
        UPLOAD(0xffde0000u, 0xbfe04u, 0x60002u),
        UPLOAD(0xfff23f00u, 0xbfe78u, 0x94d09u),
        UPLOAD(0xfff2b760u, 0xbfe80u, 0x97f4bu),
        UPLOAD(0xfff3fd80u, 0xbfe84u, 0x9841cu),
        UPLOAD(0xfff3fb20u, 0xbfe88u, 0x98457u),
        UPLOAD(0xffe00000u, 0xbfe08u, 0x6118fu),
        {'R', 0x47f58u, 0xf0u}, {'W', 0xff9baeu, 0xf0u}, {'w', 0xff9bceu, 2u}
    };
    stunrun_gsp_upload_initial(&io);
    CHECK(&f, initial);
    io = init(&f);
    stunrun_gsp_upload_small(&io);
    expect(&f, initial + 3, 12u);
    io = init(&f);
    stunrun_gsp_upload_reload(&io);
    expect(&f, initial + 15, 6u);

    /* Do not bake current ROM assets/marker into the implementation. */
    io = init(&f);
    f.streams[5] = 0x123456u;
    f.marker = 0x89abcdefu;
    stunrun_gsp_upload_reload(&io);
    const event_t changed[] = {
        UPLOAD(0xffe00000u, 0xbfe08u, 0x123456u),
        {'R', 0x47f58u, 0x89abcdefu}, {'W', 0xff9baeu, 0x89abcdefu},
        {'w', 0xff9bceu, 2u}
    };
    CHECK(&f, changed);
}

static void test_chunks(void)
{
    fixture_t f;
    stunrun_gsp_upload_callers_t io = init(&f);
    const event_t zero[] = {{'T', 0x1000u, 0u}};
    stunrun_gsp_upload_chunks(&io, 0x1000u, 0u);
    CHECK(&f, zero);
    io = init(&f);
    const event_t exact[] = {{'T', 0x1000u, 0x10000u}};
    stunrun_gsp_upload_chunks(&io, 0x1000u, 0x10000u);
    CHECK(&f, exact);
    io = init(&f);
    const event_t remainder[] = {
        {'T', 0x1000u, 0x10000u}, {'T', 0x21000u, 1u}
    };
    stunrun_gsp_upload_chunks(&io, 0x1000u, 0x10001u);
    CHECK(&f, remainder);
    io = init(&f);
    const event_t multiple[] = {
        {'T', 0x1000u, 0x10000u}, {'T', 0x21000u, 0x10000u}
    };
    stunrun_gsp_upload_chunks(&io, 0x1000u, 0x20000u);
    CHECK(&f, multiple);
    io = init(&f);
    const event_t wrap[] = {
        {'T', 0xffff0000u, 0x10000u}, {'T', 0x10000u, 2u}
    };
    stunrun_gsp_upload_chunks(&io, 0xffff0000u, 0x10002u);
    CHECK(&f, wrap);
}

static void test_pending_byte(void)
{
    fixture_t f;
    stunrun_gsp_upload_callers_t io = init(&f);
    stunrun_gsp_upload_emit_byte(&io, 0x12u);
    stunrun_gsp_upload_emit_byte(&io, 0x34u);
    const event_t pair[] = {
        {'r', 0xff9c68u, 0u}, {'r', 0xff9c66u, 0u},
        {'w', 0xff9c64u, 0x12u}, {'w', 0xff9c66u, 1u},
        {'r', 0xff9c68u, 0u}, {'r', 0xff9c66u, 1u},
        {'r', 0xff9c64u, 0x12u}, {'w', 0xc0000cu, 0x1234u},
        {'w', 0xff9c66u, 0u}
    };
    CHECK(&f, pair);
    assert(f.pending == 0u && f.previous == 0x12u);

    io = init(&f);
    f.remaining = 0x8000u;
    f.pending = 1u;
    f.previous = 0x12u;
    stunrun_gsp_upload_emit_byte(&io, 0x34u);
    const event_t skip[] = {
        {'r', 0xff9c68u, 0x8000u}, {'w', 0xff9c68u, 0x7fffu}
    };
    CHECK(&f, skip);
    assert(f.pending == 1u && f.previous == 0x12u);

    io = init(&f);
    f.remaining = 1u;
    stunrun_gsp_upload_emit_byte(&io, 0xaau); /* Discard this argument. */
    stunrun_gsp_upload_emit_byte(&io, 0xbbu); /* First retained argument. */
    const event_t countdown_end[] = {
        {'r', 0xff9c68u, 1u}, {'w', 0xff9c68u, 0u},
        {'r', 0xff9c68u, 0u}, {'r', 0xff9c66u, 0u},
        {'w', 0xff9c64u, 0xbbu}, {'w', 0xff9c66u, 1u}
    };
    CHECK(&f, countdown_end);
    assert(f.pending == 1u); /* Return does not flush the unmatched byte. */

    io = init(&f);
    f.pending = 2u; /* Any nonzero flag follows the emit path. */
    f.previous = 0x12u;
    stunrun_gsp_upload_emit_byte(&io, 0x12340456u);
    const event_t wide[] = {
        {'r', 0xff9c68u, 0u}, {'r', 0xff9c66u, 2u},
        {'r', 0xff9c64u, 0x12u}, {'w', 0xc0000cu, 0x1656u},
        {'w', 0xff9c66u, 0u}
    };
    CHECK(&f, wide);
}

int main(void)
{
    test_uploads();
    test_chunks();
    test_pending_byte();
    puts("gsp upload callers: ordered effects and edge cases passed");
    return 0;
}
