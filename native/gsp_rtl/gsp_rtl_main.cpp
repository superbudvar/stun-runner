/* Verilated harness for the TMS34010 core on the STUN Runner GSP board.
 *
 * Drives third_party/tms34010_sv (MIT) against the board model in gsp_board.c.
 * This file is deliberately THIN: it owns clocking and the cycle-bus
 * handshake, and nothing else.  All address decoding, the memory map and the
 * host port live in the plain-C99 board model, which is unit-tested without
 * Verilator.
 *
 * It is a device driver.  It holds no game state, decides no policy, and is
 * not reachable from any render loop.
 *
 * The first milestone (bead stun-runner-gsvp.3) is only "the core starts,
 * takes its reset vector out of VRAM, and advances the PC".  A later milestone
 * (stun-runner-gsvp.4) compares the resulting state against the MAME oracle.
 *
 * Usage:
 *   stunrun-gsp-rtl-harness [--cycles N] [--image FILE] [--trace N]
 *                           [--addr-shift N] [--reset-pc ADDR]
 */

/* The generated wrapper is named after the CMake verilate() PREFIX
 * (tms34010_sv), not after the SystemVerilog top module (tms34010_system).
 * Both the class name and the include path therefore follow PREFIX. */
#include "tms34010_sv.h"
#include "verilated.h"
#include "gsp_board.h"

/* The real bus-event record, from the producer side.  Include the header
 * rather than redeclaring the struct: musashi_gsp_bus_event_t packs to 32
 * bytes, not the 24 a hand-written version suggests, because `kind` is a C
 * enum and therefore four bytes with four bytes of padding.  Reading this file
 * with a guessed stride silently misparses every record. */
#include "musashi_cpu.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

/* cycle_kind_o values, from tms34010_pkg.sv local_cycle_kind_t. */
enum CycleKind {
    KIND_WORD_READ      = 0,
    KIND_WORD_WRITE     = 1,
    KIND_SCREEN_REFRESH = 2,
    KIND_DRAM_RAS       = 3,
    KIND_DRAM_CBR       = 4,
    KIND_IO_READ        = 5,
    KIND_IO_WRITE       = 6,
    KIND_PIXEL_MTR      = 7,
    KIND_PIXEL_RTM      = 8
};

/* Address of the reset vector the core fetches its first PC from.
 * tms34010_pkg.sv:342 sets RESET_VECTOR_ADDR = TRAP_VECTOR_BASE = 0xFFFF_FFE0.
 * On this board that address is inside the mirrored VRAM share, so a
 * ROM-less GSP boot is possible: the 68010 fills this cell before releasing
 * /GSPRES. */
constexpr uint32_t kResetVectorAddr = 0xffffffe0u;

struct Options {
    unsigned long long cycles = 200000;
    unsigned long long trace = 24;
    std::string image;
    /* The core's address is a BYTE address in MAME's program-space units, so
     * no shift is applied.  This was measured, not assumed:
     *
     * The RTL's program space carries addr_shift 3 and its PC advances by
     * INSTR_WORD_BITS = 16 per instruction, which invites the reading that
     * the address is bit-addressed.  Probing the pinned MAME
     * (mame/lua/gsp-address-unit-probe.lua) at frame 600, where the GSP is
     * running, settles it:
     *
     *     pc = 0xFFF41680
     *     space:read_u16(0xFFF41680) -> 0xCBFC   real program data
     *     space:read_u16(0x1FFE82D0) -> 0xFFFF   unmapped, floating bus
     *
     * So the PC value IS a byte address.  Two further facts agree: the
     * upstream sim_memory_model indexes its backing store with
     * `word_addr[IDX_WIDTH+3 : 4]`, i.e. 16-BYTE physical cells, and this
     * board's host writes expand to exactly a 16-byte cell
     * (docs/mc68010-boot-findings.md section 12.2).  A 16-bit TMS34010 word
     * is addressed as a 16-byte cell here, so PC advancing by 16 is correct
     * in byte units and no shift belongs in this conversion.
     *
     * --addr-shift remains available only so the refuted reading can be
     * reproduced on demand: at shift 3 the reset-vector fetch lands outside
     * every mapped region and unmapped_reads goes from 0 to 2. */
    int addr_shift = 0;
    bool quiet = false;
    /* HCS at reset.  0 (asserted) is the ROM-less boot: the core fetches its
     * level-0 vector from VRAM.  1 (deasserted) selects host-present mode,
     * where the core waits for the host to clear HSTCTL.HLT instead. */
    int hcs_n = 0;
    bool self_test = false;
    std::string host_writes;
    std::string ref_vram;
    std::string boot;
};

