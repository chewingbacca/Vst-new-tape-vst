# Splinter — working notes

Survives context resets: read this first. Update "Status" as steps land.

## Environment
- Ubuntu 24.04, g++ 13, cmake 3.28, ninja. JUCE 9.0.6 (FetchContent, or
  `SPLINTER_JUCE_PATH=/home/user/deps/JUCE ./build.sh` to reuse a checkout).
- apt deps: libasound2-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev
  libxext-dev libxcomposite-dev libxi-dev libxrender-dev libfreetype-dev
  libfontconfig1-dev libgl1-mesa-dev xvfb.
- Formats: VST3 + Standalone (AU added automatically on macOS).

## Architecture (decided)
All position effects are *time maps*. Timeline T is in beats (host ppq). The
engine computes, per sample, the source beat to read:

    pos = Chop(Curve(Pitch(Reverse(Stutter(TapeStop(T))))))

Each stage maps "time at its output" to "time at its input" as a pure function
(randomness = hash(seed, lane, absolute step index) so renders are repeatable).
A `Reader` turns the desired position into audio: it follows the target directly
(no integration => no drift) and, whenever the target is discontinuous, starts a
new tap and crossfades from the old one (which keeps extrapolating). Host
loops/seeks, slice jumps, stutters and slot switches all go through that one
click-free mechanism.

Sources (both addressed in beats):
- Loop: file / demo / captured audio, time-stretched offline (background
  thread) to host tempo; periodic. Re-rendered when tempo changes; meanwhile the
  old render is resampled so it stays beat-locked.
- Live: rolling ring buffer of the input (Gross Beat style); only the past is
  readable, so maps must be causal.

Signal flow:
    source -> Reader voices (loop voice + MIDI slice voices)
           -> Harmony (phase-vocoder voices, formant-preserving, scale-quantised)
           -> Crush / Comb / Ring / Filter / Freeze (step FX)
           -> Volume curve (gate)
           -> Delay throw / Reverb throw
           -> Output filter, tilt, width, dry/wet, limiter
Dry = unglitched source (live input, or plain loop playback), delayed by the
plugin latency (harmony FFT size + limiter lookahead), which is reported.

Time curve semantics follow Gross Beat (Y = offset behind realtime, top = live;
flat = normal speed, slope 1 = pause, steeper = reverse, step = jump).
12 time slots, 12 volume slots, 12 sequencer patterns; quantised switching.

Sequencer lanes: Chop, Stutter, Reverse, TapeStop, Pitch, Crush, Comb, Ring,
Filter, Freeze, Delay, Reverb, Harmony. Step = on + probability + variation.

Macros (0..1, 0.5 = "as programmed", above 0.5 injects musically weighted
generated events): Chop, Stutter, Warp, Crush; Harmony = voice level; Chaos =
seeded mutation of unlocked lanes; Mix.

MIDI: Slices mode (one slice per key from C1) or Patterns mode (octaves select
time / volume / sequence slots and momentary FX).

## Status
- [x] Pass-through plugin builds (VST3 + Standalone)
- [x] DSP core (Source/dsp, JUCE-free): Reader, Sequencer, time maps, Harmonizer,
      StepFx, Analysis, Stretch, DemoLoops, Engine
- [x] Plugin layer (Source/plugin): Parameters, LoopManager (background thread),
      PatternBank (+ standard curve library), Presets (43 factory), Processor, state
- [x] Unit tests (tests/dsp_tests.cpp, run with scripts/dsptest.sh) and render
      harness (tests/render_harness.cpp -> build/renders/*.wav + report.txt)
- [x] UI (Source/ui): Theme, Widgets, WaveformView, CurveEditor, StepGrid, Pages
      (Perform / Curves / Sequence / Harmony / Output), PluginEditor (scaled canvas)
- [x] pluginval strictness 10 passes (scripts/run_pluginval.sh), README written
- [x] tests/ui_snapshot.cpp renders the editor pages to PNG for review

## Lessons learned (keep these invariants)
- Every gain that moves must be C1 smooth: use `Fader` (ramp + 2 poles) or
  `LinearSmoother` (2 poles). A linear ramp corner or a one-pole step start
  measures as a tick on smooth material.
- Reader: per-tap gains are smooth ramps and are *normalised to sum to 1*;
  otherwise jumps closer than the fade time dip or kink. Starting from silence
  is instant; fade-ins belong to the caller (engine loopGain: instant when the
  start sample is ~silent, else 1.2 ms).
- A change of playback *speed* is treated like a jump (crossfade), not followed.
- All maps are evaluated on the delayed clock Td (= T - kReaderLookahead); post
  effects on Tp (= T - lookahead - harmoniser latency). T has a +1e-9 beat bias
  so grid decisions do not depend on host rounding (=> block-size independent).
- Output filters always run and are blended in; never switch a filter in/out.
- Harmoniser: pre-echo guard (output may not exceed 4x the aligned input
  envelope) took pre-echo on hits from -14 dB to -68 dB.
- Macro injection (macro > 0.5) ignores lane locks by design; Chaos respects them.
- Tests that need the loop: call LoopManager::waitUntilReady (2 worker passes).

## Measured (48 kHz unless noted)
- CPU: mean ~2 %, worst ~5 % of one core over 114 preset renders (512 block).
- Latency: 2160 samples @48k (40 reader + 2048 harmoniser + 72 limiter); null test exact.
- Timing: beats land within 1-2 samples (Tape/Beats stretch), 0.8 ms (Tonal).
- Harmony pitch: +-0.00 cents on sine tests at 44.1k and 96k.
- Click audit over all presets: worst score 4.3 (threshold 9; hard cut > 100).

## Research notes (reference products)
Gross Beat: 2-bar buffer, 36+36 slots, offset-behind-realtime curve, trig sync,
momentary/hold modes. Glitch 2: lane per effect, weighted random, scenes per
MIDI note. Effectrix: 14 lanes x 32 steps, 12 sub-patterns on one octave.
Stutter Edit: rate ramps between note values, gate width, grid-quantised
triggering. HalfTime: 2x/1.5x/4x, ~4.4 ms default smoothing fade.

## What I would do next
- Onset detection over-slices sustained polyphonic material (beating between
  partials passes the energy-rise gate): see Warm Keys demo loop (~96 onsets).
- Optional zero-latency mode (harmoniser off) for live slice playing.
- Scale-mode harmony follows a simple chroma-peak pitch tracker; a polyphonic
  tracker would give better chords on dense material.
- Tune factory presets by ear; all were authored blind and only checked by measurement.
- Build and validate on macOS (AU) and Windows; not done here.
