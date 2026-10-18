/* MC68010 emulator integration using Musashi.
 *
 * Bridges standalone Musashi 3.32 to the fake_ports memory abstraction.
 * The 68010 sees a flat 16MB address space; we route reads/writes
 * to ROM, RAM, or MMIO handlers based on address range.
 *
 * Memory map (24-bit address space):
 *   0x000000-0x0BFFFF  ROM banks (6 × 128KB, banked via ROM bank register)
 *   0xFF8000-0xFFFFFF  Main RAM (32KB)
 *   0x808000-0x80BFFF  ADSP data window
 *   0x810000-0x813FFF  ADSP serial/output buffer
 *   0x818000-0x818FFF  ADSP control/IRQ registers
 *   0x600000-0x603FFF  JSA II sound board interface
 *   0xA80000-0xA8001F  Input ports
 *   0xC00000-0xC000FF  GSP control + FIFO
 *   0xF5000000-0xF5000FFF  GSP palette RAM low
 *   0xF5800000-0xF5800FFF  GSP palette RAM high
 *   0xFF800000-0xFFFFFFFF  GSP VRAM mirror
 *
 * Callbacks we must provide (Musashi calls these on memory access):
 *   m68k_read_memory_8/16/32   — instruction and data reads
 *   m68k_write_memory_8/16/32   — data writes
 *   m68k_read_immediate_16/32  — immediate operands (from prefetch)
 *   m68k_read_pcrelative_8/16/32 — PC-relative reads
 *
 * Callbacks we register with Musashi:
 *   reset, interrupt acknowledge, etc.
 */

#ifndef STUNRUN_MUSASHI_CPU_H
#define STUNRUN_MUSASHI_CPU_H

#include <stdint.h>
#include <stddef.h>

#include "gsp_display_list.h"
#include "gsp_upload_callers.h"

/* Opaque forward declarations */
typedef struct stunrun_fake_ports stunrun_fake_ports_t;
typedef struct musashi_cpu musashi_cpu_t;
/* Defined in reproduction/sound/jsa_latch.h; forward-declared so this header
 * stays free of the reproduction include path. */
struct stunrun_jsa_latches;

/* ROM bank info: 6 banks × 128KB each, mapped to 0x000000-0x0BFFFF */
#define MUSASHI_NUM_ROM_BANKS  6
#define MUSASHI_ROM_BANK_SIZE  0x20000u  /* 128KB per bank */
#define MUSASHI_ROM_FILE_SIZE  0x10000u  /* 64KB per even/odd file pair (half of bank) */
#define MUSASHI_ROM_TOTAL_SIZE  (MUSASHI_NUM_ROM_BANKS * MUSASHI_ROM_BANK_SIZE)  /* 0xC0000: 6 banks, flat */

/* ── GSP host port ──
 *
 * The 68010 never addresses GSP memory directly.  The TMS34010 has its own
 * 24-bit address space, and the 68010 reaches it through four host registers
 * at 0xC00000 (per harddriv_m.cpp:139-156, where the driver swaps the word
 * index with (offset/2)^1 before dispatching):
 *
 *   0xC00000  TMS34010_HOST_ADDRESS_H   HSTADRH  upper 16 bits of address
 *   0xC00004  TMS34010_HOST_ADDRESS_L   HSTADRL  lower 16 bits of address
 *   0xC00008  TMS34010_HOST_CONTROL     HSTCTL
 *   0xC0000C  TMS34010_HOST_DATA        HSTDAT   the data port itself
 *
 * Every display-list word the 68010 produces is a HSTDAT write.  We model the
 * address latches and the target memory so those writes are observable; the
 * GSP instruction decoder is a separate, later milestone.
 */
#define MUSASHI_GSP_VRAM_BYTES      0x080000u /* 512 KiB: 16 x 64Kx4 VRAM chips */
#define MUSASHI_GSP_VRAM2BPP_BYTES  0x100000u /* 0x02000000-0x020FFFFF (1 MiB, no mirroring) */
#define MUSASHI_GSP_PAL_BYTES       0x800u    /* 1024 entries * 2 bytes */
#define MUSASHI_GSP_VRAM_WORDS      0x040000u /* VRAM_BYTES / 2 */
#define MUSASHI_GSP_PAL_ENTRIES     0x400u    /* PAL_BYTES / 2 */
/* PC execution histogram, indexed by PC for PCs below 1 MiB. */
#define MUSASHI_PC_HIST_SIZE 0x100000u

