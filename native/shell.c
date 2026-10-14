/* POSIX declarations used by the standalone C99 test build. */
#define _POSIX_C_SOURCE 200809L

/* Deterministic native shell: M4 replay scaffolding.
 *
 * Agent 2 (Implementer). Two modes:
 *
 * - compiled-in (no argv): the fixed 600-frame title-path walk over the
 *   five verified C slices at their evidence-anchored frames.
 * - file mode (one argv: path to an arcade-experiment/v1 JSON file): the
 *   walk length comes from expect.frame and the file's input events are
 *   dispatched at their frames as telemetry. Only the "frame" expect kind
 *   runs; "bounded_observation" is refused with a clear message.
 *
 * Inputs have no consumer model yet (the shell owns no game state by
 * design), so dispatch logging IS the replay plumbing being proven here:
 * file -> strict parse -> deterministic frame dispatch. Whole-run
 * contracts (control tally, title counts) are 600-frame title-path facts
 * and assert only when the terminal frame is 600; shorter/longer runs log
 * them as skipped instead of failing on unestablished ground.
 *
 * Anchors (see the slice headers for full provenance):
 * - frame 1:   6502 startup response 0xFF at $2A02 (sound-boundary-tap)
 * - frames 1/369/411: 6502 command reads at $280A returning 0x00
 *   (telemetry only: empty-latch vs zero-command is UNKNOWN)
 * - frames 407/408/411: ADSP upload EMPTY/POPULATING/COMPLETE
 *   (m3-adsp-upload-boundary)
 * - frame 412: replacement install-ready; init-state image words and DM
 *   landmarks hold (m3-adsp-reset-entry, ram-map)
 * - frame 447: 68010 command byte 0x1E through the JSA latch to the 6502
 *   read (sound-boundary-tap)
 * - frame 600 of a 600-frame run: 73-write control multiset matches;
 *   title counts match (adsp-control-window, sound-boundary-tap)
 * - unpinned: second IRQ4 response walkthrough (2 entries observed, exact
 *   frames not established)
 *
 * Exit 0 with "RESULT PASS" iff every assertion holds; exit 2 on
 * usage/parse/unsupported-terminal failures. Output is fully
 * deterministic: same bytes on every run (no addresses, no timing).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

/* Portable recursive mkdir (no fork, no shell). */
static void mkpath(const char *path, mode_t mode)
{
    char tmp[512];
    size_t len = strlen(path);
    if (len >= sizeof(tmp)) len = sizeof(tmp) - 1;
    memcpy(tmp, path, len + 1);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            (void)mkdir(tmp, mode);
            *p = '/';
        }
    }
    (void)mkdir(tmp, mode);
}

/* SDL2 for window + OpenGL context (video backend) and audio device. */
#include <SDL2/SDL.h>

/* MC68010 emulator (Musashi).  When STUNRUN_MC68010=1, the shell runs
 * the real MC68010 ROM instead of the C slice game_loop_step. */
#include "musashi_cpu.h"
#include "cpu_override.h"

/* ---- Backend integration ---- */
/* When STUNRUN_BACKEND is set, the shell runs with live video (+ optional
 * audio) output via the registered backend.  Value is the backend name, e.g.
 *   STUNRUN_BACKEND=sdl_opengl   (Linux/macOS/Windows/RPi/Android/Web)
 *   STUNRUN_BACKEND=vulkan       (Linux/Windows/Android)
 *   STUNRUN_BACKEND=metal        (macOS/iOS)
 *   STUNRUN_BACKEND=null         (headless / CI)
 * Empty or unset: runs in original "scaffold-only" mode (PPM output).
 *
 * STUNRUN_AUDIO_BACKEND selects the audio backend separately:
 *   STUNRUN_AUDIO_BACKEND=sdl_audio
 *   STUNRUN_AUDIO_BACKEND=null
 */
#include "backend/backend.h"

#include "adsp_control_seq.h"
#include "adsp_init_image.h"
#include "adsp_upload_stream.h"
#include "fifo_block.h"
#include "geom_upload.h"
#include "road_fifo.h"
#include "experiment.h"
#include "jsa_latch.h"
#include "sound_dispatch.h"
#include "checkpoint.h"
#include "render.h"
#include "gsp_display.h"
#include "gsp_video.h"
/*** GSP RTL driver: only available when Verilator was found at configure time. ***/
#ifdef STUNRUN_GSP_RTL
#include "gsp_rtl_driver.h"
/* The driver needs a gsp_board_t to carry VRAM between the MC68010 and the
 * core. The shell does not use one otherwise, so it keeps a private one: the
 * driver only ever touches s_board->vram, and the scanout reads the core's
 * output back out through sync_to_musashi, so this board is a transport buffer
 * and not a second model. It is 512 KiB, so it is static, not on the stack. */
static gsp_board_t g_gsp_rtl_board;
static int g_gsp_rtl_ready = 0;

/* Bind the core to that board and hand it a zeroed share. Must run before the
 * first GSP_RTL_SYNC_AND_STEP: the driver silently returns from every entry
 * point until it is initialised, so a missing call here shows up as a live
 * window showing only the 68010's upload and nothing else. */
static void gsp_rtl_ensure_init(void)
{
    if (g_gsp_rtl_ready) return;
    gsp_board_reset(&g_gsp_rtl_board);
    gsp_rtl_driver_init(&g_gsp_rtl_board, NULL, 0);
    g_gsp_rtl_ready = 1;
    printf("shell: gsp-rtl=bound (Verilated TMS34010 core; scanout reads the "
           "core's own output)\n");
}

#ifdef STUNRUN_GSP_RTL
/* Forward the MC68010's host-window writes into the core.
 *
 * This is the link that actually starts the machine. The 68010's HSTADR/HSTDAT
 * writes reach native/musashi_cpu.c, which stores them into its own share --
 * but the CORE has its own HSTADR/HSTDATA/HSTCTL registers and its own HLT line,
 * and nothing told it. Without this the core sits in reset forever
 * (`halted=1`), which presents as a live window showing only the upload and
 * never a pixel from the GSP. The release is the 68010 clearing HSTCTL bit 15,
 * so that write in particular has to arrive.
 *
 * The driver takes the decoded register index directly, so this forwards the
 * bus event's data without re-deriving the register: re-decoding here is how
 * the HSTADRL/HSTADRH pair got transposed once already (findings section 13). */
static unsigned long g_gsp_rtl_obs_seen, g_gsp_rtl_obs_fwd, g_gsp_rtl_obs_noack;
static unsigned long g_gsp_rtl_obs_hstdat, g_gsp_rtl_obs_hstdat_nz, g_gsp_rtl_obs_hstadr_nz;
static unsigned long g_gsp_rtl_obs_hstctl_nz, g_gsp_rtl_obs_hstctl_rel;
static uint32_t g_gsp_rtl_obs_last_adr = 0;
static void gsp_rtl_bus_observer(void *ctx,
                                 const musashi_gsp_bus_event_t *ev)
{
    (void)ctx;
    if (!g_gsp_rtl_ready) return;
    g_gsp_rtl_obs_seen++;
    if (ev->kind != MUSASHI_GSP_BUS_HOST_WINDOW) return;
    /* ev->address is the absolute 68010 address in the 0xC00000 host window;
     * reg is the same (byte_offset >> 2) ^ 1 decode the board model uses. */
    {
        unsigned r0 = gsp_board_host_reg(ev->address - 0xC00000u);
        if (r0 == GSP_BOARD_HSTDAT) {
            g_gsp_rtl_obs_hstdat++;
            if (ev->data) g_gsp_rtl_obs_hstdat_nz++;
        } else if (r0 == GSP_BOARD_HSTADRL || r0 == GSP_BOARD_HSTADRH) {
            if (ev->data) g_gsp_rtl_obs_hstadr_nz++;
        } else if (r0 == GSP_BOARD_HSTCTL) {
            if (ev->data) g_gsp_rtl_obs_hstctl_nz++;
            if (!(ev->data & 0x8000u)) g_gsp_rtl_obs_hstctl_rel++;
        }
        g_gsp_rtl_obs_last_adr = ev->address;
        (void)g_gsp_rtl_obs_last_adr;
        int rc = gsp_rtl_driver_host_write(r0, ev->data);
        if (rc == 0) g_gsp_rtl_obs_fwd++;
        else if (rc == -2) g_gsp_rtl_obs_noack++;
    }
}
#endif

#define GSP_RTL_SYNC_AND_STEP(mem) do { \
    gsp_rtl_ensure_init(); \
    if ((mem).vram) { \
        gsp_rtl_driver_sync_and_step((mem).vram, MUSASHI_GSP_VRAM_WORDS); \
        gsp_rtl_driver_sync_to_musashi((uint16_t *)(mem).vram, \
                                       MUSASHI_GSP_VRAM_WORDS); \
        gsp_rtl_driver_sync_palette_to_musashi((uint16_t *)(mem).pal_lo, \
                                               (uint16_t *)(mem).pal_hi); \
    } \
} while (0)
#else
#define GSP_RTL_SYNC_AND_STEP(mem) do { } while (0)
#endif
#include "text_cursor.h"
#include "text_record.h"
#include "fake_ports.h"
#include "game_loop.h"
#include "road_strip.h"
#ifdef STUNRUN_GSP_GL_SCANOUT
#include "gsp_scanout_gl.h"
#endif

static int g_failures = 0;
static unsigned frame = 0;
static unsigned g_terminal = 600u;
static bool g_run_until_quit = false;

/* Monotonic nanosecond clock for frame pacing.  CLOCK_MONOTONIC so a wall
 * clock adjustment cannot stall or rush the loop. */
