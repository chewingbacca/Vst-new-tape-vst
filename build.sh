#!/usr/bin/env bash
# One-command build: ./build.sh [install] [notest] [pluginval]
set -euo pipefail
cd "$(dirname "$0")"
ARGS=()
[[ -n "${SPLINTER_JUCE_PATH:-}" ]] && ARGS+=("-DSPLINTER_JUCE_PATH=${SPLINTER_JUCE_PATH}")
[[ " $* " == *" install "* ]] && ARGS+=("-DSPLINTER_COPY_AFTER_BUILD=ON")
GEN=()
command -v ninja >/dev/null 2>&1 && GEN=(-G Ninja)
cmake -S . -B build "${GEN[@]}" -DCMAKE_BUILD_TYPE=Release "${ARGS[@]}"
cmake --build build --config Release --parallel
if [[ " $* " != *" notest "* ]]; then
    ctest --test-dir build --output-on-failure -C Release
fi
if [[ " $* " == *" pluginval "* ]]; then
    scripts/run_pluginval.sh 10
fi
echo "Built: build/Splinter_artefacts/Release/{VST3,Standalone}"