/* ADSP program RAM as seen through the 68010's 0x800000-0x807FFF window.
 * harddriv.cpp:4822-4824 installs hd68k_adsp_program_r/w there.  32 KiB. */
#define MUSASHI_ADSP_PGM_WORDS 0x4000u

/* ADSP data memory through the 68010's 0x808000-0x80BFFF window.
 * harddriv.cpp:4826/4848 install hd68k_adsp_data_r/w there.  16 KiB, and
 * hd68k_adsp_data_r is a bare `m_adsp_data_memory[offset]`, so the window
 * offset IS the DM word index -- $1FFF is the last word, which is why MAME
 * can key its release trigger on that address (harddriv_m.cpp:662). */
#define MUSASHI_ADSP_DM_WORDS 0x2000u

/* ADSP output memory through the 68010's 0x810000-0x813FFF window.
 * harddriv.cpp:4830 installs hd68k_adsp_buffer_r/w, which indexes
 * `som_memory[bank * 0x2000 + offset]`.  harddriv.h:386 declares
 * m_som_memory as uint16_t[0x8000/2] = 0x4000 words: four banks of 0x2000. */
#define MUSASHI_ADSP_SOM_WORDS 0x4000u

/* Observation counters for the ADSP board interface (findings 107/108).
 * These were silently dropped before, so a run log could not tell "the ROM
 * never touched the DSP windows" from "we discarded the write". `dm_trigger`
 * counts writes to DM($1FFF), which MAME treats as the DSP release trigger
 * (harddriv_m.cpp:657-668). Zeros are a real observation, not a stub. */
typedef struct musashi_cpu_adsp_io {
    unsigned long dm_trigger;      /* writes to DM($1FFF) */
    unsigned long ctrl_bank;       /* hd68k_adsp_control_w offset&7 == 3 */
    unsigned long ctrl_reset;      /* offset&7 == 7 */
    unsigned long ctrl_halt;       /* offset&7 == 5 (/BR) or 6 (/HALT) */
    unsigned      som_bank;        /* current SOM output-memory bank */
    unsigned      xflag;           /* hd68k_adsp_irq_state_r bit 1 */
    unsigned      irq_state;       /* hd68k_adsp_irq_state_r bit 0 */
    unsigned long irq_reads;       /* polls of the status register */
    unsigned long prog_reads;      /* 0x800000-0x807FFF reads */
    unsigned long prog_writes;     /* 0x800000-0x807FFF writes */
    unsigned long watchdog;         /* 0x608000-0x60BFFF service kicks */
    unsigned long soundrst16;       /* 0x604000-0x607FFF 16-bit writes */
    unsigned long soundrst32;       /* 0x604000-0x607FFF 32-bit writes */
    unsigned long watchdog32;       /* 0x608000-0x60BFFF 32-bit writes */
    unsigned long rom_tail;         /* reads in 0x0C0000-0x0FFFFF */
    unsigned long zram_reads;      /* 0xFF4000-0xFF4FFF reads */
    unsigned long zram_writes;     /* 0xFF4000-0xFF4FFF writes attempted */
    unsigned long zram_gated;      /* of those, ones ZP1/ZP2 dropped */
    unsigned      zp1;             /* hd68k_nwr_w offset&7==4 latch */
    unsigned      zp2;             /* hd68k_nwr_w offset&7==5 latch */
} musashi_cpu_adsp_io_t;

void musashi_cpu_adsp_io(musashi_cpu_adsp_io_t *out);

/* ── Frame pacing ──
 *
 * Cycles of 8 MHz MC68010 time per displayed frame.  The multisync screen is
 * `set_raw(5000000 * 2, 317 * 2, 0, 256 * 2, 262, 0, 228)` (harddriv.cpp:1869),
 * i.e. a 10 MHz pixel clock over 634x262 = 60.20180 Hz, while the CPU runs at
 * HARDDRIV_MASTER_CLOCK/4 = 8 MHz (harddriv.cpp:1463): 8e6 / 60.20180 =
 * 132 886.
 *
 * This is the shell's STUNRUN_FRAME_CYCLES, and it lives here because the
 * MC68010 text log below stamps every line with a frame number derived from
 * it.  shell.c defines its macro directly from this one, so there is only ever
 * one copy; a second literal in a second file is exactly the drift that makes
 * a "frame" column quietly wrong. */