static uint64_t now_ns(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0u;
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static const stunrun_exp_event_t *g_events = NULL;
static unsigned g_event_count = 0;
static unsigned g_dispatched = 0;
static unsigned g_pending = 0;

/* Live backends (activated when STUNRUN_VIDEO_BACKEND / STUNRUN_AUDIO_BACKEND are set) */
static stunrun_backend_pair_t g_backend_pair;
static bool g_backend_active = false;
static SDL_Window *g_window = NULL;  /* owned by this module; destroyed on shutdown */
#ifdef STUNRUN_GSP_GL_SCANOUT
static SDL_Window *g_gl_scanout_window = NULL;
static stunrun_gsp_gl_scanout_t *g_gl_scanout = NULL;
#endif

/* JSA II audio dispatcher (OKI6295 ADPCM sample playback). */
static sound_dispatch_t g_sound_dispatch;
static bool g_sound_dispatch_active = false;

/* MC68010 instance (Musashi).  Activated when STUNRUN_MC68010=1.
 * When active, musashi_cpu_run() drives game state instead of game_loop_step(). */
static musashi_cpu_t *g_cpu = NULL;
static bool g_mc68010_active = false;

/* STUNRUN_LIVE_VIDEO=1: rasterise and present the GSP share inside the frame
 * loop, every STUNRUN_LIVE_VIDEO_STRIDE frames, so a running shell shows its
 * own 68010's work instead of a single post-mortem still.  Requires
 * STUNRUN_MC68010=1; without a live CPU there is nothing to scan out. */
static bool g_live_video = false;
static unsigned g_live_video_stride = 1u;
static unsigned g_live_video_presented = 0;
/* STUNRUN_LIVE_VIDEO_FPS caps how fast the loop runs so the window is
 * watchable.  0 = uncapped, which is what CI and headless runs want.  The
 * 68010 emulation of a frame is far faster than real time, so without this the
 * whole 600 frames flash past in well under a second. */
static unsigned g_live_video_fps = 0u;
static uint64_t g_live_video_start_ns = 0u;

/* The software framebuffer every render path draws into.  File scope, not
 * local to run_walk(), because the live-video helpers below need to reach it
 * from inside the frame loop. */
static stunrun_renderer_t renderer;

/* The confirmed frame-600 display is also retained as indexed pixels for the
 * video backend.  The software renderer remains the source for screenshots
 * and hashes; this second buffer lets an OpenGL backend consume the board's
 * indexed VRAM/palette state through video.h without inventing GSP geometry. */
static uint8_t g_gsp_indexed_pixels[STUNRUN_RENDER_WIDTH * STUNRUN_RENDER_HEIGHT];
/* Keep all four palette banks because gsp_display_build_palette() deliberately
 * decodes the complete 1024-entry board palette.  The indexed backend below
 * consumes the first 256 entries (the confirmed frame-600 bank). */
static uint8_t g_gsp_palette_rgb[STUNRUN_GSP_PALETTE_ENTRIES * 3u];
static bool g_gsp_indexed_backend_frame = false;

/* Screenshot capture: STUNRUN_CAPTURE_FRAME=N captures PNG at frame N.
 * STUNRUN_CAPTURE_ALL=1 captures every frame. STUNRUN_CAPTURE_DIR sets
 * the output directory (default: "capture/"). PNG over PPM via stb_image_write.h. */
static unsigned g_capture_frame = UINT32_MAX;  /* UINT32_MAX = disabled */
static bool g_capture_all = false;
static char g_capture_dir[256] = "capture";
static char g_dump_dir[256] = {0};
static char g_trace_window[64] = {0};

/* musashi_cpu_trace() and musashi_cpu_flow_log_*() are debugging aids declared
 * in musashi_cpu.c rather than the header, so the shell forward-declares them. */
extern void musashi_cpu_trace(musashi_cpu_t *cpu, unsigned count);
extern void musashi_cpu_trace_window(musashi_cpu_t *cpu, unsigned count,
                                     uint32_t lo, uint32_t hi);
extern void musashi_cpu_flow_log_enable(int enabled);
extern size_t musashi_cpu_flow_log_write(const char *path);
extern size_t musashi_cpu_disasm_range(const musashi_cpu_t *cpu,
                                       uint32_t start, uint32_t end,
                                       FILE *out);

/* STUNRUN_DISASM=<start:end>:<path> — static disassembly of a ROM range.
 * Written at shutdown (so it reads the same memory map the CPU saw) but it
 * never executes anything, so the addresses can be anywhere in the 68010's
 * 24-bit window. */
static unsigned long g_disasm_start, g_disasm_end;
static char g_disasm_path[512] = { 0 };

/* Where STUNRUN_FLOW_LOG asks for the control-flow inventory. */
static char g_flow_log_path[512] = { 0 };
static char g_pc_hist_path[512] = { 0 };

/* IRQ5 period in 68010 cycles: 244.14 Hz at an 8 MHz CPU = 32 768 cycles. */
#define STUNRUN_IRQ5_PERIOD 32768u

/* Cycles the 68010 gets per displayed frame.  Derived from the raster and the
 * master clock in musashi_cpu.h, which owns the number so the MC68010 text log
 * can stamp a frame column from the same value.  The arithmetic that produced
 * it is kept here: the multisync screen is
 * set_raw(5000000*2, 317*2, 0, 256*2, 262, 0, 228) (harddriv.cpp:1864-1869),
 * i.e. a 10 MHz pixel clock over 634x262 = 60.20180 Hz, while the CPU runs at
 * HARDDRIV_MASTER_CLOCK/4 = 8 MHz (harddriv.cpp:1463).  8e6 / 60.20180 = 132 886
 * cycles per frame, so just over four IRQ5 periods elapse per frame. */
#define STUNRUN_FRAME_CYCLES MUSASHI_FRAME_CYCLES

/* CPU cycles in one raster line.  634 dots at 10 MHz, CPU at 8 MHz:
 * 634 * 8 / 10 = 507.2.  The frame loop runs in slices this long so the
 * emulated raster advances while the CPU executes; see the comment at the
 * slice loop for the vblank handshake at 0x13f2 that depends on it. */
#define STUNRUN_LINE_CYCLES 507u

/* Boot-trace length, set from STUNRUN_TRACE.  0 disables tracing. */
static unsigned g_trace_count = 0;
static FILE *g_pclog_fp = NULL;

/* STUNRUN_PCLOG=<path>: one "frame pc cycles" line per frame.  Comparing this
 * against the same series from MAME answers "behind or stuck" in one look,
 * which no single end-of-run PC can do. */
static void pclog_open(void)
{
    const char *path = getenv("STUNRUN_PCLOG");
    if (path && path[0])
        g_pclog_fp = fopen(path, "w");
}

/* Generic replay boundary only: this is not game state. */
typedef struct shell_input_latch {
    char port[STUNRUN_EXP_STR_LEN];
    char field[STUNRUN_EXP_STR_LEN];
    int value;
} shell_input_latch_t;

static shell_input_latch_t g_input[STUNRUN_EXP_MAX_EVENTS];
static unsigned g_input_count = 0;
static uint32_t g_input_hash = 2166136261u;

static void input_hash_bytes(const void *data, size_t length)
{
    const unsigned char *bytes = (const unsigned char *)data;
    size_t i;
    for (i = 0; i < length; i++) {
        g_input_hash ^= bytes[i];
        g_input_hash *= 16777619u;
    }
}

static unsigned input_latch_index(const stunrun_exp_event_t *event)
{
    unsigned i;
    for (i = 0; i < g_input_count; i++)
        if (strcmp(g_input[i].port, event->port) == 0 &&
            strcmp(g_input[i].field, event->field) == 0)
            return i;
    if (g_input_count >= STUNRUN_EXP_MAX_EVENTS)
        return STUNRUN_EXP_MAX_EVENTS;
    snprintf(g_input[g_input_count].port, STUNRUN_EXP_STR_LEN, "%s",
             event->port);
    snprintf(g_input[g_input_count].field, STUNRUN_EXP_STR_LEN, "%s",
             event->field);
    g_input[g_input_count].value = 0;
    return g_input_count++;
}

static void input_latch_apply(const stunrun_exp_event_t *event)
{
    unsigned index = input_latch_index(event);
    unsigned i;
    if (index >= STUNRUN_EXP_MAX_EVENTS)
        return;
    g_input[index].value = event->action == STUNRUN_EXP_PRESS ? 1 :
                           event->action == STUNRUN_EXP_RELEASE ? 0 :
                           event->value;
    g_input_hash = 2166136261u;
    for (i = 0; i < g_input_count; i++) {
        input_hash_bytes(g_input[i].port, strlen(g_input[i].port) + 1u);
        input_hash_bytes(g_input[i].field, strlen(g_input[i].field) + 1u);
        input_hash_bytes(&g_input[i].value, sizeof(g_input[i].value));
    }
}

static unsigned input_active_count(void)
{
    unsigned i, active = 0;
    for (i = 0; i < g_input_count; i++)
        if (g_input[i].value != 0)
            active++;
    return active;
}

/* Optional evidence fixture only: the captured 68010 table bytes are
 * big-endian words.  No ROM table selection or course semantics are modeled
 * here; the literal upload slice owns only the 384-word march and twin copy. */
static int load_geometry_table(const char *path, uint16_t *table)
{
    unsigned char bytes[STUNRUN_GEOM_MARCH_WORDS * 2u];
    FILE *file;
    size_t i;

    if (path == NULL || table == NULL)
        return 0;
    file = fopen(path, "rb");
    if (file == NULL)
        return 0;
    if (fread(bytes, 1u, sizeof(bytes), file) != sizeof(bytes) ||
        fgetc(file) != EOF) {
        fclose(file);
        return 0;
    }
    fclose(file);
    for (i = 0; i < STUNRUN_GEOM_MARCH_WORDS; i++)
        table[i] = (uint16_t)(((uint16_t)bytes[i * 2u] << 8) |
                              bytes[i * 2u + 1u]);
    return 1;
}

/* Load a little-endian word fixture with an exact bounded capacity. */
static int load_word_fixture(const char *path, uint16_t *words,
                             size_t capacity, size_t *word_count)
{
    FILE *file;
    long bytes;
    size_t i;
    unsigned char raw[2];

    if (path == NULL || words == NULL || word_count == NULL)
        return 0;
    file = fopen(path, "rb");
    if (file == NULL || fseek(file, 0, SEEK_END) != 0)
        goto fail;
    bytes = ftell(file);
    if (bytes <= 0 || (bytes & 1) != 0 ||
        (size_t)bytes / 2u > capacity || fseek(file, 0, SEEK_SET) != 0)
        goto fail;
    for (i = 0; i < (size_t)bytes / 2u; i++) {
        if (fread(raw, 1u, sizeof(raw), file) != sizeof(raw))
            goto fail;
        words[i] = (uint16_t)raw[0] | ((uint16_t)raw[1] << 8);
    }
    if (fgetc(file) != EOF)
        goto fail;
    fclose(file);
    *word_count = (size_t)bytes / 2u;
    return 1;

fail:
    if (file != NULL)
        fclose(file);
    return 0;
}

static int parse_fixture_u32(const char *name, uint32_t *value)
{
    const char *text = getenv(name);
    char *end = NULL;
    unsigned long parsed;

    if (text == NULL || text[0] == '\0' || value == NULL)
        return 0;
    parsed = strtoul(text, &end, 0);
    if (end == text || *end != '\0' || parsed > UINT32_MAX)
        return 0;
    *value = (uint32_t)parsed;
    return 1;
}

static int parse_fixture_bases(const char *text, uint32_t *bases,
                               size_t count)
{
    size_t index = 0u;
    const char *cursor = text;
    char *end;
    unsigned long value;

    if (text == NULL || bases == NULL || count == 0u)
        return 0;
    while (*cursor != '\0' && index < count) {
        while (*cursor == ' ' || *cursor == '\t' || *cursor == ',')
            cursor++;
        if (*cursor == '\0')
            break;
        value = strtoul(cursor, &end, 0);
        if (end == cursor || value > UINT32_MAX)
            return 0;
        bases[index++] = (uint32_t)value;
        cursor = end;
        while (*cursor == ' ' || *cursor == '\t')
            cursor++;
        if (*cursor != '\0' && *cursor != ',')
            return 0;
    }
    return index == count && *cursor == '\0';
}

/* Read an entire file into a fresh buffer. Returns 0 on failure. */
static int read_whole_file(const char *path, uint8_t **out, size_t *out_len)
{
    FILE *f;
    long size;
    uint8_t *buf;

    *out = NULL;
    *out_len = 0;
    if (path == NULL || path[0] == '\0') {
        return 0;
    }
    f = fopen(path, "rb");
    if (f == NULL) {
        return 0;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) <= 0 ||
        fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return 0;
    }
    buf = (uint8_t *)malloc((size_t)size);
    if (buf == NULL) {
        fclose(f);
        return 0;
    }
    if (fread(buf, 1u, (size_t)size, f) != (size_t)size) {
        free(buf);
        fclose(f);
        return 0;
    }
    fclose(f);
    *out = buf;
    *out_len = (size_t)size;
    return 1;
}

/* The two palette planes are `pens` consecutive little-endian 16-bit entries
 * each -- not two interleaved halves of one array. Reading them interleaved
 * yields 1024 plausible-looking but wrong colours. */
static int read_palette_planes(const char *low_path, const char *high_path,
                               uint16_t *lo, uint16_t *hi)
{
    uint8_t *lo_bytes = NULL, *hi_bytes = NULL;
    size_t lo_len = 0, hi_len = 0;
    size_t i;

    if (!read_whole_file(low_path, &lo_bytes, &lo_len) ||
        !read_whole_file(high_path, &hi_bytes, &hi_len)) {
        free(lo_bytes);
        free(hi_bytes);
        return 0;
    }
    if (lo_len < STUNRUN_GSP_PALETTE_ENTRIES * 2u ||
        hi_len < STUNRUN_GSP_PALETTE_ENTRIES * 2u) {
        free(lo_bytes);
        free(hi_bytes);
        return 0;
    }
    for (i = 0; i < STUNRUN_GSP_PALETTE_ENTRIES; ++i) {
        lo[i] = (uint16_t)(lo_bytes[i * 2u] | (lo_bytes[i * 2u + 1u] << 8));
        hi[i] = (uint16_t)(hi_bytes[i * 2u] | (hi_bytes[i * 2u + 1u] << 8));
    }
    free(lo_bytes);
    free(hi_bytes);
    return 1;
}

/* ── Live GSP video ──────────────────────────────────────────────────────
 *
 * Scan out the GSP board share that OUR OWN 68010 has already written, and
 * present it.  The `gsp-tms34010-display` path further down reuses the same
 * rasteriser but can only read MAME dump FILES, so it can never show our own
 * work.  This is the path that can.
 *
 * Caveats, stated plainly:
 *   - Nothing here interprets the GSP.  We have no TMS34010, so nothing
 *     EXECUTES the display list the 68010 uploads, and the region of VRAM the
 *     GSP would draw (share rows 0x3c-0x77 at frame 600) stays empty here.
 *     What shows up is the 68010's own direct VRAM and 2bpp writes.
 *   - The display registers are the frame-600 measurements from
 *     stunrun_gsp_display_frame600_regs().  DPYADR/DPYSTRT/DPYTAP are set by
 *     the GSP from its own microcode, which we do not run, so there is no way
 *     to observe them move.  They are effectively constant across the title
 *     sequence, so the geometry is right even though it is not tracked.
 *   - Not synchronised. The caller decides where a frame boundary is; a
 *     partially-built frame is a legitimate thing to see here.
 *
 * Returns 0 on success, -1 if there is no live CPU or no VRAM to read. */
