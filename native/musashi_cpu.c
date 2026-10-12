/* musashi_cpu.c — Musashi MC68010 integration */

#include "musashi_cpu.h"
#include "fake_ports.h"
#include "cpu_override.h"
#include "jsa_transport.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <poll.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* Musashi public API */
#include "m68k.h"

/* Registers the debug aids below need. */
static void flow_log_instruction(unsigned pc);
static void musashi_cpu_dump_regs(const musashi_cpu_t *cpu);
static void musashi_cpu_set_reg(musashi_cpu_t *cpu, const char *name,
                                uint32_t value);
/* Set by musashi_cpu_flow_log_enable(); musashi_cpu_run() reads it to decide
 * whether to single-step so the log sees every instruction. */
static int g_flow_log_enabled;

/* PC execution histogram (STUNRUN_PC_HIST).  Where the emulated cycles
 * actually go, so "which loop is eating the frame" is a measurement rather
 * than a guess.  Counted per instruction at the top of musashi_cpu_run(). */
static int g_pc_hist_enabled;
static uint32_t g_pc_hist_scratch[MUSASHI_PC_HIST_SIZE];

/* ── Forward declarations required by Musashi ── */

unsigned int  m68k_read_memory_8(unsigned int address);
unsigned int  m68k_read_memory_16(unsigned int address);
unsigned int  m68k_read_memory_32(unsigned int address);
unsigned int  m68k_read_immediate_16(unsigned int address);
unsigned int  m68k_read_immediate_32(unsigned int address);
unsigned int  m68k_read_pcrelative_8(unsigned int address);
unsigned int  m68k_read_pcrelative_16(unsigned int address);
unsigned int  m68k_read_pcrelative_32(unsigned int address);
void m68k_write_memory_8(unsigned int address, unsigned int value);
void m68k_write_memory_16(unsigned int address, unsigned int value);
void m68k_write_memory_32(unsigned int address, unsigned int value);
void m68k_write_memory_32_pd(unsigned int address, unsigned int value);

/* ── Internal state ── */

struct musashi_cpu {
    stunrun_fake_ports_t *ports;

    /* ROM banks: 6 × 128KB.  Each bank is stored as a flat uint8_t
     * array of 131072 bytes, with even byte indices = MSB and odd = LSB
     * of each 16-bit word (matching the MC68010 bus). */
    uint8_t rom_banks[MUSASHI_NUM_ROM_BANKS][MUSASHI_ROM_BANK_SIZE];

    /* Current ROM bank selected by the 68010 bank register.
     * The bank register value determines which 128KB window is active
     * in the 0x000000-0x0BFFFF range. */
    unsigned rom_bank;

    /* ROM bank register: MAME maps this somewhere in the I/O space.
     * We patch it when the 68010 writes to the known bank-switch address.
     * Typical location: 0xFFE000 (from Hard Drivin' driver analysis).
     * Default to bank 0 on reset. */
    uint32_t rom_bank_reg;

    /* ── GSP host port (TMS34010 host interface at 0xC00000) ── */

    /* Host address latches.  HSTADRH holds the upper 16 bits, HSTADRL the
     * lower; together they form the address of the next HSTDAT access.
     * Per tms34010.cpp the latches are 16-bit registers whose upper byte of
     * HSTCTL selects auto-increment behaviour. */
    uint16_t hstadrh;
    uint16_t hstadrl;
    uint8_t  hstctlh;   /* HSTCTL high byte (0x0800 = increment on write) */
    uint8_t  hstctll;   /* HSTCTL low byte  (0x1000 = increment on read) */

    /* GSP-side memory, allocated on create.  The 68010 can only reach these
     * through HSTDAT, but once a word lands here it is real machine state. */
    uint8_t *gsp_vram;       /* 0xFF800000-0xFFBFFFFF (mirrored) */
    uint8_t *gsp_vram_2bpp;  /* 0x02000000-0x020FFFFF */
    uint8_t *gsp_pal_lo;     /* 0xF5000000-0xF50007FF */
    uint8_t *gsp_pal_hi;     /* 0xF5800000-0xF58007FF */
    uint8_t *gsp_ctrl_lo;    /* 0xF4000000-0xF40000FF */
    uint8_t *gsp_ctrl_hi;    /* 0xF4800000-0xF48000FF */

    /* Ordered log of every HSTDAT write.  This is the display list, in the
     * order the 68010 produced it — the single most valuable artifact for
     * understanding the GSP side without emulating the GSP itself. */
    musashi_gsp_write_t *gsp_wlog;
    size_t gsp_wlog_len;

    /* How many host writes landed in each GSP region (see
     * musashi_gsp_region_counts_t). */
    musashi_gsp_region_counts_t gsp_counts;

    /* Optional live fanout tap for MC68010 GSP host-window/reset events. */
    musashi_gsp_bus_observer_t gsp_bus_observer;
    void *gsp_bus_observer_context;

    /* Bounded ordered record of every MC68010 -> TMS34010 bus event.  Written
     * unconditionally (not only when an observer is attached) so the shell's
     * state dump always yields the replayable host-port stream.  See
     * musashi_cpu_dump_state() for how this differs from gsp_wlog. */
    musashi_gsp_bus_event_t *gsp_bus_log;
    size_t gsp_bus_log_len;
    size_t gsp_bus_log_cap;   /* allocated capacity, grows on demand */
    size_t gsp_bus_log_dropped; /* events lost at MUSASHI_GSP_BUS_LOG_MAX */
    uint64_t gsp_bus_event_sequence;
    uint8_t gsp_reset_released; /* active-low /GSPRES state */

    /* Cycles executed since reset, used to pace the periodic IRQ5. */
    uint32_t total_cycles;
    uint32_t *pc_hist;
    uint16_t *adsp_pgm_ram;   /* 0x800000-0x807FFF window, MUSASHI_ADSP_PGM_WORDS */

    /* Video dot clock, used to drive the /HSYNCB and /VSYNCB bits that
     * hd68k_port0_r() derives from the screen rather than from the port.
     * The GSP pixel clock is 10 MHz against the 8 MHz 68010, so we keep the
     * accumulator in quarter-dots to keep the 5/4 ratio exact. */
    uint32_t dot_accum;
    uint32_t dot_hpos;   /* 0..633 (317 * 2) */
    uint32_t dot_vpos;   /* 0..261 */

    /* ── ADC0809 (8-bit, 8 channels) at 0xb00001 + EOC on IN0 bit4 ──
     * harddriv.cpp:1473  ADC0809(config, m_adc8, 1000000)
     * harddriv.cpp:1474-1481  in_callback<0..7> -> ioport "8BADC.0..7"
     * harddriv.cpp:559/608/648  map(0xb00001,0xb00001).mirror(0x7fffe).r(data_r)
     * harddriv.cpp:1092  PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_CUSTOM)
     *                        PORT_READ_LINE_DEVICE_MEMBER("mainpcb:adc8", eoc_r)
     * The game loop spins on that EOC bit at 0x23d98/0x23da0, so an EOC that
     * never asserts wedges the 68010 with zero display-list output. */
    uint8_t  adc8_address;    /* address_w(): m_address = data & 7 */
    int      adc8_start;      /* start_w() level */
    int      adc8_eoc;        /* eoc_r(): 1 = ready/idle, 0 = converting */
    int      adc8_phase;      /* 0 idle, 1 start, 2 ready, 3 running */
    uint32_t adc8_ready_at;   /* total_cycles when EOC will assert */
    int      adc8_busy;       /* a conversion is in flight */
    uint8_t  adc8_data;       /* data_r(): last latched value (m_sar) */
    uint8_t  adc8_in[8];      /* 8BADC.0..7 */
    /* ── 12-bit ADC / shared control register at 0xb80000 ──
     * harddriv_m.cpp hd68k_adc_control_w(): COMBINE_DATA(&m_adc_control),
     * m_adc8->address_w(m_adc_control & 7), m_adc8->start_w(BIT(,3)),
     * bit6 gates the 12-bit read of m_12badc[(m_adc_control>>4)&3], bit7
     * selects which half of the 12-bit result hd68k_adc12_r() returns. */
    uint16_t adc_control;
    uint8_t  adc12_select;
    uint8_t  adc12_byte;
    uint16_t adc12_data;
    uint16_t wheel;           /* hd68k_wr2_write(): m_wheel = data >> 8 */

    /* ── JSA II sound board at $600000-$603FFF ──
     * harddriv.cpp:4806 installs one 8-bit read/write handler pair across the
     * whole window with no offset decode: main_response_r on read,
     * main_command_w on write.  Bind the live latch block here so a 68010
     * store reaches stunrun_jsa_main_command_write().  NULL until bound. */
    struct stunrun_jsa_latches *jsa_latches;
};

/* Geometry of the STUN Runner raster, from harddriv.cpp:1869
 *     m_screen->set_raw(5000000 * 2, 317 * 2, 0, 256 * 2, 262, 0, 228)
 * and harddriv.cpp:1864 m_gsp->set_pixel_clock(5000000).  Two pixels per
 * 5 MHz GSP clock gives a 10 MHz dot clock against the 8 MHz CPU. */
#define MUSASHI_H_TOTAL   634u   /* 317 * 2 */
#define MUSASHI_V_TOTAL   262u
#define MUSASHI_H_VISIBLE 512u   /* 256 * 2 */
#define MUSASHI_V_VISIBLE 228u
/* harddriv.h:493  get_hblank() == (screen.hpos() > screen.width() * 9 / 10) */
#define MUSASHI_HBLANK_POS (MUSASHI_H_VISIBLE * 9 / 10)

/* Advance the video dot position by the given number of 68010 cycles. */
static void adc8_tick(musashi_cpu_t *cpu);
static void dot_clock_advance(musashi_cpu_t *cpu, uint32_t cycles)
{
    if (cpu == NULL)
        return;
    cpu->dot_accum += cycles * 5u;      /* 10 MHz dot / 8 MHz CPU = 5/4 */
    uint32_t dots = cpu->dot_accum / 4u;
    cpu->dot_hpos = dots % MUSASHI_H_TOTAL;
    cpu->dot_vpos = (dots / MUSASHI_H_TOTAL) % MUSASHI_V_TOTAL;
    adc8_tick(cpu);
}

/* ── ADC0809 ────────────────────────────────────────────────────────────
 * A faithful-enough state machine for devices/machine/adc0808.cpp.  The
 * device runs on its own 1 MHz clock (harddriv.cpp:1473), i.e. 8 cycles of
 * the 8 MHz 68010 per tick.  update_state() is a 3-hop chain:
 *
 *   start_w(1)          -> STATE_CONVERSION_START, timer = 1 tick
 *   +1 tick             -> STATE_CONVERSION_READY, timer = 1 tick
 *   +1 tick             -> STATE_CONVERSION_RUNNING, eoc = 0,
 *                          sar = in[address], timer = 64 ticks
 *   +64 ticks           -> STATE_IDLE, eoc = 1, sar = in[address], timer off
 *
 * so EOC falls 2 ticks after START and rises 66 ticks (528 CPU cycles)
 * after it.  data_r() latches nothing new, it just returns m_sar; the two
 * sar samples are equal here because STUN Runner's channels are constants
 * (harddriv.cpp:1129-1153: two analogue sticks at 0x80, the rest inactive).
 */
#define ADC0809_CPU_CYCLES_PER_TICK 8u

static void adc8_start_w(musashi_cpu_t *cpu, int state)
{
    if (cpu == NULL || state == cpu->adc8_start)
        return;
    if (cpu->adc8_start == 0) {           /* rising edge */
        cpu->adc8_phase = 1;               /* CONVERSION_START */
        cpu->adc8_busy = 1;
        cpu->adc8_ready_at = cpu->total_cycles + ADC0809_CPU_CYCLES_PER_TICK;
    } else {                              /* falling edge restarts the timer */
        if (cpu->adc8_busy) {
            cpu->adc8_phase = 1;
            cpu->adc8_ready_at = cpu->total_cycles + ADC0809_CPU_CYCLES_PER_TICK;
        }
    }
    cpu->adc8_start = state;
}

static void adc8_tick(musashi_cpu_t *cpu)
{
    if (cpu == NULL || !cpu->adc8_busy)
        return;
    while (cpu->adc8_busy && cpu->total_cycles >= cpu->adc8_ready_at) {
        switch (cpu->adc8_phase) {
        case 1:                             /* START -> READY */
            cpu->adc8_phase = 2;
            cpu->adc8_ready_at += ADC0809_CPU_CYCLES_PER_TICK;
            break;
        case 2:                             /* READY -> RUNNING, EOC falls */
            cpu->adc8_phase = 3;
            cpu->adc8_eoc = 0;
            cpu->adc8_data = cpu->adc8_in[cpu->adc8_address & 7u];
            cpu->adc8_ready_at += 64u * ADC0809_CPU_CYCLES_PER_TICK;
            break;
        default:                            /* RUNNING -> IDLE, EOC rises */
            cpu->adc8_phase = 0;
            cpu->adc8_busy = 0;
            cpu->adc8_eoc = 1;
            cpu->adc8_data = cpu->adc8_in[cpu->adc8_address & 7u];
            break;
        }
    }
}

/* harddriv_m.cpp hd68k_adc_control_w(offs_t, uint16_t, uint16_t) */
static void adc_control_write(musashi_cpu_t *cpu, uint16_t data)
{
    if (cpu == NULL)
        return;
    cpu->adc_control = data;
    cpu->adc8_address = (uint8_t)(data & 0x07u);
    adc8_start_w(cpu, (data & 0x08u) ? 1 : 0);
    if (data & 0x40u) {
        cpu->adc12_select = (uint8_t)((data >> 4) & 0x03u);
        /* 12BADC.0-3 are all IP_ACTIVE_LOW / IPT_UNUSED on stunrun, so the
         * channel reads back as the inactive level (harddriv.cpp:1155-1167). */
        cpu->adc12_data = 0x0FFFu;
    }
    cpu->adc12_byte = (uint8_t)((data >> 7) & 0x01u);
}

/* harddriv_m.cpp hd68k_adc12_r() */
static uint16_t adc12_read(const musashi_cpu_t *cpu)
{
    if (cpu == NULL)
        return 0xFFFFu;
    return cpu->adc12_byte ? (uint16_t)((cpu->adc12_data >> 8) & 0x0Fu)
                           : (uint16_t)(cpu->adc12_data & 0x00FFu);
}

/* Global pointer used by the global Musashi callback functions.
 * Safe in single-threaded native execution. */
static musashi_cpu_t *s_cpu = NULL;

/* ── Address routing ── */

/* Memory region tags for the 68010 address space.
 * All offsets are native 24-bit byte addresses as the 68010 presents them. */
typedef enum {
    REGION_ROM       = 0,   /* 0x000000-0x0BFFFF: banked ROM */
    REGION_RAM       = 1,   /* 0xFF8000-0xFFFFFF: main RAM */
    REGION_ADSP_DATA = 2,   /* 0x808000-0x80BFFF: ADSP data window */
    REGION_ADSP_BUF  = 3,   /* 0x810000-0x813FFF: ADSP serial buffer */
    REGION_ADSP_CTRL = 4,   /* 0x818000-0x81FFFF: ADSP control/IRQ */
    REGION_SOUND     = 5,   /* 0x600000-0x603FFF: JSA II sound board */
    REGION_INPUT     = 6,   /* 0xA80000-0xA800FF: input ports */
    REGION_GSP       = 7,   /* 0xC00000-0xC03FFF: MC68010 GSP host-port window */
    REGION_IRQACK    = 10,  /* 0x60C000-0x60FFFF: port0 read / IRQ ack write */
    REGION_GSP_PAL   = 8,   /* 0xF5000000-0xF5FFFFFF: GSP palette RAM */
    REGION_GSP_VRAM_MIRROR = 9, /* 0xFF800000-0xFFFFFFFF: GSP VRAM mirror */
    REGION_ADSP_PROG = 11,  /* 0x800000-0x807FFF: ADSP program RAM window */
    REGION_ADSP_IRQ  = 12,  /* 0x838000-0x83FFFF: ADSP interrupt state (read) */
    REGION_SOUNDRST  = 13,  /* 0x604000-0x607FFF: sound reset / nwr control */
    REGION_ADC_CTRL  = 14,  /* 0xB80000-0xBFFFFF: 12-bit ADC + ADC0809 control */
    REGION_ADC8      = 15,  /* 0xB00000-0xB7FFFF: ADC0809 data / WR2 latch */
    REGION_UNMAPPED  = -1,
} region_t;

