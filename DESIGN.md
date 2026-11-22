# Automata — design

Automata is a generative rhythm and sound-design workstation: 8 tracks of synth
and sample voices driven by a deep algorithmic sequencer, with micro-editing,
modulation and built-in glitch processing. VST3 instrument + Standalone (AU on
macOS). JUCE 9, CMake, C++20.

Guiding principle: **controlled complexity**. Every random decision is a pure
function of `(seed, track, step, loop)`. Nothing random is drawn from a running
generator on the audio thread, so the same project at the same song position
always sounds the same, and any happy accident can be locked, kept and edited.

---------------------------------------------------------------------------

## 1. Layers

```
plugin/   JUCE layer: AudioProcessor, host parameters, state chunk, editor (UI)
core/     JUCE-free C++20 library, namespace atm — everything that makes sound
  model/      Params, Project/Pattern/Step data model, serialisation
  seq/        clock, step evaluation, ratchets, conditions, scales, grooves
  gen/        generators (euclid, CA, density, Markov, mutate), New Idea, evolve
  dsp/        primitives: filters, envelopes, oscillators, delays, FFT
  voices/     six engines + the per-voice channel (drive, filter, amp)
  fx/         effects and the factory that builds them
  mod/        LFO, chaos, envelope follower, mod matrix
  engine/     AudioEngine: ties it together; command queues
  factory/    synthesised factory samples, voice presets, kit presets
tools/    offline host harness, UI snapshot tool, preset tools
tests/    unit tests (core only, no JUCE) + plugin-level render tests
```

The core has no JUCE dependency. That keeps it fast to compile, unit-testable
in isolation, and makes the audio path easy to audit for real-time safety.
The plugin layer owns everything OS/host related: files, audio decoding,
threads, the message loop, drag and drop.

---------------------------------------------------------------------------

## 2. Signal flow

```
            ┌────────────── track × 8 ──────────────┐
 sequencer →│ voices(≤4): engine → drive ⇄ filter → amp │→ pan/level → glitcher → insert A → insert B ─┬→ track out bus (stereo)
 MIDI in  → └───────────────────────────────────┘                                              ├→ send A ─→ [fx][fx] ─┐
                                                                                                ├→ send B ─→ [fx][fx] ─┤
                                                                                                └→ mix ───────────────┴→ master [fx×4] → soft limiter → main out
 sidechain in → envelope follower (mod source), compressor key
```

* 9 stereo output buses: Main + one per track (post-insert, pre-master). Track
  buses that the host leaves disabled cost nothing.
* 1 optional stereo sidechain input.
* MIDI out: every track can emit its notes (channel per track).

### Voices (one engine per track)
1. **Drum** — analog-style kick / snare / hat / clap / tom / metal models.
2. **FM** — 4 operators, 8 algorithms, feedback.
3. **Sampler** — one-shot / loop / slice, reverse, mip-mapped pitch.
4. **Granular** — grain cloud over a loaded sample.
5. **Resonator** — exciter into comb (string/tube) and modal banks.
6. **Synth** — analog-style (polyBLEP) and wavetable mono synth.

Each track owns up to 4 voice instances (mono = 1 with a short retrigger
crossfade, poly = 4 round-robin with oldest-steal). A voice is
`engine → drive → filter → amp` (drive position switchable) with its own amp
and mod envelopes. All per-voice state changes are click-safe: a stolen voice
is faded over ~3 ms while the new one starts.

### Processing granularity
The engine renders in *slices*. A slice ends at whichever comes first: the next
sequencer/MIDI event, the next control tick (every 16 samples on an absolute
sample counter), or the end of the host block. Events therefore land on their
exact sample, modulation runs at a fixed 16-sample control rate independent of
host buffer size, and rendered output does not depend on block size.

---------------------------------------------------------------------------

## 3. Parameters

Every numeric setting is a **normalised 0..1 parameter** with a descriptor
(name, range, curve, unit, discrete steps, default). Two id spaces:

* `TP::*` — per-track (level, pan, tune, 16 engine slots `E0..E15` whose
  meaning depends on the engine, amp env, mod env, filter, drive, sends,
  2 insert FX × (8 + mix), 2 LFOs, step-mod glide, glitch sensitivity …).
* `GP::*` — global (macros, swing, send-bus FX, master FX, chaos, follower …).

A *destination* is `(track | global, param id)`. Modulation and parameter locks
are both expressed against destinations and both operate in the normalised
domain, so *any* voice or effect parameter can be locked per step or modulated.
Mapping to physical units happens once per control tick inside the consumer.

