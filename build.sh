#!/usr/bin/env bash
# Configure and build the native shell. Run ./run.sh afterwards to launch it.
#
# The GSP RTL core is ON by default: it is what makes the live window show the
# TMS34010's own output rather than just the MC68010's upload. It needs
# Verilator, which is optional -- without it the build still succeeds and the
# shell falls back to the native software GSP path. Pass --no-rtl for that
# deliberately (and to keep the ROM-free build free of a simulator dependency).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${NATIVE_BUILD_DIR:-$ROOT/build/native}"
JOBS="${NATIVE_JOBS:-}"
WANT_RTL="${WANT_GSP_RTL:-1}"

for arg in "$@"; do
    case "$arg" in
        --no-rtl) WANT_RTL=0 ;;
        -h|--help)
            cat <<EOF
Usage: ./build.sh [--no-rtl]

  --no-rtl   Build without the Verilated TMS34010 core. The live window then
             shows only the MC68010's VRAM writes, never the GSP's.
EOF
            exit 0 ;;
        *) printf 'build.sh: unknown option %s\n' "$arg" >&2; exit 2 ;;
    esac
done

if [[ -z "$JOBS" ]]; then
    if command -v nproc >/dev/null 2>&1; then
        JOBS="$(nproc)"
    else
        JOBS=2
    fi
fi

if ! command -v cmake >/dev/null 2>&1; then
    printf 'cmake is required to build stun-runner.\n' >&2
    exit 127
fi

# Put the pinned Verilator on PATH for the configure step. CMake already has a
# HINT for it, but Verilator is a multi-part install (bin/ plus its share/ and
# include/ trees) and it resolves its own siblings from argv[0]; having its bin
# directory first removes a class of "found it, then could not run it".
VERILATOR_HINTS=(
    "$HOME/.local/share/verilator-5.052/bin"
    "$HOME/opt/verilator/bin"
    "$HOME/.local/bin"
)
for d in "${VERILATOR_HINTS[@]}"; do
    if [[ -x "$d/verilator" ]]; then
        export PATH="$d:$PATH"
        break
    fi
done

have_verilator=0
command -v verilator >/dev/null 2>&1 && have_verilator=1
if [[ "$WANT_RTL" == 1 && "$have_verilator" == 0 ]]; then
    printf 'build.sh: GSP RTL requested but verilator is not on PATH.\n' >&2
    printf '  Looked in: %s\n' "${VERILATOR_HINTS[*]}" >&2
    printf '  The build will continue WITHOUT the TMS34010 core, and the live\n' >&2
    printf '  window will show no GSP output. Use --no-rtl to accept that.\n' >&2
    WANT_RTL=0
fi

printf 'Configuring native shell in %s\n' "$BUILD_DIR"
printf '  GSP RTL core: %s\n' "$([[ "$WANT_RTL" == 1 ]] && echo ON || echo OFF)"
cmake -S "$ROOT/native" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}" \
    -DSTUNRUN_GSP_RTL="$WANT_RTL"
if [[ "$WANT_RTL" == 1 ]]; then
    cmake --build "$BUILD_DIR" --target stunrun-native-shell stunrun-gsp-replay --parallel "$JOBS"
else
    cmake --build "$BUILD_DIR" --target stunrun-native-shell --parallel "$JOBS"
fi

# Verify the core is actually IN the binary. Configuring -DSTUNRUN_GSP_RTL=1
# and failing to link it is not a build failure -- the shell compiles fine
# without it -- so the only way to know is to look. run.sh does the same check
# before launching, because "it built but silently fell back" is exactly the
# failure that is invisible until someone wonders why the window is empty.
if grep -qa 'gsp-rtl=bound' "$BUILD_DIR/stunrun-native-shell" 2>/dev/null; then
    printf 'GSP RTL core: linked into the shell\n'
else
    printf 'GSP RTL core: NOT linked -- the live window will show no GSP output\n' >&2
fi