static region_t route_address(unsigned int address, unsigned int *out_offset)
{
    /* Returns region tag; sets *out_offset to the offset to use for
     * accessing that region (may be transformed from the input address). */

    /* ADSP program RAM, installed by harddriv_state::init_adsp()
     * (harddriv.cpp:4822-4824) ON TOP of the 1 MiB ROM mapping, so this test
     * must come first.  The window is 32 KiB on the 68010 side; the ADSP's own
     * share ("adsp_pgm_memory", harddriv.cpp:683) is only 0x0000-0x1fff words
     * and the upper half is nopr() there, so the tail is unreachable in
     * practice.  We keep a full 32 KiB of zeroed RAM: with the ADSP not
     * running, zero is what every address the ROM polls reads back. */
    if (address >= 0x800000u && address <= 0x807FFFu) {
        *out_offset = address - 0x800000u;
        return REGION_ADSP_PROG;
    }

    /* ADSP interrupt state word, read-only (harddriv.cpp:4836).  Returns
     * 0xfffd with neither m_adsp_xflag nor m_adsp_irq_state set
     * (harddriv_m.cpp:759-765); both stay clear while the ADSP is idle. */
    if (address >= 0x838000u && address <= 0x83FFFFu) {
        *out_offset = address;
        return REGION_ADSP_IRQ;
    }

    /* hd68k_sound_reset_r / hd68k_nwr_w (harddriv_m.cpp:288-293, :400-450). */
    if (address >= 0x604000u && address <= 0x607FFFu) {
        *out_offset = address;
        return REGION_SOUNDRST;
    }

    if (address < MUSASHI_ROM_TOTAL_SIZE) {
        /* Banked ROM window: 0x000000-0x0BFFFF (6 × 128KB = 768KB).
         * The bank index is implicit in the high bits of the address. */
        *out_offset = address;
        return REGION_ROM;
    }

    /* Main RAM: 0xFF8000-0xFFFFFF (512 KiB), per multisync_68k_map's
     * map(0xff8000, 0xffffff).ram().  The 68010 drives a 24-bit bus, so this
     * is a plain 24-bit range test. */
    if (address >= 0xFF8000u) {
        *out_offset = address & (STUNRUN_MAIN_RAM_BYTES - 1u);
        return REGION_RAM;
    }

    /* GSP VRAM mirror.  Note the 68010 cannot actually reach this: its address
     * space is 24 bits, so 0xFF800000 truncates to 0x800000 and never matches
     * the 32-bit test below.  The region is kept because the TMS34010 shares
     * it, and gsp_mem_read_word/gsp_mem_write_word index it directly. */
    if ((address & 0xFF000000u) == 0xFF000000u) {
        *out_offset = address;
        return REGION_GSP_VRAM_MIRROR;
    }

    /* ADSP data window */
    if (address >= 0x808000 && address <= 0x80BFFF) {
        *out_offset = address;
        return REGION_ADSP_DATA;
    }
    /* ADSP serial buffer */
    if (address >= 0x810000 && address <= 0x813FFF) {
        *out_offset = address;
        return REGION_ADSP_BUF;
    }
    /* ADSP control registers */
    if (address >= 0x818000 && address <= 0x81FFFF) {
        *out_offset = address;
        return REGION_ADSP_CTRL;
    }
    /* JSA II sound board */
    if (address >= 0x600000 && address <= 0x603FFF) {
        *out_offset = address;
        return REGION_SOUND;
    }
    /* Control port 0: reading gives the DIAGN/HSYNC/VSYNC/EOC/coin/switch
     * status word, writing acks the periodic IRQ5 (harddriv.cpp:604,
     * harddriv_m.cpp:111-115 and :201-224). */
    if (address >= 0x60C000 && address <= 0x60FFFF) {
        *out_offset = address;
        return REGION_IRQACK;
    }
    /* Input ports */
    if (address >= 0xA80000 && address <= 0xA800FF) {
        *out_offset = address;
        return REGION_INPUT;
    }
    /* Analog-to-digital converters.  harddriv.cpp:559 (multisync_68k_map):
     *   map(0xb00001, 0xb00001).mirror(0x7fffe).r("adc8", adc0808_device::data_r)
     *   map(0xb00000, 0xb7ffff).w(hd68k_wr2_write)
     *   map(0xb80000, 0xbfffff).rw(hd68k_adc12_r, hd68k_adc_control_w)
     * The 68010 waits on IN0 bit4 (the ADC0809 EOC line) at 0x23d98, so the
     * read side of 0xb00001 and the EOC line both have to be real. */
    if (address >= 0xB00000 && address <= 0xB7FFFF) {
        *out_offset = address;
        return REGION_ADC8;
    }
    if (address >= 0xB80000 && address <= 0xBFFFFF) {
        *out_offset = address;
        return REGION_ADC_CTRL;
    }
    /* MC68010 host-port window.  The TMS34010 VRAM is in the GSP's separate
     * bit-addressed space; it is not directly mapped over the 68010 C00000
     * range (harddriv.cpp:562, 611, 651). */
    if (address >= 0xC00000u && address <= 0xC03FFFu) {
        *out_offset = address;
        return REGION_GSP;
    }
    /* GSP palette RAM */
    if ((address & 0xF5000000) == 0xF5000000 ||
        (address & 0xF5800000) == 0xF5800000) {
        *out_offset = address;
        return REGION_GSP_PAL;
    }

    *out_offset = address;
    return REGION_UNMAPPED;
}

/* ── ROM helpers ── */

/* The maincpu program ROM is ONE flat region, not a banked one.
 * ROM_START(stunrun) at harddriv.cpp:2691 loads six 128 KiB even/odd pairs
 * at 0x000000, 0x020000, 0x040000, 0x060000, 0x080000 and 0x0a0000, and
 * multisync_68k_map maps 0x000000-0x0fffff straight through with .rom().
 * There is no bank-select register on this board, so the ADDRESS picks the
 * bank.  The previous code masked the address into a single 128 KiB bank and
 * used cpu->rom_bank, which nothing ever sets away from 0: every fetch above
 * 0x01ffff silently aliased back into bank 0.  That is why the boot code
 * (all below 0x10000) worked while the main loop's flag words at $801e /
 * $8036 / $8046 / $8056 -- real ROM constants 0x4552 / 0x4154 / 0x4255 /
 * 0x3638 -- read as 0 and the game never left its idle poll at 0x1032. */
static uint8_t rom_read_byte(const musashi_cpu_t *cpu, uint32_t addr)
{
    unsigned bank = addr / MUSASHI_ROM_BANK_SIZE;
    unsigned off  = addr % MUSASHI_ROM_BANK_SIZE;
    if (bank >= MUSASHI_NUM_ROM_BANKS)
        return 0xFFu;               /* map.unmap_value_high() */
    return cpu->rom_banks[bank][off];
}

static uint16_t rom_read_word(const musashi_cpu_t *cpu, uint32_t addr)
{
    uint8_t even = rom_read_byte(cpu, addr);
    uint8_t odd  = rom_read_byte(cpu, addr + 1);
    return (uint16_t)((even << 8) | odd);
}

static uint32_t rom_read_long(const musashi_cpu_t *cpu, uint32_t addr)
{
    uint16_t hi = rom_read_word(cpu, addr);
    uint16_t lo = rom_read_word(cpu, addr + 2);
    return ((uint32_t)hi << 16) | lo;
}

/* ── Public API ── */

musashi_cpu_t *musashi_cpu_create(void)
{
    musashi_cpu_t *cpu = calloc(1, sizeof(*cpu));
    if (!cpu) return NULL;
    cpu->rom_bank = 0;
    cpu->rom_bank_reg = 0;
    cpu->ports = NULL;

    /* GSP memory + host-write log.  These start zeroed, which is deliberate:
     * it means a dump taken at any frame shows only what the 68010 actually
     * wrote, with no residue from initialisation. */
    cpu->gsp_vram      = calloc(1, MUSASHI_GSP_VRAM_BYTES);
    cpu->gsp_vram_2bpp = calloc(1, MUSASHI_GSP_VRAM2BPP_BYTES);
    cpu->gsp_pal_lo    = calloc(1, MUSASHI_GSP_PAL_BYTES);
    cpu->gsp_pal_hi    = calloc(1, MUSASHI_GSP_PAL_BYTES);
    cpu->gsp_ctrl_lo   = calloc(1, 0x100);
    cpu->gsp_ctrl_hi   = calloc(1, 0x100);
    cpu->adsp_pgm_ram  = calloc(MUSASHI_ADSP_PGM_WORDS, sizeof(uint16_t));
    cpu->gsp_wlog     = calloc(MUSASHI_GSP_WLOG_CAP,
                               sizeof(musashi_gsp_write_t));
    cpu->gsp_bus_log  = calloc(MUSASHI_GSP_BUS_LOG_INITIAL,
                               sizeof(musashi_gsp_bus_event_t));
    cpu->gsp_bus_log_cap = MUSASHI_GSP_BUS_LOG_INITIAL;
    cpu->pc_hist      = NULL;
    if (g_pc_hist_enabled)
        cpu->pc_hist = calloc(MUSASHI_PC_HIST_SIZE, sizeof(uint32_t));

    /* ADC0809 power-on state, from adc0808.cpp's constructor
     * (m_eoc(1), m_start(0), m_sar(0xff)) and the channel defaults in
     * INPUT_PORTS_START(stunrun) at harddriv.cpp:1129-1153:
     *   8BADC.0 / 8BADC.2 are the analogue sticks, default 0x80
     *   8BADC.1,3,4,5,6,7 are IP_ACTIVE_LOW / IPT_UNUSED, so they read back
     *   at the inactive level, 0xff.
     * EOC starts asserted, which is what stops the game loop's
     * `btst #4,$60c001` spin at 0x23d98 from wedging before the first
     * conversion is ever started. */
    cpu->adc8_address = 0;
    cpu->adc8_start = 0;
    cpu->adc8_eoc = 1;
    cpu->adc8_phase = 0;
    cpu->adc8_busy = 0;
    cpu->adc8_data = 0xFFu;
    cpu->adc8_in[0] = 0x80u;
    cpu->adc8_in[1] = 0xFFu;
    cpu->adc8_in[2] = 0x80u;
    cpu->adc8_in[3] = 0xFFu;
    cpu->adc8_in[4] = 0xFFu;
    cpu->adc8_in[5] = 0xFFu;
    cpu->adc8_in[6] = 0xFFu;
    cpu->adc8_in[7] = 0xFFu;
    cpu->adc_control = 0;
    cpu->adc12_select = 0;
    cpu->adc12_byte = 0;
    cpu->adc12_data = 0;
    cpu->wheel = 0;

    if (!cpu->gsp_vram || !cpu->gsp_vram_2bpp || !cpu->gsp_pal_lo ||
        !cpu->gsp_pal_hi || !cpu->gsp_ctrl_lo || !cpu->gsp_ctrl_hi ||
        !cpu->gsp_wlog || !cpu->adsp_pgm_ram || !cpu->gsp_bus_log ||
        (g_pc_hist_enabled && !cpu->pc_hist)) {
        musashi_cpu_destroy(cpu);
        return NULL;
    }

    s_cpu = cpu;
    return cpu;
}

void musashi_cpu_destroy(musashi_cpu_t *cpu)
{
    if (!cpu) return;
    if (s_cpu == cpu) s_cpu = NULL;
    free(cpu->gsp_vram);
    free(cpu->gsp_vram_2bpp);
    free(cpu->gsp_pal_lo);
    free(cpu->gsp_pal_hi);
    free(cpu->gsp_ctrl_lo);
    free(cpu->gsp_ctrl_hi);
    free(cpu->gsp_wlog);
    free(cpu->gsp_bus_log);
    free(cpu->adsp_pgm_ram);
    free(cpu->pc_hist);
    free(cpu);
}

void musashi_cpu_set_pc_hist(int enabled)
{
    g_pc_hist_enabled = enabled ? 1 : 0;
}

void musashi_cpu_write_pc_histogram(const musashi_cpu_t *cpu, const char *path)
{
    if (!path || path[0] == '\0') return;
    FILE *fh = fopen(path, "w");
    if (!fh) return;
    const uint32_t *hist = (cpu && cpu->pc_hist) ? cpu->pc_hist
                                                  : g_pc_hist_scratch;
    for (unsigned pc = 0; pc < MUSASHI_PC_HIST_SIZE; pc++)
        if (hist[pc])
            fprintf(fh, "0x%06X %lu\n", pc, (unsigned long)hist[pc]);
    fclose(fh);
}

int musashi_cpu_load_rom_bank(musashi_cpu_t *cpu, unsigned bank,
                              const uint8_t *even_bytes,
                              const uint8_t *odd_bytes)
{
    if (bank >= MUSASHI_NUM_ROM_BANKS) return -1;
    if (!even_bytes || !odd_bytes) return -1;

    /* De-interleave: even_bytes[i] = MSB of word i, odd_bytes[i] = LSB of word i.
     * MC68010 big-endian bus: even byte address = MSB, odd byte address = LSB.
     * We store as linear byte array where byte[N] = byte at address N. */
    for (unsigned i = 0; i < MUSASHI_ROM_BANK_SIZE; i++) {
        cpu->rom_banks[bank][i] = (i & 1) ? odd_bytes[i >> 1] : even_bytes[i >> 1];
    }
    return 0;
}

void musashi_cpu_set_ports(musashi_cpu_t *cpu, stunrun_fake_ports_t *ports)
{
    cpu->ports = ports;
}

void musashi_cpu_set_jsa_latches(musashi_cpu_t *cpu,
                                 struct stunrun_jsa_latches *latches)
{
    if (cpu == NULL)
        return;
    cpu->jsa_latches = latches;
}

void musashi_cpu_set_gsp_bus_observer(
    musashi_cpu_t *cpu, musashi_gsp_bus_observer_t observer,
    void *context)
{
    if (cpu == NULL)
        return;
    cpu->gsp_bus_observer = observer;
    cpu->gsp_bus_observer_context = context;
}

int musashi_cpu_gsp_reset_released(const musashi_cpu_t *cpu)
{
    return cpu != NULL && cpu->gsp_reset_released != 0;
}

void musashi_cpu_reset(musashi_cpu_t *cpu)
{
    if (cpu == NULL)
        return;
    s_cpu = cpu;
    cpu->gsp_reset_released = 0;
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68010);

    /* Autovector interrupts — no callback needed for STUN Runner's IRQ levels */
    m68k_set_int_ack_callback(NULL);

    /* No reset callback needed — we call m68k_pulse_reset() below */
    m68k_set_reset_instr_callback(NULL);

    /* CPU override hook: redirects hot-path PC ranges to C implementations.
     * cpu_override_init() must be called before this function.
     *
     * NOTE: m68kops.c in this Musashi tree was generated with
     * M68K_INSTRUCTION_HOOK = M68K_OPT_OFF, so this callback is never
     * actually invoked — overrides do not fire until the ops table is
     * regenerated with the hook enabled.  musashi_cpu_run() single-steps
     * instead when the control-flow log is on, which is the only place
     * per-instruction observation currently works. */
    m68k_set_instr_hook_callback(cpu_override_dispatch);

    m68k_pulse_reset();
}

int musashi_cpu_run(musashi_cpu_t *cpu, int cycles)
{
    if (g_pc_hist_enabled) {
        /* Where do the cycles actually go?  Single-step and tally the PC at
         * each instruction boundary.  Enabled by STUNRUN_PC_HIST=<path>;
         * written by musashi_cpu_write_pc_histogram(). */
        int done = 0;
        while (done < cycles) {
            unsigned pc = m68k_get_reg(NULL, M68K_REG_PC);
            if (pc < MUSASHI_PC_HIST_SIZE) {
                if (cpu) cpu->pc_hist[pc]++;
                else g_pc_hist_scratch[pc]++;
            }
            done += m68k_execute(1);
        }
        if (cpu) {
            cpu->total_cycles += (uint32_t)done;
            dot_clock_advance(cpu, (uint32_t)done);
        }
        return done;
    }
    if (g_flow_log_enabled) {
        /* Musashi's m68kops.c was generated with M68K_INSTRUCTION_HOOK off,
         * so the per-instruction callback never fires and the hook route is
         * unavailable.  m68k_execute(1) runs exactly one instruction, so
         * stepping here gives the control-flow log the same per-instruction
         * visibility the hook would have.  Only paid for while the log is on. */
        int done = 0;
        while (done < cycles) {
            flow_log_instruction(m68k_get_reg(NULL, M68K_REG_PC));
            done += m68k_execute(1);
        }
        if (cpu) {
            cpu->total_cycles += (uint32_t)done;
            dot_clock_advance(cpu, (uint32_t)done);
        }
        return done;
    }
    int done = m68k_execute(cycles);
    if (cpu) {
        cpu->total_cycles += (uint32_t)done;
        dot_clock_advance(cpu, (uint32_t)done);
    }
    return done;
}

