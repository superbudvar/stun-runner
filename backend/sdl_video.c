/* SDL2 + OpenGL / OpenGL-ES video backend.
 *
 * Platform support:
 *   - Linux   (OpenGL 2+ via GLEW)
 *   - macOS   (OpenGL, also usable with SDL's metal/vulkan renderers)
 *   - Windows (OpenGL / D3D via SDL GL shim)
 *   - Android (OpenGL ES 2/3 via SDL + GLES2 headers)
 *   - Raspberry Pi (OpenGL ES via SDL)
 *   - Web/Emscripten (WebGL via SDL)
 *
 * Rendering pipeline:
 *   CPU pixels (RGB888 from render.c)
 *     -> glTexImage2D upload (fast path with GL_UNPACK_ROW_LENGTH)
 *     -> blit to fullscreen quad with GL_NEAREST sampling
 *
 * When a palette is supplied (PALETTED_8BPP), the backend stores it and
 * applies it during upload: each 8-bit index is expanded to RGB888 in a
 * temporary buffer before the glTexImage2D call.  This is slower than
 * uploading already-expanded pixels but keeps the CPU-side pipeline simple.
 *
 * The backend also implements a fast-path for RGBA8888 frames: direct
 * texture upload without colour conversion.
 *
 * OpenGL loader: GLEW on desktop (OpenGL 2+), GLES2 headers on mobile.
 */
#include "backend/backend.h"
#include "fullscreen_quad.h"

#include <SDL2/SDL.h>

#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__) && !defined(__RPI__)
/* Desktop Linux / macOS / Windows: use GLEW for modern GL function pointers */
# include <GL/glew.h>
#else
/* Mobile / embedded: use GLES2 headers and SDL GL proc loader */
# include <GLES2/gl2.h>
# include <SDL2/SDL_opengles.h>
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Texture object owned by this backend. */
typedef struct sdl_gl_tex {
    GLuint id;
    uint32_t width;
    uint32_t height;
    GLenum internal_format;  /* GL_RGB / GL_RGBA */
    GLenum format;           /* GL_RGB / GL_RGBA */
    GLenum type;            /* GL_UNSIGNED_BYTE */
} sdl_gl_tex_t;

typedef struct sdl_video_backend {
    const char *name;
    SDL_Window *window;
    SDL_GLContext gl_ctx;

    /* Fullscreen quad VAO / VBO */
    GLuint vao;
    GLuint vbo;
    GLuint shader_program;

    /* Current game resolution (pixels produced by core) */
    uint32_t game_w;
    uint32_t game_h;

    /* Current window resolution */
    uint32_t win_w;
    uint32_t win_h;

    /* Palette for 8bpp mode */
    uint8_t palette[256 * 3];
    bool    palette_dirty;

    /* Temporary upload buffer (for palette expansion) */
    uint8_t *upload_buf;
    size_t   upload_buf_capacity;

    /* Fast-path texture (owned) */
    sdl_gl_tex_t tex;

    /* Vertex layout for the fullscreen quad (2 verts × 3 floats × 2 = 12 floats) */
    float quad_vertices[STUNRUN_VIDEO_FULLSCREEN_QUAD_FLOATS];

    bool initialized;
} sdl_video_backend_t;

/* Vertex shader — passthrough with texture coordinates */
static const char *VERT_SRC = "#version 120\n"
    "attribute vec2 a_pos;\n"
    "attribute vec2 a_tex;\n"
    "varying vec2 v_tex;\n"
    "void main() {\n"
    "    gl_Position = vec4(a_pos, 0.0, 1.0);\n"
    "    v_tex = a_tex;\n"
    "}\n";

/* Fragment shader — samples a 2D texture with nearest-neighbour */
static const char *FRAG_SRC = "#version 120\n"
    "uniform sampler2D u_tex;\n"
    "varying vec2 v_tex;\n"
    "void main() {\n"
    "    gl_FragColor = texture2D(u_tex, v_tex);\n"
    "}\n";

/* OpenGL ES 2.0 shaders for mobile platforms */
static const char *VERT_SRC_ES = "attribute vec2 a_pos;\n"
    "attribute vec2 a_tex;\n"
    "varying vec2 v_tex;\n"
    "void main() {\n"
    "    gl_Position = vec4(a_pos, 0.0, 1.0);\n"
    "    v_tex = a_tex;\n"
    "}\n";

