# GSP indexed scanout contract

This is the toolkit-free input contract for the first video slice. It describes
the TMS34010 scanout only; it makes no claim about how the GSP populates VRAM
and does not define FIFO or geometry records.

## Board state

- VRAM backing store: `0x200000` 16-bit words (4 MiB). This is the actual
  GSP local-memory address range `0xFF800000–0xFFBFFFFF`. Consumers must mask
  word indices with `0x1fffff`.  **This was 512 KiB in older documentation
  (§12.1, now superseded by §19).** The GSP address window is 4 MiB and is
  mirrored every 4 MiB (`mirror(0x0400000)`); the offset decode is
  `addr & 0x003fffff`.  Measured by `tools/mame-gsp-mirror-probe`: the
  share is 4,194,304 bytes and the first alias of offset 0 is at
  `base+0x400000`.  See `docs/mc68010-boot-findings.md` §19.
- Each word supplies two 8-bit indexed pixels: low byte first, then high byte.
- Palette state consists of two 1024-entry 16-bit planes. For entry `i`, the
  output colour is `R = lo[i] >> 8`, `G = lo[i] & 0xff`, `B = hi[i] & 0xff`.
- The active display is 512×240 pixels. The raw timing totals are 634×262;
  the visible window is `[HEBLNK, HSBLNK) × [VEBLNK, VSBLNK)`.

## Display registers

`stunrun_gsp_display_regs_t` is the stable core representation:

| Field | Meaning |
| --- | --- |
| `dpystrt` | refresh start address (`DPYSTRT`) |
| `dpyadr` | current refresh address (`DPYADR`); zero means derive from `dpystrt` |
| `dpytap` | horizontal tap (`DPYTAP`) |
| `dpyctl` | origin, update stride, and display-enable (`NIL`) bits |
| `heblnk`, `hsblnk` | horizontal visible bounds, already in pixels |
| `veblnk`, `vsblnk` | vertical visible bounds |
| `finescroll` | low three bits of fine scroll |
| `palbank` | selected 256-colour palette bank |

With `ORG=0`, the address conversion complements the address bits using
`dpyadr ^ 0xfffc`; the low two bits remain line-count state. `rowaddr` is
`x >> 4`, while `coladdr` is `((x & 0x7c) << 4) | (dpytap & 0x3fff)`.
`DPYADR` advances only between visible lines. Its update stride comes from
`DPYCTL & 0x03fc`; advancing through vblank is incorrect and shears the image.

## Ownership and API

The producer owns the VRAM and palette buffers for the duration of a render
call. The scanout consumer never mutates them. The core API is:

```c
int stunrun_gsp_display_render(stunrun_renderer_t *renderer,
                               const uint16_t *vram, size_t vram_words,
                               const uint16_t *palette_lo,
                               const uint16_t *palette_hi, size_t palette_count,
                               const stunrun_gsp_display_regs_t *regs);
```

`stunrun_gsp_display_rasterise()` is the deterministic indexed-output form
used by tests. It writes exactly `512 * 240` pen indices. The backend may then
upload the indexed image or expand it through the palette; no toolkit header is
reachable from this contract.

## Frame-600 acceptance fixture

The pinned frame-600 attract-mode register fixture is provided by
`stunrun_gsp_display_frame600_regs()`: `DPYSTRT=0xfc3c`, `DPYCTL=0xf004`,
`HEBLNK=110`, `HSBLNK=622`, `VEBLNK=19`, `VSBLNK=259`, `FINESCROLL=7`, and
palette bank zero. With the captured MAME VRAM/palette dump, the acceptance
test checks the 512×240 output, 47 distinct colours, and dominant RGB
`(177,46,36)` at 21.71%.

## Independent OpenGL comparison path

When live video uses SDL/OpenGL, the shell can open two windows:

- **GSP Scanout (CPU Reference)**: `stunrun_gsp_display_render()` reads the
  board state on the CPU and writes RGB888 pixels.
- **OpenGL GSP Scanout**: a separate fragment shader samples packed VRAM and
  both palette planes directly, then performs pixel selection and palette
  expansion on the GPU. The CPU supplies a compact per-visible-line address
  state derived from DPYADR sequencing; it does not supply CPU-rendered RGB.

`native/gsp_scanout_gl_test.c` compares GPU readback against the CPU reference
for the frame-600 registers, a non-default DPYADR/DPYTAP/fine-scroll case, and
display-disabled output. Current parity is exact (zero differing bytes) on
those fixtures. This comparison does not imply that the TMS34010 RTL emits a
pixel stream; it still exposes board state/registers, while both scanout paths
consume that state independently.

## GSP host port registers (MC68010 → GSP)

The MC68010 talks to the GSP entirely through the host port. The relevant
registers, confirmed from Ghidra disassembly of the GSP boot and text-draw
routines (`s_000003F8_gsp_boot_reset_main`, `s_000025E8_mc68010_text_draw`):