void musashi_cpu_set_irq(musashi_cpu_t *cpu, unsigned level)
{
    (void)cpu;
    m68k_set_irq(level);
}

uint32_t musashi_cpu_total_cycles(const musashi_cpu_t *cpu)
{
    return cpu ? cpu->total_cycles : 0u;
}

uint32_t musashi_cpu_get_pc(const musashi_cpu_t *cpu)
{
    (void)cpu;
    return m68k_get_reg(NULL, M68K_REG_PC);
}

/* Register accessors used by the trace and the GDB stub.  Musashi's own
 * m68k_get_reg/m68k_set_reg take numeric indices, so the name-based API is
 * the mapping layer.  Names are lower-case with no prefix: "d0".."d7",
 * "a0".."a7", plus "pc", "sr", "usp", "ssp" and "vbr". */
static int musashi_reg_index(const char *name)
{
    if (!name || !name[0]) return -1;
    if (strcmp(name, "pc") == 0) return M68K_REG_PC;
    if (strcmp(name, "sr") == 0) return M68K_REG_SR;
    if (strcmp(name, "usp") == 0) return M68K_REG_USP;
    if (strcmp(name, "ssp") == 0) return M68K_REG_ISP;
    if (strcmp(name, "msp") == 0) return M68K_REG_MSP;
    if (strcmp(name, "vbr") == 0) return M68K_REG_VBR;
    char kind = name[0];
    const char *digits = (kind == 'd' || kind == 'D') ? name + 1 :
                         (kind == 'a' || kind == 'A') ? name + 1 : NULL;
    if (!digits || !isdigit((unsigned char)digits[0]) || digits[1] != '\0')
        return -1;
    int idx = digits[0] - '0';
    if (idx < 0 || idx > 7) return -1;
    return ((kind == 'd' || kind == 'D') ? M68K_REG_D0 : M68K_REG_A0) + idx;
}

uint32_t musashi_cpu_get_reg(const musashi_cpu_t *cpu, const char *reg_name)
{
    (void)cpu;
    int idx = musashi_reg_index(reg_name);
    return (idx < 0) ? 0u : m68k_get_reg(NULL, (unsigned)idx);
}

static void musashi_cpu_set_reg(musashi_cpu_t *cpu, const char *name,
                                uint32_t value)
{
    (void)cpu;
    int idx = musashi_reg_index(name);
    if (idx >= 0)
        m68k_set_reg((unsigned)idx, value);
}

/* ── GSP host port ── */

/* Register indices as the TMS34010 numbers them (tms34010.h:1094-1097). */
#define TMS_HOST_ADDR_L 0
#define TMS_HOST_ADDR_H 1
#define TMS_HOST_DATA   2
#define TMS_HOST_CONTROL 3

/* Decode a byte offset in the 0xC00000 window into a TMS34010 host register.
 *
 * MAME does NOT hand the handler a byte offset.  Two separate shifts apply:
 *
 *  1. emumem passes the offset already reduced to a WORD index -- the 68010
 *     program space is a 16-bit space, so byte address 0xC0000C arrives as
 *     word index 6.
 *  2. harddriv_m.cpp:150 then does `offset = (offset / 2) ^ 1`, halving to a
 *     word-pair index and swapping each adjacent pair.
 *
 * Composed with our byte offset: reg = ((byte_offset >> 1) / 2) ^ 1 =
 * (byte_offset >> 2) ^ 1, giving
 *
 *   0xC00000-0xC00003 -> HOST_ADDR_H       0xC00008-0xC0000B -> HOST_CONTROL
 *   0xC00004-0xC00007 -> HOST_ADDR_L       0xC0000C-0xC0000F -> HOST_DATA
 *
 * This pairing is what makes the ROM's `move.l #$f4800000,$c00002.l` (ROM
 * offset 0x3EC) set the host address in one instruction: the 68010 splits the
 * long into word@0x02 = 0xF480 -> ADDR_H and word@0x04 = 0x0000 -> ADDR_L.
 * Decoding byte offsets directly drops every DATA/CONTROL access on the floor,
 * which is why the boot FIFO loop wrote nothing.
 *
 * REFUTED CLAIM, kept because the error is easy to repeat.  harddriv_m.cpp:150
 * looks like it receives a raw byte offset -- emumem's dispatch_write passes
 * `offset` through unchanged -- so `(byte_offset / 2) ^ 1` looks correct and
 * `(byte_offset >> 2) ^ 1` looks like a double shift.  It is the other way
 * round.  Measured with probes on both sides of the same 600-frame run:
 *
 *     MAME  (raw is the word index it receives)
 *       reg 0 HSTADRL 3023   first raw=0x02
 *       reg 1 HSTADRH 3023   first raw=0x01
 *       reg 2 HSTDAT  1101056 first raw=0x06
 *       reg 3 HSTCTL  162    first raw=0x04
 *     ours (raw is the byte offset)
 *       reg 2 HSTDAT  372769  first raw=0x0c
 *
 * MAME's raw=6 and our 0x0C are the SAME access: word index 6 is byte address
 * 0xC0000C.  Changing the decode to `(byte_offset / 2) ^ 1` sends HSTDAT to
 * 0xC00006, which the ROM never touches, and the frame-600 host-write count
 * falls from 372,769 to ZERO.  So the stall is NOT a register-decode bug.
 */
static unsigned gsp_host_reg(unsigned byte_offset)
{
    return (byte_offset >> 2u) ^ 1u;
}

/* STUNRUN-HOSTPROBE: mirror of the probe in MAME's hd68k_gsp_io_w, so the two
 * can be compared directly.  Records which byte offsets the ROM writes and how
 * each one decodes.  Enabled with STUNRUN_GSP_HOSTPROBE=<path>.
 *
 * The MAME probe lives in mame-ref/src/mame/atari/harddriv_m.cpp; keep the two
 * in step or the comparison is meaningless. */
static void stunrun_hostprobe(unsigned raw, uint16_t data)
{
    static uint64_t s_count[8];
    static uint64_t s_off_count[16];
    static uint32_t s_first_off[8];
    static uint32_t s_first_data[8];
    static int s_seen[8];

    unsigned reg = gsp_host_reg(raw);
    if (reg < 8u) {
        s_count[reg]++;
        if (!s_seen[reg]) {
            s_seen[reg] = 1;
            s_first_off[reg] = raw;
            s_first_data[reg] = data;
        }
    }
    s_off_count[raw & 15u]++;

    const char *path = getenv("STUNRUN_GSP_HOSTPROBE");
    if (!path) return;
    FILE *f = fopen(path, "w");
    if (!f) return;
    static const char *names[4] = { "HSTADRL", "HSTADRH", "HSTDAT", "HSTCTL" };
    /* Note the different units: MAME's probe reports the WORD index its
     * handler receives, ours reports the byte offset.  MAME's raw=6 is our
     * raw=0x0c. */
    fprintf(f, "68010 -> TMS34010 host window, our decode\n");
    fprintf(f, "(raw is the BYTE offset; reg is (raw>>2)^1; MAME's word\n"
               " index for the same access is raw/2)\n\n");
    for (int i = 0; i < 4; i++)
        fprintf(f, "  reg %d %-8s writes %-10llu first: raw=0x%02x data=0x%04x\n",
                i, names[i], (unsigned long long)s_count[i],
                (unsigned)s_first_off[i], (unsigned)s_first_data[i]);
    fprintf(f, "\n  raw-offset histogram (raw & 15):\n   ");
    for (int i = 0; i < 16; i++)
        fprintf(f, " %02x:%llu", i, (unsigned long long)s_off_count[i]);
    fprintf(f, "\n");
    fclose(f);
}

/* The 2bpp write expander (harddriv_v.cpp:300-316) does a masked
 * read-modify-write of 32 bytes of MAIN VRAM, not a plain store:
 *
 *     uint32_t *dest = (uint32_t *)&m_gsp_vram[offset * 4];
 *     uint32_t *mask = &m_mask_table[data * 2];
 *     uint32_t color = m_gsp_control_lo[0];
 *     color |= color << 16;
 *     ... eight dest words, two mask halves each ...
 *
 * m_mask_table is built at harddriv_v.cpp:41-57; for the multisync board
 * (m_gsp_multisync) the low half packs bits 0x0001/0x0004/0x0010/0x0040 into
 * byte lanes 0..3 and the high half packs 0x0100/0x0400/0x1000/0x4000.
 * MASK(n) is 0xff << (n*8) (harddriv_v.cpp:24).
 */
#define MUSASHI_GSP_LANE(n) (0x000000FFu << ((n) * 8))

static uint32_t gsp_2bpp_mask_word(uint16_t data, unsigned half)
{
    static const uint16_t bits[2][4] = {
        { 0x0001u, 0x0004u, 0x0010u, 0x0040u },
        { 0x0100u, 0x0400u, 0x1000u, 0x4000u },
    };
    uint32_t mask = 0;
    for (unsigned n = 0; n < 4; n++)
        if (data & bits[half][n])
            mask |= MUSASHI_GSP_LANE(n);
    return mask;
}

/* hdgsp_control_hi_w (harddriv_v.cpp:211-256): offsets 0x02/0x03/0x04 each set
 * one bit of the palette bank from bit 3 of the offset, and 0x01 sets the
 * fine scroll (low 4 bits on the multisync board).  Keeping the bank here is
 * what makes the palette handlers' `offset & 0xff` aliasing meaningful. */
static unsigned gsp_palette_bank(const musashi_cpu_t *cpu)
{
    unsigned bank = 0;
    if (cpu->gsp_ctrl_hi[0x03] & 0x08u) bank |= 2;
    if (cpu->gsp_ctrl_hi[0x04] & 0x08u) bank |= 4;
    if (cpu->gsp_ctrl_hi[0x02] & 0x08u) bank |= 1;
    return bank;
}

/* hdgsp_paletteram_{lo,hi}_w: only the first 256 entries are addressable;
 * the rest are reached by moving the bank.  1024 entries total
 * (harddriv.cpp:399-400, :1508). */
static unsigned gsp_palette_index(const musashi_cpu_t *cpu, uint32_t off)
{
    return gsp_palette_bank(cpu) * 0x100u + (off & 0xFFu);
}

/* Write a 16-bit word into GSP memory space.  Mirrors the multisync GSP map at
 * harddriv.cpp:619-629 *including the handler semantics*, not just the
 * addresses: the palette halves are banked and the 2bpp window is a masked
 * expander over main VRAM.  Storing the raw words instead made every GSP
 * region diverge from MAME on the first frame. */
static void gsp_mem_write_word(musashi_cpu_t *cpu, uint32_t addr, uint16_t value)
{
    if (cpu->gsp_wlog_len < MUSASHI_GSP_WLOG_CAP) {
        cpu->gsp_wlog[cpu->gsp_wlog_len].addr = addr;
        cpu->gsp_wlog[cpu->gsp_wlog_len].data = value;
        cpu->gsp_wlog[cpu->gsp_wlog_len].pc =
            (uint32_t)m68k_get_reg(NULL, M68K_REG_PC);
        cpu->gsp_wlog_len++;
    }

    /* 0xFF000000-0xFFFFFFFF: the VRAM share.
     *
     * KNOWN DIVERGENCE, unresolved -- docs/mc68010-boot-findings.md section 19.
     * The comment this replaced said "the 4 MiB share ... 0xffc00000-0xffffffff
     * aliases onto it".  That was wrong twice over: MUSASHI_GSP_VRAM_BYTES is
     * 0x80000, so the offset below masks with 0x7FFFF (a 512 KiB period, not
     * 4 MiB), and MAME's `.mirror(0x0400000)` over a 0xff800000-0xffbfffff
     * window aliases only the 512 KiB at 0xffc00000-0xffc7ffff, leaving
     * 0xffc80000-0xffffffff unmapped.  The predicate above accepts all of
     * 0xff000000-0xffffffff and this mask folds the difference into VRAM
     * instead of dropping it.  351,144 captured writes land in that gap.
     *
     * A host word write does NOT store two bytes.  MAME models GSP program
     * memory as a 32-bit-wide space with AddrShift = 3
     * (memory_access<32, 1, 3, ENDIANNESS_LITTLE>::specific m_program,
     * tms34010.h:992), so one 16-bit host write covers a 16-byte cell with
     * each source byte replicated eight times.  Measured live against MAME
     * with a Lua probe at frame 2:
     *   write_u16(B+0x100, 0x1234) -> B+0x100..0x107 = 34 x8, B+0x108..0x10f = 12 x8
     *   write_u32(B+0x400, 0xdeadbeef) -> ef x8, be x8, ad x8, de x8
     *   write_u16 at an odd halfword offset still lands on the 16-byte cell
     * Storing a plain big-endian word left the display list and GSP code
     * regions byte-wrong, which is why gsp_vram.bin matched only 41% while
     * main_ram.bin matched 91%. */
    if ((addr & 0xFF000000u) == 0xFF000000u) {
        uint32_t off = ((addr - 0xFF800000u) &
                        (MUSASHI_GSP_VRAM_BYTES - 1u)) & ~0xFu;
        uint8_t lo = (uint8_t)(value & 0xFFu);
        uint8_t hi = (uint8_t)(value >> 8);
        for (unsigned k = 0; k < 8u; k++) {
            cpu->gsp_vram[off + k]      = lo;
            cpu->gsp_vram[off + 8u + k] = hi;
        }
        cpu->gsp_counts.vram++;
        return;
    }
    /* 0x02000000-0x020FFFFF: masked 32-byte expander over main VRAM. */
    if ((addr & 0xFFF00000u) == 0x02000000u) {
        uint32_t offset = ((addr & 0x000FFFFFu) >> 1);
        uint32_t color = cpu->gsp_ctrl_lo[0];
        color |= color << 16;
        for (unsigned w = 0; w < 8; w++) {
            uint32_t mask = gsp_2bpp_mask_word(value, w & 1u);
            if (mask == 0u)
                continue;
            /* m_gsp_vram is a uint16_t share (harddriv.h:353), so the cast in
             * harddriv_v.cpp:302 advances offset * 4 ELEMENTS = 8 bytes. */
            uint32_t o = (offset * 8u + w * 4u);
            /* MAME indexes m_gsp_vram unchecked, so an offset past the share
             * would corrupt the heap there.  Our fill tops out at byte
             * 0x17FFC0 (word offsets 0..0x2FFF8 step 8, data 0xFFFF), well
             * inside 4 MiB -- the counter is here so that stops being
             * true without the diff silently going wrong. */
            if (o + 4u > MUSASHI_GSP_VRAM_BYTES)
                cpu->gsp_counts.vram_2bpp_oob++;
            o &= (MUSASHI_GSP_VRAM_BYTES - 1u) & ~3u;
            uint32_t cur = ((uint32_t)cpu->gsp_vram[o] << 24) |
                           ((uint32_t)cpu->gsp_vram[o + 1] << 16) |
                           ((uint32_t)cpu->gsp_vram[o + 2] << 8) |
                           (uint32_t)cpu->gsp_vram[o + 3];
            cur = (cur & ~mask) | (color & mask);
            cpu->gsp_vram[o]     = (uint8_t)(cur >> 24);
            cpu->gsp_vram[o + 1] = (uint8_t)(cur >> 16);
            cpu->gsp_vram[o + 2] = (uint8_t)(cur >> 8);
            cpu->gsp_vram[o + 3] = (uint8_t)cur;
        }
        cpu->gsp_counts.vram_2bpp++;
        return;
    }
    /* 0xF5000000 palette low / 0xF5800000 palette high */
    if ((addr & 0xFFFFF000u) == 0xF5000000u) {
        uint32_t i = gsp_palette_index(cpu, addr) * 2u;
        cpu->gsp_pal_lo[i]     = (uint8_t)(value >> 8);
        cpu->gsp_pal_lo[i + 1] = (uint8_t)(value & 0xFF);
        cpu->gsp_counts.pal_lo++;
        return;
    }
    if ((addr & 0xFFFFF000u) == 0xF5800000u) {
        uint32_t i = gsp_palette_index(cpu, addr) * 2u;
        cpu->gsp_pal_hi[i]     = (uint8_t)(value >> 8);
        cpu->gsp_pal_hi[i + 1] = (uint8_t)(value & 0xFF);
        cpu->gsp_counts.pal_hi++;
        return;
    }
    /* 0xF4000000 / 0xF4800000 control registers */
    if ((addr & 0xFFFFFF00u) == 0xF4000000u) {
        uint32_t off = addr & 0xFFu;
        off &= ~1u;
        cpu->gsp_ctrl_lo[off]     = (uint8_t)(value >> 8);
        cpu->gsp_ctrl_lo[off + 1] = (uint8_t)(value & 0xFF);
        cpu->gsp_counts.control++;
        return;
    }
    if ((addr & 0xFFFFFF00u) == 0xF4800000u) {
        uint32_t off = addr & 0xFFu;
        off &= ~1u;
        cpu->gsp_ctrl_hi[off]     = (uint8_t)(value >> 8);
        cpu->gsp_ctrl_hi[off + 1] = (uint8_t)(value & 0xFF);
        cpu->gsp_counts.control++;
        return;
    }

    cpu->gsp_counts.unmapped++;
}

