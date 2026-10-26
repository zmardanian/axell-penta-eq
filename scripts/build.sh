#!/usr/bin/env bash
# Configures and builds inside the distrobox. Usage: scripts/build.sh [Release|Debug]
set -euo pipefail
cd "$(dirname "$0")/.."
BOX=juce-build
CFG="${1:-Release}"
DIR="$PWD"

# an earlier manual build used Makefiles; switch cleanly to Ninja
if [ -f build/CMakeCache.txt ] && ! grep -q "CMAKE_GENERATOR:INTERNAL=Ninja" build/CMakeCache.txt; then
  rm -rf build
fi

distrobox enter "$BOX" -- bash -lc "cd '$DIR' && cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=$CFG && cmake --build build -j${JOBS:-6}"

BUNDLE="build/AxellPentaEQ_artefacts/$CFG/VST3/Axell Penta EQ.vst3"
[ -d "$BUNDLE" ] && echo "Built: $DIR/$BUNDLE" || { echo "Bundle not found: $BUNDLE"; exit 1; }
