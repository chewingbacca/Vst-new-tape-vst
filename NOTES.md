# Swoon — working notes

Living document so the work survives a context reset. Newest status at the top
of each section.

## Environment
- Ubuntu 24.04, g++ 13.3, CMake 3.28, Ninja. No macOS here, so no AU build
  (CMake adds AU automatically on Apple).
- JUCE 9.0.6 (latest stable tag at time of writing), fetched by CMake
  `FetchContent`. Local dev uses `-DFETCHCONTENT_SOURCE_DIR_JUCE=/root/deps/JUCE`.
- pluginval 1.0.4 binary at `/root/deps/pluginval/pluginval` (downloaded from
  the Tracktion GitHub release). Needs `xvfb-run` on this headless box.
- Python 3.13 + numpy/scipy/matplotlib/soundfile for analysis.

## Architecture
- `Source/dsp/` is plain C++20 with **no JUCE**: `swoon_dsp` static library.
  The plugin, the render harness (`swoon_render`) and unit tests
  (`swoon_tests`) all link it. All DSP is written from scratch.
- `Params.h` holds one X-macro table: id, name, module, range, default, skew,
  unit, choices. The APVTS layout, `ParamValues` (plain float array handed to
  the engine), factory presets and UI all derive from it.
- `Engine` (pimpl) = whole signal path. `process (L, R, n, ParamValues)`.
- `PluginProcessor` copies APVTS atomics into `ParamValues` per block, handles
  mono/stereo layouts, feeds `VizData` (lock-free meters + scope FIFO) to the UI.
- `PresetManager`: factory presets from `dsp/Presets.cpp` (plain data, so the
  harness can render them) + user presets as XML in the app-data folder.
  Presets never touch `bypass` or `inGain`.

## Signal path
Input gain/swell -> seven reorderable modules -> Tape -> Output (tilt, width,
limiter) -> global Mix against latency-compensated dry.

Reorderable (default order): Glide, Fuzz, Amp, Mod, Focus, Reverse, Wash.
Order = seven automatable int params (`ordX`, 1..7), resolved by stable sort so
any combination is valid. An order change fades the wet path out, swaps, fades in.

### Macros
Each macro is 0..1 with 0.5 = "module page settings exactly"; below pulls the
effect out (0 = none), above pushes it past the module settings.

## Research digest (subagent, web search summaries only — treat as ballpark)
- Loomer: fuzz Level/Fuzz/Filter + Flat/Full/Scoop; reverb Blend/Decay/Depth/
  Warmth + Focus/Reverse/Hall; order switch. Focus = dense reverb + two parallel
  delays 250/380 ms + 4-voice chorus. Reverse = delay line read at several
  points with rising volume, 150-500 ms, Depth = envelope-triggered pitch dip.
  Hall = long wash with octave regeneration in the feedback loop.
- FX500 "Soft Focus" (community-reported): delay 250 ms L / 380 ms R, ~72 %
  feedback, reverb ~3.4 s, symphonic chorus ~1.6 Hz, mid cut near 1 kHz.
- Loveless: SPX90 reverse-gate style program (an early-reflection pattern with
  an inverted envelope, not reversed audio), 100 % wet, tone turned down;
  Midiverb II reverse programs 150-500 ms; glide guitar = arm held while
  strumming, mostly a downward dip and return (~tens of cents).
- Big Muff: input boost ~20 dB, two clipping stages ~23-25 dB each with diode
  pairs in the feedback loop, band-limited roughly 55-94 Hz to 1.2-1.8 kHz;
  tone stack = LP 408 Hz blended against HP ~1.8 kHz, notch near 0.9-1 kHz;
  "flat mids" mod moves the HP down to ~720 Hz.
- Shimmer: pitch shifter + reverb in one loop; low-pass every pass, keep pitched
  loop gain well below unity, high diffusion to hide grain.

## Status
- [x] Toolchain, JUCE fetch, pass-through plugin builds (VST3 + Standalone),
      pluginval level 5 passes on the skeleton.
- [ ] DSP modules (one commit each)
- [ ] Offline harness + analysis
- [ ] Unit tests
- [ ] Custom UI
- [ ] Factory presets
- [ ] pluginval strict, README