/* Read a 16-bit word back out of GSP memory space.  The 68010 polls VRAM to
 * see what the GSP has drawn, so this is not merely a debugging affordance.
 * Takes a non-const CPU because the host-port read path auto-increments the
 * address latches as a side effect, exactly as the hardware does. */
static uint16_t gsp_mem_read_word(musashi_cpu_t *cpu, uint32_t addr)
{
    if ((addr & 0xFF000000u) == 0xFF000000u) {
        uint32_t off = ((addr - 0xFF800000u) &
                        (MUSASHI_GSP_VRAM_BYTES - 1u)) & ~0xFu;
        /* Reads broadcast the same way: every 2-byte offset inside the cell
         * returns the word that was written to it, so the value has to be
         * reassembled from the replicated bytes rather than read as a pair.
         * Cells the 2bpp expander touched with raw dwords are the one case
         * this mis-reports; the 68010 does not poll those through this path. */
        return (uint16_t)(((uint16_t)cpu->gsp_vram[off + 8u] << 8) |
                          cpu->gsp_vram[off]);
    }
    if ((addr & 0xFFF00000u) == 0x02000000u) {
        /* hdgsp_vram_2bpp_r() returns 0 unconditionally (harddriv_v.cpp:266). */
        return 0;
    }
    if ((addr & 0xFFFFF000u) == 0xF5000000u) {
        uint32_t i = gsp_palette_index(cpu, addr) * 2u;
        return (uint16_t)((cpu->gsp_pal_lo[i] << 8) | cpu->gsp_pal_lo[i + 1]);
    }
    if ((addr & 0xFFFFF000u) == 0xF5800000u) {
        uint32_t i = gsp_palette_index(cpu, addr) * 2u;
        return (uint16_t)((cpu->gsp_pal_hi[i] << 8) | cpu->gsp_pal_hi[i + 1]);
    }
    if ((addr & 0xFFFFFF00u) == 0xF4000000u) {
        uint32_t off = (addr & 0xFFu) & ~1u;
        return (uint16_t)((cpu->gsp_ctrl_lo[off] << 8) | cpu->gsp_ctrl_lo[off + 1]);
    }
    if ((addr & 0xFFFFFF00u) == 0xF4800000u) {
        uint32_t off = (addr & 0xFFu) & ~1u;
        return (uint16_t)((cpu->gsp_ctrl_hi[off] << 8) | cpu->gsp_ctrl_hi[off + 1]);
    }
    return 0xFFFF;
}

static uint32_t gsp_host_address(const musashi_cpu_t *cpu)
{
    return (((uint32_t)cpu->hstadrh << 16) | cpu->hstadrl) & 0xFFFFFFF0u;
}

/* Advance the host address latches by one 16-byte GSP word. */
static void gsp_host_bump_address(musashi_cpu_t *cpu)
{
    uint32_t next = (gsp_host_address(cpu) + 0x10u) & 0xFFFFFFFFu;
    cpu->hstadrh = (uint16_t)(next >> 16);
    cpu->hstadrl = (uint16_t)(next & 0xFFFFu);
}

static void gsp_emit_bus_event(musashi_cpu_t *cpu,
                               musashi_gsp_bus_event_kind_t kind,
                               uint32_t address, uint16_t value,
                               uint16_t lane_mask, uint8_t width_bits,
                               uint8_t reset_released)
{
    musashi_gsp_bus_event_t event;

    if (cpu == NULL)
        return;

    event.sequence = cpu->gsp_bus_event_sequence++;
    event.cycles = (uint64_t)cpu->total_cycles;
    event.address = address;
    event.pc = (uint32_t)m68k_get_reg(NULL, M68K_REG_PC);
    event.data = value;
    event.lane_mask = lane_mask;
    event.width_bits = width_bits;
    event.kind = (uint8_t)kind;
    event.gsp_reset_released = reset_released;

    /* Record unconditionally, before the observer check.  The ordered bus
     * stream is needed for offline replay by a GSP provider, and requiring a
     * live observer to be attached just to capture it made the dump
     * unavailable in the normal shell run.  The log is bounded and dropped
     * with the CPU. */
    if (cpu->gsp_bus_log_len == cpu->gsp_bus_log_cap &&
        cpu->gsp_bus_log_cap < MUSASHI_GSP_BUS_LOG_MAX) {
        size_t next = cpu->gsp_bus_log_cap * 2u;
        if (next > MUSASHI_GSP_BUS_LOG_MAX) next = MUSASHI_GSP_BUS_LOG_MAX;
        musashi_gsp_bus_event_t *grown = realloc(
            cpu->gsp_bus_log, next * sizeof(musashi_gsp_bus_event_t));
        if (grown) {
            cpu->gsp_bus_log = grown;
            cpu->gsp_bus_log_cap = next;
        }
    }
    if (cpu->gsp_bus_log_len < cpu->gsp_bus_log_cap) {
        cpu->gsp_bus_log[cpu->gsp_bus_log_len++] = event;
    } else {
        /* Counted, never silently dropped: a truncated replay stream would
         * look like a VRAM mismatch rather than a lost record. */
        cpu->gsp_bus_log_dropped++;
    }

    if (cpu->gsp_bus_observer != NULL)
        cpu->gsp_bus_observer(cpu->gsp_bus_observer_context, &event);
}

/* Preserve actual MC68010 bus addresses; provider adapters choose how to
 * decode the host window.  C transcription adapters bypass this callback. */
static void gsp_emit_host_write_event(musashi_cpu_t *cpu,
                                      uint32_t address,
                                      uint16_t value,
                                      uint16_t lane_mask,
                                      uint8_t width_bits)
{
    if (address < 0x00C00000u || address > 0x00C03FFFu)
        return;
    gsp_emit_bus_event(cpu, MUSASHI_GSP_BUS_HOST_WINDOW, address, value,
                       lane_mask, width_bits, 0u);
}

/* MAME's NWR handler uses a word-index offset: bit 3 chooses the line value,
 * bits 0-2 choose the function, and function 6 controls active-low /GSPRES.
 * For MC68010 byte addresses the word index is `(address - 0x604000) / 2`.
 * `data` itself is explicitly ignored by harddriv_m.cpp:466-495. */
static void gsp_emit_reset_control_event(musashi_cpu_t *cpu,
                                         uint32_t address,
                                         uint16_t value,
                                         uint16_t lane_mask,
                                         uint8_t width_bits)
{
    uint32_t offset;
    uint32_t word_index;
    uint8_t released;

    if (cpu == NULL || address < 0x00604000u || address > 0x00607FFFu)
        return;
    offset = address - 0x00604000u;
    word_index = offset >> 1;
    if ((word_index & 7u) != 6u)
        return;

    released = (uint8_t)((word_index >> 3) & 1u);
    cpu->gsp_reset_released = released;
    gsp_emit_bus_event(cpu, MUSASHI_GSP_BUS_RESET_CONTROL, address, value,
                       lane_mask, width_bits, released);
}

/* 16-bit write to the host port window. */
static void gsp_host_write_word(musashi_cpu_t *cpu, unsigned byte_offset,
                                uint16_t value)
{
    switch (gsp_host_reg(byte_offset)) {
    case TMS_HOST_ADDR_H:
        cpu->hstadrh = value;
        break;
    case TMS_HOST_ADDR_L:
        cpu->hstadrl = value;
        break;
    case TMS_HOST_CONTROL:
        cpu->hstctlh = (uint8_t)(value >> 8);
        cpu->hstctll = (uint8_t)(value & 0xFF);
        break;
    case TMS_HOST_DATA: {
        uint32_t addr = gsp_host_address(cpu);
        gsp_mem_write_word(cpu, addr, value);
        /* 0x0800 in HSTCTLH: auto-increment the address after a write. */
        if (cpu->hstctlh & 0x08u) gsp_host_bump_address(cpu);
        break;
    }
    default:
        break;
    }
}

static uint16_t gsp_host_read_word(musashi_cpu_t *cpu, unsigned byte_offset)
{
    switch (gsp_host_reg(byte_offset)) {
    case TMS_HOST_ADDR_H: return cpu->hstadrh;
    case TMS_HOST_ADDR_L: return cpu->hstadrl;
    case TMS_HOST_CONTROL:
        return (uint16_t)(((uint16_t)cpu->hstctlh << 8) | cpu->hstctll);
    case TMS_HOST_DATA: {
        uint16_t v = gsp_mem_read_word(cpu, gsp_host_address(cpu));
        /* 0x1000 in HSTCTLH: auto-increment the address after a read. */
        if (cpu->hstctlh & 0x10u) gsp_host_bump_address(cpu);
        return v;
    }
    default: return 0;
    }
}

/* ── Display-list slice adapter ──────────────────────────────────────────
 *
 * Keep this adapter beside gsp_host_{read,write}_word so the transcribed
 * 68010 slice cannot grow a second interpretation of the TMS34010 host port.
 * The byte offsets below are the actual 68010 window offsets documented in
 * gsp_host_reg(): HSTADRH=$00, HSTADRL=$04, HSTCTL=$08, HSTDAT=$0c.
 */
static void display_list_set_host_address_lo(void *ctx, uint16_t value)
{
    gsp_host_write_word((musashi_cpu_t *)ctx, 0x04u, value);
}

static void display_list_set_host_address_hi(void *ctx, uint16_t value)
{
    gsp_host_write_word((musashi_cpu_t *)ctx, 0x00u, value);
}

static void display_list_set_host_control(void *ctx, uint16_t value)
{
    gsp_host_write_word((musashi_cpu_t *)ctx, 0x08u, value);
}

static void display_list_or_host_control_mask(void *ctx, uint16_t mask)
{
    musashi_cpu_t *cpu = (musashi_cpu_t *)ctx;
    uint16_t value = gsp_host_read_word(cpu, 0x08u);
    gsp_host_write_word(cpu, 0x08u, (uint16_t)(value | mask));
}

static void display_list_write_fifo_word(void *ctx, uint16_t value)
{
    gsp_host_write_word((musashi_cpu_t *)ctx, 0x0Cu, value);
}

static uint16_t display_list_peek_word(void *ctx, uint32_t address)
{
    musashi_cpu_t *cpu = (musashi_cpu_t *)ctx;
    uint16_t value = 0xFFFFu;
    (void)stunrun_mem_read16_be(cpu->ports, address, &value);
    return value;
}

static uint8_t display_list_peek_byte(void *ctx, uint32_t address)
{
    musashi_cpu_t *cpu = (musashi_cpu_t *)ctx;
    uint8_t value = 0xFFu;
    (void)stunrun_mem_read8(cpu->ports, address, &value);
    return value;
}

static void display_list_poke_word(void *ctx, uint32_t address, uint16_t value)
{
    musashi_cpu_t *cpu = (musashi_cpu_t *)ctx;
    /* $608000 is the ROM's watchdog kick.  It is unmapped in the current
     * Musashi machine model, so preserving that model means the write is a
     * no-op here too; work-RAM table stores take the normal canonical path. */
    (void)stunrun_mem_write16_be(cpu->ports, address, value);
}

static void display_list_poke_byte(void *ctx, uint32_t address, uint8_t value)
{
    musashi_cpu_t *cpu = (musashi_cpu_t *)ctx;
    (void)stunrun_mem_write8(cpu->ports, address, value);
}

int musashi_cpu_bind_gsp_display_list(musashi_cpu_t *cpu,
                                      stunrun_gsp_display_list_t *out)
{
    if (cpu == NULL || cpu->ports == NULL || out == NULL)
        return -1;

    out->ctx = cpu;
    out->set_host_address_lo = display_list_set_host_address_lo;
    out->set_host_address_hi = display_list_set_host_address_hi;
    out->set_host_control = display_list_set_host_control;
    out->or_host_control_mask = display_list_or_host_control_mask;
    out->write_fifo_word = display_list_write_fifo_word;
    out->peek_word = display_list_peek_word;
    out->peek_byte = display_list_peek_byte;
    out->poke_word = display_list_poke_word;
    out->poke_byte = display_list_poke_byte;
    return 0;
}

/* Caller bus operations deliberately reuse the same memory callbacks as CPU
 * instructions, including the two ordered word writes at $C00002/$C00004.
 * Temporarily select the bound CPU; do not retain a global selection change. */
static uint16_t upload_read_word(void *ctx, uint32_t address)
{
    musashi_gsp_upload_adapter_t *adapter = ctx;
    musashi_cpu_t *saved = s_cpu;
    s_cpu = adapter->cpu;
    uint16_t value = (uint16_t)m68k_read_memory_16(address);
    s_cpu = saved;
    return value;
}

static uint32_t upload_read_long(void *ctx, uint32_t address)
{
    musashi_gsp_upload_adapter_t *adapter = ctx;
    musashi_cpu_t *saved = s_cpu;
    s_cpu = adapter->cpu;
    uint32_t value = m68k_read_memory_32(address);
    s_cpu = saved;
    return value;
}

static void upload_write_word(void *ctx, uint32_t address, uint16_t value)
{
    musashi_gsp_upload_adapter_t *adapter = ctx;
    musashi_cpu_t *saved = s_cpu;
    s_cpu = adapter->cpu;
    m68k_write_memory_16(address, value);
    s_cpu = saved;
}

static void upload_write_long(void *ctx, uint32_t address, uint32_t value)
{
    musashi_gsp_upload_adapter_t *adapter = ctx;
    musashi_cpu_t *saved = s_cpu;
    s_cpu = adapter->cpu;
    m68k_write_memory_32(address, value);
    s_cpu = saved;
}

static void upload_compile(void *ctx, uint32_t stream)
{
    musashi_gsp_upload_adapter_t *adapter = ctx;
    adapter->compile(adapter->callee_ctx, stream);
}

static void upload_transfer(void *ctx, uint32_t address, uint32_t length)
{
    musashi_gsp_upload_adapter_t *adapter = ctx;
    adapter->transfer(adapter->callee_ctx, address, length);
}

int musashi_cpu_bind_gsp_upload_callers(musashi_gsp_upload_adapter_t *adapter,
                                       stunrun_gsp_upload_callers_t *out)
{
    if (!adapter || !adapter->cpu || !adapter->cpu->ports || !out ||
        !adapter->compile || !adapter->transfer)
        return -1;
    out->opaque = adapter;
    out->read_word = upload_read_word;
    out->read_long = upload_read_long;
    out->write_word = upload_write_word;
    out->write_long = upload_write_long;
    out->compile = upload_compile;
    out->transfer = upload_transfer;
    return 0;
}

/* ── Musashi memory callbacks ── */

