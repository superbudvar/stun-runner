/* Direct VRAM-to-pixel OpenGL scanout used as an independent comparison to
 * native/gsp_display.c. The shader fetches packed pixels and palette planes;
 * only per-scanline DPYADR sequencing is prepared on the CPU and uploaded as a
 * compact line-state texture. It intentionally does not consume RGB output
 * from the CPU renderer. */
#include "gsp_scanout_gl.h"

#include <SDL2/SDL.h>
#include <GL/glew.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GSP_GL_VRAM_WIDTH 2048
#define GSP_GL_VRAM_HEIGHT 128
#define GSP_GL_VRAM_WORDS (GSP_GL_VRAM_WIDTH * GSP_GL_VRAM_HEIGHT)

struct stunrun_gsp_gl_scanout {
    SDL_Window *window;
    SDL_GLContext context;
    GLuint program;
    GLuint quad;
    GLuint vram_texture;
    GLuint palette_lo_texture;
    GLuint palette_hi_texture;
    GLuint lines_texture;
    uint8_t line_bytes[STUNRUN_RENDER_HEIGHT * 8u];
    /* 1 = fit into 4:3 and letterbox (default). 0 = stretch, for parity only. */
    int letterbox;
};

static const char *vertex_shader_source =
    "#version 120\n"
    "attribute vec2 a_pos;\n"
    "void main() { gl_Position = vec4(a_pos, 0.0, 1.0); }\n";

static const char *fragment_shader_source =
    "#version 120\n"
    "uniform sampler2D u_vram;\n"
    "uniform sampler2D u_pal_lo;\n"
    "uniform sampler2D u_pal_hi;\n"
    "uniform sampler2D u_lines;\n"
    "uniform vec2 u_window_size;\n"
    "uniform float u_visible_width;\n"
    "uniform float u_palbank;\n"
    "uniform float u_letterbox;\n"
    "float byte_at(vec4 v, int n) {\n"
    "  if (n == 0) return floor(v.r * 255.0 + 0.5);\n"
    "  if (n == 1) return floor(v.g * 255.0 + 0.5);\n"
    "  if (n == 2) return floor(v.b * 255.0 + 0.5);\n"
    "  return floor(v.a * 255.0 + 0.5);\n"
    "}\n"
    "void main() {\n"
    /* Fit a 4:3 display rect inside the window instead of stretching to fill
     * it.  The GSP scans out 512x240, but this board's raster is
     * set_raw(...,634,0,512,262,0,228) -- 512x228 visible on a 4:3 multisync
     * monitor, so the pixels are NOT square (0.625 as wide as they are tall).
     * Mapping the 2.133:1 image across a 16:9 window stretched it by ~1.6x.
     * Letterbox the bars instead of distorting the picture; that way the frame
     * can be compared against an oracle frame pixel for pixel.
     * u_letterbox == 0 is the unscaled path, used only by the parity test. */
    "  vec2 win = u_window_size;\n"
    "  vec2 rect = win;\n"
    "  vec2 origin = vec2(0.0);\n"
    "  if (u_letterbox > 0.5) {\n"
    "    float target = 4.0 / 3.0;\n"
    "    rect = (win.x / win.y > target)\n"
    "        ? vec2(win.y * target, win.y)\n"
    "        : vec2(win.x, win.x / target);\n"
    "    origin = (win - rect) * 0.5;\n"
    "  }\n"
    "  vec2 p = vec2(gl_FragCoord.x, win.y - gl_FragCoord.y);\n"
    "  if (u_letterbox > 0.5 && (p.x < origin.x || p.y < origin.y ||\n"
    "      p.x >= origin.x + rect.x || p.y >= origin.y + rect.y)) {\n"
    "    gl_FragColor = vec4(0.0,0.0,0.0,1.0); return;\n"
    "  }\n"
    "  vec2 local = (p - origin) / rect;\n"
    "  int x = int(floor(local.x * 512.0));\n"
    "  int y = int(floor(local.y * 240.0));\n"
    "  x = clamp(x, 0, 511); y = clamp(y, 0, 239);\n"
    "  if (float(x) >= u_visible_width) { gl_FragColor = vec4(0.0,0.0,0.0,1.0); return; }\n"
    "  float fy = (float(y) + 0.5) / 240.0;\n"
    "  vec4 a = texture2D(u_lines, vec2(0.25, fy));\n"
    "  vec4 b = texture2D(u_lines, vec2(0.75, fy));\n"
    "  int base = int(byte_at(a,0) + byte_at(a,1)*256.0 + byte_at(a,2)*65536.0);\n"
    "  int col0 = int(byte_at(b,0) + byte_at(b,1)*256.0);\n"
    "  int enabled = int(byte_at(a,3));\n"
    "  if (enabled == 0) { gl_FragColor = vec4(0.0,0.0,0.0,1.0); return; }\n"
    "  int ci = int(mod(float(col0 + x), 2048.0));\n"
    "  int wi = int(mod(float(base + (ci / 2)), 262144.0));\n"
    "  float vx = (mod(float(wi), 2048.0) + 0.5) / 2048.0;\n"
    "  float vy = (floor(float(wi) / 2048.0) + 0.5) / 128.0;\n"
    "  vec4 packed_word = texture2D(u_vram, vec2(vx,vy));\n"
    "  float ink = mod(float(ci), 2.0);\n"
    "  float colour = ink < 0.5 ? floor(packed_word.r*255.0+0.5) : floor(packed_word.a*255.0+0.5);\n"
    "  float pi = mod(u_palbank*256.0 + colour, 1024.0);\n"
    "  float px = (pi + 0.5) / 1024.0;\n"
    "  vec4 plo = texture2D(u_pal_lo, vec2(px,0.5));\n"
    "  vec4 phi = texture2D(u_pal_hi, vec2(px,0.5));\n"
    "  gl_FragColor = vec4(plo.a, plo.r, phi.r, 1.0);\n"
    "}\n";