#define MUSASHI_FRAME_CYCLES 132886u

#define MUSASHI_GSP_WLOG_CAP        (4u * 1024u * 1024u) /* host-write log cap */

/* Ordered MC68010 -> TMS34010 bus-event log.  This grows on demand from a
 * small initial block rather than being preallocated: a fixed cap large enough
 * for a long run costs hundreds of megabytes up front for a debug aid, and
 * preallocating it made the shell abort before its first frame.  Growth
 * doubling is amortised O(1) per event. */
#define MUSASHI_GSP_BUS_LOG_INITIAL 0x10000u  /* 65536 events, ~1.3 MiB */
#define MUSASHI_GSP_BUS_LOG_MAX     0x8000000u /* hard ceiling, ~256 MiB */

/* One captured HSTDAT write: where the GSP was told to put it, and what. */
typedef struct musashi_gsp_write {
    uint32_t addr;   /* GSP address, already masked to 0xFFFFFFF0 */
    uint16_t data;
    uint32_t pc;     /* MC68010 PC executing the HSTDAT write */
} musashi_gsp_write_t;

typedef enum musashi_gsp_bus_event_kind {
    MUSASHI_GSP_BUS_HOST_WINDOW = 1,
    MUSASHI_GSP_BUS_RESET_CONTROL = 2
} musashi_gsp_bus_event_kind_t;

/* One ordered MC68010 event affecting the TMS34010.  Host-window events retain
 * the raw byte address; providers apply their own register-index decode.  Long
 * CPU writes are exposed as two 16-bit host events.  For byte host writes,
 * `data` is the native effective 16-bit word after lane merge; consumers must
 * merge only lane_mask into their own register state.  Reset-control events
 * also carry the active-low board line's decoded released/asserted state.
 *
 * `cycles` is the MC68010 cycle count at the bus callback.  It is what makes
 * the captured stream replayable in lockstep rather than as two separate
 * passes: a consumer advances the GSP to this point before applying the event,
 * so each host write lands on the state the board actually had.
 *
 * This field is load-bearing for the on-disk ABI.  gsp_bus_events.bin is a raw
 * dump_blob() of this struct, so the file's record stride is exactly
 * sizeof(musashi_gsp_bus_event_t).  Removing `cycles` shrinks the struct from
 * 40 to 32 bytes, and a 32-byte reader on a 40-byte file does NOT fail
 * cleanly: it reads a plausible first record and then garbage, whose `kind`
 * bytes are almost never HOST_WINDOW or RESET_CONTROL, so the replay loop
 * spins over ~1.13M nonsense records and the run takes hours instead of
 * minutes.  Anything that changes this struct must recapture the stream. */
typedef struct musashi_gsp_bus_event {
    uint64_t sequence;        /* monotonically increasing within this CPU instance */
    uint64_t cycles;          /* MC68010 cycles elapsed at this bus callback */
    uint32_t address;         /* original MC68010 byte address */
    uint32_t pc;              /* MC68010 PC at the bus callback */
    uint16_t data;            /* effective host word or reset bus value */
    uint16_t lane_mask;       /* 0xFFFF word, 0xFF00 even-byte, 0x00FF odd-byte */
    uint8_t width_bits;       /* 8 or 16; long writes split into 16-bit events */
    musashi_gsp_bus_event_kind_t kind;
    uint8_t gsp_reset_released; /* valid for RESET_CONTROL; active-low /GSPRES */
} musashi_gsp_bus_event_t;

typedef void (*musashi_gsp_bus_observer_t)(
    void *context, const musashi_gsp_bus_event_t *event);

/* Create/destroy the CPU */
musashi_cpu_t *musashi_cpu_create(void);
void musashi_cpu_destroy(musashi_cpu_t *cpu);

/* Load ROM data.  Each bank is 128KB (64KB even + 64KB odd interleaved).
 * Pass flat 128KB buffers (already de-interleaved by the loader). */
int musashi_cpu_load_rom_bank(musashi_cpu_t *cpu, unsigned bank,
                              const uint8_t *even_bytes,
                              const uint8_t *odd_bytes);

