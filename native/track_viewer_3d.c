/* 3D Track Viewer for S.T.U.N. Runner
 *
 * Extracts track geometry from ROM and renders it in 3D using OpenGL.
 * Uses the transcribed road routines to decode track data.
 *
 * Track data is stored in ROM at addresses like 0x044630 (384 words per track).
 * The road_expand_bits algorithm decodes each 16-bit word into a 32-bit value
 * which represents track strip geometry (colour and position).
 *
 * Build:
 *   gcc -o track_viewer_3d track_viewer_3d.c -lGL -lGLU -lglut -lm
 *
 * Usage:
 *   ./track_viewer_3d <rom_path> [-t track_num] [-d strips] [-o output.txt]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

#ifdef USE_OPENGL
#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/glut.h>
#endif

/* ── ROM loading ─────────────────────────────────────────────────────── */

#define STUNRUN_ROM_MAX_SIZE (12 * 64 * 1024)  /* 12 lanes × 64 KiB */

static uint8_t *load_rom(const char *path, size_t *size_out)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Cannot open ROM: %s\n", path);
        return NULL;
    }

    uint8_t *rom = malloc(STUNRUN_ROM_MAX_SIZE);
    if (!rom) {
        fclose(f);
        return NULL;
    }

    size_t bytes = fread(rom, 1, STUNRUN_ROM_MAX_SIZE, f);
    fclose(f);

    *size_out = bytes;
    return rom;
}

/* ── ROM helpers (from mc68010_road_writer.c) ─────────────────────────── */

static uint16_t rom_word(const uint8_t *rom, uint32_t address)
{
    return (uint16_t)(((uint16_t)rom[address] << 8) |
                      (uint16_t)rom[address + 1]);
}

/* ── road_expand_bits ─────────────────────────────────────────────────── */

/* Bit expansion algorithm from ROM $234B6.
 * Rotate-left-through-carry for 16 iterations, then shift right by 1.
 * This spreads the bits of the input word across a 32-bit result. */
static uint32_t road_expand_bits(uint16_t word)
{
    uint32_t d6 = 0;
    uint16_t d5 = word;
    unsigned i;

    /* roxl.w #1,D5 followed by roxl.l #2,D6, 16 times */
    for (i = 0; i < 16; i++) {
        /* Rotate D5 left through carry by 1 */
        d5 = (uint16_t)((d5 << 1) | (d6 >> 31));
        /* Rotate D6 left through carry by 2 */
        d6 = (d6 << 2) | (d5 >> 15);
    }

    /* lsr.l #1,D6 — final shift */
    d6 >>= 1;

    return d6;
}

/* ── Track table addresses ────────────────────────────────────────────── */

typedef struct {
    const char *name;
    uint32_t rom_base;
    unsigned track_index;
} track_entry_t;

static const track_entry_t track_tables[] = {
    {"Track 0",  0x044630, 0},
    {"Track 1",  0x044930, 1},
    {"Track 2",  0x044C30, 2},
    {"Track 3",  0x044F30, 3},
    {"Track 4",  0x045530, 4},
    {"Track 5",  0x045230, 5},
    {"Track 6",  0x045530, 6},  /* Note: 4 and 6 share same table per analysis */
};
static const unsigned NUM_TRACKS = sizeof(track_tables) / sizeof(track_tables[0]);

/* ── Strip data structures ───────────────────────────────────────────── */

#define MAX_STRIPS 64

typedef struct {
    float x[4];
    float y[4];
    float z[4];
    float r, g, b;  /* colour */
} track_strip_t;

typedef struct {
    track_strip_t strips[MAX_STRIPS];
    unsigned count;
} track_data_t;

/* ── Global state for OpenGL ──────────────────────────────────────────── */

static track_data_t g_track;
static int g_track_loaded = 0;

/* ── Extract track from ROM ──────────────────────────────────────────── */

