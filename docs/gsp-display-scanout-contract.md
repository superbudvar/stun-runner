# GSP indexed scanout contract

This is the toolkit-free input contract for the first video slice. It describes
the TMS34010 scanout only; it makes no claim about how the GSP populates VRAM
and does not define FIFO or geometry records.

## Board state

- VRAM share: `0x40000` 16-bit words (512 KiB). The GSP address window is
  larger and mirrors this share; consumers must mask word indices with
  `0x3ffff`.
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

## Provenance

The implementation is transcribed from the pinned MAME 0.289 source:
`src/mame/atari/harddriv_v.cpp` (`scanline_multisync()` and
`gsp_palette_change()`) and `src/devices/cpu/tms34010/tms34010.cpp`
(`get_display_params()` and display-address update). The project-side
implementation is `native/gsp_display.{h,c}` and its observable tests are
`gsp-display-slice-c` plus the optional captured-dump acceptance test.