/* Wire to the fake_ports instance (provides MMIO handlers and RAM) */
void musashi_cpu_set_ports(musashi_cpu_t *cpu, stunrun_fake_ports_t *ports);

/* Bind the JSA-II command/response latch the 68010's $600000-$603FFF window
 * lands in (harddriv.cpp:4806).  Until this is called, 68010 stores to the
 * window go nowhere and reads return the open-bus not-ready byte 0xFF.
 * Pass NULL to detach. */
void musashi_cpu_set_jsa_latches(musashi_cpu_t *cpu,
                                 struct stunrun_jsa_latches *latches);

/* Observe live MC68010 writes to MAME's TMS34010 host window and /GSPRES. The
 * callback runs synchronously after the native host-register side effect.  It
 * receives only actual MC68010 bus writes (not calls to C display-list slice
 * adapters).  Pass NULL to detach; callback context remains caller-owned. */
void musashi_cpu_set_gsp_bus_observer(
    musashi_cpu_t *cpu, musashi_gsp_bus_observer_t observer,
    void *context);
int musashi_cpu_gsp_reset_released(const musashi_cpu_t *cpu);

/* Reset the CPU */
void musashi_cpu_reset(musashi_cpu_t *cpu);

/* Run for a given number of 68010 cycles.
 * Returns actual cycles executed. */
int musashi_cpu_run(musashi_cpu_t *cpu, int cycles);

/* Assert an IRQ line (level 1-7).
 *
 * The CPU holds SIX interrupt lines at once (harddriv_m.cpp:94-100 drives lines
 * 1..6 from the MSP, the ADSP, the GSP, /LINKIRQ, the periodic timer and the
 * DUART), and MAME keeps them as a bitmask:
 * m68000_musashi_device::set_irq_line (m68kcpu.cpp:708-735) sets or clears one
 * bit of `m_virq_state` and then re-derives `m_int_level` from the HIGHEST
 * line still asserted.  A single global level cannot express that: asserting
 * IPL5 would erase a pending IPL2, and dropping IPL5 would drop IPL2 too.
 *
 * So both entry points go through Musashi's m68k_set_virq(), which is a
 * transcription of that same routine (m68kcpu.c:1080-1093), and callers drop a
 * line with musashi_cpu_clear_irq() rather than by setting level 0.
 *
 * A delivered interrupt clears the CPU's level by itself -- M68K_EMULATE_INT_ACK
 * is M68K_OPT_OFF (third_party/musashi/m68kconf.h:111), so
 * m68ki_exception_interrupt() ends with `CPU_INT_LEVEL = 0`
 * (third_party/musashi/m68kcpu.h:2155-2158).  Re-assertion therefore needs a
 * new board-side transition, which is what the real hardware and MAME both do. */
void musashi_cpu_set_irq(musashi_cpu_t *cpu, unsigned level);

/* Deassert one IRQ line, leaving any other asserted line in charge. */
void musashi_cpu_clear_irq(musashi_cpu_t *cpu, unsigned level);

/* Peek the PC (for debugging / oracle comparison) */
uint32_t musashi_cpu_get_pc(const musashi_cpu_t *cpu);
uint32_t musashi_cpu_total_cycles(const musashi_cpu_t *cpu);

/* PC execution histogram.  Enabled with STUNRUN_PC_HIST=<path>; the tally is
 * written by musashi_cpu_write_pc_histogram() at shutdown. */
void musashi_cpu_set_pc_hist(int enabled);
void musashi_cpu_write_pc_histogram(const musashi_cpu_t *cpu, const char *path);

/* Peek a register by name (for debugging) */
uint32_t musashi_cpu_get_reg(const musashi_cpu_t *cpu, const char *reg_name);

