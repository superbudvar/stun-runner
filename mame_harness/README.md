# MAME Oracle Harness

Drives MAME as a frame-accurate **oracle** to capture GSP VRAM state at any
frame, then feeds those dumps into the native render pipeline for comparison.

## Architecture

```
mame_oracle.py          Python orchestrator (spawns MAME + Lua script)
  └─ mame_oracle.lua    Lua script (runs N frames → dumps memory → exits)
run_oracle.sh           Shell wrapper (paths, env, prerequisites check)
test_oracle_vram.c      C test (loads dumps → render pipeline → PPM)
```

## Prerequisites

1. **MAME binary** built at `../mame-ref/mame`
2. **STUN Runner ROMs** at `../roms/stunrun/` (the MAME `-rompath` is `../roms/`)
3. **Python 3**
4. **C compiler** (for `test_oracle_vram.c`)

## Quick Start

```bash
# 1. Build MAME (one-time, ~30-60 min on fast machine)
cd ../mame-ref && make OSD=sdl -j$(nproc)

# 2. Capture GSP state at frame 600
./run_oracle.sh --frames 600

# 3. Feed dumps into render pipeline
cd ../native/build
cmake .. && make test_oracle_vram
./test_oracle_vram ../oracle_dumps 600
```

## Scripted process policy

All Python MAME runners use `mame_harness/process.py`. It starts MAME in its
own process group and guarantees cleanup on timeout, interruption, or failure,
so an experiment cannot leave an emulator running after the test exits.

State snapshots are headless by default. Use `--windowed` when visual
interaction or mouse input is required:

```bash
./tools/mame-gsp-state-snapshot roms/stunrun /tmp/stunrun-gsp \
  --windowed --input none --frames 600 --targets 600
```

The windowed option selects SDL with `-window`; it does not use fullscreen.
For safe visual captures, add `--isolated-display`; this runs the windowed SDL
output on a private Xvfb display rather than the physical desktop.

To build a reviewable screen oracle, request several frame targets and a
directory of PNG snapshots. The service input is asserted before reset:

```bash
# Normal boot/title/early-attract candidates
./tools/mame-gsp-state-snapshot roms/stunrun /tmp/stunrun-oracle-normal \
  --frames 600 --targets 1,60,120,240,360,480,600 \
  --screen-dir /tmp/stunrun-oracle-normal/screens

# Service/self-test candidates (the service result still requires inspection)
./tools/mame-gsp-state-snapshot roms/stunrun /tmp/stunrun-oracle-service \
  --input service --frames 1800 --targets 1,60,120,240,600,900,1200,1800 \
  --screen-dir /tmp/stunrun-oracle-service/screens
```

Each target produces `screens/frame-NNNNNN.png`; the selected GSP state is
recorded alongside it in `result.json`.

## Files produced

| File | Size | Description |
|------|------|-------------|
| `vram.bin` | 1 MB | GSP VRAM (512×240×2 bytes, little-endian) |
| `palette_low.bin` | 512 B | Palette R+G channels (256×16-bit) |
| `palette_high.bin` | 256 B | Palette B channel (256×8-bit) |

## End-to-end test (once MAME is built)

```bash
# Run oracle capture
./mame_harness/run_oracle.sh --frames 600 --outdir ./oracle_dumps

# Build and run the C test
cd native/build
cmake ..
make test_oracle_vram
./mame_harness/test_oracle_vram ../oracle_dumps 600
```

## How the oracle works

1. MAME is started headlessly with a Lua script (`mame_oracle.lua`)
2. The Lua script:
   - Gets references to the `:maincpu` (68010) and `:gsp` (TMS34010) devices
   - Steps the machine frame-by-frame for N iterations
   - At frame N: reads GSP VRAM via `gsp_space:readblock(0, 0x100000)`
   - Reads palette RAM via maincpu space at `0xF5000000`
   - Writes three binary files to `--outdir`
   - Exits MAME cleanly
3. The C test loads the three files via `stunrun_gsp_video_load_palette_planes()`
   and renders via `stunrun_gsp_video_render()`

## Troubleshooting

**MAME binary not found**
```
ERROR: MAME not found at ../mame-ref/mame
```
→ Build MAME: `cd ../mame-ref && make OSD=sdl -j$(nproc)`

**No ROMs found**
```
ERROR: ROM directory not found at ../roms/stunrun
```
→ Place STUN Runner ROMs (25 files, CRC-rev6) at `../roms/stunrun/`

**Lua script fails to find `:gsp` device**
→ MAME Lua scripting may require a debug build. Try:
  `mame stunrun -debug -script mame_harness/mame_oracle.lua`

**palette_low.bin is empty or small**
→ The palette address `0xF5000000` may differ per MAME version.
  Check `src/mame/atari/harddriv.cpp` for `PALETTE_BASE` in your MAME version.
