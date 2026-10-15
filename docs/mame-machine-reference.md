# MAME machine reference — `stunrun` (S.T.U.N. Runner, rev 6)

What the pinned MAME tree at `/mnt/build/mame-ref` **actually models**: which chips
exist, what each one's address space looks like, and how the parts are wired to
each other. This is the `[MAME]` half of the contract; `docs/hardware-contract.md`
is the `[MANUAL]` half, and the two are meant to be read against each other.

**Reading rule.** Every claim below is tagged.

| Tag | Meaning |
|---|---|
| `[MAME]` | Read directly from the pinned source at `/mnt/build/mame-ref`, with file and line. |
| `[MANUAL]` | SP-338 / TM-238 operator manual, source **S6**. |
| `[ROM]` | Measured against the pinned ROM set with a repo tool, with the evidence file named. |
| `[INFER]` | Our reading of the above. Not an observation. |
| `UNKNOWN` | Not settled here. Says so rather than guessing. |

Source numbers are in [`sources.md`](sources.md): **S7** = `src/mame/atari/harddriv.cpp`,
**S8** = `src/devices/cpu/tms34010/`. Manual is **S6**, main ROM is **S1**.

---

## 0. Provenance warning — read this before trusting anything below

`/mnt/build/mame-ref` is **not stock MAME**. It is a git tree on branch
`stunrun-observe` at base commit `e4c1c2b5102c032ce93216bc5ce5bae2b6a1f23e`
(MAME 0.289) carrying local edits `[MAME] analysis/mame-local-patches.json`.

* **Eight observation-only hooks** (`STUNRUN-DEBUG`, `STUNRUN-DUMP`,
  `STUNRUN-DPY`, `STUNRUN-HOSTPROBE`, `STUNRUN-OPLOG`, `STUNRUN-FETCH`,
  `STUNRUN-IOB`, `VERSION`) in `harddriv_v.cpp`, `harddriv_m.cpp`,
  `tms34010.{cpp,h}` and `version.h`. These are inert unless their environment
  variable is set.
* **One behavioural change: `MD-12-IOBOUNDS`.** `m_IOregs` was grown from 64 to
  `0x200` entries and `IOREG()` now masks its offset to `0x1ff`
  (`src/devices/cpu/tms34010/34010ops.h`). `internal_regs_map` exposes
  `0x00`–`0x1ff` `[MAME tms34010.cpp:37]` while `m_IOregs` held 64 entries, so
  the unfixed build ran off the end of the array wherever this game's GSP
  program writes above offset `0x40`. **This changes behaviour exactly where
  S.T.U.N. Runner exercises it.**

Two consequences that matter for anything built on this tree:

1. The frozen oracle binary `/mnt/build/mame-oracle/mame`
   (sha256 `75b09ee4…`) predates MD-12 and is behaviourally stock MAME plus
   observation hooks only. **The oracle and the working tree are not the same
   emulator.** A measurement taken with one must not be compared against a
   capture taken with the other.
2. `tests/test_mame_local_patches.py` diffs the tree against the JSON and fails
   on any unrecorded edit. It passes as of this writing (9 tests, OK), so the
   inventory above is current — but re-run it before trusting a fresh reading.

---

## 1. The machine, and how MAME composes it

`GAME(1989, stunrun, …, stunrun_machine, stunrun, …, "S.T.U.N. Runner (rev 6)")`
`[MAME harddriv.cpp:5375]`. All eleven S.T.U.N. Runner sets (`stunrun`,
`stunrunj`, `stunrun5`, `stunrune`, `stunrun4`, `stunrun3`, `stunrun3e`,
`stunrun2`, `stunrun2e`, `stunrun0`, `stunrunp`) share one machine
configuration and one ROM parent, so **everything in this document applies to
every revision**. Only the ROM images differ.

Composition, from the inside out:

```text
stunrun_machine                          harddriv.cpp:2046
└── STUNRUN_BOARD "mainpcb"             DEFINE_DEVICE_TYPE, harddriv.cpp:1852
    └── stunrun_board_device_state::device_add_mconfig     harddriv.cpp:1859
        ├── multisync_nomsp()            harddriv.cpp:1538   ← the base
        │   └── driver_nomsp()           harddriv.cpp:1460   ← the common driver board
        │       ├── M68010  maincpu      :mainpcb:maincpu
        │       ├── SLAPSTIC 117         ← removed by stunrun (see §1.2)
        │       ├── WATCHDOG_TIMER       :mainpcb:watchdog
        │       ├── ADC0809              :mainpcb:adc8
        │       ├── TMS34010  gsp        :mainpcb:gsp
        │       ├── M48T02 timekeeper    :mainpcb:200e
        │       ├── EEPROM_2816          :mainpcb:210e
        │       ├── MC68681 DUART        :mainpcb:duartn68681
        │       ├── RS232_PORT           :mainpcb:rs232
        │       ├── PALETTE (1024 pens)  :mainpcb:palette
        │       └── SCREEN               :mainpcb:screen
        ├── adsp()                       harddriv.cpp:1595
        │   └── ADSP2100  adsp           :mainpcb:adsp
        ├── screen/pixel-clock overrides harddriv.cpp:1864, 1869
        ├── config.device_remove("slapstic")
        ├── SPEAKER "mono"
        └── ATARI_JSA_II  jsa           :mainpcb:jsa   "atjsa2"
            ├── M6502       jsa:cpu
            ├── ATARI_SOUND_COMM  jsa:soundcomm
            ├── YM2151      jsa:ym2151
            └── OKIM6295    jsa:oki1
```

`device_start()` calls `init_stunrun()` (harddriv.cpp:1848 →
harddriv.cpp:5082), which calls `init_multisync(0)` and `init_adsp()` and then
installs the speed-up handlers of §4.9. Those two `init_*` functions are where
the 68010's view of the DSP and the sound board is bolted on — they are not in
the address map function, which is easy to miss.

### 1.1 Runtime confirmation

`[ROM] reference/inventory/stunrun.json` (schema `stunrun-runtime-inventory/v1`,
MAME `0.289 (mame0289-dirty)`) lists **22 started devices** with **no
unconfigured device present**:

```text
:                              stunrun            :mainpcb:adsp        adsp2100
:mainpcb                       stunrun_board      :mainpcb:gsp         tms34010
:mainpcb:200e                  m48t02             :mainpcb:jsa         atjsa2
:mainpcb:210e                  2816               :mainpcb:jsa:cpu     m6502
:mainpcb:adc8                  adc0809            :mainpcb:jsa:oki1    okim6295
:mainpcb:duartn68681           mc68681            :mainpcb:jsa:soundcomm atariscom
:mainpcb:duartn68681:cha/chb   duart_channel      :mainpcb:jsa:ym2151  ym2151
:mainpcb:maincpu               m68010             :mainpcb:mono        speaker
:mainpcb:palette               palette            :mainpcb:rs232       rs232
:mainpcb:screen                screen             :mainpcb:watchdog    watchdog
```

### 1.2 Devices the board has but this driver does **not** instantiate

