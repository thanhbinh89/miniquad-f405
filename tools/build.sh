#!/usr/bin/env bash
# Build everything on Linux / macOS / WSL.
#
#   tools/build.sh            firmware (release, board v2 / USB-C) + host tests
#   tools/build.sh debug      firmware with -Og
#   tools/build.sh v1         firmware for the first board (UART J2)
#   tools/build.sh all        v2 release + debug + v1 + WebAssembly emulator + web tests
#
# Needs: cmake >= 3.21, make, python3, arm-none-eabi-gcc (+ newlib),
#        gcc for the host tests; clang + wasi-libc and node >= 18 for "all".
set -euo pipefail
cd "$(dirname "$0")/.."
jobs=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
what=${1:-release}

fw() {
  cmake --preset "$1" >/dev/null
  cmake --build --preset "$1" -j"$jobs"
}

case "$what" in
  release|debug) fw "$what" ;;
  v1) fw release-v1 ;;
  all) fw release; fw debug; fw release-v1 ;;
  *) echo "usage: $0 [release|debug|v1|all]"; exit 2 ;;
esac

cmake --preset host-tests >/dev/null
cmake --build --preset host-tests -j"$jobs"
ctest --preset host-tests

if [ "$what" = all ]; then
  cmake --preset wasm-emu >/dev/null
  cmake --build --preset wasm-emu -j"$jobs"
  (cd web && npm test)
fi
echo "images: build/release/out (v2), build/release-v1/out (v1)"
