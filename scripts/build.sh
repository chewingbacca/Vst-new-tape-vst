#!/usr/bin/env bash
# One-command build: configures and builds the plugin (VST3 + Standalone, AU on macOS),
# the offline renderer and the unit tests into ./build.
#
#   ./scripts/build.sh                 # Release build
#   JUCE_PATH=/path/to/JUCE ./scripts/build.sh   # use a local JUCE checkout instead of downloading
#   VERDANT_INSTALL=1 ./scripts/build.sh         # also copy the plugin to your user plug-in folder
set -euo pipefail
cd "$(dirname "$0")/.."

GEN=()
if command -v ninja >/dev/null 2>&1; then GEN=(-G Ninja); fi

ARGS=(-DCMAKE_BUILD_TYPE="${BUILD_TYPE:-Release}")
[ -n "${JUCE_PATH:-}" ] && ARGS+=(-DFETCHCONTENT_SOURCE_DIR_JUCE="$JUCE_PATH")
[ -n "${VERDANT_INSTALL:-}" ] && ARGS+=(-DVERDANT_COPY_PLUGIN=ON)

cmake -B build "${GEN[@]}" "${ARGS[@]}"
cmake --build build --parallel --config "${BUILD_TYPE:-Release}"

echo
echo "Built:"
find build/Verdant_artefacts -maxdepth 3 \( -name "*.vst3" -o -name "*.component" -o -name "Verdant" -o -name "Verdant.app" -o -name "Verdant.exe" \) 2>/dev/null | sed 's/^/  /'