static void dpy_params(const stunrun_gsp_display_regs_t *regs, uint16_t adr,
                       unsigned *row, unsigned *col, unsigned *yoff)
{
    unsigned x = (regs->dpyctl & STUNRUN_DPYCTL_ORG) == 0u
                     ? ((unsigned)adr ^ 0xFFFCu) : adr;
    *row = x >> 4;
    *col = ((x & 0x7Cu) << 4) | ((unsigned)regs->dpytap & 0x3FFFu);
    *yoff = ((unsigned)regs->dpystrt - (unsigned)adr) & 3u;
}

static uint16_t advance_dpyadr(uint16_t adr, uint16_t ctl, uint16_t start)
{
    unsigned stride = (unsigned)ctl & STUNRUN_DPYCTL_DUDATE;
    if ((adr & 3u) == 0u)
        return (uint16_t)((((unsigned)adr & 0xFFFCu) - (stride & 0xFFFCu)) |
                          ((unsigned)start & 3u));
    return (uint16_t)(((unsigned)adr & 0xFFFCu) |
                      (((unsigned)adr - 1u) & 3u));
}

static void make_line_state(stunrun_gsp_gl_scanout_t *s,
                            const stunrun_gsp_display_regs_t *regs)
{
    unsigned y0 = regs->veblnk;
    unsigned y1 = regs->vsblnk > STUNRUN_GSP_DISPLAY_V_TOTAL
                      ? STUNRUN_GSP_DISPLAY_V_TOTAL : regs->vsblnk;
    uint16_t adr = regs->dpyadr != 0u ? regs->dpyadr : regs->dpystrt;
    unsigned v;

    memset(s->line_bytes, 0, sizeof(s->line_bytes));
    for (v = 0; v < y1; ++v) {
        unsigned row, col, yoff;
        if (v >= y0 && v - y0 < STUNRUN_RENDER_HEIGHT) {
            unsigned out_y = v - y0;
            size_t p = (size_t)out_y * 8u;
            unsigned base, col0;
            dpy_params(regs, adr, &row, &col, &yoff);
            base = (row << 10) & STUNRUN_GSP_VRAM_MASK;
            col0 = (yoff << 9) + ((col & 0xFFu) << 3) - 7u +
                   ((unsigned)regs->finescroll & 7u);
            s->line_bytes[p + 0] = (uint8_t)base;
            s->line_bytes[p + 1] = (uint8_t)(base >> 8);
            s->line_bytes[p + 2] = (uint8_t)(base >> 16);
            s->line_bytes[p + 3] =
                (regs->dpyctl & STUNRUN_DPYCTL_NIL) ? 1u : 0u;
            s->line_bytes[p + 4] = (uint8_t)col0;
            s->line_bytes[p + 5] = (uint8_t)(col0 >> 8);
        }
        if (v >= y0 && v + 1u < y1)
            adr = advance_dpyadr(adr, regs->dpyctl, regs->dpystrt);
    }
}