static int get_live_gsp_state(musashi_gsp_memory_t *mem,
                              stunrun_gsp_display_regs_t *dregs)
{
    if (g_cpu == NULL || mem == NULL || dregs == NULL ||
        musashi_cpu_gsp_memory(g_cpu, mem) != 0)
        return -1;

    /* Read display registers from the GSP RTL.
     *
     * The test is "has the core written them", NOT "is the core running right
     * now". The GSP halts on STOP whenever it finishes a display-list pass, so
     * gating on is_halted() means the registers are used only in the brief
     * windows between passes and the frame-600 fallback wins almost always --
     * which is why the live window kept showing frame-600 geometry no matter
     * what the core was doing. DPYCTL is non-zero once the core has set up
     * scanout, so it is the honest "have we ever run" flag.
     *
     * These are the RTL's OWN registers, tracked from its I/O bus, not
     * constants: DPYSTRT/DPYADR/DPYTAP/DPYCTL below are read back from the
     * core rather than assumed -- and, since the decode at
     * gsp_rtl_driver.cpp's I/O bus trace was fixed, actually observed to
     * change. Which of them the CORE has written is reported by
     * gsp_rtl_driver_display_regs_seen(); a register the core never set must
     * not be presented as if it had. */
#ifdef STUNRUN_GSP_RTL
    {
        uint16_t dpystrt = 0, dpyadr = 0, dpytap = 0, dpyctl = 0;
        uint32_t seen = gsp_rtl_driver_display_regs_seen();
        gsp_rtl_driver_get_display_regs(&dpystrt, &dpyadr, &dpytap, &dpyctl);
        if (dpyctl != 0u) {
            dregs->dpystrt = dpystrt;
            /* Report DPYADR as 0 -- meaning "the core has not written it" --
             * unless it really has. Both scanouts already read a zero DPYADR
             * as "start the frame at DPYSTRT", which is what the hardware
             * does: DPYCTL.SRE=1 means the timing generator owns DPYADR and
             * reloads it from DPYSTRT at the top of each frame, so the GSP
             * PROGRAM has no reason to write it.
             *
             * Measured, 600 frames: the core writes DPYSTRT and DPYCTL and
             * never once writes DPYADR or DPYTAP. Reporting the initialiser
             * here would feed the scanout a rowaddr of 63 (share byte
             * 0x7E00, zero) in place of the rowaddr the hardware would be
             * at. Passing 0 engages the existing shared fallback in both
             * gsp_display.c and gsp_scanout_gl.c, so the two windows stay in
             * step and gsp-scanout-gl-parity is unaffected. */
            dregs->dpyadr  = (seen & 0x2u) ? dpyadr : 0u;
            dregs->dpytap  = (seen & 0x4u) ? dpytap : 0u;
            dregs->dpyctl  = dpyctl;
            /* Timing constants the RTL does not track.
             *
             * vsblnk MUST differ from veblnk. DPYADR only advances while
             * veblnk <= v < vsblnk (findings section 12), so setting both to
             * 19 makes the visible window ZERO ROWS TALL: rasterise returns
             * -1 and the shell reports live-video=unavailable. That, not the
             * address units, is what `mode=blank-scaffold` was.
             *
             * 0x0103 is the board's measured value: findings section 12
             * records `vcount=0013 vsblnk=0103 veblnk=0013 palbank=0
             * finescroll=7` for STUN Runner, i.e. 259 against 19. */
            dregs->heblnk   = 110;
            dregs->hsblnk   = 622;
            dregs->veblnk   = 19;
            dregs->vsblnk   = 0x0103;
            dregs->finescroll = 7;
            dregs->palbank    = 0;
        } else {
            stunrun_gsp_display_frame600_regs(dregs);
        }
    }
#else
    stunrun_gsp_display_frame600_regs(dregs);
#endif

    /* Diagnostic override for oracle-reported GSP display registers. The
     * fallback frame-600 registers are wrong for the early 68010-only
     * self-test screen. Supplying these values lets us test the captured VRAM
     * through the same CPU scanout without pretending the halted GSP produced
     * them. Values are hexadecimal, matching the MAME register dump. */
    {
        const struct { const char *name; uint16_t *field; } regs[] = {
            { "STUNRUN_DPYADR", &dregs->dpyadr },
            { "STUNRUN_DPYSTRT", &dregs->dpystrt },
            { "STUNRUN_DPYTAP", &dregs->dpytap },
            { "STUNRUN_DPYCTL", &dregs->dpyctl },
            { "STUNRUN_HEBLNK", &dregs->heblnk },
            { "STUNRUN_HSBLNK", &dregs->hsblnk },
            { "STUNRUN_VEBLNK", &dregs->veblnk },
            { "STUNRUN_VSBLNK", &dregs->vsblnk },
        };
        size_t i;
        for (i = 0; i < sizeof(regs) / sizeof(regs[0]); ++i) {
            const char *text = getenv(regs[i].name);
            if (text && text[0]) {
                char *end = NULL;
                unsigned long value = strtoul(text, &end, 16);
                if (end != text && *end == '\0' && value <= 0xFFFFu)
                    *regs[i].field = (uint16_t)value;
            }
        }
        {
            const char *text = getenv("STUNRUN_FINESCROLL");
            if (text && text[0]) {
                char *end = NULL;
                unsigned long value = strtoul(text, &end, 16);
                if (end != text && *end == '\0' && value <= 0xFFu)
                    dregs->finescroll = (uint8_t)value;
            }
        }
    }

    /* STUNRUN_FORCE_FRAMEBUF=<hex>: diagnostic scanout of a physical byte offset
     * in the 512 KiB share. The rasterizer's row base is a uint16 word index:
     * byte_base = rowaddr << 11. The low address remainder is converted into
     * DPYTAP (8 display pixels per tap unit). For the $25E8 text target
     * HSTADR=$FF896040 -> share byte $12C08 -> rowaddr=$25, tap=$81. */
    {
        static unsigned s_force_row = 0xFFFFu;
        const char *fb = getenv("STUNRUN_FORCE_FRAMEBUF");
        if (fb && fb[0]) {
            char *end = NULL;
            unsigned long v = strtoul(fb, &end, 16);
            /* v is a physical byte offset; display base is in 16-bit words. */
            if (end != fb && *end == '\0' && v < MUSASHI_GSP_VRAM_WORDS * 2u) {
                unsigned row = (unsigned)(v >> 11);
                unsigned row_byte_base = row << 11;
                if (s_force_row == 0xFFFFu) {
                    printf("shell: force-framebuf=0x%lX byte-base=0x%lX row=0x%X dpyadr=0x%04X\n",
                           v, (unsigned long)row_byte_base,
                           row, (row ^ 0xFFFCu) & 0xFFFFu);
                }
                s_force_row = row;
                dregs->dpyadr = (uint16_t)((row ^ 0xFFFCu) & 0xFFFFu);
                dregs->dpytap = (uint16_t)((v - row_byte_base) / 8u);
                dregs->dpyctl &= ~STUNRUN_DPYCTL_ORG;
            }
        }
    }

    return 0;
}

static int render_live_gsp_frame(void)
{
    musashi_gsp_memory_t mem;
    stunrun_gsp_display_regs_t dregs;
    if (get_live_gsp_state(&mem, &dregs) != 0)
        return -1;

    /* stunrun_gsp_display_render returns 0 on SUCCESS and -1 on error. */
    return stunrun_gsp_display_render(&renderer, mem.vram,
                                      MUSASHI_GSP_VRAM_WORDS,
                                      mem.pal_lo, mem.pal_hi,
                                      STUNRUN_GSP_PALETTE_ENTRIES, &dregs);
}

/* Upload the renderer to the video backend and show it.
 *
 * No palette is uploaded here.  Every path that reaches this helper has
 * already resolved colours to RGB888 pixels, so the backend has nothing left
 * to expand; the legacy `gsp-visible-state` mode, which does hand the backend
 * an indexed image, presents on its own path further down. */
static void present_to_backend(void)
{
    stunrun_video_frame_t vf;

    if (!g_backend_active || g_backend_pair.video.user == NULL)
        return;
    stunrun_video_frame_init(&vf, STUNRUN_RENDER_WIDTH, STUNRUN_RENDER_HEIGHT,
                            STUNRUN_RENDER_WIDTH * 3, /* stride in bytes */
                            STUNRUN_VIDEO_FORMAT_RGB888,
                            renderer.pixels);
    if (g_backend_pair.video.upload == NULL ||
        !g_backend_pair.video.upload(g_backend_pair.video.user, &vf))
        return;
    if (g_backend_pair.video.present)
        g_backend_pair.video.present(g_backend_pair.video.user);
}

static int render_text_fixture(stunrun_renderer_t *renderer,
                               const char *table_path,
                               const char *words_path)
{
    uint16_t table[128u * 4u];
    uint16_t words[1024u];
    stunrun_gsp_text_glyph_t glyphs[2048u];
    size_t word_count = 0u;
    size_t glyph_count;
    uint32_t a0;
    uint32_t a1;
    uint32_t y_bias;
    size_t i;

    if (!load_word_fixture(table_path, table, sizeof(table) / sizeof(*table),
                           &word_count) || word_count != 128u * 4u ||
        !load_word_fixture(words_path, words,
                           sizeof(words) / sizeof(*words), &word_count) ||
        !parse_fixture_u32("STUNRUN_GSP_TEXT_A0", &a0) ||
        !parse_fixture_u32("STUNRUN_GSP_TEXT_A1", &a1) ||
        !parse_fixture_u32("STUNRUN_GSP_TEXT_Y_BIAS", &y_bias))
        return 0;
    glyph_count = stunrun_gsp_text_cursor_decode(
        a0, a1, (int)y_bias, words, word_count, glyphs,
        sizeof(glyphs) / sizeof(*glyphs));
    if (glyph_count == 0u)
        return 0;
    for (i = 0u; i < glyph_count; i++) {
        if (!stunrun_render_gsp_glyph_from_table(
                renderer, table, sizeof(table) / sizeof(*table),
                glyphs[i].glyph_code, glyphs[i].x, glyphs[i].y,
                0xFFu, 0xFEu, 0u))
            return 0;
    }
    printf("shell: gsp-text fixture=loaded words=%u glyphs=%u "
           "a0=0x%08X a1=0x%08X y-bias=%u\n",
           (unsigned)word_count, (unsigned)glyph_count,
           (unsigned)a0, (unsigned)a1, (unsigned)y_bias);
    return 1;
}

static int render_text_record_fixture(stunrun_renderer_t *renderer,
                                       const char *table_path,
                                       const char *records_path)
{
    uint16_t table[128u * 4u];
    uint16_t words[1024u];
    stunrun_gsp_text_record_t record;
    stunrun_gsp_text_glyph_t glyphs[2048u];
    size_t table_count = 0u;
    size_t word_count = 0u;
    size_t record_index;
    uint32_t record_base;
    uint32_t record_bases[128u];
    const char *record_bases_text;
    uint32_t y_bias;

    if (!load_word_fixture(table_path, table, sizeof(table) / sizeof(*table),
                           &table_count) || table_count != 128u * 4u ||
        !load_word_fixture(records_path, words, sizeof(words) / sizeof(*words),
                           &word_count) ||
        word_count == 0u || word_count % STUNRUN_GSP_TEXT_RECORD_WORDS != 0u ||
        !parse_fixture_u32("STUNRUN_GSP_TEXT_Y_BIAS", &y_bias))
        return 0;
    if (word_count / STUNRUN_GSP_TEXT_RECORD_WORDS >
        sizeof(record_bases) / sizeof(*record_bases))
        return 0;
    record_bases_text = getenv("STUNRUN_GSP_TEXT_RECORD_BASES");
    if (record_bases_text != NULL && record_bases_text[0] != '\0') {
        char copy[4096];
        if (strlen(record_bases_text) >= sizeof(copy))
            return 0;
        memcpy(copy, record_bases_text, strlen(record_bases_text) + 1u);
        if (!parse_fixture_bases(
                copy, record_bases, word_count / STUNRUN_GSP_TEXT_RECORD_WORDS))
            return 0;
    } else {
        if (!parse_fixture_u32("STUNRUN_GSP_TEXT_RECORD_BASE", &record_base))
            return 0;
        for (record_index = 0u;
             record_index < word_count / STUNRUN_GSP_TEXT_RECORD_WORDS;
             record_index++)
            record_bases[record_index] = record_base +
                                         (uint32_t)(record_index * 0x80u);
    }
    for (record_index = 0u;
         record_index < word_count / STUNRUN_GSP_TEXT_RECORD_WORDS;
         record_index++) {
        const uint16_t *packed;
        uint32_t descriptor;
        uint32_t a1;
        size_t glyph_count;
        size_t glyph_index;
        stunrun_gsp_text_record_load(
            words + record_index * STUNRUN_GSP_TEXT_RECORD_WORDS, &record);
        if (!stunrun_gsp_text_record_cursor(
                record_bases[record_index], &record,
                &descriptor, &a1, &packed))
            return 0;
        (void)descriptor;
        glyph_count = stunrun_gsp_text_cursor_decode(
            descriptor, a1, (int)y_bias, packed, 4u, glyphs,
            sizeof(glyphs) / sizeof(*glyphs));
        if (glyph_count == 0u)
            return 0;
        for (glyph_index = 0u; glyph_index < glyph_count; glyph_index++)
            if (!stunrun_render_gsp_glyph_from_table(
                    renderer, table, sizeof(table) / sizeof(*table),
                    glyphs[glyph_index].glyph_code, glyphs[glyph_index].x,
                    glyphs[glyph_index].y, 0xFFu, 0xFEu, 0u))
                return 0;
    }
    printf("shell: gsp-text-record fixture=loaded records=%u words=%u "
           "base=0x%08X y-bias=%u\n",
           (unsigned)(word_count / STUNRUN_GSP_TEXT_RECORD_WORDS),
           (unsigned)word_count, (unsigned)record_bases[0], (unsigned)y_bias);
    return 1;
}

static uint32_t geometry_sum(const uint16_t *words)
{
    uint32_t sum = 0;
    unsigned i;
    for (i = 0; i < STUNRUN_GEOM_MARCH_WORDS; i++)
        sum += words[i];
    return sum;
}

