# Automata — working notes

Keep this file current: it is how work survives a context reset.

## Layout
- `core/` JUCE-free library (namespace `atm`): model, seq, gen, dsp, voices, fx, mod, engine, factory
- `plugin/` JUCE processor (`PluginProcessor`), editor (`PluginEditor`), drag-out export (`Export`), UI in `plugin/ui/`
- `tests/` core unit tests (`automata_tests [filter]`)
- `tools/core/` core-only bench tools (`voice_render`, `fx_render`, `core_smoke`)
- `tools/host/` tools around the real AudioProcessor: `automata_render` (render project/preset/idea to WAV),
  `automata_render_tests` (all kits + voice presets: NaN/clip/DC/clicks/silence/determinism/state), `automata_snapshot` (editor to PNG)
- `tools/py/` analysis (`analyze.py`, `specgram.py`, `wavio.py`)
- `docs/dsp-authoring.md` rules for engines/effects (also the brief for sub-agents)
- JUCE 9.0.6 at `/opt/deps/JUCE` on the dev machine (`AUTOMATA_JUCE_DIR`), otherwise fetched by CMake.
- pluginval 1.0.4 at `/opt/deps/pluginval` on the dev machine (`./build.sh --pluginval` downloads it elsewhere).

## Build / test
- `./build.sh [--test] [--pluginval] [--install]`
- Core only: `cmake -S . -B build-core -G Ninja -DAUTOMATA_BUILD_PLUGIN=OFF && cmake --build build-core && build-core/tests/automata_tests`
- Render tests: `build/tools/automata_render_tests [--quick] [--out dir]`
- Snapshot: `xvfb-run -a build/tools/automata_snapshot out.png --factory 9 --play 1 --track 4 --tab 1 [--lock 3] [--crop x,y,w,h] [--scale 2]`
- Render a preset: `build/tools/automata_render --factory "Gnat Swarm" --bars 8 --out x.wav` / `--idea drill:1234` / `--list-presets`

## Decisions
- JUCE 9.0.6 (latest stable tag). AGPLv3 unless a JUCE licence is held.
- Core is JUCE-free; all params normalised 0..1 with descriptor tables in `core/model/ParamTables.cpp`.
- Sequencer is a pure function of song position (no running state) — DESIGN.md §4.
- Event at beat t plays on sample ceil(t*spb); a block owns beats (b0-bps, b0+(n-1)bps]. MIDI-out test proves it exactly at 44.1–192 kHz and any block size.
- Control rate 16 samples on an absolute counter; slices split at events, control ticks and block ends. Every effect and engine must give identical output whatever the slice split (state changes only on the control grid). `ResonatorFx` used to free voices at the end of a process call: moved to setParams.
- Step pitch on scale-quantised tracks = scale degrees (Scala works the same way).
- Locks: 12 lanes per track per pattern; hold until the next step starts.
- Voices: 4 per track; mono = previous voice killed with a 3 ms fade; poly = 3 sounding + 1 spare.
- Main out = full mix; track buses carry post-insert track signal; `directOut` removes a track from the main mix.
- Common track tune key is `coarse` (engine keys like drum `tune` take precedence in presets).
- Factory samples are synthesised at run time (`core/factory/FactorySamples.cpp`), breaks rendered through the drum engine; pre-warmed on a background thread when the plug-in loads.
- Voice presets: text table `core/factory/VoicePresets.inc` (physical values, parameter keys). Kits: New Idea with fixed seed/tempo + overrides (`core/factory/Kits.cpp`).
- Stutter's first slot plays live while it records, so one-step locks need a slot shorter than the step: default length 1/32, New Idea uses 1/32–1/64.
- Compressor makeup default 6 dB (effects agent) so the default is near unity.

## Status
- [x] Design doc, CMake, pass-through build
- [x] Core: params, model, sequencer, generators, audio engine, timing tests (incl. exact MIDI-out scheduling)
- [x] Engines: drum, FM, sampler, granular, resonator, synth (engine verification/drum tuning pass by a sub-agent: see its report)
- [x] Effects: all 16, `tests/test_fx.cpp` (24 tests), bit-exact under any call split
- [x] Plugin processor: 9 output buses, sidechain in, MIDI in/out, host params, state with embedded FLAC samples
- [x] Session: undo/redo with transactions, sample pool, engine sync
- [x] UI: top bar, grid, visualizer, SOUND / STEPS / SEQ-GEN / MOD / FX / SONG-SETUP pages, keyboard, drag and drop, resizable
- [x] Presets: 160 voice presets, 40 kits, 20 factory samples, New Idea for 5 styles
- [x] Export: MIDI and WAV drag-out
- [x] Offline harness, render tests, snapshot tool, pluginval passes at strictness 10 (re-run after DSP changes)
- [x] README

## Known issues / next
- Kits peak at the limiter ceiling (rms around -12..-15 dB); acid kits lean on the limiter hardest. Gain staging could be gentler.
- GrainDelay costs 1.6 % at maximum density and size.
- Reverb RT60 measured at 1 kHz; broadband reads ~20 % short of the `decay` value (damping by design).
- Not verified by ear: everything. See README "Verification".