static const char *FRAG_SRC_ES = "precision mediump float;\n"
    "uniform sampler2D u_tex;\n"
    "varying vec2 v_tex;\n"
    "void main() {\n"
    "    gl_FragColor = texture2D(u_tex, v_tex);\n"
    "}\n";

/* Check GL error and return false if one occurred. */
static bool gl_ok(const char *file, int line)
{
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        fprintf(stderr, "sdl_video: GL error 0x%x at %s:%d\n", err, file, line);
        return false;
    }
    return true;
}

/* Compile a shader and return its ID.  Logs on failure. */
static GLuint compile_shader(const char *src, GLenum type)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        fprintf(stderr, "sdl_video: shader compile error: %s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    (void)gl_ok;
    return shader;
}

/* Link a vertex + fragment shader pair into a program.  Logs on failure. */
static GLuint link_program(const char *vert_src, const char *frag_src)
{
    GLuint vs = compile_shader(vert_src, GL_VERTEX_SHADER);
    GLuint fs = compile_shader(frag_src, GL_FRAGMENT_SHADER);
    if (!vs || !fs)
        return 0;
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(prog, sizeof(log), NULL, log);
        fprintf(stderr, "sdl_video: program link error: %s\n", log);
        glDeleteProgram(prog);
        prog = 0;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

/* Build a fullscreen quad in NDC.  Vertices are (x, y, u, v). */
static void build_quad(float *verts, uint32_t win_w, uint32_t win_h)
{
    (void)win_w;
    (void)win_h;
    stunrun_video_fullscreen_quad(verts);
}

/* (internal) */
static void sdl_video_init_quad(sdl_video_backend_t *b)
{
    build_quad(b->quad_vertices, b->win_w, b->win_h);

    glGenVertexArrays(1, &b->vao);
    glGenBuffers(1, &b->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, b->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(b->quad_vertices),
                 b->quad_vertices, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

/* (internal) Initialise GL objects after context is current. */
static bool sdl_video_init_gl(sdl_video_backend_t *b)
{
    /* Detect GL version and select shaders accordingly */
    const char *gl_version = (const char *)glGetString(GL_VERSION);
    bool is_es = gl_version && (strncmp(gl_version, "OpenGL ES", 9) == 0);

    b->shader_program = link_program(
        is_es ? VERT_SRC_ES : VERT_SRC,
        is_es ? FRAG_SRC_ES : FRAG_SRC);
    if (!b->shader_program)
        return false;

    sdl_video_init_quad(b);

    /* Create initial texture */
    glGenTextures(1, &b->tex.id);
    glBindTexture(GL_TEXTURE_2D, b->tex.id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    b->tex.width  = b->game_w;
    b->tex.height = b->game_h;
    b->tex.internal_format = GL_RGB;
    b->tex.format = GL_RGB;
    b->tex.type   = GL_UNSIGNED_BYTE;

    return gl_ok(__FILE__, __LINE__);
}

/* Allocate or grow the upload buffer. */
static bool sdl_video_ensure_upload_buf(sdl_video_backend_t *b, size_t min_bytes)
{
    if (b->upload_buf != NULL && b->upload_buf_capacity >= min_bytes)
        return true;
    free(b->upload_buf);
    b->upload_buf = (uint8_t *)malloc(min_bytes);
    if (!b->upload_buf)
        return false;
    b->upload_buf_capacity = min_bytes;
    return true;
}

/* Expand a paletted 8bpp frame to RGB888 in-place into upload_buf. */
static void expand_palette_8bpp(sdl_video_backend_t *b,
                                 const uint8_t *src, size_t src_stride)
{
    for (uint32_t row = 0; row < b->game_h; row++) {
        const uint8_t *src_row = src + row * src_stride;
        uint8_t *dst_row = b->upload_buf + row * b->game_w * 3;
        for (uint32_t col = 0; col < b->game_w; col++) {
            uint8_t idx = src_row[col];
            dst_row[col * 3 + 0] = b->palette[idx * 3 + 0];
            dst_row[col * 3 + 1] = b->palette[idx * 3 + 1];
            dst_row[col * 3 + 2] = b->palette[idx * 3 + 2];
        }
    }
}

/* ---- Public backend v-table ---- */

static bool sdl_video_create(void *user, void *context)
{
    sdl_video_backend_t *b = (sdl_video_backend_t *)user;
    b->window = (SDL_Window *)context;
    (void)b;
    return true;
}

static bool sdl_video_init(void *user, uint32_t game_w, uint32_t game_h,
                          uint32_t win_w, uint32_t win_h)
{
    sdl_video_backend_t *b = (sdl_video_backend_t *)user;
    b->game_w = game_w;
    b->game_h = game_h;
    b->win_w  = win_w;
    b->win_h  = win_h;

    b->gl_ctx = SDL_GL_CreateContext(b->window);
    if (!b->gl_ctx) {
        fprintf(stderr, "sdl_video: SDL_GL_CreateContext failed: %s\n",
                SDL_GetError());
        return false;
    }
    SDL_GL_MakeCurrent(b->window, b->gl_ctx);

#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__) && !defined(__RPI__)
    /* GLEW on desktop: must be called after GL context is current */
    {
        GLenum glew_err = glewInit();
        if (glew_err != GLEW_OK) {
            fprintf(stderr, "sdl_video: glewInit failed: %s\n",
                    glewGetErrorString(glew_err));
            return false;
        }
    }
#endif

    if (!sdl_video_init_gl(b))
        return false;

    b->palette_dirty = false;
    b->initialized = true;
    return true;
}

static void sdl_video_shutdown(void *user)
{
    sdl_video_backend_t *b = (sdl_video_backend_t *)user;
    if (b->gl_ctx) {
        SDL_GL_MakeCurrent(b->window, b->gl_ctx);
        glDeleteTextures(1, &b->tex.id);
        glDeleteBuffers(1, &b->vbo);
        glDeleteVertexArrays(1, &b->vao);
        if (b->shader_program)
            glDeleteProgram(b->shader_program);
        SDL_GL_DeleteContext(b->gl_ctx);
        b->gl_ctx = NULL;
    }
    b->initialized = false;
}

static void sdl_video_destroy(void *user)
{
    sdl_video_backend_t *b = (sdl_video_backend_t *)user;
    free(b->upload_buf);
    b->upload_buf = NULL;
    b->upload_buf_capacity = 0;
}

static int sdl_video_upload(void *user, const stunrun_video_frame_t *frame)
{
    sdl_video_backend_t *b = (sdl_video_backend_t *)user;
    if (!b->initialized || !frame || !frame->pixels)
        return 0;

    SDL_GL_MakeCurrent(b->window, b->gl_ctx);

    glBindTexture(GL_TEXTURE_2D, b->tex.id);

    if (frame->format == STUNRUN_VIDEO_FORMAT_RGB888) {
        /* Fast path: already RGB888 */
        glPixelStorei(GL_UNPACK_ROW_LENGTH, (GLint)(frame->stride / 3));
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB,
                     (GLsizei)frame->width, (GLsizei)frame->height,
                     0, GL_RGB, GL_UNSIGNED_BYTE, frame->pixels);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

    } else if (frame->format == STUNRUN_VIDEO_FORMAT_RGBA8888) {
        glPixelStorei(GL_UNPACK_ROW_LENGTH, (GLint)(frame->stride / 4));
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                     (GLsizei)frame->width, (GLsizei)frame->height,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, frame->pixels);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

    } else if (frame->format == STUNRUN_VIDEO_FORMAT_PALETTED_8BPP) {
        size_t needed = (size_t)frame->width * (size_t)frame->height * 3;
        if (!sdl_video_ensure_upload_buf(b, needed))
            return 0;
        expand_palette_8bpp(b, frame->pixels, frame->stride);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, (GLint)(frame->stride));
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB,
                     (GLsizei)frame->width, (GLsizei)frame->height,
                     0, GL_RGB, GL_UNSIGNED_BYTE, b->upload_buf);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

    } else if (frame->format == STUNRUN_VIDEO_FORMAT_RGB565) {
        size_t needed = (size_t)frame->width * (size_t)frame->height * 2;
        if (!sdl_video_ensure_upload_buf(b, needed))
            return 0;
        /* Convert RGB565 to RGB888 in-place */
        for (size_t i = 0; i < needed / 2; i++) {
            uint16_t rgb565 = ((const uint16_t *)frame->pixels)[i];
            uint8_t r = (rgb565 >> 11) & 0x1F;
            uint8_t g = (rgb565 >> 5)  & 0x3F;
            uint8_t bl = rgb565 & 0x1F;
            b->upload_buf[i * 3 + 0] = (r * 255) / 31;
            b->upload_buf[i * 3 + 1] = (g * 255) / 63;
            b->upload_buf[i * 3 + 2] = (bl * 255) / 31;
        }
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB,
                     (GLsizei)frame->width, (GLsizei)frame->height,
                     0, GL_RGB, GL_UNSIGNED_BYTE, b->upload_buf);

    } else {
        fprintf(stderr, "sdl_video: unsupported pixel format %d\n",
                (int)frame->format);
        return 0;
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    return 1;
}

