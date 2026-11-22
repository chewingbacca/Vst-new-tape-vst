# Automata — working notes

Keep this file current: it is how work survives a context reset.

## Layout
- `core/` JUCE-free library (namespace `atm`): model, seq, gen, dsp, voices, fx, mod, engine
- `plugin/` JUCE processor + editor
- `tests/` core unit tests (`automata_tests`), `tools/core/` core-only tools (`voice_render`, `fx_render`)
- `tools/py/` analysis scripts (`analyze.py`, `specgram.py`)
- `docs/dsp-authoring.md` rules for engines/effects (also the brief given to sub-agents)
- JUCE 9.0.6 is at `/opt/deps/JUCE` on the dev machine (`AUTOMATA_JUCE_DIR`), otherwise fetched by CMake.

## Build
- Full: `./build.sh` (plugin + tests). Core only: `cmake -S . -B build-core -G Ninja -DAUTOMATA_BUILD_PLUGIN=OFF && cmake --build build-core`
- Tests: `build-core/tests/automata_tests [filter]`

## Decisions
- JUCE 9.0.6 (latest stable tag at the time). AGPLv3 unless a JUCE licence is held.
- Core is JUCE-free; all params normalised 0..1 with descriptor tables in `core/model/ParamTables.cpp`.
- Sequencer is a pure function of song position (no running state) — see DESIGN.md §4.
- Event at beat t plays on sample ceil(t*spb); a block owns beats (b0-bps, b0+(n-1)bps]. Verified sample-exact.
- Control rate: 16 samples on an absolute counter; slices split at events, so output is block-size independent.
- Step pitch on scale-quantised tracks = scale degrees (works for Scala too).
- Locks: 12 lanes per track per pattern; hold until the next step starts.
- Voices: 4 instances per track; mono = kill previous with 3 ms fade; poly = 3 sounding + 1 spare.
- Main out = full mix; track buses carry post-insert track signal; `directOut` removes a track from the main mix.
- Common track tune key is `coarse` (engine keys like drum `tune` take precedence in presets).

## Status
- [x] Design doc, CMake, pass-through build
- [x] Core: params, model, sequencer evaluation, generators, drum engine, voice, audio engine, timing tests
- [ ] Engines: FM, sampler, granular, resonator, synth (stubs in place)
- [ ] Effects (16 stubs in place), glitcher exists
- [ ] Plugin processor, state, sample pool, factory samples
- [ ] Offline host harness, pluginval
- [ ] UI
- [ ] Presets (150 voice, 40 kits), New Idea generator
- [ ] README

## Next
See status list, top to bottom.
