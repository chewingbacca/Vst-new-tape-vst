# Splinter

A glitch sampler and multi-effect (VST3 + Standalone, AU on macOS). Give it a
loop — drop in a file or capture the incoming audio — and it slices, resequences,
mangles and harmonises it in sync with the host: stutters, half-time drops, tape
stops, reversed hits, pitched repeats and stacked harmonies, always on the grid
and in key.

It is an audio effect that also takes MIDI, so it can sit on a track and process
it live, or be played from a keyboard or pads.

## Build

One command from a clean checkout:

    ./build.sh

That configures with CMake, fetches JUCE 9.0.6, builds the plugin, and runs the
unit tests and the offline render tests. Options:

    ./build.sh install     # also copy the plugin into your plugin folder
    ./build.sh notest      # skip the tests
    ./build.sh pluginval   # also download pluginval and validate at strictness 10
    SPLINTER_JUCE_PATH=/path/to/JUCE ./build.sh   # reuse an existing JUCE checkout

Requirements: CMake 3.22+, a C++20 compiler, Ninja (optional), git. On Linux also
the usual JUCE packages:

    sudo apt install build-essential cmake ninja-build pkg-config \
        libasound2-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev \
        libxext-dev libxcomposite-dev libxi-dev libxrender-dev \
        libfreetype-dev libfontconfig1-dev libgl1-mesa-dev

### Where things end up

| | |
|---|---|
| VST3 | `build/Splinter_artefacts/Release/VST3/Splinter.vst3` |
| Standalone | `build/Splinter_artefacts/Release/Standalone/Splinter` |
| AU (macOS) | `build/Splinter_artefacts/Release/AU/Splinter.component` |

`./build.sh install` copies them to the standard user locations:
`~/.vst3` (Linux), `~/Library/Audio/Plug-Ins/VST3` and `.../Components` (macOS),
`%CommonProgramFiles%\VST3` (Windows). Or copy the `.vst3` bundle there yourself
and rescan in your DAW. User presets are stored in
`~/.config/Splinter/Presets` (Linux), `~/Library/Splinter/Presets` (macOS) or
`%APPDATA%\Splinter\Presets` (Windows).

This was developed and verified on Linux (Ubuntu 24.04, GCC 13). The macOS and
Windows paths go through the same JUCE/CMake setup but have not been built here.

## Quick start

1. Put Splinter on an audio track (or open the Standalone and press PLAY).
2. It starts with a demo loop and the preset *Pocket Change*. Press play in the
   host: the loop follows the host tempo and bar position.
3. Step through presets with `<` `>` or click the name for the browser.
4. Turn the big knobs. Each has a tooltip; in short:

| Macro | What it does |
|---|---|
| **Chop** | Slice rearrangement. At 50 % the pattern plays as programmed; lower thins it, higher adds swaps. |
| **Stutter** | Repeats and freezes. Above 50 % it adds fills, mostly at the end of the bar. |
| **Warp** | Time and pitch bending. Scales the time curve up to 50 %, then adds reverses, tape stops and pitch dives. |
| **Harmony** | Level of the harmony voices. |
| **Crush** | Bit crushing, tuned resonators, ring mod. |
| **Chaos** | Seeded randomness on every unlocked lane. Same seed, same result, every time. |
| **Mix** | Dry / wet. Dry is the unglitched loop (or the live input). |

### Your own loops

- Drop a WAV, AIFF, FLAC or MP3 on the waveform, or use **LOOPS → Open audio file**.
- Tempo and length are detected (the file is assumed to be a loop of whole
  beats). If it guessed double or half, use **/2** and **x2**.
- The loop is time-stretched to the host tempo with pitch preserved. Choose the
  algorithm on the Output page: *Beats* (keeps drum hits intact), *Tonal* (phase
  vocoder, for sustained material), *Tape* (plain resampling, pitch follows
  tempo) or *Auto*.
- **SLICE** chooses transient or grid slicing; `-` `+` set the sensitivity.
  Drag a marker to move it, double-click to add or remove one, click a slice to
  audition it.
- The key is detected for tonal material; set **Key** and **Scale** in the top
  bar if it is wrong. Everything pitched follows them.

### Live input

Set the source to **Live Input**. The incoming audio runs through a rolling
buffer and every effect works on it as it plays (only the past can be read, so
"reverse" plays back what you just heard). **CAPTURE** grabs the last 1–8 bars
(Capture Bars, Output page) as a loop, slices it and switches to Loop mode. The
captured audio is saved with the project.

## Pages

**Perform** — Twelve slots each for time curves, volume curves and glitch
sequences; click to switch (quantised to the setting on the right). The bottom
row are *hold* pads: press one to fire that effect right now. *Generate pattern*
writes a new, musically placed sequence into the current slot.

**Curves** — The drawable envelopes, Gross Beat style.
- *Time*: height is how far behind real time playback sits. Flat = normal
  speed. A step down = jump back (a repeat). A slope = slower; the dashed line
  is "standing still", steeper than that plays backwards. The line is coloured
  by the speed it produces (green normal, cyan slow, white stopped, pink
  reverse, amber fast).
