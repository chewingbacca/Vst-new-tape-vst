#!/usr/bin/env bash
# Renders every factory preset on three synthesised sources (synth phrase, plucked string,
# sung vowel) so you can audition the plugin without a DAW.
#
#   ./scripts/render-demos.sh [output-dir]     (default: build/demos; FLAC if ffmpeg is installed)
set -euo pipefail
cd "$(dirname "$0")/.."
OUT="${1:-build/demos}"
[ -x build/tests/verdant_render ] || ./scripts/build.sh
mkdir -p "$OUT"

n=$(./build/tests/verdant_render --list-presets | wc -l)
for ((p = 0; p < n; p++)); do
    name=$(./build/tests/verdant_render --list-presets | sed -n "$((p + 1))p" | cut -f1 | tr ' ' '_' | tr -cd '[:alnum:]_-')
    for sig in phrase pluck voice; do
        base=$(printf "%02d_%s__%s" "$((p + 1))" "$name" "$sig")
        ./build/tests/verdant_render --preset "$p" --signal "$sig" --seconds 24 --sustain --out "$OUT" --name "$base" > /dev/null
        if command -v ffmpeg >/dev/null 2>&1; then
            ffmpeg -loglevel error -y -i "$OUT/$base.wav" -sample_fmt s16 "$OUT/$base.flac" && rm "$OUT/$base.wav"
        fi
    done
    echo "rendered $name"
done
echo "Demos are in $OUT"