| Absent | Where the "full" version would come from | Note |
|---|---|---|
| **TMS34010 MSP** | `multisync_msp()` harddriv.cpp:1557 | `multisync_nomsp()` is the deliberate no-MSP variant `[MAME harddriv.cpp:1537]`. Confirmed absent at runtime above. |
| **SLAPSTIC** | `driver_nomsp()` harddriv.cpp:1467 | `config.device_remove("slapstic")` harddriv.cpp:1866. The MSB/LSB bank-swapping chip is removed because this board does not use it. |
| **TMS34012 pixel processor** | — | **[MANUAL]** SP-338 Page 10 draws `34010-50 GSP --> 34012-50 PSP --> VDD-VD15`. **MAME has no 34012 device at all.** Scanout is done by the 34010's own `tms340x0_ind16` screen update (§4.5). `BUGS.md` MD-04/MD-05 and `docs/hardware-contract.md` §7 record the consequence. |
| **DS III/IV board (ADSP-2101 + two ADSP-2105)** | `ds3()` harddriv.cpp:1605 | That is Steel Talons' / Street Drivin''s sound DSP board, not ours. |
| **DSK board (DSP32C + 2× MK48Z02 + ASIC65)** | `dsk()` harddriv.cpp:1645 | Hard/Race Drivin' only. |
| **12-bit A/D converter as a device** | — | `[MAME]` models `12BADC.0..3` as four `required_ioport_array` entries read synchronously in `hd68k_adc_control_w`, not as a converter device. |

`config.device_remove("slapstic")` is cosmetic here: `multisync_68k_map` never
mapped `$0E0000` anyway, and the ROM region is a flat 1 MiB, so `$0E0000`
reads as ordinary ROM.

---

## 2. Clocks and timing

| Clock | Value | Source |
|---|---|---|
| `HARDDRIV_MASTER_CLOCK` | `XTAL(32'000'000)` | harddriv.h:38 |
| **68010** | `HARDDRIV_MASTER_CLOCK/4` = **8 MHz** | harddriv.cpp:1463 |
| **68010 timer IRQ** | `MASTER/16/16/16/16/2` = **244.140625 Hz**, IPL 5 | harddriv.cpp:1465 |
| `HARDDRIV_GSP_CLOCK` | `XTAL(48'000'000)` | harddriv.h:39 |
| **GSP core** | 48 MHz | harddriv.cpp:1483 |
| **GSP pixel clock** | 5 MHz (**stunrun override**; `multisync_nomsp` sets 6 MHz) | harddriv.cpp:1864, 1546 |
| **GSP pixels per clock** | 2 | harddriv.cpp:1547 |
| **ADSP-2100** | `XTAL(32'000'000)/4` = **8 MHz** | harddriv.cpp:1598 |
| **Screen raw** | `5000000*2` = 10 MHz; htotal 634, hbegin 0, hend 512, vtotal 262, vbegin 0, vend 228 | harddriv.cpp:1869 |
| **JSA 6502** | `JSA_MASTER_CLOCK/2` = 1 789 772.5 Hz | atarijsa.cpp:757, 80 |
| **JSA timer IRQ** | `MASTER/4/16/16/14` = ≈249.69 Hz | atarijsa.cpp:759 |
| **YM2151** | `JSA_MASTER_CLOCK` = 3 579 545 Hz | atarijsa.cpp:762 |
| **OKI M6295** | `JSA_MASTER_CLOCK/3` = 1 193 181.67 Hz, pin7 high | atarijsa.cpp:766 |
| Max scheduler quantum | 30 000 Hz | harddriv.cpp:1494 |

> 🧠 **From Hindsight memory (Conventions and patterns, Key decisions and rationale)** — the project's rule of precedence is: for what the board *is*, the manual wins; for what the emulator *does*, the pinned MAME source wins; when the two disagree the driver has a bug. That is why this document is split from `docs/hardware-contract.md` rather than merged into it, and why every table below is tagged.

**Cross-check worth keeping:** the 68010's 8 MHz comes straight out of
`HARDDRIV_MASTER_CLOCK/4`, and independently `[MANUAL]` SP-338 Page 3 (PDF 60)
draws `8MHZ` to `U68010 CLK` pin 15 (`docs/hardware-contract.md` §8.1). Two
independent sources agree. **Do not** derive any audio-rate clock by assuming
16 MHz for the CPU — that 16 MHz label belongs to a different domain.

**Screen refresh** from the raw parameters: `10 000 000 / 634 / 262` ≈
**60.2 Hz**. The GSP's own `HEBLNK`/`HSBLNK`/`VEBLNK`/`VSBLNK` registers then
narrow that fixed area further at run time (§4.5), so the *effective* visible
rectangle is data-driven, not the `256*2 × 228` constant.

---

## 3. Address maps

### 3.1 68010 program space — `multisync_68k_map` (harddriv.cpp:599)

`map.unmap_value_high()` at the top of every map in this driver: nothing is
mirrored above the stated top. 16-bit, big-endian, 24-bit address bus
(`[ROM] reference/inventory/stunrun.json`).

| Range | Size | Read | Write | `[MANUAL]` S6 name |
|---|---|---|---|---|
| `$000000–$0FFFFF` | 1 MiB | `rom` | — | `ROM0..ROM7`, 128 KiB each ×8 |
| `$604000–$607FFF` | 16 KiB | `hd68k_sound_reset_r` | `hd68k_nwr_w` | `SCRES` + `LSBUS` latch bank |
| `$608000–$60BFFF` | 16 KiB | — | `watchdog` `reset16_w` | `WOCLR` |
| `$60C000–$60FFFF` | 16 KiB | `hd68k_port0_r` | `hd68k_irq_ack_w` | `SW1` (read) / clear timer IRQ (write) |
| `$A00000–$A7FFFF` | 512 KiB | — | `hd68k_wr0_write` | `WRO` shifter + `CC1`/`CC2` |
| `$A80000–$AFFFFF` | 512 KiB | `hd68k_a80000_r` | `hd68k_wr1_write` | `/SW2` (read) / `/WRI` shifter latch |
| `$B00000–$B7FFFF` | 512 KiB | — | `hd68k_wr2_write` | `/WR2` steering-wheel latch |
| `$B00001` (mirror `0x7FFFE`) | — | `adc8.data_r` | — | `/RD2` 8-bit A/D |
| `$B80000–$BFFFFF` | 512 KiB | `hd68k_adc12_r` | `hd68k_adc_control_w` | `/RD3` + `/WR3` 12-bit A/D |
| `$C00000–$C03FFF` | 16 KiB | `hd68k_gsp_io_r` | `hd68k_gsp_io_w` | **not in the legible manual map** |
| `$C04000–$C07FFF` | 16 KiB | `hd68k_msp_io_r` | `hd68k_msp_io_w` | same; MSP absent, reads `$FFFF` |
| `$FF0000–$FF001F` | 32 B | `mc68681.read` | `mc68681.write`, `umask16(0xFF00)` | `DUART` |
| `$FF4000–$FF4FFF` | 4 KiB | `hd68k_zram_r` | `hd68k_zram_w` | `ZRAM` |
| `$FF8000–$FFFFFF` | 512 KiB | `ram` | `ram` | `RAM 0` 16 KiB + `RAM 1` 16 KiB |

**Two overlays are installed at reset**, neither of which appears in the map
function:

* `init_adsp()` (harddriv.cpp:5070) adds five windows — see §5.2.
* `init_multisync()` (harddriv.cpp:5040) replaces the top of `$600000` with
  the JSA sound port: `install_readwrite_handler(0x600000, 0x603fff,
  main_response_r, main_command_w, 0xff00)`.