/* The road strip processing reads 16 words per strip from the ROM table.
 * Each word is expanded via road_expand_bits and split into:
 * - Colour: high nibble of high byte
 * - Geometry: remaining bits encode position data
 *
 * We decode this into 3D vertices. The Y coordinate increases down the screen
 * in the 2D view, but we treat it as Z (depth) in 3D.
 *
 * The expand_bits algorithm spreads each input bit across 2 output bits:
 *   input bit 0 -> output bits 0-1 (00 if bit=0, 11 if bit=1)
 *   input bit 1 -> output bits 2-3
 *   etc.
 *
 * The 32-bit result is: each pair of bits represents one input bit.
 * We use this to extract signed offsets for vertex positioning. */
static int extract_track(const uint8_t *rom, track_data_t *track,
                         uint32_t rom_base, unsigned strip_count)
{
    unsigned s;
    uint32_t addr = rom_base;

    track->count = 0;

    for (s = 0; s < strip_count && track->count < MAX_STRIPS; s++) {
        uint16_t raw_word = rom_word(rom, addr);
        uint32_t expanded = road_expand_bits(raw_word);
        addr += 2;

        /* Decode the expanded word:
         * Each 2-bit field in 'expanded' represents one bit of the original.
         * Bit 31 = input bit 15, Bit 30 = input bit 14, ... Bit 0 = input bit 0
         *
         * Upper 8 bits of 'expanded' (bits 31-24) = colour data (byte replicated)
         * The remaining bits encode geometry as signed offsets.
         *
         * Extract geometry: we use bits 23-16 as a signed offset for width.
         * The road is a trapezoid that narrows with distance. */
        int8_t width_offset = (int8_t)((expanded >> 16) & 0xFF);
        int8_t height_offset = (int8_t)((expanded >> 8) & 0xFF);

        /* Colour: upper 8 bits byte-replicated */
        uint8_t colour_byte = (uint8_t)(expanded >> 24);

        /* Base road shape: widens then narrows (perspective effect) */
        float base_width = 3.0f;
        float strip_z = (float)s * 0.5f;
        float perspective = 1.0f / (1.0f + strip_z * 0.15f);
        float width = base_width * perspective + (float)width_offset * 0.02f;
        if (width < 0.5f) width = 0.5f;

        float center_x = (float)height_offset * 0.01f;
        float elevation = (float)(raw_word & 0xFF) * 0.001f;  /* subtle height variation */

        track_strip_t *strip = &track->strips[track->count];

        /* Build a road strip as a trapezoid:
         * - 4 vertices: near-left, near-right, far-right, far-left
         * - The road gets narrower with distance (perspective)
         * - Centre can shift left/right based on track curves */

        float next_z = strip_z + 0.5f;
        float next_perspective = 1.0f / (1.0f + next_z * 0.15f);
        float next_width = base_width * next_perspective;
        float next_center = center_x;  /* for now, straight road */

        /* Near vertices (at this depth) */
        strip->x[0] = center_x - width;  /* near left */
        strip->y[0] = elevation;
        strip->z[0] = strip_z;

        strip->x[1] = center_x + width;  /* near right */
        strip->y[1] = elevation;
        strip->z[1] = strip_z;

        /* Far vertices (at next depth) */
        strip->x[2] = next_center + next_width;  /* far right */
        strip->y[2] = elevation;
        strip->z[2] = next_z;

        strip->x[3] = next_center - next_width;  /* far left */
        strip->y[3] = elevation;
        strip->z[3] = next_z;

        /* Colour from the ROM data - alternate for road marking effect */
        if ((s & 1) == 0) {
            /* Darker shade */
            strip->r = 0.15f + (colour_byte & 0x0F) / 255.0f;
            strip->g = 0.15f + (colour_byte & 0x0F) / 255.0f;
            strip->b = 0.2f + (colour_byte & 0x0F) / 200.0f;
        } else {
            /* Lighter shade (road edge marking) */
            strip->r = 0.25f + (colour_byte & 0x0F) / 200.0f;
            strip->g = 0.25f + (colour_byte & 0x0F) / 200.0f;
            strip->b = 0.3f + (colour_byte & 0x0F) / 180.0f;
        }

        track->count++;
    }

    return 1;
}

