# GSP indexed scanout contract

This is the frozen, evidence-backed input contract for the indexed GSP
scanout. It describes how an already-produced GSP board state becomes the
native renderer's `512×240` output. It does **not** describe how the GSP
produces that state.

## Scope and output dimensions

The consumer output is exactly 512 columns by 240 rows. The native framebuffer
boundary defines `STUNRUN_RENDER_WIDTH = 512` and
`STUNRUN_RENDER_HEIGHT = 240` (`native/render.h:12-16`). The scanline probe
and rasteriser select:

```text
x = [HEBLNK, HSBLNK) = [110, 622)       -> 512 pixels
y = [VEBLNK, VSBLNK) = [19, 259)       -> 240 lines
```

These are pixel coordinates after the two-pixels-per-pixel-clock conversion
(`0x37 * 2 = 110`, `0x137 * 2 = 622`). The raw MAME device timing also records
a configured 512×228 visible timing window (`native/gsp_display.h:27-33` and
`tools/gsp_render.py:33-38`). That 512×228 timing fact is not the output
contract used by the verified frame crop; do not silently substitute it for
the 512×240 renderer dimensions.

## Scanout inputs

### VRAM plane

The scanout consumes 16-bit little-endian words from the 512 KiB `gsp_vram`
share: `0x80000` bytes, or `0x40000` (262,144) words. The 4 MiB GSP address
window is a mirror of that share, not additional framebuffer storage. The
share size and mask are confirmed by the MAME map and the `STUNRUN-DPY` probe:

```text
BASE off=61440 (0x00f000) byte=0x01e000 vram_words=262144 mask=0x003ffff
```

Evidence: `docs/mc68010-boot-findings.md:1921-1954`,
`native/musashi_cpu.h:67-70`, and `native/gsp_display.h:35-40`.

For each visible line, the rasteriser derives a word base and reads indexed
colour bytes as follows (`native/gsp_display.c:143-173`):

```text
base       = (rowaddr << 10) & 0x3ffff
colour_i   = (ci is odd) ? (vram[base + (ci >> 1)] >> 8)
                         : (vram[base + (ci >> 1)] & 0xff)
palette_i  = palbank * 256 + colour_i
```

The active colour index is seven-bit fine-scroll adjusted within an 11-bit
index stream (`ci & 0x7ff`). The row base is therefore a word address, not a
byte address. The observed DUDATE=4 state gives four 512-byte lines per VRAM
row; this is also recorded by the MAME-derived decoder in
`tools/gsp_render.py:78-103` and `docs/mc68010-boot-findings.md:1942-1947`.

### Palette planes

The palette is two separate, parallel planes, each containing 1024 consecutive
little-endian 16-bit entries (2048 bytes):

```text
low plane:  0xF5000000, entry = R << 8 | G
high plane: 0xF5800000, entry = B << 8 | 0
```

For palette entry `i`, scanout expands
`RGB = ((low[i] >> 8) & 0xff, low[i] & 0xff, high[i] & 0xff)`.
The planes must not be interleaved. The indexed palette has four 256-entry
banks; `palette_i = palbank * 256 + colour_i`. Evidence: the MAME oracle
comments and address constants in `tools/mame_oracle.lua:89-101,107-121`, the
native expansion in `native/gsp_display.c:61-77`, and the palette/bank finding
in `docs/mc68010-boot-findings.md:1977-1984`.

### Display registers

The scanout register input is the `stunrun_gsp_display_regs_t` set, with the
following frame-600 attract-mode observation:

| register/input | value | role in this contract |
|---|---:|---|
| `DPYSTRT` | `0xFC3C` | refresh start / reload value |
| `DPYADR` | derived; initial renderer value is `DPYSTRT` | current refresh address |
| `DPYTAP` | `0x0000` | column tap contribution |
| `DPYCTL` | `0xF004` | `NIL` enable, `ORG=0`, `DUDATE=4` |
| `HEBLNK` | `110` | first output pixel |
| `HSBLNK` | `622` | exclusive output-pixel end |
| `VEBLNK` | `19` | first output line |
| `VSBLNK` | `259` | exclusive output-line end |
| fine scroll | `7` | low three bits of the column start |
| palette bank | `0` | 256-entry bank selector |

