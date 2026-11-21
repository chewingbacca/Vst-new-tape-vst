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
- [ ] DSP core, sampler, glitch engine, harmony, output
- [ ] UI, presets, demo loops
- [ ] Harness, unit tests, pluginval
- [ ] README

## Research notes (reference products)
Gross Beat: 2-bar buffer, 36+36 slots, offset-behind-realtime curve, trig sync,
momentary/hold modes. Glitch 2: lane per effect, weighted random, scenes per
MIDI note. Effectrix: 14 lanes x 32 steps, 12 sub-patterns on one octave.
Stutter Edit: rate ramps between note values, gate width, grid-quantised
triggering. HalfTime: 2x/1.5x/4x, ~4.4 ms default smoothing fade.
