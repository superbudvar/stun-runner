/* End-to-end check for the 68010 -> JSA-II transport.
 *
 * The defect this slice fixes is ROUTING, not arithmetic: the region exists
 * in route_address() but both the read and the write side of REGION_SOUND
 * were inert stubs, so a `move.b d0,$600000.l` was dropped on the floor.
 * Therefore this test drives the REAL Musashi core with a synthetic
 * `move.b` / `move.w` / `move.l` to $600000 placed in main RAM, and asserts
 * what came out the other end of the chain: the latch byte, then a live
 * sound_dispatch_frame() producing PCM at a stub audio backend.
 *
 * Sub-checks:
 *   1. a byte store through the real decoder lands the right byte in the latch
 *   2. the byte lane is 0xFF00 (D8-D15) for even addresses, 0x00FF for odd
 *   3. a read from the window does not touch command_pending
 *   4. addresses just outside $600000-$603FFF are no-ops for the latch
 *   5. end-to-end: store $38 -> sound_dispatch_frame() -> PCM at 9039 Hz
 */
#include "fake_ports.h"
#include "jsa_transport.h"
#include "musashi_cpu.h"
#include "../reproduction/sound/jsa_latch.h"
#include "../reproduction/sound/oki6295.h"
#include "../reproduction/sound/sound_dispatch.h"
#include "../backend/audio.h"
#include "../third_party/musashi/m68k.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
static int checks;

#define CHECK(condition) do { \
    checks++; \
    if (!(condition)) { \
        fprintf(stderr, "FAIL: %s\n", #condition); \
        failures++; \
    } \
} while (0)

#define CHECK_EQ(actual, expected) do { \
    long long a_ = (long long)(actual), e_ = (long long)(expected); \
    checks++; \
    if (a_ != e_) { \
        fprintf(stderr, "FAIL: %s == %s (got %lld, want %lld)\n", \
                #actual, #expected, a_, e_); \
        failures++; \
    } \
} while (0)

/* ---- stub audio backend ------------------------------------------------- */

typedef struct stub_audio {
    unsigned long stream_writes;
    unsigned long samples;
    uint32_t last_sample_rate;
    stunrun_audio_format_t last_format;
    uint64_t last_buffer_index;
    int16_t first_nonzero;
} stub_audio_t;

static stub_audio_t g_stub;

static bool stub_create(void *user) { (void)user; return true; }
static bool stub_init(void *user, uint32_t rate)
{
    (void)user;
    g_stub.last_sample_rate = rate;
    return true;
}
static void stub_shutdown(void *user) { (void)user; }
static void stub_destroy(void *user) { (void)user; }
static void stub_set_mute(void *user, bool mute) { (void)user; (void)mute; }
static void stub_drain(void *user) { (void)user; }
static void stub_set_gain(void *user, float g) { (void)user; (void)g; }
static void stub_set_master_volume(void *user, uint8_t stream_id, float v)
{
    (void)user; (void)stream_id; (void)v;
}
static size_t stub_stream_adpcm(void *user, const stunrun_audio_adpcm_t *b)
{
    (void)user; (void)b;
    return 0;
}

static size_t stub_stream_write(void *user, const stunrun_audio_buffer_t *buf)
{
    size_t i;
    (void)user;
    g_stub.stream_writes++;
    g_stub.samples += buf->sample_count;
    g_stub.last_sample_rate = buf->sample_rate;
    g_stub.last_format = buf->format;
    g_stub.last_buffer_index = buf->buffer_index;
    for (i = 0; i < buf->sample_count; i++) {
        if (buf->samples[i] != 0 && g_stub.first_nonzero == 0)
            g_stub.first_nonzero = buf->samples[i];
    }
    return buf->sample_count;
}

static const stunrun_audio_backend_t kStubBackend = {
    "stub",
    stub_create, stub_init, stub_shutdown, stub_destroy,
    stub_stream_write, stub_stream_adpcm, stub_drain,
    stub_set_gain, stub_set_master_volume,
    NULL,               /* send_command */
    stub_set_mute,
    NULL, NULL          /* get_latency_ms, get_load */
};

/* ---- musashi harness ---------------------------------------------------- */

/* Main RAM lives at 0xFF8000-0xFFFFFF, so a synthetic code stub is
 * placeable there without loading any ROM.  musashi_loader_load() is only
 * used to assert the real ROM bytes if a ROM dir is supplied. */