static int sdl_video_set_palette(void *user, const stunrun_video_palette_t *pal)
{
    sdl_video_backend_t *b = (sdl_video_backend_t *)user;
    if (!pal || !pal->entries || pal->count != 256)
        return 0;
    memcpy(b->palette, pal->entries, sizeof(b->palette));
    b->palette_dirty = true;
    return 1;
}

static void sdl_video_present(void *user)
{
    sdl_video_backend_t *b = (sdl_video_backend_t *)user;
    if (!b->initialized)
        return;

    SDL_GL_MakeCurrent(b->window, b->gl_ctx);

    glViewport(0, 0, (GLsizei)b->win_w, (GLsizei)b->win_h);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(b->shader_program);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, b->tex.id);
    glUniform1i(glGetUniformLocation(b->shader_program, "u_tex"), 0);

    /* Rebuild quad if window size changed (simple: just update each present) */
    build_quad(b->quad_vertices, b->win_w, b->win_h);
    glBindBuffer(GL_ARRAY_BUFFER, b->vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(b->quad_vertices),
                    b->quad_vertices);

    GLint pos_loc  = glGetAttribLocation(b->shader_program, "a_pos");
    GLint tex_loc  = glGetAttribLocation(b->shader_program, "a_tex");

    glEnableVertexAttribArray((GLuint)pos_loc);
    glEnableVertexAttribArray((GLuint)tex_loc);
    glVertexAttribPointer((GLuint)pos_loc, 2, GL_FLOAT, GL_FALSE,
                          4 * sizeof(float), (void *)(0));
    glVertexAttribPointer((GLuint)tex_loc, 2, GL_FLOAT, GL_FALSE,
                          4 * sizeof(float), (void *)(2 * sizeof(float)));

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisableVertexAttribArray((GLuint)pos_loc);
    glDisableVertexAttribArray((GLuint)tex_loc);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);

    SDL_GL_SwapWindow(b->window);
}

