# MAME STUN Runner / Hard Drivin' Driver Findings

## Overview

This is the historical driver-source extract, **not the current machine contract
or implementation status**. Unique source excerpts are retained below; board-
family details must not be assumed to apply to the target. For current boot,
host bus, display-share and upload evidence, use
[MC68010 boot findings](mc68010-boot-findings.md). For the JSA-II 6502 sound CPU,
the command→OKI6295 mapping and the OKI ADPCM codec, use
[JSA-II sound findings](jsa2-sound-findings.md). For the mined target baseline,
use [driver-mining findings](../stun-runner/analysis/driver-mining/stunrun.md)
and its [machine map](../stun-runner/analysis/driver-mining/stunrun.machine-map.yaml).
Line numbers refer to the original `mame-ref/` snapshot and may drift.

### Corrections and units

| Historical error / overstatement | Current reading |
|---|---|
| 68010 RAM "512KB" | `$FF8000-$FFFFFF` is **32 KiB**; boot findings §9. |
| 1024 palette entries = "8 banks × 256" | **4 banks × 256**. Offset `$04`'s third bank bit is conditional on a larger family palette, not this target. |
| GSP addresses treated as byte spans | TMS34010 addresses are **bit-addressed**. `gsp_vram` is a **4 MiB backing share**; boot findings §19 (supersedes §12.1). |
| Expansion-window size treated as independent RAM | `$02000000-$020FFFFF` is a handler window; do not derive backing bytes from its numeric span. |
| Main CPU directly accesses GSP palette | Main CPU accesses the GSP host window; GSP addresses belong to a different space. |
| SW1 described as "all ON" | Target definitions default to `$00`; do not infer switch labels from that number. |

The native port does not execute GSP instructions. These extracts and the
historical raster comparison do not establish C compression-interpreter parity:
the known word-fidelity / `MOVE.B` issue remains; caller differential tests use
the original ROM compiler.

---

## 1. Palette Format and Banking

### Palette Storage

The palette uses **two separate 16-bit memory shares** that together store **R8G8B8** (8 bits per channel, 24-bit color):

- **`m_gsp_paletteram_lo`** (1024 × 2 bytes, LITTLE_ENDIAN): stores **red (upper 8 bits)** and **green (lower 8 bits)**
- **`m_gsp_paletteram_hi`** (1024 × 2 bytes, LITTLE_ENDIAN): stores **blue (lower 8 bits)**

From `harddriv_v.cpp:326-332`:
```cpp
inline void harddriv_state::gsp_palette_change(int offset)
{
    int red   = (m_gsp_paletteram_lo[offset] >> 8) & 0xff;
    int green =  m_gsp_paletteram_lo[offset]        & 0xff;
    int blue  =  m_gsp_paletteram_hi[offset]        & 0xff;
    m_palette->set_pen_color(offset, rgb_t(red, green, blue));
}
```

### Palette Banking

Palette banks are controlled by the GSP `control_hi` register bits 0–2 (via `hdgsp_control_hi_w` at `harddriv_v.cpp:222-256`):

| Bank Bit | Control Offset | Function |
|----------|----------------|----------|
| bit 0 | offset 0x02 | Palette bank bit 0 (LSB) |
| bit 1 | offset 0x03 | Palette bank bit 1 |
| bit 2 | offset 0x04 | Palette bank bit 2 (MSB), only if `palette.entries() >= 256*8` |

Each palette bank covers 256 entries (0x100). Total target palette =
**1024 entries (4 banks × 256)**. The family handler computes the entry as:

```cpp
// harddriv_v.cpp:339, 349, 367, 377
offset = m_gfx_palettebank * 0x100 + (offset & 0xff);
```

The `m_gfx_palettebank` member is a `uint8_t` (`harddriv.h:456`).

Palette bank updates trigger a partial screen update:
```cpp
// harddriv_v.cpp:163-169
void harddriv_state::update_palette_bank(int newbank)
{
    screen_device &scr = m_gsp->screen();
    scr.update_partial(scr.vpos());
    m_gfx_palettebank = newbank;
}
```

---

## 2. GSP Rendering Pipeline

### VRAM Organization

The GSP has two distinct VRAM regions:

**Driver board (original Hard Drivin'):**
- `0x02000000–0x0207ffff`: 1bpp shift-register VRAM (512KB)
- `0xff800000–0xffffffff`: 16bpp main VRAM (1MB)

**Multisync board (STUN Runner, Steel Talons, Hard Drivin' Compact, Race Drivin' Compact):**
- `0x02000000–0x020fffff`: 2bpp expansion-handler window (bit addresses, not independent 512 KiB RAM)
- `0xff800000–0xffbfffff`: 16bpp VRAM (4 MiB, mirrored via `mirror(0x0400000)`; MAME models this as 512KB with the same mirror, which is too small — mirror-probe §19 found 4 MiB backing)

From `harddriv.cpp:619-629`:
```cpp
void harddriv_state::multisync_gsp_map(address_map &map)
{
    map.unmap_value_high();
    map(0x00000000, 0x0000200f).noprw();                 /* hit during self-test */
    map(0x02000000, 0x020fffff).rw(FUNC(harddriv_state::hdgsp_vram_2bpp_r), FUNC(harddriv_state::hdgsp_vram_2bpp_w));
    map(0xf4000000, 0xf40000ff).rw(...);  // control_lo
    map(0xf4800000, 0xf48000ff).rw(...);  // control_hi
    map(0xf5000000, 0xf5000fff).rw(...);  // paletteram_lo
    map(0xf5800000, 0xf5800fff).rw(...);  // paletteram_hi
    map(0xff800000, 0xffbfffff).mirror(0x0400000).ram().share("gsp_vram");
}
```

### Mask Table (1bpp vs 2bpp expansion)

The `m_mask_table` array (65536 × 4 × uint32) converts 1bpp/2bpp write data into 16bpp pixel mask patterns:

**Multisync (2bpp mode) — `harddriv_v.cpp:42-56`:**
```cpp
if (i & 0x0001) mask |= MASK(0);   // bit 0 → byte lane 0
if (i & 0x0004) mask |= MASK(1);   // bit 2 → byte lane 1
if (i & 0x0010) mask |= MASK(2);   // bit 4 → byte lane 2
if (i & 0x0040) mask |= MASK(3);   // bit 6 → byte lane 3
// (every 4th bit starting from bit 0)
```

**Driver board (1bpp mode) — `harddriv_v.cpp:59-86`:**
```cpp
// Uses all 4 bits of each nibble:
if (i & 0x0001) mask |= MASK(0);  // bit 0 → byte lane 0
if (i & 0x0002) mask |= MASK(1);  // bit 1 → byte lane 1
if (i & 0x0004) mask |= MASK(2);  // bit 2 → byte lane 2
if (i & 0x0008) mask |= MASK(3);  // bit 3 → byte lane 3
// Repeated for each nibble pair
```

### Scanline Rendering

Two scanline callbacks are registered (`harddriv.h:216-217`):
- `scanline_driver` for the original driver board
- `scanline_multisync` for multisync boards

**Driver board scanline** (`harddriv_v.cpp:411-433`):
```cpp
uint16_t const *const vram_base = &m_gsp_vram[(params->rowaddr << 11) & m_vram_mask];
int coladdr = (params->yoffset << 9) + ((params->coladdr & 0xff) << 4) - 15 + (m_gfx_finescroll & 0x0f);
```
Row stride: **2048 uint16_t** (shift by 11 = ×2048)

**Multisync scanline** (`harddriv_v.cpp:436-457`):
```cpp
uint16_t const *const vram_base = &m_gsp_vram[(params->rowaddr << 10) & m_vram_mask];
int coladdr = (params->yoffset << 9) + ((params->coladdr & 0xff) << 3) - 7 + (m_gfx_finescroll & 0x07);
```
Row stride: **1024 uint16_t** (shift by 10 = ×1024)

Both extract color index per pixel:
```cpp
int coloridx = coladdr++ & 0xfff;  // driver: 12-bit = 4096 pixels
// or
int coloridx = coladdr++ & 0x7ff;  // multisync: 11-bit = 2048 pixels

if (coloridx & 1)
    color = vram_base[coloridx >> 1] >> 8;
else
    color = vram_base[coloridx >> 1];
dest[x] = m_gfx_palettebank * 256 + color;
```

### GSP Control Registers

**`control_lo`** (`harddriv_v.cpp:185-195`): Color (8-bit palette index for 1bpp writes)

**`control_hi`** (`harddriv_v.cpp:211-256`):

| Offset | Bits | Function |
|--------|------|----------|
| 0x00 | bit 0 | `m_shiftreg_enable` — enables shift register transfer |
| 0x01 | 0-3 | `m_gfx_finescroll` — fine scroll offset (4 bits driver, 3 bits multisync) |
| 0x02 | bit 0 | Palette bank bit 0 |
| 0x03 | bit 0 | Palette bank bit 1 |
| 0x04 | bit 0 | Palette bank bit 2 (only if ≥2048 palette entries) |
| 0x07 | — | LED (no-op) |

---

## 3. Display Coordinates

### STUN Runner (Multisync board, no MSP)
From `harddriv.cpp:1859-1869`:
```cpp
m_gsp->set_pixel_clock(5000000);
m_gsp->set_pixels_per_clock(2);
m_screen->set_raw(5000000*2, 317*2, 0, 256*2, 262, 0, 228);
```
- **Pixel clock**: 5 MHz
- **Pixels per clock**: 2
- **HTOTAL**: 317 × 2 = 634 pixels
- **Visible width**: 256 × 2 = 512 pixels
- **VTOTAL**: 262 lines
- **Visible height**: 228 lines

### Multisync (generic, harddriv.cpp:1552)
```cpp
m_screen->set_raw(HARDDRIV_GSP_CLOCK/8*2, 323*2, 0, 256*2, 308, 0, 288);
```
- **HTOTAL**: 323 × 2 = 646
- **Visible width**: 512
- **VTOTAL**: 308
- **Visible height**: 288

### Driver board (harddriv.cpp:1511)
```cpp
m_screen->set_raw(HARDDRIV_GSP_CLOCK/12*4, 160*4, 0, 127*4, 417, 0, 384);
```
- **Pixel clock**: 4 MHz
- **HTOTAL**: 160 × 4 = 640
- **Visible width**: 127 × 4 = 508
- **VTOTAL**: 417
- **Visible height**: 384

---

## 4. Memory Map for STUN Runner

STUN Runner uses the **multisync board without MSP** (`multisync_nomsp` + `stunrun_board_device_state`).

### 68k Main CPU Map (multisync_68k_map, harddriv.cpp:599-616)
```
0x000000-0x0fffff  ROM (1MB)
0x604000-0x607fff  R/W  Sound reset / nwr
0x608000-0x60bfff  W    Watchdog reset
0x60c000-0x60ffff  R/W  Port0 read / IRQ ack write
0xa00000-0xa7ffff  W    WR0 (unknown)
0xa80000-0xafffff  R    a80000 read / W    WR1 (unknown)
0xb00001           R    ADC0808 data (8-bit, gas pedal)
0xb00000-0xb7ffff  W    WR2 (unknown)
0xb80000-0xbfffff  R/W  ADC12 read / ADC control write
0xc00000-0xc03fff  R/W  GSP I/O registers
0xc04000-0xc07fff  R/W  MSP I/O registers (not populated on STUN Runner)
0xff0000-0xff001f  R/W  DUART68681
0xff4000-0xff4fff  R/W  ZRAM (4 KiB mapped window; backing/interleave is separate)
0xff8000-0xffffff  RAM (32 KiB)
```

### GSP Map (multisync_gsp_map, harddriv.cpp:619-629)
```
0x00000000-0x0000200f  NOPRW (self-test access; see source excerpt in §2)
0x02000000-0x020fffff  R/W   2bpp VRAM expansion area
0xf4000000-0xf40000ff  R/W   GSP control registers (lo)
0xf4800000-0xf48000ff  R/W   GSP control registers (hi)
0xf5000000-0xf5000fff  R/W   Palette RAM lo (2048 bytes)
0xf5800000-0xf5800fff  R/W   Palette RAM hi (2048 bytes)
0xff800000-0xffbfffff  RAM   Main VRAM (4 MiB, MAME models as 512KB with same mirror; mirror-probe §19 found 4 MiB)
```

### ADSP Map (adsp, harddriv.cpp:1594-1601)
```
Program: 0x0000-0x1fff  RAM (8KB)
         0x2000-0x3fff  NOPR (ROM?)
Data:    0x0000-0x1fff  RAM (8KB)
         0x2000-0x2fff  R/W  Special I/O
```

### Sound (JSA II, harddriv.cpp:1874-1877 + atarijsa.cpp:116-130)
```
0x0000-0x1fff    RAM (8KB)
0x2000-0x2001    YM2151 (music)
0x2800          OKI6295 read
0x2802          Sound command read (from main 68010)
0x2804          I/O read
0x2806          IRQ ack
0x2a00          OKI6295 write
0x2a02          Response write (to main 68010)
0x2a04          Bank select / WRIO
0x3000-0x3fff   Banked ROM (4 pages × 4KB)
0x4000-0xffff   Static ROM (48KB)
```

---

## 5. Sound / OKI6295 Command Format

STUN Runner uses the **JSA II** sound board (`atarijsa.cpp:17-20`):
- 6502 CPU @ ~1.79 MHz (JSA_MASTER_CLOCK/2 = 3.579545MHz/2)
- YM2151 for music
- **OKI6295 ADPCM** for sample playback

### OKI6295 Interface

From `atarijsa.cpp:116-130` and `atarijsa.cpp:464-473`:
```cpp
void atari_jsa_oki_base_device::oki_w(offs_t offset, uint8_t data)
{
    // offset 0: OKI6295 #1
    // offset 1: OKI6295 #2 (JSA IIIs only)
    if (m_oki2 != nullptr && offset == 1)
        m_oki2->write(data);
    else if (m_oki1 != nullptr)
        m_oki1->write(data);
}
```

The OKI6295 is clocked at `JSA_MASTER_CLOCK/3` (~1.19 MHz) with `PIN7_HIGH` (`atarijsa.cpp:901`).

### Command Protocol (main ↔ sound)

Communication between the main 68010 and the JSA II 6502 uses the `soundcomm` device:
- Main→Sound: `main_command_w(data)` writes a command byte to the 6502
- Sound→Main: `main_response_r()` reads a response byte
- Handshake via `sound_to_main_ready` and `main_to_sound_ready` lines

### WRIO register (atarijsa.cpp:481-518)
```cpp
// wrio_w data bits:
0xc0 = bank address (2 bits → 4 banks of 4KB each at 0x3000-0x3fff)
0x20 = coin counter 2
0x10 = coin counter 1
0x08 = voice frequency (tweaks OKI6295 frequency via PIN7)
0x04 = OKI6295 reset (active low)
0x02 = OKI6295 #1 bank bit 0 (JSA III/IIIs only)
0x01 = YM2151 reset (active low)
```

### MIX register (atarijsa.cpp:526-542)
```cpp
// mix_w data bits:
0xc0 = right OKI bank bits (JSA IIIs only)
0x20 = low-pass filter enable (unemulated)
0x10 = OKI6295 #1 bank bit 1
0x0e = YM2151 volume (0-7)
0x01 = OKI6295 volume (0=0.5, 1=1.0)
```

---

## 6. Hardware Variations Between Board Types

### VRAM Size
| Board | VRAM Size | GSP VRAM Map |
|-------|-----------|--------------|
| Driver (Hard Drivin') | 1MB | `0xff800000-0xffffffff` |
| Multisync (STUN Runner) | 4 MiB | `0xff800000-0xffbfffff` (mirror-probe §19; MAME models as 512KB) |
| Multisync II (Hard Drivin's Airborne) | 1MB | `0xff800000-0xffffffff` |

### GSP Clock / Pixel Clock
| Board Variant | GSP Clock | Pixel Clock | Pixels/Clock |
|---------------|-----------|-------------|--------------|
| Driver board | 48 MHz | 4 MHz | 4 |
| Multisync (generic) | 48 MHz | 6 MHz | 2 |
| STUN Runner | 48 MHz | **5 MHz** | 2 |
| Hard Drivin' Compact | 48 MHz | 5 MHz | 2 |

### MSP Presence
- **Hard Drivin' Cockpit**: Has MSP (TMS34010 @ 50MHz)
- **STUN Runner**: No MSP (multisync_nomsp)
- **Hard Drivin' Compact**: Has MSP (multisync_msp)
- **Race Drivin' Compact**: No MSP (multisync_nomsp)

### ADSP Board
| Game | ADSP Board | CPU | Clock |
|------|-----------|-----|-------|
| Hard Drivin' | ADSP or ADSP II | ADSP-2100 | 8 MHz |
| STUN Runner | **ADSP II** | ADSP-2100 | 8 MHz |
| Steel Talons | DS III | ADSP-2101 | 12 MHz |

---

## 7. Key Register Definitions

### GSP I/O Register Map (tms34010.cpp:33-41, 1234-1244)
```
HESYNC, HEBLNK, HSBLNK, HTOTAL
VESYNC, VEBLNK, VSBLNK, VTOTAL
DPYCTL, DPYSTART, DPYINT, CONTROL
HSTDATA, HSTADRL, HSTADRH, HSTCTLL
HSTCTLH, INTENB, INTPEND, CONVSP
CONVDP, PSIZE, PMASK, RESERVED
RESERVED, RESERVED, RESERVED, DPYTAP
HCOUNT, VCOUNT, DPYADR, REFCNT
```

### Display Parameters (tms34010.cpp:1137-1166)
```cpp
params->enabled  = (DPYCTL & 0x8000) != 0;
params->vcount    = VCOUNT;
params->veblnk   = VEBLNK;
params->vsblnk   = VSBLNK;
params->heblnk   = HEBLNK * pixels_per_clock;
params->hsblnk   = HSBLNK * pixels_per_clock;
params->rowaddr  = DPYADR >> 4;    // VRAM row base (shifted by 11 or 10 in scanline cb)
params->coladdr  = ((DPYADR & 0x007c) << 4) | (DPYTAP & 0x3fff);
params->yoffset  = (DPYSTRT - DPYADR) & 3;  // sub-row pixel offset
```

---

## 8. STUN Runner-Specific Configuration

From `harddriv.cpp:1844-1878`:
```cpp
void stunrun_board_device_state::device_add_mconfig(machine_config &config)
{
    multisync_nomsp(config);

    /* basic machine hardware */
    m_gsp->set_pixel_clock(5000000);        // 5 MHz pixel clock (different from generic multisync)
    adsp(config);                            // ADSP II board
    config.device_remove("slapstic");        // No slapstic on multisync

    /* video hardware */
    m_screen->set_raw(5000000*2, 317*2, 0, 256*2, 262, 0, 228);

    /* sound hardware */
    SPEAKER(config, "mono").front_center();
    ATARI_JSA_II(config, m_jsa);
    m_jsa->main_int_cb().set(FUNC(harddriv_state::sound_int_write_line));
    m_jsa->test_read_cb().set_ioport("IN0").bit(5);
    m_jsa->add_route(ALL_OUTPUTS, "mono", 0.5);
}
```

STUN Runner's screen resolution is **512×228** (visible), running at approximately **60 Hz** refresh.

---

## 9. Input System

STUN Runner input ports (`harddriv.cpp:1084-1170`):
- **IN0** at `0x60c000`: Diagnostic jumper, HBLANK, VBLANK, ADC EOC, coins
- **SW1** at `0x60c002`: Definitions default to `0x00` (different from the `harddriv` input block)
- **a80000**: Buttons (2 action buttons + start)
- **8BADC.0** (8-bit ADC): X-axis analog stick
- **8BADC.2** (8-bit ADC): Y-axis analog stick
- **12BADC.0-3**: Not used (all IP_ACTIVE_LOW)

---

## Summary of Critical Values for STUN Runner

| Parameter | Value | Source |
|-----------|-------|--------|
| Palette format | R8G8B8 (2×16-bit shares) | harddriv_v.cpp:326-332 |
| Palette size | 1024 entries (4 banks × 256) | harddriv.cpp:1508 |
| Palette bank control | control_hi offsets 0x02/0x03; 0x04 conditional on ≥2048 entries | harddriv_v.cpp:222-256 |
| VRAM address | 0xff800000 | harddriv.cpp:628 |
| VRAM size | 4 MiB (MAME models as 512KB with mirror) | harddriv.cpp:628 / mirror-probe §19 |
| 2bpp VRAM area | 0x02000000-0x020fffff | harddriv.cpp:623 |
| Display origin | (0,0) | tms34010.cpp:1154 |
| Row stride (multisync) | 1024 × uint16_t | harddriv_v.cpp:439 |
| Column modulo (multisync) | 2048 pixels (0x7ff) | harddriv_v.cpp:446 |
| Visible display | 512×228 | harddriv.cpp:1869 |
| Pixel clock | 5 MHz | harddriv.cpp:1864 |
| GSP clock | 48 MHz | harddriv.cpp:39 |
| Sound CPU | 6502 @ ~1.79 MHz | atarijsa.cpp:888 |
| Sound chip | YM2151 + OKI6295 | atarijsa.cpp:896-902 |