#define STUB_BASE 0x00FFF000u
#define SENTINEL 0x00FFF040u

static void write_insn(stunrun_fake_ports_t *ports, unsigned addr,
                       const uint8_t *bytes, size_t n)
{
    size_t i;
    for (i = 0; i + 1 < n; i += 2) {
        uint16_t w = (uint16_t)(((uint16_t)bytes[i] << 8) | bytes[i + 1]);
        CHECK(stunrun_mem_write16_be(ports, addr + (unsigned)i, w));
    }
}

/* Run the stub at STUB_BASE until it branches to SENTINEL (bra.s to self). */
static void run_stub(musashi_cpu_t *cpu, uint32_t d0)
{
    unsigned cycles = 0u;
    m68k_set_reg(M68K_REG_D0, d0);
    m68k_set_reg(M68K_REG_SR, 0x2700u);   /* IRQs masked */
    m68k_set_reg(M68K_REG_A7, 0x00FFF800u);
    m68k_set_reg(M68K_REG_PC, STUB_BASE);
    while (musashi_cpu_get_pc(cpu) != SENTINEL && cycles < 10000u) {
        int used = musashi_cpu_run(cpu, 100);
        CHECK(used > 0);
        if (used <= 0)
            break;
        cycles += (unsigned)used;
    }
    CHECK(musashi_cpu_get_pc(cpu) == SENTINEL);
}

/* ---- 1 & 2. byte store through the real decoder ------------------------- */

/* 13c000600000  move.b d0,$600000.l   (absolute long, the ROM's exact form)
 * 4e71          nop
 * 60f0          bra.s SENTINEL       */
static const uint8_t kStubMoveB[] = {
    0x13, 0xc0, 0x00, 0x60, 0x00, 0x60, 0x4e, 0x71, 0x60, 0x36
};

/* 33c000600000  move.w d0,$600000.l */
static const uint8_t kStubMoveW[] = {
    0x33, 0xc0, 0x00, 0x60, 0x00, 0x60, 0x4e, 0x71, 0x60, 0x36
};

/* 23c000600000  move.l d0,$600000.l */
static const uint8_t kStubMoveL[] = {
    0x23, 0xc0, 0x00, 0x60, 0x00, 0x60, 0x4e, 0x71, 0x60, 0x36
};

/* 13fc00ff006003ff  move.b #$ff,$603fff.l -- the very top of the window
 * 4e71             nop
 * 6034             bra.s SENTINEL */
static const uint8_t kStubMoveBTop[] = {
    0x13, 0xfc, 0x00, 0xff, 0x00, 0x60, 0x03, 0xff, 0x4e, 0x71, 0x60, 0x34
};

/* 13fc00ff00604000  move.b #$ff,$604000.l -- one byte past the window */
static const uint8_t kStubMoveBOutside[] = {
    0x13, 0xfc, 0x00, 0xff, 0x00, 0x60, 0x40, 0x00, 0x4e, 0x71, 0x60, 0x34
};