The values and their provenance are in `native/gsp_display.c:5-30` and the
MAME probe transcript at `docs/mc68010-boot-findings.md:1935-1947`.

With `ORG=0`, the address bits of `DPYADR` are one's-complemented while the
low two line-count bits are retained:

```text
x        = DPYADR ^ 0xFFFC
rowaddr  = x >> 4
coladdr  = ((x & 0x007C) << 4) | (DPYTAP & 0x3FFF)
yoffset  = (DPYSTRT - DPYADR) & 3
```

`DPYADR` is reloaded from the display start at the beginning of each line and
advances only while `VEBLNK <= line < VSBLNK`; it is frozen through vertical
blank. The literal advance is implemented in
`native/gsp_display.c:80-96,137-179`, transcribed from MAME's
`get_display_params()` and `update_display_params()`. Advancing it for every
timing line is not this contract.

## Ownership and provenance

The contract has three distinct owners:

1. **Producer state:** the MAME TMS34010/GSP execution and the 68010 host-port
   writes produce or modify the VRAM and palette contents. The 68010 reaches
   GSP memory through the host address/data registers at `0xC00000` rather
   than by directly addressing the GSP address space
   (`native/musashi_cpu.h:51-65`). The MAME GSP microcode is the producer of
   the populated display rows in the frame-600 oracle; the current native
   shell does not execute that microcode (`native/shell.c:458-478`).
2. **Scanout consumer:** the TMS34010 display logic owns the register-driven
   address walk and indexed conversion. The native `gsp_display` code is a
   toolkit-free transcription of that consumer; it reads supplied VRAM,
   palette planes, and register state and emits RGB pixels
   (`native/gsp_display.c:184-237`).
3. **Native frame/backend boundary:** the renderer owns only its copied/input
   snapshot and resulting RGB framebuffer. A live native frame is not a
   synchronized board snapshot: a partially built frame is explicitly
   legitimate, and the native live path uses fixed frame-600 register values
   because the GSP is not running (`native/shell.c:465-476`).

No stronger producer ownership is asserted for individual VRAM words or
palette entries. In particular, this document does not assign game meaning
to scanout rows, pixels, records, or host-port writes.

## Observable frame-600 acceptance example

The repeatable MAME checkpoint produces a `512×240` RGB oracle whose SHA-256
is:

```text
9aa7b12be07ef28b5796c64e67e722b595664cd09b8c5daead3d7a3707b9e85
```

The paired frame-600 VRAM/palette export, fed through the native visible-state
bridge, compares with zero changed pixels and zero changed channels. The
procedure and fixture provenance are recorded in
`analysis/m5-visible-output.md:128-147` and
`reference/experiments/stunrun/m5-native-visible-bridge.metadata.json`.
This is acceptance of the scanout boundary only; it is not acceptance that
the native port has reconstructed the GSP producer.

## Explicit non-goals

This contract does **not** claim or implement any of the following:

- FIFO protocol, HSTDAT transaction semantics, or display-list decoding;
- GSP geometry, `FILL XY`, `LINE`, `PIXBLT`, road/object/HUD record formats, or
  the producer of the populated VRAM rows;
- synchronization, frame fencing, or a guarantee that a live snapshot is
  internally stable;
- meanings for `DPYADR`, `DPYTAP`, VRAM rows, palette banks, or colour indices
  beyond their literal scanout roles;
- toolkit, OpenGL, window, audio, or presentation policy.

Those are separate investigations. The indexed scanout boundary starts with
already-produced VRAM/palette/register inputs and ends with the 512×240 RGB
frame.