/* ── ADSP transport observer (OBSERVATION ONLY) ───────────────────────────
 *
 * We do NOT execute the ADSP-2100.  There is no ADSP instruction decoder in
 * this tree, no ADSP PC and no ADSP bus, so nothing here can observe the DSP
 * executing anything, emitting a SOMLATCH, or raising GINT.  This observer
 * covers only what the MC68010 itself does: its stores into the ADSP window
 * 0x818000-0x81FFFF (the LEDs, /BR, /HALT, the reset assert/release pair, the
 * deferred bank switch, the default controls, and the IRQ-clear pair at
 * 0x818060).  The window's device side is DROPPED by the model; this hook sits
 * in front of the drop.
 *
 * `pc` is the MC68010 PC as seen INSIDE the bus callback, and that is NOT the
 * address of the executing instruction.  Musashi fetches the opcode through
 * m68ki_read_imm_16(), which advances REG_PC by two per word consumed, and
 * M68K_EMULATE_PREFETCH is off in this tree (third_party/musashi/m68kconf.h),
 * so REG_PC here is instruction_address + 2 + 2 * extension_words_consumed --
 * `move.b d0,$818002.l` reports six bytes past the instruction that issued it.
 * musashi_cpu_write_watch() recovers the true address because arming it
 * single-steps musashi_cpu_run(); this observer deliberately does not, because
 * single-stepping moves m68ki_check_interrupts() to every instruction boundary
 * and therefore changes interrupt latency.  A log that changes the machine it
 * is logging is worse than one whose PC field is a few bytes off, so callers
 * must treat `pc` as a bus-callback PC and NOT as writer attribution; MAME
 * remains the authority for which routine wrote these.
 *
 * 32-bit stores are reported as two 16-bit events, high half first at the
 * lower address, matching the real bus and gsp_emit_reset_control_event().
 * With no observer installed the cost is one load-and-test of a NULL
 * file-static pointer in the REGION_ADSP_CTRL arm, which already existed. */
typedef void (*musashi_adsp_write_observer_t)(
    void *context, uint32_t address, uint16_t data, uint32_t pc,
    unsigned width_bits);

void musashi_cpu_set_adsp_write_observer(
    musashi_cpu_t *cpu, musashi_adsp_write_observer_t observer, void *context);

/* ROM loader: loads all 6 STUN Runner ROM banks into the CPU instance.
 * Returns 0 on success, -1 on I/O or internal failure.
 * Logs reset vector on success. */
int musashi_loader_load(musashi_cpu_t *cpu, const char *roms_dir);

/* ── Instrumentation ── */

/* How many HSTDAT writes the host port has taken (equals the number of words
 * the 68010 has pushed at the GSP so far). */
size_t musashi_cpu_gsp_write_count(const musashi_cpu_t *cpu);

/* Read back the Nth captured host write.  Returns 0 on success, -1 if out of
 * range.  Note this returns -1 both for "no such write" and for an
 * out-of-range index; callers that care should check the count first. */
int musashi_cpu_gsp_write_at(const musashi_cpu_t *cpu, size_t index,
                             musashi_gsp_write_t *out);

/* Current GSP host address latches, as the 68010 last set them. */
void musashi_cpu_gsp_host_addr(const musashi_cpu_t *cpu,
                               uint32_t *hstadrh, uint32_t *hstadrl);

/* Counts of host writes that landed in each GSP memory region.  Used to tell
 * "the 68010 never touched VRAM" apart from "it did and we mis-routed it". */
typedef struct musashi_gsp_region_counts {
    uint32_t vram;      /* 0xFF800000-0xFFBFFFFF */
    uint32_t vram_2bpp; /* 0x02000000-0x020FFFFF */
    uint32_t pal_lo;    /* 0xF5000000 */
    uint32_t pal_hi;    /* 0xF5800000 */
    uint32_t control;   /* 0xF4000000 / 0xF4800000 */
    uint32_t unmapped;  /* everything else */
    /* 2bpp expander writes that reached past the end of the 512 KiB share.
     * MAME indexes m_gsp_vram unchecked there (harddriv_v.cpp:302); we fold
     * instead, so a nonzero count here means our folding decided something. */
    uint32_t vram_2bpp_oob;
} musashi_gsp_region_counts_t;

void musashi_cpu_gsp_region_counts(const musashi_cpu_t *cpu,
                                   musashi_gsp_region_counts_t *out);

/* Cycles remaining until the periodic IRQ5 next fires.  MAME models this with
 * set_periodic_int at 244.14 Hz, which is every 32 768 cycles of an 8 MHz
 * 68010.  Callers run this many cycles, then assert IRQ5.  Returns
 * `period` when the timer is already overdue.
 *
 * The name keeps the `gsp_` prefix only for symmetry with the other
 * instrumentation entry points; this is main-CPU timing, not GSP state. */
