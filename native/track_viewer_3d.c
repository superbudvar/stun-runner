/* 3D Track Viewer for S.T.U.N. Runner
 *
 * Extracts track geometry from ROM and renders it in 3D using OpenGL.
 * Uses the transcribed road_expand_bits algorithm to decode track data.
 *
 * Build:
 *   gcc -o track_viewer_3d track_viewer_3d.c -lGL -lGLU -lglut -lm
 *
 * Usage:
 *   ./track_viewer_3d <rom_path> [options]
 *   Options:
 *     -n        No OpenGL (text output only)
 *     -h        Show help
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

#define STUNRUN_ROM_MAX_SIZE (12 * 64 * 1024)

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

/* ── ROM helpers ────────────────────────────────────────────────────── */

static uint16_t rom_word(const uint8_t *rom, uint32_t address)
{
    return (uint16_t)(((uint16_t)rom[address] << 8) |
                      (uint16_t)rom[address + 1]);
}

/* ── road_expand_bits ─────────────────────────────────────────────── */

/* Bit expansion algorithm from ROM $234B6.
 * Rotate-left-through-carry for 16 iterations, then shift right by 1.
 * Each 16-bit word expands to 32 bits.
 *
 * Decoded format after expansion:
 *   bits 31..24 : colour/type code
 *   bits 23..16 : left edge position
 *   bits  7.. 0 : right edge position
 *
 * Colour codes:
 *   0x2a = ROAD (asphalt)
 *   0x22 = EDGE (barrier)
 *   0x20 = SHOULDER (rumble strip)
 *   0x08 = CENTER (centre line)
 *   0x02/0x0a/0x28 = TUNNEL variants
 *   0x00 = EMPTY
 */
static uint32_t road_expand_bits(uint16_t word)
{
    uint32_t d6 = 0;
    uint16_t d5 = word;
    unsigned i;

    for (i = 0; i < 16; i++) {
        d5 = (uint16_t)((d5 << 1) | (d6 >> 31));
        d6 = (d6 << 2) | (d5 >> 15);
    }
    d6 >>= 1;
    return d6;
}

/* ── Track table addresses ──────────────────────────────────────────── */

typedef struct {
    const char *name;
    uint32_t rom_base;
    unsigned track_index;
    /* Decoded stats */
    unsigned road_count;
    unsigned curve_count;
    unsigned tunnel_count;
} track_info_t;

static const track_info_t track_tables[] = {
    {"Track 0",  0x044630, 0, 0, 0, 0},
    {"Track 1",  0x044930, 1, 0, 0, 0},
    {"Track 2",  0x044C30, 2, 0, 0, 0},
    {"Track 3",  0x044F30, 3, 0, 0, 0},
    {"Track 4",  0x045230, 4, 0, 0, 0},
    {"Track 5",  0x045530, 5, 0, 0, 0},
};
static const unsigned NUM_TRACKS = sizeof(track_tables) / sizeof(track_tables[0]);

/* ── Strip data structures ─────────────────────────────────────────── */

#define MAX_STRIPS 384

typedef struct {
    uint8_t colour_code;    /* road_expand_bits upper byte */
    int left;              /* left edge position (bits 23-16) */
    int right;             /* right edge position (bits 7-0) */
    uint16_t raw_word;    /* original ROM word */
} road_word_t;

typedef struct {
    road_word_t *words;          /* dynamically allocated */
    unsigned count;
    unsigned road_count;
    unsigned curve_left_count;
    unsigned curve_right_count;
    unsigned tunnel_count;
} track_data_t;

/* ── Global state ─────────────────────────────────────────────────── */

static track_data_t *g_tracks = NULL;  /* allocated at runtime */
static unsigned g_current_track = 0;
static unsigned g_view_start = 0;     /* first strip to render */
static unsigned g_view_count = 32;    /* how many strips to render */
static const uint8_t *g_rom = NULL;

/* ── Colour names ──────────────────────────────────────────────────── */

