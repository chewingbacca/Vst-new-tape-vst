# Swoon — working notes

Living document so the work survives a context reset. Read this first.

## Where things stand

- [x] Toolchain, JUCE 9.0.6 fetch, VST3 + Standalone build on Linux
- [x] DSP engine: all modules, macros, reordering, limiter (`Source/dsp/`)
- [x] Offline harness (`swoon_render`), unit tests (`swoon_tests`), Python
      analysis (`tools/`), suite runner (`tools/run_suite.py`)
- [x] Custom UI (`Source/ui/`, `Source/PluginEditor.*`), embedded fonts; two
      rounds (second: front-page Freeze, typed values, preset search, status line)
- [x] 39 factory presets, level-matched (`Source/dsp/Presets.cpp`,
      `PresetTrims.inc`)
- [x] pluginval strictness 10 passes on the full plugin
- [x] README with build, install and parameter guide
- [ ] Anything under "What I'd tune next" below

## Environment

- Ubuntu 24.04, g++ 13.3, CMake 3.28, Ninja. No macOS here, so no AU build
  (CMake adds AU automatically on Apple). macOS/Windows are untested.
- JUCE 9.0.6 fetched by CMake `FetchContent`. Local dev:
  `SWOON_JUCE_DIR=/root/deps/JUCE ./build.sh` (or
  `-DFETCHCONTENT_SOURCE_DIR_JUCE=...`).
- pluginval 1.0.4 at `/root/deps/pluginval/pluginval` (Tracktion GitHub
  release). Headless: wrap in `xvfb-run -a`. Run it with `HOME` pointed at a
  scratch folder, because its tests can write user presets.
- Python 3.13 + numpy/scipy/matplotlib/soundfile for `tools/`.
- JUCE 9 notes: `AudioProcessor::createEditor()` can't be called from outside
  (use `createEditorIfNeeded()`); `juce::Drawable` is no longer a Component;
  libxi-dev is required on Linux.

## Architecture

- `Source/dsp/` is plain C++20 with **no JUCE**: static library `swoon_dsp`.
  The plugin, the harness and the unit tests all link it. All DSP is original.
- `Params.h`: one X-macro table (id, name, module, kind, range, default, skew,
  unit, choices). APVTS layout, `ParamValues` (plain float array handed to the
  engine), presets and the UI all derive from it. Add a parameter there and it
  appears in the host, the state and the Machine page automatically.
- `Engine` (pimpl) is the whole signal path. `process (L, R, n, ParamValues)`
  collects input into a FIFO and always processes whole chunks of
  `kChunk = 32` samples (control-rate updates per chunk, per-sample smoothing
  inside), handing each chunk back one chunk later. That adds 32 samples of
  reported latency and makes the output bit-identical for any host block
  size. (Before this, LFO/drift/smoother speeds scaled with small or odd host
  buffers, because control-rate objects advanced once per call.)
- Input is sanitised on entry (non-finite -> 0, clamp to +-16). `isBad()` tests
  the bit pattern so fast-math modes cannot remove it.
- `PluginProcessor` copies APVTS atomics into `ParamValues` each block, handles
  mono/stereo layouts, and feeds `VizData` (lock-free meters + scope FIFO).
- On/off parameters use a custom `SwitchParameter` that snaps to 0/1.
  `juce::AudioParameterBool` keeps raw values, so APVTS skipped restoring "on"
  over a fuzzed 0.58 and pluginval's state test failed.
- `PresetManager`: factory presets from `dsp/Presets.cpp` + user presets as XML
  (`*.swoon`) in the app-data folder. Presets never touch `bypass` or `inGain`.

### Signal path

input gain + auto-swell → seven reorderable modules → Tape → Output (tilt,
warm shelf, width, gain) → global Mix against latency-compensated dry → soft
limiter → bypass crossfade.

Reorderable, default order: Glide, Fuzz, Amp, Mod, Focus, Reverse, Wash. Order
= seven automatable int params (`ordX`, 1..7), resolved by stable sort, so any
combination is valid.

