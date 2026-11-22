# Writing engines and effects for Automata

Read these first (they are short):

* `DESIGN.md` — architecture and threading model
* `core/model/Params.h`, `core/model/ParamTables.cpp` — every parameter's key, range, curve and meaning
* `core/voices/EngineBase.h` (engines) or `core/fx/Effect.h` (effects) — the interface you implement
* `core/voices/DrumEngine.cpp` — a complete reference engine
* `core/dsp/*.h` — shared primitives (SVF, ladder, biquad, envelopes, delay line, FFT, oversampler, polyBLEP)

## Hard rules

1. **Real-time safe.** `prepare()` may allocate. Nothing else may allocate, lock, do I/O, or call
   anything unbounded. No `std::vector::push_back`, no `new`, no `printf`, no exceptions.
2. **Deterministic.** No `rand()`, no clocks, no statics that carry state between instances. Random
   numbers come from `atm::Noise` / `atm::Rng` / the hash functions in `core/util/Rng.h`, seeded from
   `EngineNote::seed` (engines) or a fixed constant re-seeded in `reset()` (effects).
   An engine must reset **all** of its state on a non-legato `noteOn` (phases, filters, DC blockers,
   delay lines it owns, envelopes), so the output of a note never depends on what was played before
   or on how the host split its buffers. Rendering the same thing with slice sizes 1, 7 or 32 must
   give the same samples.
3. **Any slice size from 1 to 32 samples**, any sample rate from 44.1 kHz to 192 kHz. Scale every
   time constant and delay length by the sample rate. Clamp frequencies below Nyquist.
4. **No clicks.** Parameters arrive at control rate (once per slice, at most every 16 samples) and may
   jump (parameter locks). Smooth anything that would click: gains, delay times, mix levels. Notes
   start from silence without a step. Switches (types, modes) may change mid-stream without NaNs.
5. **Never output NaN/Inf**, whatever the parameters. Guard feedback paths (clamp or saturate), keep
   filter states sane, flush denormals in recursive paths (`atm::undenorm`). All 0..1 normalised
   inputs may be exactly 0 or exactly 1.
6. **Levels.** An engine at velocity 1 with default parameters should peak around -3 dBFS (+-3 dB)
   and stay below about +6 dBFS at the extremes of its parameters. An effect at default settings
   should be roughly unity loudness on typical material.
7. **CPU** at 48 kHz on this machine, measured with the tools below: an engine voice should cost about
   0.3-0.8 % of real time (granular cloud: up to ~3 %), an effect 0.2-1 % (reverbs up to ~2.5 %, the
   spectral effect up to ~3 %).
8. **Original work.** Write the DSP yourself. Well-known published algorithms and topologies are fine
   (credit them in a comment at the top of the file: author/paper/licence). Do not copy code.
9. Only touch the files you were assigned (plus a new test file). Do **not** run git commands, do not
   edit `ParamTables.cpp` rows that are not yours, and do not use the shared `build-core/` directory.
   If you need a parameter changed (range, default, a spare slot), change only your own rows and say
   so in your report.

Parameters are normalised 0..1. Map them with the descriptor table:
`kDrumParams[Decay].toPhys (e[Decay])`, `.toIndex()` for discrete ones. Mapping costs a `pow` for
exponential curves, so do it in `control()` / `setParams()`, never per sample.

## Building and measuring your work

A snapshot of the rest of the core is prebuilt at `/opt/deps/snap/libautomata_core.a`. Compile your
file(s) together with a tool and link the snapshot; your object file takes precedence over the stub
inside the archive. Use your own output directory under your scratch dir, never `build-core/`.

```bash
OUT=<your scratch dir>
# an engine
g++ -std=c++20 -O2 -Wall -Wextra -Wno-unused-parameter -Icore -Itools/core \
    core/voices/FmEngine.cpp tools/core/voice_render.cpp /opt/deps/snap/libautomata_core.a -o $OUT/voice_render
$OUT/voice_render fm $OUT/a.wav note=48 dur=2 gate=0.5 algo=2 ratio2=3.5 level2=0.8 cutoff=4000 flttype=LP24

# an effect
g++ -std=c++20 -O2 -Wall -Wextra -Wno-unused-parameter -Icore -Itools/core \
    core/fx/DelayFx.cpp tools/core/fx_render.cpp /opt/deps/snap/libautomata_core.a -o $OUT/fx_render
$OUT/fx_render delay $OUT/b.wav in=drums bpm=120 dur=6 feedback=0.6 time=0.5 'feedback@2:3=0.95'

# measurements and pictures (you cannot hear: measure and look)
python3 tools/py/analyze.py $OUT/a.wav            # levels, DC, click candidates, spectrum, envelope
python3 tools/py/specgram.py $OUT/a.wav $OUT/a.png [t0 t1]   # then view the PNG with the Read tool

# unit tests: put yours in tests/test_<name>.cpp using tests/TestFramework.h
g++ -std=c++20 -O2 -Icore -Itests tests/main.cpp tests/test_<name>.cpp <your .cpp files> \
    /opt/deps/snap/libautomata_core.a -o $OUT/tests && $OUT/tests
```

`voice_render` runs the engine inside a full voice (amp envelope, filter, drive). Useful options:
`retrig=0.11` retriggers every 0.11 s (click testing), `notes=48,51,55 slide=1` tests legato,
`sample=test` loads a synthetic drum break, `block=1|7|32` changes the slice size (outputs must match),
`sr=44100|96000|192000`. `fx_render` inputs: `in=drums|noise|sine|saw|chord|impulse|sweep|file.wav`;
`key@t0:t1=value` overrides a parameter between two times, like a parameter lock would.

Judge the sound by its numbers and its spectrogram: does the pitch envelope look right, are the
partials where the model says they should be, is aliasing visible as mirrored partials, does the
decay match the parameter, is the noise floor clean, do parameter sweeps look continuous?

## What to hand back

A short report: what you built (files), the signal flow of each engine/effect in two or three lines,
any parameter-table changes, measured CPU and levels, what you verified (block-size independence,
sample rates, extreme settings, retrigger clicks), and anything you are unsure about or would tune
with ears.