#define expect(cond) \
    do { \
        if (!(cond)) { \
            printf("shell: ASSERT FAIL %s frame=%u\n", #cond, frame); \
            g_failures++; \
        } \
    } while (0)

/* Feed the exact title-path control multiset into the tally. */
static void feed_control_contract(stunrun_adsp_control_tally_t *tally)
{
    const stunrun_adsp_control_op_t *ops = stunrun_adsp_control_ops();
    size_t i;
    unsigned k;
    for (i = 0; i < stunrun_adsp_control_op_count(); i++) {
        for (k = 0; k < ops[i].expected_writes; k++)
            stunrun_adsp_control_tally_add(tally, ops[i].address,
                                           STUNRUN_ADSP_CONTROL_DATA);
    }
}

static const char *action_name(stunrun_exp_action_t action)
{
    switch (action) {
    case STUNRUN_EXP_PRESS:
        return "press";
    case STUNRUN_EXP_RELEASE:
        return "release";
    case STUNRUN_EXP_SET:
        return "set";
    default:
        return "unknown";
    }
}

/* Dispatch file input events scheduled for the current frame. Order across
 * events sharing a frame follows file order; unsorted files still dispatch
 * every event exactly once because each frame scans the whole list. */
static void dispatch_inputs(stunrun_fake_ports_t *ports)
{
    unsigned i;
    for (i = 0; i < g_event_count; i++) {
        const stunrun_exp_event_t *ev = &g_events[i];
        if (ev->frame != frame)
            continue;
        g_dispatched++;
        printf("shell: input frame=%u port=%s field=%s action=%s",
               frame, ev->port, ev->field, action_name(ev->action));
        if (ev->has_value)
            printf(" value=%d", ev->value);
        if (ev->label[0] != '\0')
            printf(" label=%s", ev->label);
        printf("\n");
        input_latch_apply(ev);
        (void)stunrun_port_set_named(ports, ev->port, ev->field,
                                     ev->action == STUNRUN_EXP_PRESS ? 1 :
                                     ev->action == STUNRUN_EXP_RELEASE ? 0 :
                                     ev->value);
        printf("shell: input-state frame=%u active=%u hash=0x%08X\n",
               frame, input_active_count(), (unsigned)g_input_hash);
    }
}

static const char *upload_name(stunrun_adsp_upload_state_t state)
{
    switch (state) {
    case STUNRUN_ADSP_UPLOAD_EMPTY:
        return "empty";
    case STUNRUN_ADSP_UPLOAD_POPULATING:
        return "populating";
    case STUNRUN_ADSP_UPLOAD_COMPLETE:
        return "complete";
    default:
        return "unknown";
    }
}

/* Emit the shared checkpoint shape for the native transport model.  CPU
 * registers are deliberately zero because this shell models verified
 * interconnect contracts, not a CPU interpreter.  Keeping those fields
 * explicit prevents the output from being mistaken for an oracle match. */
static void emit_model_checkpoint(const uint32_t *image, size_t image_words)
{
    stunrun_checkpoint_t cp;
    char document[4096];
    unsigned i;
    unsigned nonzero = 0;
    uint32_t sum32 = 0;
    unsigned first = 0;
    memset(&cp, 0, sizeof(cp));
    snprintf(cp.system, sizeof(cp.system), "stunrun");
    snprintf(cp.description, sizeof(cp.description),
             "native-shell-transport-model");
    snprintf(cp.mame, sizeof(cp.mame), "native-model");
    cp.frame = g_terminal;
    cp.time_seconds = (double)g_terminal / 60.0;
    snprintf(cp.maincpu_tag, sizeof(cp.maincpu_tag), ":mainpcb:maincpu");
    snprintf(cp.gsp_tag, sizeof(cp.gsp_tag), ":mainpcb:gsp");
    snprintf(cp.adsp_tag, sizeof(cp.adsp_tag), ":mainpcb:adsp");
    snprintf(cp.soundcpu_tag, sizeof(cp.soundcpu_tag), ":mainpcb:jsa:cpu");
    snprintf(cp.region_space, sizeof(cp.region_space), "program");
    snprintf(cp.region_range, sizeof(cp.region_range), "0x0000-0x1FFF");
    cp.region_word_width = 24;
    for (i = 0; i < image_words; i++) {
        if (image[i] != 0u) {
            if (first < STUNRUN_CHECKPOINT_FIRST_WORDS)
                cp.region_first_words[first++] = image[i];
            nonzero++;
        }
        sum32 += image[i];
    }
    cp.region_nonzero_words = nonzero;
    cp.region_sum32 = sum32;
    cp.adsp_program_loaded = g_terminal >= 412u;
    if (stunrun_checkpoint_emit(&cp, document, sizeof(document)) == 0u) {
        printf("shell: checkpoint-json=emit-failed\n");
        g_failures++;
        return;
    }
    printf("shell: checkpoint-json=");
    for (i = 0; document[i] != '\0'; i++)
        putchar(document[i] == '\n' ? ' ' : document[i]);
    putchar('\n');
}

static int run_walk(void)
{
    static uint32_t image[STUNRUN_ADSP_INIT_STATE_WORDS];
    static uint16_t dm[STUNRUN_ADSP_DM_SIZE];
    stunrun_gsp_video_state_t video;
    const char *render_path;
    stunrun_jsa_latches_t latches;
    stunrun_adsp_control_tally_t tally;
    stunrun_jsa_title_counts_t counts;
    stunrun_fifo_run_summary_t fifo_summary;
    stunrun_fake_ports_t ports;
    stunrun_game_loop_t game_loop;
    const char *control_state = "skipped";
    const char *counts_state = "skipped";
    size_t emitted;
    unsigned i;
    const char *gsp_vram_path = getenv("STUNRUN_GSP_VRAM_BIN");
    const char *gsp_palette_path = getenv("STUNRUN_GSP_PALETTE_BIN");
    const char *gsp_palette_low_path = getenv("STUNRUN_GSP_PALETTE_LOW_BIN");
    const char *gsp_palette_high_path = getenv("STUNRUN_GSP_PALETTE_HIGH_BIN");
    const char *geometry_table_path = getenv("STUNRUN_GEOM_TABLE_BIN");
    const char *gsp_text_table_path = getenv("STUNRUN_GSP_TEXT_TABLE_BIN");
    const char *gsp_text_words_path = getenv("STUNRUN_GSP_TEXT_WORDS_BIN");
    const char *gsp_text_record_path = getenv("STUNRUN_GSP_TEXT_RECORD_BIN");
    const char *road_strip_fixture = getenv("STUNRUN_ROAD_STRIP_FIXTURE");
    const char *render_mode = "blank-scaffold";

    /* Screenshot capture env vars: STUNRUN_CAPTURE_FRAME=N, STUNRUN_CAPTURE_ALL=1, STUNRUN_CAPTURE_DIR=path */
    {
        const char *cf = getenv("STUNRUN_CAPTURE_FRAME");
        if (cf && cf[0]) {
            g_capture_frame = (unsigned)atoi(cf);
            printf("DEBUG: STUNRUN_CAPTURE_FRAME='%s' -> %u\n", cf, g_capture_frame);
        }
        if (getenv("STUNRUN_CAPTURE_ALL")) {
            g_capture_all = true;
            printf("DEBUG: STUNRUN_CAPTURE_ALL set\n");
        }
        const char *cd = getenv("STUNRUN_CAPTURE_DIR");
        if (cd && cd[0]) {
            size_t len = strlen(cd);
            if (len >= sizeof(g_capture_dir)) len = sizeof(g_capture_dir) - 1;
            memcpy(g_capture_dir, cd, len);
            g_capture_dir[len] = '\0';
            printf("DEBUG: STUNRUN_CAPTURE_DIR='%s'\n", g_capture_dir);
        }
        const char *ff = getenv("STUNRUN_FRAMES");
        if (ff && ff[0]) {
            /* Zero is the opt-in interactive mode: run until SDL_QUIT.  Keep
             * the bounded default and positive frame-count behavior for
             * deterministic boot traces and tests. */
            unsigned requested = (unsigned)strtoul(ff, NULL, 0);
            if (requested == 0u) {
                g_run_until_quit = true;
                printf("DEBUG: STUNRUN_FRAMES='%s' -> run-until-window-close\n",
                       ff);
            } else {
                g_terminal = requested;
                printf("DEBUG: STUNRUN_FRAMES='%s' -> terminal=%u\n",
                       ff, g_terminal);
            }
        }
        const char *dd = getenv("STUNRUN_DUMP_DIR");
        if (dd && dd[0]) {
            size_t len = strlen(dd);
            if (len >= sizeof(g_dump_dir)) len = sizeof(g_dump_dir) - 1;
            memcpy(g_dump_dir, dd, len);
            g_dump_dir[len] = '\0';
            printf("DEBUG: STUNRUN_DUMP_DIR='%s'\n", g_dump_dir);
        }
        const char *tw = getenv("STUNRUN_TRACE_WINDOW");
        if (tw && tw[0]) {
            size_t len = strlen(tw);
            if (len >= sizeof(g_trace_window))
                len = sizeof(g_trace_window) - 1;
            memcpy(g_trace_window, tw, len);
            g_trace_window[len] = '\0';
        }
        const char *tr = getenv("STUNRUN_TRACE");
        if (tr && tr[0]) g_trace_count = (unsigned)atoi(tr);
        const char *fl = getenv("STUNRUN_FLOW_LOG");
        if (fl && fl[0]) {
            size_t len = strlen(fl);
            if (len >= sizeof(g_flow_log_path))
                len = sizeof(g_flow_log_path) - 1;
            memcpy(g_flow_log_path, fl, len);
            g_flow_log_path[len] = '\0';
        }
        const char *ph = getenv("STUNRUN_PC_HIST");
        if (ph && ph[0]) {
            size_t len = strlen(ph);
            if (len >= sizeof(g_pc_hist_path))
                len = sizeof(g_pc_hist_path) - 1;
            memcpy(g_pc_hist_path, ph, len);
            g_pc_hist_path[len] = '\0';
            musashi_cpu_flow_log_enable(1);
        }
        pclog_open();
        const char *ds = getenv("STUNRUN_DISASM");
        if (ds && ds[0]) {
            unsigned long s = 0, e = 0;
            int p = 0;
            if (sscanf(ds, "%lx:%lx:%n", &s, &e, &p) >= 2 && p > 0) {
                g_disasm_start = s;
                g_disasm_end = e;
                const char *path = ds + p;
                size_t len = strlen(path);
                if (len >= sizeof(g_disasm_path))
                    len = sizeof(g_disasm_path) - 1;
                memcpy(g_disasm_path, path, len);
                g_disasm_path[len] = '\0';
            } else {
                printf("DEBUG: STUNRUN_DISASM wants <start:end>:<path>, got "
                       "'%s'\n", ds);
            }
        }
    }
    uint16_t geometry_table[STUNRUN_GEOM_MARCH_WORDS];

    stunrun_jsa_latches_init(&latches);
    stunrun_ports_init(&ports);
    {
        /* Input-port overrides.  The DIP defaults are transcribed from
         * INPUT_PORTS_START(stunrun), but the ones that steer the boot path
         * (IN0 bit5 = service/self-test, IN0 bit0 = diagnostic jumper) are
         * easy to get wrong, and a wrong guess sends the 68010 down a 35-frame
         * VRAM fill that MAME never executes.  Let the environment settle the
         * question by brute force instead of by more static reasoning:
         *   STUNRUN_IN0=<hex>   STUNRUN_SW1=<hex>   STUNRUN_A800=<hex>  */
        static const struct { const char *env; stunrun_port_id_t id; } ov[] = {
            { "STUNRUN_IN0", STUNRUN_PORT_MAIN_IN0 },
            { "STUNRUN_SW1", STUNRUN_PORT_MAIN_SW1 },
            { "STUNRUN_A800", STUNRUN_PORT_MAIN_A800 },
        };
        size_t i;
        for (i = 0; i < sizeof(ov) / sizeof(ov[0]); i++) {
            const char *v = getenv(ov[i].env);
            if (v && v[0]) {
                unsigned long raw = strtoul(v, NULL, 0);
                stunrun_port_write(&ports, ov[i].id, (uint16_t)raw);
                printf("DEBUG: %s -> 0x%04lX\n", ov[i].env, raw);
            }
        }
    }
    stunrun_game_loop_init(&game_loop, &ports);
    stunrun_adsp_control_tally_init(&tally);
    stunrun_render_init(&renderer);
    stunrun_gsp_video_init(&video);
    g_gsp_indexed_backend_frame = false;

    /* Optional live backends: activated when STUNRUN_VIDEO_BACKEND or
     * STUNRUN_AUDIO_BACKEND are set (or auto-detected if not specified).
     * In backend mode the renderer writes to an internal RGB888 buffer
     * that is uploaded to the GPU each frame via the backend v-table.
     *
     * SDL_Init must be called before creating any window or audio device.
     * We init both subsystems regardless of which backends are active so
     * that audio can still play even when the video backend is headless. */
    {
        const char *vid_name = getenv("STUNRUN_VIDEO_BACKEND");
        const char *aud_name = getenv("STUNRUN_AUDIO_BACKEND");
        int want_video = !vid_name || (vid_name[0] != '\0' && strcmp(vid_name, "0") != 0);
        int want_audio = !aud_name || (aud_name[0] != '\0' && strcmp(aud_name, "0") != 0);

        if (want_video || want_audio) {
            /* Initialise SDL video + audio subsystems once.  Both are needed
             * for the SDL2 audio backend even if no window is created. */
            if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
                fprintf(stderr, "shell: SDL_Init failed: %s\n", SDL_GetError());
            } else {
                printf("shell: sdl=init video=%d audio=%d\n",
                       want_video ? 1 : 0, want_audio ? 1 : 0);
            }

            /* Pre-load env names so pair_init can pick them up */
            if (vid_name && vid_name[0] != '\0' && strcmp(vid_name, "0") != 0)
                setenv("STUNRUN_VIDEO_BACKEND", vid_name, 1);
            if (aud_name && aud_name[0] != '\0' && strcmp(aud_name, "0") != 0)
                setenv("STUNRUN_AUDIO_BACKEND", aud_name, 1);

            /* If a video backend is needed, create a window for it.
             * The null backend does not need a window (pass NULL). */
            g_window = NULL;
            if (want_video) {
                const char *auto_vid = getenv("STUNRUN_VIDEO_BACKEND");
                if (!auto_vid || strcmp(auto_vid, "null") != 0) {
                    /* We only ever upload a texture and blit a fullscreen quad,
                     * so ask for no depth or stencil and let the driver choose a
                     * default visual.
                     *
                     * The GL context request is best-effort and DEGRADES IN
                     * THREE STEPS, because it is not portable:
                     *   1. GL 2.1 compatibility profile -- what GLEW wants.
                     *   2. No version or profile constraint -- drivers that
                     *      expose only core profiles, and Mesa's GLX path,
                     *      reject a compatibility request with an X error
                     *      that SDL surfaces as a bare `BadValue` on
                     *      X_CreateWindow, which is exactly what this used to
                     *      do on a stock X server.
                     *   3. No GL at all -- SDL software surface. The sdl_opengl
                     *      backend will then report no context and fall back
                     *      rather than taking the whole shell down.
                     * GLEW probes extensions at run time, so a 3.3 core
                     * context satisfies everything sdl_video.c asks for. */
                    static const char *const window_flags[] = {
                        "sdl_opengl_gl21_compat", "sdl_opengl_default",
                        "sdl_software"
                    };
                    size_t step;
                    for (step = 0; step < sizeof(window_flags) / sizeof(*window_flags);
                         ++step) {
                        Uint32 flags = SDL_WINDOW_RESIZABLE;
                        if (strcmp(window_flags[step], "sdl_software") != 0) {
                            SDL_GL_ResetAttributes();
                            SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
                            SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
                            SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
                            if (strcmp(window_flags[step],
                                       "sdl_opengl_gl21_compat") == 0) {
                                SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
                                SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
                                SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                                    SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
                            }
                            flags |= SDL_WINDOW_OPENGL;
                        }
                        g_window = SDL_CreateWindow(
                            "STUN Runner",
                            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                            (int)STUNRUN_RENDER_WIDTH * 2,
                            (int)STUNRUN_RENDER_HEIGHT * 2,
                            flags);
                        if (g_window) break;
                        if (step + 1u < sizeof(window_flags) / sizeof(*window_flags))
                            fprintf(stderr,
                                    "shell: SDL_CreateWindow(%s) failed (%s); "
                                    "relaxing GL request\n",
                                    window_flags[step], SDL_GetError());
                    }
                    if (!g_window) {
                        fprintf(stderr,
                                "shell: SDL_CreateWindow failed for every "
                                "attempt: %s\n", SDL_GetError());
                    } else {
                        fprintf(stderr, "shell: window=%zu backend=%s\n",
                                step + 1, window_flags[step]);
                    }
                }
            }

            memset(&g_backend_pair, 0, sizeof(g_backend_pair));
            if (stunrun_backend_pair_init(&g_backend_pair,
                    g_window,   /* window_ctx — NULL for headless */
                    STUNRUN_RENDER_WIDTH, STUNRUN_RENDER_HEIGHT,
                    g_window ? (uint32_t)(STUNRUN_RENDER_WIDTH * 2)
                             : STUNRUN_RENDER_WIDTH,
                    g_window ? (uint32_t)(STUNRUN_RENDER_HEIGHT * 2)
                             : STUNRUN_RENDER_HEIGHT,
                    48000)) {
                g_backend_active = true;
                printf("shell: backend=active video=%s audio=%s window=%s\n",
                       g_backend_pair.video.name
                           ? g_backend_pair.video.name : "(auto)",
                       g_backend_pair.audio.name
                           ? g_backend_pair.audio.name : "(auto)",
                       g_window ? "created" : "headless");

#ifdef STUNRUN_GSP_GL_SCANOUT
                /* Left: CPU GSP reference scanout. Right: an independent
                 * fragment-shader scanout reading the live board state. */
                {
                    const char *live_env = getenv("STUNRUN_LIVE_VIDEO");
                    const char *dual_env = getenv("STUNRUN_DUAL_SCANOUT_GL");
                    int dual_enabled = live_env && live_env[0] &&
                                       strcmp(live_env, "0") != 0 &&
                                       (!dual_env || !dual_env[0] ||
                                        strcmp(dual_env, "0") != 0);
                    if (dual_enabled && g_window && g_backend_pair.video.name &&
                        strcmp(g_backend_pair.video.name, "sdl_opengl") == 0) {
                        int x = SDL_WINDOWPOS_CENTERED, y = SDL_WINDOWPOS_CENTERED;
                        int w = (int)STUNRUN_RENDER_WIDTH * 2;
                        Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;
                        SDL_GL_ResetAttributes();
                        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
                        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
                        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
                        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
                        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
                        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                                            SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
                        SDL_GetWindowPosition(g_window, &x, &y);
                        g_gl_scanout_window = SDL_CreateWindow(
                            "OpenGL GSP Scanout", x + w, y, w,
                            (int)STUNRUN_RENDER_HEIGHT * 2, flags);
                        if (g_gl_scanout_window)
                            g_gl_scanout = stunrun_gsp_gl_scanout_create(
                                g_gl_scanout_window);
                        if (g_gl_scanout) {
                            SDL_SetWindowTitle(g_window,
                                               "GSP Scanout (CPU Reference)");
                            printf("shell: scanout-compare=active left=cpu-gsp right=gl-shader\n");
                        } else {
                            fprintf(stderr,
                                    "shell: GL scanout comparison unavailable; "
                                    "keeping CPU reference window\n");
                            if (g_gl_scanout_window) {
                                SDL_DestroyWindow(g_gl_scanout_window);
                                g_gl_scanout_window = NULL;
                            }
                        }
                    }
                }
#endif

                /* JSA II audio: load OKI6295 ROM and initialise the dispatcher.
                 * The ROM is the concatenation of all four jsa:oki1 files.
                 * STUNRUN_JSA_OKI_ROM can be a colon-separated list of up to 4
                 * paths; if not set, defaults to the standard ROM location. */
                {
                    const char *oki_paths_env = getenv("STUNRUN_JSA_OKI_ROM");
                    char oki_paths[4][260];
                    int oki_count = 0;
                    /* Default: try the standard ROM names in the stunrun/ directory
                     * relative to STUNRUN_STUNRUN_DIR or the current directory. */
                    const char *base = getenv("STUNRUN_STUNRUN_DIR");
                    if (!base) base = ".";

                    if (oki_paths_env && oki_paths_env[0]) {
                        /* Colon-separated list. */
                        char copy[1024];
                        if (strlen(oki_paths_env) < sizeof(copy)) {
                            strcpy(copy, oki_paths_env);
                            char *tok = strtok(copy, ":");
                            while (tok && oki_count < 4) {
                                strncpy(oki_paths[oki_count], tok, sizeof(oki_paths[0]) - 1);
                                oki_paths[oki_count][sizeof(oki_paths[0]) - 1] = '\0';
                                oki_count++;
                                tok = strtok(NULL, ":");
                            }
                        }
                    } else {
                        /* Fall back to the standard four ROM filenames. */
                        const char *defaults[] = {
                            "136070-2124.1fh",
                            "136070-2125.1ef",
                            "136070-2126.1de",
                            "136070-2127.1cd",
                        };
                        for (int i = 0; i < 4 && oki_count < 4; i++) {
                            snprintf(oki_paths[oki_count], sizeof(oki_paths[0]),
                                     "%s/%s", base, defaults[i]);
                            oki_count++;
                        }
                    }

                    /* Load all OKI ROM files into one contiguous 256 KB buffer. */
                    uint8_t *oki_rom = NULL;
                    uint32_t oki_rom_size = 0;
                    if (oki_count > 0) {
                        oki_rom = (uint8_t *)malloc((size_t)oki_count * 65536);
                        if (oki_rom) {
                            memset(oki_rom, 0, (size_t)oki_count * 65536);
                            oki_rom_size = (uint32_t)oki_count * 65536;
                            for (int i = 0; i < oki_count; i++) {
                                FILE *f = fopen(oki_paths[i], "rb");
                                if (f) {
                                    size_t got = fread(oki_rom + (size_t)i * 65536,
                                                       1, 65536, f);
                                    printf("shell: oki-rom[%d] path=%s loaded=%zu\n",
                                           i, oki_paths[i], got);
                                    fclose(f);
                                } else {
                                    printf("shell: oki-rom[%d] path=%s not-found\n",
                                           i, oki_paths[i]);
                                }
                            }
                        }
                    }

                    if (oki_rom && oki_rom_size > 0) {
                        if (sound_dispatch_init(&g_sound_dispatch,
                                g_backend_pair.audio.name ? &g_backend_pair.audio : NULL,
                                g_backend_pair.audio.user,
                                oki_rom, oki_rom_size)) {
                            g_sound_dispatch_active = true;
                            printf("shell: sound-dispatch=active oki-rom-size=%u\n",
                                   oki_rom_size);

                            /* Optional audio-path probe. This deliberately
                             * starts one real ROM-mapped effect directly at
                             * the sound dispatcher, bypassing the MC68010
                             * producer so SDL/backend PCM output can be
                             * checked even when game boot has not reached its
                             * sound-command queue. It is opt-in and does not
                             * change normal gameplay. */
                            const char *probe_env =
                                getenv("STUNRUN_AUDIO_PROBE_COMMAND");
                            if (probe_env && probe_env[0]) {
                                char *end = NULL;
                                long command = strtol(probe_env, &end, 0);
                                if (end != probe_env && *end == '\0' &&
                                    command >= 0 && command <= 0x6C) {
                                    sound_dispatch_send_command(
                                        &g_sound_dispatch, (uint8_t)command);
                                    printf("shell: audio-probe=command-0x%02lX "
                                           "(bypasses MC68010 producer)\n",
                                           command);
                                } else {
                                    fprintf(stderr,
                                            "shell: invalid STUNRUN_AUDIO_PROBE_COMMAND '%s' "
                                            "(expected 0..0x6C)\n",
                                            probe_env);
                                }
                            }
                        } else {
                            free(oki_rom);
                            printf("shell: sound-dispatch=init-failed\n");
                        }
                    } else {
                        free(oki_rom);
                        printf("shell: oki-rom=not-loaded\n");
                    }
                }
            } else {
                printf("shell: backend=init-failed\n");
            }
        }
    }

    /* MC68010: boot the real ROM when STUNRUN_MC68010=1.
     * The ROM dir defaults to STUNRUN_STUNRUN_DIR or ".". */
    {
        const char *mc68010_env = getenv("STUNRUN_MC68010");
        if (mc68010_env && strcmp(mc68010_env, "0") != 0) {
            const char *rom_dir = getenv("STUNRUN_STUNRUN_DIR");
            const char *lv = getenv("STUNRUN_LIVE_VIDEO");
            const char *lvs = getenv("STUNRUN_LIVE_VIDEO_STRIDE");
            const char *lvf = getenv("STUNRUN_LIVE_VIDEO_FPS");
            if (!rom_dir) rom_dir = ".";
            if (lv && lv[0] && strcmp(lv, "0") != 0) {
                g_live_video = true;
                g_live_video_stride = 1u;
                if (lvs && lvs[0]) {
                    long s = strtol(lvs, NULL, 0);
                    if (s > 0) g_live_video_stride = (unsigned)s;
                }
                if (lvf && lvf[0]) {
                    long f = strtol(lvf, NULL, 0);
                    if (f > 0) g_live_video_fps = (unsigned)f;
                }
            }
            if (g_pc_hist_path[0])
                musashi_cpu_set_pc_hist(1);
            g_cpu = musashi_cpu_create();
            if (!g_cpu) {
                printf("shell: mc68010=create-failed\n");
            } else {
                /* Init the override table before resetting the CPU so the
                 * instruction hook is registered before the first instruction. */
                cpu_override_init();
                musashi_cpu_set_ports(g_cpu, &ports);
#ifdef STUNRUN_GSP_RTL
                /* Give the core the 68010's host-port traffic. Without this it
                 * never sees the HSTCTL write that clears HLT, so it never
                 * leaves reset and never draws anything. */
                gsp_rtl_ensure_init();
                musashi_cpu_set_gsp_bus_observer(g_cpu, gsp_rtl_bus_observer, NULL);
#endif
                /* Bind the LIVE JSA latch block, not a local: a 68010 store
                 * into $600000-$603FFF reaches stunrun_jsa_main_command_write()
                 * and is consumed by sound_dispatch_frame() below.  With no
                 * dispatch (no OKI ROM) we bind NULL, which the transport
                 * treats as "no latch": stores are dropped, reads return the
                 * open-bus 0xFF exactly as the pre-slice stub did. */
                musashi_cpu_set_jsa_latches(
                    g_cpu, g_sound_dispatch_active
                               ? &g_sound_dispatch.latches
                               : NULL);
                if (musashi_loader_load(g_cpu, rom_dir) == 0) {
                    musashi_cpu_reset(g_cpu);
                    g_mc68010_active = true;
                    printf("shell: mc68010=active rom-dir=%s pc=0x%08X\n",
                           rom_dir, musashi_cpu_get_pc(g_cpu));
                    /* STUNRUN_TEXT_LOG=1: decode every string the MC68010
                     * draws through the GSP host port and print one line each,
                     * so a service-mode run reads as commentary instead of a
                     * 36 MB bus dump.  OBSERVATION ONLY, and off by default:
                     * the gsp_host_write_word() hook is one not-taken branch
                     * when unset, and all 901 354 gsp_bus_events.bin records
                     * are identical in every named field either way.  (The raw
                     * file bytes are not, because gsp_emit_bus_event() leaves
                     * the struct padding unassigned -- see findings 23.6.)
                     * Enable here rather than at create() because the decoder
                     * reads the selector table out of the loaded ROM. */
                    {
                        const char *tl = getenv("STUNRUN_TEXT_LOG");
                        if (tl && tl[0] && strcmp(tl, "0") != 0) {
                            unsigned n = musashi_cpu_text_log_enable(g_cpu);
                            printf("shell: text-log=1 descriptors=%u "
                                   "(394 expected for this ROM)\n", n);
                        }
                    }
                    /* STUNRUN_BOOT_TRACE=<n>: single-step the first n
                     * instructions straight out of reset, before the frame
                     * loop touches anything.  The boot path is only a few
                     * thousand instructions before it reaches the 5M-cycle
                     * VRAM fill, so this is the cheapest way to see where the
                     * CPU actually goes -- a trace taken at shutdown always
                     * starts deep inside the fill instead. */
                    {
                        const char *bt = getenv("STUNRUN_BOOT_TRACE");
                        if (bt && bt[0]) {
                            unsigned n = (unsigned)strtoul(bt, NULL, 0);
                            printf("shell: boot trace %u instructions:\n", n);
                            musashi_cpu_trace(g_cpu, n);
                        }
                    }
                } else {
                    printf("shell: mc68010=rom-load-failed\n");
                    musashi_cpu_destroy(g_cpu);
                    g_cpu = NULL;
                }
            }
        }
    }

    if (geometry_table_path != NULL) {
        unsigned passes;
        unsigned i;
        int copies_match = 1;

        if (!load_geometry_table(geometry_table_path, geometry_table)) {
            printf("shell: geometry-upload fixture=read-failed\n");
            g_failures++;
        } else {
            passes = stunrun_game_loop_load_road_table(&game_loop,
                                                       geometry_table) ?
                     STUNRUN_GEOM_COPY_COUNT : 0u;
            size_t fifo_words = stunrun_game_loop_submit_road(&game_loop);
            int fifo_match = fifo_words == STUNRUN_ROAD_FIFO_WRITES;
            for (i = 0; i < STUNRUN_GEOM_MARCH_WORDS; i++)
                if (game_loop.road_base[i] != game_loop.road_twin[i] ||
                    game_loop.road_base[i] != geometry_table[i])
                    copies_match = 0;
            for (i = 0; i < STUNRUN_ROAD_FIFO_WRITES; i++)
                if (game_loop.road_fifo[i] != game_loop.road_base[i * 2u])
                    fifo_match = 0;
            expect(passes == STUNRUN_GEOM_COPY_COUNT);
            expect(copies_match);
            expect(fifo_match);
            printf("shell: geometry-upload fixture=loaded passes=%u "
                   "bytes=%u sum=0x%08X copies=match fifo-drain=%u/384 "
                   "dest=0x%08X\n", passes,
                   STUNRUN_GEOM_MARCH_WORDS * 2u,
                   (unsigned)geometry_sum(game_loop.road_base),
                   (unsigned)fifo_words, STUNRUN_ROAD_FIFO_DEST);
        }
    }

    for (frame = 0; g_run_until_quit || frame <= g_terminal; frame++) {
        dispatch_inputs(&ports);

        /* Run the MC68010 (real ROM) or the C slices (reconstructed logic).
         * STUNRUN_MC68010=1 activates Musashi emulation.
         * At 8 MHz / 60 fps ≈ 133 333 cycles per frame.  We run a
         * conservative 80 000 cycles here; if the MC68010 hits an
         * unmapped MMIO region it returns 0xFF/0xFFFF which the game
         * should treat as idle/default state. */
        if (g_mc68010_active && g_cpu) {
            /* One frame is STUNRUN_FRAME_CYCLES of CPU time, not a single IRQ
             * period.  Gating the frame on the IRQ period ran 32 768 cycles
             * per frame, so after 600 frames we had retired 19 660 800 cycles
             * where MAME retires ~80 M — roughly 4x behind, which is why the
             * dump showed the CPU still inside the boot VRAM-fill loop. */

            /* IRQ5: the board's periodic interrupt.
             *
             * harddriv.cpp:1465 wires m_maincpu->set_periodic_int to
             * hd68k_irq_gen at HARDDRIV_MASTER_CLOCK/16/16/16/16/2 = 244.14 Hz,
             * which asserts level 5.  At 8 MHz that is one interrupt every
             * 32 768 cycles, i.e. about four times per frame.  The 68010
             * parks in a wait loop until this arrives, so without it the boot
             * never gets past the early RAM-stack setup.  Tick it every period
             * for the whole frame rather than once per frame. */
            /* Slice the frame one raster line at a time.  The dot clock that
             * feeds port0_status() is only advanced by musashi_cpu_run(), so a
             * whole-frame chunk froze hpos/vpos for the whole frame.  The ROM
             * gates on /VSYNCB changing state at 0x13f2:
             *   0013f2: btst #$2, $60c001.l / 0013fa: beq $13f2
             *   0013fc: btst #$2, $60c001.l / 001404: bne $13fc
             * and with a frozen raster the first loop spins forever.  One line
             * is 634 dots at 10 MHz against an 8 MHz CPU, i.e.
             * 634 * 4 / 5 = 507.2 cycles. */
            uint32_t remaining = STUNRUN_FRAME_CYCLES;
            while (remaining > 0u) {
                uint32_t until_irq = musashi_cpu_gsp_cycles_until_irq(
                    g_cpu, STUNRUN_IRQ5_PERIOD);
                uint32_t chunk = STUNRUN_LINE_CYCLES;
                if (until_irq < chunk)
                    chunk = until_irq;
                if (chunk == 0u)
                    chunk = 1u;
                if (chunk > remaining)
                    chunk = remaining;
                musashi_cpu_run(g_cpu, (int)chunk);
                remaining -= chunk;
                if (until_irq <= STUNRUN_LINE_CYCLES)
                    musashi_cpu_set_irq(g_cpu, 5);
            }
            if (g_pclog_fp)
                fprintf(g_pclog_fp, "%u 0x%08lX %lu %zu\n", frame,
                        (unsigned long)musashi_cpu_get_pc(g_cpu),
                        (unsigned long)musashi_cpu_total_cycles(g_cpu),
                        musashi_cpu_gsp_write_count(g_cpu));
        } else if (frame != 0u) {
            stunrun_game_loop_step(&game_loop);
        }

        /* Pump window events and dispatch pending sound commands each frame.
         * SDL_PollEvent is non-blocking; it returns 0 when the event queue
         * is empty.  Window resize events are forwarded to the video backend
         * so it can update its viewport / texture dimensions.  SDL_QUIT
         * terminates the walk early (sets g_terminal to the current frame). */
        if (g_backend_active) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_WINDOWEVENT &&
                    e.window.event == SDL_WINDOWEVENT_RESIZED) {
#ifdef STUNRUN_GSP_GL_SCANOUT
                    if (g_gl_scanout_window &&
                        e.window.windowID == SDL_GetWindowID(g_gl_scanout_window))
                        continue; /* GL scanout queries its drawable size per frame */
#endif
                    if (g_backend_pair.video.on_resize &&
                        g_backend_pair.video.user) {
                        g_backend_pair.video.on_resize(
                            g_backend_pair.video.user,
                            (uint32_t)e.window.data1,
                            (uint32_t)e.window.data2);
                    }
                } else if (e.type == SDL_WINDOWEVENT &&
                           e.window.event == SDL_WINDOWEVENT_CLOSE) {
                    g_run_until_quit = false;
                    g_terminal = frame;
                } else if (e.type == SDL_QUIT) {
                    g_run_until_quit = false;
                    g_terminal = frame;
                }
            }
            /* Dispatch JSA II commands to the OKI6295 emulator and enqueue PCM
             * samples to the audio backend each frame.  sound_dispatch_frame
             * handles its own command-peek (non-clearing), so no separate
             * peek call is needed here. */
            if (g_sound_dispatch_active)
                (void)sound_dispatch_frame(&g_sound_dispatch);
        }
        if (frame == 1) {
            /* Startup response: 6502 writes 0xFF, main IRQ4 fires,
             * handler reads 0x600000, IRQ4 clears. */
            stunrun_jsa_sound_response_write(&latches,
                                             STUNRUN_JSA_STARTUP_BYTE);
            expect(latches.irq4_asserted);
            expect(stunrun_jsa_main_response_read(&latches) == 0xFFu);
            expect(!latches.irq4_asserted);
            printf("shell: frame=1 startup-response=0xFF irq4=handled\n");
        }
        if (frame == 1 || frame == 369 || frame == 411) {
            /* Observed $280A reads returning 0x00. Telemetry: the
             * transport meaning of an idle read is UNKNOWN, so the
             * latch is not driven here. */
            printf("shell: frame=%u command-read addr=0x280A data=0x00 "
                   "telemetry\n", frame);
        }
        if (frame == 407)
            expect(stunrun_adsp_upload_state_at(frame) ==
                   STUNRUN_ADSP_UPLOAD_EMPTY);
        if (frame == 408)
            expect(stunrun_adsp_upload_state_at(frame) ==
                   STUNRUN_ADSP_UPLOAD_POPULATING);
        if (frame == 411)
            expect(stunrun_adsp_upload_state_at(frame) ==
                   STUNRUN_ADSP_UPLOAD_COMPLETE);
        if (frame == 412) {
            expect(stunrun_jsa_main_response_read(&latches) ==
                   STUNRUN_JSA_NO_BYTE);
            expect(stunrun_adsp_install_frame_ready(frame));
            emitted = stunrun_adsp_emit(STUNRUN_ADSP_FIXTURE_INIT_STATE,
                                        image, STUNRUN_ADSP_INIT_STATE_WORDS);
            expect(emitted == STUNRUN_ADSP_INIT_STATE_WORDS);
            expect(image[0x0004u] == 0x001C780Fu);
            expect(image[0x0005u] == 0x001C834Fu);
            expect(image[STUNRUN_ADSP_MAILBOX_BOUNDARY] == 0x0018050Fu);
            stunrun_adsp_apply_dm_landmarks(dm);
            expect(dm[0x0955u] == 0x1242u);
            expect(dm[0x0956u] == 0x124Eu);
            expect(dm[0x0959u] == 0x7FFFu);
            expect(dm[0x095Au] == 0xFFFFu);
            printf("shell: frame=412 install-ready=1 image=init-state "
                   "landmarks=ok\n");
        }
        if (frame == 447) {
            /* Frame-447 command: 68010 bus event decodes to 0x1E, the
             * latch carries it across the NMI, the 6502 read gets 0x1E. */
            int byte = stunrun_jsa_command_byte(
                STUNRUN_JSA_FRAME447_BUS_DATA, STUNRUN_JSA_FRAME447_BUS_MASK);
            expect(byte == STUNRUN_JSA_FRAME447_BYTE);
            stunrun_jsa_main_command_write(&latches, (uint8_t)byte);
            expect(latches.nmi_asserted);
            expect(stunrun_jsa_sound_command_read(&latches) == 0x1Eu);
            expect(!latches.nmi_asserted);
            printf("shell: frame=447 command-byte=0x1E nmi=handled\n");
        }
        if (frame == g_terminal && g_terminal == 600u) {
            feed_control_contract(&tally);
            expect(stunrun_adsp_control_tally_matches(&tally));
            counts = stunrun_jsa_observed_title_counts();
            expect(stunrun_jsa_title_counts_match(&counts));
            fifo_summary = stunrun_fifo_observed_run();
            expect(stunrun_fifo_run_matches(&fifo_summary));
            control_state = "match";
            counts_state = "match";
            printf("shell: frame=600 control-tally=match "
                   "title-counts=match fifo-block=match\n");
        }

        /* Live video: scan out the GSP share the 68010 just wrote and present
         * it.  This is the only path that puts our OWN work on screen -- the
         * gsp-tms34010-display path below reads MAME dump files.  Strided so
         * 600 frames do not all rasterise when a coarse cadence is enough. */
        if (g_live_video && g_cpu != NULL &&
            (frame % g_live_video_stride) == 0) {
            /* Step the GSP RTL to process display lists from the MC68010 frame.
             * musashi and the GSP board share the same VRAM array, so the GSP's
             * pixel writes appear directly in musashi VRAM.
             * Guarded by STUNRUN_GSP_RTL (Verilator build only). */
            musashi_gsp_memory_t mem;
            if (musashi_cpu_gsp_memory(g_cpu, &mem) == 0) {
                GSP_RTL_SYNC_AND_STEP(mem);
            }
            if (render_live_gsp_frame() == 0) {
                g_live_video_presented++;
                render_mode = "live-gsp-share";
                present_to_backend();
#ifdef STUNRUN_GSP_GL_SCANOUT
                if (g_gl_scanout) {
                    musashi_gsp_memory_t mem;
                    stunrun_gsp_display_regs_t dregs;
                    if (get_live_gsp_state(&mem, &dregs) == 0 &&
                        stunrun_gsp_gl_scanout_present(
                            g_gl_scanout, mem.vram, MUSASHI_GSP_VRAM_WORDS,
                            mem.pal_lo, mem.pal_hi,
                            STUNRUN_GSP_PALETTE_ENTRIES, &dregs,
                            NULL, 0u) != 0) {
                        fprintf(stderr,
                                "shell: GL shader scanout failed at frame %u\n",
                                frame);
                    }
                }
#endif
            } else if (frame == 0) {
                printf("shell: live-video=unavailable\n");
                g_live_video = false;
            }
        }

        /* Frame pacing.  Emulating a frame costs far less than 1/60 s, so
         * without a cap the window is unwatchable.  Measured from the first
         * paced frame, not from process start, so the time spent loading ROMs
         * and building backends does not count against the budget. */
        if (g_live_video && g_live_video_fps > 0u && frame > 0u) {
            uint64_t budget_ns = 1000000000ull / g_live_video_fps;
            if (g_live_video_start_ns == 0u)
                g_live_video_start_ns = now_ns();
            uint64_t target = g_live_video_start_ns + budget_ns * frame;
            uint64_t now = now_ns();
            if (target > now) {
                uint64_t wait_ns = target - now;
                /* SDL_Delay is millisecond-granular, so only sleep once the
                 * remainder is worth a syscall; otherwise busy-yield. */
                if (wait_ns >= 2000000ull)
                    SDL_Delay((unsigned)((wait_ns + 999999ull) / 1000000ull));
                while (now_ns() < target)
                    ;
            }
        }

        /* Per-frame capture: PNG via stb_image_write.h.
         * Runs inside the loop so `frame` is correct (not loop-exit +1).
         * Only fires when STUNRUN_CAPTURE_FRAME=<N> matches current frame. */
        if (g_capture_frame != UINT32_MAX && frame == g_capture_frame) {
            /* Render the frame that was just simulated into the backend,
             * then capture it.  The renderer writes to renderer.pixels. */
            if (g_live_video && g_cpu != NULL)
                render_live_gsp_frame();
            if (g_backend_active) {
                if (g_backend_pair.video.set_palette && render_mode != NULL &&
                    strcmp(render_mode, "gsp-visible-state") == 0) {
                    stunrun_video_palette_t vp;
                    stunrun_video_palette_init(&vp, 256, video.palette_rgb);
                    g_backend_pair.video.set_palette(
                        g_backend_pair.video.user, &vp);
                }
                if (g_backend_pair.video.present)
                    g_backend_pair.video.present(g_backend_pair.video.user);
            }
            {
                char path[512];
                int n = snprintf(path, sizeof(path), "%s/frame_%04u.png",
                                 g_capture_dir, frame);
                if (n > 0 && n < (int)sizeof(path)) {
                    mkpath(g_capture_dir, 0755);
                    if (stunrun_render_write_png(&renderer, path))
                        printf("shell: capture frame=%u path=%s\n", frame, path);
                    else
                        printf("shell: capture frame=%u path=%s FAILED\n",
                               frame, path);
                }
            }
        }
    }

    if (g_terminal != 600u)
        printf("shell: frame=%u full-contract=skipped terminal!=600\n",
               g_terminal);

    /* Second IRQ4 response walkthrough. Two entries are observed per
     * 600-frame run but exact frames are not established, so this is
     * order-only and carries no frame attribution. The byte is a
     * mechanism-test placeholder: observed response values beyond the
     * startup 0xFF are not established. */
    stunrun_jsa_sound_response_write(&latches, 0xA5u);
    expect(latches.irq4_asserted);
    expect(stunrun_jsa_main_response_read(&latches) == 0xA5u);
    expect(!latches.irq4_asserted);
    printf("shell: response-pair-2 irq4=handled unpinned\n");

    g_pending = 0;
    for (i = 0; i < g_event_count; i++) {
        if (g_events[i].frame > g_terminal)
            g_pending++;
    }

    /* Rendering accepts either the captured whole-VRAM bridge or the
     * evidence-backed text-cursor fixture. Neither path supplies game state.
     *
     * Do NOT begin a frame here if the live path already rendered one.  The
     * live path draws inside the frame loop; stunrun_render_begin() floods
     * every pixel with the background colour, so calling it afterwards threw
     * the frame away and left the buffer black.  Nothing redrew it in a live
     * run (the captured-VRAM and text-fixture paths below are inert without
     * their inputs), so the `render frame=... hash=...` line was reporting a
     * hash of a black buffer -- identical for every run, every origin, every
     * amount of GSP output.  Three runs with visibly different pictures
     * printed the same hash, which is how this was found.
     *
     * A hash that cannot change is worse than no hash: it looks like a
     * regression detector and is a constant. */
    if (g_live_video_presented == 0)
        stunrun_render_begin(&renderer, g_terminal, 0u, 0u, 0u);
    else
        renderer.frame = g_terminal;
    {
        int has_vram = gsp_vram_path != NULL && gsp_vram_path[0] != '\0';
        int has_rgb = gsp_palette_path != NULL && gsp_palette_path[0] != '\0';
        int has_low = gsp_palette_low_path != NULL &&
                      gsp_palette_low_path[0] != '\0';
        int has_high = gsp_palette_high_path != NULL &&
                       gsp_palette_high_path[0] != '\0';
        int has_text_table = gsp_text_table_path != NULL &&
                             gsp_text_table_path[0] != '\0';
        int has_text_words = gsp_text_words_path != NULL &&
                             gsp_text_words_path[0] != '\0';
        int has_text_records = gsp_text_record_path != NULL &&
                               gsp_text_record_path[0] != '\0';
        int has_road_strip = road_strip_fixture != NULL &&
                             strcmp(road_strip_fixture, "1") == 0;
        int has_any_text = has_text_table || has_text_words || has_text_records;
        const char *gsp_display_flag = getenv("STUNRUN_GSP_DISPLAY");
        int use_gsp_display = gsp_display_flag != NULL &&
                              strcmp(gsp_display_flag, "0") != 0 &&
                              has_vram && has_low && has_high;
        if (has_road_strip &&
            (has_vram || has_rgb || has_low || has_high || has_any_text)) {
            printf("shell: road-strip fixture excludes other render inputs\n");
            g_failures++;
        } else if (has_road_strip) {
            stunrun_road_frame_t road_frame;
            stunrun_road_frame_fixture(&road_frame);
            if (!stunrun_render_road_frame(&renderer, &road_frame)) {
                printf("shell: road-strip fixture=error\n");
                g_failures++;
            } else {
                render_mode = "road-strip-fixture";
            }
        } else if (has_any_text && (!has_text_table ||
            (has_text_words == has_text_records) ||
            (has_text_table && (has_vram || has_rgb || has_low || has_high)))) {
            printf("shell: gsp-text requires table plus exactly one of words/records and excludes gsp-video\n");
            g_failures++;
        } else if (has_text_records) {
            if (!render_text_record_fixture(&renderer, gsp_text_table_path,
                                            gsp_text_record_path)) {
                printf("shell: gsp-text-record load=error\n");
                g_failures++;
            } else {
                render_mode = "gsp-text-record-fixture";
            }
        } else if (has_text_table) {
            if (!render_text_fixture(&renderer, gsp_text_table_path,
                                     gsp_text_words_path)) {
                printf("shell: gsp-text load=error\n");
                g_failures++;
            } else {
                render_mode = "gsp-text-cursor-fixture";
            }
        } else if (use_gsp_display) {
            /* The verified TMS34010 display path: 512 KiB share, 1024 pens,
             * row base (rowaddr << 10) masked to the share, DPYADR advancing
             * once per visible line. Reproduces MAME's own frame output when
             * fed a board dump the real GSP produced. This path reads pixels
             * that are ALREADY in VRAM -- the drawing is the TMS34010 core,
             * which does not exist yet. */
            uint16_t lo[STUNRUN_GSP_PALETTE_ENTRIES];
            uint16_t hi[STUNRUN_GSP_PALETTE_ENTRIES];
            uint8_t *vram_bytes = NULL;
            size_t vram_bytes_len = 0;
            stunrun_gsp_display_regs_t dregs;

            if (!read_whole_file(gsp_vram_path, &vram_bytes, &vram_bytes_len) ||
                vram_bytes_len < STUNRUN_GSP_VRAM_WORDS * 2u) {
                printf("shell: gsp-display needs a >=%u byte VRAM share (%s)\n",
                       (unsigned)(STUNRUN_GSP_VRAM_WORDS * 2u),
                       gsp_vram_path ? gsp_vram_path : "(unset)");
                free(vram_bytes);
                g_failures++;
            } else if (!read_palette_planes(gsp_palette_low_path,
                                            gsp_palette_high_path, lo, hi)) {
                printf("shell: gsp-display palette load=error\n");
                free(vram_bytes);
                g_failures++;
            } else {
                stunrun_gsp_display_frame600_regs(&dregs);
                if (stunrun_gsp_display_render(&renderer,
                                               (const uint16_t *)(const void *)vram_bytes,
                                               vram_bytes_len / 2u, lo, hi,
                                               STUNRUN_GSP_PALETTE_ENTRIES,
                                               &dregs) != 0) {
                    printf("shell: gsp-display render=error\n");
                    g_failures++;
                } else {
                    render_mode = "gsp-tms34010-display";

                    /* The backend palette contract is one 256-entry bank.
                     * Frame 600 selects bank zero, so the confirmed scanout
                     * can be passed as 8bpp indices without assigning meaning
                     * to any other GSP palette-bank behaviour.  Keep the RGB
                     * renderer above as the fallback for future captures that
                     * select another bank or exceed the seam's 8bpp contract. */
                    if (dregs.palbank == 0u) {
                        static uint16_t pens[STUNRUN_RENDER_WIDTH *
                                              STUNRUN_RENDER_HEIGHT];
                        size_t pixel_count = STUNRUN_RENDER_WIDTH *
                                              STUNRUN_RENDER_HEIGHT;
                        size_t pixel;
                        int indexed_ok =
                            stunrun_gsp_display_rasterise(
                                pens, pixel_count,
                                (const uint16_t *)(const void *)vram_bytes,
                                vram_bytes_len / 2u, &dregs) == 0 &&
                            stunrun_gsp_display_build_palette(
                                lo, hi, STUNRUN_GSP_PALETTE_ENTRIES,
                                g_gsp_palette_rgb,
                                sizeof(g_gsp_palette_rgb)) == 0;
                        if (indexed_ok) {
                            for (pixel = 0u; pixel < pixel_count; ++pixel) {
                                if (pens[pixel] > 0xFFu) {
                                    indexed_ok = 0;
                                    break;
                                }
                                g_gsp_indexed_pixels[pixel] =
                                    (uint8_t)pens[pixel];
                            }
                        }
                        g_gsp_indexed_backend_frame = indexed_ok != 0;
                        printf("shell: gsp-display indexed-scanout=%s\n",
                               indexed_ok ? "enabled" : "fallback-rgb");
                    }
                }
                free(vram_bytes);
            }
        } else if (!has_vram || (has_rgb && (has_low || has_high)) ||
            (!has_rgb && (has_low != has_high))) {
            if (has_vram || has_rgb || has_low || has_high) {
                printf("shell: gsp-video requires VRAM plus either RGB or both raw palette planes\n");
                g_failures++;
            }
        } else if (has_rgb || has_low) {
            int loaded = has_rgb ?
                stunrun_gsp_video_load(&video, gsp_vram_path,
                                       gsp_palette_path) :
                stunrun_gsp_video_load_palette_planes(
                    &video, gsp_vram_path, gsp_palette_low_path,
                    gsp_palette_high_path);
            if (!loaded) {
            printf("shell: gsp-video load=error\n");
            g_failures++;
            } else if (!stunrun_gsp_video_render(&video, &renderer)) {
            printf("shell: gsp-video render=error\n");
            g_failures++;
            } else {
            render_mode = "gsp-visible-state";
            }
        }
    }
    render_path = getenv("STUNRUN_RENDER_PPM");
    if (render_path != NULL && render_path[0] != '\0') {
        if (!stunrun_render_write_ppm(&renderer, render_path)) {
            printf("shell: render-output path=%s status=error\n", render_path);
            g_failures++;
        } else {
            printf("shell: render-output path=%s status=written\n", render_path);
        }
    }
    /* Window trace (STUNRUN_TRACE_WINDOW=lo:hi): same walk, but only prints
     * instructions landing in [lo,hi).  The boot code is dominated by
     * 230k-iteration fill loops, so an unfiltered trace is useless here. */
    if (g_trace_window[0] && g_cpu) {
        unsigned long lo = strtoul(g_trace_window, NULL, 0);
        const char *colon = strchr(g_trace_window, ':');
        unsigned long hi = colon ? strtoul(colon + 1, NULL, 0) : (lo + 0x100u);
        printf("shell: trace window %lx..%lx\n", lo, hi);
        musashi_cpu_trace_window(g_cpu, g_trace_count ? g_trace_count : 200u,
                                 (uint32_t)lo, (uint32_t)hi);
    }

    /* Boot trace (STUNRUN_TRACE=N): disassemble N instructions from the
     * current PC before the state dump, so we can see exactly where the
     * 68010 is sitting when the walk ends. */
    if (g_trace_count > 0u && g_cpu && !g_trace_window[0]) {
        printf("shell: trace %u instructions from PC=0x%08X\n",
               g_trace_count, musashi_cpu_get_pc(g_cpu));
        musashi_cpu_trace(g_cpu, g_trace_count);
    }

    /* Static disassembly (STUNRUN_DISASM=<start:end>:<path>).  Runs before
     * the flow log because both are pure reads of the same memory map. */
    if (g_disasm_path[0] && g_cpu) {
        FILE *df = fopen(g_disasm_path, "w");
        if (df) {
            fprintf(df, "; stunrunner MC68010 disassembly %06lx-%06lx\n",
                    g_disasm_start, g_disasm_end);
            size_t n = musashi_cpu_disasm_range(g_cpu, (uint32_t)g_disasm_start,
                                                (uint32_t)g_disasm_end, df);
            fclose(df);
            printf("shell: disasm %06lx-%06lx -> %s (%zu instructions)\n",
                   g_disasm_start, g_disasm_end, g_disasm_path, n);
        } else {
            printf("shell: disasm FAILED (cannot write %s)\n",
                   g_disasm_path);
            g_failures++;
        }
    }

    /* Control-flow inventory (STUNRUN_FLOW_LOG=<path>): write the set of
     * distinct call / branch / return targets the 68010 reached.  With no
     * symbols in the ROM, the distinct call targets are the closest thing to
     * a function list we can get, and they tell us which addresses are worth
     * disassembling next. */
    if (g_pc_hist_path[0]) {
        musashi_cpu_write_pc_histogram(g_cpu, g_pc_hist_path);
        printf("shell: pc histogram=%s\n", g_pc_hist_path);
    }
    if (g_flow_log_path[0] && g_cpu) {
        size_t edges = musashi_cpu_flow_log_write(g_flow_log_path);
        printf("shell: flow log=%s edges=%zu\n", g_flow_log_path, edges);
    }

    /* Machine-state dump (STUNRUN_DUMP_DIR).  Dumps main RAM, the GSP host
     * write log, and every GSP memory region the 68010 wrote to, so the
     * display list can be decoded offline.  Runs once, after the walk
     * finishes, so the state is whatever the terminal frame left behind.
     * An unset STUNRUN_DUMP_DIR means "do not dump", not "dump into the empty
     * path" — the latter failed the run on every invocation without it. */
    if (g_dump_dir[0] != '\0' && g_cpu) {
        if (musashi_cpu_dump_state(g_cpu, g_dump_dir) == 0) {
            musashi_gsp_region_counts_t counts;
            musashi_cpu_gsp_region_counts(g_cpu, &counts);
            /* The ordered bus stream is the replayable artifact, distinct from
             * gsp_writes which is the decoded HSTDAT result.  A dropped-event
             * count means the stream is incomplete, so say so loudly: a VRAM
             * mismatch against such a run is not evidence about the board
             * model. */
            size_t bus_events = musashi_cpu_gsp_bus_event_count(g_cpu);
            size_t bus_dropped = musashi_cpu_gsp_bus_event_dropped(g_cpu);
            printf("shell: dump dir=%s gsp_writes=%zu bus_events=%zu "
                   "bus_dropped=%zu vram=%u vram2bpp=%u "
                   "pal_lo=%u pal_hi=%u control=%u unmapped=%u\n",
                   g_dump_dir, musashi_cpu_gsp_write_count(g_cpu),
                   bus_events, bus_dropped,
                   counts.vram, counts.vram_2bpp, counts.pal_lo,
                   counts.pal_hi, counts.control, counts.unmapped);
            if (bus_dropped > 0) {
                printf("shell: WARNING bus-event stream INCOMPLETE "
                       "(%zu dropped) - replay and VRAM comparison against "
                       "this run are not conclusive\n", bus_dropped);
                g_failures++;
            }
            /* Text-log self-check.  `colour_miss` is the number that matters:
             * it counts arms that matched a descriptor base but whose HSTDAT
             * colour matched no descriptor there, which is what an arming rule
             * admitting a non-text draw would look like.  Ambiguous lines are
             * not an error -- 35 of the 394 descriptors share a base and three
             * share both base and colour, and the host port genuinely cannot
             * separate those. */
            {
                const char *tl = getenv("STUNRUN_TEXT_LOG");
                if (tl && tl[0] && strcmp(tl, "0") != 0) {
                    musashi_text_log_stats_t ts;
                    musashi_cpu_text_log_stats(&ts);
                    printf("shell: text-log arms=%lu matched=%lu fired=%lu "
                           "colour_miss=%lu ambiguous=%lu\n",
                           ts.arms, ts.matched, ts.fired, ts.colour_miss,
                           ts.ambiguous);
                }
            }
        } else {
            printf("shell: dump dir=%s FAILED\n", g_dump_dir);
            g_failures++;
        }
    }

    if (g_live_video_presented > 0) {
        printf("shell: live-video frames-presented=%u stride=%u "
               "source=live-gsp-share\n",
               g_live_video_presented, g_live_video_stride);
    }
