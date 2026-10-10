#include "gsp_word_pair.h"

#include <stdio.h>

typedef struct {
    uint16_t words[2];
    unsigned count;
} fixture_t;

static void write_host_data(void *opaque, uint16_t value)
{
    fixture_t *f = (fixture_t *)opaque;
    if (f->count < 2u)
        f->words[f->count] = value;
    ++f->count;
}

int main(void)
{
    fixture_t f = {{0u, 0u}, 0u};
    stunrun_gsp_word_pair_io_t io = {&f, write_host_data};

    stunrun_gsp_emit_word_pair(&io, 0x1234u, 0xABCDu);
    if (f.count != 2u || f.words[0] != 0xABCDu || f.words[1] != 0x1234u) {
        fprintf(stderr, "$2FE6E/$2FE76 order mismatch\n");
        return 1;
    }

    stunrun_gsp_emit_word_pair(NULL, 1u, 2u);
    return 0;
}
