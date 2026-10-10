# STUN Runner — Chip Interconnects & Memory Map

## Sources
- `mame-ref/src/mame/atari/harddriv.cpp` (driver + address maps)
- `mame-ref/src/mame/atari/harddriv_m.cpp` (MC68000-side handlers)
- `mame-ref/src/mame/atari/harddriv_v.cpp` (GSP/VRAM handlers)
- `mame-ref/src/mame/atari/atariscom.cpp` (soundcomm device)
- `mame-ref/src/mame/atari/atarijsa.cpp` (JSA sound board)

---

## MC68010 Address Map (`multisync_68k_map`)

| Address Range | Size | Read | Write | Purpose |
|---|---|---|---|---|
| `0x000000–0x0fffff` | 1MB | ROM | — | Program ROM (6 banks) |
| `0x604000–0x607fff` | 16KB | `hd68k_sound_reset_r` | `hd68k_nwr_w` | Sound board control / NWR latch |
| `0x608000–0x60bfff` | 16KB | — | watchdog reset | Watchdog |
| `0x60c000–0x60ffff` | 16KB | `hd68k_port0_r` | `hd68k_irq_ack_w` | Input port 0 + IRQ ack |
| `0xa00000–0xa7ffff` | 512KB | — | `hd68k_wr0_write` | SEL1-4 + coin counters (CC1/CC2) |
| `0xa80000–0xafffff` | 512KB | `hd68k_a80000_r` | `hd68k_wr1_write` | Shifter interface / SEL data latch |
| `0xb00001` (mirrored) | — | ADC8 data | — | 8-bit ADC (ADC0808) |
| `0xb00000–0xb7ffff` | 512KB | — | `hd68k_wr2_write` | Steering wheel latch |
| `0xb80000–0xbfffff` | 512KB | `hd68k_adc12_r` | `hd68k_adc_control_w` | 12-bit ADC (ADC1208) |
| `0xc00000–0xc03fff` | 16KB | `hd68k_gsp_io_r` | `hd68k_gsp_io_w` | **GSP host I/O** (main 68010↔TMS34010 channel) |
| `0xc04000–0xc07fff` | 16KB | `hd68k_msp_io_r` | `hd68k_msp_io_w` | MSP (未确认; maybe audio DSP) |
| `0xff0000–0xff001f` | 32B | DUART (mc68681) | DUART | Serial/parallel I/O |
| `0xff4000–0xff4fff` | 4KB | `hd68k_zram_r` | `hd68k_zram_w` | Z80 RAM (if present) |
| `0xff8000–0xffffff` | 512KB | RAM | RAM | Work RAM |

### Input Port 0 bits (`hd68k_port0_r`)
```
0x0001 = DIAGN (diagnostic mode)
0x0002 = /HSYNCB (HBLANK)
0x0004 = /VSYNCB (VBLANK)
0x0008 = EOC12 (12-bit ADC end-of-conversion)
0x0010 = EOC8  (8-bit ADC end-of-conversion)
0x0020 = SELF-TEST
0x0040 = COIN2
0x0080 = COIN1
0x0100–0x8000 = SW1 bits 8–1 (DIP switches)
```

### Output Latches (`hd68k_wr0_write`)
- `offset & 7 == 1–4`: SEL1–SEL4 select → `m_sel_select`
- `offset & 7 == 6–7`: CC1–CC2 coin counter pulses
- `offset & 7 == 0,5`: CR1/CR2, LC1 (unknown/unused)

### Shift Select Latch (`hd68k_wr1_write`)
Written when `offset == 0`, data byte goes to SEL1–SEL4 selected via `m_sel_select`:
- SEL1 → `m_sel1_data`
- SEL2 → `m_sel2_data`
- SEL3 → `m_sel3_data`
- SEL4 → `m_sel4_data`

### Sound Control (`hd68k_nwr_w`, `hd68k_sound_reset_r`)
- `0x604000–0x607fff` read: pulses JSA sound board reset, returns `~0`
- `0x604000–0x607fff` write: `hd68k_nwr_w` (NWR latch; CR2/CR1/LC1/LC2)

---

## MC68010 ↔ TMS34010 (GSP) — Host I/O

Accessed at `0xc00000–0xc03fff` via `hd68k_gsp_io_r/w`.