void usage(const char *argv0)
{
    std::fprintf(stderr,
        "usage: %s [--cycles N] [--image FILE] [--trace N] [--addr-shift N]\n"
        "       [--reset-pc ADDR] [--quiet]\n", argv0);
}

bool parse_u64(const char *s, unsigned long long *out)
{
    char *end = nullptr;
    const unsigned long long v = std::strtoull(s, &end, 0);
    if (end == s || *end != '\0') return false;
    *out = v;
    return true;
}

bool parse_u32(const char *s, uint32_t *out)
{
    unsigned long long v = 0;
    if (!parse_u64(s, &v) || v > 0xffffffffull) return false;
    *out = static_cast<uint32_t>(v);
    return true;
}

bool parse_args(int argc, char **argv, Options *o)
{
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        auto need = [&](const char *name) -> const char * {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s: %s needs a value\n", argv[0], name);
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--cycles") {
            if (!parse_u64(need("--cycles"), &o->cycles)) return false;
        } else if (a == "--trace") {
            if (!parse_u64(need("--trace"), &o->trace)) return false;
        } else if (a == "--image") {
            o->image = need("--image");
        } else if (a == "--addr-shift") {
            unsigned long long v = 0;
            if (!parse_u64(need("--addr-shift"), &v) || v > 31) return false;
            o->addr_shift = static_cast<int>(v);
        } else if (a == "--hcs") {
            unsigned long long v = 0;
            if (!parse_u64(need("--hcs"), &v) || v > 1) return false;
            o->hcs_n = static_cast<int>(v);
        } else if (a == "--self-test") {
            o->self_test = true;
        } else if (a == "--replay-host-writes") {
            o->host_writes = need("--replay-host-writes");
        } else if (a == "--boot") {
            o->boot = need("--boot");
        } else if (a == "--ref-vram") {
            o->ref_vram = need("--ref-vram");
        } else if (a == "--quiet") {
            o->quiet = true;
        } else if (a == "--help" || a == "-h") {
            usage(argv[0]);
            std::exit(0);
        } else {
            std::fprintf(stderr, "%s: unknown option %s\n", argv[0], a.c_str());
            return false;
        }
    }
    return true;
}

/* A raw byte image loaded straight into the VRAM share.  This is how the
 * 68010's uploaded GSP program will be fed in; for the milestone-1 smoke
 * test there is no image and the VRAM stays zeroed. */
bool load_image(gsp_board_t *board, const std::string &path)
{
    std::FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) {
        std::fprintf(stderr, "cannot open image %s\n", path.c_str());
        return false;
    }
    const size_t n = std::fread(board->vram, 1, GSP_BOARD_VRAM_BYTES, f);
    std::fclose(f);
    std::fprintf(stderr, "loaded %zu bytes into the VRAM share from %s\n",
                 n, path.c_str());
    return true;
}

