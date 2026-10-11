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

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

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

} // namespace

int main(int argc, char **argv)
{
    Options opt;
    if (!parse_args(argc, argv, &opt)) {
        usage(argv[0]);
        return 2;
    }
    if (opt.self_test) return self_test(opt.cycles);

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