**On the MC68010 side** (`harddriv_m.cpp:139-156`):
```cpp
offset = (offset / 2) ^ 1;          // byte-swap offset
m_hd34010_host_access = true;
result = m_gsp->host_r(offset);    // TMS34010 host read
m_hd34010_host_access = false;
```
The offset is mangled: `(offset/2)^1`. The GSP `host_r/host_w` are the TMS34010-side handlers.

**GSP address map** (`multisync_gsp_map`):
| Address | Size | Access | Purpose |
|---|---|---|---|
| `0x00000000–0x0000200f` | 2KB | nop | Self-test area |
| `0x02000000–0x020fffff` | 1MB | VRAM | Frame buffer (2bpp packed) |
| `0xf4000000–0xf40000ff` | 256B | GSP control lo | Shift enable + 2 color bits |
| `0xf4800000–0xf48000ff` | 256B | GSP control hi | Fine scroll, palette bank select, LED |
| `0xf5000000–0xf5000fff` | 4KB | Palette RAM lo | Lower 8 bits of 16-bit palette |
| `0xf5800000–0xf5800fff` | 4KB | Palette RAM hi | Upper 8 bits |
| `0xff800000–0xffbfffff` | 4MB | VRAM mirror | Contiguous VRAM mirror |

**GSP control lo** (`m_gsp_control_lo[]`):
- `offset 0`: 8-bit color value used by VRAM expander

**GSP control hi** (`m_gsp_control_hi[]`):
- `offset & 7 == 0x00`: shift register enable (`m_shiftreg_enable`)
- `offset & 7 == 0x01`: fine scroll (`m_gfx_finescroll`, triggers `update_partial`)
- `offset & 7 == 0x02`: palette bank bit 0
- `offset & 7 == 0x03`: palette bank bit 1
- `offset & 7 == 0x04`: palette bank bit 2
- `offset & 7 == 0x07`: LED

**VRAM layout** (`hdgsp_vram_2bpp_w`):
- offset × 4 → uint32_t writes (4 pixels per write, 2bpp = 4 pixels × 2 bits each = 8 bits = 1 uint32_t)
- Color from `m_gsp_control_lo[0]`, masked via `m_mask_table[data * 2]`

**Speedup handlers** (installed in `init_stunrun`):
```cpp
m_gsp->space(AS_PROGRAM).install_write_handler(0xfff9fc00, 0xfff9fc0f, ...);  // speedup 1
m_gsp->space(AS_PROGRAM).install_write_handler(0xfffcfc00, 0xfffcfc0f, ...);  // speedup 2
m_gsp_speedup_pc = 0xfff41070;  // GSP PC that spins on these addresses
```

---

## MC68010 ↔ ADSP-2100

**Program ROM** (ADSP code image, loaded by MC68010 into ADSP RAM at boot):
- Region `"mainpcb:user1"` = `ROM_REGION16_BE(0x60000, "mainpcb:user1")`
- 3 banks × 128KB = 384KB total
- MAME loads via `ROM_LOAD16_BYTE` (even/odd byte interleaving)

**MC68010 address space** (from `init_adsp()`):
| Address Range | Size | Purpose | Handler |
|---|---|---|---|
| `0x800000–0x807fff` | 32KB | ADSP program RAM (accessible from MC68010) | `hd68k_adsp_program_r/w` → `m_adsp_pgm_memory[]` |
| `0x808000–0x80bfff` | 16KB | ADSP data RAM | `hd68k_adsp_data_r/w` → `m_adsp_data_memory[]` |
| `0x810000–0x813fff` | 16KB | ADSP serial/buffer RAM (SOM) | `hd68k_adsp_buffer_r/w` → `m_som_memory[bank*0x2000+offset]` |
| `0x818000–0x81801f` | 32B | ADSP control registers | `hd68k_adsp_control_w` |
| `0x818060–0x81807f` | 32B | ADSP IRQ clear | `hd68k_adsp_irq_clear_w` |
| `0x838000–0x83ffff` | 512KB | ADSP IRQ state | `hd68k_adsp_irq_state_r` (read) |

**Key ADSP sync mechanism** (`harddriv_m.cpp:596-601`):
```cpp
// Any write to offset 0x1fff in data RAM triggers ADSP sync
if (offset == 0x1fff) {
    machine().scheduler().synchronize();
    m_adsp->signal_interrupt_trigger();
}
```
This is the primary synchronization mechanism between MC68010 and ADSP-2100.

**ADSP control register write** (`hd68k_adsp_control_w`):
- Writes to `m_m68k_adsp_buffer_bank` (controls which 8KB SOM bank is visible at `0x810000`)