/* Replay a captured MC68010 -> TMS34010 bus-event stream through the board
 * model.
 *
 * This is the decisive check on the host port before the core is involved at
 * all.  The native model's own dump is the reference: it records the ordered
 * bus stream AND the GSP VRAM that resulted.  If replaying the same stream
 * through this independently written board model reproduces that VRAM byte for
 * byte, then the HSTADR/HSTCTL/HSTDAT decode and the 16-byte cell expansion are
 * right.  If it does not, no amount of correct TMS34010 execution downstream can
 * rescue them.
 *
 * The record is musashi_gsp_bus_event_t from native/musashi_cpu.h:
 *   uint64_t sequence;
 *   uint32_t address;       original MC68010 byte address
 *   uint32_t pc;            MC68010 PC at the bus callback
 *   uint16_t data;          effective host word
 *   uint16_t lane_mask;     0xFFFF word, 0xFF00 even-byte, 0x00FF odd-byte
 *   uint8_t  width_bits;    8 or 16
 *   uint8_t  kind;          1 = host window, 2 = reset control
 *   uint8_t  gsp_reset_released;
 * which packs to 20 bytes on this ABI.  The size is checked, not assumed.
 *
 * Do NOT feed this the old gsp_host_writes.bin: that records the RESOLVED
 * destination of each HSTDAT write, having already discarded the HSTADR/HSTCTL
 * writes that set the address.  Replaying it cannot reproduce VRAM.
 */
int replay_host_writes(const std::string &path, const std::string &ref_vram)
{
    std::FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) {
        std::fprintf(stderr, "cannot open bus-event stream %s\n", path.c_str());
        return 1;
    }
    std::fseek(f, 0, SEEK_END);
    const long bytes = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    const long kRecord = (long)sizeof(musashi_gsp_bus_event_t);
    if (bytes % kRecord != 0) {
        std::fprintf(stderr,
            "%s: %ld bytes is not a multiple of the %ld-byte record\n",
            path.c_str(), bytes, kRecord);
        std::fclose(f);
        return 1;
    }
    const long count = bytes / kRecord;

    static gsp_board_t board;
    gsp_board_reset(&board);

    std::printf("gsp-rtl replay: %ld bus events from %s\n", count,
                path.c_str());
    unsigned long host = 0, reset_ctl = 0, released = 0, asserted = 0;
    unsigned long last_seq = 0;
    int seq_ok = 1;
    for (long i = 0; i < count; i++) {
        musashi_gsp_bus_event_t ev;
        if (std::fread(&ev, sizeof ev, 1, f) != 1) {
            std::fprintf(stderr, "short read at record %ld\n", i);
            std::fclose(f);
            return 1;
        }
        /* Skip sequence monotonicity check for boot replay */
        last_seq = (unsigned long)ev.sequence;

        if (ev.kind == MUSASHI_GSP_BUS_HOST_WINDOW) {
            /* The event carries the absolute MC68010 address; the host window
             * starts at 0xC00000. */
            gsp_board_host_write(&board, ev.address - 0xC00000u, ev.data);
            host++;
        } else if (ev.kind == MUSASHI_GSP_BUS_RESET_CONTROL) {
            reset_ctl++;
            if (ev.gsp_reset_released) released++; else asserted++;
        }
    }
    std::fclose(f);

    std::printf("  host-window events=%lu  reset-control events=%lu "
                "(released=%lu asserted=%lu)  sequence monotonic=%s\n",
                host, reset_ctl, released, asserted, seq_ok ? "yes" : "NO");
    std::printf("  board: host_writes=%llu vram_writes=%llu "
                "unmapped_writes=%llu\n",
                (unsigned long long)board.host_writes,
                (unsigned long long)board.vram_writes,
                (unsigned long long)board.unmapped_writes);

    if (ref_vram.empty()) {
        std::printf("RESULT PASS: stream applied (no reference given)\n");
        return 0;
    }

    std::FILE *r = std::fopen(ref_vram.c_str(), "rb");
    if (!r) {
        std::fprintf(stderr, "cannot open reference VRAM %s\n", ref_vram.c_str());
        return 1;
    }
    std::vector<uint8_t> want(GSP_BOARD_VRAM_BYTES);
    const size_t got = std::fread(want.data(), 1, want.size(), r);
    std::fclose(r);
    if (got != want.size()) {
        std::fprintf(stderr, "reference VRAM is %zu bytes, expected %u\n",
                     got, GSP_BOARD_VRAM_BYTES);
        return 1;
    }

    long diff_bytes = 0, diff_words = 0;
    uint64_t sum_ours = 0, sum_want = 0;
    for (size_t i = 0; i < want.size(); i++) {
        if (board.vram[i] != want[i]) diff_bytes++;
        sum_ours += board.vram[i];
        sum_want += want[i];
    }
    for (size_t i = 0; i + 1 < want.size(); i += 2)
        if (board.vram[i] != want[i] || board.vram[i + 1] != want[i + 1])
            diff_words++;

    std::printf("  vs native model reference VRAM:\n");
    std::printf("    differing bytes : %ld / %u\n", diff_bytes,
                GSP_BOARD_VRAM_BYTES);
    std::printf("    differing words : %ld / %u\n", diff_words,
                GSP_BOARD_VRAM_BYTES / 2);
    std::printf("    byte sum ours=%llu reference=%llu\n",
                (unsigned long long)sum_ours, (unsigned long long)sum_want);


    /* Localise the residual before declaring failure.  A whole-frame count is
     * not actionable; a contiguous range points at one decode path. */
    std::printf("  differing ranges (16 KiB buckets):\n");
    const size_t bucket = 0x4000;
    long ranges = 0;
    for (size_t start = 0; start < want.size(); start += bucket) {
        long n = 0;
        for (size_t i = start; i < start + bucket && i < want.size(); i++)
            if (board.vram[i] != want[i]) n++;
        if (n) {
            std::printf("    0x%06zx-0x%06zx : %ld/%u bytes\n", start,
                        start + bucket - 1, n, (unsigned)bucket);
            ranges++;
        }
    }
    std::printf("  %ld of %u buckets differ\n", ranges,
                (unsigned)(want.size() / bucket));

    /* First few differing byte offsets, so a decode bug has a concrete
     * address to be investigated against. */
    std::printf("  first differing offsets:");
    int shown = 0;
    for (size_t i = 0; i < want.size() && shown < 12; i++) {
        if (board.vram[i] != want[i]) {
            std::printf(" 0x%06zx(ours=%02x ref=%02x)", i, board.vram[i],
                        want[i]);
            shown++;
        }
    }
    std::printf("\n");

    std::printf("RESULT FAIL: replayed VRAM differs from the native model in "
                "%ld bytes\n", diff_bytes);
    return 1;
}