Only a small stable set is published to the host for automation (6 generative
macros, 4 mod macros, per-track level/pan/mute, master, swing, fill, break,
evolve). Everything else is plugin state edited from the UI.

---------------------------------------------------------------------------

## 4. Sequencer data model

```
Project
 ├─ Kit                       one sound set shared by all patterns
 │   ├─ TrackSound × 8        engine, params[TP::count], fx types, sample ref,
 │   │                        LFO draw tables, poly mode, MIDI-out, lock flag
 │   ├─ global params[GP::count], send/master fx types
 │   ├─ ModSlot × 64          {source, srcTrack, dstTrack|global, dstParam, amount}
 │   └─ tuning                root key, scale (built-in or Scala .scl data)
 ├─ Pattern × 16
 │   ├─ lengthBars, pattern swing
 │   └─ TrackPattern × 8
 │       ├─ length 1..64, speed (rational step length), direction
 │       ├─ swing (inherit / own), groove template + amount, rotation
 │       ├─ freeRun (polymeter never resets) / reset each pattern cycle
 │       ├─ Step × 64  {on, velocity, pitch, length, probability, condition,
 │       │              micro, slide, ratchet{count, curve, amount, pitchRamp,
 │       │              velRamp, gate}}
 │       ├─ LockLane × 12 {destination, value[64], setMask}   (parameter locks)
 │       ├─ stepMod[64]                                        (mod lane)
 │       └─ GeneratorState {type, seed, seedLocked, params…}
 ├─ Song: chain of {pattern, repeats} × 64, songMode
 └─ Performance: seeds, evolve settings, style
```

All of this is plain fixed-size data (no pointers, no heap), so a
`TrackPattern` or `TrackSound` can be copied to the audio thread with a
`memcpy`, snapshotted for undo, and serialised field by field.

### Time
Musical time is the host's PPQ position (quarter notes, double). A track's step
length is `speed.num/speed.den` quarter notes (1/16 = 1/4; triplets, dotted and
odd ratios such as 3:4 or 5:4 are just other rationals). For a track,

```
abs      = floor((beat − anchor) / stepLen)        absolute step counter
loop     = abs div length                           loop counter (conditions)
index    = direction(abs, length, seed)             which Step plays
t_nominal= anchor + abs · stepLen
t_event  = t_nominal + swing(abs) + groove(index) + micro · stepLen
```

`anchor` is 0 for free-running tracks, the start of the current pattern cycle
otherwise, and the chain entry start in song mode. Because step evaluation is a
pure function of the beat position, there is no sequencer "state" to
desynchronise: host loops, jumps, tempo changes and offline bounces all land on
the same events. Each block the engine asks for all events with
`t_event ∈ [b0, b1)`; continuity with the previous block is tracked so
consecutive blocks tile exactly (no event is dropped or doubled on a boundary).
An event's sample position is the first sample at or after its time.

### Step evaluation (pure function)
For absolute step `abs` on track `t`:

1. look up the written step; apply **Mutation** macro variation keyed on
   `(seed, t, index, loop)` (velocity/pitch/skip/add changes that differ each loop);
2. apply **Density** macro: thresholds on a per-step hash, so turning the knob
   up adds ghost hits in a fixed order and turning it down thins the quietest
   hits first (nested sets — a sweep is repeatable);
3. conditions: probability (hash on `abs`), `A:B` loop conditions, `FILL`,
   `!FILL`, `PRE`, `!PRE`, `1ST`, `!1ST` (PRE walks back to the previous
   candidate step and evaluates it with the same pure function);
4. apply **Complexity** macro: hash-selected ratchets and micro-timing spread;
5. expand ratchets: `count` hits over the step, spaced by curve
   (even / accelerating / decelerating / bouncing-ball geometric), with a
   velocity ramp and a pitch ramp across the roll;
6. pitch → scale/key quantise → frequency (12-TET scale snap, or Scala degree
   mapping for microtonal tunings);
7. gather parameter locks of that step. Locks hold until the next step starts,
   then destinations return to their base values. A step that has locks but no
   trigger is a trig-less lock.

Fill/Break are performance states that add offsets to the macros and enable
`FILL` conditions until the next bar line, then snap back.