Unmapped everything else reads as zero and writes are dropped.

**Note on `map(0xff8000, 0xffffff).ram()`:** 512 KiB in one allocation. The
manual says 32 KiB in two 16 KiB banks (`docs/hardware-contract.md` §1) and
`analysis/ram-map.md` carries the annotation set. MAME does not model a bank
split here — one flat array.

### 3.2 GSP program space — `multisync_gsp_map` (harddriv.cpp:619)

**The single most important line in this document for a port author:**

```cpp
m_program_config("program", ENDIANNESS_LITTLE, is_34020 ? 32 : 16, 32, 3, …)
                                                    data_width=16  addrshift=3
```
`[MAME tms34010.cpp:67]`; confirmed at runtime as
`program:mask=0xffffffff,data=16,little,shift=3`
`[ROM] reference/inventory/stunrun.json`.

**`addrshift = 3` means every TMS34010 address is a BIT address.** One address
unit = one bit; one 16-bit word = `0x10` address units. The canonical
conversion used throughout this project:

```c
byte_offset = ((addr - 0xFF800000) >> 3) & 0x7FFFF;
```

This is the trap the project already paid for once: the map below *looks* like
4 MiB of RAM, and a naive reading makes every GSP-side address off by 8×.

| Range | Access | Share |
|---|---|---|
| `$00000000–$0000200F` | `noprw()` — "hit during self-test" | — |
| `$02000000–$020FFFFF` | `hdgsp_vram_2bpp_r` (returns 0) / `hdgsp_vram_2bpp_w` | writes into `gsp_vram` |
| `$F4000000–$F40000FF` | `hdgsp_control_lo_r/w` | **`gsp_control_lo`** |
| `$F4800000–$F48000FF` | `hdgsp_control_hi_r/w` | **`gsp_control_hi`** |
| `$F5000000–$F5000FFF` | `hdgsp_paletteram_lo_r/w` | `gsp_paletteram_lo` |
| `$F5800000–$F5800FFF` | `hdgsp_paletteram_hi_r/w` | `gsp_paletteram_hi` |
| `$FF800000–$FFBFFFFF` `.mirror(0x0400000)` | `ram()` | **`gsp_vram`** |

**Sizing the share.** The entry is 4 MiB of *address space*; with `addrshift=3`
the backing array is `4 MiB / 8` = **524 288 bytes = 262 144 words**.
`init_video()` computes `m_vram_mask = m_gsp_vram.bytes()/2 - 1` = `0x3FFFF`
(harddriv_v.cpp:94). `[ROM] reference/inventory/…` and the frame-600 direct
share capture both report exactly 524 288 bytes. `docs/hardware-contract.md` §7
records the retraction of the earlier "4 MiB of VRAM" reading.

The `.mirror(0x0400000)` makes `$FFC00000–$FFFFFFFF` an alias of
`$FF800000–$FFBFFFFF`. **[INFER]** The mirror exists because the hardware
answers the full 32-bit space; it is not 1 MiB of extra storage.

GSP **internal register file** lives in the same space, mapped by the device
itself at `$C0000000–$C00001FF` to `io_register_r/w`
`[MAME tms34010.cpp:37]`. Register index `n` is at `$C0000000 + 2n`. That
register file is **not reachable from the 68010** — it is reached through the
host port of §5.1. `BUGS.md` MD-04: Lua cannot read it either, which is why
`STUNRUN-DEBUG` exists.

| idx | name | idx | name | idx | name | idx | name |
|---|---|---|---|---|---|---|---|
| 0 | `HESYNC` | 8 | `DPYCTL` | 16 | `HSTCTLH` | 24 | `DPYTAP` |
| 1 | `HEBLNK` | 9 | `DPYSTRT` | 17 | `INTENB` | 25 | `HCOUNT` |
| 2 | `HSBLNK` | 10 | `DPYINT` | 18 | `INTPEND` | 26 | `VCOUNT` |
| 3 | `HTOTAL` | 11 | `CONTROL` | 19 | `CONVSP` | 27 | **`DPYADR`** |
| 4 | `VESYNC` | — | — | 20 | `CONVDP` | 28 | `REFCNT` |
| 5 | `VEBLNK` | — | — | 21 | `PSIZE` | | |
| 6 | `VSBLNK` | — | — | 22 | `PMASK` | | |
| 7 | `VTOTAL` | — | — | 23 | reserved | | |

Enumerated from `[MAME tms34010.h:61–96]`. 34010-internal interrupts:
`INT1 0x0002`, `INT2 0x0004`, `HOST 0x0200`, `DISPLAY 0x0400`, `WV 0x0800`
(tms34010.h:99–103).

### 3.3 ADSP-2100 address spaces — `adsp_program_map` / `adsp_data_map`

`[MAME harddriv.cpp:680, 688]`. Device config:
program = 32-bit data, 14 address bits, shift −2; data = 16-bit data, 14 address
bits, shift −1 `[MAME adsp2100.cpp:134–135]`.

**Program space**

| Range | Access | Share |
|---|---|---|
| `$0000–$1FFF` | `ram` | `adsp_pgm_memory` (32-bit cells) |
| `$2000–$3FFF` | `nopr()` — `// ROM?` | — |

**Data space**

| Range | Access | Share |
|---|---|---|
| `$0000–$1FFF` | `ram` | `adsp_data` (16-bit) |
| `$2000–$2FFF` | `hdadsp_special_r/w` — **the whole host interface** | — |

The special window is decoded on `offset & 7`, giving `$2000–$2007` — see §5.3.

`[INFER]` `m_adsp_pgm_memory_word` is computed in `device_start()`
(harddriv_m.cpp:42) as `(uint16_t *)(base_bytes + 1)` and then **never read
anywhere in the driver**. It is dead code; do not treat it as the packing rule.
The live packing is `hd68k_adsp_program_r/w` (§4.2), where the even 68010 word
is the **high** half of the 32-bit cell and the odd word the low half.
`UNKNOWN`: which half the ADSP core itself fetches from is a property of
`src/devices/cpu/adsp2100/`, not of this driver, and is not established here.

### 3.4 JSA II (6502 sound) space — `atarijsa2_map`

`[MAME atarijsa.cpp:116]`.

| Range | Access |
|---|---|
| `$0000–$1FFF` | 8 KiB RAM |
| `$2000–$2001` | `ym2151` read/write |
| `$2800` (mirror `$01F9`) | `oki_r` — `/RDV` |
| `$2802` (mirror `$01F9`) | `soundcomm.sound_command_r` — `/RDP` |
| `$2804` (mirror `$01F9`) | `rdio_r` — `/RDIO` |
| `$2806` (mirror `$01F9`) | `sound_irq_ack_r/w` — `/IRQACK` |
| `$2A00` (mirror `$01F9`) | `oki_w` — `/WRV` |
| `$2A02` (mirror `$01F9`) | `soundcomm.sound_response_w` — `/WRP` |
| `$2A04` (mirror `$01F9`) | `wrio_w` — `/WRIO` |
| `$2A06` (mirror `$01F9`) | `mix_w` — `/MIX` |
| `$3000–$3FFF` | `cpubank` |
| `$4000–$FFFF` | ROM |