#ifdef STUNRUN_GSP_RTL
    printf("shell: gsp-rtl host-window events seen=%lu forwarded=%lu "
           "no-ack=%lu  core_halted_now=%d\n",
           g_gsp_rtl_obs_seen, g_gsp_rtl_obs_fwd, g_gsp_rtl_obs_noack,
           gsp_rtl_driver_is_halted());
    printf("shell: gsp-rtl board host_writes=%llu vram_writes=%llu "
           "unmapped_writes=%llu word_reads=%llu word_writes=%llu\n",
           (unsigned long long)g_gsp_rtl_board.host_writes,
           (unsigned long long)g_gsp_rtl_board.vram_writes,
           (unsigned long long)g_gsp_rtl_board.unmapped_writes,
           (unsigned long long)g_gsp_rtl_board.word_reads,
           (unsigned long long)g_gsp_rtl_board.word_writes);
    printf("shell: gsp-rtl board vram nonzero words=%u\n",
           gsp_board_vram_nonzero_words(&g_gsp_rtl_board));
    printf("shell: gsp-rtl host HSTDAT=%lu (nonzero %lu)  HSTADR-nonzero=%lu  "
           "HSTCTL-nonzero=%lu  HLT-cleared=%lu\n",
           g_gsp_rtl_obs_hstdat, g_gsp_rtl_obs_hstdat_nz,
           g_gsp_rtl_obs_hstadr_nz, g_gsp_rtl_obs_hstctl_nz,
           g_gsp_rtl_obs_hstctl_rel);
    {
        uint16_t a=0,b=0,c=0,d=0;
        uint32_t seen;
        gsp_rtl_driver_get_display_regs(&a,&b,&c,&d);
        seen = gsp_rtl_driver_display_regs_seen();
        printf("shell: gsp-rtl shadow DPYSTRT=0x%04x DPYADR=0x%04x "
               "DPYTAP=0x%04x DPYCTL=0x%04x core-set=[%s%s%s%s]\n", a,b,c,d,
               (seen & 0x1u) ? "DPYSTRT " : "",
               (seen & 0x2u) ? "DPYADR " : "",
               (seen & 0x4u) ? "DPYTAP " : "",
               (seen & 0x8u) ? "DPYCTL" : "");
        if (seen == 0u)
            printf("shell: gsp-rtl note: the core has set NO display register; "
                   "every value above is this driver's initialiser, not evidence.\n");
    }