/* hd68k_port0_r() — harddriv_m.cpp:201-224.
 *
 *     int temp = (m_sw1.read_safe(0xff) << 8) | m_in0->read();
 *     if (get_hblank(scr)) temp ^= 0x0002;
 *     temp ^= 0x0008;     12-bit EOC always high for now
 *
 * The board's two halves, per the INPUT_PORTS_START(stunrun) block at
 * harddriv.cpp:1084-1128:
 *
 *   SW1 (high byte) — eight PORT_DIPNAMEs whose default is 0x00, so an
 *   untouched machine reads 0x0000.  This matters: the ROM reads port0 with
 *   `move.b $60c000.l,D0` and an even-address byte read on this big-endian
 *   bus takes the HIGH byte, i.e. SW1.  With SW1 = 0 that `beq` skips the
 *   whole self-test dispatch table; with the 0xFF we used to return the ROM
 *   fell into the $0 case and spun.
 *
 *   IN0 (low byte) — active-low bits read 1 when idle, so bits 0 (DIAGN),
 *   1 (/HSYNCB), 2 (/VSYNCB), 5 (service), 6 (COIN2), 7 (COIN1) read 1 and
 *   bits 3 (EOC12) and 4 (EOC8) default to 0: 0xE7.  MAME re-derives bit 1
 *   from the screen by XORing /HSYNCB, and bit 2 arrives through the port as
 *   the active-low screen vblank line, so we synthesize both from the dot
 *   clock rather than trusting a stored value.
 */
static uint16_t port0_status(const musashi_cpu_t *cpu)
{
    /* Fallbacks only -- the live values come from the fake-port table below (see
     * stunrun_ports_init() in fake_ports.c, which transcribes
     * INPUT_PORTS_START(stunrun) at mame-ref/src/mame/atari/harddriv.cpp:1084).
     * These two constants only matter if the port table is unreachable.
     *
     *   bit0 diagnostic jumper (default on)
     *   bit1 /HBLANK   active low, from the raster
     *   bit2 /VSYNCB   active low, from the raster
     *   bit3 12-bit EOC, then unconditionally cleared by the XOR below
     *   bit4 8-bit ADC EOC
     *   bit5 PORT_SERVICE -- SET at rest.  The ROM tests it at 0x254 with
     *        btst #5,$60c001.l / beq $2a0
     *     clear -> 0x2a0 -> 0x506c -> a 230400-word VRAM fill at 0x548e that
     *             MAME's per-frame PC log never shows, then on to the game.
     *     set   -> 0x264 `jsr $3f8`, whose `bsr $67a` at 0x45e is the 900x256
     *             VRAM fill, then `jsr $636` (the 2bpp fill), then
     *             0x286-0x29e set VBR to ROM 0x20000 and `jmp (A1)` where
     *             A1 = [0x20004] = 0x212ae -- the real post-self-test boot.
     *     MAME's PC at frame 1 is 0x69C, i.e. inside the 0x67a fill, so bit 5
     *     reads SET.  That matches frames 1-31 = 0x69C, frames 32-36 = 0x66A
     *     (the 0x636 2bpp fill's loop) in tools/mame_pclog.lua's output.
     *   bit6 /COIN2, bit7 /COIN1, active low, both 1 with no coin */
    uint16_t in0 = 0x00E7u;
    /* SW1: every DIP in stunrun's own block is PORT_DIPNAME(mask, 0x00, ...),
     * so an untouched board reads 0x00.  (The harddriv-wide block at
     * harddriv.cpp:802-826 is the one with 0xFF defaults; using that table
     * sends the ROM into the self-test dispatch at 0x9010.) */
    uint16_t sw1 = 0x0000u;

    /* stunrun_port_read() returns NON-zero on success, so the test has to be
     * `!= 0`.  Written as `== 0` the ports are silently never consulted and
     * these hardcoded fallbacks always win -- which is what kept the boot
     * stuck: every DIP override, including STUNRUN_IN0, was inert. */
    if (cpu && cpu->ports) {
        int32_t v;
        if (stunrun_port_read(cpu->ports, STUNRUN_PORT_MAIN_SW1, &v) != 0)
            sw1 = (uint16_t)(v & 0xFF);
        if (stunrun_port_read(cpu->ports, STUNRUN_PORT_MAIN_IN0, &v) != 0)
            in0 = (uint16_t)(v & 0xFF);
    }

    /* /VSYNCB is PORT_READ_LINE_DEVICE_MEMBER(..., vblank), active low. */
    uint32_t vpos = cpu ? cpu->dot_vpos : 0u;
    in0 = (uint16_t)((in0 & ~(uint16_t)0x0004u) |
                     (vpos >= MUSASHI_V_VISIBLE ? 0u : 0x0004u));
    /* IN0 bit4 is PORT_READ_LINE_DEVICE_MEMBER("mainpcb:adc8", eoc_r) with
     * IP_ACTIVE_HIGH (harddriv.cpp:1092), so the raw ADC0809 EOC line goes
     * in unmangled.  The game loop spins on it at 0x23d98. */
    if (cpu)
        in0 = (uint16_t)((in0 & ~(uint16_t)0x0010u) |
                         (cpu->adc8_eoc ? 0x0010u : 0u));

    uint16_t temp = (uint16_t)((sw1 << 8) | in0);
    uint32_t hpos = cpu ? cpu->dot_hpos : 0u;
    if (hpos > MUSASHI_HBLANK_POS)
        temp ^= 0x0002u;
    temp ^= 0x0008u;     /* 12-bit EOC always high for now */
    return temp;
}

unsigned int m68k_read_memory_8(unsigned int address)
{
    if (!s_cpu) return 0xFF;

    unsigned offset;
    region_t region = route_address(address, &offset);

    switch (region) {
    case REGION_ROM:
        return rom_read_byte(s_cpu, offset);
    case REGION_RAM: {
        uint8_t val;
        if (s_cpu->ports &&
            stunrun_mem_read8(s_cpu->ports, 0xFF8000u | offset, &val))
            return val;
        return 0xFF;
    }
    case REGION_GSP_VRAM_MIRROR: {
        /* VRAM mirror — use the full 32-bit offset */
        uint16_t val;
        if (s_cpu->ports &&
            stunrun_mem_read16_be(s_cpu->ports, offset, &val))
            return (uint8_t)(val >> 8); /* high byte first */
        return 0xFF;
    }
    case REGION_GSP:
        /* Byte reads see one half of the register word.  Big-endian: the low
         * byte offset is the LSB. */
        return (address & 1u)
            ? (gsp_host_read_word(s_cpu, offset - 0x00C00000u) & 0xFF)
            : (gsp_host_read_word(s_cpu, offset - 0x00C00000u) >> 8);
    case REGION_IRQACK: {
        uint16_t port = port0_status(s_cpu);
        return (address & 1u) ? (port & 0xFF) : (port >> 8);
    }
    case REGION_ADC8:
        /* adc0808_device::data_r() returns m_sar.  MAME decodes offset 1 only
         * (mirrored every 0x80000); the rest of the window is open bus. */
        return ((offset & 0xFFFFu) == 0x0001u) ? s_cpu->adc8_data : 0xFFu;
    case REGION_ADC_CTRL:
        /* hd68k_adc12_r(): high nibble or low byte of the 12-bit result. */
        return (uint8_t)adc12_read(s_cpu);
    case REGION_INPUT: {
        /* hd68k_a80000_r (harddriv_m.cpp:196-199) returns m_a80000->read()
         * raw, and INPUT_PORTS_START(stunrun) (harddriv.cpp:1123-1127) gives
         * bit0 BUTTON1, bit1 BUTTON2, bit2 IPT_START1, rest IPT_UNUSED.  The
         * boot gate is btst #$2,$a80001 at 0x50e8 - a clear bit takes jmp
         * $0f94 into the game, a set bit enters the diagnostics routine at
         * 0x4dae which then runs the RAM march tests at 0x59e6 for ~30M
         * cycles.  Stubbing this to 0xFF sent us down the diagnostics path. */
        int32_t value = 0;
        if (s_cpu->ports &&
            stunrun_port_read(s_cpu->ports, STUNRUN_PORT_MAIN_A800, &value))
            return (address & 1u) ? ((uint16_t)value & 0xFF)
                                  : (((uint16_t)value >> 8) & 0xFF);
        return 0xFF;
    }
    case REGION_ADSP_PROG:
        return (offset & 1u) ? (uint8_t)(s_cpu->adsp_pgm_ram[offset >> 1] & 0xFFu)
                             : (uint8_t)(s_cpu->adsp_pgm_ram[offset >> 1] >> 8);
    case REGION_ADSP_IRQ:
        /* hd68k_adsp_irq_state_r: 0xfffd, both flag bits clear while idle. */
        return (offset & 1u) ? 0xFDu : 0xFFu;
    case REGION_SOUNDRST:
        /* hd68k_sound_reset_r returns ~0 and resets the JSA (harddriv_m.cpp:288). */
        return 0xFFu;
    case REGION_SOUND: {
        /* harddriv.cpp:4806 main_response_r.  With no 6502 emulated there is
         * never a response pending, so this returns the not-ready byte the
         * stub has always returned.  The command latch is not touched. */
        uint8_t byte = 0xFFu;
        (void)stunrun_jsa_transport_read(s_cpu->jsa_latches, offset, 1u, &byte);
        return byte;
    }
    case REGION_ADSP_DATA: case REGION_ADSP_BUF:
    case REGION_ADSP_CTRL:
    case REGION_GSP_PAL:
        /* MMIO stubs — return 0xFF for now */
        return 0xFF;
    default:
        return 0xFF;
    }
}

unsigned int m68k_read_memory_16(unsigned int address)
{
    if (!s_cpu) return 0xFFFF;

    unsigned offset;
    region_t region = route_address(address, &offset);

    switch (region) {
    case REGION_ROM:
        return rom_read_word(s_cpu, address);

    case REGION_RAM: {
        uint16_t val;
        if (s_cpu->ports &&
            stunrun_mem_read16_be(s_cpu->ports, 0xFF8000u | offset, &val))
            return val;
        return 0xFFFF;
    }
    case REGION_GSP_VRAM_MIRROR: {
        uint16_t val;
        if (s_cpu->ports &&
            stunrun_mem_read16_be(s_cpu->ports, offset, &val))
            return val;
        return 0xFFFF;
    }
    case REGION_GSP:
        /* harddriv.cpp maps only 0xC00000-0xC03FFF. The word-index decode
         * aliases each adjacent pair of byte offsets; higher indices are
         * invalid host registers and read as zero. */
        return gsp_host_read_word(s_cpu, offset - 0x00C00000u);
    case REGION_IRQACK:
        /* Same status word as the 8-bit path above. */
        return port0_status(s_cpu);
    case REGION_ADC8:
        /* adc0808_device::data_r() returns m_sar.  MAME only decodes offset 1
         * (mirrored every 0x80000); every other byte in the window is open
         * bus and reads as 0xff. */
        if ((offset & 0xFFFFu) == 0x0001u)
            return s_cpu->adc8_data;
        return 0x00FFu;
    case REGION_ADC_CTRL:
        return adc12_read(s_cpu);
    case REGION_INPUT: {
        /* Same port as the 8-bit path above. */
        int32_t value = 0;
        if (s_cpu->ports &&
            stunrun_port_read(s_cpu->ports, STUNRUN_PORT_MAIN_A800, &value))
            return (uint16_t)value;
        return 0xFFFF;
    }
    case REGION_ADSP_PROG:
        return s_cpu->adsp_pgm_ram[offset >> 1];
    case REGION_ADSP_IRQ:
        return 0xFFFDu;
    case REGION_SOUNDRST:
        return 0xFFFFu;
    case REGION_SOUND: {
        /* harddriv.cpp:4806 main_response_r (8-bit device split). */
        uint8_t byte = 0xFFu;
        (void)stunrun_jsa_transport_read(s_cpu->jsa_latches, offset, 2u, &byte);
        return (uint16_t)(((uint16_t)byte << 8) | 0x00FFu);
    }
    case REGION_ADSP_DATA: case REGION_ADSP_BUF:
    case REGION_ADSP_CTRL:
    case REGION_GSP_PAL:
        return 0xFFFF;
    default:
        return 0xFFFF;
    }
}

unsigned int m68k_read_memory_32(unsigned int address)
{
    if (!s_cpu) return 0xFFFFFFFFu;

    unsigned offset;
    region_t region = route_address(address, &offset);

    switch (region) {
    case REGION_ROM:
        return rom_read_long(s_cpu, address);

    case REGION_RAM: {
        uint32_t val;
        if (s_cpu->ports &&
            stunrun_mem_read32_be(s_cpu->ports, 0xFF8000u | offset, &val))
            return val;
        return 0xFFFFFFFFu;
    }
    case REGION_GSP_VRAM_MIRROR: {
        uint32_t val;
        if (s_cpu->ports &&
            stunrun_mem_read32_be(s_cpu->ports, offset, &val))
            return val;
        return 0xFFFFFFFFu;
    }
    case REGION_GSP:
        {
            uint32_t value =
                (uint32_t)gsp_host_read_word(
                    s_cpu, offset - 0x00C00000u) << 16;
            if (address + 2u <= 0x00C03FFFu) {
                value |= gsp_host_read_word(
                    s_cpu, offset - 0x00C00000u + 2u);
            } else {
                value |= 0xFFFFu;
            }
            return value;
        }
    case REGION_INPUT: {
        int32_t value = 0;
        if (s_cpu->ports &&
            stunrun_port_read(s_cpu->ports, STUNRUN_PORT_MAIN_A800, &value))
            return (uint32_t)(uint16_t)value;
        return 0xFFFFFFFFu;
    }
    case REGION_ADSP_PROG:
        return (uint32_t)s_cpu->adsp_pgm_ram[offset >> 1] << 16 |
               (uint32_t)s_cpu->adsp_pgm_ram[(offset + 2) >> 1];
    case REGION_ADSP_IRQ:
        return 0xFFFDFFFDu;
    case REGION_SOUNDRST:
        return 0xFFFFFFFFu;
    case REGION_SOUND: {
        /* harddriv.cpp:4806 main_response_r (8-bit device split). */
        uint8_t byte = 0xFFu;
        (void)stunrun_jsa_transport_read(s_cpu->jsa_latches, offset, 4u, &byte);
        return ((uint32_t)byte << 24) | 0x00FFFFFFu;
    }
    default:
        return 0xFFFFFFFFu;
    }
}

unsigned int m68k_read_immediate_16(unsigned int address)
{
    return m68k_read_memory_16(address);
}

unsigned int m68k_read_immediate_32(unsigned int address)
{
    return m68k_read_memory_32(address);
}

unsigned int m68k_read_pcrelative_8(unsigned int address)
{
    return m68k_read_memory_8(address);
}

unsigned int m68k_read_pcrelative_16(unsigned int address)
{
    return m68k_read_memory_16(address);
}

unsigned int m68k_read_pcrelative_32(unsigned int address)
{
    return m68k_read_memory_32(address);
}

