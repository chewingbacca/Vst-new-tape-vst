# Verdant — working notes

Living document so work survives a context reset. Newest status at the top.

## Status
- [x] Toolchain: Ubuntu 24.04, GCC 13, CMake 3.28, Ninja. JUCE 9.0.5 (FetchContent, pinned tag).
      Local checkout at /opt/deps/JUCE (use `-DFETCHCONTENT_SOURCE_DIR_JUCE=/opt/deps/JUCE`).
      pluginval v1.0.4 built from source at /opt/deps/pluginval/build/pluginval_artefacts/Release/pluginval
      (release binaries are blocked by the network policy here).
- [x] Pass-through plugin (VST3 + Standalone), parameter table, look-ahead limiter, bypass, latency.
- [ ] Modulation sources
- [ ] Tape
- [ ] Grain cloud
- [ ] Arp / shimmer (+ pitch tracker)
- [ ] Chorus
- [ ] Reverb
- [ ] Macros + factory presets
- [ ] Custom UI
- [ ] README, final verification

## Architecture
- `src/dsp/` — JUCE-free C++20 engine (`verdant_dsp` static lib). One source of truth for
  parameters (`Params.h`) and presets (`Presets.cpp`).
- `src/plugin/` — JUCE wrapper: APVTS built from the table, preset management, editor.
- `tests/` — `verdant_render` (offline renderer + verification suite) and `verdant_tests` (unit tests).
- `tools/` — Python analysis (spectrograms, report).

## Engine conventions
- Control rate: fixed internal grid of 32 samples (`Engine::kChunk`), independent of host block
  size, so output is identical for any buffer size. Smoothed values tick once per chunk; gains
  are linearly ramped per sample inside the chunk.
- Latency = limiter look-ahead only (2 ms, W-1 samples). The limiter is the last stage, after the
  dry/wet mix, so nothing leaves the plugin above -0.3 dBFS. Bypass crossfades to a
  latency-matched clean path.
- Limiter proof sketch: g_min is a sliding minimum over W samples, g_box a moving average over W
  samples of g_min, audio delayed D = W-1. Every term g_min[n-j], j in 0..W-1, covers sample n-D,
  so g_box[n] <= g_raw[n-D] and |out| <= ceiling.
- Engine::process ends with a non-finite scan; on NaN/Inf it resets all state and outputs silence.
