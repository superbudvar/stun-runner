#!/usr/bin/env bash
# Launch the live native MC68010/GSP scanout windows; build on first use.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${NATIVE_BUILD_DIR:-$ROOT/build/native}"
ROM_DIR="${STUNRUN_STUNRUN_DIR:-$ROOT/roms/stunrun}"
BIN="$BUILD_DIR/stunrun-native-shell"

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