void m68k_write_memory_8(unsigned int address, unsigned int value)
{
    if (!s_cpu) return;

    unsigned offset;
    region_t region = route_address(address, &offset);

    if (region == REGION_UNMAPPED || region == REGION_ROM) return;

    if (!s_cpu->ports) return;

    switch (region) {
    case REGION_RAM:
        stunrun_mem_write8(s_cpu->ports, 0xFF8000u | offset, (uint8_t)value);
        break;
    case REGION_GSP:
        /* Byte write: read-modify-write the 16-bit register word. */
        if (address & 1u) {
            unsigned host_offset = offset - 0x00C00000u;
            uint16_t cur = gsp_host_read_word(s_cpu, host_offset);
            uint16_t merged = (uint16_t)((cur & 0xFF00u) | (value & 0xFFu));
            gsp_host_write_word(s_cpu, host_offset, merged);
            gsp_emit_host_write_event(s_cpu, address, merged, 0x00FFu, 8u);
        } else {
            unsigned host_offset = offset - 0x00C00000u;
            uint16_t cur = gsp_host_read_word(s_cpu, host_offset);
            uint16_t merged = (uint16_t)((cur & 0x00FFu) |
                                         ((value & 0xFFu) << 8));
            gsp_host_write_word(s_cpu, host_offset, merged);
            gsp_emit_host_write_event(s_cpu, address, merged, 0xFF00u, 8u);
        }
        break;
    case REGION_IRQACK:
        /* Any write acks the periodic IRQ (harddriv_m.cpp:111-115 clears
         * m_irq_state unconditionally, then update_interrupts drops line 5).
         * Musashi's m68k_set_irq is level-driven, so dropping to 0 clears. */
        musashi_cpu_set_irq(s_cpu, 0);
        break;
    case REGION_ADC_CTRL:
        /* hd68k_adc_control_w() COMBINE_DATA(&m_adc_control): a byte write
         * updates only its own half of the control word. */
        s_cpu->adc_control = (uint16_t)((s_cpu->adc_control & 0xFF00u) |
                                        ((value & 0xFFu) << 8));
        adc_control_write(s_cpu, s_cpu->adc_control);
        break;
    case REGION_ADC8:
        if ((offset & 0xFFFFu) == 0x0000u)
            s_cpu->wheel = (uint16_t)(value & 0xFF00u);
        break;
    case REGION_ADSP_PROG: {
        uint16_t *cell = &s_cpu->adsp_pgm_ram[offset >> 1];
        if (offset & 1u) *cell = (uint16_t)((*cell & 0xFF00u) | (value & 0xFFu));
        else            *cell = (uint16_t)((*cell & 0x00FFu) | ((value & 0xFFu) << 8));
        break;
    }
    case REGION_SOUND:
        /* harddriv.cpp:4806 main_command_w.  The whole window is the command
         * latch; there is no offset decode to do. */
        (void)stunrun_jsa_transport_write(s_cpu->jsa_latches, offset, 1u,
                                          (uint8_t)value);
        break;
    case REGION_SOUNDRST: {
        uint16_t bus_value = (address & 1u) ? (uint16_t)(value & 0xFFu)
                                            : (uint16_t)((value & 0xFFu) << 8);
        uint16_t lane_mask = (address & 1u) ? 0x00FFu : 0xFF00u;
        gsp_emit_reset_control_event(s_cpu, address, bus_value, lane_mask, 8u);
        break;
    }
    case REGION_ADSP_DATA: case REGION_ADSP_BUF:
    case REGION_ADSP_CTRL:
    case REGION_INPUT: case REGION_GSP_PAL:
    case REGION_GSP_VRAM_MIRROR: case REGION_ADSP_IRQ:
        /* TODO: wire to proper MMIO handlers */
        break;
    }
}

void m68k_write_memory_16(unsigned int address, unsigned int value)
{
    if (!s_cpu) return;

    unsigned offset;
    region_t region = route_address(address, &offset);

    if (region == REGION_UNMAPPED || region == REGION_ROM) return;
    if (!s_cpu->ports) return;

    switch (region) {
    case REGION_RAM:
        stunrun_mem_write16_be(s_cpu->ports, 0xFF8000u | offset, (uint16_t)value);
        break;
    case REGION_GSP:
        /* The display list arrives here, one word per HSTDAT write. */
        stunrun_hostprobe(offset - 0x00C00000u, (uint16_t)value);
        gsp_host_write_word(s_cpu, offset - 0x00C00000u,
                            (uint16_t)value);
        gsp_emit_host_write_event(s_cpu, address, (uint16_t)value,
                                  0xFFFFu, 16u);
        break;
    case REGION_SOUNDRST:
        gsp_emit_reset_control_event(s_cpu, address, (uint16_t)value,
                                     0xFFFFu, 16u);
        break;
    case REGION_GSP_VRAM_MIRROR:
        /* Writes to GSP VRAM mirror */
        stunrun_mem_write16_be(s_cpu->ports, offset, (uint16_t)value);
        break;
    case REGION_IRQACK:
        musashi_cpu_set_irq(s_cpu, 0);
        break;
    case REGION_ADSP_PROG:
        /* The main loop's handshake: it counts itself up in this RAM
         * (0x1000-0x10a6 reads $801e and the $8036/$8046/$8056 flags) and then
         * calls the GSP message writer at 0x238e.  Until this window was real
         * RAM it aliased the 1 MiB ROM and the poll could never advance. */
        s_cpu->adsp_pgm_ram[offset >> 1] = (uint16_t)value;
        break;
    case REGION_ADC_CTRL:
        /* hd68k_adc_control_w(): a 16-bit write is a full m_adc_control
         * update (COMBINE_DATA with no mem_mask). */
        adc_control_write(s_cpu, (uint16_t)value);
        break;
    case REGION_ADC8:
        /* hd68k_wr2_write(): offset 0 latches the steering wheel, everything
         * else only logs. */
        if ((offset & 0xFFFFu) == 0x0000u)
            s_cpu->wheel = (uint16_t)(value & 0xFF00u);
        break;
    case REGION_SOUND:
        /* harddriv.cpp:4806 main_command_w (8-bit device split). */
        (void)stunrun_jsa_transport_write(s_cpu->jsa_latches, offset, 2u,
                                          (uint16_t)value);
        break;
    case REGION_ADSP_DATA: case REGION_ADSP_BUF:
    case REGION_ADSP_CTRL:
    case REGION_INPUT: case REGION_GSP_PAL:
    case REGION_ADSP_IRQ:
        break;
    }
}

void m68k_write_memory_32(unsigned int address, unsigned int value)
{
    if (!s_cpu) return;

    unsigned offset;
    region_t region = route_address(address, &offset);

    if (region == REGION_UNMAPPED || region == REGION_ROM) return;
    if (!s_cpu->ports) return;

    switch (region) {
    case REGION_RAM:
        stunrun_mem_write32_be(s_cpu->ports, 0xFF8000u | offset, value);
        break;
    case REGION_GSP:
        /* Only 16-bit registers exist; a long write becomes two of them in
         * big-endian order. */
        gsp_host_write_word(s_cpu, offset - 0x00C00000u,
                            (uint16_t)(value >> 16));
        gsp_emit_host_write_event(s_cpu, address, (uint16_t)(value >> 16),
                                  0xFFFFu, 16u);
        if (address + 2u <= 0x00C03FFFu) {
            gsp_host_write_word(s_cpu, offset - 0x00C00000u + 2u,
                                (uint16_t)(value & 0xFFFFu));
            gsp_emit_host_write_event(s_cpu, address + 2u,
                                      (uint16_t)(value & 0xFFFFu),
                                      0xFFFFu, 16u);
        }
        break;
    case REGION_SOUNDRST:
        gsp_emit_reset_control_event(s_cpu, address,
                                     (uint16_t)(value >> 16), 0xFFFFu, 16u);
        gsp_emit_reset_control_event(s_cpu, address + 2,
                                     (uint16_t)value, 0xFFFFu, 16u);
        break;
    case REGION_GSP_VRAM_MIRROR:
        stunrun_mem_write32_be(s_cpu->ports, offset, value);
        break;
    case REGION_IRQACK:
        musashi_cpu_set_irq(s_cpu, 0);
        break;
    case REGION_SOUND:
        /* harddriv.cpp:4806 main_command_w (8-bit device split). */
        (void)stunrun_jsa_transport_write(s_cpu->jsa_latches, offset, 4u, value);
        break;
    default:
        break;
    }
}

void m68k_write_memory_32_pd(unsigned int address, unsigned int value)
{
    /* Pre-decrement move.l: write high word at [addr+2], low word at [addr] */
    m68k_write_memory_16(address + 2, (uint16_t)(value >> 16));
    m68k_write_memory_16(address,     (uint16_t)(value & 0xFFFF));
}

/* ── Instrumentation ── */

size_t musashi_cpu_gsp_write_count(const musashi_cpu_t *cpu)
{
    return cpu ? cpu->gsp_wlog_len : 0;
}

int musashi_cpu_gsp_write_at(const musashi_cpu_t *cpu, size_t index,
                             musashi_gsp_write_t *out)
{
    if (!cpu || !out || index >= cpu->gsp_wlog_len) return -1;
    *out = cpu->gsp_wlog[index];
    return 0;
}

void musashi_cpu_gsp_host_addr(const musashi_cpu_t *cpu,
                               uint32_t *hstadrh, uint32_t *hstadrl)
{
    if (!cpu) return;
    if (hstadrh) *hstadrh = cpu->hstadrh;
    if (hstadrl) *hstadrl = cpu->hstadrl;
}

void musashi_cpu_gsp_region_counts(const musashi_cpu_t *cpu,
                                   musashi_gsp_region_counts_t *out)
{
    if (!cpu || !out) return;
    *out = cpu->gsp_counts;
}

int musashi_cpu_gsp_memory(const musashi_cpu_t *cpu,
                           musashi_gsp_memory_t *out)
{
    if (!cpu || !out) return -1;
    if (!cpu->gsp_vram || !cpu->gsp_pal_lo || !cpu->gsp_pal_hi) return -1;
    /* The share is written as bytes by the host port but read as 16-bit pixels
     * (harddriv_v.cpp scanline_multisync) and 16-bit palette entries
     * (gsp_palette_change), so hand callers the word view.  calloc guarantees
     * the alignment; on a big-endian host the byte-swapped word would not be
     * what the board holds, but every target here is little-endian. */
    out->vram   = (const uint16_t *)cpu->gsp_vram;
    out->pal_lo = (const uint16_t *)cpu->gsp_pal_lo;
    out->pal_hi = (const uint16_t *)cpu->gsp_pal_hi;
    return 0;
}

uint32_t musashi_cpu_gsp_cycles_until_irq(const musashi_cpu_t *cpu,
                                          uint32_t period)
{
    if (!cpu || period == 0u) return period;
    uint32_t done = cpu->total_cycles % period;
    return done ? (period - done) : period;
}

/* ── Boot trace ──
 *
 * Disassembles the next `count` instructions from wherever the PC currently
 * sits.  Used to answer "where does boot go wrong" without a debugger: run
 * the shell, dump the PC, then ask for the disassembly around it.
 *
 * Declared here rather than in the header because it is a debugging aid, not
 * part of the machine model.
 */
/* Musashi's disassembler reads instruction words through these two hooks.
 * m68kcpu.c only supplies them for the MAME build, so the shell build has to
 * provide them.  Routing them through the project's own read paths is what
 * makes the trace show real ROM words rather than zeroes. */
unsigned int m68k_read_disassembler_16(unsigned int address)
{
    return m68k_read_memory_16(address);
}

unsigned int m68k_read_disassembler_32(unsigned int address)
{
    return m68k_read_memory_32(address);
}

/* Disassembles [start, end) without executing anything, writing one
 * "addr: bytes mnemonic operands" line per instruction.  This is how a ROM
 * routine with no symbols gets turned into something a human can read and a
 * subagent can transcribe.  Address increments come from the instruction
 * length the disassembler reports, so multi-word 68010 encodings line up
 * correctly. */
size_t musashi_cpu_disasm_range(const musashi_cpu_t *cpu, uint32_t start,
                                uint32_t end, FILE *out)
{
    (void)cpu;
    size_t count = 0;
    for (uint32_t pc = start; pc < end && pc <= 0x00FFFFFFu; ) {
        char line[160];
        unsigned len = (unsigned)m68k_disassemble(line, pc,
                                                  M68K_CPU_TYPE_68010);
        if (len < 2u) len = 2u;            /* undecodable: step one word */

        char bytes[40];
        size_t blen = 0;
        static const char hx[] = "0123456789abcdef";
        for (unsigned i = 0; i < len && blen + 5u < sizeof(bytes); i++) {
            uint8_t b = (uint8_t)m68k_read_memory_8(pc + i);
            bytes[blen++] = hx[b >> 4];
            bytes[blen++] = hx[b & 0xf];
            bytes[blen++] = (i + 1u < len) ? ' ' : '\0';
        }
        fprintf(out, "%06x: %-*s %s\n", pc, (int)(len * 3u), bytes, line);
        pc += len;
        count++;
    }
    return count;
}

/* Prints the full register file.  Called at the top of a trace and whenever
 * the tracer stops, so a trace can be read against machine state. */
static void musashi_cpu_dump_regs(const musashi_cpu_t *cpu)
{
    static const char *names[8] = { "d0", "d1", "d2", "d3",
                                    "d4", "d5", "d6", "d7" };
    for (unsigned i = 0; i < 8u; i++)
        printf("  %s=%08x", names[i], musashi_cpu_get_reg(cpu, names[i]));
    printf("\n  a0=%08x a1=%08x a2=%08x a3=%08x\n",
           musashi_cpu_get_reg(cpu, "a0"), musashi_cpu_get_reg(cpu, "a1"),
           musashi_cpu_get_reg(cpu, "a2"), musashi_cpu_get_reg(cpu, "a3"));
    printf("  a4=%08x a5=%08x a6=%08x a7=%08x  pc=%08x sr=%04x\n",
           musashi_cpu_get_reg(cpu, "a4"), musashi_cpu_get_reg(cpu, "a5"),
           musashi_cpu_get_reg(cpu, "a6"), musashi_cpu_get_reg(cpu, "a7"),
           musashi_cpu_get_pc(cpu), musashi_cpu_get_reg(cpu, "sr"));
}

/* ── Control-flow / entry-point logging ──
 *
 * Boot hangs in wait loops, and the fastest way to map a ROM you have no
 * symbols for is to watch where it *goes*: every JSR / JMP / BSR / trap is an
 * edge, and the set of distinct targets is a function inventory.
 *
 * The recorder runs from the instruction hook (same callback cpu_override
 * uses), so it sees every executed PC with no extra cost per frame.  Edges
 * are deduplicated in a fixed-size open-addressing table: the same call site
 * is hit thousands of times a second and we only want each edge once.
 */

#define EDGE_TABLE_SIZE 8192u   /* power of two */
#define EDGE_MAX_KINDS  8

typedef enum {
    JUMP_KIND_CALL,      /* jsr / bsr — target is definitely a function */
    JUMP_KIND_BRANCH,    /* jmp / bcc taken — target may be a function or a
                            * jump-table entry */
    JUMP_KIND_RETURN,    /* rts / rtr / rte — popped address is a return site */
    JUMP_KIND_OTHER,     /* any non-sequential transfer, opcode-independent */
    JUMP_KIND_TRAP,      /* trap / trapv / illegal — a vector, not code */
} jump_kind_t;

typedef struct {
    uint32_t  from;              /* PC of the instruction that branched */
    uint32_t  to;                /* branch target, or popped return address */
    uint32_t  count;             /* times this exact edge was taken */
    uint8_t   kind;
    uint8_t   in_use;
} edge_t;

static edge_t   g_edges[EDGE_TABLE_SIZE];
static uint32_t g_edge_total;        /* distinct edges recorded */
static uint32_t g_branch_total;      /* branches seen, including repeats */
static uint32_t g_return_total;

static inline uint32_t edge_hash(uint32_t from, uint32_t to)
{
    /* from is already word-aligned; fold both halves so that a table walk
     * over neighbouring call sites does not cluster. */
    uint32_t h = from * 0x9e3779b1u ^ (to * 0x85ebca6bu);
    return (h >> 13) & (EDGE_TABLE_SIZE - 1u);
}

static void flow_edge_add(uint32_t from, uint32_t to, int kind)
{
    g_branch_total++;
    if (kind == JUMP_KIND_RETURN) g_return_total++;

    /* Rts with no frame yet returns garbage; a target outside the ROM/RAM
     * window is never a function and would just be noise. */
    if (to > 0x00FFFFFFu) return;

    uint32_t slot = edge_hash(from, to);
    for (uint32_t probe = 0; probe < 64u; probe++) {
        edge_t *e = &g_edges[(slot + probe) & (EDGE_TABLE_SIZE - 1u)];
        if (!e->in_use) {
            e->in_use = 1;
            e->from = from;
            e->to = to;
            e->kind = (uint8_t)kind;
            e->count = 1;
            g_edge_total++;
            return;
        }
        if (e->from == from && e->to == to) {
            /* The catch-all in flow_log_instruction() sees a big forward step
             * before the opcode decoder does, so nearly every call edge lands
             * here already labelled JUMP_KIND_OTHER.  Let a real decode
             * upgrade it, or the log reports "no calls" while the CPU is
             * clearly making them. */
            if (e->kind == JUMP_KIND_OTHER && kind != JUMP_KIND_OTHER)
                e->kind = (uint8_t)kind;
            e->count++;
            return;
        }
    }
    /* Table saturated in this neighbourhood: drop the edge rather than
     * scan the whole table.  The inventory is still useful. */
}

/* Extracts the branch target of the instruction at `pc`, or returns 0 when
 * the instruction does not branch.  Handles the encodings the 68010 actually
 * uses in this ROM: jsr/bsr/jmp (absolute and pc-relative), rts/rtr/rte, and
 * the 68010's 32-bit branch forms (bsr.l, jmp.l via extension words).
 *
 * `ret_popped` is set when the target came off the stack rather than from an
 * operand, because those are the return addresses that tell us which call
 * sites exist.
 */