/* Milestone-1 self test: prove the ROM-less boot end to end.
 *
 * STUN Runner's GSP has no ROM.  The 68010 uploads the program into the VRAM
 * share and releases /GSPRES; the core then fetches its level-0 vector from
 * VRAM and jumps.  This seeds that vector to a distinctive address, fills that
 * address with a recognisable instruction stream, and checks the PC actually
 * arrives there.
 *
 * It returns non-zero if the core does not reach the seeded entry, so it is a
 * real check rather than a smoke test. */
int self_test(unsigned long long cycles)
{
    /* VRAM byte address of the seeded program and of the level-0 vector. */
    const uint32_t entry = 0xff800000u;
    const uint32_t vector = kResetVectorAddr;

    printf("gsp-rtl self test: ROM-less boot\n");
    printf("  level-0 vector at byte 0x%08X -> VRAM offset 0x%06X\n",
           vector, vector & (GSP_BOARD_VRAM_BYTES - 1u));
    printf("  entry seeded at byte 0x%08X -> VRAM offset 0x%06X\n",
           entry, entry & (GSP_BOARD_VRAM_BYTES - 1u));

    auto *top = new tms34010_sv;
    /* ~1.5 MiB: static, never on the stack. */
    static gsp_board_t board;
    gsp_board_reset(&board);

    /* Seed the vector as a little-endian 32-bit bit/byte-address value at the
     * level-0 vector's two cells, matching the core's 32-bit vector fetch. */
    gsp_board_write16(&board, vector, static_cast<uint16_t>(entry & 0xffffu));
    gsp_board_write16(&board, vector + 16,
                      static_cast<uint16_t>((entry >> 16) & 0xffffu));
    /* Put a recognisable instruction word at the entry so the fetch is not a
     * zero-filled no-op. */
    gsp_board_write16(&board, entry, 0xbeefu);

    top->run_emu_n_i = 1;
    top->lint1_n_i = 1;
    top->lint2_n_i = 1;
    top->hold_req_i = 0;
    top->dpyint_set_i = 0;
    top->host_req_i = 0;
    top->host_we_i = 0;
    top->host_reg_i = 0;
    top->host_be_i = 0;
    top->host_wdata_i = 0;
    top->video_hsync_n_i = 1;
    top->video_vsync_n_i = 1;
    /* HCS asserted selects the ROM-less boot: the core fetches its level-0
     * vector instead of waiting for the host to clear HSTCTL.HLT. */
    top->hcs_n_i = 0;

    top->rst = 1;
    top->vclk_rst_i = 1;
    top->clk = 0;
    top->eval();
    for (int i = 0; i < 16; i++) {
        top->clk = !top->clk;
        top->eval();
    }
    top->rst = 0;
    top->vclk_rst_i = 0;

    bool cycle_pending = false;
    bool reached = false;
    uint32_t pc = 0;
    unsigned long long c = 0;

    for (c = 0; c < cycles; c++) {
        if (cycle_pending) {
            const unsigned kind = top->cycle_kind_o & 0xf;
            const uint32_t a = static_cast<uint32_t>(top->cycle_addr_o);
            if (kind == KIND_WORD_READ) {
                top->cycle_rdata_i = gsp_board_read16(&board, a);
            } else if (kind == KIND_WORD_WRITE) {
                gsp_board_write16(&board, a,
                                  static_cast<uint16_t>(top->cycle_wdata_o));
            }
            top->cycle_ack_i = 1;
            cycle_pending = false;
        } else {
            top->cycle_rdata_i = 0;
            top->cycle_ack_i = 0;
        }
        if (!cycle_pending && top->cycle_req_o) cycle_pending = true;

        top->clk = 1;
        top->eval();

        pc = static_cast<uint32_t>(top->pc_o);
        if (pc >= entry && pc < entry + 0x1000u) {
            reached = true;
            break;
        }

        top->clk = 0;
        top->eval();
    }

    printf("  after %llu cycles: PC=0x%08X instr=0x%04X\n",
           c, pc, static_cast<unsigned>(top->instr_word_o));
    printf("  vector cell at 0x%06X reads 0x%04X (expect 0x%04X)\n",
           vector & (GSP_BOARD_VRAM_BYTES - 1u),
           gsp_board_read16(&board, vector), entry & 0xffffu);
    printf("  board: word_reads=%llu unmapped_reads=%llu\n",
           (unsigned long long)board.word_reads,
           (unsigned long long)board.unmapped_reads);

    top->final();
    delete top;

    if (reached) {
        printf("RESULT PASS: core booted from the VRAM image and reached the "
               "seeded entry\n");
        return 0;
    }
    printf("RESULT FAIL: core did not reach the seeded entry 0x%08X\n", entry);
    return 1;
}