| Ghidra label | Address | Access | Purpose |
|---|---|---|---|
| `HSTADR_HI` | `$C00002` | write | Host address register, high word. Write first of two words to set a 32-bit GSP local-memory pointer. |
| `HSTADR_LO` | `$C00004` | write | Host address register, low word. Write second. |
| `HSTCTL` | `$C00008` | read-modify-write | Host control. `bit 15 = HLT` (halt GSP), `bit 12 = INCR` (auto-increment address on read), `bit 11 = INCW` (auto-increment on write), `bit 13 = LBL` (low byte last). |
| `HSTDAT` | `$C0000C` | read/write | Host data port. One 16-bit word per indirect GSP local-memory access. |

Address and data are always written as two 16-bit words (big-endian 68010 store).

## GSP boot / reset sequence

The full reset sequence is `s_000003F8_gsp_boot_reset_main` (`$3F8`).
Transcribed from Ghidra decompilation:

```
$C00008 = 0xA000       ; HSTCTL: HLT=1 (halt GSP), HLT preserved on read-modify-write
call $370              ; 400-cycle delay (s_00000370_gsp_boot_delay_400)
call $37C              ; load palette entries from RAM table via host port (s_0000037C_gsp_boot_load_palette)
call $34A              ; write (address, word) record pairs to FIFO (s_0000034A_gsp_boot_write_fifo_records)
call $7C2              ; write GSP control table (s_000007C2_gsp_boot_write_gsp_table)
call $3BE              ; set VRAM base 0xF4800000 (s_000003BE_gsp_boot_set_vram_base_F4800000)
call $37C              ; load palette x2
call $37C
call $48E              ; init FIFO write mode: HLT|INCR, delay, fill with incrementing values (s_0000048E_gsp_boot_init_fifo_write)
$C00002 = 0xF4800090  ; HSTADR = frame-buffer base
$C0000C = 0xFFFF      ; HSTDAT = colour
call $67A              ; fill VRAM 0xFF800000–0xFF9FFFFF with colour (s_0000067A_gsp_boot_fill_vram_color)
rts
```

**Palette loading** (`$37C`): reads 3-byte entries from a RAM table
(indexed by `D0 & 0xFFFF * 10 + 0x32C`), shifts each entry by
`entry[2]` bits (0–63), and ORs the result into `HSTDAT`. The table is
supplied by the MC68010 via shared RAM.

**FIFO record write** (`$34A`): writes `(address, word)` pairs from a table
to `HSTADR`/`HSTDAT` in a loop.

## Per-frame pipeline: MC68010 writes, GSP halts during scanout

The GSP is halted (`HSTCTL.HLT = 1`) for the majority of each frame.
The MC68010 owns VRAM during this time and writes:

- **Text** — `s_000025E8_mc68010_text_draw` (`$25E8`): writes 8-pixel cells via
  `HSTADR`/`HSTDAT`, incrementing by `0x10` per pixel and `0x1000` per row.
  Text glyphs live in the GSP's local-addressed glyph table at `DAT_00001FBC`
  (word-aligned 8×8 bitmaps, one 16-bit word per row, 16 words per glyph).
- **Road polygons** — `s_000022EC_mc68010_road_write_fifo_words` (`$22EC`):
  writes 8 words per poly to the FIFO, stride `0x400` between calls.

At end of frame the GSP is briefly released (`HSTCTL.HLT = 0`) to execute
its display list and scan out the frame. The display list blits from the
offscreen text/road buffer into the visible frame buffer.

**Service mode** (`STUNRUN_IN0` bit 5 held): the GSP is halted throughout.
Text is written to VRAM base `0x016040` (confirmed by MAME capture of
selector `$47` "PRESS START TO BYPASS SELFTEST"). The rasterizer in
`render_live_gsp_frame` must be pointed at `0x16000` word base
(`rowaddr` = `0x3C`, `DPYSTRT` = `0x00F000`) for service mode text to appear.

## BAD GSP VRAM diagnostic

`s_00004F82_mc68010_gsp_vram_test_main` (`$4F82`) is the "BAD GSP VRAM"
self-test. It writes `0x01010101` eight times to `0xFF945E` via the host
port, then calls `$010932` and `$004A48` before entering an infinite loop.
A correct read-back should return `0xFFFF` for each write (host port
mirrors/acks). If any bit reads back as 0, the test fails. This is the
MAME debugger hook for MD-09 ("BAD GSP VRAM") in `BUGS.md`.

## Provenance

The implementation is transcribed from the pinned MAME 0.289 source:
`src/mame/atari/harddriv_v.cpp` (`scanline_multisync()` and
`gsp_palette_change()`) and `src/devices/cpu/tms34010/tms34010.cpp`
(`get_display_params()` and display-address update). The project-side
implementation is `native/gsp_display.{h,c}` and its observable tests are
`gsp-display-slice-c` plus the optional captured-dump acceptance test.