uint32_t musashi_cpu_gsp_cycles_until_irq(const musashi_cpu_t *cpu,
                                          uint32_t period);

/* Write every observable piece of machine state into `out_dir` as raw binary:
 * main RAM, the GSP host write log, and each GSP memory region that received
 * host writes.  Returns 0 on success, -1 on I/O failure. */
int musashi_cpu_dump_state(const musashi_cpu_t *cpu, const char *out_dir);

/* Number of ordered MC68010 -> TMS34010 bus events recorded, and how many were
 * dropped at MUSASHI_GSP_BUS_LOG_MAX.  A nonzero dropped count means the
 * replay stream in gsp_bus_events.bin is incomplete, so any VRAM mismatch
 * against that run is not evidence about the board model. */
size_t musashi_cpu_gsp_bus_event_count(const musashi_cpu_t *cpu);
size_t musashi_cpu_gsp_bus_event_dropped(const musashi_cpu_t *cpu);

/* ── MC68010 text log (OBSERVATION ONLY) ──────────────────────────────────
 *
 * ROM $25E8-$269E is the MC68010's own text path: it resolves a selector
 * through the 394-long pointer table at $10136, decodes a four-byte descriptor,
 * and pushes 8x8 glyphs at the TMS34010 host port.  One call draws ONE string,
 * in `byte 3` characters, each character at base + 0x40*k with eight glyph rows
 * at +0x1000 each.
 *
 * With this enabled, every string the running game draws is printed as one
 * line:
 *
 *   text frame=48 cycles=6508646 sel=$047 x=01 y=0F colour=0xDF \
 *        base=0xFF896040 chars=30 "PRESS START TO BYPASS SELFTEST"
 *
 * This is a decoder bolted onto the host-port write path, not a change to it.
 * It reads the latches and the ROM and writes to stdout; it writes no latch,
 * no GSP memory, no VRAM, and no bus event, and it is off by default, where
 * the hook in gsp_host_write_word() is one predictable not-taken branch.
 *
 * That is verified rather than promised, and the check has a subtlety worth
 * knowing before you write it yourself: over a 600-frame run, every one of the
 * 901 354 gsp_bus_events.bin records is identical -- in every field the struct
 * names -- with the log off and on.  The raw FILE is not byte-identical, and
 * was not before this feature either: gsp_emit_bus_event() never assigns the
 * three padding bytes at record offsets 29-31 and 37-39, so they carry
 * uninitialised stack and change with any recompile.  Compare the named fields,
 * or mask those offsets.  See docs/mc68010-boot-findings.md section 23.6.
 *
 * `musashi_cpu_text_log_enable` walks the ROM's selector table once, so call it
 * after musashi_loader_load() and before the first musashi_cpu_run().  It
 * returns how many descriptors it decoded: 394 for the real ROM, 0 on failure.
 * Pass NULL, or call musashi_cpu_text_log_disable(), to turn it back off. */
unsigned musashi_cpu_text_log_enable(musashi_cpu_t *cpu);
void musashi_cpu_text_log_disable(void);

/* How the decoder is doing, so a run can be checked without diffing prose.
 *
 *   arms        HSTCTL writes with the two auto-increment bits clear.  One per
 *               $25E8 call, but also written by other code, so this is >= fired.
 *   matched     of those arms, the ones whose next HSTADR composition landed on
 *               a descriptor base.  Equal to the number of $25E8 calls that
 *               reached $2656.
 *   fired       lines printed.
 *   colour_miss matched a base but no descriptor there carries the colour the
 *               HSTDAT carried.  Nonzero means the arming rule is admitting a
 *               non-text draw; it is the number to watch.
 *   ambiguous   printed lines where more than one descriptor shares that
 *               (base, colour) pair.  These lines list the runners-up rather
 *               than guessing: 35 of the 394 descriptors share a base with
 *               another, and three share both base and colour at $FF800040. */
typedef struct musashi_text_log_stats {
    unsigned long arms;
    unsigned long matched;
    unsigned long fired;
    unsigned long colour_miss;
    unsigned long ambiguous;
} musashi_text_log_stats_t;

void musashi_cpu_text_log_stats(musashi_text_log_stats_t *out);