static const char *colour_name(uint8_t code)
{
    switch (code) {
        case 0x2a: return "ROAD";
        case 0x22: return "EDGE";
        case 0x20: return "SHOULDER";
        case 0x08: return "CENTER";
        case 0x02: return "TUNNEL_A";
        case 0x0a: return "TUNNEL_B";
        case 0x28: return "TUNNEL_C";
        case 0x00: return "EMPTY";
        default:   return "UNKNOWN";
    }
}

static void colour_from_code(uint8_t code, float *r, float *g, float *b)
{
    switch (code) {
        case 0x2a: /* ROAD - asphalt grey */
            *r = 0.25f; *g = 0.25f; *b = 0.28f;
            break;
        case 0x22: /* EDGE - barrier red/white */
            *r = 0.7f; *g = 0.2f; *b = 0.2f;
            break;
        case 0x20: /* SHOULDER - rumble strip */
            *r = 0.5f; *g = 0.3f; *b = 0.1f;
            break;
        case 0x08: /* CENTER - centre line yellow */
            *r = 0.9f; *g = 0.85f; *b = 0.2f;
            break;
        case 0x02: /* TUNNEL_A */
        case 0x0a: /* TUNNEL_B */
        case 0x28: /* TUNNEL_C */
            *r = 0.4f; *g = 0.4f; *b = 0.45f;
            break;
        default:
            *r = 0.1f; *g = 0.1f; *b = 0.15f;
            break;
    }
}

/* ── Extract track from ROM ────────────────────────────────────────── */

static void extract_all_tracks(const uint8_t *rom)
{
    unsigned t, w;

    /* Allocate track arrays */
    g_tracks = calloc(NUM_TRACKS, sizeof(track_data_t));
    if (!g_tracks) {
        fprintf(stderr, "Failed to allocate track data\n");
        return;
    }

    for (t = 0; t < NUM_TRACKS; t++) {
        track_data_t *track = &g_tracks[t];
        uint32_t base = track_tables[t].rom_base;

        track->words = calloc(MAX_STRIPS, sizeof(road_word_t));
        if (!track->words) {
            fprintf(stderr, "Failed to allocate strip data for track %u\n", t);
            continue;
        }

        track->count = 0;
        track->road_count = 0;
        track->curve_left_count = 0;
        track->curve_right_count = 0;
        track->tunnel_count = 0;

        for (w = 0; w < MAX_STRIPS; w++) {
            uint16_t raw = rom_word(rom, base + w * 2);
            uint32_t expanded = road_expand_bits(raw);

            road_word_t *rw = &track->words[w];
            rw->raw_word = raw;
            rw->colour_code = (expanded >> 24) & 0xff;
            rw->left = (expanded >> 16) & 0xff;
            rw->right = expanded & 0xff;

            track->count++;

            /* Count feature types */
            switch (rw->colour_code) {
                case 0x2a:
                    track->road_count++;
                    /* Detect curve direction */
                    if (rw->left > 100) track->curve_right_count++;
                    else if (rw->right < 70) track->curve_left_count++;
                    break;
                case 0x02:
                case 0x0a:
                case 0x28:
                    track->tunnel_count++;
                    break;
            }
        }
    }
}

/* ── Print track info ───────────────────────────────────────────────── */

static void print_track_info(unsigned track_idx)
{
    if (!g_tracks) return;

    const track_info_t *info = &track_tables[track_idx];
    const track_data_t *track = &g_tracks[track_idx];

    printf("\n=== %s ===\n", info->name);
    printf("ROM base: 0x%06X\n", info->rom_base);
    printf("Total strips: %u\n", track->count);
    printf("Road segments: %u\n", track->road_count);
    printf("Curves: %u left, %u right\n",
           track->curve_left_count, track->curve_right_count);
    printf("Tunnels: %u\n", track->tunnel_count);
    printf("Viewing strips %u-%u\n",
           g_view_start, g_view_start + g_view_count - 1);
}

/* ── OpenGL rendering ─────────────────────────────────────────────── */

#ifdef USE_OPENGL

/* Camera parameters */
static float camera_distance = 20.0f;
static float camera_height = 8.0f;
static float camera_angle = 0.0f;
static int auto_rotate = 0;

