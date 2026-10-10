# Chip API Requirements — STUN Runner Modernization

## Status

**Analysis draft.** Derived from MAME 0.289 driver source (`src/mame/atari/harddriv.cpp`), runtime traces in `reference/experiments/stunrun/`, and project analysis documents. ROM-revision-independent where possible.

---

## Overview

STUN Runner has four processors:

| Processor | Role | Needs RE for modernization? |
|-----------|------|----------------------------|
| **MC68010** (8 MHz) | All game logic, state machine, physics, AI, course data | ✅ YES — the game lives here |
| **ADSP-2100** (8 MHz) | Geometry/trajectory math assist | ❌ NO — replace with C++ |
| **TMS34010 GSP** (48 MHz) | Graphics rendering pipeline | ❌ NO — replace with OpenGL/Vulkan |
| **6502 JSA II** (1.7895 MHz) | Sound effects + speech | ❌ NO — replace with SDL_mixer |

The three "helper" processors are specialized execution engines. The ADSP does math. The GSP draws to its own VRAM. The 6502 plays sounds. None of them contain game content — they are peripherals with firmware.

**Modernization strategy**: treat the ADSP, GSP, and 6502 as hardware peripherals with documented I/O interfaces. Replace each with a modern software equivalent. Only the MC68010 ROM needs actual reverse engineering.

---

## Chip 1 — Motorola 68010 (Main CPU)

This is the only chip that contains actual game content.

### Memory Map (68010-visible address space)

From MAME driver `harddriv.cpp` and `machine map`:

```
0x000000 – 0x7FFFFF   ROM (program ROM, 8 × 64KB banks switched by bank register)
0xFF8000 – 0xFFFFFF   RAM (working memory)
0x800000 – 0x807FFF   ADSP program space (68010 writes program words here)
0x808000 – 0x80BFFF   ADSP data space (68010 ↔ ADSP shared data)
0x810000 – 0x813FFF   ADSP serial/output buffer
0x818000 – 0x81801F   ADSP control registers
0x818060 – 0x81807F   ADSP clear/state registers
0x838000 – 0x83FFFF   ADSP control mirror
0xA80000 – 0xA8001F   Input port (player controls)
0xA80020 – 0xA8003F   Input port mirror
0x600000 – 0x603FFF   JSA II sound board interface
0xC00000 – 0xC0001F   GSP VRAM aperture (low)
0xC00020 – 0xC0003F   GSP control registers
0xC00040 – 0xC0005F   GSP FIFO data
0xC00060 – 0xC0007F   GSP FIFO status/control
0xF5000000 – 0xF5000FFF  GSP palette RAM low plane
0xF5800000 – 0xF5800FFF  GSP palette RAM high plane
0xFF800000 – 0xFFFFFFFF  GSP VRAM mirror
```

### Key 68010 Entry Points (from traces)

| PC | Function |
|----|----------|
| `0x0222DE` | Global interrupt tick — writes `addq.l #1,$FF8014` |
| `0x02452C` | State dispatch — dispatches through `0xFF9550` |
| `0x020430` | Input sampling — reads `0x60C001` / `0xA80001` |
| `0x02248E` | Road buffer drain — 384-word buffer → 192-word GSP FIFO writes |
| `0x023EF6` | Sound command — writes byte to `0x600000` |
| `0x02F0AE` | ADSP buffer consumer — reads from `0x810000`, copies to GSP FIFO |
| `0x03CF40` | Object record update — 16-byte indexed records at `0xFFDD86` |
| `0x02B7E8` | Geometry/FIFO primitive emitter — calls `0x02F470` |

### What the 68010 needs to do (for modernization)

1. **Game state machine** — title, attract, race, crash, game-over states
2. **Vehicle physics** — speed, trajectory, boost, shield management
3. **Course/track progression** — track position, tunnel detection, ramp triggers
4. **Object management** — enemy vehicles, obstacles, powerups
5. **Collision detection** — wall hits, vehicle collisions, weapon impacts
6. **Scoring** — star collection, speed bonuses, time bonuses
7. **Input processing** — steering, boost, weapons (twin lasers, shockwave bomb)
8. **Road buffer building** — constructing the road geometry for GSP
9. **Command submission** — sending commands to ADSP, GSP, and sound board