static void check_lane_and_routing(const char *roms)
{
    unsigned pass;

    for (pass = 0u; pass < (roms ? 2u : 1u); pass++) {
        stunrun_fake_ports_t ports;
        musashi_cpu_t *cpu;
        stunrun_jsa_latches_t latches;
        const uint8_t *caller_bytes;

        stunrun_ports_init(&ports);
        cpu = musashi_cpu_create();
        CHECK(cpu != NULL);
        if (!cpu) return;
        stunrun_jsa_latches_init(&latches);
        musashi_cpu_set_ports(cpu, &ports);
        musashi_cpu_set_jsa_latches(cpu, &latches);
        if (roms && musashi_loader_load(cpu, roms) != 0) {
            CHECK(0);
            musashi_cpu_destroy(cpu);
            return;
        }
        musashi_cpu_reset(cpu);
        musashi_cpu_set_jsa_latches(cpu, &latches);  /* reset clears bindings */

        if (roms && pass == 0u) {
            /* The exact encoding the 68010 ROM uses at PC 0x023EF6 and
             * 0x023EC0 -- checked against the real ROM so a wrong bank or a
             * different ROM version cannot silently be our oracle. */
            caller_bytes = kStubMoveB;
            {
                unsigned i;
                for (i = 0u; i < 4u; i++) {
                    CHECK(m68k_read_memory_8(0x023EF6u + i) == caller_bytes[i]);
                    CHECK(m68k_read_memory_8(0x023EC0u + i) == caller_bytes[i]);
                }
                /* And the boot-time literal store at 0x008DDA. */
                CHECK(m68k_read_memory_16(0x008DDAu) == 0x13FCu);
                CHECK(m68k_read_memory_16(0x008DDCu) == 0x0007u);
                CHECK(m68k_read_memory_16(0x008DDEu) == 0x0060u);
                CHECK(m68k_read_memory_16(0x008DE0u) == 0x0000u);
            }
        }

        /* --- window boundaries --- */
        CHECK_EQ(stunrun_jsa_in_window(0x600000u), 1);
        CHECK_EQ(stunrun_jsa_in_window(0x603FFFu), 1);
        CHECK_EQ(stunrun_jsa_in_window(0x5FFFFFu), 0);
        CHECK_EQ(stunrun_jsa_in_window(0x604000u), 0);

        /* --- 2. lane masks --- */
        /* Byte access to an EVEN address drives D8-D15; to an ODD address,
         * D0-D7.  This is the mask MAME reported for the frame-447 tap. */
        CHECK_EQ(stunrun_jsa_lane_mask(0x600000u, 1u), 0xFF00u);
        CHECK_EQ(stunrun_jsa_lane_mask(0x600001u, 1u), 0x00FFu);
        CHECK_EQ(stunrun_jsa_lane_mask(0x600000u, 2u), 0xFFFFu);

        /* --- 2. the extractor, at both levels --- */
        /* Bits 8-15 of 0xAABBCCDD are 0xCC.  The brief guessed 0xAA; that
         * would be bits 24-31, which mask 0xFF00 does not select. */
        CHECK_EQ(stunrun_jsa_command_byte(0xAABBCCDDu, 0xFF00u), 0xCC);
        /* Wrong mask yields nothing at all, never a zero byte. */
        CHECK_EQ(stunrun_jsa_command_byte(0xAABBCCDDu, 0x00FFu),
                 STUNRUN_JSA_NO_BYTE);
        CHECK_EQ(stunrun_jsa_command_byte(0xAABBCCDDu, 0xFFFFu),
                 STUNRUN_JSA_NO_BYTE);
        /* The frame-447 observation itself. */
        CHECK_EQ(stunrun_jsa_command_byte(STUNRUN_JSA_FRAME447_BUS_DATA,
                                          STUNRUN_JSA_FRAME447_BUS_MASK),
                 STUNRUN_JSA_FRAME447_BYTE);
        /* And the transport's own split agrees with the extractor. */
        {
            uint8_t bytes[STUNRUN_JSA_XFER_MAX_BYTES];
            /* Width 2 is a 16-bit bus word, so only bits 15-0 are driven. */
            CHECK_EQ(stunrun_jsa_command_bytes(0x600000u, 2u, 0xAABBu,
                                               bytes), 2);
            CHECK_EQ(bytes[0], 0xAA);
            CHECK_EQ(bytes[1], 0xBB);
            CHECK_EQ(stunrun_jsa_command_bytes(0x5FFFFFu, 2u, 0xAABBu,
                                               bytes), 0);
        }

        /* --- 1. byte store through the real decoder, even address --- */
        write_insn(&ports, STUB_BASE, kStubMoveB, sizeof(kStubMoveB));
        CHECK(stunrun_mem_write16_be(&ports, SENTINEL, 0x60FEu));  /* bra.s * */
        run_stub(cpu, 0x0000001Eu);
        CHECK_EQ(latches.command_byte, 0x1Eu);
        CHECK_EQ(latches.command_pending, 1);
        CHECK_EQ(latches.nmi_asserted, 1);
        /* Consumed exactly once. */
        CHECK_EQ(stunrun_jsa_sound_command_read(&latches), 0x1Eu);
        CHECK_EQ(latches.command_pending, 0);
        CHECK_EQ(stunrun_jsa_sound_command_read(&latches),
                 STUNRUN_JSA_NO_BYTE);

        /* --- 1b. same instruction, 0xAABBCCDD in D0: the byte store takes
         * D0's low byte, delivered on the high lane. --- */
        run_stub(cpu, 0xAABBCCDDu);
        CHECK_EQ(latches.command_byte, 0xDDu);

        /* --- 1c. top of the window is still inside it --- */
        write_insn(&ports, STUB_BASE, kStubMoveBTop, sizeof(kStubMoveBTop));
        run_stub(cpu, 0u);
        CHECK_EQ(latches.command_byte, 0xFFu);
        CHECK_EQ(latches.command_pending, 1);

        /* --- 4. one byte past the window is a no-op --- */
        {
            uint8_t before = latches.command_byte;
            int pending = latches.command_pending;
            write_insn(&ports, STUB_BASE, kStubMoveBOutside,
                       sizeof(kStubMoveBOutside));
            run_stub(cpu, 0x77u);
            CHECK_EQ(latches.command_byte, before);
            CHECK_EQ(latches.command_pending, pending);
            /* ...and the direct entry point agrees. */
            CHECK_EQ(stunrun_jsa_transport_write(&latches, 0x604000u, 1u,
                                                 0x77u), 0);
            CHECK_EQ(stunrun_jsa_transport_write(&latches, 0x5FFFFFu, 1u,
                                                 0x77u), 0);
            CHECK_EQ(latches.command_byte, before);
        }

        /* --- 2b. wider stores split big-endian over the byte cells --- */
        write_insn(&ports, STUB_BASE, kStubMoveW, sizeof(kStubMoveW));
        run_stub(cpu, 0x0000AABBu);
        CHECK_EQ(latches.command_byte, 0xBBu);

        write_insn(&ports, STUB_BASE, kStubMoveL, sizeof(kStubMoveL));
        run_stub(cpu, 0xAABBCCDDu);
        CHECK_EQ(latches.command_byte, 0xDDu);

        /* --- 3. reads never touch the command latch --- */
        {
            uint8_t byte = 0xFFu;   /* the not-ready value a caller would use */
            int pending = latches.command_pending;
            uint8_t cmd = latches.command_byte;

            /* No response is pending (the 6502 is not emulated), so the
             * window reads as the not-ready byte the old stub returned. */
            CHECK_EQ(stunrun_jsa_transport_read(&latches, 0x600000u, 1u,
                                                &byte), 0);
            CHECK_EQ(byte, 0xFFu);
            CHECK_EQ(m68k_read_memory_8(0x600000u), 0xFFu);
            CHECK_EQ(m68k_read_memory_16(0x600000u), 0xFFFFu);
            CHECK_EQ(m68k_read_memory_32(0x600000u), 0xFFFFFFFFu);

            CHECK_EQ(latches.command_pending, pending);
            CHECK_EQ(latches.command_byte, cmd);

            /* A pending response IS served to the 68010, and reading it
             * still leaves the command latch alone. */
            stunrun_jsa_sound_response_write(&latches, 0x3Cu);
            CHECK_EQ(latches.irq4_asserted, 1);
            CHECK_EQ(stunrun_jsa_transport_read(&latches, 0x600000u, 1u,
                                                &byte), 1);
            CHECK_EQ(byte, 0x3Cu);
            CHECK_EQ(latches.irq4_asserted, 0);
            CHECK_EQ(latches.command_pending, pending);
            CHECK_EQ(latches.command_byte, cmd);

            /* Read-and-clear, like sound_command_r. */
            CHECK_EQ(stunrun_jsa_transport_read(&latches, 0x600000u, 1u,
                                                &byte), 0);
            /* Reads outside the window never reach the latch at all. */
            CHECK_EQ(stunrun_jsa_transport_read(&latches, 0x5FFFFFu, 1u,
                                                &byte), 0);
        }

        /* A detached transport is inert, not a crash. */
        CHECK_EQ(stunrun_jsa_transport_write(NULL, 0x600000u, 1u, 0x38u), 0);
        CHECK_EQ(stunrun_jsa_transport_read(NULL, 0x600000u, 1u, NULL), 0);

        musashi_cpu_set_jsa_latches(cpu, NULL);
        musashi_cpu_destroy(cpu);
    }
}