static void sdl_video_on_resize(void *user, uint32_t win_w, uint32_t win_h)
{
    sdl_video_backend_t *b = (sdl_video_backend_t *)user;
    b->win_w = win_w;
    b->win_h = win_h;
}

/* ---- Backend registration ---- */

static stunrun_video_backend_t the_backend = {
    .name          = "sdl_opengl",
    .create        = sdl_video_create,
    .init          = sdl_video_init,
    .shutdown      = sdl_video_shutdown,
    .destroy       = sdl_video_destroy,
    .upload        = sdl_video_upload,
    .set_palette   = sdl_video_set_palette,
    .present       = sdl_video_present,
    .on_resize     = sdl_video_on_resize,
    .upload_tex    = NULL,
    .user          = NULL,
    .user_size     = sizeof(sdl_video_backend_t),
};

const stunrun_video_backend_t *stunrun_video_backend_sdl_opengl(void)
{
    return &the_backend;
}

/* Registration entry point — called before main() via static init */
const stunrun_video_backend_t *stunrun_video_backend_sdl_opengl_register(void)
{
    /* Statically register with the registry.  The registry is defined in
     * backend_registry.c. */
    extern void stunrun_backend_register_video(const stunrun_video_backend_t *);
    stunrun_backend_register_video(&the_backend);
    return &the_backend;
}

/* Force registration to run before main().
 *
 * A static function POINTER only forces the linker to keep the symbol; it does
 * not call it, so the registry stayed empty and every backend looked "not
 * found".  A constructor attribute actually invokes the register function. */
static void __attribute__((constructor))
stunrun_backend_auto_register(void)
{
    (void)stunrun_video_backend_sdl_opengl_register();
}
