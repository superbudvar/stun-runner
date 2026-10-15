#!/usr/bin/env bash
# Launch the live native MC68010/GSP scanout windows; build on first use.
#
#   ./run.sh              normal boot   (IN0=0xE7) -- this is the one that
#                                              runs the TMS34010 core
#   ./run.sh --service    self-test screen (IN0=0xC7)
#
# Note the asymmetry, because it is real and not a bug in this script: the
# self-test screen is drawn by the MC68010, and that ROM path never clears
# HSTCTL bit 15. The GSP is therefore never released and the core never runs, so
# --service shows no GSP output. See docs/mc68010-boot-findings.md.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${NATIVE_BUILD_DIR:-$ROOT/build/native}"
ROM_DIR="${STUNRUN_STUNRUN_DIR:-$ROOT/roms/stunrun}"
BIN="$BUILD_DIR/stunrun-native-shell"
REPLAY_DIR=""

MODE="normal"
WANT_RTL="${WANT_GSP_RTL:-1}"

show_help() {
    cat <<EOF
Usage: run.sh [--service] [--gsp-replay CAPTURE-DIR] [--no-rtl] [--frames N] [-h]

  --service     Boot to the self-test screen instead of normal play.
  --gsp-replay DIR  Replay an oracle GSP host-event capture (RTL required).
  --no-rtl      Do not require the Verilated TMS34010 core.
  --frames N    Stop after N frames (default 0 = run until closed).
  -h, --help    This text.

Environment:
  NATIVE_BUILD_DIR   Where the shell was built (default: build/native)
  STUNRUN_LIVE_VIDEO=0  Headless; same run, no window.
  STUNRUN_REPLAY_PNG     Final replay PNG (default: gsp-replay-frame.png).
  STUNRUN_REPLAY_OUTPUTS GSP output observation log (default: gsp-replay-outputs.log).
  STUNRUN_IN0        Overrides the port value chosen by --service.
  STUNRUN_FRAME_LOG=N   Print lit=/colours= every N frames. Use this to see
                      when a screen appears or vanishes -- the terminal
                      summary only reports the LAST frame, and the thing worth
                      watching here is the MC68010 -> GSP handoff, which are
                      two different authors drawing the same window.

Examples:
  ./run.sh                      # normal boot, GSP core running
  ./run.sh --service            # self-test screen (no GSP core output)
  STUNRUN_LIVE_VIDEO=0 STUNRUN_FRAMES=600 ./run.sh    # headless 600 frames

Note: --service does NOT show GSP output. That ROM path never releases the
GSP. That is a property of the game, not of this launcher.
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --service) MODE="service" ;;
        --gsp-replay) shift; REPLAY_DIR="${1:?--gsp-replay needs a capture directory}"; MODE="gsp-replay" ;;
        --no-rtl)  WANT_RTL=0 ;;
        --frames)  shift; STUNRUN_FRAMES="${1:?--frames needs a number}" ;;
        --frames=*) STUNRUN_FRAMES="${1#--frames=}" ;;
        -h|--help) show_help; exit 0 ;;
        *) printf 'run.sh: unknown option %s (try -h)\n' "$1" >&2; exit 2 ;;
    esac
    shift
done

if [[ "$MODE" == "gsp-replay" ]]; then
    BIN="$BUILD_DIR/stunrun-gsp-replay"
    if [[ ! -x "$BIN" ]]; then "$ROOT/build.sh"; fi
    if [[ ! -x "$BIN" ]]; then
        printf 'run.sh: GSP replay executable unavailable; build with Verilator and STUNRUN_GSP_RTL=1\n' >&2
        exit 3
    fi
    if [[ ! -f "$REPLAY_DIR/gsp-upload-events.txt" ]]; then
        printf 'run.sh: missing %s/gsp-upload-events.txt\n' "$REPLAY_DIR" >&2
        exit 2
    fi
    exec "$BIN" "$REPLAY_DIR"
fi

if [[ ! -d "$ROM_DIR" ]]; then
    printf 'ROM directory not found: %s\n' "$ROM_DIR" >&2
    printf 'Set STUNRUN_STUNRUN_DIR to the directory containing the ROM chips.\n' >&2
    exit 2
fi

# The GSP core is the point of the live window. If it is missing, say so BEFORE
# opening a window, because otherwise the launch succeeds and the user is left
# looking at an empty share with no explanation.
if [[ "$WANT_RTL" == 1 ]]; then
    if [[ ! -x "$BIN" ]]; then
        "$ROOT/build.sh"
    fi
    if ! grep -qa 'gsp-rtl=bound' "$BIN" 2>/dev/null; then
        printf 'run.sh: the TMS34010 GSP core is NOT in %s\n' "$BIN" >&2
        printf '  The window would open showing no GSP output at all.\n' >&2
        printf '  Rebuild with:  ./build.sh          (needs verilator on PATH)\n' >&2
        printf '  Or accept the native software path:  ./run.sh --no-rtl\n' >&2
        exit 3
    fi
elif [[ ! -x "$BIN" ]]; then
    WANT_RTL=0 "$ROOT/build.sh" --no-rtl
fi

if [[ "$MODE" == "service" ]]; then
    # Bit 5 (0x20) is the active-low TEST SW. 0xE7 has it clear, 0xC7 sets it.
    export STUNRUN_IN0="${STUNRUN_IN0:-0xC7}"
else
    export STUNRUN_IN0="${STUNRUN_IN0:-0xE7}"
fi

export STUNRUN_STUNRUN_DIR="$ROM_DIR"
export STUNRUN_MC68010="${STUNRUN_MC68010:-1}"
export STUNRUN_LIVE_VIDEO="${STUNRUN_LIVE_VIDEO:-1}"
export STUNRUN_LIVE_VIDEO_FPS="${STUNRUN_LIVE_VIDEO_FPS:-30}"
export STUNRUN_VIDEO_BACKEND="${STUNRUN_VIDEO_BACKEND:-sdl_opengl}"
export STUNRUN_AUDIO_BACKEND="${STUNRUN_AUDIO_BACKEND:-null}"
export STUNRUN_FRAMES="${STUNRUN_FRAMES:-0}"

printf 'run.sh: mode=%s IN0=%s frames=%s core=%s\n' \
    "$MODE" "$STUNRUN_IN0" "$STUNRUN_FRAMES" \
    "$([[ "$WANT_RTL" == 1 ]] && echo on || echo off)"

exec "$BIN" "$@"