### RE Priority for MC68010

**High priority** (needed for working game):
- State machine entry points and transitions
- Vehicle physics constants and update loop
- Road buffer building and course data format
- Collision detection logic
- Input processing

**Medium priority** (nice to have):
- Object AI behavior
- Scoring formula details
- Sound trigger conditions

**Low priority** (cosmetic/audio):
- Exact particle effects
- Speech trigger timing

---

## Chip 2 — ADSP-2100 (Geometry/trajectory assist)

### Role in STUN Runner

The ADSP-2100 is a Harvard-architecture DSP. It receives commands and data from the 68010, performs mathematical computations (trajectory projection, road geometry calculations), and returns results via its serial output buffer.

From traces: the ADSP runs an internal program that reads from DM (data memory) and PM (program memory), and communicates via:
- **GINT** — general interrupt to 68010 (IRQ2)
- **Serial output buffer** — 68010 reads this and forwards to GSP FIFO
- **Special I/O registers** (DM 0x2000–0x2007):
  - `DM($2000)` = SIMBUF (object ROM data buffer)
  - `DM($2001)` = SIMCLK
  - `DM($2002)` = SOMLATCH
  - `DM($2003)` = SOMCLK
  - `DM($2006)` = GINT (trigger)
  - `DM($2007)` = object ROM bank select (MP)

### ADSP Memory Windows (68010-visible)

```
0x800000 – 0x807FFF   ADSP program ROM upload window
0x808000 – 0x80BFFF   ADSP data memory (68010 ↔ ADSP shared)
0x810000 – 0x813FFF   ADSP serial/output buffer
0x818000 – 0x81801F   ADSP control/IRQ registers
0x818060 – 0x81807F   ADSP clear/state
```

### ADSP Program Upload

From traces: 5,456 halfword writes from PC `0x02D35C` over `0x800000–0x807fff`. The 68010 uploads the ADSP program at startup. The project has verified this with the `adsp_init_image.c` replacement image.

### ADSP Serial Buffer Protocol

Observed at PC `0x02F0AE`: the 68010 reads a **length-prefixed block** from the ADSP serial buffer (`0x810000`), then copies it word-by-word to the GSP FIFO (`0xC0000C`), followed by a `-1` terminator.

This is the ADSP's primary output path back to the 68010 → GSP chain.

### ADSP Reset Entry

- PC = `0x0004` (confirmed)
- First non-NOP instruction: `CALL $0780`
- Second instruction: `CALL $0834`

### What we need from ADSP analysis (minimum)

The ADSP-2100 program is 64KB of ROM. We do **not** need to fully reverse-engineer the ADSP firmware. Instead, we need to characterize:

1. **Command format** — what 68010 writes to ADSP DM window triggers what computation
2. **Result format** — what serial data the ADSP produces for the 68010
3. **GINT semantics** — what the 30 observed GINT writes per trace represent (trajectory ready? geometry ready?)

### ADSP Replacement Strategy (for modernization)

Replace the ADSP entirely with a C++ geometry/trajectory module. The replacement must:

```cpp
// Conceptual ADSP replacement API
struct ADSPCommand {
    uint16_t command_type;   // 0 = trajectory, 1 = road_geometry, etc.
    uint16_t input_words[??]; // input parameters from 68010
};

struct ADSPResult {
    uint16_t serial_words[??]; // output to GSP FIFO
    bool gint_asserted;         // whether to trigger 68010 IRQ2
};

// Called from 68010 emulation when 68010 writes to ADSP data window
ADSPResult adsp_process(const ADSPCommand& cmd);

// Called every video frame to check for background ADSP activity
void adsp_tick();
```

**How to derive the actual API**: Run MAME with instrumentation on the ADSP data window (`0x808000–0x80Bfff`) and serial buffer (`0x810000–0x813fff`) and capture the exact read/write sequences during gameplay. This gives us the command/reply format without disassembling the ADSP firmware.

