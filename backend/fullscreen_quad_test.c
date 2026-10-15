#include "fullscreen_quad.h"

#include <stdio.h>

int main(void)
{
    static const float expected[STUNRUN_VIDEO_FULLSCREEN_QUAD_FLOATS] = {
        -1.0f, -1.0f, 0.0f, 1.0f,
        -1.0f,  1.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 1.0f, 1.0f,
         1.0f,  1.0f, 1.0f, 0.0f
    };
    float actual[STUNRUN_VIDEO_FULLSCREEN_QUAD_FLOATS];
    unsigned i;

    for (i = 0; i < STUNRUN_VIDEO_FULLSCREEN_QUAD_FLOATS; ++i)
        actual[i] = -2.0f;  /* deterministic control for an omitted assignment */
    stunrun_video_fullscreen_quad(actual);
    for (i = 0; i < STUNRUN_VIDEO_FULLSCREEN_QUAD_FLOATS; ++i) {
        if (actual[i] != expected[i]) {
            fprintf(stderr, "fullscreen quad component %u: got %.1f, want %.1f\n",
                    i, actual[i], expected[i]);
            return 1;
        }
    }
    puts("PASS: all four fullscreen quad vertices are initialized");
    return 0;
}
