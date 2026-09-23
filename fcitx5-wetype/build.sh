#!/usr/bin/env bash
# Build only. Installation is an explicit, separate cmake --install step.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${BUILD_DIR:-$HERE/build}"

cmake -S "$HERE" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}"
cmake --build "$BUILD_DIR" --parallel "${JOBS:-$(nproc)}"
echo "Build complete: $BUILD_DIR"
echo "Install with: sudo cmake --install '$BUILD_DIR' --prefix /usr"
