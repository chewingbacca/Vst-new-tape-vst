#!/usr/bin/env bash
# Build dependencies for Debian / Ubuntu (JUCE 9 on Linux).
set -euo pipefail
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build pkg-config git \
    libasound2-dev libx11-dev libxext-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
    libxcomposite-dev libxrender-dev libfreetype-dev libfontconfig1-dev libgl1-mesa-dev libegl1-mesa-dev