#endif

    {
        unsigned lit = 0u, colours = 0u;
        stunrun_render_content_stats(&renderer, &lit, &colours);
        printf("render frame=%u width=%u height=%u hash=0x%08X mode=%s "
               "lit=%u/%u colours=%u\n",
               renderer.frame, STUNRUN_RENDER_WIDTH, STUNRUN_RENDER_HEIGHT,
               (unsigned)stunrun_render_hash(&renderer), render_mode,
               lit, STUNRUN_RENDER_WIDTH * STUNRUN_RENDER_HEIGHT, colours);
        if (lit == 0u)
            printf("render note: the frame is ENTIRELY BLACK. A stable hash "
                   "here does not mean the scanout is working -- a scanout "
                   "pointed at a zero region of the share hashes just as "
                   "reproducibly as a correct one.\n");
    }

    /* Screenshot capture for g_capture_all: dump every rendered frame.
     * Per-frame capture (STUNRUN_CAPTURE_FRAME=N) is handled inside the
     * per-frame loop above so `frame` is accurate. */
    if (g_capture_all) {
        char path[512];
        int n = snprintf(path, sizeof(path), "%s/frame_%04u.png",
                         g_capture_dir, frame);
        if (n > 0 && n < (int)sizeof(path)) {
            mkpath(g_capture_dir, 0755);
            if (stunrun_render_write_png(&renderer, path))
                printf("shell: capture frame=%u path=%s\n", frame, path);
            else
                printf("shell: capture frame=%u path=%s FAILED\n",
                       frame, path);
        }
    }

    /* Live backend present: upload the completed frame and show it.  The
     * renderer writes RGB888, but the confirmed GSP display path can instead
     * pass its indexed 512×240 scanout plus palette through video.h. */
    if (g_backend_active && g_backend_pair.video.user) {
        stunrun_video_frame_t vf;
        int indexed_scanout =
            g_gsp_indexed_backend_frame && render_mode != NULL &&
            strcmp(render_mode, "gsp-tms34010-display") == 0;

        if (g_backend_pair.video.set_palette && indexed_scanout) {
            stunrun_video_palette_t vp;
            stunrun_video_palette_init(&vp, 256, g_gsp_palette_rgb);
            g_backend_pair.video.set_palette(g_backend_pair.video.user, &vp);
        } else if (g_backend_pair.video.set_palette &&
                   render_mode != NULL &&
                   strcmp(render_mode, "gsp-visible-state") == 0) {
            /* Palette is only valid for gsp-visible-state; for road-strip/text
             * fixtures the palette is all-zeros which is harmless. */
            stunrun_video_palette_t vp;
            stunrun_video_palette_init(&vp, 256, video.palette_rgb);
            g_backend_pair.video.set_palette(g_backend_pair.video.user, &vp);
        }

        if (indexed_scanout) {
            stunrun_video_frame_init_paletted_8bpp(
                &vf, g_gsp_indexed_pixels, STUNRUN_RENDER_WIDTH,
                STUNRUN_RENDER_HEIGHT, renderer.frame);
        } else {
            stunrun_video_frame_init(&vf, STUNRUN_RENDER_WIDTH,
                                    STUNRUN_RENDER_HEIGHT,
                                    STUNRUN_RENDER_WIDTH * 3,
                                    STUNRUN_VIDEO_FORMAT_RGB888,
                                    renderer.pixels);
        }

        if (g_backend_pair.video.upload &&
            g_backend_pair.video.upload(g_backend_pair.video.user, &vf)) {
            if (g_backend_pair.video.present)
                g_backend_pair.video.present(g_backend_pair.video.user);
        }
    }
    printf("game-loop frame=%u ticks=%u steering=%d trajectory=0x%04X "
           "motion-delta=%d\n",
           game_loop.frame, (unsigned)game_loop.global_tick,
           (int)game_loop.steering_delta,
           (unsigned)(uint16_t)game_loop.trajectory_coordinate,
           (int)game_loop.motion_delta);
    printf("checkpoint frames=%u time-us=%u events=%u pending=%u "
           "input-active=%u input-hash=0x%08X upload=%s "
           "install_ready=%d control=%s counts=%s nmi=%d irq4=%d\n",
           g_terminal, (unsigned)(((uint64_t)g_terminal * 1000000u) / 60u),
           g_dispatched, g_pending, input_active_count(),
           (unsigned)g_input_hash,
           upload_name(stunrun_adsp_upload_state_at(g_terminal)),
           stunrun_adsp_install_frame_ready(g_terminal),
           control_state, counts_state,
           latches.nmi_asserted ? 1 : 0,
           latches.irq4_asserted ? 1 : 0);
    emit_model_checkpoint(image, STUNRUN_ADSP_INIT_STATE_WORDS);
    stunrun_gsp_video_free(&video);

    /* Shut down backends, destroy window, and shut down SDL after the final frame */
    if (g_cpu) {
        musashi_cpu_destroy(g_cpu);
        g_cpu = NULL;
        g_mc68010_active = false;
    }
    if (g_sound_dispatch_active) {
        printf("shell: sound-dispatch stats frames=%llu commands=%llu "
               "oki-bytes=%llu samples-enqueued=%llu sample-rate=%u\n",
               (unsigned long long)g_sound_dispatch.frames_dispatched,
               (unsigned long long)g_sound_dispatch.commands_dispatched,
               (unsigned long long)g_sound_dispatch.oki_bytes_written,
               (unsigned long long)g_sound_dispatch.samples_enqueued,
               (unsigned)oki6295_sample_rate(&g_sound_dispatch.oki));
        sound_dispatch_shutdown(&g_sound_dispatch);
        g_sound_dispatch_active = false;
    }