#define FLOW_MAX_STEP 16
static uint32_t g_flow_prev_pc;
static int g_flow_prev_pc_valid;

static uint32_t flow_branch_target(unsigned pc, uint16_t op, int *kind,
                                   int *ret_popped)
{
    uint16_t mode = (uint16_t)(op & 0x01f8);
    uint16_t reg = (uint16_t)(op & 0x0007);

    *ret_popped = 0;

    /* jsr/jmp (0100 111 0 xx mmmrrr).  Bits 15..9 select the group and bit 8
     * distinguishes jsr (0) from jmp (1); the mode is op[8:3].  So the group
     * masks are 0x4e80 (jsr) and 0x4ec0 (jmp) -- note they are NOT one
     * contiguous range, because op[8:3] overlaps the mask.  abs.l, the
     * commonest form in this ROM and the one used for every call into the
     * GSP boot routines, is mode 0x00b8.  Matching mode 0x0038 (as an earlier
     * version did) silently discarded every jsr $nnn.l in the ROM, which is
     * why the flow log showed 27 edges and no returns at all. */
    if ((op & 0xffc0) == 0x4e80 || (op & 0xffc0) == 0x4ec0) {
        int is_call = (op & 0x0040) == 0x0000;
        if (mode == 0x00b8) {                     /* jsr/jmp xxx.l */
            uint32_t hi = m68k_read_memory_16(pc + 2);
            uint32_t lo = m68k_read_memory_16(pc + 4);
            *kind = is_call ? JUMP_KIND_CALL : JUMP_KIND_BRANCH;
            return (hi << 16) | lo;
        }
        *kind = is_call ? JUMP_KIND_CALL : JUMP_KIND_BRANCH;
        return 0;                                 /* target unknown cheaply */
    }
    /* jsr (an) / jmp (an): indirect, target is the register */
    if ((op & 0xffc7) == 0x4e50 || (op & 0xffc7) == 0x4e58) {
        *kind = (op & 0xffc7) == 0x4e50 ? JUMP_KIND_CALL : JUMP_KIND_BRANCH;
        return m68k_get_reg(NULL, M68K_REG_A0 + (int)reg);
    }
    /* jsr (d16,pc) / jmp (d16,pc) */
    if ((op & 0xffc7) == 0x4eb8 || (op & 0xffc7) == 0x4ef8) {
        int32_t d16 = (int16_t)m68k_read_memory_16(pc + 2);
        *kind = (op & 0xffc7) == 0x4eb8 ? JUMP_KIND_CALL : JUMP_KIND_BRANCH;
        return (uint32_t)((int32_t)pc + 2 + d16);
    }
    /* bsr (16) — opcode 0x6100 */
    if ((op & 0xffc0) == 0x6100) {
        int32_t d16 = (int16_t)m68k_read_memory_16(pc + 2);
        *kind = JUMP_KIND_CALL;
        return (uint32_t)((int32_t)pc + 2 + d16);
    }
    /* dbra (0101 0001 11 001 rrr = 0x51C8|rn) and the plain dbcc short form
     * (0101 0001 0011 0000 = 0x51B0|cc).  Every fill loop in the boot code
     * closes with a dbra, so without it the back-edge of a repeating routine
     * never appears in the log. */
    if ((op & 0xfff8) == 0x51c8) {
        int32_t d16 = (int16_t)m68k_read_memory_16(pc + 2);
        *kind = JUMP_KIND_BRANCH;
        return (uint32_t)((int32_t)pc + 2 + d16);
    }
    /* bra.w (0110 0000 dddd) and the conditional forms bra.s/bra.l, plus the
     * per-condition 16-bit branches beq..bls (0110 cccc 1000 dddd).  The
     * loop back-edge lives here, so without it the flow log cannot show what
     * a repeating routine loops back to. */
    if ((op & 0xff00) == 0x6000) {
        uint32_t disp;
        if (op & 0x0040u) disp = m68k_read_memory_16(pc + 2);          /* .w */
        else              disp = (uint32_t)(int32_t)(int8_t)m68k_read_memory_8(pc + 2);
        uint32_t base = (op & 0x0040u) ? (uint32_t)(pc + 2) : (uint32_t)(pc + 2);
        *kind = JUMP_KIND_BRANCH;
        return (uint32_t)((int32_t)base + (int32_t)disp);
    }
    /* bsr.l (68010) — opcode 0x48f8 */
    if (op == 0x48f8) {
        uint32_t disp = m68k_read_memory_32(pc + 2);
        *kind = JUMP_KIND_CALL;
        return (uint32_t)((int32_t)pc + 6 + (int32_t)disp);
    }
    /* rts / rtr / rte — target comes off the stack */
    if (op == 0x4e75 || op == 0x4e77 || op == 0x4e73) {
        uint32_t sp = m68k_get_reg(NULL, M68K_REG_A7);
        *kind = JUMP_KIND_RETURN;
        *ret_popped = 1;
        return m68k_read_memory_16(sp);
    }
    return 0;
}

/* Called from the instruction hook for every executed instruction. */
static void flow_log_instruction(unsigned pc)
{
    /* Catch-all: any transfer that is not a short forward step is recorded as
     * kind 3 even when the opcode decoder does not recognise it.  Without
     * this, an `rts` or `jmp (an)` that the decoder mis-classifies leaves no
     * trace at all -- which is exactly how the boot loop hid for hours: the
     * decoded call edges showed the 0x3f8 init entered 15 times and not a
     * single return, and no other instrument said why. */
    if (g_flow_prev_pc_valid &&
        (int32_t)(pc - g_flow_prev_pc) > FLOW_MAX_STEP)
        flow_edge_add(g_flow_prev_pc, pc, JUMP_KIND_OTHER);
    g_flow_prev_pc = pc;
    g_flow_prev_pc_valid = 1;

    uint16_t op = (uint16_t)m68k_read_memory_16(pc);
    int kind = 0, popped = 0;
    uint32_t target = flow_branch_target(pc, op, &kind, &popped);
    if (target == 0) return;
    flow_edge_add(pc, target, kind);
}

void musashi_cpu_flow_log_enable(int enabled)
{
    g_flow_log_enabled = enabled;
}

size_t musashi_cpu_flow_log_write(const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f) return 0;
    fprintf(f, "# stunrunner flow log\n");
    fprintf(f, "# distinct_edges %u branches_seen %u returns_seen %u\n",
            g_edge_total, g_branch_total, g_return_total);
    fprintf(f, "# columns: from to kind count\n");
    fprintf(f, "# kind: 0=call(jsr/bsr) 1=branch(jmp/bra/dbra) 2=return(rts/rte)"
               " 3=other non-sequential transfer\n");

    /* Emit sorted by target then source, so the inventory reads like a
     * listing and two runs of the same ROM can be diffed.  Collect the live
     * entries first, then insertion-sort: n is a few thousand at most, and
     * this runs once at shutdown. */
    static const uint32_t sort_chunk = 4096;
    edge_t *live = (edge_t *)malloc(sort_chunk * sizeof(edge_t));
    if (live) {
        size_t n = 0;
        for (uint32_t i = 0; i < EDGE_TABLE_SIZE && n < sort_chunk; i++)
            if (g_edges[i].in_use && g_edges[i].to <= 0x00FFFFFFu)
                live[n++] = g_edges[i];
        for (size_t i = 1; i < n; i++) {          /* insertion sort */
            edge_t key = live[i];
            size_t j = i;
            while (j > 0 &&
                   (live[j - 1].to > key.to ||
                    (live[j - 1].to == key.to && live[j - 1].from > key.from))) {
                live[j] = live[j - 1];
                j--;
            }
            live[j] = key;
        }
        for (size_t i = 0; i < n; i++)
            fprintf(f, "%06x %06x %u %u\n",
                    live[i].from, live[i].to, live[i].kind, live[i].count);
        if (n < sort_chunk)
            fprintf(f, "# entries %zu (table capacity %u)\n",
                    n, EDGE_TABLE_SIZE);
        else
            fprintf(f, "# entries >%u, truncated by sort buffer\n",
                    sort_chunk);
        free(live);
    }
    fclose(f);
    return g_edge_total;
}

void musashi_cpu_trace(musashi_cpu_t *cpu, unsigned count)
{
    if (!cpu) return;
    musashi_cpu_dump_regs(cpu);
    printf("--- trace ---\n");
    for (unsigned i = 0; i < count; i++) {
        uint32_t pc = musashi_cpu_get_pc(cpu);
        /* The GSP's own PC lives in a different address space; only trace
         * while we are in the 68010's ROM/RAM space. */
        if (pc > 0x00FFFFFFu) {
            printf("trace: PC left 68010 space at 0x%08x\n", pc);
            return;
        }
        char line[128];
        if (m68k_disassemble(line, pc, M68K_CPU_TYPE_68010) == 0) {
            printf("%06X: (bad opcode)\n", pc);
            break;
        }
        unsigned before = (unsigned)cpu->total_cycles;
        musashi_cpu_run(cpu, 1);
        unsigned spent = (unsigned)(cpu->total_cycles - before);
        printf("%06X: %-44s [%u cyc]\n", pc, line, spent);
    }
}

/* Steps from an explicitly chosen PC rather than the live one.  Boot lives in
 * a handful of routines, so the useful question is usually "what does this
 * address do" rather than "where did execution stop". */
void musashi_cpu_trace_window(musashi_cpu_t *cpu, unsigned count,
                              uint32_t lo, uint32_t hi)
{
    /* Executes instructions normally but prints only those landing inside
     * [lo, hi), stopping after `count` printed lines.  Needed because the
     * boot code is dominated by 230k-iteration fill loops: an unconditional
     * trace either prints nothing useful or has to be enormous.  This is the
     * instrument for "what does the CPU do just after the fill returns". */
    if (!cpu) return;
    unsigned printed = 0;
    unsigned long long executed = 0;
    printf("--- trace window %06X..%06X (max %u lines) ---\n",
           lo, hi, count);
    while (printed < count) {
        uint32_t pc = musashi_cpu_get_pc(cpu);
        if (pc > 0x00FFFFFFu) {
            printf("trace: PC left 68010 space at 0x%08x\n", pc);
            return;
        }
        char line[128];
        if (m68k_disassemble(line, pc, M68K_CPU_TYPE_68010) == 0) {
            printf("%06X: (bad opcode)\n", pc);
            return;
        }
        unsigned before = (unsigned)cpu->total_cycles;
        musashi_cpu_run(cpu, 1);
        unsigned spent = (unsigned)(cpu->total_cycles - before);
        if (pc >= lo && pc < hi) {
            /* The window exists to answer "what address was the CPU actually
             * touching", which for the RAM march tests at 0x59e6 is A0/A1/A2
             * -- the test walks between A1 and A2 and bails to $5e82 on the
             * first mismatch, so without the pointers the failure says
             * nothing. */
            printf("%06X: %-40s [%u cyc] A0=%08lX A1=%08lX A2=%08lX"
                   " D0=%04lX D4=%04lX\n",
                   pc, line, spent,
                   m68k_get_reg(NULL, M68K_REG_A0),
                   m68k_get_reg(NULL, M68K_REG_A1),
                   m68k_get_reg(NULL, M68K_REG_A2),
                   m68k_get_reg(NULL, M68K_REG_D0) & 0xFFFFu,
                   m68k_get_reg(NULL, M68K_REG_D4) & 0xFFFFu);
            printed++;
        }
        executed++;
        if (executed > 40000000ull) {
            printf("trace window: gave up after %llu instructions\n", executed);
            return;
        }
    }
    printf("--- trace window done (%llu instructions executed) ---\n", executed);
}
void musashi_cpu_trace_from(musashi_cpu_t *cpu, uint32_t addr, unsigned count)
{
    if (!cpu) return;
    m68k_set_reg(9, addr);
    musashi_cpu_trace(cpu, count);
}

/* GDB-style remote stub, enabled with STUNRUN_GDB_PORT.
 *
 * The 68010 is booting from a real ROM with no source, so the trace above only
 * shows the next few instructions.  A real debugger is what makes the rest of
 * boot tractable: break at addresses, single-step, read memory, and — once a
 * breakpoint is known-good — let the emulation run and only stop at the
 * interesting places.
 *
 * Wire protocol is RSP over TCP: '$' + hex payload + '#' + checksum, with
 * '+' ACKs and '-' NAKs.  We answer the subset that GDB's "remote target"
 * needs for a bare-metal debug session:
 *
 *   ?            target description (stopped, T05)
 *   g/G          read / write all registers
 *   m/M          read / write memory
 *   c/s          continue / single-step
 *   Z/z          set / clear breakpoint
 *   qSupported   feature negotiation
 *   k            detach
 *
 * Every wait-loop exit is a candidate for a breakpoint, so this is the tool
 * that turns "the CPU is stuck in a DBRA" into "the CPU reached routine X".
 */
#define SR_MAX_BREAKPOINTS 64

typedef struct sr_gdb_state {
    int          listen_fd;
    int          client_fd;
    uint32_t     watch_addr;     /* read-watch address, 0 = disabled */
    uint32_t     watch_mask;
    uint32_t     watch_last;
    int          watch_hits;
    struct {
        uint32_t addr;
        uint32_t value;
        int      enabled;
    }           bp[SR_MAX_BREAKPOINTS];
    int          next_bp;
    int          attached;
} sr_gdb_state_t;

static sr_gdb_state_t g_gdb;

static int sr_hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void sr_put_byte(char *dst, size_t *len, uint8_t b)
{
    static const char hex[] = "0123456789abcdef";
    if (*len + 2 >= 64) return;
    dst[(*len)++] = hex[b >> 4];
    dst[(*len)++] = hex[b & 0xf];
}

static void sr_put_u32(char *dst, size_t *len, uint32_t v)
{
    sr_put_byte(dst, len, (uint8_t)(v >> 24));
    sr_put_byte(dst, len, (uint8_t)(v >> 16));
    sr_put_byte(dst, len, (uint8_t)(v >> 8));
    sr_put_byte(dst, len, (uint8_t)v);
}

/* Sends one RSP packet and waits for the '+' ack.  NAKs are retried, which
 * matters because a large register or memory read can arrive garbled. */
static void sr_gdb_send(const char *payload)
{
    if (g_gdb.client_fd < 0) return;
    unsigned sum = 0;
    for (const char *p = payload; *p; p++)
        sum += (unsigned char)*p;
    char frame[80];
    size_t n = 0;
    frame[n++] = '$';
    for (const char *p = payload; *p && n < sizeof(frame) - 5; p++)
        frame[n++] = *p;
    frame[n++] = '#';
    char csum[3] = { "0123456789abcdef"[sum >> 4],
                     "0123456789abcdef"[sum & 0xf], 0 };
    for (int i = 0; i < 2; i++) frame[n++] = csum[i];

    for (int attempt = 0; attempt < 3; attempt++) {
        if (write(g_gdb.client_fd, frame, n) != (ssize_t)n) return;
        char ack = 0;
        ssize_t got = read(g_gdb.client_fd, &ack, 1);
        if (got == 1 && ack == '+') return;
        if (got == 1 && ack == '-') continue;
        if (got <= 0) return;
    }
}

/* Decodes a hex byte string into a caller-supplied buffer.
 * Returns the number of bytes decoded, or -1 on a malformed payload. */
static int sr_unhex(const char *hex, uint8_t *out, size_t max_out)
{
    size_t n = 0;
    while (hex[0] && hex[1] && n < max_out) {
        int hi = sr_hexval((unsigned char)hex[0]);
        int lo = sr_hexval((unsigned char)hex[1]);
        if (hi < 0 || lo < 0) return -1;
        out[n++] = (uint8_t)((hi << 4) | lo);
        hex += 2;
    }
    return (int)n;
}