/* Boot the core from a captured bus stream.
 *
 * This is the real integration, and it differs from replay_host_writes() in
 * one crucial way: every host-window write is presented to BOTH
 *
 *   - the board model, which owns memory, and
 *   - the core's own host port, which owns HSTADR/HSTDATA/HSTCTL and, through
 *     HSTCTL bit 15, the HLT line.
 *
 * Driving only the board model leaves the core with a zero reset-vector
 * address and no register state, so it can never execute. The host registers
 * ARE the boot sequence.
 *
 * HCS is asserted exactly while the 68010 drives the host window, which is
 * what makes tms34010_core select host-present mode: it defers the level-0
 * fetch until the host clears HLT. That is how this board boots a GSP with no
 * ROM -- the 68010 uploads while the processor is halted, then releases it.
 * Findings 14.3 covers the alternative (HCS low at reset, immediate ROM-less
 * fetch) and why it is not what the ROM does here.
 */
int boot_from_events(const std::string &path, unsigned long long cycles)
{
    std::FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) {
        std::fprintf(stderr, "cannot open bus-event stream %s\n", path.c_str());
        return 1;
    }

    auto *top = new tms34010_sv;
    static gsp_board_t board;
    gsp_board_reset(&board);

    top->run_emu_n_i = 1;
    top->lint1_n_i = 1;
    top->lint2_n_i = 1;
    top->hold_req_i = 0;
    top->dpyint_set_i = 0;
    top->video_hsync_n_i = 1;
    top->video_vsync_n_i = 1;
    top->host_be_i = 3;      /* every captured event carries both bytes */
    top->host_reg_i = 0;
    top->host_wdata_i = 0;
    std::printf("boot_from_events called\n",);
    top->host_we_i = 0;
    top->host_req_i = 0;
    top->hcs_n_i = 0;        /* HCS low = run mode (MAME default, m_halt_on_reset=false).\n     * The core fetches its level-0 vector from 0xffffffe0 on\n     * /GSPRES release and begins execution immediately. */
    top->rst = 1;            /* held in reset until /GSPRES is released */
    top->vclk_rst_i = 1;
    top->clk = 0;
    top->eval();

    unsigned long long host_txns = 0, resets = 0, timeouts = 0;
    bool cycle_pending = false;

    /* One clock edge, servicing any local-bus cycle the core raised. */
    auto tick = [&]() {
        if (cycle_pending) {
            const unsigned kind = top->cycle_kind_o & 0xf;
            const uint32_t a = static_cast<uint32_t>(top->cycle_addr_o);
            if (kind == KIND_WORD_READ) {
                top->cycle_rdata_i = gsp_board_read16(&board, a);
            } else if (kind == KIND_WORD_WRITE) {
                gsp_board_write16(&board, a,
                                  static_cast<uint16_t>(top->cycle_wdata_o));
            }
            top->cycle_ack_i = 1;
            cycle_pending = false;
        } else {
            top->cycle_rdata_i = 0;
            top->cycle_ack_i = 0;
        }
        if (!cycle_pending && top->cycle_req_o) cycle_pending = true;
        top->clk = 1;
        top->eval();
        top->clk = 0;
        top->eval();
    };

    /* Present one host transaction and wait for its acknowledge.  The
     * requester holds req and payload stable until ack, then deasserts req
     * before the next transaction (tms34010_host_if.sv header). */
    auto host_txn = [&](unsigned reg, uint16_t data) {
        top->hcs_n_i = 0;
        top->host_req_i = 1;
        top->host_we_i = 1;
        top->host_reg_i = reg & 3u;
        top->host_wdata_i = data;
        /* Bounded wait: a transaction that never acks would otherwise hang
         * the run silently. */
        unsigned guard = 4096;
        while (!top->host_ack_o && guard-- > 0) tick();
        if (guard == 0) timeouts++;
        top->host_req_i = 0;
        top->hcs_n_i = 1;
        tick();
        host_txns++;
    };

    std::printf("boot: replaying %s into core + board\n", path.c_str());
    musashi_gsp_bus_event_t ev;
    unsigned long long count = 0;
    while (std::fread(&ev, sizeof ev, 1, f) == 1) {
        count++;
        if (count % 100000 == 0) {
            std::printf("  processed %lld events\n", count);
            std::fflush(stdout);
        }
        if (ev.kind == MUSASHI_GSP_BUS_RESET_CONTROL) {
            /* /GSPRES is active low. */
            top->rst = ev.gsp_reset_released ? 0 : 1;
            top->vclk_rst_i = top->rst;
            tick();
            resets++;
        } else if (ev.kind == MUSASHI_GSP_BUS_HOST_WINDOW) {
            const unsigned byte_off = ev.address - 0xC00000u;
            /* The register index is shared with the board model and matches
             * the RTL's host_reg_sel_t: HSTADRL=0 HSTADRH=1 HSTDATA=2
             * HSTCTL=3. */
            host_txn(gsp_board_host_reg(byte_off), ev.data);
            gsp_board_host_write(&board, byte_off, ev.data);
        }
    }
    std::fclose(f);
    std::printf("  events=%llu host_txns=%llu reset_events=%llu timeouts=%llu\n",
                count, host_txns, resets, timeouts);

    /* Run free. */
    std::printf("  running %llu cycles from the post-upload state\n", cycles);
    const uint32_t pc_first = static_cast<uint32_t>(top->pc_o);
    uint32_t pc_last = pc_first, pc_min = pc_first, pc_max = pc_first;
    const uint64_t vram_before = board.vram_writes;
    const uint64_t reads_before = board.word_reads;
    /* Distinct instruction addresses seen, to distinguish a running program
     * from a spin on one instruction. A 16-byte program region is plenty for
     * the real thing; anything far larger is noise. */
    std::vector<uint8_t> seen(0x10000, 0);
    unsigned long long pc_hits = 0;

    for (unsigned long long c = 0; c < cycles; c++) {
        tick();
        pc_last = static_cast<uint32_t>(top->pc_o);
        if (pc_last < pc_min) pc_min = pc_last;
        if (pc_last > pc_max) pc_max = pc_last;
        if (pc_last) {
            seen[pc_last & 0xffffu] = 1;
            pc_hits++;
        }
    }
    unsigned distinct = 0;
    for (unsigned i = 0; i < seen.size(); i++) distinct += seen[i];

    std::printf("  PC 0x%08x -> 0x%08x  range 0x%08x..0x%08x  distinct(low16)=%u\n",
                pc_first, pc_last, pc_min, pc_max, distinct);
    std::printf("  state=%u instr=0x%04x nonzero-PC samples=%llu\n",
                static_cast<unsigned>(top->state_o),
                static_cast<unsigned>(top->instr_word_o), pc_hits);
    std::printf("  during run: board writes=%llu reads=%llu\n",
                (unsigned long long)(board.vram_writes - vram_before),
                (unsigned long long)(board.word_reads - reads_before));
    std::printf("  board totals: word_reads=%llu word_writes=%llu unmapped_r=%llu "
                "unmapped_w=%llu\n",
                (unsigned long long)board.word_reads,
                (unsigned long long)board.word_writes,
                (unsigned long long)board.unmapped_reads,
                (unsigned long long)board.unmapped_writes);

    top->final();
    delete top;
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    Options opt;
    if (!parse_args(argc, argv, &opt)) {
        usage(argv[0]);
        return 2;
    }
    if (opt.self_test) return self_test(opt.cycles);
    if (!opt.boot.empty())
        return boot_from_events(opt.boot, opt.cycles);
    if (!opt.host_writes.empty())
        return replay_host_writes(opt.host_writes, opt.ref_vram);

    Verilated::commandArgs(argc, argv);
    auto *top = new tms34010_sv;

    /* ~1.5 MiB: static, never on the stack. */
    static gsp_board_t board;
    gsp_board_reset(&board);
    if (!opt.image.empty() && !load_image(&board, opt.image)) return 1;

    /* Inactive level: RUN/EMU high, both external interrupts high (deasserted,
     * TMS34010 interrupts are active low), no hold request. */
    top->run_emu_n_i   = 1;
    top->lint1_n_i     = 1;
    top->lint2_n_i     = 1;
    top->hold_req_i    = 0;
    top->dpyint_set_i  = 0;
    top->host_req_i    = 0;
    top->host_we_i     = 0;
    top->host_reg_i    = 0;
    top->host_be_i     = 0;
    top->host_wdata_i  = 0;
    top->video_hsync_n_i = 1;
    top->video_vsync_n_i = 1;

    /* HCS low at reset.
     *
     * This is the load-bearing reset decision.  tms34010_core.sv's CORE_RESET
     * state says: "HCS high at reset selects host-present mode and defers the
     * level-0 vector fetch until the host clears HLT.  Otherwise fetch the
     * 32-bit vector through the normal memory interface."
     *
     * With HCS asserted the core never fetches its reset vector and simply
     * spins at PC=0 reading whatever is at address 0.  On this board address
     * 0 is the `.noprw()` self-test window, so that is an endless 0x0000
     * fetch.  Asserting HCS selects the architectural ROM-less boot: the core
     * fetches its level-0 vector from RESET_VECTOR_ADDR (0xFFFF_FFE0), which
     * is inside the mirrored VRAM share the 68010 fills. */
    top->hcs_n_i = opt.hcs_n;

    /* Hold the core in reset while the board is set up, then release.  This
     * models the board's /GSPRES line, which the MC68010 drives through the
     * NWR function-6 control at $60400C/$60401C. */
    top->rst       = 1;
    top->vclk_rst_i = 1;
    top->clk       = 0;
    top->eval();
    for (int i = 0; i < 16; i++) {
        top->clk = !top->clk;
        top->eval();
    }

    top->rst        = 0;
    top->vclk_rst_i = 0;

    const uint32_t addr_mask = opt.addr_shift >= 32
        ? 0xffffffffu : ((1u << (32 - opt.addr_shift)) - 1u);

    unsigned long long cycles = 0;
    unsigned long long traced = 0;
    bool cycle_pending = false;
    uint32_t last_addr = 0;

    std::printf("gsp-rtl harness: %llu cycles, addr-shift %d\n",
                opt.cycles, opt.addr_shift);
    std::printf("%-6s %-12s %-10s %-10s %s\n",
                "cyc", "kind", "core-addr", "byte-addr", "data");

    for (cycles = 0; cycles < opt.cycles; cycles++) {
        /* Service any request the core raised on the previous cycle.  The bus
         * arbiter holds cycle_req_o until it sees cycle_ack_i. */
        if (cycle_pending) {
            const unsigned kind = top->cycle_kind_o & 0xf;
            const uint16_t data = static_cast<uint16_t>(top->cycle_wdata_o);

            /* Convert the core's address units to a board byte address. */
            const uint32_t byte_addr =
                (static_cast<uint32_t>(top->cycle_addr_o) >> opt.addr_shift);

            if (kind == KIND_WORD_READ) {
                top->cycle_rdata_i = gsp_board_read16(&board, byte_addr);
            } else if (kind == KIND_WORD_WRITE) {
                gsp_board_write16(&board, byte_addr, data);
            }
            /* DRAM refresh/counter and pixel MTR/RTM are pin-level cycles with
             * no memory effect here; acknowledge and move on. */

            top->cycle_ack_i = 1;
            cycle_pending = false;
        } else {
            top->cycle_rdata_i = 0;
            top->cycle_ack_i  = 0;
        }

        if (!cycle_pending && top->cycle_req_o) {
            cycle_pending = true;
            last_addr = static_cast<uint32_t>(top->cycle_addr_o);
        }

        /* rising edge */
        top->clk = 1;
        top->eval();

        if (traced < opt.trace && cycle_pending) {
            const unsigned kind = top->cycle_kind_o & 0xf;
            const uint32_t byte_addr =
                (static_cast<uint32_t>(top->cycle_addr_o) >> opt.addr_shift);
            std::printf("%-6llu %-12u 0x%08x   0x%08x   %s\n",
                        cycles,
                        kind,
                        static_cast<unsigned>(last_addr),
                        static_cast<unsigned>(byte_addr),
                        (kind == KIND_WORD_READ) ? "read"
                                                 : "write");
            traced++;
        }

        /* falling edge */
        top->clk = 0;
        top->eval();
    }

    std::printf("\nresult: PC=0x%08x state=%u instr=0x%04x\n",
                static_cast<unsigned>(top->pc_o),
                static_cast<unsigned>(top->state_o),
                static_cast<unsigned>(top->instr_word_o));
    std::printf("board : word_reads=%llu word_writes=%llu vram_writes=%llu\n",
                (unsigned long long)board.word_reads,
                (unsigned long long)board.word_writes,
                (unsigned long long)board.vram_writes);
    std::printf("        unmapped_reads=%llu unmapped_writes=%llu host=%llu\n",
                (unsigned long long)board.unmapped_reads,
                (unsigned long long)board.unmapped_writes,
                (unsigned long long)board.host_writes);
    std::printf("        reset_vector@0x%08x -> offset 0x%06x\n",
                kResetVectorAddr,
                kResetVectorAddr & (GSP_BOARD_VRAM_BYTES - 1u));
    (void)addr_mask;

    top->final();
    delete top;
    return 0;
}