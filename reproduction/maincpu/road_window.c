/* See road_window.h for the observed listing contract and the evidence split. */
#include <string.h>

#include "road_buffer_math.h"
#include "road_window.h"

void stunrun_road_window_init(stunrun_road_window *w, uint8_t *base,
                              uint8_t *twin)
{
    if (w == NULL) {
        return;
    }
    w->base = base;
    w->twin = twin;
    w->latch = 0u;
    w->active = 0u;
}

uint16_t stunrun_road_window_latch(const stunrun_road_window *w)
{
    return (w == NULL) ? 0u : w->latch;
}

void stunrun_road_window_latch_store(stunrun_road_window *w, uint16_t value)
{
    if (w == NULL) {
        return;
    }
    w->latch = value;
}

void stunrun_road_window_latch_load_divisor(stunrun_road_window *w,
                                            uint8_t divisor)
{
    if (w == NULL) {
        return;
    }
    /* moveq #0,D0 ; move.b (divisor),D0 ; move.w D0,$FF954E */
    w->latch = (uint16_t)divisor;
}

uint8_t stunrun_road_window_divisor(const stunrun_road_window *w)
{
    return (w == NULL) ? 0u : (uint8_t)(w->latch & 0xFFu);
}

int stunrun_road_window_load(stunrun_road_window *w, const uint8_t *source,
                             uint8_t divisor, size_t bytes)
{
    if (w == NULL || w->base == NULL || w->twin == NULL || source == NULL ||
        divisor == 0u) {
        return 0;
    }
    if (!stunrun_road_buffer_copy_scaled(source, w->base, w->twin, divisor,
                                         bytes)) {
        return 0;
    }
    stunrun_road_window_latch_load_divisor(w, divisor);
    return 1;
}

stunrun_road_pass stunrun_road_window_gate(stunrun_road_window *w)
{
    if (w == NULL) {
        return STUNRUN_ROAD_PASS_NONE;
    }
    /* subq.w #1,$FF954E wraps; beq tests the result. Both callers run the same
     * three instructions and differ only in the jsr that follows, so this
     * function reports "a pass was selected" and cannot report which. */
    w->latch = (uint16_t)(w->latch - 1u);
    return (w->latch == 0u) ? STUNRUN_ROAD_PASS_NONE
                            : STUNRUN_ROAD_PASS_SUBTRACT;
}

int stunrun_road_window_subtract(stunrun_road_window *w)
{
    if (w == NULL || w->base == NULL || w->twin == NULL) {
        return 0;
    }
    stunrun_road_buffer_subtract(w->base, w->twin,
                                 STUNRUN_ROAD_WINDOW_BYTES);
    w->active = STUNRUN_ROAD_WINDOW_BASE_ADDR;
    return 1;
}

int stunrun_road_window_add(stunrun_road_window *w)
{
    if (w == NULL || w->base == NULL || w->twin == NULL) {
        return 0;
    }
    stunrun_road_buffer_add(w->base, w->twin, STUNRUN_ROAD_WINDOW_BYTES);
    w->active = STUNRUN_ROAD_WINDOW_BASE_ADDR;
    return 1;
}

int stunrun_road_window_step_subtract(stunrun_road_window *w)
{
    if (stunrun_road_window_gate(w) != STUNRUN_ROAD_PASS_SUBTRACT) {
        return 0;
    }
    return stunrun_road_window_subtract(w);
}

int stunrun_road_window_step_add(stunrun_road_window *w)
{
    if (stunrun_road_window_gate(w) != STUNRUN_ROAD_PASS_SUBTRACT) {
        return 0;
    }
    return stunrun_road_window_add(w);
}

uint32_t stunrun_road_window_active(const stunrun_road_window *w)
{
    return (w == NULL) ? 0u : w->active;
}

static uint8_t *road_window_buffer(stunrun_road_window *w,
                                   stunrun_road_buffer_id which)
{
    return (which == STUNRUN_ROAD_BUFFER_TWIN) ? w->twin : w->base;
}

int stunrun_road_window_byte(const stunrun_road_window *w,
                             stunrun_road_buffer_id which, size_t index,
                             uint8_t *out)
{
    const uint8_t *src;

    if (w == NULL || out == NULL || index >= STUNRUN_ROAD_WINDOW_BYTES) {
        return 0;
    }
    src = (which == STUNRUN_ROAD_BUFFER_TWIN) ? w->twin : w->base;
    if (src == NULL) {
        return 0;
    }
    *out = src[index];
    return 1;
}

int stunrun_road_window_geometry(stunrun_road_window *w,
                                 stunrun_road_buffer_id which,
                                 size_t offset_words, size_t count_words,
                                 uint16_t *words_out, size_t words_cap,
                                 stunrun_road_geometry *view)
{
    const uint8_t *src;
    size_t i;

    if (view != NULL) {
        memset(view, 0, sizeof(*view));
    }
    if (w == NULL || words_out == NULL || view == NULL) {
        return 0;
    }
    src = road_window_buffer(w, which);
    if (src == NULL) {
        return 0;
    }
    if (offset_words > STUNRUN_ROAD_WINDOW_WORDS ||
        count_words > STUNRUN_ROAD_WINDOW_WORDS - offset_words ||
        count_words > words_cap) {
        return 0;
    }
    for (i = 0; i < count_words; i++) {
        const uint8_t *p = src + ((offset_words + i) * 2u);
        words_out[i] = (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
    }
    view->words = words_out;
    view->count = count_words;
    view->stride_words = STUNRUN_ROAD_WINDOW_TWIN_STRIDE / 2u;
    view->offset_words = offset_words;
    return 1;
}

int stunrun_road_window_geometry_base(stunrun_road_window *w, uint16_t *words_out,
                                      size_t words_cap,
                                      stunrun_road_geometry *view)
{
    return stunrun_road_window_geometry(w, STUNRUN_ROAD_BUFFER_BASE, 0u,
                                        STUNRUN_ROAD_WINDOW_WORDS, words_out,
                                        words_cap, view);
}

size_t stunrun_road_window_divergence(const stunrun_road_window *w,
                                      size_t *first_offset)
{
    size_t differing = 0u;
    size_t first = STUNRUN_ROAD_WINDOW_BYTES;
    size_t i;

    if (first_offset != NULL) {
        *first_offset = STUNRUN_ROAD_WINDOW_BYTES;
    }
    if (w == NULL || w->base == NULL || w->twin == NULL) {
        return 0u;
    }
    for (i = 0u; i < STUNRUN_ROAD_WINDOW_BYTES; i++) {
        if (w->base[i] != w->twin[i]) {
            differing++;
            if (first > i) {
                first = i;
            }
        }
    }
    if (differing != 0u && first_offset != NULL) {
        *first_offset = first;
    }
    return differing;
}