static GLuint compile_shader(GLenum kind, const char *source)
{
    GLuint shader = glCreateShader(kind);
    GLint ok = 0;
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        fprintf(stderr, "gsp_scanout_gl: shader compile failed: %s\n", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static bool init_program(stunrun_gsp_gl_scanout_t *s)
{
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vertex_shader_source);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fragment_shader_source);
    GLint ok = 0;
    if (!vs || !fs) return false;
    s->program = glCreateProgram();
    glAttachShader(s->program, vs);
    glAttachShader(s->program, fs);
    glBindAttribLocation(s->program, 0, "a_pos");
    glLinkProgram(s->program);
    glGetProgramiv(s->program, GL_LINK_STATUS, &ok);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(s->program, sizeof(log), NULL, log);
        fprintf(stderr, "gsp_scanout_gl: program link failed: %s\n", log);
        return false;
    }
    return true;
}

static void init_texture(GLuint *texture, GLint internal, GLenum format,
                         int width, int height, const void *pixels)
{
    glGenTextures(1, texture);
    glBindTexture(GL_TEXTURE_2D, *texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, internal, width, height, 0, format,
                 GL_UNSIGNED_BYTE, pixels);
}

void stunrun_gsp_gl_scanout_set_letterbox(stunrun_gsp_gl_scanout_t *scanout,
                                          int letterbox)
{
    if (scanout)
        scanout->letterbox = letterbox ? 1 : 0;
}

stunrun_gsp_gl_scanout_t *stunrun_gsp_gl_scanout_create(SDL_Window *window)
{
    static const GLfloat vertices[] = {
        -1.f, -1.f,  1.f, -1.f, -1.f,  1.f,
        -1.f,  1.f,  1.f, -1.f,  1.f,  1.f
    };
    stunrun_gsp_gl_scanout_t *s;
    if (!window) return NULL;
    s = (stunrun_gsp_gl_scanout_t *)calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->window = window;
    /* calloc leaves this 0; the display path WANTS the 4:3 letterbox, so set
     * it explicitly rather than relying on the zero-initialisation. */
    s->letterbox = 1;
    s->context = SDL_GL_CreateContext(window);
    if (!s->context || SDL_GL_MakeCurrent(window, s->context) != 0) {
        fprintf(stderr, "gsp_scanout_gl: context creation failed: %s\n",
                SDL_GetError());
        stunrun_gsp_gl_scanout_destroy(s);
        return NULL;
    }
    if (glewInit() != GLEW_OK || !init_program(s)) {
        stunrun_gsp_gl_scanout_destroy(s);
        return NULL;
    }
    glGenBuffers(1, &s->quad);
    glBindBuffer(GL_ARRAY_BUFFER, s->quad);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    init_texture(&s->vram_texture, GL_LUMINANCE_ALPHA, GL_LUMINANCE_ALPHA,
                 GSP_GL_VRAM_WIDTH, GSP_GL_VRAM_HEIGHT, NULL);
    init_texture(&s->palette_lo_texture, GL_LUMINANCE_ALPHA,
                 GL_LUMINANCE_ALPHA, 1024, 1, NULL);
    init_texture(&s->palette_hi_texture, GL_LUMINANCE_ALPHA,
                 GL_LUMINANCE_ALPHA, 1024, 1, NULL);
    init_texture(&s->lines_texture, GL_RGBA, GL_RGBA, 2,
                 (int)STUNRUN_RENDER_HEIGHT, NULL);
    (void)SDL_GL_SetSwapInterval(0);
    return s;
}

