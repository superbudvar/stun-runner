#include "gsp_vram_clear.h"

#include <stdio.h>

typedef struct fixture {
    unsigned address_high;
    unsigned address_low;
    unsigned control;
    unsigned watchdogs;
    size_t writes;
    uint16_t first;
    uint16_t last;
} fixture_t;

static void set_high(void *ctx, uint16_t value) { ((fixture_t *)ctx)->address_high = value; }
static void set_low(void *ctx, uint16_t value) { ((fixture_t *)ctx)->address_low = value; }
static void set_control(void *ctx, uint16_t value) { ((fixture_t *)ctx)->control = value; }
static void watchdog(void *ctx) { ((fixture_t *)ctx)->watchdogs++; }
static void write_word(void *ctx, uint16_t value)
{
    fixture_t *f = (fixture_t *)ctx;
    if (f->writes == 0u) f->first = value;
    f->last = value;
    f->writes++;
}

int main(void)
{
    fixture_t f = {0};
    stunrun_gsp_vram_clear_io_t io = {
        &f, set_high, set_low, set_control, write_word, watchdog
    };
    size_t writes = stunrun_gsp_vram_clear(&io);
    if (writes != STUNRUN_GSP_VRAM_CLEAR_WORDS ||
        f.writes != STUNRUN_GSP_VRAM_CLEAR_WORDS || f.first != 0u ||
        f.last != 0u || f.address_high != 0xFF80u || f.address_low != 0u ||
        f.control != 0xB800u || f.watchdogs != STUNRUN_GSP_VRAM_CLEAR_PASSES) {
        fprintf(stderr, "gsp vram clear slice failed\n");
        return 1;
    }
    puts("gsp vram clear slice: all checks passed");
    return 0;
}