/* ---- 5. end-to-end: 68010 store -> sound_dispatch_frame() -> PCM -------- */

static void check_end_to_end(const char *roms)
{
    stunrun_fake_ports_t ports;
    musashi_cpu_t *cpu;
    stunrun_jsa_latches_t latches;
    sound_dispatch_t dispatch;
    uint8_t *oki_rom;
    size_t i;
    int ok;

    memset(&g_stub, 0, sizeof(g_stub));
    stunrun_ports_init(&ports);
    cpu = musashi_cpu_create();
    CHECK(cpu != NULL);
    if (!cpu) return;

    /* Deterministic non-silent ROM content: a sawtooth nibble ramp, so a
     * decoded voice cannot read back as all zeros.  sound_dispatch_init()
     * takes ownership, so this has to be heap memory. */
    oki_rom = (uint8_t *)malloc(256 * 1024);
    CHECK(oki_rom != NULL);
    if (!oki_rom) { musashi_cpu_destroy(cpu); return; }
    for (i = 0; i < 256u * 1024u; i++)
        oki_rom[i] = (uint8_t)(i & 0x0Fu);

    ok = sound_dispatch_init(&dispatch, &kStubBackend, &g_stub,
                             oki_rom, (uint32_t)(256 * 1024));
    CHECK(ok);
    if (!ok) {
        free(oki_rom);
        musashi_cpu_destroy(cpu);
        return;
    }

    /* The live chain the shell uses: the CPU's JSA window is the dispatch
     * block's own latch, and the frame loop drains it. */
    musashi_cpu_set_ports(cpu, &ports);
    musashi_cpu_set_jsa_latches(cpu, &dispatch.latches);
    if (roms && musashi_loader_load(cpu, roms) != 0) {
        CHECK(0);
        musashi_cpu_destroy(cpu);
        return;
    }
    musashi_cpu_reset(cpu);
    musashi_cpu_set_jsa_latches(cpu, &dispatch.latches);  /* reset clears */
    stunrun_jsa_latches_init(&dispatch.latches);

    /* Command $38 -- the STUN Runner engine note (60 Hz) per oki_cmdmap. */
    write_insn(&ports, STUB_BASE, kStubMoveB, sizeof(kStubMoveB));
    CHECK(stunrun_mem_write16_be(&ports, SENTINEL, 0x60FEu));  /* bra.s * */
    run_stub(cpu, 0x00000038u);

    CHECK_EQ(dispatch.latches.command_byte, 0x38u);
    CHECK_EQ(dispatch.latches.command_pending, 1);
    CHECK_EQ(dispatch.frames_dispatched, 0);

    for (i = 0; i < 8u; i++)
        (void)sound_dispatch_frame(&dispatch);

    CHECK_EQ(dispatch.frames_dispatched, 8);
    CHECK_EQ(dispatch.commands_dispatched, 1);
    CHECK_EQ(dispatch.latches.command_pending, 0);
    CHECK_EQ(oki6295_is_playing(&dispatch.oki), 1);
    CHECK_EQ(dispatch.oki_bytes_written > 0, 1);
    CHECK_EQ(dispatch.samples_enqueued > 0, 1);

    /* The audio actually arrived, in the format the backend declares. */
    CHECK_EQ(g_stub.stream_writes, 8);
    CHECK_EQ(g_stub.samples > 0, 1);
    CHECK_EQ(g_stub.first_nonzero != 0, 1);
    CHECK_EQ(g_stub.last_sample_rate, 9039u);
    CHECK_EQ(g_stub.last_format, STUNRUN_AUDIO_FORMAT_S16LE_STEREO);
    CHECK(g_stub.last_buffer_index > 0u);

    sound_dispatch_shutdown(&dispatch);
    musashi_cpu_destroy(cpu);
}

/* ---- 6. the D16 rate --------------------------------------------------- */

static void check_default_rate(void)
{
    oki6295_t c;
    oki6295_init(&c);
    /* Nothing in the port calls oki6295_set_pin7(); the default has to be
     * the board's, or every sound plays at 7231 Hz. */
    CHECK_EQ(oki6295_sample_rate(&c), 9039u);
    CHECK_EQ(oki6295_set_pin7(&c, 0), 165);
    CHECK_EQ(oki6295_sample_rate(&c), 7231u);
    oki6295_init(&c);
    CHECK_EQ(oki6295_sample_rate(&c), 9039u);
}

int main(int argc, char **argv)
{
    const char *roms = (argc > 1) ? argv[1] : getenv("STUNRUN_JSA_TRANSPORT_ROMDIR");

    check_lane_and_routing(roms);
    check_end_to_end(roms);
    check_default_rate();

    if (failures == 0)
        printf("jsa-transport-musashi: all %d checks passed\n", checks);
    else
        printf("jsa-transport-musashi: %d of %d checks FAILED\n",
               failures, checks);
    return failures == 0 ? 0 : 1;
}