### Generators
Operate on a `TrackPattern` value and return a new one (pure, allocation-free):
euclidean, 1-D cellular automata, probability density, Markov variation of the
existing pattern, mutate(amount). Each stores its seed and a seed lock. *Evolve*
calls mutate on unlocked tracks at bar boundaries **on the audio thread**
(it's just arithmetic on fixed-size data), keyed by seed and bar number, so an
evolving render is still deterministic; changed tracks are posted back to the UI.

*New Idea* builds a whole kit and pattern set from `(style, seed, tempo range)`
using style rule-sets layered over the generators; locked tracks are untouched.

---------------------------------------------------------------------------

## 5. Modulation

Per track: 2 LFOs (free or tempo-sync; sine, triangle, saw, square, S&H,
smooth random, drawable 32-point table), a step-mod lane, mod envelope,
velocity, per-note random. Global: 4 macros, chaos (logistic or Lorenz),
envelope follower on the sidechain input. A single 64-slot matrix routes
sources to destinations; amounts are bipolar in the normalised domain.

The audio thread publishes the live modulated value of every parameter of the
selected track to an atomic array so the UI can draw modulation on each
control. Mod sources that are random (S&H, smooth random, chaos) are seeded and
tied to song position where possible (LFO phase is derived from the beat
position when synced).

---------------------------------------------------------------------------

## 6. Effects

One interface (`Effect`: prepare / reset / process(L, R, n, ctx) with 8
normalised params + mix). Slots: 2 inserts per track, 2 slots on each of the two
send buses, 4 master slots, then a fixed soft limiter. Each track also has a
built-in **Glitcher** (repeat / reverse / tape-stop / crush / gate) which the
Glitch macro drives deterministically per step.

Effects: stutter (buffer repeat with pitch ramp), reverse, tape stop, bitcrush /
sample-rate reduction, distortion + wavefolder (2× oversampled), comb,
resonator bank, frequency shifter, ring mod, spectral freeze/blur (STFT),
granular delay, modulated delay, plate reverb, large diffuse reverb, compressor
(sidechain from any track or the external input), filter/EQ, soft limiter.

Effect objects are built and `prepare()`d on a background/message thread and
handed to the audio thread by pointer; replaced objects go back through a
garbage queue to be freed off the audio thread.

---------------------------------------------------------------------------

## 7. Threading model

| Thread | Owns | May do |
|---|---|---|
| Audio | `rt` copy of the Project, voices, FX, queues' consumer side | arithmetic only: no allocation, no locks, no I/O |
| Message (UI) | canonical `Project`, undo history, sample pool | allocate, file dialogs, build FX |
| Background worker | — | decode samples, build mip-maps, synthesise factory samples, offline renders for drag-out |

**UI → audio**: a single-consumer lock-free command FIFO. Commands are small
(`setTrackParam`, `setStep`…) or carry a pointer to a heap object prepared by
the sender (`TrackPattern`, `TrackSound`, whole `Project`, `Effect*`,
`SampleData*`). The audio thread copies or swaps and returns the old object via
a garbage FIFO which the message thread drains on a timer. Producers (UI,
host thread calling `setStateInformation`) serialise on a small mutex that the
audio thread never touches. If the FIFO is full (audio not running), the sender
marks a resync and later sends a whole-project snapshot.

**Audio → UI**: atomics for playheads, meters, live modulation values and
performance state; a FIFO of changed `TrackPattern`s for evolve.

**Samples** are immutable, reference-counted `SampleData` (float channels +
mip levels). The audio thread only ever holds raw pointers that the message
thread keeps alive until the audio thread has acknowledged the replacement.

**State**: `getStateInformation` serialises the canonical project to JSON
(+ embedded sample blobs). `setStateInformation` parses on the calling thread,
loads samples, then swaps the whole project in with one command.

---------------------------------------------------------------------------

## 8. Verification

* `tests/` — unit tests for sequencer maths (step timing, ratchet curves,
  conditions, directions, generators, scales, serialisation round-trip) and DSP
  (filter stability, envelope continuity, FFT identity, effect sanity).
* `tools/automata_render` — instantiates the real `AudioProcessor`, simulates a
  host (tempo, transport, loop, MIDI, changing block sizes), renders every
  factory preset and voice preset to WAV and checks: NaN/Inf, clipping, DC,
  clicks (sample-to-sample discontinuity against local envelope), silence,
  loudness spread, determinism (two renders bit-identical), block-size
  independence, onset timing vs expected step times.
* pluginval at strictness 10.
* `tools/automata_snapshot` — renders the editor to PNG headlessly for UI review.