---

## Chip 3 — TMS34010 GSP (Graphics System Processor)

### Role in STUN Runner

The TMS34010 is a 48 MHz graphics processor. It:
1. Receives a **command stream** from the 68010 via a FIFO (`0xC0000C`)
2. Reads from its **VRAM** (512×240 display, 16-bit color)
3. Executes **graphics primitives**: `PIXBLT` (pixel block transfer), `LINE`, `FILL XY`, `FILL L`
4. Writes to **palette RAM** (two 256-color planes) and **VRAM**

The GSP is a **tile-based renderer**. It does not receive "draw a polygon" commands — it receives primitive operations that it executes against its own VRAM.

### GSP Memory Map

```
0x02000000 – 0x0200FFFF   GSP VRAM (low aperture)
0xC00000   – 0xC0001F     GSP control registers
0xC00020   – 0xC0003F     GSP VRAM aperture (high)
0xC00040   – 0xC0005F     GSP FIFO data
0xC00060   – 0xC0007F     GSP FIFO status
0xF5000000 – 0xF5000FFF   Palette RAM low plane (256 × 16-bit)
0xF5800000 – 0xF5800FFF   Palette RAM high plane (256 × 16-bit)
0xFF800000 – 0xFFFFFFFF   GSP VRAM mirror
```

### GSP Display Parameters (from M5 traces)

From `m5-visible-output.md`:

```
DPYCTL  = register 8   — display control
DPYSTART = register 9  — display start address
DPYTAP  = register 27  — display tap
DPYADR  = register 30  — display address
HEBLNK  = register 1   — horizontal blank end
HSBLNK  = register 2   — horizontal sync blank end
fine_scroll = control register 1, masked to 3 bits
```

Display resolution: **512×240** (interlaced, 15kHz horizontal)

VRAM word layout: 4 visible lines per 2048-byte row:
```
row = y / 4
byte_offset = (y % 4) * 512
```

### GSP Command FIFO Format

The 68010 writes 16-bit words to `0xC0000C`. From trace `m5-gsp-instruction-fork-trace.metadata.json`:

**Known GSP primitives** (from decoded GSP instruction traces):

| Primitive | Description | Observed at PC |
|-----------|-------------|---------------|
| `PIXBLT B,XY` | Pixel block transfer from source to VRAM | `0xFFF46590` |
| `LINE 0` | Line draw | `0xFFF45DD0` |
| `FILL XY` | Rectangle fill with computed coordinates | `0xFFF454E0`, `0xFFF45A00`, `0xFFF47AB0` |
| `FILL L` | Line fill | `0xFFF43030` |

**GSP program address space**: word-addressed, with `address << 3` for byte-equivalent addresses. GSP PC observed at `0xFFF59920` (first instruction) and others like `0xFFF45330`, `0xFFF479E0`, `0xFFF454E0`.

### GSP Palette Format

Two 256-entry palette RAM planes at `0xF5000000` and `0xF5800000`. Each entry is 16-bit (5-5-5 BGR or 5-6-5?). MAME's `hdgsp_paletteram_lo/hi_r/w` handlers confirm the split-plane structure.

**Palette lookup during scanout**: MAME's multisync scanline callback reads palette entries indexed by VRAM pixel values to produce the final RGB output.

### GSP Road Rendering (most critical)

From `m5-visible-output.md` and `analysis/gsp-road-fifo-consumer.md`:

The 68010 builds a 384-word road buffer at `0xFF9584–0xFF9882`, drains it as 192 words to the GSP FIFO. The GSP has an identified **FIFO-fed consumer** at PC `0xFFF48C20` which processes the FIFO data and writes to two **work buffer** families:
- Base buffer: `0xFFF6F650` (256 words)
- Twin buffer: `0xFFF70650` (256 words)

These work buffers are then copied to the **display memory** (`0xF5000000` and `0xF5800000` — the palette planes) by a copy path at `0xFFF4AA60`/`0xFFF4AAD0`.