static void setup_camera(void)
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(60.0, 800.0/600.0, 0.1, 200.0);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    float x = camera_distance * sinf(camera_angle);
    float z = camera_distance * cosf(camera_angle);
    float y = camera_height;

    float look_z = 15.0f + g_view_start * 0.3f;

    gluLookAt(x, y, z,
              0.0, 0.0, look_z,
              0.0, 1.0, 0.0);
}

static void render_strip(const road_word_t *rw, unsigned index)
{
    float r, g, b;
    colour_from_code(rw->colour_code, &r, &g, &b);

    /* Skip empty strips */
    if (rw->colour_code == 0x00) {
        /* Draw dark ground for empty areas */
        r = 0.08f; g = 0.08f; b = 0.1f;
    }

    /* Calculate 3D position
     * The road is viewed from above/side, Z is depth
     * X is horizontal position, Y is height
     *
     * Scale the left/right positions from ROM (typically 0-170 range)
     * to world coordinates. Center is around 85. */
    float strip_z = (float)index * 0.4f;
    float strip_depth = 0.4f;

    /* Convert ROM positions to world coords
     * ROM range 0-170 maps to world -3 to +3 */
    float left_world = ((float)rw->left - 85.0f) * 0.05f;
    float right_world = ((float)rw->right - 85.0f) * 0.05f;

    /* Handle edge cases where left > right (indicates curve) */
    if (rw->left > rw->right && rw->right > 10) {
        /* Normal case - road is well defined */
    } else if (rw->left > 100) {
        /* Right curve - road shifts left */
        right_world = left_world - 1.5f;
    } else if (rw->right < 70 && rw->left > 10) {
        /* Left curve - road shifts right */
        left_world = right_world + 1.5f;
    }

    /* Clamp to reasonable values */
    if (left_world < -5.0f) left_world = -5.0f;
    if (right_world > 5.0f) right_world = 5.0f;
    if (right_world < -5.0f) right_world = -5.0f;
    if (left_world > 5.0f) left_world = 5.0f;

    float next_z = strip_z + strip_depth;

    glColor3f(r, g, b);
    glBegin(GL_TRIANGLE_STRIP);
        /* Near vertices */
        glVertex3f(left_world, 0.0f, strip_z);
        glVertex3f(right_world, 0.0f, strip_z);
        /* Far vertices */
        glVertex3f(left_world, 0.0f, next_z);
        glVertex3f(right_world, 0.0f, next_z);
    glEnd();

    /* Draw edge markings for road surfaces */
    if (rw->colour_code == 0x2a) {
        glColor3f(0.7f, 0.7f, 0.7f);
        glLineWidth(2.0f);
        glBegin(GL_LINES);
            /* Left edge */
            glVertex3f(left_world, 0.05f, strip_z);
            glVertex3f(left_world, 0.05f, next_z);
            /* Right edge */
            glVertex3f(right_world, 0.05f, strip_z);
            glVertex3f(right_world, 0.05f, next_z);
        glEnd();
        glLineWidth(1.0f);
    }
}

static void render_track(void)
{
    if (!g_tracks) return;

    const track_data_t *track = &g_tracks[g_current_track];
    unsigned i;

    for (i = g_view_start;
         i < g_view_start + g_view_count && i < track->count;
         i++) {
        render_strip(&track->words[i], i - g_view_start);
    }
}

static void render_overlay_text(void)
{
    /* Simple text overlay using GLUT - draw in 2D orthographic mode */
    char buf[128];

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, 800, 0, 600, -1, 1);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glColor3f(1.0f, 1.0f, 0.0f);
    glRasterPos2i(10, 580);

    /* Track name */
    sprintf(buf, "%s (press < > to switch, [ ] for strips)",
            track_tables[g_current_track].name);
    // Would need stroke font or bitmap font - skip for now

    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
}

static void render_frame(void)
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glClearColor(0.02f, 0.02f, 0.05f, 1.0f);

    setup_camera();

    /* Draw ground plane */
    glColor3f(0.1f, 0.1f, 0.12f);
    glBegin(GL_QUADS);
        glVertex3f(-10.0f, -0.1f, -5.0f);
        glVertex3f( 10.0f, -0.1f, -5.0f);
        glVertex3f( 10.0f, -0.1f, 50.0f);
        glVertex3f(-10.0f, -0.1f, 50.0f);
    glEnd();

    /* Draw track */
    render_track();

    /* Auto-rotate camera */
    if (auto_rotate) {
        camera_angle += 0.005f;
        if (camera_angle > 2.0f * 3.14159f) camera_angle -= 2.0f * 3.14159f;
    }

    glutSwapBuffers();
}

