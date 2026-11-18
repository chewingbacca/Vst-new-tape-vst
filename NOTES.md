# Verdant — working notes

Living document so work survives a context reset. Status first, then design decisions.

## Status

- [x] Toolchain: Ubuntu 24.04, GCC 13, CMake 3.28, Ninja. JUCE 9.0.5 via FetchContent (pinned tag).
      Local checkout at `/opt/deps/JUCE` (configure with `-DFETCHCONTENT_SOURCE_DIR_JUCE=/opt/deps/JUCE`
      to skip the download). pluginval v1.0.4 built from source at
      `/opt/deps/pluginval/build/pluginval_artefacts/Release/pluginval` (release binaries are blocked
      by this sandbox's network policy; git clones of GitHub work).
- [x] Pass-through plugin (VST3 + Standalone, AU on macOS), parameter table, limiter, bypass, latency.
- [x] Modulation sources (4 drifting LFOs, envelope follower).
- [x] Tape (granular varispeed/stretch with WSOLA alignment, wow/flutter, ADAA saturation, age, stutter).
- [x] Grain cloud.
- [x] Arp / shimmer with YIN pitch tracking.
- [x] Chorus (smooth/glassy, 24 voices, multiband).
- [x] Reverb (16-line FDN, in-loop shimmer, freeze, infinite, energy governor).
- [x] Output (tilt, roots shelf, width, ducking, look-ahead limiter).
- [x] Macros, 28 factory presets.
- [x] Offline render harness + verification suite (320 renders), 41 unit tests.
- [x] Independent code review of `src/dsp`; all 11 findings fixed with regression tests (see below).
- [x] Custom UI incl. typed value entry and HiDPI check (see "UI" below).
- [x] README.

## What to do next (ideas, roughly by value)

1. Listening pass with real ears: preset levels, shimmer amount, grain shape — see README
   "What could not be verified without ears".
2. Oversample the tape saturator 2x (ADAA is first order; fine at normal drive, audible aliasing
   is possible with very bright input at Drive = 100 %).
3. True-peak (inter-sample) limiting; currently sample-peak with a -0.3 dBFS ceiling.
4. A user-assignable modulation matrix page; routings are fixed today (see "Modulation routing").
5. MIDI input for the arp key/scale (follow played chords), CLAP target.

## Repository layout

- `src/dsp/` — JUCE-free C++20 engine (`verdant_dsp` static lib). One source of truth for
  parameters (`Params.h`) and presets (`Presets.cpp`). Header-only modules + `Engine.cpp`.
- `src/plugin/` — JUCE wrapper: APVTS built from the table, preset management, editor (`ui/`).
- `tests/` — `verdant_render` (offline renderer + verification suite), `verdant_tests` (unit tests),
  `TestSignals.h` (synthesised impulse / sweep / noise / phrase / pluck / voice / hostile signals).
- `tools/analyze.py` — spectrograms, contact sheets, stats, suite report. `tools/ui_snapshot` —
  renders the editor to PNG under Xvfb.
- `scripts/` — one-command build / test / validate.

## Engine conventions

- **Control grid.** Fixed internal grid of 32 samples (`Engine::kChunk`), independent of the host
  block size, so output is identical for any buffer size (unit test `engine_is_block_size_invariant`).
  Smoothed values tick once per chunk; gains and delay times are linearly ramped per sample.
  Step/beat boundaries add 1e-7 beats before `floor()` so float noise cannot move an event by a chunk.
- **Signal flow.**
  `in -> Tape -> [+ Grain cloud + Arp direct] -> Chorus -> (+ Arp wash) Reverb -> tone/width -> mix -> limiter`.
  The dry path is delayed by `tape.nominalDelay()` (4 samples + 3 ms) so the wet path's direct
  component is exactly in phase with it (unit test `engine_dry_and_wet_paths_are_time_aligned`).
- **Latency** = tape nominal delay + limiter look-ahead (about 5 ms; reported to the host).
- **Mix law.** Equal power with a -1.5 dB trim at 50 % (the wet path carries a coherent copy of
  the direct sound, so plain equal power bumps the middle).
- **Limiter.** g_min = sliding minimum over W samples of the instant-attack/120 ms-release gain,
  g_box = moving average over W samples of g_min, audio delayed D = W-1. Every term g_min[n-j],
  j in 0..W-1, covers sample n-D, so g_box[n] <= g_raw[n-D] and |out| <= ceiling (0.966).
  It is the last processing stage (after dry/wet), so nothing above -0.3 dBFS leaves the plugin
  while it is active. Bypass is a true bypass: the latency-matched clean signal is crossfaded in
  *after* the limiter, so a bypassed plugin passes hot input untouched.
- **NaN guard.** `Engine::process` scans its output; on a non-finite sample it resets all state and
  outputs silence for that block.
- **Denormals.** `ScopedNoDenormals` (FTZ/DAZ on x86, FZ on arm64) inside `Engine::process`.
- **Bypass** crossfades (20 ms) to a latency-matched clean path and gates the engine input, so
  tails ring out instead of being cut or frozen.
- **Reset** restores every module to a bit-identical initial state (RNG seeds, LFO phases, ring
  write indices, free-running beat clock, shimmer shifter ratio): two renders around `reset()`
  are identical with a playing or a stopped transport (unit tests).
- **Tempo.** The beat clock uses the host tempo as given (1..999 BPM) and re-syncs to the host
  ppq every block; durations derived from it are clamped per module, not the tempo itself.

## Module notes

### Tape (`Tape.h`)
- Grain engine: 4x overlapped Hann grains of 100 ms. Neutral (speed 0, stretch 1) the grains all
  read the same spot, so the stage is an exact delay; the engine is pre-seeded at reset so there
  is no fade-in. (The whole module is then a delay plus the Drive-0 curve `2 tanh(x/2)`, the
  21 kHz tone filter and the 8 Hz DC blocker.)
- Varispeed: grains resample at the speed ratio; a new grain starts up to 12 ms further back at
  the offset that best correlates with what the previous grain is about to play (WSOLA). A pure
  tone stays a pure tone (within 1 cent) instead of a comb-filtered cluster. Non-neutral grains
  carry 12 ms of extra lag so the comparison windows are always behind the write head; the
  search runs on a ~16 kHz grid and is refined at full resolution.
- Speeding up band-limits what is recorded (4th-order Butterworth at 0.42 fs / ratio): no aliasing.
- Stretch: the read head falls behind at rate 1 - 1/stretch and snaps back to "now" every bar
  (4 beats), like Tape Fiasco's tempo-synced capture.
- Wow/flutter: modulated Hermite delay after the grains (base 3 ms). Depth is defined as pitch
  deviation and converted to delay excursion (`A = dev / 2 pi f`). Measured: Wow 30 % = +-2 cents,
  100 % = +-12 cents; Flutter 100 % = +-6 cents.
- Saturation: tanh with first-order ADAA applied to the residual `tanh(x) - x` only (plain ADAA
  costs 2.4 dB at 10 kHz; this is flat). Asymmetry is an even-order term `u^2/(1+u^2)`, not a DC
  bias, so silence stays digitally silent and turning Drive never thumps. Measured H3: -47 dB at
  Drive 30 %, -20 dB at 100 %.
- Age: 2-pole low-pass 21 kHz -> 3 kHz, head bump, random dropouts.
- Stutter: on its beat grid, with probability 0.6 * amount^2, the next slice plays live and is then
  repeated 2-8 times (3 ms edge fades, optional per-repeat pitch ramp / decay).

### Grain cloud (`GrainCloud.h`)
- Mono capture (8 s), up to 64 grains, each a panned point source. Loudness is normalised by the
  expected overlap (density x size), so 1 grain/s and 80 grains/s sit in the same window (test).
- Harmonics: octaves and fifths only (key-agnostic, always consonant); Sunlight biases up,
  Roots biases down. Regrow feeds the cloud (band-limited, soft-clipped, gain <= 0.72) back into
  the capture buffer.
- Grains read at least 40 samples behind the write head, and each block is rendered in segments
  that end at every spawn time, so slot reuse is sample-accurate: output is bit-identical for any
  block size even when more grains are requested than the 64-grain pool holds. (A saturated pool
  skips grains, so the cloud is then a little quieter than the normalisation assumes.)
- Grains transposed up read band-limited copies of the capture (0.20 fs up to x2.2, 0.11 fs above).

### Arp (`Arp.h`, `PitchTracker.h`)
- Records the input only while it sounds (gate with hysteresis), so the buffer always holds the
  last sound. Notes are read from the region that ends where the sound was last "strong"
  (>= half its recent peak): a decaying pluck is replayed from its body, not its tail.
- Notes are resampled slices. Three capture bands (full, 0.20 fs, 0.11 fs; 4th order) keep every
  ratio alias-free; the octave-up Sparkle is skipped above x4. Patterns derive from the beat
  position (deterministic in the host timeline). If quieter material has been sounding for 3 s
  the anchor re-references to it, so the arp cannot go silent after a loud passage.
- 6- and 7-note scales are walked in thirds (arpeggio, not a scale run). Bloom = ascending chord
  with long overlapping notes, widening pan, then a two-step breath.
- Pitch Track off (default): Scale = intervals above whatever is played (default "Fifths" is
  key-agnostic). Pitch Track on: YIN tracker (decimated to ~12 kHz, 85 ms window, < 10 cents on
  rich tones 82 Hz .. 1 kHz) and notes land on the Key/Scale exactly, correcting input detune.
- Linger: time constant 0.1 s .. 12 s; 100 % never fades.

### Chorus (`Chorus.h`)
- 24 dB/oct high-pass feeds the voices; the low band is the exact remainder (perfect
  reconstruction at 0 % mix; a 55 Hz tone is untouched at 100 %, unit test).
- Voice parameters are fixed per voice index (golden-ratio spreads), so changing the voice
  count fades voices in/out without moving the others. Level is normalised by sum of gains^2.
- Depth is defined in cents (max 21) and converted to delay excursion per voice rate.
- Glassy: 1.5-7.5 ms taps, 40 % of the depth, feedback of the *mean* tap (loop gain <= 0.5 for any
  phase relationship), high-passed and high-shelved wet.

### Reverb (`Reverb.h`)
- 16 delay lines 41-307 ms x size scale (0.22 .. 1.72), each followed by an allpass (9-37 ms),
  damping and decay gain; Hadamard mix; 4 input allpasses per channel; pre-delay; early tap.
- Decay gain per line from its loop time: measured T60 within about 15 % of the control.
- Shimmer is passive: on two mixed channels a fraction is *replaced* by its pitch-shifted copy
  (`cos t x + sin t shift(x)`), the shifter's windows sum to one, its input is band-limited
  (160 Hz .. 0.42 fs / ratio). No extra feedback path, so loop gain stays <= 1.
- Infinite: line gain exactly 1, bass absorption off, damping relaxed to >= 9 kHz. It sustains
  indefinitely only with Shimmer at 0: shimmer keeps moving energy up (or down) and out through
  its band limit, so an infinite tail with shimmer slowly dissolves (about -50 dB per minute at
  Shimmer 50 %). Freeze: input muted, gains 1, damping bypassed, shimmer off, modulation at 40 %.
- Decay is smoothed in the log domain, "infinite" is a smoothed amount, and line gains / bass
  absorption are ramped per sample: jumping Decay anywhere (including Infinite) does not click.
- The input is gated *before* the pre-delay and the module keeps running for 0.45 s after it is
  switched off and silent, so nothing stale is left in the pre-delay or diffusers.
- Energy governor: if mean-square per line exceeds 0.06 the effective T60 is shortened
  (log-interpolated towards 0.5 s over 6 dB). Soft limiter before the delay-line writes.
  Unit test `reverb_cannot_run_away`: infinite + full shimmer (all 5 intervals) + 0.9 FS noise
  for 30 s stays bounded and governed.
- Wet level is partly compensated for decay time (`(4 / T60)^0.3`): about -8 dB at 1 s,
  -3 dB at 6 s, -1 dB at infinite relative to the input.

### Output
- Ducking ("Clearing") lowers grains/arp and the reverb return while you play; the ducked-away
  ambience is still sent to the reverb, so the tail afterwards is unaffected (unit test).
- Grain and arp outputs are high-passed at 70 Hz (keeps envelope thumps out of the bass).

## Modulation routing (fixed)

| Source | Destinations |
|---|---|
| Drift 1 | tape wow depth and rate |
| Drift 2 | grain density, grain scatter |
| Drift 3 | grain size, chorus rate |
| Drift 4 | chorus depth, reverb motion, tilt (+-1 dB at Bloom 50 %, up to +-1.5 dB) |
| Envelope (Touch) | grain density (up to x2.2), reverb shimmer (+0.15) |
| Envelope (always) | ducking |

Drift rates 0.047 .. 0.163 Hz x Growth (0.2 .. 5); depth = Drift x Bloom (0.5 .. 1.5).

## Macro map (0.5 = preset as designed)

| Macro | Effect |
|---|---|
| Bloom | grain mix x0.3..1.5, grain density x0.5..1.8, harmonics x0.6..1.4, arp mix x0.3..1.5, chorus mix x0.4..1.3, wow/flutter x0.35..1.5, drive x0.6..1.3, stutter x0.5..1.4, shimmer x0.4..1.5, drift depth x0.5..1.5 |
| Growth | drift rate x0.2..5, chorus rate x0.5..2, reverb motion rate x0.4..2.5, wow rate x0.5..2.2, grain density x0.8..1.3 |
| Moisture | reverb mix x0.25..1.6, decay x0.4..2.5, size x0.8..1.2, arp wash +-0.3 |
| Sunlight | tilt +-4.5 dB, reverb damping x0.4..2.2, shimmer +0.3, arp sparkle +0.3, grain harmonics +0.25 and upward bias, tape age -+0.35 |
| Roots | low shelf +-8 dB @ 240 Hz, reverb low cut x2..0.5, tape drive +-0.15 and head bump +-2 dB, grain downward-octave bias |
| Mix | global dry/wet |

## Verification results (latest)

- Unit tests: 41 tests, ~7980 checks, all pass.
- Render suite (`verdant_render --suite`): 320 renders, 0 failures; max peak -0.30 dBFS;
  max |DC| 0.0008; worst CPU 6.0 % of one core at 48 kHz (everything maxed), typical preset 2-3.5 %;
  worst at 192 kHz about 15 %.
- pluginval v1.0.4, strictness 10, 44.1/48/96/192 kHz, block sizes 1..1024: SUCCESS.

## Bugs found by the tests (kept as a reminder of why they exist)

- `Ring::clear()` did not reset the write index -> after `reset()` grains read the wrong place.
- Subtractive one-pole crossover leaked 47 % of a 55 Hz tone into the chorus -> real 24 dB/oct split.
- Tape path faded in over 100 ms after reset (grain windows not overlapped yet) -> pre-seeded.
- DC bias in the saturator made "silence in" produce -175 dBFS that circulated in long reverbs.
- Free-running granular varispeed produced inharmonic line clusters -> WSOLA alignment.
- Ducking starved the reverb of the ambience it ducked -> ducked remainder is sent to the reverb.
- Arp replayed the quiet tail of decaying notes -> anchor on the last "strong" position.
- Step clock could fire one sample early/late depending on block size (float noise at exact
  sample boundaries) -> epsilon + 31.5-sample anticipation window.

## Independent review (round 1) — all fixed, each with a regression test

1. Tape alignment search read samples ahead of the write head; downward Speed turned into a
   beating line pair once the capture ring wrapped (~11 s). -> extra lag + bounds.
2. Reverb line gains stepped per control tick; Decay jumps (esp. to Infinite) clicked. -> ramps.
3. Re-enabling the reverb replayed stale pre-delay content. -> gate before pre-delay + flush.
4. Saturated grain pool made output depend on host block size. -> segment rendering.
5. Regrow gain un-smoothed (step recorded into the capture). -> smoothed + ramped.
6. Arp could go silent for seconds after a loud-then-quiet input. -> anchor timeout.
7. `reset()` left the free-running beat clock and the shimmer shifter ratio. -> restored.
8. Tempo below 20 BPM desynchronised the beat clock from the host. -> real tempo.
9. Drift parameter jump stepped the tilt EQ. -> smoothed.
10. With the reverb off, ducking was partly undone. -> only the arp wash is re-added.
11. Alignment search cost 3x more at 44.1 kHz (stride truncation). -> rounded stride + refine.

Worth a second review round after any larger DSP change: the tests that existed before the
review all passed while these defects were present.

## UI

Custom-drawn editor in `src/plugin/ui/` (see README). The UI reads `Engine::meters` (atomics) and
parameters only; it is data-driven from `kParams`, so new parameters appear automatically.
`tools/ui_snapshot` renders pages to PNG for visual checks (`--page`, `--width`, `--scale`,
`--browser`, `--value-entry ID`, `--audio S --real-meters`, `--stress S`, `--bench N`).

- Paint cost of the Garden visual (software renderer): ~2.7 ms at default size, 7.5 ms at 2x.
  A repaint governor keeps 30 fps while a paint costs <= 6 ms and falls to no less than 15 fps
  above that (`kPaintBudgetMs` in `GardenVisualDetail.h`).
- Verified headlessly only (Xvfb, pluginval, ASan/LSan stress runs). Never driven with a real
  mouse/keyboard or inside a DAW window; macOS/Windows untested.
- ninja can keep stale objects if a `src/dsp` header is saved during a compile (different
  `sizeof(Engine)` across translation units -> crashes). If something inexplicable happens,
  delete the objects under `build/CMakeFiles/Verdant.dir` and `verdant_dsp.dir` and rebuild.