- *Volume*: gain across the bar, for gates and pumping.
- Click to add a point, drag to move, double-click to delete, alt-drag a segment
  to bend it, right-click for hold/step. Shift disables snapping. **Pencil**
  paints one flat step per grid cell as you drag. **Insert shape** drops in
  common starting points.

**Sequence** — One lane per effect, 8–32 steps. Click toggles a step, dragging
sideways paints a run (joined steps play as one gesture: one long tape stop
rather than four short ones). Drag up/down on a lit step for its probability;
the mouse wheel or right-click chooses its variation (stutter division, which
slice, pitch interval, filter shape …). Click a lane name for its settings; the
padlock protects a lane from Chaos and Generate.

| Lane | |
|---|---|
| Chop | play another slice here (`?` = random, favouring the same part of the beat) |
| Stutter | repeat the start of the step; pitch ramp, gate, decay, accelerate, arpeggio through the scale |
| Reverse | play the run backwards |
| Tape Stop | slow to a halt (or spin up, or half-speed drag) |
| Pitch | tape-style re-pitch by scale steps, optional glide |
| Crush | bit depth and sample-rate reduction |
| Comb / Ring | resonator / ring modulator tuned to a note of the key |
| Filter | low-, high- or band-pass sweep across the run |
| Freeze | granular sustain of the moment |
| Delay / Reverb | throws: only this step is sent, the tail rings on |
| Harmony | when set to *Sequenced*, voices only sound on these steps; variations move the chord |

**Harmony** — Up to four extra voices, pitch-shifted with formants preserved.
Intervals are in scale steps (a "third" is major or minor as the key demands,
following the note being played) or plain semitones. Per-voice pan, detune and
level. The wheel shows the scale, the note being heard and where each voice lands.

**Output** — High-pass, low-pass, tilt, width, output gain, limiter, plus
session settings (stretch mode, quantise, MIDI mode, capture length).

## MIDI

Choose what notes do with **MIDI NOTES PLAY** (Perform / Output page).

*Slices*: each key from C1 (note 36) upwards plays one slice, polyphonically,
through the whole glitch chain. *Slice keys* selects gate or one-shot. Turn
*Loop plays* off to play the loop only from the keys.

*Patterns*:

| Keys | |
|---|---|
| C1–B1 (36–47) | select time curve slot 1–12 |
| C2–B2 (48–59) | select volume curve slot 1–12 |
| C3–B3 (60–71) | select sequence slot 1–12 |
| C4–C5 (72–84) | hold an effect: Chop, Stutter, Reverse, Tape Stop, Pitch, Crush, Comb, Ring, Filter, Freeze, Delay, Reverb, Harmony |

Slot changes are quantised (off, 1/16, 1/8, 1/4, bar). Slots are also ordinary
automatable parameters.

## Latency

Splinter reports its latency to the host: 2160 samples at 48 kHz (45 ms; it
scales with sample rate). Most of that is the look-ahead of the harmoniser, the
rest a band-limited reader and the limiter. In a DAW this is compensated; in the
Standalone you will feel it when playing slices from a keyboard.

## Tests

    ctest --test-dir build --output-on-failure   # unit tests + render tests
    scripts/dsptest.sh [filter]                  # fast DSP-only loop, no JUCE
    scripts/run_pluginval.sh 10                  # pluginval, strictness 10

The render harness (`tests/render_harness.cpp`) simulates a host — tempo,
transport, MIDI, block sizes, sample rates — renders the synthesised demo loops
through every factory preset and measures the result. It writes WAVs and
`report.txt` to `build/renders/`. It checks for NaN/Inf, clipping, DC offset,
clicks, timing against the grid, harmony pitch, block-size independence, state
round trip, latency (null test) and CPU.

`build/tests/splinter_uishot_artefacts/Release/splinter_uishot <dir>` renders
the editor pages to PNG.

## Design notes

`NOTES.md` has the architecture and the measured numbers. The short version:
every time effect is a *time map* evaluated per sample from the host position,
and a single reader follows the result, crossfading whenever the target jumps.
Nothing integrates, so playback cannot drift, and host loops, slice jumps,
stutters and slot switches all share one click-free mechanism.

## Credits

- Built with [JUCE](https://juce.com) 9 (AGPLv3 / commercial licence — check
  which applies to you before distributing binaries).
- All DSP is original. The harmoniser uses the peak-region shifting technique
  described by J. Laroche and M. Dolson, "New phase-vocoder techniques for
  pitch-shifting, harmonizing and other exotic effects" (1999); key detection
  uses the Krumhansl–Schmuckler key profiles. No third-party DSP code is included.
- The demo loops are synthesised from scratch at run time and are free to use.
- Workflow inspiration: Image-Line Gross Beat (time/volume curves and slots),
  dBlue Glitch, Sugar Bytes Effectrix (effect lanes), iZotope Stutter Edit,
  Cableguys ShaperBox / HalfTime.
- Validated with [pluginval](https://github.com/Tracktion/pluginval).
