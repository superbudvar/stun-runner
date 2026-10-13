#!/usr/bin/env bash
# Launch the live native MC68010/GSP scanout windows; build on first use.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${NATIVE_BUILD_DIR:-$ROOT/build/native}"
ROM_DIR="${STUNRUN_STUNRUN_DIR:-$ROOT/roms/stunrun}"
BIN="$BUILD_DIR/stunrun-native-shell"

show_help() {
    cat <<EOF
Usage: run.sh [OPTIONS]

Launch the native STUN Runner emulation with live MC68010 + GSP video output.

Options:
  -h, --help     Show this help message

Key environment variables (set as VAR=value before the command, or export):

  STUNRUN_IN0=<hex>      IN0 port value (default: 0xE7)
                           Bit 5 (0x20) is active-low TEST SW
                           0xC7 = self-test switch ON; coin inputs idle
                           0xE7 = self-test switch OFF; coin inputs idle

  STUNRUN_MC68010=0|1    Enable MC68010 emulation (default: 1)
  STUNRUN_LIVE_VIDEO=0|1 Enable video output (default: 1)
  STUNRUN_FRAMES=N        Frame count (0 = unlimited, default: 0)
  STUNRUN_TRACE=N         Trace N instructions from the terminal PC
  STUNRUN_TRACE_WINDOW=L:H
                           Trace instructions in the address range [L,H)

Examples:

  # Service mode (self-test screen):
  STUNRUN_IN0=0xC7 ./run.sh

  # Headless test (no video, 600 frames):
  STUNRUN_LIVE_VIDEO=0 STUNRUN_FRAMES=600 ./run.sh

  # Service-mode boot trace around the text routine:
  STUNRUN_IN0=0xC7 STUNRUN_TRACE_WINDOW=0x25e8:0x26a0 ./run.sh

EOF
}

# Parse arguments
for arg in "$@"; do
    case $arg in
        -h|--help) show_help; exit 0 ;;
    esac
done

if [[ ! -d "$ROM_DIR" ]]; then
    printf 'ROM directory not found: %s\n' "$ROM_DIR" >&2
    printf 'Set STUNRUN_STUNRUN_DIR to the directory containing the STUN Runner ROM chips.\n' >&2
    exit 2
fi

if [[ ! -x "$BIN" ]]; then
    "$ROOT/build.sh"
fi

export STUNRUN_STUNRUN_DIR="$ROM_DIR"
export STUNRUN_MC68010="${STUNRUN_MC68010:-1}"
export STUNRUN_LIVE_VIDEO="${STUNRUN_LIVE_VIDEO:-1}"
export STUNRUN_LIVE_VIDEO_FPS="${STUNRUN_LIVE_VIDEO_FPS:-30}"
export STUNRUN_VIDEO_BACKEND="${STUNRUN_VIDEO_BACKEND:-sdl_opengl}"
export STUNRUN_AUDIO_BACKEND="${STUNRUN_AUDIO_BACKEND:-null}"
export STUNRUN_FRAMES="${STUNRUN_FRAMES:-0}"

exec "$BIN" "$@"
