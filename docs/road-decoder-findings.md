# Road Decoder Tool — Findings

## Overview

Two tools decode the 384-word track tables from the MC68010 ROM:

- **`native/road_decoder.c`** — Text-based analysis tool, good for scripting
- **`native/track_viewer_3d`** — OpenGL 3D viewer with interactive controls

## Key Decodings

### Road_expand_bits format

Each 16-bit word in the track table expands to 32 bits via ROXL expansion:

```text
bits 31..24  : color/type code
bits 23..16  : left edge position
bits  7.. 0  : right edge position
```

### Color/Type Codes

| Code (hex) | Meaning |
| --- | --- |
| `0x2a` | ROAD — asphalt surface |
| `0x22` | EDGE — barrier/edge markings |
| `0x20` | SHOULDER — rumble strip |
| `0x08` | CENTER — center line dashes |
| `0x02` | TUNNEL_A — tunnel interior variant A |
| `0x0a` | TUNNEL_B — tunnel interior variant B |
| `0x28` | TUNNEL_C — tunnel interior variant C |
| `0x00` | EMPTY — no drawing |

### Curve Detection

The left/right positions encode the horizontal pixel positions of the road edges:
- For a straight road, both edges are roughly symmetric around center (~85)
- For curves:
  - center < 70 → left curve (road shifts right as it goes into distance)
  - center > 100 → right curve (road shifts left as it goes into distance)

### Example Track 0 Decoding

From ROM address `0x044630`:
```
Idx   Raw     Expanded     Code  Left  Right  Type
  5   0xFF73  0x2AAA2A0A  0x2A   170    10   ROAD
  7   0xFF57  0x2AAA222A  0x2A   170    42   ROAD RIGHT->
 19   0x519A  0x22028288  0x22     2   136   EDGE <-LEFT
 20   0xB268  0x0A082880  0x0A     8   128   TUNNEL_B
 22   0xF84E  0x2A8020A8  0x2A   128   168   ROAD RIGHT->
 58   0x7432  0x2A200A08  0x2A    32     8   ROAD <-LEFT
```

## Tunnel Geometry

Tunnel entries (codes 0x02, 0x0a, 0x28) appear throughout Track 0 at specific
ROM offsets. Tunnel mouth data was found at ROM address `0x49348` with a repeating
signature pattern `2580 0000 9880 0005 dc00` appearing at regular intervals.

## Usage

### road_decoder (text tool)

```bash
# List available tracks
./road_decoder <rom_path> -l

# Decode track 0 (default)
./road_decoder <rom_path> -t 0

# Decode strip data tables
./road_decoder <rom_path> -t 0 -s

# Raw dump of all 384 words
./road_decoder <rom_path> -t 0 -r
```

### track_viewer_3d (OpenGL viewer)

```bash
# Launch with OpenGL (interactive)
./track_viewer_3d <rom_path>

# Launch headless (text output)
./track_viewer_3d <rom_path> -n

# Start on specific track
./track_viewer_3d <rom_path> -t 2

# Show specific strip range
./track_viewer_3d <rom_path> -s 50 -c 32
```

## Build

```bash
cd native
gcc -o road_decoder -Wall -Wextra road_decoder.c
gcc -o track_viewer_3d -DUSE_OPENGL track_viewer_3d.c -lGL -lGLU -lglut -lm
```

## Track Viewer Controls

The 3D track viewer supports these keyboard controls:

| Key | Action |
|-----|--------|
| `<` / `>` | Switch between tracks |
| `0` - `9` | Quick track selection |
| `[` / `]` | Scroll through strips |
| `W` / `S` | Zoom in / out |
| `A` / `D` | Rotate camera left / right |
| `R` / `F` | Raise / lower camera |
| `O` | Toggle auto-rotate |
| `V` | Toggle strip view count (32/64) |
| `ESC` | Quit |

## Status

The road_expand_bits format is now decoded. The tunnel mouth data at `0x49348`
has a repeating pattern but its detailed field semantics remain unknown.