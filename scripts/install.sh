#!/usr/bin/env bash
# Copies the built plugin into the per-user plug-in folders (Linux / macOS).
set -euo pipefail
cd "$(dirname "$0")/.."
VST3="$(find build/Verdant_artefacts -maxdepth 3 -name 'Verdant.vst3' | head -n 1)"
[ -n "$VST3" ] || { echo "Build first: ./scripts/build.sh"; exit 1; }

case "$(uname)" in
    Darwin)
        mkdir -p "$HOME/Library/Audio/Plug-Ins/VST3" "$HOME/Library/Audio/Plug-Ins/Components"
        rm -rf "$HOME/Library/Audio/Plug-Ins/VST3/Verdant.vst3"
        cp -R "$VST3" "$HOME/Library/Audio/Plug-Ins/VST3/"
        AU="$(find build/Verdant_artefacts -maxdepth 3 -name 'Verdant.component' | head -n 1)"
        if [ -n "$AU" ]; then
            rm -rf "$HOME/Library/Audio/Plug-Ins/Components/Verdant.component"
            cp -R "$AU" "$HOME/Library/Audio/Plug-Ins/Components/"
        fi
        echo "Installed to ~/Library/Audio/Plug-Ins/{VST3,Components}" ;;
    *)
        mkdir -p "$HOME/.vst3"
        rm -rf "$HOME/.vst3/Verdant.vst3"
        cp -R "$VST3" "$HOME/.vst3/"
        echo "Installed to ~/.vst3/Verdant.vst3" ;;
esac