The road data path is:
```
68010 road buffer → GSP FIFO → GSP FIFO consumer → work buffers → palette/VRAM
```

### GSP Text/Glyph Rendering

From `m5-visible-output.md`: the GSP has a text blit path using `PIXBLT B,XY`:
- Source glyph table at `0xFFF5DBC0` (512 words, 8×8 glyphs, LSB-first row order)
- Glyph selection: `(code & 0x7F) * 64` byte offset
- Destination: VRAM space, 8-pixel horizontal cell advance
- Byte-lane interleaving: consecutive descriptor words use alternating low/high byte lanes

Known glyphs verified against oracle: `0` (row masks `1C,36,63,63,63,36,1C,00`), `C`, `r`, `Credits:`, `0:35.0`, `TIME`, `LEVEL`.

### GSP Record Walk

From `analysis/gsp-record-walk.md`: the GSP has an indexed record parser at `0xFFF45000`:
- Base + index × 16 address computation
- Loads records into A4, A8, A6 registers
- Later feeds coordinate values into `FILL XY` loop

### What the GSP needs (for modernization)

The GSP is the most complex peripheral to replace. A modern renderer must:

1. **Parse the command FIFO** — interpret the 16-bit word stream sent by the 68010 at `0xC0000C`
2. **Execute equivalent primitives** — `PIXBLT`, `LINE`, `FILL XY`, `FILL L` in an OpenGL/Vulkan equivalent
3. **Maintain VRAM state** — 512×240 × 16-bit framebuffer with palette lookup
4. **Handle palette RAM** — two planes, 256 entries each, 16-bit entries
5. **Manage double-buffering** — the display is scanned out while rendering continues

### GSP Replacement Strategy

```cpp
// Conceptual GSP replacement API
struct GSPCommand {
    enum Type { PIXBLT, LINE, FILL_XY, FILL_L } type;
    uint16_t params[??];  // type-specific parameters
};

// Called from 68010 emulation when 68010 writes to GSP FIFO
void gsp_submit_command(uint16_t word);

// Called every frame to produce display output
void gsp_render_frame(uint16_t* vram_out, palette_t* palette_out);

// GSP state
struct GSPState {
    uint16_t vram[512 * 240];        // VRAM content
    uint16_t palette[2][256];         // dual palette planes
    uint16_t control_regs[32];        // DPYCTL, DPYSTART, etc.
    uint16_t fifo[256];               // command FIFO buffer
    int fifo_head, fifo_tail;
};
```

**Critical unknown**: the exact 16-bit command word format for each primitive type. This is derivable from MAME's GSP instruction traces — capture the FIFO writes and the GSP PC at each write to correlate word values with primitive types.

---

## Chip 4 — 6502 JSA II (Sound)

### Role in STUN Runner

The JSA II sound board uses a 6502-class CPU (1.7895 MHz) to:
1. Receive **command bytes** from the 68010 (game events)
2. Read **ADPCM samples** from ROM (OKI MSM5205 ADPCM playback)
3. Produce **digital audio output**

### JSA II Interface (from MAME driver and traces)

```
0x600000  main_command_w    — 68010 writes sound command byte → 6502 NMI
0x600000  main_response_r   — 68010 reads sound response byte ← 6502 IRQ4
0x2802    sound_command_r   — 6502 reads command (with mirror mask 0x1F9)
0x280A    sound_response_w  — 6502 writes response (mirror 0x1F9)
0x280C    rdio_r           — read data (ADPCM status)
0x2A02    sound_response_w  — 6502 writes response → 68010 IRQ4
```

**Transport confirmed from traces**:
1. 68010 writes command byte to `0x600000` → triggers 6502 NMI
2. 6502 reads command at `0x280A` via `sound_command_r`
3. 6502 processes and may write response at `0x2A02`
4. 6502 write at `0x2A02` → 68010 IRQ4
5. 68010 reads and clears response at `0x600000`

**Known title-path byte pairing** (from frame 447 trace):
- 68010 writes `0x1E` to `0x600000`
- 6502 reads `0x1E` at PC `0x5839` from `$280A`

