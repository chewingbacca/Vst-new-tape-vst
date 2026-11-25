#!/usr/bin/env bash
# Build Automata from a clean checkout:   ./build.sh [--test] [--pluginval] [--install] [--debug]
#
#   --test       run the unit tests and the plugin-level render tests afterwards
#   --pluginval  download pluginval (if needed) and validate the VST3 at strictness 10
#   --install    copy the VST3 (and AU on macOS) to the user plug-in folder
#   --debug      Debug build instead of Release
#
# JUCE is fetched automatically by CMake (tag pinned in CMakeLists.txt). To use a local
# checkout instead, set AUTOMATA_JUCE_DIR=/path/to/JUCE.
set -euo pipefail
cd "$(dirname "$0")"

CONFIG=Release; RUN_TESTS=0; RUN_PV=0; INSTALL=0
for a in "$@"; do
  case "$a" in
    --test) RUN_TESTS=1 ;;
    --pluginval) RUN_PV=1 ;;
    --install) INSTALL=1 ;;
    --debug) CONFIG=Debug ;;
    -h|--help) sed -n '2,12p' "$0"; exit 0 ;;
    *) echo "unknown option $a"; exit 2 ;;
  esac
done

GEN=()
command -v ninja >/dev/null 2>&1 && GEN=(-G Ninja)
LAUNCH=()
command -v ccache >/dev/null 2>&1 && LAUNCH=(-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
JOBS=$(getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

cmake -S . -B build "${GEN[@]}" "${LAUNCH[@]}" -DCMAKE_BUILD_TYPE=$CONFIG -DAUTOMATA_COPY_PLUGIN=$([ $INSTALL = 1 ] && echo ON || echo OFF)
cmake --build build --config $CONFIG -j "$JOBS"

ART=build/Automata_artefacts/$CONFIG
[ -d "$ART" ] || ART=build/Automata_artefacts
echo
echo "Built:"
ls -d "$ART"/VST3/*.vst3 "$ART"/Standalone/* "$ART"/AU/*.component 2>/dev/null || true

if [ $RUN_TESTS = 1 ]; then
  echo; echo "== unit tests"
  ./build/tests/automata_tests
  echo; echo "== render tests"
  ./build/tools/automata_render_tests --quick
fi

if [ $RUN_PV = 1 ]; then
  PV=./build/pluginval/pluginval
  case "$(uname -s)" in
    Darwin) PVZIP=pluginval_macOS.zip; PV=./build/pluginval/pluginval.app/Contents/MacOS/pluginval ;;
    Linux)  PVZIP=pluginval_Linux.zip ;;
    *)      PVZIP=pluginval_Windows.zip; PV=./build/pluginval/pluginval.exe ;;
  esac
  if [ ! -x "$PV" ]; then
    mkdir -p build/pluginval
    curl -sL -o build/pluginval/pv.zip "https://github.com/Tracktion/pluginval/releases/latest/download/$PVZIP"
    (cd build/pluginval && unzip -o -q pv.zip)
  fi
  RUNNER=()
  [ "$(uname -s)" = Linux ] && [ -z "${DISPLAY:-}" ] && command -v xvfb-run >/dev/null && RUNNER=(xvfb-run -a)
  "${RUNNER[@]}" "$PV" --strictness-level 10 --validate-in-process --timeout-ms 600000 "$ART/VST3/Automata.vst3"
fi