`rdio_r` (atarijsa.cpp:859) returns `0x80` self-test / `0x40` NMI / `0x20`
sound-out-full / `0x18` `+5V` / `0x02` coin2 / `0x01` coin1, and **forces
`0x80` high** unless `test_read_cb()` is satisfied. For S.T.U.N. Runner
`m_jsa->test_read_cb().set_ioport("IN0").bit(5)` (harddriv.cpp:1876) — i.e. the
board's own self-test bit at `IN0.5`, which is `PORT_SERVICE(0x20, IP_ACTIVE_LOW)`.
The 6502 cannot independently declare self-test.

### 3.5 ROM regions — `ROM_START(stunrun)` (harddriv.cpp:2691)

| Region | Size | Contents |
|---|---|---|
| `mainpcb:maincpu` | `0x100000` | 12 × 64 KiB `136070-21xx` as 6 `ROM_LOAD16_BYTE` byte-planes at `0x000000/0x000001`, `0x020000/0x020001`, … `0x0A0000/0x0A0001` |
| `mainpcb:jsa:cpu` | `0x10000` | `136070-2123.10c` |
| `mainpcb:user1` | `0x60000` | `ROM_REGION16_BE` — **the ADSP object ROM**, 6 × 64 KiB `136070-21xx.9x/.10x` |
| `mainpcb:jsa:oki1` | `0x40000` | 4 × 64 KiB ADPCM `136070-21xx.1xx` |
| `mainpcb:200e` | `0x800` | `stunrun.200e` — M48T02 timekeeper |
| `mainpcb:210e` | `0x800` | `stunrun.210e` — 2816 EEPROM |

25 files total, matching the canonical working set in `STATUS.md`.

---

## 4. Chip-to-chip wiring

### 4.1 68010 → GSP host port (`$C00000`)

`[MAME harddriv.cpp:611]` maps 16 KiB to `hd68k_gsp_io_r/w`, which reduce the
offset and hand it to the device:

```cpp
offset = (offset / 2) ^ 1;
m_hd34010_host_access = true;
m_gsp->host_w(offset, data);
```

**`offset` is a WORD offset, not a byte offset.** That single fact decides the
whole decode and has already been got wrong twice in this project — see
`docs/mc68010-boot-findings.md` §31.4, which retracts the first attempt.

```text
reg = ((cpu_addr - 0xC00000) / 2 / 2) ^ 1        integer division throughout
```

| 68010 address | word offset | `reg` | register (`tms34010.h:1099–1102`) |
|---|---|---|---|
| `$C00000` / `$C00002` | 0 / 1 | 1 | **`HSTADRH`** — upper 16 bits of host address |
| `$C00004` / `$C00006` | 2 / 3 | 0 | **`HSTADRL`** — lower 16 bits |
| `$C00008` / `$C0000A` | 4 / 5 | 3 | **`HSTCTL`** — control |
| `$C0000C` / `$C0000E` | 6 / 7 | 2 | **`HSTDAT`** — the data register |

Note the order is **H then L**, not L then H, and each register is reachable
from two consecutive addresses because `offset/2` repeats. `$C0000C`/`$C0000E`
→ HSTDAT is the single channel that moves bytes into or out of GSP VRAM.

**Both halves of that decode are measured, not argued.**
`[ROM] docs/mc68010-boot-findings.md` §13.2 and §31.4, using MAME's own
`stunrun_hostprobe` (`STUNRUN_GSP_HOSTPROBE`) *and* an independent
`install_write_tap` counting absolute addresses on the maincpu program space:

| 68010 address | word offset | `reg` | register | probe | absolute tap |
|---|---|---|---|---|---|
| `$C00000`/`$C00002` | 0/1 | 1 | **`HSTADRH`** | 39 | `$C00002`=39, `$C00000`=0 |
| `$C00004`/`$C00006` | 2/3 | 0 | **`HSTADRL`** | 39 | `$C00004`=39, `$C00006`=0 |
| `$C00008`/`$C0000A` | 4/5 | 3 | **`HSTCTL`** | 6 | `$C00008`=6 |
| `$C0000C`/`$C0000E` | 6/7 | 2 | **`HSTDAT`** | 780 315 | `$C0000C`=388 723, `$C0000E`=132 696 |

**Which address reaches which register is settled** — the two measurements agree
exactly for HSTADRH, HSTADRL and HSTCTL, and the HSTDAT *address pair* is
unambiguous because both halves of it decode to `reg 2` regardless of the count.

`UNKNOWN` — **a count discrepancy in the existing evidence record, flagged
rather than repeated.** §31.4's HSTDAT probe tally (780 315) does not equal the
sum of its absolute taps (388 723 + 132 696 = 521 419), yet the text there
claims the tap "reproduces the driver's four counts exactly". It reproduces
three of four. A separate §13.2 run reports HSTDAT = 1 101 056 from the probe
alone. Nothing here explains the gap; do not build anything on these totals as
if they were consistent. The per-register *routing* is what a port needs and it
is independently confirmed.

**`host_w` semantics** `[MAME tms34010.cpp:2003]`:

| `reg` | write |
|---|---|
| `HSTADRH` | `IOREG(REG_HSTADRH) = data` |
| `HSTADRL` | `IOREG(REG_HSTADRL) = data` |
| `HSTDAT` | `addr = (HSTADRH << 16) \| HSTADRL`; **`TMS34010_WRMEM_WORD(addr & 0xFFFFFFF0, data)`**; then if `HSTCTLH & 0x0800`, `addr += 0x10` and the address pair is rewritten (post-increment) |
| `HSTCTL` | split by `mem_mask`: high byte → `REG_HSTCTLH`, low byte → `REG_HSTCTLL` |

Two things a port must reproduce exactly:

* `& 0xFFFFFFF0` is a **16-bit-word** alignment (4 address units = 16 bits),
  not a 16-byte-cell alignment. This is the `>>3` correction of
  `docs/hardware-contract.md` §7: `HSTADR $FF896040` → physical byte offset
  `$12C08`.
* **`HSTDAT` write increments by `0x10` address units = 2 bytes = 1 word.**
  The auto-increment lives in `HSTCTLH` bit 11.

`host_r` (tms34010.cpp:2059) mirrors it, and reads post-increment on
`HSTCTLH & 0x1000` (bit 12), not bit 11.

**`HSTCTL` bit 15 (`HLT`) is the GSP freeze.** `[ROM] STATUS.md` gsvp.5:
`/GSPRES` goes released once at power-on *before any host write* and toggles
again at frames 362 and 396 while the GSP stays halted; the freeze that holds it
through the whole 6 091-word upload is `HSTCTL` bit 15, cleared at `$0002C1C4`
in frame 405 at `HSTCTL $7800`. A port that models only `/GSPRES` will run the
GSP at the wrong times.

**Reset, the other way — and here MAME and the manual agree exactly.**
`hd68k_nwr_w` (harddriv_m.cpp:466) again takes its value from **address bit 3**
and decodes the function from the low three bits:

```c
data = (offset >> 3) & 1;   /* offset = word offset inside $604000-$607FFF */
offset &= 7;
case 6: /* /GSPRES */ m_gsp->set_input_line(INPUT_LINE_RESET, data ? CLEAR_LINE : ASSERT_LINE);
case 7: /* /MSPRES */ (MSP absent — no effect)
```

