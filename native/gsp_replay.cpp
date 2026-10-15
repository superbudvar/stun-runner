/* Standalone app mode for frame-ordered MAME GSP host-traffic replay. */
#include "gsp_rtl_driver.h"
extern "C" {
#include "gsp_display.h"
#include "render.h"
}
#include "backend/backend.h"
#include <SDL2/SDL.h>

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <map>
#include <regex>
#include <string>
#include <vector>

namespace {
static gsp_board_t board;
static stunrun_renderer_t renderer;

/* MAME configures the MC68010 at HARDDRIV_MASTER_CLOCK/4 = 8 MHz and the
 * TMS34010 at HARDDRIV_GSP_CLOCK = 48 MHz (harddriv.cpp:1463,1483;
 * harddriv.h:38-39), so one captured main-CPU frame budget of 132886 cycles
 * advances the GSP by 6x that amount. The live shell's 5:1 approximation is
 * not used by oracle replay. */
static constexpr uint64_t MAME_GSP_CYCLES_PER_CPU_FRAME = 132886ULL * 6ULL;

struct oracle_display_t {
    uint16_t dpyadr;
    uint16_t dpytap;
    uint16_t dpystart;
    uint16_t dpyctl;
    unsigned heblnk;
    unsigned hsblnk;
    unsigned fine_scroll;
    uint64_t time_ns;
};

bool json_uint(const std::string &object, const char *name, unsigned *value)
{
    const std::regex pattern(std::string("\\\"") + name +
                             "\\\"\\s*:\\s*(\\d+)");
    std::smatch match;
    if (!std::regex_search(object, match, pattern)) return false;
    *value = static_cast<unsigned>(std::strtoul(match[1].str().c_str(), nullptr, 10));
    return true;
}

bool read_oracle_display_snapshots(
    const char *path, std::map<unsigned, oracle_display_t> *snapshots)
{
    std::ifstream file(path);
    if (!file) {
        std::fprintf(stderr, "gsp-replay: cannot open display snapshot %s\n", path);
        return false;
    }
    const std::string json((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
    const std::regex snapshot_re(
        R"(\{\s*"frame"\s*:\s*(\d+)[\s\S]*?"display"\s*:\s*\{([^}]*)\})");
    for (std::sregex_iterator it(json.begin(), json.end(), snapshot_re), end;
         it != end; ++it) {
        const unsigned frame = static_cast<unsigned>(
            std::strtoul((*it)[1].str().c_str(), nullptr, 10));
        const std::string display = (*it)[2].str();
        unsigned dpyadr, dpytap, dpystart, dpyctl, heblnk, hsblnk, fine_scroll;
        if (!json_uint(display, "dpyadr", &dpyadr) ||
            !json_uint(display, "dpytap", &dpytap) ||
            !json_uint(display, "dpystart", &dpystart) ||
            !json_uint(display, "dpyctl", &dpyctl) ||
            !json_uint(display, "heblnk", &heblnk) ||
            !json_uint(display, "hsblnk", &hsblnk) ||
            !json_uint(display, "fine_scroll", &fine_scroll) ||
            dpyadr > 0xFFFFu || dpytap > 0xFFFFu || dpystart > 0xFFFFu ||
            dpyctl > 0xFFFFu) {
            std::fprintf(stderr,
                "gsp-replay: malformed/unsupported display snapshot at frame %u\n",
                frame);
            return false;
        }
        (*snapshots)[frame] = {
            static_cast<uint16_t>(dpyadr), static_cast<uint16_t>(dpytap),
            static_cast<uint16_t>(dpystart), static_cast<uint16_t>(dpyctl),
            heblnk, hsblnk, fine_scroll, 0u
        };
    }
    return !snapshots->empty();
}

bool write_blob(const std::filesystem::path &path, const uint8_t *bytes,
                size_t length)
{
    FILE *file = std::fopen(path.string().c_str(), "wb");
    if (!file) return false;
    const size_t written = std::fwrite(bytes, 1, length, file);
    const int close_result = std::fclose(file);
    return written == length && close_result == 0;
}

bool parse_event(const char *line, unsigned long long *seq, unsigned *frame,
                 char *kind, unsigned *address, unsigned *data,
                 unsigned *mask, unsigned *destination,
                 bool *has_timestamp, uint64_t *time_ns)
{
    unsigned pc, a0, a1, a2;
    double event_time_ns = 0.0;
    char extra;
    int n = std::sscanf(line, "%llu %u %15s %x %x %x %x %x %x %x %x %lf %c",
        seq, frame, kind, address, data, mask, destination, &pc, &a0, &a1,
        &a2, &event_time_ns, &extra);
    *has_timestamp = n == 12;
    if (n != 11 && n != 12) return false;
    if (*has_timestamp) {
        if (!std::isfinite(event_time_ns) || event_time_ns < 0.0) return false;
        *time_ns = static_cast<uint64_t>(std::llround(event_time_ns));
    }
    return true;
}

bool host_reg_from_event(const char *kind, unsigned address, unsigned *reg)
{
    if (!std::strcmp(kind, "HSTADRL") && address == 0x00C00004u)
        *reg = GSP_BOARD_HSTADRL;
    else if (!std::strcmp(kind, "HSTADRH") && address == 0x00C00002u)
        *reg = GSP_BOARD_HSTADRH;
    else if (!std::strcmp(kind, "HSTDAT") && address == 0x00C0000Cu)
        *reg = GSP_BOARD_HSTDAT;
    else if (!std::strcmp(kind, "HSTCTL") && address == 0x00C00008u)
        *reg = GSP_BOARD_HSTCTL;
    else
        return false;
    return true;
}

bool stage_host_event(gsp_board_t *target, const char *kind, unsigned address,
                      unsigned data, unsigned mask)
{
    unsigned reg;
    if (data > 0xFFFFu || mask != 0xFFFFu ||
        !host_reg_from_event(kind, address, &reg))
        return false;
    gsp_board_host_write(target, address - 0x00C00000u,
                         static_cast<uint16_t>(data));
    return true;
}

bool read_summary_count(const char *capture_dir, unsigned long long *stored,
                        unsigned long long *dropped,
                        unsigned long long *sequence,
                        unsigned long long *release_seq,
                        unsigned *capture_frames,
                        bool *has_timestamps,
                        uint64_t *end_time_ns)
{
    const std::string path = std::string(capture_dir) + "/result.json";
    std::ifstream input(path);
    if (!input) {
        std::fprintf(stderr, "gsp-replay: missing capture manifest %s\n",
                     path.c_str());
        return false;
    }
    const std::string json((std::istreambuf_iterator<char>(input)),
                           std::istreambuf_iterator<char>());
    if (json.find("\"schema\": \"stunrun-gsp-program-upload-result/v2\"") !=
        std::string::npos) {
        *has_timestamps = true;
    } else if (json.find("\"schema\": \"stunrun-gsp-program-upload-result/v1\"") !=
               std::string::npos) {
        *has_timestamps = false;
    } else {
        std::fprintf(stderr, "gsp-replay: unsupported capture manifest schema\n");
        return false;
    }
    const size_t key = json.find("\"summary\"");
    if (key == std::string::npos) return false;
    const size_t open = json.find('{', key);
    if (open == std::string::npos) return false;
    const size_t close = json.find('}', open);
    if (close == std::string::npos) return false;
    const std::string summary = json.substr(open, close - open + 1);
    auto field = [&](const char *name, unsigned long long *value) {
        const std::regex pattern(std::string("\\\"") + name +
                                 "\\\"\\s*:\\s*(\\d+)");
        std::smatch match;
        if (!std::regex_search(summary, match, pattern)) return false;
        *value = std::strtoull(match[1].str().c_str(), nullptr, 10);
        return true;
    };
    unsigned long long frames;
    if (!field("frames", &frames) ||
        !field("stored", stored) || !field("dropped", dropped) ||
        !field("seq", sequence) || !field("release_seq", release_seq))
        return false;
    *capture_frames = static_cast<unsigned>(frames);
    if (*has_timestamps) {
        unsigned long long end_ns;
        if (!field("end_time_ns", &end_ns)) return false;
        *end_time_ns = static_cast<uint64_t>(end_ns);
    }
    return true;
}

int run(const char *capture_dir)
{
    std::map<unsigned, oracle_display_t> oracle_displays;
    const char *oracle_state = std::getenv("STUNRUN_REPLAY_DISPLAY_SNAPSHOT");
    if (oracle_state && !read_oracle_display_snapshots(oracle_state,
                                                       &oracle_displays)) {
        std::fprintf(stderr, "gsp-replay: no usable display snapshots in %s\n",
                     oracle_state);
        return 2;
    }
    const std::string events_path = std::string(capture_dir) + "/gsp-upload-events.txt";
    unsigned long long expected_records = 0, dropped_records = 0;
    unsigned long long summary_sequence = 0;
    unsigned long long release_seq = 0;
    unsigned capture_frames = 0;
    bool has_timestamps = false;
    uint64_t capture_end_time_ns = 0;
    if (!read_summary_count(capture_dir, &expected_records, &dropped_records,
                            &summary_sequence, &release_seq, &capture_frames,
                            &has_timestamps,
                            &capture_end_time_ns) ||
        expected_records == 0 || summary_sequence != expected_records ||
        capture_frames == 0 ||
        dropped_records != 0 || release_seq == 0 ||
        release_seq > expected_records) {
        std::fprintf(stderr,
            "gsp-replay: capture manifest is invalid, incomplete, or truncated\n");
        return 2;
    }
    FILE *events = std::fopen(events_path.c_str(), "r");
    if (!events) {
        std::fprintf(stderr, "gsp-replay: cannot open %s: %s\n",
                     events_path.c_str(), std::strerror(errno));
        return 2;
    }
    gsp_board_reset(&board);
    std::vector<std::string> post_release_rows;
    char *line = nullptr;
    size_t line_cap = 0;
    unsigned long long expected_seq = 1, records = 0;
    unsigned last_frame = 0, staged_host_events = 0;
    unsigned release_frame = 0, release_data = 0;
    uint64_t last_input_time_ns = 0, release_time_ns = 0;
    bool have_input_time = false;
    bool reset_released = false, saw_reset = false, saw_release = false;
    bool malformed = false;

    /* Replay the upload into the board model only. At the oracle's HLT-clear
     * boundary the share already contains the complete uploaded program; the
     * GSP has been held. Do not spend hundreds of thousands of RTL host-port
     * handshakes reproducing that setup. Initialize the core from that state,
     * restore HSTADR/HSTCTL, then let the recorded post-release traffic govern
     * the autonomous run. */
    while (getline(&line, &line_cap, events) >= 0) {
        unsigned long long seq = 0;
        unsigned frame, address, data, mask, destination;
        uint64_t event_time_ns = 0;
        bool event_has_timestamp = false;
        char kind[16];
        if (!parse_event(line, &seq, &frame, kind, &address, &data, &mask,
                         &destination, &event_has_timestamp, &event_time_ns) ||
            event_has_timestamp != has_timestamps ||
            (has_timestamps && have_input_time &&
             event_time_ns < last_input_time_ns) || seq != expected_seq ||
            (records && frame < last_frame)) {
            std::fprintf(stderr,
                "gsp-replay: malformed/out-of-order event at record %llu (expected seq %llu)\n",
                records + 1, expected_seq);
            malformed = true;
            break;
        }
        ++expected_seq;
        ++records;
        last_frame = frame;
        if (has_timestamps) {
            last_input_time_ns = event_time_ns;
            have_input_time = true;
        }
        if (seq < release_seq) {
            if (!std::strcmp(kind, "GSPRES")) {
                if (destination > 1u) { malformed = true; break; }
                reset_released = destination == 0u;
                saw_reset = true;
            } else if (stage_host_event(&board, kind, address, data, mask)) {
                ++staged_host_events;
            } else {
                malformed = true;
                break;
            }
        } else if (seq == release_seq) {
            unsigned reg;
            if (std::strcmp(kind, "HSTCTL") != 0 ||
                !host_reg_from_event(kind, address, &reg) ||
                data > 0xFFFFu || mask != 0xFFFFu || (data & 0x8000u)) {
                malformed = true;
                break;
            }
            release_frame = frame;
            release_data = data;
            if (has_timestamps) release_time_ns = event_time_ns;
            saw_release = true;
        } else {
            if (!std::strcmp(kind, "GSPRES")) {
                if (destination > 1u) { malformed = true; break; }
            } else {
                unsigned reg;
                if (data > 0xFFFFu || mask != 0xFFFFu ||
                    !host_reg_from_event(kind, address, &reg)) {
                    malformed = true;
                    break;
                }
            }
            post_release_rows.emplace_back(line);
        }
    }
    free(line);
    if (ferror(events) || malformed || records != expected_records ||
        !saw_release || !saw_reset || !reset_released ||
        !(board.hstctl & 0x8000u)) {
        std::fprintf(stderr,
            "gsp-replay: invalid/incomplete capture or release boundary "
            "(records=%llu expected=%llu reset=%d released=%d HLT=0x%04x)\n",
            records, expected_records, saw_reset, reset_released, board.hstctl);
        std::fclose(events);
        return 2;
    }
    std::fclose(events);
    const char *stage_vram_path = std::getenv("STUNRUN_REPLAY_STAGE_VRAM");
    if (stage_vram_path && stage_vram_path[0] &&
        !write_blob(stage_vram_path, board.vram, GSP_BOARD_VRAM_BYTES)) {
        std::fprintf(stderr, "gsp-replay: failed to write staged VRAM %s\n",
                     stage_vram_path);
        gsp_rtl_driver_shutdown();
        return 2;
    }
    if (stage_vram_path && stage_vram_path[0])
        std::printf("gsp-replay: staged pre-release VRAM captured at %s\n",
                    stage_vram_path);
    /* Unlike the live-shell optimization, replay must not infer a STOP from
     * host-port silence: the capture supplies frame boundaries, so clock the
     * core for the full interval by default. A caller can still explicitly
     * select an experimental early-idle limit in the environment. */
    if (!std::getenv("STUNRUN_GSP_IDLE_CYCLES"))
        setenv("STUNRUN_GSP_IDLE_CYCLES", "0", 0);
    gsp_rtl_driver_init_replay(&board);
    if (gsp_rtl_driver_set_reset(reset_released ? 1 : 0) != 0 ||
        gsp_rtl_driver_host_write(GSP_BOARD_HSTADRH,
                                  (uint16_t)(board.hstadr >> 16)) != 0 ||
        gsp_rtl_driver_host_write(GSP_BOARD_HSTADRL,
                                  (uint16_t)board.hstadr) != 0 ||
        gsp_rtl_driver_host_write(GSP_BOARD_HSTCTL, board.hstctl) != 0 ||
        gsp_rtl_driver_host_write(GSP_BOARD_HSTCTL,
                                  (uint16_t)release_data) != 0) {
        std::fprintf(stderr, "gsp-replay: could not restore GSP release state\n");
        gsp_rtl_driver_shutdown();
        return 2;
    }
    stunrun_backend_pair_t backends{};
    SDL_Window *window = nullptr;
    bool video = true;
    const char *live = std::getenv("STUNRUN_LIVE_VIDEO");
    if (live && std::strcmp(live, "0") == 0) video = false;
    if (video && SDL_Init(SDL_INIT_VIDEO) == 0) {
        window = SDL_CreateWindow("STUN Runner — GSP capture replay",
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            STUNRUN_RENDER_WIDTH * 2, STUNRUN_RENDER_HEIGHT * 2,
            SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);
    } else if (video) {
        std::fprintf(stderr, "gsp-replay: SDL video unavailable: %s\n", SDL_GetError());
    }
    if (window) {
        setenv("STUNRUN_VIDEO_BACKEND", "sdl_opengl", 0);
    } else {
        setenv("STUNRUN_VIDEO_BACKEND", "null", 1);
    }
    setenv("STUNRUN_AUDIO_BACKEND", "null", 1);
    if (!stunrun_backend_pair_init(&backends, window,
            STUNRUN_RENDER_WIDTH, STUNRUN_RENDER_HEIGHT,
            STUNRUN_RENDER_WIDTH * 2, STUNRUN_RENDER_HEIGHT * 2, 48000)) {
        std::fprintf(stderr, "gsp-replay: video backend initialization failed\n");
        if (window) SDL_DestroyWindow(window);
        if (SDL_WasInit(0)) SDL_Quit();
        return 2;
    }

    const char *observations_path = std::getenv("STUNRUN_REPLAY_OUTPUTS");
    if (!observations_path) observations_path = "gsp-replay-outputs.log";
    FILE *observations = std::fopen(observations_path, "w");
    if (!observations) {
        std::perror(observations_path);
        return 2;
    }
    std::string deltas_path = std::string(observations_path) + ".vram-deltas.txt";
    FILE *deltas = std::fopen(deltas_path.c_str(), "w");
    if (!deltas) {
        std::perror(deltas_path.c_str());
        std::fclose(observations);
        return 2;
    }
    std::fprintf(deltas,
        "# GSP-only VRAM byte changes between frame-ordered RTL steps: frame offset old new\n");
    std::fprintf(observations,
        "# post-release replay; timing=%s; MAME event time converted at 48MHz when present\n"
        "# frame, GSP-only word-write delta, GSP-only VRAM-write delta, HINT asserts, clears, asserted\n",
        has_timestamps ? "MAME-time-ns" : "frame-sequence-legacy");
    const uint64_t baseline_word_writes = board.word_writes;
    const uint64_t baseline_vram_writes = board.vram_writes;
    auto render_checkpoint = [&](unsigned frame, const char *path) -> bool {
        stunrun_gsp_display_regs_t checkpoint_regs;
        stunrun_gsp_display_frame600_regs(&checkpoint_regs);
        uint16_t dpystrt, dpyadr, dpytap, dpyctl;
        const uint32_t seen = gsp_rtl_driver_display_regs_seen();
        gsp_rtl_driver_get_display_regs(&dpystrt, &dpyadr, &dpytap, &dpyctl);
        std::fprintf(observations,
            "# display frame=%u DPYSTRT=%04x DPYADR=%04x DPYTAP=%04x "
            "DPYCTL=%04x core-set=%x GSP_PC=%08x\n",
            frame, dpystrt, dpyadr, dpytap, dpyctl, seen,
            gsp_rtl_driver_get_pc());
        /* Never let the driver's illustrative frame-600 initializer masquerade
         * as GSP state. MAME's reset read is DPYSTRT=FFFF, DPYTAP=0,
         * DPYCTL=F010 (§30.4); DPYADR is timing-generator state and zero
         * engages the renderer's documented DPYSTRT fallback. */
        checkpoint_regs.dpystrt = (seen & 0x1u) ? dpystrt : 0xFFFFu;
        checkpoint_regs.dpyadr  = (seen & 0x2u) ? dpyadr : 0u;
        checkpoint_regs.dpytap  = (seen & 0x4u) ? dpytap : 0u;
        checkpoint_regs.dpyctl  = (seen & 0x8u) ? dpyctl : 0xF010u;
        const char *scanout_source = "rtl-shadow/fallback";
        const auto oracle = oracle_displays.find(frame);
        if (oracle != oracle_displays.end()) {
            /* DPYADR in the MAME snapshot is sampled mid-frame. MAME reloads
             * it from DPYSTRT at vblank, so start this full-frame raster at
             * DPYSTRT and let the shared scanout advance it. */
            checkpoint_regs.dpystrt = oracle->second.dpystart;
            checkpoint_regs.dpyadr = 0u;
            checkpoint_regs.dpytap = oracle->second.dpytap;
            checkpoint_regs.dpyctl = oracle->second.dpyctl;
            checkpoint_regs.heblnk = oracle->second.heblnk * 2u;
            checkpoint_regs.hsblnk = oracle->second.hsblnk * 2u;
            checkpoint_regs.finescroll = oracle->second.fine_scroll;
            scanout_source = "mame-snapshot";
        }
        std::fprintf(observations,
            "# raster frame=%u DPYSTRT=%04x DPYADR=%04x DPYTAP=%04x "
            "DPYCTL=%04x HEBLNK=%u HSBLNK=%u VEBLNK=%u VSBLNK=%u "
            "FINESCROLL=%u source=%s\n",
            frame, checkpoint_regs.dpystrt, checkpoint_regs.dpyadr,
            checkpoint_regs.dpytap, checkpoint_regs.dpyctl,
            checkpoint_regs.heblnk, checkpoint_regs.hsblnk,
            checkpoint_regs.veblnk, checkpoint_regs.vsblnk,
            checkpoint_regs.finescroll, scanout_source);
        uint16_t lo[STUNRUN_GSP_PALETTE_ENTRIES], hi[STUNRUN_GSP_PALETTE_ENTRIES];
        for (unsigned i = 0; i < STUNRUN_GSP_PALETTE_ENTRIES; ++i) {
            lo[i] = (uint16_t)(board.pal_lo[i*2] | (board.pal_lo[i*2+1] << 8));
            hi[i] = (uint16_t)(board.pal_hi[i*2] | (board.pal_hi[i*2+1] << 8));
        }
        renderer.frame = frame;
        if (stunrun_gsp_display_render(&renderer,
                reinterpret_cast<const uint16_t *>(board.vram),
                GSP_BOARD_VRAM_BYTES / 2, lo, hi, STUNRUN_GSP_PALETTE_ENTRIES,
                &checkpoint_regs) != 0 || !stunrun_render_write_png(&renderer, path))
            return false;
        stunrun_video_frame_t frame_data;
        stunrun_video_frame_init(&frame_data, STUNRUN_RENDER_WIDTH,
            STUNRUN_RENDER_HEIGHT, STUNRUN_RENDER_WIDTH * 3,
            STUNRUN_VIDEO_FORMAT_RGB888, renderer.pixels);
        if (backends.video.upload)
            (void)backends.video.upload(backends.video.user, &frame_data);
        if (backends.video.present) backends.video.present(backends.video.user);
        unsigned lit = 0, colours = 0;
        stunrun_render_content_stats(&renderer, &lit, &colours);
        printf("gsp-replay checkpoint frame=%u png=%s lit=%u colours=%u "
               "scanout=%s gsp_pc=%08x\n",
            frame, path, lit, colours, scanout_source,
            gsp_rtl_driver_get_pc());
        return true;
    };
    auto step_and_record = [&](unsigned frame, uint64_t cycles) {
        std::vector<uint8_t> before(board.vram, board.vram + GSP_BOARD_VRAM_BYTES);
        (void)gsp_rtl_driver_step(cycles);
        unsigned changed = 0;
        for (size_t i = 0; i < before.size(); ++i) {
            if (before[i] != board.vram[i]) {
                std::fprintf(deltas, "%u %06zx %02x %02x\n", frame, i,
                             before[i], board.vram[i]);
                changed++;
            }
        }
        std::fprintf(observations, "# frame=%u changed_vram_bytes=%u\n", frame, changed);
    };
    auto finish_frame = [&](unsigned frame) {
        uint32_t asserts, clears; int asserted;
        gsp_rtl_driver_hint_observation(&asserts, &clears, &asserted);
        std::fprintf(observations, "%u %llu %llu %u %u %d\n", frame,
            (unsigned long long)(board.word_writes - baseline_word_writes),
            (unsigned long long)(board.vram_writes - baseline_vram_writes),
            asserts, clears, asserted);
        if (frame % 30u == 0u) {
            char path[1024];
            const char *base = std::getenv("STUNRUN_REPLAY_PNG");
            if (!base) base = "gsp-replay-frame.png";
            std::snprintf(path, sizeof(path), "%s.frame-%04u.png", base, frame);
            if (!render_checkpoint(frame, path)) malformed = true;
        }
    };
    auto cycles_for_ns = [](uint64_t delta_ns) {
        /* 48 MHz = 48 GSP cycles per microsecond. Round only after scaling. */
        return (delta_ns * 48ULL + 500ULL) / 1000ULL;
    };
    unsigned current_frame = release_frame;
    last_frame = release_frame;
    uint64_t replay_time_ns = release_time_ns;
    unsigned host_events = 0, reset_events = 0;
    malformed = false;
    for (const std::string &row : post_release_rows) {
        unsigned long long seq = 0;
        unsigned frame, address, data, mask, destination;
        uint64_t event_time_ns = 0;
        bool event_has_timestamp = false;
        char kind[16];
        if (!parse_event(row.c_str(), &seq, &frame, kind, &address, &data,
                         &mask, &destination, &event_has_timestamp,
                         &event_time_ns) ||
            event_has_timestamp != has_timestamps || frame < last_frame ||
            (has_timestamps && event_time_ns < replay_time_ns)) {
            std::fprintf(stderr,
                "gsp-replay: malformed/out-of-order post-release event seq=%llu\n",
                seq);
            malformed = true;
            break;
        }
        if (frame != current_frame) {
            if (!has_timestamps) {
                step_and_record(current_frame,
                    (uint64_t)(frame - current_frame) *
                        MAME_GSP_CYCLES_PER_CPU_FRAME);
            } else {
                /* If there is an oracle screenshot/state at this exact frame,
                 * stop at its MAME timestamp before rendering. Otherwise the
                 * first event in the next frame is the best available cut. */
                uint64_t cut_ns = event_time_ns;
                const auto snap = oracle_displays.find(current_frame);
                if (snap != oracle_displays.end() &&
                    snap->second.time_ns >= replay_time_ns &&
                    snap->second.time_ns <= event_time_ns)
                    cut_ns = snap->second.time_ns;
                step_and_record(current_frame,
                                cycles_for_ns(cut_ns - replay_time_ns));
                replay_time_ns = cut_ns;
            }
            finish_frame(current_frame);
            last_frame = frame;
            current_frame = frame;
            if (malformed) break;
        }
        if (has_timestamps) {
            step_and_record(current_frame,
                            cycles_for_ns(event_time_ns - replay_time_ns));
            replay_time_ns = event_time_ns;
        }
        if (std::strcmp(kind, "GSPRES") == 0) {
            if (destination > 1u ||
                gsp_rtl_driver_set_reset(destination == 0u) != 0) malformed = true;
            reset_events++;
        } else if (std::strcmp(kind, "HSTADRL") == 0 ||
                   std::strcmp(kind, "HSTADRH") == 0 ||
                   std::strcmp(kind, "HSTDAT") == 0 ||
                   std::strcmp(kind, "HSTCTL") == 0) {
            unsigned reg;
            if (data > 0xFFFFu || mask != 0xFFFFu ||
                !host_reg_from_event(kind, address, &reg)) {
                malformed = true;
            }
            if (!malformed && gsp_rtl_driver_host_write(reg, (uint16_t)data) != 0)
                malformed = true;
            host_events++;
        } else {
            malformed = true;
        }
        if (malformed) break;
    }
    if (malformed) {
        std::fprintf(stderr,
            "gsp-replay: rejected invalid post-release replay\n");
        std::fclose(observations); std::fclose(deltas);
        stunrun_backend_pair_shutdown(&backends);
        if (window) SDL_DestroyWindow(window);
        if (SDL_WasInit(0)) SDL_Quit();
        gsp_rtl_driver_shutdown();
        return 2;
    }
    if (has_timestamps) {
        uint64_t target_time_ns = capture_end_time_ns;
        const auto snap = oracle_displays.find(current_frame);
        if (snap != oracle_displays.end() &&
            snap->second.time_ns >= replay_time_ns)
            target_time_ns = snap->second.time_ns;
        if (target_time_ns > replay_time_ns) {
            step_and_record(current_frame,
                            cycles_for_ns(target_time_ns - replay_time_ns));
            replay_time_ns = target_time_ns;
        }
        current_frame = capture_frames;
    } else {
        step_and_record(current_frame, 200000);
    }
    uint32_t asserts, clears; int asserted;
    gsp_rtl_driver_hint_observation(&asserts, &clears, &asserted);
    std::fprintf(observations, "%u %llu %llu %u %u %d\n", current_frame,
        (unsigned long long)(board.word_writes - baseline_word_writes),
        (unsigned long long)(board.vram_writes - baseline_vram_writes),
        asserts, clears, asserted);

    const char *image = std::getenv("STUNRUN_REPLAY_PNG");
    if (!image) image = "gsp-replay-frame.png";
    if (!render_checkpoint(current_frame, image)) {
        std::fprintf(stderr, "gsp-replay: renderer rejected final state or could not write %s\n", image);
        std::fclose(observations);
        std::fclose(deltas);
        stunrun_backend_pair_shutdown(&backends);
        if (window) SDL_DestroyWindow(window);
        if (SDL_WasInit(0)) SDL_Quit();
        gsp_rtl_driver_shutdown();
        return 2;
    }
    unsigned lit = 0, colours = 0;
    stunrun_render_content_stats(&renderer, &lit, &colours);
    printf("gsp-replay: staged-host=%u post-release-host=%u post-release-reset=%u "
           "frame=%u timing=%s\n",
        staged_host_events, host_events, reset_events, current_frame,
        has_timestamps ? "MAME-time-ns" : "frame-sequence-legacy");
    printf("gsp-replay: rendered %s lit=%u colours=%u rtl-word-writes=%llu vram-writes=%llu\n",
        image, lit, colours,
        (unsigned long long)(board.word_writes - baseline_word_writes),
        (unsigned long long)(board.vram_writes - baseline_vram_writes));
    printf("gsp-replay: HINT/INTOUT asserts=%u clears=%u currently=%d; host read responses absent from oracle capture\n",
        asserts, clears, asserted);
    uint64_t vclk_edges = 0;
    int vclk_enabled = 0;
    gsp_rtl_driver_video_clock_observation(&vclk_edges, &vclk_enabled);
    std::fprintf(observations, "# replay-vclk enabled=%d rising_edges=%llu\n",
                 vclk_enabled, (unsigned long long)vclk_edges);
    printf("gsp-replay: replay VCLK enabled=%d rising_edges=%llu\n",
           vclk_enabled, (unsigned long long)vclk_edges);
    for (unsigned slot = 0; slot < 2; ++slot) {
        gsp_rtl_word_watch_t watch{};
        gsp_rtl_driver_word_watch(slot, &watch);
        const uint32_t address = slot == 0 ? 0xFFF9FC00u : 0xFFFCFC00u;
        const uint16_t final_value = gsp_board_read16(&board, address);
        std::fprintf(observations,
            "# GSP_WORD addr=%08x reads=%llu last_read=%04x read_pc=%08x "
            "writes=%llu last_write=%04x write_pc=%08x final=%04x\n",
            address, (unsigned long long)watch.reads, watch.last_read,
            watch.last_read_pc, (unsigned long long)watch.writes,
            watch.last_write, watch.last_write_pc, final_value);
        std::printf("gsp-replay: GSP_WORD %08x reads=%llu last=%04x "
                    "writes=%llu last=%04x final=%04x\n",
            address, (unsigned long long)watch.reads, watch.last_read,
            (unsigned long long)watch.writes, watch.last_write, final_value);
    }
    const char *state_dir = std::getenv("STUNRUN_REPLAY_STATE_DIR");
    if (state_dir && state_dir[0]) {
        std::error_code ec;
        std::filesystem::create_directories(state_dir, ec);
        const std::filesystem::path dir(state_dir);
        const std::string frame = std::to_string(current_frame);
        if (ec ||
            !write_blob(dir / ("frame-" + frame + "-gsp_vram.bin"), board.vram,
                        GSP_BOARD_VRAM_BYTES) ||
            !write_blob(dir / ("frame-" + frame + "-gsp_pal_lo.bin"), board.pal_lo,
                        GSP_BOARD_PAL_BYTES) ||
            !write_blob(dir / ("frame-" + frame + "-gsp_pal_hi.bin"), board.pal_hi,
                        GSP_BOARD_PAL_BYTES)) {
            std::fprintf(stderr, "gsp-replay: failed to write board state under %s\n",
                         state_dir);
            std::fclose(observations);
            std::fclose(deltas);
            stunrun_backend_pair_shutdown(&backends);
            if (window) SDL_DestroyWindow(window);
            if (SDL_WasInit(0)) SDL_Quit();
            gsp_rtl_driver_shutdown();
            return 2;
        }
        std::printf("gsp-replay: captured VRAM+palette at frame %u under %s\n",
                    current_frame, state_dir);
    }
    std::fclose(observations);
    std::fclose(deltas);
    if (window) {
        bool running = true;
        while (running) {
            SDL_Event ev;
            while (SDL_PollEvent(&ev)) if (ev.type == SDL_QUIT) running = false;
            SDL_Delay(20);
        }
    }
    stunrun_backend_pair_shutdown(&backends);
    if (window) SDL_DestroyWindow(window);
    if (SDL_WasInit(0)) SDL_Quit();
    gsp_rtl_driver_shutdown();
    return 0;
}
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        std::fprintf(stderr, "usage: stunrun-gsp-replay <capture-directory>\n");
        return 2;
    }
    return run(argv[1]);
}