static void keyboard_handler(unsigned char key, int x, int y)
{
    (void)x; (void)y;

    if (!g_tracks) return;

    unsigned track_count = g_tracks[g_current_track].count;

    switch (key) {
        /* Camera controls */
        case 'a': camera_angle -= 0.15f; break;
        case 'd': camera_angle += 0.15f; break;
        case 'w': camera_distance -= 1.0f; if (camera_distance < 5.0f) camera_distance = 5.0f; break;
        case 's': camera_distance += 1.0f; if (camera_distance > 50.0f) camera_distance = 50.0f; break;
        case 'r': camera_height += 0.5f; break;
        case 'f': camera_height -= 0.5f; if (camera_height < 1.0f) camera_height = 1.0f; break;

        /* Track switching */
        case ',':
        case '<':
            if (g_current_track > 0) {
                g_current_track--;
                g_view_start = 0;
                print_track_info(g_current_track);
            }
            break;
        case '.':
        case '>':
            if (g_current_track < NUM_TRACKS - 1) {
                g_current_track++;
                g_view_start = 0;
                print_track_info(g_current_track);
            }
            break;

        /* Strip navigation */
        case '[':
            if (g_view_start >= 10) g_view_start -= 10;
            else g_view_start = 0;
            print_track_info(g_current_track);
            break;
        case ']':
            if (g_view_start + g_view_count + 10 < track_count)
                g_view_start += 10;
            print_track_info(g_current_track);
            break;

        /* View controls */
        case 'v':
            g_view_count = (g_view_count == 32) ? 64 : 32;
            if (g_view_start + g_view_count > track_count)
                g_view_start = (track_count > g_view_count) ? track_count - g_view_count : 0;
            print_track_info(g_current_track);
            break;

        /* Auto-rotate */
        case 'o':
            auto_rotate = !auto_rotate;
            printf("Auto-rotate: %s\n", auto_rotate ? "ON" : "OFF");
            break;

        /* Number keys for quick track select */
        case '0'...'9': {
            unsigned n = key - '0';
            if (n < NUM_TRACKS) {
                g_current_track = n;
                g_view_start = 0;
                print_track_info(g_current_track);
            }
            break;
        }

        case 27:  /* ESC */
            printf("Exiting...\n");
            exit(0);
            break;
    }

    printf("Controls: < > switch track  [ ] scroll strips  0-9 quick select\n");
    printf("          W/S zoom  A/D rotate  R/F height  O auto-rotate  V view size\n");

    glutPostRedisplay();
}

static void init_opengl(int argc, char **argv)
{
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH);
    glutInitWindowSize(1024, 768);
    glutCreateWindow("S.T.U.N. Runner 3D Track Viewer");

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    glClearColor(0.02f, 0.02f, 0.05f, 1.0f);

    glutDisplayFunc(render_frame);
    glutKeyboardFunc(keyboard_handler);

    printf("\n========================================\n");
    printf("S.T.U.N. Runner 3D Track Viewer\n");
    printf("========================================\n");
    print_track_info(g_current_track);
    printf("\nControls:\n");
    printf("  < / >     Switch between tracks\n");
    printf("  [ / ]     Scroll through strips\n");
    printf("  0-9       Quick track select\n");
    printf("  W / S     Zoom in / out\n");
    printf("  A / D     Rotate camera left / right\n");
    printf("  R / F     Raise / lower camera\n");
    printf("  O         Toggle auto-rotate\n");
    printf("  V         Toggle strip view count (32/64)\n");
    printf("  ESC       Quit\n");
    printf("========================================\n\n");
}

#endif /* USE_OPENGL */

/* ── Text-only output ─────────────────────────────────────────────── */