### Conventions every module follows

- `ModuleGate` (Util.h): smoothed wet mix; when the mix reaches zero the module
  runs on silent input for `flushSamples` so its memory empties, then sleeps
  (zero CPU). No stale tail when it wakes.
- Modules with short "always valid" memory (fuzz stack line, tape transport,
  glide heads, chorus lines) keep pushing real input while asleep and while
  flushing, so waking is seamless. Flushing zeroes the signal *after* that
  first delay stage, never before it.
- Modules with latency (fuzz, amp, tape) delay their own dry path by the same
  amount whether on or off, so reported latency is constant.
- Discrete switches go through `ModeSwitch`: dip the wet to zero over a few
  ms, swap, come back.
- `Smooth` (one-pole) snaps onto its target when float rounding would stall
  it; use `SmoothD` (double) for anything measured in samples (delay lengths),
  where a float stall is whole samples at high rates.
- Every module ends with an `isBad()` guard that clears its state; the engine
  has a final guard that resets everything. Neither has ever fired in testing.

### Macros (`Macros.h`)

0.5 = "module page settings exactly"; 0 removes that part of the sound; above
0.5 pushes past the module values. The laws live in `Macros.h`; the engine
applies them and `Presets.cpp` inverts them, so a preset is written as the
sound you hear plus macro positions and the base values are solved. With a
macro at 0 the written values are taken as the module's own settings.

## DSP design decisions

**Fuzz** (`Fuzz.h`). Two cascaded clipping stages, each the ODE of a gain stage
with a capacitor and anti-parallel diodes in the feedback loop:
`du/dt = wc (d - u - beta sinh u)`, solved with BDF2 + two Halley steps from a
bounded initial guess (min of the linear and asinh solutions). Two Halley steps
land within 1e-4 of the root; three Newton steps were both slower and 200x
less accurate. The capacitor slews the output between the
rails, which gives the woolly sustain and makes the stage self-band-limiting.
`DiodeQuad` solves both stages of both channels as four lanes so the compiler
vectorises it (second stage runs one oversampled sample behind the first; the
latency padding accounts for it). 8x oversampling at 44.1/48 kHz, 4x at
88.2/96, 2x above; linear-phase half-band FIRs, padded to whole-sample latency.
- Octave: full-wave rectifier ahead of the clippers. Velcro: bias-starved
  second stage with a sagging bias, lower first-stage gain.
- Tone stack: one-pole LP and HP blended; corners 408/723 Hz (flat), 900/480
  (full, plus a body peak), 408/1809 (scoop, about -10 dB notch at 860 Hz).
- Hand-rolled exp/log were *slower* than glibc's in scalar code; they only pay
  off inside the vectorised four-lane loop.

**Fuzz stack**. Each side's input goes through its own slowly wandering delay
around a common 9 ms mean (scaled by Stack), so neither side always leads and
the two fuzzes hear slightly different tunings.

**Glide** (`Glide.h`). Variable-speed read head (delay changes by `1 - ratio`
per sample): exact, artefact-free pitch change. A weak spring pulls the head
back to a 10 ms rest delay; if it leaves its 0.6–46 ms window it is spliced
back with a 12 ms crossfade at the lag found by correlation search. Sources:
sine wobble with wandering rate, band-limited random drift (common + per-side),
and a strum-triggered dip (fast/slow envelope ratio; strength keeps updating
for 30 ms so harder strums dip further). Every detected strum also re-anchors a
wandered head to the rest delay with a fast crossfade: the audio skipped is the
fading end of the previous stroke, and the new attack lands on time. Spread is square-law: any pitch
difference between sides integrates into a time difference and decorrelates
the image fast.

