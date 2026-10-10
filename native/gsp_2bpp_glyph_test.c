#include "gsp_2bpp_glyph.h"

#include <stdio.h>

typedef struct {
    uint32_t addresses[2];
    uint16_t words[2];
    unsigned address_count;
    unsigned word_count;
} fixture_t;

static void set_host_address(void *opaque, uint32_t address)
{
    fixture_t *f = (fixture_t *)opaque;
    if (f->address_count < 2u)
        f->addresses[f->address_count] = address;
    ++f->address_count;
}

static void write_host_data(void *opaque, uint16_t value)
{
    fixture_t *f = (fixture_t *)opaque;
    if (f->word_count < 2u)
        f->words[f->word_count] = value;
    ++f->word_count;
}

int main(void)
{
    fixture_t f = {{0u, 0u}, {0u, 0u}, 0u, 0u};
    stunrun_gsp_2bpp_glyph_io_t io = {&f, set_host_address, write_host_data};

    stunrun_gsp_2bpp_emit_row(&io, 0x02001000u, 0xFFFFu, 0u);
    if (f.address_count != 2u || f.word_count != 2u ||
        f.addresses[0] != 0x02001000u || f.addresses[1] != 0x02001010u ||
        f.words[0] != 0x5555u || f.words[1] != 0x5555u) {
        fprintf(stderr, "2bpp glyph expansion mismatch\n");
        return 1;
    }
    return 0;
}