static void sr_gdb_stop_reply(char *dst, size_t *len, const musashi_cpu_t *cpu)
{
    /* T05 = SIGTRAP, with pc as the first register so GDB syncs its view. */
    static const char *names[8] = { "d0", "d1", "d2", "d3",
                                    "d4", "d5", "d6", "d7" };
    *len = 0;
    dst[(*len)++] = 'T';
    dst[(*len)++] = '0';
    dst[(*len)++] = '5';
    for (unsigned i = 0; i < 8u; i++) {
        dst[(*len)++] = names[i][0];
        dst[(*len)++] = ':';
        sr_put_u32(dst, len, musashi_cpu_get_reg(cpu, names[i]));
    }
    static const char *anames[8] = { "a0", "a1", "a2", "a3",
                                     "a4", "a5", "a6", "a7" };
    for (unsigned i = 0; i < 8u; i++) {
        dst[(*len)++] = anames[i][0];
        dst[(*len)++] = ':';
        sr_put_u32(dst, len, musashi_cpu_get_reg(cpu, anames[i]));
    }
    dst[(*len)++] = 'p';
    dst[(*len)++] = ':';
    sr_put_u32(dst, len, musashi_cpu_get_pc(cpu));
    dst[(*len)++] = 's';
    dst[(*len)++] = ':';
    sr_put_u32(dst, len, musashi_cpu_get_reg(cpu, "sr"));
    dst[(*len)++] = '0';
    dst[(*len)++] = '0';
}

static int sr_gdb_hit_breakpoint(uint32_t pc)
{
    for (int i = 0; i < g_gdb.next_bp; i++)
        if (g_gdb.bp[i].enabled && g_gdb.bp[i].addr == pc)
            return 1;
    return 0;
}

/* Handles one client command.  Returns 1 to continue emulation, 0 when the
 * debugger has detached (k, or a dropped connection). */
static int sr_gdb_command(musashi_cpu_t *cpu, const char *payload,
                          char *dst, size_t *len)
{
    *len = 0;
    if (payload[0] == '\0') return 1;

    if (strcmp(payload, "?") == 0) {
        sr_gdb_stop_reply(dst, len, cpu);
        return 1;
    }
    if (payload[0] == 'g') {              /* read all registers */
        static const char *names[16] = {
            "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7",
            "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7"
        };
        for (unsigned i = 0; i < 16u; i++)
            sr_put_u32(dst, len, musashi_cpu_get_reg(cpu, names[i]));
        sr_put_u32(dst, len, musashi_cpu_get_pc(cpu));
        sr_put_u32(dst, len, musashi_cpu_get_reg(cpu, "sr"));
        return 1;
    }
    if (payload[0] == 'G') {              /* write all registers */
        uint32_t regs[18];
        int n = sr_unhex(payload + 1, (uint8_t *)regs, sizeof(regs));
        if (n != 18) { dst[(*len)++] = 'E'; dst[(*len)++] = '0'; return 1; }
        for (unsigned i = 0; i < 8u; i++) {
            static const char *dn[8] = { "d0", "d1", "d2", "d3",
                                         "d4", "d5", "d6", "d7" };
            musashi_cpu_set_reg(cpu, dn[i], regs[i]);
        }
        for (unsigned i = 0; i < 8u; i++) {
            static const char *an[8] = { "a0", "a1", "a2", "a3",
                                         "a4", "a5", "a6", "a7" };
            musashi_cpu_set_reg(cpu, an[i], regs[8 + i]);
        }
        m68k_set_reg(9, regs[16]);
        musashi_cpu_set_reg(cpu, "sr", regs[17]);
        dst[(*len)++] = 'O'; dst[(*len)++] = 'K';
        return 1;
    }
    if (payload[0] == 'm') {              /* read memory */
        const char *a = strchr(payload + 1, ',');
        if (!a) { dst[(*len)++] = 'E'; dst[(*len)++] = '0'; return 1; }
        uint32_t addr = (uint32_t)strtoul(payload + 1, NULL, 16);
        uint32_t count = (uint32_t)strtoul(a + 1, NULL, 16);
        if (count > 512u) count = 512u;
        for (uint32_t i = 0; i < count; i++)
            sr_put_byte(dst, len, (uint8_t)m68k_read_memory_8(addr + i));
        return 1;
    }
    if (payload[0] == 'M') {              /* write memory */
        const char *a = strchr(payload + 1, ',');
        if (!a) { dst[(*len)++] = 'E'; dst[(*len)++] = '0'; return 1; }
        const char *colon = strchr(a + 1, ':');
        if (!colon) { dst[(*len)++] = 'E'; dst[(*len)++] = '0'; return 1; }
        uint32_t addr = (uint32_t)strtoul(payload + 1, NULL, 16);
        /* Data goes to memory, not to the reply buffer, so decode into a
         * scratch area and only then build the OK response. */
        uint8_t bytes[512];
        int nb = sr_unhex(colon + 1, bytes, sizeof(bytes));
        if (nb < 0) { dst[(*len)++] = 'E'; dst[(*len)++] = '0'; return 1; }
        for (int i = 0; i < nb; i++)
            m68k_write_memory_8(addr + (unsigned)i, bytes[i]);
        dst[(*len)++] = 'O'; dst[(*len)++] = 'K';
        return 1;
    }
    if (payload[0] == 'Z' || payload[0] == 'z') {
        int add = (payload[0] == 'Z');
        /* Only software breakpoints on instruction addresses: type 4. */
        uint32_t addr = (uint32_t)strtoul(strchr(payload, ',') ?
                                          strchr(payload, ',') + 1 : payload + 1,
                                          NULL, 16);
        if (add && g_gdb.next_bp < SR_MAX_BREAKPOINTS) {
            g_gdb.bp[g_gdb.next_bp].addr = addr;
            g_gdb.bp[g_gdb.next_bp].enabled = 1;
            g_gdb.next_bp++;
        } else if (!add) {
            for (int i = 0; i < g_gdb.next_bp; i++)
                if (g_gdb.bp[i].enabled && g_gdb.bp[i].addr == addr)
                    g_gdb.bp[i].enabled = 0;
        }
        dst[(*len)++] = 'O'; dst[(*len)++] = 'K';
        return 1;
    }
    if (payload[0] == 'c' || payload[0] == 's') {
        dst[(*len)++] = 'S';               /* reply sent after we stop */
        return 1;
    }
    if (payload[0] == 'k') { dst[(*len)++] = 'O'; dst[(*len)++] = 'K'; return 0; }
    if (strncmp(payload, "qSupported", 10) == 0) {
        memcpy(dst, "PacketSize=1000;swbreak+;hwbreak-;", *len = 29);
        return 1;
    }
    if (strcmp(payload, "qC") == 0) {
        dst[(*len)++] = 'Q'; dst[(*len)++] = 'C'; dst[(*len)++] = '1';
        return 1;
    }
    dst[(*len)++] = '\0';                 /* empty reply: unsupported */
    return 1;
}

/* Pumps the debugger socket.  Called every frame from the shell's main loop
 * while a client is attached; cheap when nothing is connected because
 * poll() reports zero readable bytes. */
void musashi_cpu_gdb_poll(musashi_cpu_t *cpu)
{
    if (!cpu) return;
    if (g_gdb.listen_fd < 0) {
        const char *port = getenv("STUNRUN_GDB_PORT");
        if (!port || !port[0]) return;
        g_gdb.listen_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (g_gdb.listen_fd < 0) return;
        int one = 1;
        setsockopt(g_gdb.listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons((uint16_t)atoi(port));
        if (bind(g_gdb.listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
            listen(g_gdb.listen_fd, 1) < 0) {
            close(g_gdb.listen_fd);
            g_gdb.listen_fd = -1;
            return;
        }
        printf("shell: gdb listening on 127.0.0.1:%s\n", port);
        fflush(stdout);
        return;                            /* accept on the next poll */
    }
    struct pollfd pfd[2];
    pfd[0].fd = g_gdb.listen_fd;
    pfd[0].events = (g_gdb.client_fd < 0) ? POLLIN : 0;
    pfd[1].fd = g_gdb.client_fd;
    pfd[1].events = POLLIN;
    int nfds = (g_gdb.client_fd < 0) ? 1 : 2;

    if (poll(pfd, (nfds_t)nfds, 0) <= 0) return;

    if (g_gdb.client_fd < 0 && (pfd[0].revents & POLLIN)) {
        g_gdb.client_fd = accept(g_gdb.listen_fd, NULL, NULL);
        if (g_gdb.client_fd >= 0) {
            g_gdb.attached = 1;
            g_gdb.next_bp = 0;
            printf("shell: gdb attached\n");
            fflush(stdout);
        }
        return;
    }

    if (g_gdb.client_fd < 0 || !(pfd[1].revents & (POLLIN | POLLHUP)))
        return;

    char c = 0;
    ssize_t got = read(g_gdb.client_fd, &c, 1);
    if (got <= 0) {
        close(g_gdb.client_fd);
        g_gdb.client_fd = -1;
        g_gdb.attached = 0;
        return;
    }
    if (c == '+' || c == '-') return;     /* stray ack */

    if (c != '$') return;
    char payload[256];
    size_t n = 0;
    for (;;) {
        ssize_t r = read(g_gdb.client_fd, &c, 1);
        if (r <= 0) return;
        if (c == '#') break;
        if (n < sizeof(payload) - 1) payload[n++] = c;
    }
    payload[n] = '\0';
    if (write(g_gdb.client_fd, "+", 1) != 1) return;

    char reply[600];
    size_t rlen = 0;
    int keep = sr_gdb_command(cpu, payload, reply, &rlen);
    if (rlen > 0) sr_gdb_send(reply);
    if (!keep) {
        close(g_gdb.client_fd);
        g_gdb.client_fd = -1;
        g_gdb.attached = 0;
    }
}

/* True when a connected debugger wants emulation to run. */
int musashi_cpu_gdb_connected(void)
{
    return g_gdb.client_fd >= 0;
}

/* True when emulation should stop this frame because a breakpoint was hit.
 * Checked before each instruction batch so a break is reported at the exact
 * PC that triggered it rather than at the end of the frame. */
int musashi_cpu_gdb_check_breakpoint(const musashi_cpu_t *cpu)
{
    if (!cpu || g_gdb.client_fd < 0) return 0;
    if (!sr_gdb_hit_breakpoint(musashi_cpu_get_pc(cpu))) return 0;
    char reply[600];
    size_t rlen = 0;
    sr_gdb_stop_reply(reply, &rlen, cpu);
    sr_gdb_send(reply);
    return 1;
}

/* Write `len` bytes to `out_dir`/`name`.  Returns 0 on success. */
/* Best-effort: ignore the result so callers can mkdir -p a parent path. */
static void make_dir(const char *path)
{
    (void)mkdir(path, 0777);
}

static int dump_blob(const char *out_dir, const char *name,
                     const void *data, size_t len)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", out_dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t n = len ? fwrite(data, 1, len, f) : 0;
    int rc = (n == len) ? 0 : -1;
    if (fclose(f) != 0) rc = -1;
    return rc;
}

size_t musashi_cpu_gsp_bus_event_count(const musashi_cpu_t *cpu)
{
    return cpu ? cpu->gsp_bus_log_len : 0;
}

size_t musashi_cpu_gsp_bus_event_dropped(const musashi_cpu_t *cpu)
{
    return cpu ? cpu->gsp_bus_log_dropped : 0;
}

int musashi_cpu_dump_state(const musashi_cpu_t *cpu, const char *out_dir)
{
    if (!cpu || !out_dir) return -1;

    /* Create the output directory.  The shell does not pre-create it, and
     * without this every dump silently degrades to "FAILED". */
    make_dir(out_dir);

    /* Main RAM.  This is the first capture of this region anywhere: the MAME
     * oracle script was reading 0x000000-0x00FFFF, which is ROM, so nothing
     * had ever recorded what the 68010 actually kept in scratch RAM. */
    dump_blob(out_dir, "main_ram.bin", stunrun_ports_main_ram(),
              STUNRUN_MAIN_RAM_BYTES);

    /* The display list, verbatim and in order.
     *
     * NOTE what this is NOT: these are the RESOLVED destinations of each
     * HSTDAT write (GSP address, masked to 0xFFFFFFF0), not the host-port
     * stream that produced them.  Replaying this file into a board model
     * cannot reproduce VRAM, because it has already thrown away the
     * HSTADR/HSTCTL writes that set the address and the increment.  The
     * ordered bus stream is gsp_bus_events.bin below; analysis
     * dual-gsp-comparison-plan.md records the same distinction. */
    dump_blob(out_dir, "gsp_host_writes.bin",
              cpu->gsp_wlog,
              cpu->gsp_wlog_len * sizeof(musashi_gsp_write_t));

    /* The real ordered MC68010 -> TMS34010 bus stream: every host-window write
     * with its raw byte address and lane mask, plus every /GSPRES control
     * event, captured through musashi_cpu_set_gsp_bus_observer.  This is the
     * artifact a GSP provider replays; gsp_host_writes.bin is the decoded
     * result, not the input. */
    if (cpu->gsp_bus_log_len > 0) {
        /* A short fwrite must fail the dump, not silently produce a truncated
         * file.  A truncated bus stream replays into a VRAM that merely looks
         * wrong, with nothing pointing at the real cause. */
        if (dump_blob(out_dir, "gsp_bus_events.bin", cpu->gsp_bus_log,
                      cpu->gsp_bus_log_len * sizeof(musashi_gsp_bus_event_t))
            != 0) {
            return -1;
        }
        /* Say so explicitly if the stream was truncated, so a later VRAM
         * mismatch is not misread as a board-model defect. */
        /* gsp_bus_events.truncated is written by the shell, which can report
         * the dropped count on stdout; this function only signals failure. */
    }

    /* Each GSP memory region, so we can see what the latched addresses
     * actually resolved to. */
    dump_blob(out_dir, "gsp_vram.bin", cpu->gsp_vram, MUSASHI_GSP_VRAM_BYTES);
    dump_blob(out_dir, "gsp_vram_2bpp.bin", cpu->gsp_vram_2bpp,
              MUSASHI_GSP_VRAM2BPP_BYTES);
    dump_blob(out_dir, "gsp_pal_lo.bin", cpu->gsp_pal_lo,
              MUSASHI_GSP_PAL_BYTES);
    dump_blob(out_dir, "gsp_pal_hi.bin", cpu->gsp_pal_hi,
              MUSASHI_GSP_PAL_BYTES);
    dump_blob(out_dir, "gsp_ctrl_lo.bin", cpu->gsp_ctrl_lo, 0x100);
    dump_blob(out_dir, "gsp_ctrl_hi.bin", cpu->gsp_ctrl_hi, 0x100);

    /* A human-readable summary: how many words went where, and the first and
     * last few writes so the log format is self-documenting. */
    char path[1024];
    snprintf(path, sizeof(path), "%s/gsp_summary.txt", out_dir);
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "gsp_host_writes %zu\n", cpu->gsp_wlog_len);
    fprintf(f, "hstadrh 0x%04x\nhstadrl 0x%04x\n",
            cpu->hstadrh, cpu->hstadrl);
    fprintf(f, "hstctlh 0x%02x\nhstctll 0x%02x\n",
            cpu->hstctlh, cpu->hstctll);
    fprintf(f, "counts vram %u vram_2bpp %u vram_2bpp_oob %u pal_lo %u "
               "pal_hi %u control %u unmapped %u\n",
            cpu->gsp_counts.vram, cpu->gsp_counts.vram_2bpp,
            cpu->gsp_counts.vram_2bpp_oob, cpu->gsp_counts.pal_lo,
            cpu->gsp_counts.pal_hi, cpu->gsp_counts.control,
            cpu->gsp_counts.unmapped);
    fprintf(f, "pc 0x%08x\n", (unsigned)musashi_cpu_get_pc(cpu));
    fprintf(f, "total_cycles %lu\n", (unsigned long)cpu->total_cycles);
    musashi_cpu_dump_regs(cpu);
    fprintf(f, "--- first 32 host writes (addr data pc) ---\n");
    for (size_t i = 0; i < cpu->gsp_wlog_len && i < 32; i++)
        fprintf(f, "%08x %04x %06x\n", cpu->gsp_wlog[i].addr,
                cpu->gsp_wlog[i].data, cpu->gsp_wlog[i].pc);
    fprintf(f, "--- last 32 host writes (addr data pc) ---\n");
    for (size_t i = (cpu->gsp_wlog_len > 32 ? cpu->gsp_wlog_len - 32 : 0);
         i < cpu->gsp_wlog_len; i++)
        fprintf(f, "%08x %04x %06x\n", cpu->gsp_wlog[i].addr,
                cpu->gsp_wlog[i].data, cpu->gsp_wlog[i].pc);
    if (fclose(f) != 0) return -1;

    return 0;
}