#ifdef STUNRUN_GSP_GL_SCANOUT
    if (g_gl_scanout) {
        stunrun_gsp_gl_scanout_destroy(g_gl_scanout);
        g_gl_scanout = NULL;
    }
    if (g_gl_scanout_window) {
        SDL_DestroyWindow(g_gl_scanout_window);
        g_gl_scanout_window = NULL;
    }
#endif
    if (g_backend_active) {
        stunrun_backend_pair_shutdown(&g_backend_pair);
        g_backend_active = false;
    }
    if (g_window) {
        SDL_DestroyWindow(g_window);
        g_window = NULL;
    }
    SDL_Quit();

    if (g_failures == 0)
        printf("RESULT PASS\n");
    else
        printf("RESULT FAIL failures=%d\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}

int main(int argc, char **argv)
{
    static stunrun_experiment_t exp;
    stunrun_exp_error_t err;
    if (argc == 1)
        return run_walk();
    if (argc != 2) {
        fprintf(stderr, "usage: %s [experiment.json]\n", argv[0]);
        return 2;
    }
    err = stunrun_experiment_parse(argv[1], &exp);
    if (err != STUNRUN_EXP_OK) {
        fprintf(stderr, "shell: experiment parse failed: %s\n",
                stunrun_experiment_error_string(err));
        return 2;
    }
    if (exp.expect_kind != STUNRUN_EXP_EXPECT_FRAME) {
        fprintf(stderr, "shell: unsupported terminal kind "
                "(bounded_observation needs a Verifier harness)\n");
        return 2;
    }
    g_terminal = exp.terminal_frame;
    g_events = exp.events;
    g_event_count = exp.event_count;
    printf("shell: experiment id=%s start=%s events=%u terminal=%u\n",
           exp.id, exp.start, exp.event_count, exp.terminal_frame);
    return run_walk();
}
