#!/usr/bin/env bash
# run_oracle.sh — Run MAME oracle for STUN Runner frame capture
#
# Usage:
#   cd /path/to/stun-runner
#   ./mame_harness/run_oracle.sh [--frames N] [--outdir DIR]
#
# Prerequisites:
#   1. MAME binary built at: stun-runner/mame-ref/mame
#   2. STUN Runner ROMs at: stun-runner/roms/stunrun/
#   3. Python 3 with access to system lua (MAME builtin)
#
# Output (to --outdir or ./oracle_dumps/):
#   vram.bin          — 1 MB GSP VRAM dump
#   palette_low.bin   — 512 bytes (256 × 16-bit R/G)
#   palette_high.bin  — 256 bytes (256 × 8-bit B)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
MAME_PATH="${PROJECT_ROOT}/mame-ref/mame"
ROMS_DIR="${PROJECT_ROOT}/roms"
OUTDIR="${PROJECT_ROOT}/oracle_dumps"
FRAMES=600

while [[ $# -gt 0 ]]; do
  case $1 in
    --frames)  FRAMES="$2"; shift 2 ;;
    --outdir)  OUTDIR="$2"; shift 2 ;;
    *)         echo "unknown arg: $1"; exit 1 ;;
  esac
done

echo "=== MAME Oracle for STUN Runner ==="
echo "  MAME:     $MAME_PATH"
echo "  ROMs:     $ROMS_DIR"
echo "  Outdir:   $OUTDIR"
echo "  Frames:   $FRAMES"
echo ""

if [[ ! -x "$MAME_PATH" ]]; then
  echo "ERROR: MAME not found at $MAME_PATH"
  echo "  Build with: cd mame-ref && make OSD=sdl -j$(nproc)"
  exit 1
fi

if [[ ! -d "$ROMS_DIR/stunrun" ]]; then
  echo "ERROR: STUN Runner ROM set not found at $ROMS_DIR/stunrun"
  exit 1
fi

mkdir -p "$OUTDIR"

python3 "$SCRIPT_DIR/mame_oracle.py" \
  --mame "$MAME_PATH" \
  --roms "$ROMS_DIR" \
  --outdir "$OUTDIR" \
  --frames "$FRAMES"

echo ""
echo "=== Oracle capture complete ==="
ls -lh "$OUTDIR/"
