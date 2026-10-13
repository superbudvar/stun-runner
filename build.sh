#!/usr/bin/env bash
# Configure and build the native shell. Run ./run.sh afterwards to launch it.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${NATIVE_BUILD_DIR:-$ROOT/build/native}"
JOBS="${BUILD_JOBS:-}"

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

printf 'Configuring native shell in %s\n' "$BUILD_DIR"
cmake -S "$ROOT/native" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}"
cmake --build "$BUILD_DIR" --target stunrun-native-shell --parallel "$JOBS"