### ADPCM Samples

From the 7z archive: four files (`.10c`, `.9h`, `.9k`, `.1fh` etc.) — all 64KB. These are ADPCM sample ROMs. MAME lists `136070-2123.10c` as the sound CPU ROM and `136070-2118.90k` etc. as ADPCM sample ROMs.

The JSA II board uses **OKI MSM5205** ADPCM decoders (confirmed from MAME driver). Each sample ROM feeds one MSM5205.

### What sounds exist

STUN Runner sound categories:
- **Engine noise** — continuous, speed-dependent
- **Twin laser** — weapon fire
- **Shockwave bomb** — explosion
- **Collision/crash** — impact sounds
- **Boost** — acceleration sound
- **Speech** — digitised announcements (STUN Runner, etc.)

### Sound Replacement Strategy

```cpp
// Conceptual sound replacement API
enum SFX {
    SFX_ENGINE,
    SFX_LASER,
    SFX_SHOCKWAVE,
    SFX_CRASH,
    SFX_BOOST,
    SFX_STAR_COLLECT,
    SFX_SPEECH_STUN_RUNNER,   // requires sample identification
};

// Called from 68010 emulation when 68010 writes to sound latch
void sound_submit_command(uint8_t cmd);

// Per-frame audio tick
void sound_tick(float dt, float vehicle_speed);

// Output: stereo float samples
void sound_render(float* left_out, float* right_out, int num_samples);
```

**ADPCM sample identification**: the 4 sample ROMs need to be identified. Once `stunrun.zip` rev 6 is obtained:
1. Load sample ROMs into Audacity or a hex editor
2. Look for ADPCM sync patterns (`0xAA`/`0x55` block markers`)
3. Play them back at the appropriate ADPCM clock rate (MSM5205 typically uses 8kHz or 10kHz sample rate)
4. Match to game events to label them

---

## Summary: What We Need vs. What We Can Skip

```
NEED (actual RE work):
├── MC68010 ROM disassembly (Ghidra, m68k support excellent)
├── 68010 game state machine entry points
├── 68010 vehicle physics/trajectory constants
├── 68010 road buffer format
└── Command FIFO protocol between 68010 and GSP

SKIP (hardware peripheral replacement, no RE needed):
├── ADSP-2100 firmware reverse engineering
├── ADSP-2100 instruction set analysis
├── TMS34010 GSP firmware reverse engineering
├── TMS34010 GSP instruction set analysis
└── 6502 JSA II sound CPU firmware reverse engineering

DERIVE (interface characterization, trace-based):
├── ADSP command/reply format from data window traces
├── GSP command word format from FIFO traces
├── Sound command byte meanings from command/response traces
├── ADPCM sample ROM identification and labeling
└── Palette format from VRAM/palette snapshots
```

---

## Immediate Next Steps

1. **Obtain `stunrun.zip` rev 6 ROMs** — the only prerequisite blocker
2. **Capture ADSP data window traces** — instrument `0x808000–0x80Bfff` reads/writes during gameplay
3. **Capture GSP FIFO traces** — instrument `0xC0000C` writes and GSP PC simultaneously
4. **Identify ADPCM sample ROMs** — play each 64KB ROM at 8kHz ADPCM and listen
5. **Load main ROM into Ghidra** — start function identification at known entry points

---

## References

- MAME driver: `src/mame/atari/harddriv.cpp` (and `harddriv.h`, `harddriv_m.cpp`, `harddriv_v.cpp`)
- MAME ADSP-2100: `src/devices/cpu/adsp2100/adsp2100.cpp`, `2100dasm.cpp`
- MAME TMS34010: `src/devices/cpu/tms34010/34010dsm.cpp`
- Project traces: `reference/experiments/stunrun/adsp-*.metadata.json`, `gsp-*.metadata.json`
- Project analysis: `analysis/interconnect.md`, `analysis/game-loop-model.md`, `analysis/m5-visible-output.md`, `analysis/object-record-renderer-path.md`, `analysis/gsp-road-fifo-consumer.md`