int stunrun_gsp_gl_scanout_present(stunrun_gsp_gl_scanout_t *s,
                                    const uint16_t *vram, size_t vram_words,
                                    const uint16_t *palette_lo,
                                    const uint16_t *palette_hi,
                                    size_t palette_count,
                                    const stunrun_gsp_display_regs_t *regs,
                                    uint8_t *readback_rgb,
                                    size_t readback_capacity)
{
    int win_w, win_h;
    GLuint textures[4];
    GLint loc;
    size_t pal_bytes;
    if (!s || !vram || !regs || vram_words < GSP_GL_VRAM_WORDS) return -1;
    SDL_GL_MakeCurrent(s->window, s->context);
    make_line_state(s, regs);
    SDL_GetWindowSize(s->window, &win_w, &win_h);
    if (win_w <= 0 || win_h <= 0) return 0;

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glBindTexture(GL_TEXTURE_2D, s->vram_texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, GSP_GL_VRAM_WIDTH,
                    GSP_GL_VRAM_HEIGHT, GL_LUMINANCE_ALPHA,
                    GL_UNSIGNED_BYTE, vram);
    glBindTexture(GL_TEXTURE_2D, s->palette_lo_texture);
    pal_bytes = palette_count < 1024u ? palette_count : 1024u;
    if (palette_lo)
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, (GLsizei)pal_bytes, 1,
                        GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, palette_lo);
    if (pal_bytes < 1024u) {
        static const uint8_t zeroes[2048] = {0};
        glTexSubImage2D(GL_TEXTURE_2D, 0, (GLint)pal_bytes, 0,
                        (GLsizei)(1024u - pal_bytes), 1,
                        GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE,
                        zeroes);
    }
    glBindTexture(GL_TEXTURE_2D, s->palette_hi_texture);
    if (palette_hi)
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, (GLsizei)pal_bytes, 1,
                        GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, palette_hi);
    if (pal_bytes < 1024u) {
        static const uint8_t zeroes[2048] = {0};
        glTexSubImage2D(GL_TEXTURE_2D, 0, (GLint)pal_bytes, 0,
                        (GLsizei)(1024u - pal_bytes), 1,
                        GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE,
                        zeroes);
    }
    glBindTexture(GL_TEXTURE_2D, s->lines_texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 2,
                    (GLsizei)STUNRUN_RENDER_HEIGHT, GL_RGBA,
                    GL_UNSIGNED_BYTE, s->line_bytes);

    glViewport(0, 0, win_w, win_h);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(s->program);
    textures[0] = s->vram_texture;
    textures[1] = s->palette_lo_texture;
    textures[2] = s->palette_hi_texture;
    textures[3] = s->lines_texture;
    for (int i = 0; i < 4; ++i) {
        glActiveTexture((GLenum)(GL_TEXTURE0 + i));
        glBindTexture(GL_TEXTURE_2D, textures[i]);
    }
    loc = glGetUniformLocation(s->program, "u_vram"); glUniform1i(loc, 0);
    loc = glGetUniformLocation(s->program, "u_pal_lo"); glUniform1i(loc, 1);
    loc = glGetUniformLocation(s->program, "u_pal_hi"); glUniform1i(loc, 2);
    loc = glGetUniformLocation(s->program, "u_lines"); glUniform1i(loc, 3);
    loc = glGetUniformLocation(s->program, "u_window_size");
    glUniform2f(loc, (GLfloat)win_w, (GLfloat)win_h);
    loc = glGetUniformLocation(s->program, "u_letterbox");
    glUniform1f(loc, (GLfloat)(s->letterbox ? 1.0 : 0.0));
    loc = glGetUniformLocation(s->program, "u_visible_width");
    {
        unsigned visible_width = regs->hsblnk > regs->heblnk
                                     ? (unsigned)regs->hsblnk - regs->heblnk
                                     : 0u;
        if (visible_width > STUNRUN_RENDER_WIDTH)
            visible_width = STUNRUN_RENDER_WIDTH;
        glUniform1f(loc, (GLfloat)visible_width);
    }
    loc = glGetUniformLocation(s->program, "u_palbank");
    glUniform1f(loc, (GLfloat)regs->palbank);
    glBindBuffer(GL_ARRAY_BUFFER, s->quad);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, NULL);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
    if (readback_rgb) {
        size_t required = (size_t)win_w * (size_t)win_h * 3u;
        uint8_t *bottom_up;
        int y;
        if (readback_capacity < required) return -1;
        bottom_up = (uint8_t *)malloc(required);
        if (!bottom_up) return -1;
        glFinish();
        glReadBuffer(GL_BACK);
        glReadPixels(0, 0, win_w, win_h, GL_RGB, GL_UNSIGNED_BYTE,
                     bottom_up);
        for (y = 0; y < win_h; ++y)
            memcpy(readback_rgb + (size_t)y * win_w * 3u,
                   bottom_up + (size_t)(win_h - 1 - y) * win_w * 3u,
                   (size_t)win_w * 3u);
        free(bottom_up);
    }
    SDL_GL_SwapWindow(s->window);
    return glGetError() == GL_NO_ERROR ? 0 : -1;
}

void stunrun_gsp_gl_scanout_destroy(stunrun_gsp_gl_scanout_t *s)
{
    if (!s) return;
    if (s->context) {
        (void)SDL_GL_MakeCurrent(s->window, s->context);
        glDeleteTextures(1, &s->vram_texture);
        glDeleteTextures(1, &s->palette_lo_texture);
        glDeleteTextures(1, &s->palette_hi_texture);
        glDeleteTextures(1, &s->lines_texture);
        glDeleteBuffers(1, &s->quad);
        if (s->program) glDeleteProgram(s->program);
        SDL_GL_DeleteContext(s->context);
    }
    free(s);
}