/* ── OpenGL rendering ────────────────────────────────────────────────── */

#ifdef USE_OPENGL

/* Camera parameters */
static float camera_distance = 15.0f;
static float camera_height = 5.0f;
static float camera_angle = 0.0f;
static float camera_elevation = 0.3f;  /* looking down angle */

static void setup_camera(void)
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(60.0, 800.0/600.0, 0.1, 100.0);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    float x = camera_distance * sinf(camera_angle);
    float z = camera_distance * cosf(camera_angle);
    float y = camera_height;

    gluLookAt(x, y, z,
              0.0, 0.0, 10.0,  /* look at point ahead */
              0.0, 1.0, 0.0);
}

static void render_track_strip(const track_strip_t *strip)
{
    glColor3f(strip->r, strip->g, strip->b);

    glBegin(GL_TRIANGLE_STRIP);
        glVertex3f(strip->x[0], strip->y[0], strip->z[0]);
        glVertex3f(strip->x[1], strip->y[1], strip->z[1]);
        glVertex3f(strip->x[3], strip->y[3], strip->z[3]);
        glVertex3f(strip->x[2], strip->y[2], strip->z[2]);
    glEnd();
}

static void render_track(void)
{
    unsigned i;
    for (i = 0; i < g_track.count; i++) {
        render_track_strip(&g_track.strips[i]);
    }
}

static void render_frame(void)
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glClearColor(0.05f, 0.05f, 0.1f, 1.0f);

    setup_camera();

    /* Draw ground grid */
    glColor3f(0.15f, 0.15f, 0.2f);
    glBegin(GL_LINES);
    {
        float i;
        for (i = -20.0f; i <= 20.0f; i += 2.0f) {
            glVertex3f(i, -0.5f, -5.0f);
            glVertex3f(i, -0.5f, 40.0f);
            glVertex3f(-20.0f, -0.5f, i * 2.0f - 5.0f);
            glVertex3f(20.0f, -0.5f, i * 2.0f - 5.0f);
        }
    }
    glEnd();

    /* Draw track */
    render_track();

    glutSwapBuffers();
}

static void keyboard_handler(unsigned char key, int x, int y)
{
    (void)x; (void)y;

    switch (key) {
        case 'a': camera_angle -= 0.1f; break;
        case 'd': camera_angle += 0.1f; break;
        case 'w': camera_distance -= 0.5f; if (camera_distance < 5.0f) camera_distance = 5.0f; break;
        case 's': camera_distance += 0.5f; if (camera_distance > 30.0f) camera_distance = 30.0f; break;
        case 'r': camera_height += 0.2f; break;
        case 'f': camera_height -= 0.2f; if (camera_height < 0.5f) camera_height = 0.5f; break;
        case 27:  /* ESC */ exit(0); break;
    }
    glutPostRedisplay();
}

static void init_opengl(int argc, char **argv)
{
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH);
    glutInitWindowSize(1024, 768);
    glutCreateWindow("S.T.U.N. Runner 3D Track Viewer");

    glEnable(GL_DEPTH_TEST);
    glShadeModel(GL_SMOOTH);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    glutDisplayFunc(render_frame);
    glutKeyboardFunc(keyboard_handler);

    glClearColor(0.0f, 0.0f, 0.1f, 1.0f);

    printf("\n3D Track Viewer Controls:\n");
    printf("  W/S - Zoom in/out\n");
    printf("  A/D - Rotate camera left/right\n");
    printf("  R/F - Raise/lower camera\n");
    printf("  ESC - Quit\n");
}

#endif /* USE_OPENGL */

/* ── main ────────────────────────────────────────────────────────────── */

static void print_usage(const char *prog)
{
    printf("Usage: %s <rom_path> [options]\n", prog);
    printf("\nOptions:\n");
    printf("  -t <n>    Select track (0-%u, default: 0)\n", NUM_TRACKS - 1);
    printf("  -l        List available tracks\n");
    printf("  -d <n>    Number of strips to decode (default: 32)\n");
    printf("  -o <file> Output track data to text file\n");
    printf("  -n        No OpenGL (text output only)\n");
    printf("  -h        Show this help\n");
    printf("\nTrack ROM bases (from static analysis):\n");
    unsigned i;
    for (i = 0; i < NUM_TRACKS; i++) {
        printf("  Track %u: 0x%06X (%s)\n", i,
               track_tables[i].rom_base, track_tables[i].name);
    }
}