**Wash** (`Wash.h`). 16-line FDN, Hadamard mixing, 4 allpass diffusers per side
at the input, 8 lines modulated (cubic reads), 8 static (whole-sample reads,
lossless). Per-line low-pass and low thinning; decay gains from RT60.
- Shimmer is inside the loop: six lines add a pitch-shifted copy of themselves
  (two-grain delay-line shifter, random grain start). The copy's gain is capped
  at `s^2 <= 1/g^2 - 1`, i.e. the energy that line just lost to decay, so each
  line stays passive and nothing can grow. An earlier version *replaced* part of
  the line with the shifted copy (power-preserving crossfade); that drained the
  note's body and shortened the tail, so it was changed.
- Freeze: gains to 1, damping bypassed, modulated lines fade to whole-sample
  reads (so it holds without loss), input closed, shimmer off.
- A soft clipper at ±4..8 in the loop is a safety net only.

**Reverse** (`Reverse.h`). Swell = 32 jittered, random-polarity taps per side
across the window with gains rising as `pos^k` (k from Smear), then three
allpasses; regeneration goes through a plain delay of one window so it is
unconditionally stable. Tape = two overlapping sin² grains reading backwards.

**Soft focus** (`SoftFocus.h`). 4-voice micro-detune chorus → two cross-fed
delays (ratio 1 : 1.52) → 8-line haze FDN.

**Amp / Tape** (`AmpTape.h`). Saturators use first-order antiderivative
anti-aliasing (two half-sample stages per module = one whole sample of
latency). Cabinets are 7-biquad hand-voiced sets. Tape wow/flutter modulate a
3.5 ms transport delay (reported as latency).

**Output** (`Output.h`). Limiter = gain rider (1.5 ms attack, 160 ms release,
target -3 dBFS) + soft clipper with a hard ceiling at -0.2 dBFS. Zero latency.

**Reordering** (`Engine.cpp`). Muting the output is not enough: each module's
memory would record the step in its input. So every module's *input* and the
chain output are faded to zero, the latency lines are allowed to drain, the
order swaps, and everything fades back. Tails survive and re-emerge. It is an
explicit state machine (idle / fadeOut / drain / settle / fadeIn); the swap
always takes the newest requested order, so changes that arrive mid-transition
cannot strand it (an earlier ad-hoc version could deadlock muted).

**Tempo sync**. `focSync` / `revSync` (Free, 1/16 .. 1/2). Synced Focus uses a
1 : 1.5 ratio (straight against dotted) instead of 1 : 1.52. The processor
passes the host BPM with `Engine::setTempo` each block (120 if unknown).

## Measurements (48 kHz unless noted)

- Fuzz aliasing at full sustain, inharmonic energy vs harmonics:
  1 kHz -130 dB, 2.5 kHz -88 dB, 5 kHz -76 dB (was -50 dB at 4x). Octave type:
  -113 / -78 / -59 dB. Velcro (deliberately harsh, faster edges): -115 / -57 / -50 dB.
- Fuzz types and voicings are level-matched to within about 1 dB at Tone 50 %
  (around -18 dBFS RMS on the strummed phrase); Tone moves level by +-2-3 dB.
- Strum timing through Glide: each stroke leaves 8-14 ms after it arrives, even
  with 95-cent dips (the head is re-anchored at every detected onset).
- Glide: a 10-cent wobble measures ±10.0–10.6 cents on a tone; level unchanged.
- Wash RT60 measures 80–90 % of the Decay setting in the mids; wet level stays
  within ±1.5 dB from 1 s to 40 s decay.
- Shimmer: fundamental decays identically with shimmer on or off; octaves reach
  the fundamental's level a few seconds into the tail at 30 % and overtake it at
  100 %. Maximum decay + maximum shimmer + loud noise for 20 s stays bounded.
- Click tests: 24/24 (HF spike at each event < -58 dBFS or within 9 dB of the
  steady HF level).
- Latency: 251 samples at 48 kHz (fuzz 49, amp 1, tape 169, internal block
  32). Bypass is a bit-exact delayed copy of the input, also when engaged
  while running.
- `Engine::process` performs zero heap allocations (unit test counts calls to
  operator new across preset loads, reordering and random parameter changes).