| 68010 address | word offset | bit 3 | result | `[MANUAL]` S6 |
|---|---|---|---|---|
| `$60400C` | 6 | 0 | `ASSERT_LINE` — **GSP held in reset** | `GSP Reset Low` |
| `$60401C` | 14 | 1 | `CLEAR_LINE` — **GSP released** | `GSP Reset High` |

Both sources name the same two addresses for the same two polarities. This is
the strongest manual↔driver agreement in the whole `$60xxxx` window, and it is
the right place to learn the idiom: **on this board the control bits are
addresses, and the data bus carries nothing at these strobes.**

### 4.2 68010 → ADSP-2100

Five windows installed by `init_adsp()` (harddriv.cpp:5070), none of which is in
`multisync_68k_map`:

| 68010 range | Handler | ADSP side |
|---|---|---|
| `$800000–$807FFF` (32 KiB) | `hd68k_adsp_program_r/w` | program RAM, cell `n = offset/2`; **even offset → `word >> 16`, odd → `word & 0xFFFF`** |
| `$808000–$80BFFF` (16 KiB) | `hd68k_adsp_data_r/w` | data RAM `$0000–$1FFF` |
| `$810000–$813FFF` (16 KiB) | `hd68k_adsp_buffer_r/w` | the serial output (SOM) buffer, one of two 16 KiB banks |
| `$818000–$81801F` | `hd68k_adsp_control_w` | control strobes, decoded on `offset & 7` |
| `$818060–$81807F` | `hd68k_adsp_irq_clear_w` | clears `m_adsp_irq_state` |
| `$838000–$83FFFF` | `hd68k_adsp_irq_state_r` | returns `$FFFD`, `^2` if `XOUT`, `^1` if an ADSP interrupt is pending |

`m_som_memory` is declared `uint16_t[0x8000/2]` = **`0x4000` words = 32 KiB**
(harddriv.h:386), and every access uses a bank stride of `0x2000` words. That
divides exactly: **two banks of 16 KiB**, no more. The bank selector is
`val = (offset >> 3) & 1` in `hd68k_adsp_control_w` `case 3`, so it can only ever
be **0 or 1**. The 68010 window is exactly one bank wide (`$810000`–`$813FFF`
= 0x2000 word offsets), so `hd68k_adsp_buffer_r/w` indexes
`m_som_memory[bank * 0x2000 + offset]` (harddriv_m.cpp:683, 689) with **no
masking** — a window read always lands in the selected bank, and a bank of 2 or
above would run off the array.

So the ADSP and the 68010 are always on **opposite halves of one 32 KiB
buffer**: the ADSP writes `bank ^ 1`, the 68010 reads `bank`. That is a
two-deep ping-pong, not a four-way ring — worth getting right, because the
`^1` looks like an XOR into a larger array than it actually is.

**Control strobes** — the value comes from **address bit 3, not from the data**
(`int val = (offset >> 3) & 1; offset &= 7;` harddriv_m.cpp:751):

| `offset & 7` | Function |
|---|---|
| 0, 1 | LEDs (ignored) |
| 2 | ignored |
| 3 | **SOM buffer bank switch**, deferred to a scheduler synchronize |
| 4 | ignored |
| 5 | `/BR` — bus request; asserts ADSP `INPUT_LINE_HALT` |
| 6 | `/HALT` — asserts ADSP `INPUT_LINE_HALT` |
| 7 | **ADSP `INPUT_LINE_RESET`**; also `m_maincpu->yield()` |

Clearing `/BR` or `/HALT` additionally calls `m_maincpu->spin()` — the source
comment says a plain yield is not enough and the interleaving would have to be
increased.

**Data-window write side effect** (harddriv_m.cpp:657): *any* write to
`$808000 + 0x1FFF` (i.e. `$809FFF`) is taken as a synchronisation trigger:
`machine().scheduler().synchronize(); m_adsp->signal_interrupt_trigger();`.
That is the 68010's way of waking the ADSP.

**Reset state** `device_reset()` (harddriv_m.cpp:50): the ADSP is halted at
reset, `m_adsp_halt = 1`, `m_adsp_br = 0`, `m_adsp_xflag = 0`,
`m_m68k_adsp_buffer_bank = 0`.

### 4.3 ADSP-2100 → 68010 (data memory, object ROM, interrupts)

The whole host interface is `DM($2000)`–`DM($2007)`, decoded on `offset & 7`
inside `hdadsp_special_r/w` (harddriv_m.cpp:842, 869):

| DM | Name | Direction | MAME behaviour |
|---|---|---|---|
| `$2000` | `/SIMBUF` | ADSP ← host | `m_sim_memory[m_adsp_eprom_base + m_adsp_sim_address++]`, else `$FF` |
| `$2001` | `/SIMLD` (r) / `/SIMCLK` (w) | ADSP → host | write loads `m_adsp_sim_address` |
| `$2002` | `/SOMO` (r) / `SOMLATCH` (w) | ADSP → host | write: `m_som_memory[(bank ^ 1) * 0x2000 + (addr++ & 0x1FFF)] = data` |
| `$2003` | `/SOMLD` (r) / `/SOMCLK` (w) | ADSP → host | write loads `m_adsp_som_address` |
| `$2004` | — | — | falls to the default `logerror` branch |
| `$2005` | `/XOUT` | ADSP → host | `m_adsp_xflag = data & 1` |
| `$2006` | `/GINT` | ADSP → host | asserts `m_adsp_irq_state` → 68010 IPL 2 |
| `$2007` | `/MP` | ADSP → host | `m_adsp_eprom_base = 0x10000 * data` — selects a **128 KiB** object-ROM bank |

Two asymmetries that are easy to get backwards and are worth stating flatly:

* **`/MP` selects the *object ROM*, not program RAM.** `$2007` sets
  `m_adsp_eprom_base = 0x10000 * data`, indexing `m_sim_memory` — which is
  `required_region_ptr<uint16_t>` on `mainpcb:user1`, the 384 KiB
  `ROM_REGION16_BE` (§3.5). **`0x10000` is words, so each `/MP` value is a
  128 KiB bank and only values 0–2 are in range.** The read at `$2000` is
  bounds-checked (`if (m_adsp_eprom_base + m_adsp_sim_address <
  m_sim_memory.length())`) and returns `$FF` otherwise. The ADSP's own program
  RAM is loaded by the **68010**, over `$800000` (§4.2), not by the ADSP
  fetching from a ROM.
* **`SOMLATCH` writes to the *opposite* bank:** `(m_m68k_adsp_buffer_bank ^ 1)`.
  The 68010 reads bank `m_m68k_adsp_buffer_bank`; the ADSP writes bank `^1`.
  Double-buffering, and with only two banks (§4.2) the XOR is exactly the
  ping-pong. The same XOR must appear on both sides or the producer and the
  consumer read different halves.

**[ROM]** `analysis/interconnect.md` records the observed title-path traffic:
`SIMBUF` 73 416, `SIMCLK` 33 158, `SOMLATCH` 84 700, `SOMCLK` 30, `GINT` 30,
`MP` 10 335 accesses per 600-frame trace, with `$2004` and `XOUT` at zero.
30 `GINT` writes correspond to 30 68010 `IRQ 2` entries.

### 4.4 68010 → JSA II sound board