int main(int argc, char **argv)
{
    const char *rom_path = NULL;
    unsigned track_index = 0;
    unsigned strip_count = 32;
    int list_tracks = 0;
    const char *output_file = NULL;
    int no_opengl = 0;

    /* Parse arguments */
    int i;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            track_index = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-l") == 0) {
            list_tracks = 1;
        } else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            strip_count = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_file = argv[++i];
        } else if (strcmp(argv[i], "-n") == 0) {
            no_opengl = 1;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (argv[i][0] != '-') {
            rom_path = argv[i];
        }
    }

    if (list_tracks) {
        printf("Available tracks:\n");
        unsigned i;
        for (i = 0; i < NUM_TRACKS; i++) {
            printf("  %u: %s (ROM base 0x%06X)\n",
                   i, track_tables[i].name, track_tables[i].rom_base);
        }
        return 0;
    }

    if (!rom_path) {
        fprintf(stderr, "Error: ROM path required\n");
        print_usage(argv[0]);
        return 1;
    }

    if (track_index >= NUM_TRACKS) {
        fprintf(stderr, "Error: track index out of range (0-%u)\n", NUM_TRACKS - 1);
        return 1;
    }

    /* Load ROM */
    size_t rom_size;
    uint8_t *rom = load_rom(rom_path, &rom_size);
    if (!rom) {
        return 1;
    }

    printf("Loaded ROM: %zu bytes\n", rom_size);

    /* Extract track data */
    uint32_t rom_base = track_tables[track_index].rom_base;

    printf("Extracting track %u (%s) from ROM base 0x%06X, %u strips\n",
           track_index, track_tables[track_index].name, rom_base, strip_count);

    if (!extract_track(rom, &g_track, rom_base, strip_count)) {
        fprintf(stderr, "Failed to extract track data\n");
        free(rom);
        return 1;
    }

    printf("Extracted %u track strips\n", g_track.count);
    g_track_loaded = 1;

    /* Output to file if requested */
    if (output_file) {
        FILE *f = fopen(output_file, "w");
        if (!f) {
            fprintf(stderr, "Cannot open output file: %s\n", output_file);
            free(rom);
            return 1;
        }
        fprintf(f, "Track: %s\n", track_tables[track_index].name);
        fprintf(f, "ROM base: 0x%06X\n", rom_base);
        fprintf(f, "Strips: %u\n", g_track.count);
        unsigned s;
        for (s = 0; s < g_track.count; s++) {
            const track_strip_t *strip = &g_track.strips[s];
            fprintf(f, "Strip %u: colour=(%.2f,%.2f,%.2f)\n", s,
                    strip->r, strip->g, strip->b);
            fprintf(f, "  v0=(%.2f,%.2f,%.2f) v1=(%.2f,%.2f,%.2f)\n",
                    strip->x[0], strip->y[0], strip->z[0],
                    strip->x[1], strip->y[1], strip->z[1]);
            fprintf(f, "  v2=(%.2f,%.2f,%.2f) v3=(%.2f,%.2f,%.2f)\n",
                    strip->x[2], strip->y[2], strip->z[2],
                    strip->x[3], strip->y[3], strip->z[3]);
        }
        fclose(f);
        printf("Track data written to %s\n", output_file);
    }

#ifdef USE_OPENGL
    if (!no_opengl) {
        init_opengl(argc, argv);
        glutMainLoop();
    } else {
        printf("\nOpenGL disabled. Track data extracted to %s\n",
               output_file ? output_file : "memory only");
    }
#else
    (void)no_opengl;
    printf("\nOpenGL not compiled in. Recompile with -DUSE_OPENGL\n");
#endif

    free(rom);
    return 0;
}