/* ── Live GSP memory ──
 *
 * Pointers into the live GSP memory the running 68010 has written, so a
 * renderer can scan out a frame without waiting for `musashi_cpu_dump_state`
 * to write files.  These alias the CPU's own buffers: valid until the CPU is
 * destroyed or `musashi_cpu_reset` reallocates them, read-only, and NOT
 * synchronised — the caller decides when a frame boundary is.
 *
 * `vram` holds `MUSASHI_GSP_VRAM_WORDS` 16-bit pixels (the 512 KiB board share,
 * which MAME's `scanline_multisync()` reads as words with mask 0x03FFFF).
 * `pal_lo`/`pal_hi` are the two 1024-entry colour planes at 0xF5000000 and
 * 0xF5800000, matching mame's `gsp_palette_change()`. */
typedef struct musashi_gsp_memory {
    const uint16_t *vram;      /* MUSASHI_GSP_VRAM_WORDS entries */
    const uint16_t *pal_lo;    /* MUSASHI_GSP_PAL_ENTRIES entries */
    const uint16_t *pal_hi;    /* MUSASHI_GSP_PAL_ENTRIES entries */
} musashi_gsp_memory_t;

/* Fill `out` with live GSP memory pointers.  Returns 0 on success, -1 if
 * `cpu` is NULL or its GSP buffers were never allocated. */
int musashi_cpu_gsp_memory(const musashi_cpu_t *cpu,
                           musashi_gsp_memory_t *out);

/* Bind the literal 68010 display-list slice to this CPU's canonical GSP host
 * port and work-RAM model.  The adapter does not execute GSP instructions or
 * render pixels; it only makes the slice's existing host writes observable in
 * the same latches, write log, and memory used by live Musashi execution.
 * Returns 0 on success, -1 when cpu, its fake-ports RAM, or out is missing. */
int musashi_cpu_bind_gsp_display_list(musashi_cpu_t *cpu,
                                      stunrun_gsp_display_list_t *out);

/* Caller bus adapter. The caller owns this context until the bound IO is no
 * longer used. Callees are explicit so transport integration does not silently
 * select the unverified C compiler or invent the opaque transfer operation.
 * Like Musashi itself, this interface is single-threaded. */
typedef struct {
    musashi_cpu_t *cpu;
    void *callee_ctx;
    void (*compile)(void *, uint32_t);
    void (*transfer)(void *, uint32_t, uint32_t);
} musashi_gsp_upload_adapter_t;

/* Requires a CPU with ports and both callee callbacks. Returns -1 for missing
 * prerequisites, leaving out unchanged; otherwise 0. Does not install overrides. */
int musashi_cpu_bind_gsp_upload_callers(musashi_gsp_upload_adapter_t *adapter,
                                       stunrun_gsp_upload_callers_t *out);

/* ── Runtime write watch + interrupt-delivery counter (OBSERVATION ONLY) ───
 *
 * Enabled by musashi_cpu_write_watch_enable() from STUNRUN_WATCH_WRITE.  With
 * the feature OFF there is no hook in the memory path at all: the three
 * m68k_write_memory_* callbacks and m68k_read_memory_32 each take one
 * predictable branch on a file-static count of zero and return.
 *
 * WHAT IT MEASURES.  Every m68k_write_memory_8/16/32 call is tested for an
 * overlap with a watched 68010 bus address, and each hit records the writing
 * PC (`M68K_REG_PC` at the time of the store, which in Musashi is the address
 * of the instruction currently executing because M68K_EMULATE_PREFETCH is off),
 * the value, and the access width.  The test is on the RAW bus address before
 * `route_address()`, so a watched address is matched however it is reached --
 * including by `m68k_write_memory_32_pd`, which presents a predecrement long
 * store as two word writes.
 *
 * WHAT IT CANNOT SEE.  Stores that bypass these callbacks: `display_list_poke_*
 * -> stunrun_mem_write*_be()` and any future direct write into the fake-ports
 * RAM.  For the question this exists to answer -- who writes a work-RAM word --
 * those are the ROM's own C transcriptions, not the 68010, and a zero count is
 * a statement about the emulated 68010 only.
 *
 * WHY `musashi_irq_watch_t` IS HERE.  There was no 68010 interrupt counter in
 * the checkpoint before this, so "the loop never exits" could not be separated
 * from "no interrupt is ever delivered".  Musashi fetches the handler address
 * for a taken interrupt as `m68ki_read_data_32((vector<<2) + REG_VBR)`
 * (m68kcpu.h:2134) with `vector = EXCEPTION_INTERRUPT_AUTOVECTOR + level`
 * (:2116, and `#define EXCEPTION_INTERRUPT_AUTOVECTOR 24` at :169), i.e. a
 * longword read of `(24+level)*4` for levels 1..7.  Counting longword reads of
 * the aligned range 0x64..0xBC therefore counts delivered interrupts, without
 * single-stepping and without touching Musashi's own build options
 * (M68K_EMULATE_INT_ACK is M68K_OPT_OFF in third_party/musashi/m68kconf.h:111,
 * so m68ki_int_ack is a constant and m68k_set_int_ack_callback() is dead --
 * which is why the count is taken at the vector fetch instead).
 *
 * A longword read at one of those addresses from ROM code would be a false
 * positive, and the counter cannot tell the two apart from the inside.  The
 * `target` field is what makes that checkable: an interrupt's target is the
 * longword just read, so `target` landing inside the ROM image is the expected
 * result and `target` landing in RAM is a warning.  Cross-check the count
 * against the PC histogram (`STUNRUN_PC_HIST`), which independently records
 * whether the handler was ever entered. */