The `$600000` window (16 KiB) is re-installed in `init_multisync()` over the
`jsa` device, `umask16(0xFF00)`, so only the even byte at `$600000` is live.
`[MANUAL]` calls it `SCOM`, serial sound communications
(`docs/hardware-contract.md` §1).

On the 6502 side the same conversation appears as `$2802`/`$2A02`
(`/RDP` / `/WRP`). `ATARI_SOUND_COMM` (src/mame/atari/atariscom.{h,cpp}) is the
bidirectional latch: `main_command_w` / `main_response_r` on the 68010 side,
`sound_command_r` / `sound_response_w` on the 6502 side, with `int_callback()`
wired to `harddriv_state::sound_int_write_line` → 68010 IPL 4.

`$604000` read (`hd68k_sound_reset_r`) calls `m_jsa->reset()` and returns
`$FFFF`. **`[MANUAL]`** gives `SCRES` its own address and separates it from the
main GSP reset net (`docs/hardware-contract.md` §8.3) — MAME agrees they are
different resets.

**`umask16(0xFF00)` means only the even byte is decoded**, and that is not a
technicality. `[ROM] docs/mc68010-boot-findings.md` §14.2 scanned the whole
1 MiB 68010 image for absolute-long `$600000` operands and found **five
instruction sites, every one of them a `move.b`** — none at `$600002`, no
`move.w` and no `move.l` anywhere in the ROM. (The sixth row below is the IPL 4
handler's read, recorded separately in §14.4.)

| 68010 PC | Instruction | Role |
|---|---|---|
| `$008DCC` | `move.b $600000.l,D0` | boot handshake, poll for `$FF` |
| `$008DDA` | `move.b #$7,$600000.l` | boot handshake — the JSA kick command |
| `$008DFA` | `move.b $600000.l,D0` | boot handshake, read back |
| `$023EC0` | `move.b D0,$600000.l` | sound-command queue, first store |
| `$023EF6` | `move.b D0,$600000.l` | queue **retry** store, under `sr $2400` |
| `$023F66` | `move.b $600000.l,(A4)` | IPL 4 handler, response read |

So the multi-byte split behaviour of the transport is unobservable in this ROM.
A port does not need to guess what a `move.l` to `$600000` would do.

### 4.5 GSP → video, and the scanout arithmetic

The 68010 and the ADSP have **no** access to the frame buffer except through the
host port. All display state is in the GSP's own register file, and **MAME's
renderer reads that register file directly** — there is no pixel-generating
device to model.

```text
tms340x0_ind16                              (screen update, harddriv.cpp:1512)
  └── get_display_params()                  tms34010.cpp:1429
        params->enabled = DPYCTL & 0x8000
        params->vcount  = VCOUNT
        params->veblnk  = VEBLNK      params->vsblnk = VSBLNK
        params->heblnk  = HEBLNK * m_pixperclock
        params->hsblnk  = HSBLNK * m_pixperclock
        if (!(DPYCTL & 0x0400)) dpyadr ^= 0xFFFC;
        params->rowaddr  = dpyadr >> 4
        params->coladdr  = ((dpyadr & 0x007C) << 4) | (DPYTAP & 0x3FFF)
        params->yoffset  = (DPYSTRT - DPYADR) & 3
  └── scanline_multisync()                  harddriv_v.cpp:448
```

`scanline_multisync` — the exact arithmetic a port has to reproduce:

```c
uint16_t const *const vram_base = &m_gsp_vram[(params->rowaddr << 10) & m_vram_mask];
int coladdr = (params->yoffset << 9)
            + ((params->coladdr & 0xff) << 3)
            - 7
            + (m_gfx_finescroll & 0x07);
for (int x = params->heblnk; x < params->hsblnk; x++) {
    int coloridx = coladdr++ & 0x7ff;
    uint8_t color = (coloridx & 1) ? (vram_base[coloridx >> 1] >> 8)
                                   :  (vram_base[coloridx >> 1]);
    dest[x] = m_gfx_palettebank * 256 + color;
}
```

Five things in that one expression, all of which have already been mis-derived
at least once in this project:

* `<< 10` — the row stride, in **words** (2 bytes each).
* `<< 9` on `yoffset`, `<< 3` on `coladdr` — both land in the same
  bit-address-derived unit.
* `& 0x7FF` — the 2048-word visible span within the row.
* `- 7` — the pipeline offset, with `+7` for `coladdr` cancelling against the
  `yoffset << 9` term at whole-line boundaries.
* even `coloridx` → low byte of the word, odd → high byte.

**Palette.** `PALETTE(config, m_palette).set_entries(1024)` — 1024 pens, and the
scanout writes `palettebank * 256 + colour` into an `ind16` bitmap. The two
palette planes are 16-bit each, combined by `gsp_palette_change`
(harddriv_v.cpp:330):

```c
red   = (paletteram_lo[offset] >> 8) & 0xff;
green =  paletteram_lo[offset]       & 0xff;
blue  =  paletteram_hi[offset]       & 0xff;
```

Both plane handlers fold the bank in identically:

```c
offset = m_gfx_palettebank * 0x100 + (offset & 0xff);
```

so only the low 8 bits of the window address reach the array. The bank comes
from `gsp_control_hi`: `offset & 7 == 2` sets bit 0, `== 3` sets bit 1
(harddriv_v.cpp:238–243), and `== 4` sets bit 2 **only if
`m_palette->entries() >= 256*8`**. At 1024 entries that test fails, so the bank
is **0–3** and the 1024 pens are exactly 4 banks × 256. `m_gfx_finescroll`
comes from `gsp_control_hi` `offset & 7 == 1`, masked `data & (15 >> 1)` = `& 7`.

**The same idiom again, and it is easy to miss:** in `hdgsp_control_hi_w` the
*written data* is latched into `m_gsp_control_hi[offset]` via `COMBINE_DATA`,
but **the side effects read `val = (offset >> 3) & 1` — address bit 3, not the
data** (harddriv_v.cpp:215–217). Every `gsp_control_hi` sub-register is
therefore selected by its address and valued by address bit 3, the same way the
68010's control strobes are. `gsp_control_lo` has no such behaviour: it is a
plain 256-entry latch, read back verbatim, and its entry 0 is the fill colour
(§4.5's expander).

**Shift register / the 2bpp expander.** `gsp_control_hi` `offset & 7 == 0`
sets `m_shiftreg_enable` from address bit 3. The buffer is
`m_shiftreg[(8 * 512 * sizeof(uint16_t))/2]` = **4096 `uint16_t` = 8 KiB**
(tms34010.h:354) — eight lines of 512 words. Two callbacks are wired in
`driver_nomsp()` (harddriv.cpp:1490–1491) and inherited by the multisync config:

* `hdgsp_write_to_shiftreg` (`set_shiftreg_in_callback`, harddriv_v.cpp:105) —
  **on a GSP shift-register write** it only selects `m_gsp_shiftreg_source` as a
  pointer into `m_gsp_vram`; it copies nothing.
* `hdgsp_read_from_shiftreg` (`set_shiftreg_out_callback`, harddriv_v.cpp:131) —
  **on the read-back** it does the actual `memmove` into VRAM, and is gated on
  `m_shiftreg_enable` (it returns immediately if clear).

Two addressing formulas, one per window:

| Window | address → word index | alignment mask | bytes moved |
|---|---|---|---|
| `$02000000–$020FFFFF` | `>>= (m_gsp_multisync + 1)` = `>>2` after the base subtract | `& ~((256*8 >> 1) - 1)` = `& ~1023` | `512*8 >> 1` = 2048 `uint16` = 4 KiB |
| `$FF800000–$FFFFFFFF` | `/= 16` after the base subtract | `& ~255` | `512` `uint16` = 1 KiB |

Both then `& m_vram_mask`. `[MANUAL]` Page 10 puts a **TMS34012** between the GSP
and the pixel stream, and **[MAME] has no such device**. MAME's shift-register
model is therefore a stand-in for hardware it does not simulate — which is why
`docs/hardware-contract.md` §7 calls the scanout contract's "two 1024×16-bit
planes" an implementation hypothesis rather than a transcription.

**The 2bpp write expander** `[MAME harddriv_v.cpp:304]`:

```c
void harddriv_state::hdgsp_vram_2bpp_w(offs_t offset, uint16_t data)
{
    uint32_t *dest = (uint32_t *)&m_gsp_vram[offset * 4];
    uint32_t *mask = &m_mask_table[data * 2];
    uint32_t color = m_gsp_control_lo[0];
    color |= color << 16;
    … four masked word writes …
}
```

Each 16-bit source word written into `$0200xxxx` expands to **four 32-bit
writes**, coloured from **`m_gsp_control_lo[0]`** (the full 16 bits, no mask),
using `m_mask_table[data * 2]` — the two-entries-per-word half of the table,
because `m_gsp_multisync == 1` (`init_video()`, harddriv_v.cpp:46). The 1bpp
variant `hdgsp_vram_1bpp_w` uses `data * 4` and `control_lo[0] & 0xff`, and is
only wired into `driver_gsp_map`, **not** the multisync map.

`UNKNOWN`: the stride relationship between this direct expander and the shift
register path (`address >>= m_gsp_multisync + 1`) is not reconciled here — they
mask into the same 2 KiB row but the offsets are computed differently. The
project's native model works through its own derivation; see findings §34/§35
rather than treating this paragraph as settled.

### 4.6 Analog and digital inputs

**`IN0` at `$60C000`** — `hd68k_port0_r` composes two ioports:

```c
int temp = (m_sw1.read_safe(0xff) << 8) | m_in0->read();
if (get_hblank(scr)) temp ^= 0x0002;   /* HBLANK from the GSP's screen */
temp ^= 0x0008;                         /* 12-bit EOC forced high "for now" */
```

| bit | source | note |
|---|---|---|
| D0 | `PORT_DIPNAME` | Diagnostic jumper |
| D1 | `get_hblank(screen)` | **live from the GSP screen object** |
| D2 | `screen.vblank()` | live |
| D3 | hard-wired `$1` | 12-bit EOC is forced; `hd68k_adc_control_w` really drives the converter |
| D4 | `adc8.eoc_r()` | live |
| D5 | `PORT_SERVICE(0x20, IP_ACTIVE_LOW)` | self-test; also the JSA `test_read_cb` (§3.4) |
| D6 / D7 | `IPT_COIN2` / `IPT_COIN1` | |
| D8–D15 | `mainpcb:SW1`, eight DIPs, default `0x00` | boot-behaviour selectors |

`[MANUAL]` calls the whole 16-bit word `SW1`
(`docs/hardware-contract.md` §2), and that document also establishes that these
are **boot option switches, not coin/credit settings** — the coin policy lives
in RAM behind the operator screens. Do not build a DIP-driven coin mechanism.

**`$A80000`** — `hd68k_a80000_r` returns the `a80000` ioport, which is:

```c
PORT_BIT( 0x0001, IP_ACTIVE_LOW, IPT_BUTTON1 )
PORT_BIT( 0x0002, IP_ACTIVE_LOW, IPT_BUTTON2 )
PORT_BIT( 0x0004, IP_ACTIVE_LOW, IPT_START1 )
PORT_BIT( 0xfff8, IP_ACTIVE_LOW, IPT_UNUSED )      ← thirteen bits discarded
```

**[MANUAL]** calls this `/SW2`, *"Sixteen External Switch Inputs"*, and
SP-338 Page 1 draws **left and right triggers as discrete switch nets** that
drive the operator menus. So MAME is throwing away inputs the board has.
Recorded as a driver gap in `docs/hardware-contract.md` §5 and §8.2, not fixed
here.

**ADC0808** at `$B00001` (mirrored `0x7FFFE`), driven by `hd68k_adc_control_w`
(bits 0–2 = channel, bit 3 = start). Channels wired: `8BADC.0` = `IPT_AD_STICK_X`,
`8BADC.2` = `IPT_AD_STICK_Y`; channels 1 and 3–7 are `IPT_UNUSED`.

**12-bit A/D** at `$B80000` (harddriv_m.cpp:369). The control register is read
bit-by-bit rather than as an address decode: **bit 6** enables a conversion,
**bits 5–4** select one of four channels and latch its value immediately,
**bit 7** chooses whether `$B80000` returns the low or the high byte. All four
channels are `IPT_UNUSED` in MAME. `[MANUAL]` describes the flight control as
**two** potentiometer axes (vertical from the handle assembly, horizontal from
the flight control assembly) plus a latched steering wheel at `/WR2`.

### 4.7 Interrupt priority — the 68010 IPL lines

`update_interrupts()` (harddriv_m.cpp:93) is the whole table:

| IPL | Asserted by | Source |
|---|---|---|
| 1 | `m_msp_irq_state` | MSP — **never, absent** |
| 2 | `m_adsp_irq_state` | ADSP `/GINT` (DM `$2006`) |
| 3 | `m_gsp_irq_state` | GSP `output_int` |
| 4 | `m_sound_int_state` | `/LINKIRQ` from the JSA |
| 5 | `m_irq_state` | periodic 244.14 Hz timer; cleared by `hd68k_irq_ack_w` |
| 6 | `m_duart_irq_state` | MC68681 |

**[MANUAL]** SP-338 Page 3 has a priority encoder combining `/OUIRQ`,
`/LINKIRQ`, `/GSPIRQ`, `/ADSPIRQ`, `/MSPIRQ` into IPL2/IPL1/IPL0 — five nets,
where MAME spreads them over six IPL lines with the timer and the DUART added.

**`UNKNOWN` / observation, not a bug claim:** `m_screen->screen_vblank().set(
FUNC(harddriv_state::video_int_write_line))` is wired at harddriv.cpp:1515, and
`video_int_write_line` sets `m_video_int_state = 1` (harddriv.cpp:1449–1456) —
but `m_video_int_state` is **never read anywhere in the driver**
(`grep m_video_int_state` returns the declaration, the constructor init, and that
one assignment). **[INFER]** VBlank therefore raises **no** 68010 interrupt in
this model; the game must poll `$60C000` bit 2 or take the GSP's display
interrupt (IPL 3) instead. A port that wires VBlank to an interrupt is *not*
faithful to MAME and will not match its frame-boundary behaviour.

### 4.8 The `$810000` buffer and the GSP FIFO — the cross-chip data path

This is the path the whole renderer hangs off, and it is worth stating as one
chain:

```text
ADSP  DM($2002) SOMLATCH ──▶ m_som_memory[(bank^1) * 0x2000 + addr++ & 0x1FFF]
                                    │  68010-visible at $810000 + (bank * 0x2000) + offset
                                    ▼
68010  reads a length-prefixed block from $810000
                                    ▼
68010  copies it to $C0000C (HSTDAT) ──▶ GSP VRAM
                                    ▼
GSP   rasterises from that display list
```

`[ROM] analysis/interconnect.md`: on the title path, 68010 PC `$02C5EA` runs 14
times per 600-frame trace, each passing `$810000` to `$02F0AE`, which consumes a
length-prefixed block and copies it to the GSP FIFO at `$C0000C` with a `-1`
terminator check.

> Reading MAME's own logs: the FIFO entries print as `addr=00C0000C`
> (`reference/experiments/stunrun/display-list-service-f2-60-180/mame.log`).
> That leading `00` is MAME's zero-padded 24-bit 68010 formatting — it is the
> same address as `$C0000C`, HSTDAT, not a different window.

**The GSP has no ROM of its own.** `mainpcb:user1` is the ADSP object ROM; the
GSP's program is uploaded by the 68010 through the host port. `[ROM] STATUS.md`
gsvp.5: `$0002FE68`/`$0002FE6E` write 6 091 words each across frames 396–405
into `$FFF40000`–`$FFFFFFE0`, then `$0002C1C4` clears `HLT` in frame 405.

### 4.9 Speed-up handlers (debug only)

`init_stunrun()` installs four. They are **always live** — they intercept real
addresses. What `#define DISPLAY_SPEEDUPS 0` (harddriv_v.cpp:26) disables is
only the *on-screen counter* drawn by `display_speedups()`. Each write handler
`COMBINE_DATA`s straight into the backing VRAM and, when the result is `$FFFF`,
calls `m_gsp->signal_interrupt_trigger()` to break the GSP out of a spin loop
(harddriv_m.cpp:1904–1922):

| Where | Handler |
|---|---|
| GSP `$FFF9FC00–$FFF9FC0F` | `hdgsp_speedup1_w` |
| GSP `$FFFCFC00–$FFFCFC0F` | `hdgsp_speedup2_w` |
| GSP `$FFF9FC00–$FFF9FC0F` | `hdgsp_speedup_r` |
| ADSP data `$1FFF` | `hdadsp_speedup_r` |

`m_gsp_speedup_addr[]` is computed as `&m_gsp_vram[(addr - 0xFFC00000) >> 4]`,
which encodes the same `addrshift = 3` (§3.2) — a useful cross-check that the
whole driver is consistent about bit addressing.

---

## 5. What to take into a modern implementation

### 5.1 Facts that are cheap to get wrong

1. **TMS34010 addresses are bit addresses.** `>> 3` for a byte offset, `>> 4`
   for a word index. Every GSP-side address is wrong by 8× without it.
2. **VRAM is 512 KiB**, not 4 MiB. The 4 MiB in the map is address space.
3. **The host port decodes on word offsets** and in the order **HSTADRH,
   HSTADRL, HSTCTL, HSTDAT** at `$C00000`, `$C00004`, `$C00008`, `$C0000C`.
4. **`HSTCTL` bit 15 freezes the GSP** for the whole upload; `/GSPRES` does not.
5. **Control strobes take their value from address bit 3**, never from the
   written data — for `/BR`, `/HALT`, ADSP reset, `/GSPRES`, the shifter
   selects and the coin counters alike.
6. **The ADSP writes the SOM buffer bank `^1`** relative to the bank the 68010
   reads.
7. **`/MP` ($2007) selects the object ROM**, not program RAM.
8. **No VBlank interrupt reaches the 68010** in this model.
9. **The 68010 loads the ADSP's program** through `$800000`; the ADSP never
   fetches instructions from `user1`.

### 5.2 Gaps in the model, listed so they are not mistaken for facts about the board

| Gap | Evidence |
|---|---|
| No TMS34012 pixel processor | absent from the runtime inventory; `[MANUAL]` Page 10 |
| Thirteen of sixteen `/SW2` inputs discarded | harddriv.cpp:1123–1127 vs `[MANUAL]` |
| Shifters and triggers absent | `hd68k_wr0_write` latches `m_sel[0..3]` and `hd68k_wr1_write` is the only reader — there is no consumer; the trigger lines have no input port at all |
| 12-bit ADC channels all `IPT_UNUSED` | harddriv.cpp:1153–1163 |
| `OPTO` optical wheel reader absent | `[MANUAL]` §6 — but SP-338 Page 9 says `NOT LOADED` for this game, so its absence is probably correct |
| 12-bit EOC forced high at `$60C000` bit 3 | harddriv_m.cpp:288, comment says "always high for now" |
| Main RAM modelled as one flat 512 KiB | no bank split, no ROM into RAM |
| 34012-era colour RAM not modelled | palette is two 16-bit planes inside the board object |

### 5.3 Reproducing any of this

```bash
# the tree still matches its recorded patch inventory (9 tests)
python3 -m unittest discover -s tests -p 'test_mame_local_patches.py'

# the runtime device list behind §1.1
python3 -m unittest discover -s tests -p 'test_public_tools.py' -k inventory -v
tools/mame-inventory roms OUT

# the host-port decode behind §4.1, including the raw-offset histogram
STUNRUN_GSP_HOSTPROBE=/tmp/opencode/hostprobe.txt \
  PATH=/mnt/build/mame-ref:$PATH mame stunrun -noreadconfig -nowriteconfig \
  -cfg_directory /tmp/opencode/cfg -nonvram_save -video none -input none \
  -seconds_to_run 10

# the GSP display geometry and the VRAM share, behind §4.5
STUNRUN_DUMP_DIR=/tmp/opencode/srshare \
  PATH=/mnt/build/mame-ref:$PATH mame stunrun … -video none -input none -seconds_to_run 10
```

`mame` is not on `PATH` by default. Use `/mnt/build/mame-ref` for the working
tree (has MD-12) or `/mnt/build/mame-oracle/mame` for the frozen oracle (does
not) — and say which one produced a number.

---

## 6. Related documents

| Document | Owns |
|---|---|
| [`docs/hardware-contract.md`](hardware-contract.md) | the `[MANUAL]` half: the 1989 SP-338 map against MAME, with the gaps |
| [`analysis/interconnect.md`](../analysis/interconnect.md) | the M1 working record, observed-in-trace edges |
| [`analysis/chip-interconnects.md`](../analysis/chip-interconnects.md) | an earlier interconnect table (predates several corrections here) |
| [`analysis/ram-map.md`](../analysis/ram-map.md) | RAM annotations with runtime confidence |
| [`analysis/hardware-lineage.md`](../analysis/hardware-lineage.md) | the Hard/Race Drivin' board lineage (S3) |
| [`docs/mc68010-boot-findings.md`](mc68010-boot-findings.md) | §31.4 host-port decode, §34 VRAM correction, and the refutations |
| [`analysis/mame-local-patches.json`](../analysis/mame-local-patches.json) | the tree's patch inventory |
| [`BUGS.md`](../BUGS.md) | MD-04, MD-05, MD-12 and the rest |
| [`docs/tms34010-reference.md`](tms34010-reference.md) | the processor itself, beyond the driver's use of it |