**ADSP program RAM format**: 16-bit words stored as 32-bit values in `m_adsp_pgm_memory[]`:
```cpp
// Even address = upper 16 bits, odd address = lower 16 bits
word = m_adsp_pgm_memory[offset/2];
return (!offset & 1) ? (word >> 16) : (word & 0xffff);
```

**ADSP speedup handler**:
```cpp
m_adsp->space(AS_DATA).install_read_handler(0x1fff, 0x1fff, hdadsp_speedup_r);
```

---

## MC68010 ↔ 6502 (JSA II Sound Board)

**Communication** at `0x600000–0x603fff` (8-bit, `0xff00` mask):
- MC68010 **write**: `main_command_w(data)` → goes to 6502 as NMI
- MC68010 **read**: `main_response_r()` → reads `m_sound_to_main_data`, clears IRQ to MC68010

**Soundcomm device** (`atariscom.cpp`):
```cpp
// MC68010 → 6502: main_command_w writes data, triggers NMI to sound CPU
m_sound_write_timer->adjust(attotime::zero, data);

// 6502 → MC68010: sound_response_w stores data, sets main_to_sound IRQ
m_sound_to_main_data = data;
m_main_int_cb(ASSERT_LINE);

// MC68010 reads: clears ready flag, returns data
return m_sound_to_main_data;
```

**Sound CPU type**: MC6805 (6502-class) at 1.7895MHz — this is the JSA II board.
- ROM region `"mainpcb:jsa:cpu"`: 64KB (`136070-2123.10c`)
- ADPCM samples: 4 × 64KB via OKI MSM5205 at `"mainpcb:jsa:oki1"`

**IRQ to MC68010**: The JSA asserts IRQ4 when sound has data ready for main CPU.

---

## Summary of All Communication Channels

```
MC68010 (main CPU)
├── 0x600000-0x603fff ──→ [soundcomm] ──→ 6502 (JSA II sound)
│         (8-bit command/NMI)              ├── MSM5205 ADPCM × 4
│                                           └── MC6805 program ROM
│
├── 0x800000-0x807fff ←─→ ADSP-2100 (geometry/road processor)
│         (16-bit program RAM)              └── ADSP-2100 object ROM (384KB)
│         0x808000-0x80bfff ←─→ data RAM
│         0x810000-0x813fff ←─→ serial buffer (SOM, banked 8KB×4)
│         0x818000-0x81801f →  control regs
│         0x818060-0x81807f →  IRQ clear
│         0x838000-0x83ffff ←  IRQ state
│         ⚡ sync trigger: write 0x1fff in data RAM
│
├── 0xc00000-0xc03fff ←─→ TMS34010 GSP (video processor)
│         (16-bit host I/O, offset mangled)  ├── VRAM @ 0x02000000 (1MB)
│                                           ├── Control regs @ 0xf4000000, 0xf4800000
│                                           ├── Palette @ 0xf5000000, 0xf5800000
│                                           └── VRAM mirror @ 0xff800000
│
├── 0xa00000-0xa7ffff → SEL1-4 latches (shift selection)
├── 0xa80000-0xafffff ←→ Shift register data
├── 0xb00000-0xb7ffff → Steering wheel latch
├── 0xb80000-0xbfffff ←→ 12-bit ADC (steering position)
├── 0x60c000-0x60ffff ←  port0 (inputs, DIP switches, coins, Vsync)
├── 0x604000-0x607fff →  sound reset
├── 0xff0000-0xff001f ←→  DUART mc68681
└── 0xff8000-0xffffff →  work RAM
```

---

## DSP Firmware — NOT Reverse-Engineered

Key architectural decision (confirmed against MAME source):
- ADSP-2100 and TMS34010 are **replaced** in our native implementation
- We implement **equivalent behavior** from traces, not decompiled firmware
- Only the MC68010 program ROM (384KB) needs decompilation
- ADSP-2100 firmware is an I/O peripheral, not game logic
- TMS34010 GSP firmware is a video renderer, not game logic

## Priority for Native Implementation

1. **MC68010** (384KB ROM) → decompile with Ghidra; game logic lives here
2. **ADSP-2100** → replace with native C (geometry processing, track generation)
3. **TMS34010** → replace with OpenGL renderer (VRAM is frame buffer)
4. **6502/JSA** → replace with SDL_mixer (ADPCM samples + music)