- Clang 18 builds the DSP and tests with no warnings; same results, same speed.
- A fresh `git clone` + `./build.sh` (fetching JUCE) builds and passes.
- CPU (one core, 256-sample blocks): everything on measured 11 % at 48 kHz
  (fuzz 6 %, wash 1.5 %, focus 1.3 %), clean presets about 5 %; 96 kHz 18 %,
  192 kHz 31 %. The VM later moved to a slower host (2.1 GHz Xeon vCPU), where
  the same binaries measure about 14 % everything-on. CPU no longer depends on
  host block size (block size 1 used to cost half as much again).
- Presets: walls sit at -17.5 dBFS RMS on the strummed phrase, clean presets
  -20.5, ambient -22; the limiter never engages on any of them.

## Research digest (from web-search summaries; primary pages were not readable)

- Keeley Loomer: fuzz Level/Fuzz/Filter + Flat/Full/Scoop; reverb
  Blend/Decay/Depth/Warmth + Focus/Reverse/Hall; order switch. Focus = dense
  reverb + two delays at 250/380 ms + 4-voice chorus. Reverse = delay line read
  at several points with rising volume, 150–500 ms, Depth = envelope-triggered
  pitch dip. Hall = long wash with octave regeneration in the feedback loop.
- Yamaha FX500 "Soft Focus" (community-reported): delays 250/380 ms, heavy
  feedback, ~3.4 s reverb, symphonic chorus, mid cut.
- Loveless: SPX90 reverse-gate style program (an inverted-envelope reflection
  pattern, not reversed audio), used fully wet with the tone turned down;
  Midiverb II reverse programs 150–500 ms; glide guitar = arm held while
  strumming, mostly a downward dip and return.
- Big Muff: ~20 dB input boost, two clipping stages ~23–25 dB each with diodes
  in the feedback loop, band-limited to roughly 60 Hz–1.5 kHz; tone stack LP
  408 Hz vs HP 1.8 kHz; "flat mids" mod moves the HP to ~720 Hz.
- Shimmer: low-pass every pass, keep pitched loop gain below unity, high
  diffusion to hide grain.

## Independent review (done once, by a separate agent, read-only)

Found and fixed, each with a regression test in `Tests/unit_tests.cpp`:
reorder state machine could deadlock muted; Reverse replayed stale audio after
a mode switch (lines are now written in every mode); Wash line lengths stalled
short of whole samples after a Size change, so Freeze slowly lost level (now
`SmoothD` + whole-sample reads on all lines when frozen); control-rate speeds
depended on host block size (now a fixed internal block); a frozen tail
survived switching Wash off; bypass engaged at run time leaked wet at -100 dB;
non-finite input was not scrubbed; MSVC was set to `/fp:fast`; preset list
read without the lock. Left as is, on purpose: Glide's ~10 ms rest delay is
not reported as latency (see README, "Latency and CPU").

## Not verified (needs ears or another machine)

- Whether any of it actually sounds good. Everything above is measurement.
  In particular: fuzz voicing against a real guitar, cabinet voicings, how
  natural the glide dip feels under real strumming, grain audibility in the
  shimmer, metallic colour in the reverse swell and in short Wash sizes.
- The synthetic test guitar is brighter and has a higher crest factor than a
  real DI, so preset levels and gate thresholds may sit differently on real
  playing.
- macOS (AU, CoreGraphics rendering of the haze), Windows, HiDPI, and real
  DAW hosts. Only pluginval and the headless snapshot tool have loaded it.
- Mouse wheel handling in the UI was implemented but never driven; typed value
  entry and browser keys were driven only by synthetic events in the snapshot
  tool, not in a live host window.

## What I'd tune next

- A listening pass over the presets with a real guitar DI; adjust gate,
  sustain and trims.
- Fuzz: slight asymmetry between the diodes for even harmonics; optional
  2x/4x "eco" mode.
- Wash: early-reflection stage for small sizes; a width control of its own.
- Glide: expose the dip sensitivity; latency-free mode for live playing.
- UI: drive the mouse-wheel and keyboard paths in a real host; HiDPI check;
  a tap/hold "momentary freeze" gesture.
