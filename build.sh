#!/usr/bin/env bash
# One-command build + test for Swoon.
#
#   ./build.sh              configure, build everything (Release), run unit tests
#   ./build.sh --full       ... then also run the offline render suite, click tests and pluginval
#   ./build.sh --install    ... and copy the plugin to the user plugin folder
#
# Environment:
#   SWOON_JUCE_DIR   use a local JUCE checkout instead of fetching the pinned tag
#   PLUGINVAL        path to a pluginval binary (only needed for --full; skipped if absent)
set -euo pipefail
cd "$(dirname "$0")"

FULL=0
INSTALL=OFF
for arg in "$@"; do
    case "$arg" in
        --full) FULL=1 ;;
        --install) INSTALL=ON ;;
        *) echo "unknown option: $arg" >&2; exit 2 ;;
    esac
done

GEN=()
command -v ninja >/dev/null 2>&1 && GEN=(-G Ninja)
EXTRA=()
[ -n "${SWOON_JUCE_DIR:-}" ] && EXTRA+=("-DFETCHCONTENT_SOURCE_DIR_JUCE=${SWOON_JUCE_DIR}")
command -v ccache >/dev/null 2>&1 && EXTRA+=(-DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache)

cmake -S . -B build "${GEN[@]}" -DCMAKE_BUILD_TYPE=Release -DSWOON_COPY_PLUGIN=${INSTALL} "${EXTRA[@]}"
cmake --build build --config Release --parallel
ctest --test-dir build --output-on-failure -C Release

echo
echo "Built:"
find build/Swoon_artefacts -maxdepth 3 \( -name 'Swoon.vst3' -o -name 'Swoon.component' -o -name 'Swoon' -o -name 'Swoon.app' -o -name 'Swoon.exe' \) 2>/dev/null | sed 's/^/  /'

if [ "$FULL" = 1 ]; then
    python3 tools/run_suite.py --out out/suite
fi
