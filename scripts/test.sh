#!/usr/bin/env bash
# Runs every automated check: unit tests, the offline render suite and (if available) pluginval.
#
#   ./scripts/test.sh            # full suite (about 4 minutes)
#   QUICK=1 ./scripts/test.sh    # reduced render matrix (about 1 minute)
#   PLUGINVAL=/path/to/pluginval ./scripts/test.sh
set -euo pipefail
cd "$(dirname "$0")/.."

[ -x build/tests/verdant_tests ] || ./scripts/build.sh

echo "== unit tests"
./build/tests/verdant_tests

echo "== render suite"
if [ -n "${QUICK:-}" ]; then
    ./build/tests/verdant_render --suite --quick --out build/renders
else
    ./build/tests/verdant_render --suite --out build/renders
fi
if command -v python3 >/dev/null 2>&1 && python3 -c "import numpy, scipy" 2>/dev/null; then
    python3 tools/analyze.py report build/renders
fi

PV="${PLUGINVAL:-$(command -v pluginval || true)}"
VST3="$(find build/Verdant_artefacts -maxdepth 3 -name 'Verdant.vst3' | head -n 1)"
if [ -n "$PV" ] && [ -n "$VST3" ]; then
    echo "== pluginval (strictness 10)"
    RUN=("$PV" --strictness-level 10 --validate-in-process --timeout-ms 600000 "$VST3")
    if [ "$(uname)" = "Linux" ] && [ -z "${DISPLAY:-}" ] && command -v xvfb-run >/dev/null 2>&1; then
        xvfb-run -a "${RUN[@]}"
    else
        "${RUN[@]}"
    fi
else
    echo "== pluginval not found (set PLUGINVAL=/path/to/pluginval to include it)"
fi
echo "All checks passed."