#define MUSASHI_WRITE_WATCH_MAX 16

typedef struct musashi_write_watch {
    uint32_t address;       /* watched 68010 bus address */
    uint32_t writes;        /* bus writes overlapping [address, address+width) */
    uint32_t last_pc;       /* M68K_REG_PC during the most recent one */
    uint16_t last_value;    /* value carried by the most recent one */
    /* Writes whose DATA was non-zero, counted and attributed separately.
     * `last_pc` alone cannot answer "who set this flag": for a handshake word
     * the most recent writer is usually the one that clears it, and the frame
     * sampler misses a set-and-clear inside one frame. */
    uint32_t nonzero_writes;
    uint32_t nonzero_last_pc;
    uint16_t nonzero_last_value;
    uint8_t  last_width_bits;
    uint8_t  in_use;
} musashi_write_watch_t;

typedef struct musashi_irq_watch {
    uint32_t assert_calls;         /* musashi_cpu_set_irq() invocations */
    uint32_t assert_level[8];      /* ... by level, index 0..7 */
    uint32_t autovector_reads;     /* longword reads of (24+level)*4 + VBR */
    uint32_t vbr;                  /* the VBR those reads were relative to */
    uint32_t level5_reads;         /* ... at vector 29  (0x74): IRQ5 */
    uint32_t level7_reads;         /* ... at vector 31  (0x7C): NMI */
    uint32_t uninit_vector_reads;  /* reads of vector 75 (0x12C): handler was 0 */
    uint32_t last_vector;
    uint32_t last_source_pc;       /* PC the fetch interrupted */
    uint32_t last_target_pc;       /* handler address read out of the vector */
} musashi_irq_watch_t;

/* Parse `spec` -- comma-separated hex addresses, e.g. "0xFFDB48,0x608000" --
 * and arm the watch.  Returns how many addresses were armed (0 on a malformed
 * spec, which also leaves the watch OFF so a typo cannot look like a negative
 * result).  Arming also enables the interrupt counter.  Call it after
 * musashi_cpu_reset() and before the first musashi_cpu_run(); re-calling it
 * replaces the list. */
unsigned musashi_cpu_write_watch_enable(musashi_cpu_t *cpu, const char *spec);
void musashi_cpu_write_watch_disable(void);

/* Copy out the armed watch entries (up to `max`); returns how many were
 * written, so `out[i].in_use` is always 1 for i < that. */
unsigned musashi_cpu_write_watch(const musashi_cpu_t *cpu,
                                 musashi_write_watch_t *out, unsigned max);

/* The interrupt counter.  Zeroed by musashi_cpu_write_watch_enable(). */
const musashi_irq_watch_t *musashi_cpu_irq_watch(const musashi_cpu_t *cpu);

/* Instructions retired while the watch was armed -- the run is single-stepped
 * then, so this is also the sanity number that proves the stepping happened
 * (it is 0 in every run that did not arm the watch). */
uint32_t musashi_cpu_insn_count(void);

#endif /* STUNRUN_MUSASHI_CPU_H */
