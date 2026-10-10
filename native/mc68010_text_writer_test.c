#include "mc68010_text_writer.h"

#include <stdio.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "check failed: %s\n", #x); return 1; } } while (0)

typedef struct { uint32_t address[128]; uint16_t value[128]; size_t count; } log_t;

static int capture(void *ctx, uint32_t address, uint16_t value)
{
    log_t *log = (log_t *)ctx;
    if (log->count == 128u) return 0;
    log->address[log->count] = address;
    log->value[log->count++] = value;
    return 1;
}

int main(void)
{
    static const uint8_t descriptor[] = {1u, 15u, 0xDFu, 2u, 'A', 'A'};
    uint16_t font[24u * 8u] = {0};
    log_t log = {{0}, {0}, 0u};
    font[23u * 8u] = 0x0005u; /* 'A' - the ROM's 0x2A glyph bias */
    CHECK(stunrun_mc68010_text_write(descriptor, sizeof(descriptor), font,
                                     sizeof(font) / sizeof(font[0]), capture,
                                     &log) == 128u);
    CHECK(log.count == 128u);
    CHECK(log.address[0] == 0xFF896040u && log.value[0] == 0xDFDFu);
    CHECK(log.address[1] == 0xFF896050u && log.value[1] == 0u);
    CHECK(log.address[7] == 0xFF8960B0u);
    CHECK(log.address[8] == 0xFF897040u);
    CHECK(log.address[63] == 0xFF89D0B0u);
    CHECK(log.address[64] == 0xFF896080u);
    return 0;
}
