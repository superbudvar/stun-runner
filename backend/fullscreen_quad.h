/* Canonical fullscreen texture quad used by the SDL OpenGL backends.
 * Vertices are (x, y, u, v), in GL_TRIANGLE_STRIP order. */
#ifndef STUNRUN_BACKEND_FULLSCREEN_QUAD_H
#define STUNRUN_BACKEND_FULLSCREEN_QUAD_H

#define STUNRUN_VIDEO_FULLSCREEN_QUAD_FLOATS 16u

static inline void stunrun_video_fullscreen_quad(float *v)
{
    v[0]  = -1.0f; v[1]  = -1.0f; v[2]  = 0.0f; v[3]  = 1.0f; /* bottom-left */
    v[4]  = -1.0f; v[5]  =  1.0f; v[6]  = 0.0f; v[7]  = 0.0f; /* top-left */
    v[8]  =  1.0f; v[9]  = -1.0f; v[10] = 1.0f; v[11] = 1.0f; /* bottom-right */
    v[12] =  1.0f; v[13] =  1.0f; v[14] = 1.0f; v[15] = 0.0f; /* top-right */
}

#endif