static void print_text_track(void)
{
    if (!g_tracks) return;

    const track_info_t *info = &track_tables[g_current_track];
    const track_data_t *track = &g_tracks[g_current_track];

    printf("\n");
    printf("+======================================================+\n");
    printf("| %-48s |\n", info->name);
    printf("+======================================================+\n");
    printf("| ROM base: 0x%06X                                 |\n", info->rom_base);
    printf("| Strips: %u                                          |\n", track->count);
    printf("| Road: %u | Curves: %u (%uL/%uR) | Tunnels: %u          |\n",
           track->road_count,
           track->curve_left_count + track->curve_right_count,
           track->curve_left_count,
           track->curve_right_count,
           track->tunnel_count);
    printf("+======================================================+\n");
    printf("| Showing strips %u-%u                               |\n",
           g_view_start, g_view_start + g_view_count - 1);
    printf("+======================================================+\n");
    printf("|  Idx  Raw    Exp        Code  L    R    Type       |\n");
    printf("+------+-------+-----------+-----+-----+------------+\n");

    unsigned i;
    for (i = g_view_start;
         i < g_view_start + g_view_count && i < track->count;
         i++) {
        const road_word_t *rw = &track->words[i];
        uint32_t exp = road_expand_bits(rw->raw_word);
        printf("| %4u  0x%04X 0x%08X  0x%02X  %3d  %3d  %-9s |\n",
               i, rw->raw_word, exp,
               rw->colour_code, rw->left, rw->right,
               colour_name(rw->colour_code));
    }
    printf("+======================================================+\n");
    printf("\nUse < > to switch tracks, [ ] to scroll\n");
}

/* ── main ─────────────────────────────────────────────────────────── */

static void print_usage(const char *prog)
{
    printf("Usage: %s <rom_path> [options]\n", prog);
    printf("\nOptions:\n");
    printf("  -t <n>    Select initial track (0-%u, default: 0)\n", NUM_TRACKS - 1);
    printf("  -s <n>    Starting strip (default: 0)\n");
    printf("  -c <n>    Strip count to display (default: 32)\n");
    printf("  -n        No OpenGL (text output only)\n");
    printf("  -h        Show this help\n");
    printf("\nTracks:\n");
    unsigned i;
    for (i = 0; i < NUM_TRACKS; i++) {
        printf("  %u: %s (ROM base 0x%06X)\n",
               i, track_tables[i].name, track_tables[i].rom_base);
    }
}

int main(int argc, char **argv)
{
    const char *rom_path = NULL;
    int no_opengl = 0;
    unsigned start_track = 0;

    /* Parse arguments */
    int i;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            start_track = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            g_view_start = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            g_view_count = atoi(argv[++i]);
            if (g_view_count > MAX_STRIPS) g_view_count = MAX_STRIPS;
        } else if (strcmp(argv[i], "-n") == 0) {
            no_opengl = 1;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (argv[i][0] != '-') {
            rom_path = argv[i];
        }
    }

    if (!rom_path) {
        fprintf(stderr, "Error: ROM path required\n");
        print_usage(argv[0]);
        return 1;
    }

    if (start_track >= NUM_TRACKS) {
        fprintf(stderr, "Error: track index out of range (0-%u)\n", NUM_TRACKS - 1);
        return 1;
    }

    /* Load ROM */
    size_t rom_size;
    g_rom = load_rom(rom_path, &rom_size);
    if (!g_rom) {
        return 1;
    }

    printf("Loaded ROM: %zu bytes\n", rom_size);

    /* Extract all tracks */
    extract_all_tracks(g_rom);
    g_current_track = start_track;

    /* Show current track info */
    print_track_info(g_current_track);

#ifdef USE_OPENGL
    if (!no_opengl) {
        init_opengl(argc, argv);
        glutMainLoop();
    } else {
        print_text_track();
    }
#else
    (void)no_opengl;
    printf("\nOpenGL not compiled in. Use -n for text output.\n");
#endif

    /* Cleanup */
    if (g_tracks) {
        unsigned t;
        for (t = 0; t < NUM_TRACKS; t++) {
            free(g_tracks[t].words);
        }
        free(g_tracks);
    }
    free((void*)g_rom);
    return 0;
}