#!/usr/bin/env bash
# Fast inner loop: build and run only the JUCE-free DSP tests.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build-dsp
g++ -std=c++20 -O2 -g -Wall -Wextra -Wno-sign-compare -ISource -Itests Source/dsp/*.cpp tests/dsp_tests.cpp -o build-dsp/dsp_tests
./build-dsp/dsp_tests "$@"
