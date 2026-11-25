// All parameter descriptor tables live here so names, ranges and keys stay
// consistent between the DSP, the UI, presets and serialisation.
#include "Params.h"
#include "ParamTables.h"
#include <cstdio>
#include <cstdlib>
#include <cctype>

namespace atm {

using PD = ParamDesc;
#define LABELS(arr) arr, (int) (sizeof (arr) / sizeof (arr[0]))
#define DISC(key, name, arr, def, help) { key, name, 0.f, (float) (sizeof (arr) / sizeof (arr[0]) - 1), (float) (def), PD::Discrete, "", LABELS (arr), help }
#define UNUSED { "", "", 0.f, 1.f, 0.f, PD::Lin, "", nullptr, 0, "" }

static const char* const kOffOn[] = { "Off", "On" };

// ============================================================================ engines
// ---- Drum
static const char* const kDrumModels[] = { "Kick", "Snare", "Hat", "Clap", "Tom", "Metal" };
const ParamDesc kDrumParams[kEngineParams] = {
    DISC ("model", "Model", kDrumModels, 0, "Drum circuit model"),
    { "tune",   "Tune",   -24.f, 24.f, 0.f,   PD::Lin,  "st", nullptr, 0, "Pitch offset from the model's natural pitch" },
    { "decay",  "Decay",  0.01f, 8.f,  0.35f, PD::Exp,  "s",  nullptr, 0, "Body decay time (snare heads ring 0.6x as long; clap: the tail; hat: closed to open)" },
    { "sweep",  "Sweep",  0.f,   1.f,  0.5f,  PD::Lin,  "",   nullptr, 0, "Pitch envelope depth (kick up to 5 octaves, tom 2, snare 1.5, hat/metal subtle; clap: band-pass sweep)" },
    { "sweept", "SwpTime", 0.002f, 0.5f, 0.04f, PD::Exp, "s", nullptr, 0, "Pitch envelope time" },
    { "tone",   "Tone",   0.f,   1.f,  0.5f,  PD::Lin,  "",   nullptr, 0, "Brightness: tone low-pass and click colour (kick, tom; tom also its 2nd partial), upper head mode and wire brightness (snare), filter colour (hat, metal), clap pitch" },
    { "noise",  "Noise",  0.f,   1.f,  0.2f,  PD::Lin,  "",   nullptr, 0, "Noise layer level: thump (kick, tom), wires / snappy (snare), sizzle (hat, metal), air (clap)" },
    { "ndecay", "N.Decay", 0.005f, 4.f, 0.15f, PD::Exp, "s",  nullptr, 0, "Noise layer decay" },
    { "ncolor", "N.Color", 0.f,  1.f,  0.5f,  PD::Lin,  "",   nullptr, 0, "Noise filter colour, dark to bright" },
    { "snap",   "Snap",   0.f,   1.f,  0.3f,  PD::Lin,  "",   nullptr, 0, "Transient level: click (kick, tom), stick (snare), tick (hat, metal), first clap burst" },
    { "fm",     "FM",     0.f,   1.f,  0.f,   PD::Pow2, "",   nullptr, 0, "Cross-modulation depth from the second oscillator (clap: ring modulation of the bursts)" },
    { "fmratio","FM Ratio", 0.25f, 16.f, 1.5f, PD::Exp, "x",  nullptr, 0, "Frequency ratio of the modulating oscillator" },
    { "shape",  "Shape",  0.f,   1.f,  0.f,   PD::Lin,  "",   nullptr, 0, "Body waveshape: sine through folded harmonics (hat: sum to XOR; metal: pair to group ring mod; clap: 3 to 7 bursts)" },
    { "spread", "Spread", 0.f,   1.f,  0.3f,  PD::Lin,  "",   nullptr, 0, "Clap burst spacing / hat and metal partial spread, plus stereo width of snare, hat, metal and clap (no effect on kick and tom)" },
    { "hold",   "Hold",   0.f,   0.5f, 0.f,   PD::Pow2, "s",  nullptr, 0, "Body hold before the decay starts (clap: hold of the tail)" },
    { "dirt",   "Dirt",   0.f,   1.f,  0.f,   PD::Lin,  "",   nullptr, 0, "Internal saturation" },
};

// ---- FM
static const char* const kFmAlgos[] = { "1>2>3>4 stack", "2 stacks", "3 into 1", "branch", "1 mod, 3 car", "stack + car", "pair + 2 car", "additive" };
const ParamDesc kFmParams[kEngineParams] = {
    DISC ("algo", "Algo", kFmAlgos, 0, "Operator routing"),
    { "fb",     "Feedback", 0.f, 1.f, 0.f, PD::Lin, "", nullptr, 0, "Operator 4 self-modulation" },
    { "ratio1", "Ratio 1", 0.25f, 16.f, 1.f, PD::Exp, "x", nullptr, 0, "Operator 1 frequency ratio (snaps near halves)" },
    { "ratio2", "Ratio 2", 0.25f, 16.f, 1.f, PD::Exp, "x", nullptr, 0, "Operator 2 frequency ratio" },
    { "ratio3", "Ratio 3", 0.25f, 16.f, 2.f, PD::Exp, "x", nullptr, 0, "Operator 3 frequency ratio" },
    { "ratio4", "Ratio 4", 0.25f, 16.f, 3.f, PD::Exp, "x", nullptr, 0, "Operator 4 frequency ratio" },
    { "level1", "Level 1", 0.f, 1.f, 1.f,  PD::Lin, "", nullptr, 0, "Operator 1 output level" },
    { "level2", "Level 2", 0.f, 1.f, 0.5f, PD::Lin, "", nullptr, 0, "Operator 2 output level / modulation index" },
    { "level3", "Level 3", 0.f, 1.f, 0.3f, PD::Lin, "", nullptr, 0, "Operator 3 output level / modulation index" },
    { "level4", "Level 4", 0.f, 1.f, 0.2f, PD::Lin, "", nullptr, 0, "Operator 4 output level / modulation index" },
    { "decay1", "Decay 1", 0.005f, 10.f, 10.f, PD::Exp, "s", nullptr, 0, "Operator 1 envelope decay (maximum = sustain)" },
    { "decay2", "Decay 2", 0.005f, 10.f, 0.3f, PD::Exp, "s", nullptr, 0, "Operator 2 envelope decay (maximum = sustain)" },
    { "decay3", "Decay 3", 0.005f, 10.f, 0.2f, PD::Exp, "s", nullptr, 0, "Operator 3 envelope decay (maximum = sustain)" },
    { "decay4", "Decay 4", 0.005f, 10.f, 0.1f, PD::Exp, "s", nullptr, 0, "Operator 4 envelope decay (maximum = sustain)" },
    { "penv",   "P.Env",  -48.f, 48.f, 0.f, PD::Lin, "st", nullptr, 0, "Pitch envelope depth" },
    { "penvt",  "P.Time", 0.002f, 2.f, 0.05f, PD::Exp, "s", nullptr, 0, "Pitch envelope time" },
};

// ---- Sampler
static const char* const kLoopModes[] = { "Off", "Forward", "PingPong" };
static const char* const kSamplerModes[] = { "OneShot", "Gate", "Slice" };
static const char* const kSliceBy[] = { "Grid", "Transient" };
const ParamDesc kSamplerParams[kEngineParams] = {
    { "start",  "Start",  0.f, 1.f, 0.f, PD::Lin, "", nullptr, 0, "Start point within the sample" },
    { "length", "Length", 0.f, 1.f, 1.f, PD::Lin, "", nullptr, 0, "Length of the played region" },
    DISC ("reverse", "Reverse", kOffOn, 0, "Play backwards"),
    DISC ("loop", "Loop", kLoopModes, 0, "Loop the region"),
    { "xfade",  "Fade",   0.f, 0.5f, 0.02f, PD::Pow2, "", nullptr, 0, "Edge fade / loop crossfade as a fraction of the region" },
    DISC ("mode", "Mode", kSamplerModes, 0, "One-shot, gated, or slice playback"),
    { "slices", "Slices", 1.f, 64.f, 16.f, PD::Discrete, "", nullptr, 0, "Number of slices" },
    { "slice",  "Slice",  0.f, 63.f, 0.f,  PD::Discrete, "", nullptr, 0, "Slice to play (lock this per step to chop a break)" },
    { "penv",   "P.Env",  -48.f, 48.f, 0.f, PD::Lin, "st", nullptr, 0, "Pitch envelope depth" },
    { "penvt",  "P.Time", 0.002f, 2.f, 0.05f, PD::Exp, "s", nullptr, 0, "Pitch envelope time" },
    { "startrnd", "Start Rnd", 0.f, 1.f, 0.f, PD::Pow2, "", nullptr, 0, "Random start offset per note" },
    DISC ("keytrack", "KeyTrack", kOffOn, 1, "Follow note pitch"),
    { "gain",   "Gain",   -24.f, 24.f, 0.f, PD::Lin, "dB", nullptr, 0, "Sample gain trim" },
    DISC ("sliceby", "Slice By", kSliceBy, 0, "Slice on an even grid or at detected transients"),
    { "width",  "Width",  0.f, 2.f, 1.f, PD::Lin, "", nullptr, 0, "Stereo width of stereo samples" },
    UNUSED,
};

// ---- Granular
static const char* const kGrainQuant[] = { "Free", "Octaves", "Fifths" };
const ParamDesc kGranularParams[kEngineParams] = {
    { "pos",    "Position", 0.f, 1.f, 0.25f, PD::Lin, "", nullptr, 0, "Read position in the sample" },
    { "size",   "Size",   0.004f, 1.f, 0.08f, PD::Exp, "s", nullptr, 0, "Grain length" },
    { "density","Density", 1.f, 250.f, 30.f, PD::Exp, "/s", nullptr, 0, "Grains per second" },
    { "spray",  "Spray",  0.f, 1.f, 0.1f, PD::Pow2, "", nullptr, 0, "Random position offset per grain" },
    { "scatter","Scatter", 0.f, 24.f, 0.f, PD::Pow2, "st", nullptr, 0, "Random pitch offset per grain" },
    { "scan",   "Scan",   -2.f, 2.f, 0.f, PD::Lin, "x", nullptr, 0, "Speed at which the read position travels" },
    { "revprob","Rev Prob", 0.f, 1.f, 0.f, PD::Lin, "", nullptr, 0, "Chance a grain plays backwards" },
    { "shape",  "Shape",  0.f, 1.f, 0.5f, PD::Lin, "", nullptr, 0, "Grain window: percussive, bell, reversed" },
    { "spread", "Spread", 0.f, 1.f, 0.5f, PD::Lin, "", nullptr, 0, "Random pan per grain" },
    { "jitter", "Jitter", 0.f, 1.f, 0.3f, PD::Lin, "", nullptr, 0, "Timing irregularity of grain onsets" },
    DISC ("quant", "Scat.Quant", kGrainQuant, 0, "Quantise the pitch scatter"),
    { "gain",   "Gain",   -24.f, 24.f, 0.f, PD::Lin, "dB", nullptr, 0, "Cloud gain trim" },
    DISC ("keytrack", "KeyTrack", kOffOn, 1, "Follow note pitch"),
    UNUSED, UNUSED, UNUSED,
};

// ---- Resonator
static const char* const kResModels[] = { "String", "Tube", "Bar", "Bell", "Membrane", "Glass" };
static const char* const kResExciters[] = { "Impulse", "Noise", "Mallet", "Bow" };
const ParamDesc kResonatorParams[kEngineParams] = {
    DISC ("model", "Model", kResModels, 0, "Resonating body"),
    DISC ("exciter", "Exciter", kResExciters, 2, "What strikes or drives the body"),
    { "excdecay", "Exc.Decay", 0.0005f, 1.f, 0.01f, PD::Exp, "s", nullptr, 0, "Exciter burst length" },
    { "exccolor", "Exc.Color", 0.f, 1.f, 0.6f, PD::Lin, "", nullptr, 0, "Exciter brightness" },
    { "decay",  "Decay",  0.02f, 20.f, 1.5f, PD::Exp, "s", nullptr, 0, "Ring time" },
    { "damp",   "Damping", 0.f, 1.f, 0.4f, PD::Lin, "", nullptr, 0, "High-frequency loss: bright metal to dull wood" },
    { "struct", "Structure", 0.f, 1.f, 0.2f, PD::Lin, "", nullptr, 0, "Inharmonicity / stiffness / mode stretch" },
    { "position", "Position", 0.f, 1.f, 0.3f, PD::Lin, "", nullptr, 0, "Strike position: which modes are excited" },
    { "spread", "Spread", 0.f, 1.f, 0.3f, PD::Lin, "", nullptr, 0, "Stereo spread of the modes" },
    { "bow",    "Sustain", 0.f, 1.f, 0.f, PD::Lin, "", nullptr, 0, "Continuous excitation level while the gate is held" },
    { "penv",   "P.Env",  -24.f, 24.f, 0.f, PD::Lin, "st", nullptr, 0, "Pitch bend depth after the strike" },
    { "penvt",  "P.Time", 0.002f, 2.f, 0.05f, PD::Exp, "s", nullptr, 0, "Pitch bend time" },
    { "partials", "Partials", 2.f, 24.f, 12.f, PD::Discrete, "", nullptr, 0, "Number of modes (modal models)" },
    { "hard",   "Hardness", 0.f, 1.f, 0.5f, PD::Lin, "", nullptr, 0, "How much velocity brightens the strike" },
    UNUSED, UNUSED,
};

// ---- Synth
static const char* const kSynthModes[] = { "Analog", "WT Basic", "WT Vowel", "WT Metal", "WT Digital", "WT Organ" };
static const char* const kUnison[] = { "1", "2", "3", "5", "7" };
static const char* const kPhaseModes[] = { "Free", "Reset" };
const ParamDesc kSynthParams[kEngineParams] = {
    { "wave1",  "Wave 1", 0.f, 1.f, 0.4f, PD::Lin, "", nullptr, 0, "Osc 1 shape: triangle > saw > square > pulse, or wavetable position" },
    DISC ("mode", "Osc Mode", kSynthModes, 0, "Analog-style oscillators or a wavetable set"),
    { "wave2",  "Wave 2", 0.f, 1.f, 0.4f, PD::Lin, "", nullptr, 0, "Osc 2 shape / wavetable position" },
    { "pitch2", "Osc2 Pitch", -24.f, 24.f, 0.f, PD::Discrete, "st", nullptr, 0, "Osc 2 pitch offset" },
    { "detune", "Detune", 0.f, 60.f, 7.f, PD::Pow2, "ct", nullptr, 0, "Osc 2 detune" },
    { "mix",    "Mix",    0.f, 1.f, 0.f, PD::Lin, "", nullptr, 0, "Osc 1 to osc 2 balance" },
    { "sub",    "Sub",    0.f, 1.f, 0.f, PD::Lin, "", nullptr, 0, "Sub oscillator level (one octave down)" },
    { "noise",  "Noise",  0.f, 1.f, 0.f, PD::Lin, "", nullptr, 0, "Noise level" },
    { "pw",     "PW/Warp", 0.f, 1.f, 0.5f, PD::Lin, "", nullptr, 0, "Pulse width (analog) or phase warp (wavetable)" },
    { "sync",   "Sync",   1.f, 8.f, 1.f, PD::Exp, "x", nullptr, 0, "Hard-sync ratio for osc 2 (1 = off)" },
    { "fm",     "FM",     0.f, 1.f, 0.f, PD::Pow2, "", nullptr, 0, "Osc 2 modulates osc 1 phase" },
    DISC ("unison", "Unison", kUnison, 0, "Stacked voices"),
    { "unidet", "Uni Det", 0.f, 100.f, 18.f, PD::Pow2, "ct", nullptr, 0, "Unison detune spread" },
    { "drift",  "Drift",  0.f, 1.f, 0.1f, PD::Lin, "", nullptr, 0, "Slow pitch drift, wow and flutter" },
    { "width",  "Width",  0.f, 1.f, 0.5f, PD::Lin, "", nullptr, 0, "Stereo spread of unison voices and oscillators" },
    DISC ("phase", "Phase", kPhaseModes, 0, "Reset oscillator phase on each note"),
};

// ============================================================================ effects
static const char* const kStutterDir[] = { "Fwd", "Alt", "Rev" };
static const char* const kStutterLens[] = { "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T", "1/64", "1/128" };
static const char* const kReverseLens[] = { "2 bar", "1 bar", "1/2", "1/4", "1/8", "1/16" };
const ParamDesc kFxStutter[kFxParams] = {
    { "on",     "On",     0.f, 1.f, 0.f, PD::Discrete, "", kOffOn, 2, "Engage the repeat (lock per step)" },
    DISC ("length", "Length", kStutterLens, 7, "Repeat length (the first slot plays live while it is captured, so a one-step lock needs a slot shorter than the step)"),
    { "pitch",  "Pitch",  -12.f, 12.f, 0.f, PD::Lin, "st", nullptr, 0, "Pitch change per repeat" },
    { "decay",  "Decay",  0.f, 1.f, 0.f, PD::Lin, "", nullptr, 0, "Level drop per repeat" },
    { "shrink", "Shrink", 0.5f, 1.f, 1.f, PD::Lin, "x", nullptr, 0, "Each repeat is this much shorter (accelerating repeats)" },
    DISC ("dir", "Direction", kStutterDir, 0, "Forward, alternating or reversed repeats"),
    { "gate",   "Gate",   0.1f, 1.f, 1.f, PD::Lin, "", nullptr, 0, "Length of each repeat within its slot" },
    { "smooth", "Smooth", 0.f, 1.f, 0.3f, PD::Lin, "", nullptr, 0, "Crossfade length at repeat edges" },
};
const ParamDesc kFxReverse[kFxParams] = {
    { "on",     "On",     0.f, 1.f, 0.f, PD::Discrete, "", kOffOn, 2, "Engage reverse playback" },
    DISC ("length", "Length", kReverseLens, 3, "Reversed chunk length"),
    { "speed",  "Speed",  0.25f, 4.f, 1.f, PD::Exp, "x", nullptr, 0, "Reverse playback speed" },
    { "fade",   "Fade",   0.f, 1.f, 0.3f, PD::Lin, "", nullptr, 0, "Crossfade at chunk edges" },
    { "feedback", "Feedback", 0.f, 0.95f, 0.f, PD::Lin, "", nullptr, 0, "Feed reversed audio back into the buffer" },
    UNUSED, UNUSED, UNUSED,
};
const ParamDesc kFxTapeStop[kFxParams] = {
    { "on",     "Stop",   0.f, 1.f, 0.f, PD::Discrete, "", kOffOn, 2, "Stop the tape (release to spin back up)" },
    { "stopt",  "Stop Time", 0.01f, 4.f, 0.25f, PD::Exp, "s", nullptr, 0, "Time to come to rest" },
    { "startt", "Start Time", 0.005f, 2.f, 0.06f, PD::Exp, "s", nullptr, 0, "Time to return to speed" },
    { "curve",  "Curve",  0.f, 1.f, 0.5f, PD::Lin, "", nullptr, 0, "Deceleration curve: linear to exponential" },
    { "sync",   "Sync",   0.f, 1.f, 0.f, PD::Discrete, "", kOffOn, 2, "Stop time follows tempo (time knob picks a division)" },
    { "wow",    "Wow",    0.f, 1.f, 0.f, PD::Pow2, "", nullptr, 0, "Constant pitch wobble" },
    UNUSED, UNUSED,
};
static const char* const kCrushModes[] = { "Round", "Trunc", "Xor", "And" };
const ParamDesc kFxCrush[kFxParams] = {
    { "bits",   "Bits",   1.f, 16.f, 16.f, PD::Lin, "bit", nullptr, 0, "Bit depth" },
    { "rate",   "Rate",   200.f, 48000.f, 48000.f, PD::Exp, "Hz", nullptr, 0, "Sample-rate reduction" },
    { "jitter", "Jitter", 0.f, 1.f, 0.f, PD::Lin, "", nullptr, 0, "Random variation of the hold length" },
    { "drive",  "Drive",  0.f, 36.f, 0.f, PD::Lin, "dB", nullptr, 0, "Input gain into the quantiser" },
    DISC ("mode", "Mode", kCrushModes, 0, "Quantiser behaviour"),
    { "smooth", "Smooth", 0.f, 1.f, 0.f, PD::Lin, "", nullptr, 0, "Interpolate held samples (softer aliasing)" },
    { "tone",   "Tone",   200.f, 20000.f, 20000.f, PD::Exp, "Hz", nullptr, 0, "Low-pass after the crusher" },
    UNUSED,
};
static const char* const kDistTypes[] = { "Tanh", "Hard", "Fold", "SineFold", "Tube", "Rectify", "Fuzz" };
const ParamDesc kFxDistort[kFxParams] = {
    { "drive",  "Drive",  0.f, 48.f, 12.f, PD::Lin, "dB", nullptr, 0, "Input gain" },
    DISC ("type", "Type", kDistTypes, 0, "Shaper"),
    { "tone",   "Tone",   -1.f, 1.f, 0.f, PD::Lin, "", nullptr, 0, "Tilt: dark to bright" },
    { "bias",   "Bias",   -1.f, 1.f, 0.f, PD::Lin, "", nullptr, 0, "Asymmetry (even harmonics)" },
    { "lowcut", "Low Cut", 10.f, 2000.f, 20.f, PD::Exp, "Hz", nullptr, 0, "High-pass before the shaper" },
    { "output", "Output", -24.f, 12.f, 0.f, PD::Lin, "dB", nullptr, 0, "Output trim (drive is partly compensated automatically)" },
    UNUSED, UNUSED,
};
const ParamDesc kFxComb[kFxParams] = {
    { "note",   "Pitch",  12.f, 108.f, 48.f, PD::Lin, "nt", nullptr, 0, "Comb pitch (MIDI note)" },
    { "feedback", "Feedback", -0.99f, 0.99f, 0.8f, PD::Lin, "", nullptr, 0, "Feedback; negative gives the hollow octave-down comb" },
    { "damp",   "Damp",   0.f, 1.f, 0.3f, PD::Lin, "", nullptr, 0, "High-frequency loss in the loop" },
    { "spread", "Spread", 0.f, 1.f, 0.1f, PD::Pow2, "", nullptr, 0, "Left/right detune" },
    { "modrate", "Mod Rate", 0.02f, 20.f, 0.5f, PD::Exp, "Hz", nullptr, 0, "Pitch modulation rate" },
    { "moddepth", "Mod Depth", 0.f, 1.f, 0.f, PD::Pow2, "", nullptr, 0, "Pitch modulation depth" },
    UNUSED, UNUSED,
};
static const char* const kChords[] = { "Unison", "Octaves", "Fifth", "Minor", "Major", "Sus4", "Min7", "Maj7", "Min9", "Whole", "Cluster", "Fourths" };
const ParamDesc kFxResonator[kFxParams] = {
    { "note",   "Root",   24.f, 96.f, 48.f, PD::Discrete, "nt", nullptr, 0, "Root note" },
    DISC ("chord", "Chord", kChords, 3, "Tuning of the resonators"),
    { "decay",  "Decay",  0.05f, 20.f, 2.f, PD::Exp, "s", nullptr, 0, "Ring time" },
    { "damp",   "Damp",   0.f, 1.f, 0.4f, PD::Lin, "", nullptr, 0, "High-frequency loss" },
    { "detune", "Detune", 0.f, 1.f, 0.1f, PD::Pow2, "", nullptr, 0, "Left/right detune" },
    { "input",  "Input",  -24.f, 12.f, -6.f, PD::Lin, "dB", nullptr, 0, "Input gain into the resonators" },
    { "count",  "Voices", 1.f, 6.f, 4.f, PD::Discrete, "", nullptr, 0, "Number of resonators" },
    UNUSED,
};
const ParamDesc kFxFreqShift[kFxParams] = {
    { "shift",  "Shift",  -1.f, 1.f, 0.1f, PD::Lin, "", nullptr, 0, "Frequency shift, bipolar and exponential (to +-5 kHz)" },
    { "feedback", "Feedback", 0.f, 0.95f, 0.f, PD::Lin, "", nullptr, 0, "Feedback through a short delay (barber-pole spirals)" },
    { "delay",  "Delay",  0.001f, 0.5f, 0.05f, PD::Exp, "s", nullptr, 0, "Feedback path delay" },
    { "spread", "Spread", 0.f, 1.f, 0.f, PD::Lin, "", nullptr, 0, "Opposite shift on the right channel" },
    { "lforate", "LFO Rate", 0.02f, 20.f, 0.3f, PD::Exp, "Hz", nullptr, 0, "Shift modulation rate" },
    { "lfodepth", "LFO Depth", 0.f, 1.f, 0.f, PD::Pow2, "", nullptr, 0, "Shift modulation depth" },
    UNUSED, UNUSED,
};
static const char* const kRingWaves[] = { "Sine", "Triangle", "Square", "S&H" };
const ParamDesc kFxRingMod[kFxParams] = {
    { "freq",   "Freq",   1.f, 8000.f, 220.f, PD::Exp, "Hz", nullptr, 0, "Carrier frequency" },
    DISC ("wave", "Wave", kRingWaves, 0, "Carrier waveform"),
    { "lforate", "LFO Rate", 0.02f, 20.f, 0.5f, PD::Exp, "Hz", nullptr, 0, "Carrier frequency modulation rate" },
    { "lfodepth", "LFO Depth", 0.f, 1.f, 0.f, PD::Pow2, "", nullptr, 0, "Carrier frequency modulation depth" },
    { "follow", "Follow", -1.f, 1.f, 0.f, PD::Lin, "", nullptr, 0, "Input level moves the carrier frequency" },
    { "phase",  "Stereo", 0.f, 1.f, 0.f, PD::Lin, "", nullptr, 0, "Carrier phase offset between channels" },
    { "am",     "AM",     0.f, 1.f, 0.f, PD::Lin, "", nullptr, 0, "Blend from ring modulation to amplitude modulation" },
    UNUSED,
};
const ParamDesc kFxSpectral[kFxParams] = {
    { "freeze", "Freeze", 0.f, 1.f, 0.f, PD::Discrete, "", kOffOn, 2, "Hold the current spectrum (lock per step)" },
    { "blur",   "Blur",   0.f, 1.f, 0.5f, PD::Lin, "", nullptr, 0, "Smear the spectrum over time" },
    { "shift",  "Shift",  -1.f, 1.f, 0.f, PD::Lin, "", nullptr, 0, "Shift partials up or down" },
    { "stretch", "Stretch", 0.5f, 2.f, 1.f, PD::Exp, "x", nullptr, 0, "Stretch or compress the spectrum" },
    { "scramble", "Scramble", 0.f, 1.f, 0.f, PD::Lin, "", nullptr, 0, "Randomise phases (washes transients away)" },
    { "gate",   "Gate",   0.f, 1.f, 0.f, PD::Pow2, "", nullptr, 0, "Remove quiet partials, leaving peaks" },
    { "locut",  "Lo Cut", 20.f, 5000.f, 20.f, PD::Exp, "Hz", nullptr, 0, "Remove bins below" },
    { "hicut",  "Hi Cut", 200.f, 20000.f, 20000.f, PD::Exp, "Hz", nullptr, 0, "Remove bins above" },
};
const ParamDesc kFxGrainDelay[kFxParams] = {
    { "time",   "Time",   0.01f, 2.f, 0.25f, PD::Exp, "s", nullptr, 0, "Delay time" },
    { "size",   "Size",   0.005f, 0.5f, 0.08f, PD::Exp, "s", nullptr, 0, "Grain length" },
    { "density", "Density", 1.f, 100.f, 20.f, PD::Exp, "/s", nullptr, 0, "Grains per second" },
    { "spray",  "Spray",  0.f, 1.f, 0.2f, PD::Pow2, "", nullptr, 0, "Random delay time per grain" },
    { "pitch",  "Pitch",  -24.f, 24.f, 0.f, PD::Lin, "st", nullptr, 0, "Grain pitch shift" },
    { "scatter", "Scatter", 0.f, 12.f, 0.f, PD::Pow2, "st", nullptr, 0, "Random pitch per grain" },
    { "feedback", "Feedback", 0.f, 0.95f, 0.3f, PD::Lin, "", nullptr, 0, "Feedback" },
    { "revprob", "Rev Prob", 0.f, 1.f, 0.f, PD::Lin, "", nullptr, 0, "Chance a grain plays backwards" },
};
const ParamDesc kFxDelay[kFxParams] = {
    { "time",   "Time",   0.f, 1.f, 0.5f, PD::Lin, "", nullptr, 0, "Delay time (1 ms to 2 s, or tempo divisions when synced)" },
    { "sync",   "Sync",   0.f, 1.f, 1.f, PD::Discrete, "", kOffOn, 2, "Tempo-sync the delay time" },
    { "feedback", "Feedback", 0.f, 1.05f, 0.4f, PD::Lin, "", nullptr, 0, "Feedback (above 1 self-oscillates into the saturator)" },
    { "modrate", "Mod Rate", 0.02f, 20.f, 0.4f, PD::Exp, "Hz", nullptr, 0, "Time modulation rate" },
    { "moddepth", "Mod Depth", 0.f, 1.f, 0.1f, PD::Pow2, "", nullptr, 0, "Time modulation depth (chorus to wobble)" },
    { "tone",   "Tone",   -1.f, 1.f, -0.2f, PD::Lin, "", nullptr, 0, "Feedback filter: darker or thinner each repeat" },
    { "width",  "Width",  0.f, 1.f, 0.7f, PD::Lin, "", nullptr, 0, "Ping-pong amount / stereo offset" },
    { "drive",  "Drive",  0.f, 1.f, 0.2f, PD::Lin, "", nullptr, 0, "Saturation in the feedback loop" },
};
const ParamDesc kFxPlate[kFxParams] = {
    { "predelay", "PreDelay", 0.f, 0.25f, 0.01f, PD::Pow2, "s", nullptr, 0, "Delay before the reverb" },
    { "decay",  "Decay",  0.1f, 30.f, 1.8f, PD::Exp, "s", nullptr, 0, "Decay time" },
    { "size",   "Size",   0.3f, 1.6f, 1.f, PD::Lin, "x", nullptr, 0, "Plate size" },
    { "damp",   "Damp",   0.f, 1.f, 0.4f, PD::Lin, "", nullptr, 0, "High-frequency damping" },
    { "diffusion", "Diffuse", 0.f, 1.f, 0.75f, PD::Lin, "", nullptr, 0, "Input diffusion" },
    { "mod",    "Mod",    0.f, 1.f, 0.3f, PD::Lin, "", nullptr, 0, "Tank modulation" },
    { "lowcut", "Low Cut", 20.f, 1000.f, 120.f, PD::Exp, "Hz", nullptr, 0, "High-pass on the reverb input" },
    { "width",  "Width",  0.f, 1.f, 1.f, PD::Lin, "", nullptr, 0, "Stereo width" },
};
const ParamDesc kFxCloud[kFxParams] = {
    { "size",   "Size",   0.2f, 4.f, 1.5f, PD::Exp, "x", nullptr, 0, "Space size" },
    { "decay",  "Decay",  0.3f, 120.f, 8.f, PD::Exp, "s", nullptr, 0, "Decay time (maximum freezes)" },
    { "diffusion", "Diffuse", 0.f, 1.f, 0.8f, PD::Lin, "", nullptr, 0, "Diffusion: echoes to wash" },
    { "damphi", "Hi Damp", 500.f, 20000.f, 6000.f, PD::Exp, "Hz", nullptr, 0, "High-frequency damping" },
    { "damplo", "Lo Damp", 20.f, 1000.f, 80.f, PD::Exp, "Hz", nullptr, 0, "Low-frequency damping" },
    { "mod",    "Mod",    0.f, 1.f, 0.4f, PD::Lin, "", nullptr, 0, "Delay-line modulation (chorused tail)" },
    { "shimmer", "Shimmer", 0.f, 1.f, 0.f, PD::Lin, "", nullptr, 0, "Octave-up feedback into the tail" },
    { "predelay", "PreDelay", 0.f, 0.5f, 0.02f, PD::Pow2, "s", nullptr, 0, "Delay before the reverb" },
};
static const char* const kScSources[] = { "Self", "Track 1", "Track 2", "Track 3", "Track 4", "Track 5", "Track 6", "Track 7", "Track 8", "Ext In" };
const ParamDesc kFxCompressor[kFxParams] = {
    { "thresh", "Thresh", -60.f, 0.f, -18.f, PD::Lin, "dB", nullptr, 0, "Threshold" },
    { "ratio",  "Ratio",  1.f, 20.f, 4.f, PD::Exp, ":1", nullptr, 0, "Ratio" },
    { "attack", "Attack", 0.0001f, 0.2f, 0.005f, PD::Exp, "s", nullptr, 0, "Attack time" },
    { "release", "Release", 0.01f, 2.f, 0.12f, PD::Exp, "s", nullptr, 0, "Release time" },
    { "makeup", "Makeup", 0.f, 24.f, 6.f, PD::Lin, "dB", nullptr, 0, "Make-up gain" },
    { "knee",   "Knee",   0.f, 24.f, 6.f, PD::Lin, "dB", nullptr, 0, "Knee width" },
    DISC ("source", "Key", kScSources, 0, "Sidechain source: this signal, another track, or the external input"),
    { "schpf",  "Key HPF", 20.f, 2000.f, 20.f, PD::Exp, "Hz", nullptr, 0, "High-pass on the sidechain" },
};
static const char* const kFxFilterTypes[] = { "LP12", "LP24", "BP", "HP12", "HP24", "Notch", "Peak" };
const ParamDesc kFxFilter[kFxParams] = {
    DISC ("type", "Type", kFxFilterTypes, 0, "Filter response"),
    { "cutoff", "Cutoff", 20.f, 20000.f, 1200.f, PD::Exp, "Hz", nullptr, 0, "Cutoff frequency" },
    { "res",    "Reso",   0.f, 1.f, 0.2f, PD::Lin, "", nullptr, 0, "Resonance" },
    { "drive",  "Drive",  0.f, 24.f, 0.f, PD::Lin, "dB", nullptr, 0, "Drive into the filter" },
    { "lforate", "LFO Rate", 0.02f, 40.f, 1.f, PD::Exp, "Hz", nullptr, 0, "Cutoff modulation rate" },
    { "lfodepth", "LFO Depth", 0.f, 1.f, 0.f, PD::Lin, "", nullptr, 0, "Cutoff modulation depth (up to 4 octaves)" },
    { "follow", "Follow", -1.f, 1.f, 0.f, PD::Lin, "", nullptr, 0, "Input level moves the cutoff" },
    { "spread", "Stereo", -1.f, 1.f, 0.f, PD::Lin, "", nullptr, 0, "Cutoff offset between channels" },
};
static const ParamDesc kFxNoneParams[kFxParams] = { UNUSED, UNUSED, UNUSED, UNUSED, UNUSED, UNUSED, UNUSED, UNUSED };
static const ParamDesc kFxMix = { "mix", "Mix", 0.f, 1.f, 1.f, PD::Lin, "", nullptr, 0, "Dry/wet" };

// ============================================================================ common track params
static const char* const kFilterTypes[] = { "Off", "LP12", "LP24", "BP", "HP12", "HP24", "Notch", "Ladder" };
static const char* const kDriveTypes[]  = { "Soft", "Hard", "Fold", "Tube", "Rectify" };
static const char* const kDrivePos[]    = { "Pre", "Post" };
static const char* const kLfoShapes[]   = { "Sine", "Triangle", "Saw", "Ramp", "Square", "S&H", "Smooth", "Draw" };
static const char* const kLfoModes[]    = { "Free", "Trig", "One-shot" };

#define LFO_BLOCK(n) \
    { "lfo" #n "rate",  "Rate",  0.f, 1.f, 0.45f, PD::Lin, "", nullptr, 0, "LFO rate (0.01-200 Hz free, or a tempo division when synced)" }, \
    DISC ("lfo" #n "shape", "Shape", kLfoShapes, 0, "LFO waveform"), \
    { "lfo" #n "sync",  "Sync",  0.f, 1.f, 1.f, PD::Discrete, "", kOffOn, 2, "Tempo-sync the LFO" }, \
    { "lfo" #n "phase", "Phase", 0.f, 1.f, 0.f, PD::Lin, "", nullptr, 0, "Start phase" }, \
    DISC ("lfo" #n "mode", "Mode", kLfoModes, 0, "Free running (locked to song position when synced), retriggered per note, or one cycle per note")

static const ParamDesc kTrackCommon[TP::count] = {
    { "level",  "Level",  0.f, 1.f, 0.7071f, PD::Lin, "", nullptr, 0, "Track level" },
    { "pan",    "Pan",    -1.f, 1.f, 0.f, PD::Lin, "", nullptr, 0, "Pan" },
    { "coarse", "Tune",   -36.f, 36.f, 0.f, PD::Lin, "st", nullptr, 0, "Coarse tuning" },
    { "fine",   "Fine",   -100.f, 100.f, 0.f, PD::Lin, "ct", nullptr, 0, "Fine tuning" },
    { "glide",  "Glide",  0.f, 2.f, 0.f, PD::Pow3, "s", nullptr, 0, "Portamento time (slides always glide)" },
    UNUSED, UNUSED, UNUSED, UNUSED, UNUSED, UNUSED, UNUSED, UNUSED, UNUSED, UNUSED, UNUSED, UNUSED, UNUSED, UNUSED, UNUSED, UNUSED,
    { "ampa",   "Attack", 0.0001f, 10.f, 0.0005f, PD::Exp, "s", nullptr, 0, "Amp attack" },
    { "amph",   "Hold",   0.f, 2.f, 0.f, PD::Pow3, "s", nullptr, 0, "Amp hold" },
    { "ampd",   "Decay",  0.001f, 20.f, 1.f, PD::Exp, "s", nullptr, 0, "Amp decay" },
    { "amps",   "Sustain", 0.f, 1.f, 1.f, PD::Lin, "", nullptr, 0, "Amp sustain" },
    { "ampr",   "Release", 0.001f, 20.f, 0.05f, PD::Exp, "s", nullptr, 0, "Amp release (after the step's gate ends)" },
    { "ampvel", "Vel>Amp", 0.f, 1.f, 0.7f, PD::Lin, "", nullptr, 0, "How much velocity changes loudness" },
    { "moda",   "Attack", 0.0001f, 10.f, 0.0005f, PD::Exp, "s", nullptr, 0, "Mod envelope attack" },
    { "modd",   "Decay",  0.001f, 20.f, 0.2f, PD::Exp, "s", nullptr, 0, "Mod envelope decay" },
    { "mods",   "Sustain", 0.f, 1.f, 0.f, PD::Lin, "", nullptr, 0, "Mod envelope sustain" },
    { "modr",   "Release", 0.001f, 20.f, 0.2f, PD::Exp, "s", nullptr, 0, "Mod envelope release" },
    DISC ("flttype", "Filter", kFilterTypes, 0, "Filter type"),
    { "cutoff", "Cutoff", 20.f, 20000.f, 20000.f, PD::Exp, "Hz", nullptr, 0, "Filter cutoff" },
    { "res",    "Reso",   0.f, 1.f, 0.1f, PD::Lin, "", nullptr, 0, "Filter resonance" },
    { "fltenv", "Env",    -1.f, 1.f, 0.f, PD::Lin, "", nullptr, 0, "Mod envelope to cutoff (up to 8 octaves)" },
    { "fltkey", "Key",    0.f, 1.f, 0.f, PD::Lin, "", nullptr, 0, "Cutoff follows note pitch" },
    { "drive",  "Drive",  0.f, 1.f, 0.f, PD::Lin, "", nullptr, 0, "Voice drive" },
    DISC ("drvtype", "Type", kDriveTypes, 0, "Drive character"),
    DISC ("drvpos", "Position", kDrivePos, 0, "Drive before or after the filter"),
    { "senda",  "Send A", 0.f, 1.f, 0.f, PD::Pow2, "", nullptr, 0, "Send to bus A" },
    { "sendb",  "Send B", 0.f, 1.f, 0.f, PD::Pow2, "", nullptr, 0, "Send to bus B" },
    UNUSED, UNUSED, UNUSED, UNUSED, UNUSED, UNUSED, UNUSED, UNUSED, UNUSED,
    UNUSED, UNUSED, UNUSED, UNUSED, UNUSED, UNUSED, UNUSED, UNUSED, UNUSED,
    LFO_BLOCK (1),
    LFO_BLOCK (2),
    { "stepglide", "Glide", 0.f, 1.f, 0.f, PD::Lin, "", nullptr, 0, "Smoothing between step-mod values" },
    { "glitchsens", "Glitch", 0.f, 1.f, 0.5f, PD::Lin, "", nullptr, 0, "How strongly the Glitch macro hits this track" },
    { "accent", "Accent", 0.f, 1.f, 0.f, PD::Lin, "", nullptr, 0, "Hard hits open the filter and push the drive" },
};

static const char* const kChaosTypes[] = { "Lorenz", "Logistic" };
static const char* const kEvolveRates[] = { "1 bar", "2 bars", "4 bars", "8 bars", "16 bars" };

static const ParamDesc kGlobalCommon[GP::SendFx0] = {
    { "master", "Master", 0.f, 1.f, 0.7071f, PD::Lin, "", nullptr, 0, "Master level" },
    { "density", "Density", -1.f, 1.f, 0.f, PD::Lin, "", nullptr, 0, "Thin the patterns out or add ghost hits" },
    { "complexity", "Complexity", 0.f, 1.f, 0.f, PD::Lin, "", nullptr, 0, "Add ratchets and micro-timing" },
    { "mutation", "Mutation", 0.f, 1.f, 0.f, PD::Lin, "", nullptr, 0, "How much each loop differs from the written pattern" },
    { "chaos",  "Chaos",  0.f, 1.f, 0.f, PD::Lin, "", nullptr, 0, "Depth of the chaos source on pitch, timbre and timing" },
    { "glitch", "Glitch", 0.f, 1.f, 0.f, PD::Lin, "", nullptr, 0, "Chance and intensity of stutters, reverses, stops and crushes" },
    { "space",  "Space",  0.f, 1.f, 0.5f, PD::Lin, "", nullptr, 0, "Scales all sends and lengthens the tails" },
    { "macro1", "Macro 1", 0.f, 1.f, 0.f, PD::Lin, "", nullptr, 0, "Assignable macro" },
    { "macro2", "Macro 2", 0.f, 1.f, 0.f, PD::Lin, "", nullptr, 0, "Assignable macro" },
    { "macro3", "Macro 3", 0.f, 1.f, 0.f, PD::Lin, "", nullptr, 0, "Assignable macro" },
    { "macro4", "Macro 4", 0.f, 1.f, 0.f, PD::Lin, "", nullptr, 0, "Assignable macro" },
    { "swing",  "Swing",  0.25f, 0.8f, 0.5f, PD::Lin, "%", nullptr, 0, "Global swing (50% is straight)" },
    { "tempo",  "Tempo",  30.f, 300.f, 140.f, PD::Lin, "bpm", nullptr, 0, "Internal tempo (used when the host is not playing)" },
    { "chaosrate", "Rate", 0.01f, 50.f, 0.5f, PD::Exp, "Hz", nullptr, 0, "Chaos speed" },
    DISC ("chaostype", "Type", kChaosTypes, 0, "Chaos system"),
    { "chaosshape", "Shape", 0.f, 1.f, 0.5f, PD::Lin, "", nullptr, 0, "Lorenz rho / logistic r: orderly to wild" },
    { "followatk", "Attack", 0.0001f, 0.5f, 0.005f, PD::Exp, "s", nullptr, 0, "Envelope follower attack" },
    { "followrel", "Release", 0.005f, 5.f, 0.15f, PD::Exp, "s", nullptr, 0, "Envelope follower release" },
    { "followgain", "Gain", 0.f, 36.f, 0.f, PD::Lin, "dB", nullptr, 0, "Envelope follower input gain" },
    DISC ("evolverate", "Every", kEvolveRates, 1, "How often evolve mutates unlocked tracks"),
    { "evolveamt", "Amount", 0.f, 1.f, 0.25f, PD::Lin, "", nullptr, 0, "How much evolve changes each time" },
    { "fillamt", "Fill", 0.f, 1.f, 0.6f, PD::Lin, "", nullptr, 0, "Intensity of the Fill button" },
    { "breakamt", "Break", 0.f, 1.f, 0.6f, PD::Lin, "", nullptr, 0, "Intensity of the Break button" },
    { "sendaret", "Return A", 0.f, 1.f, 0.7071f, PD::Lin, "", nullptr, 0, "Send bus A return level" },
    { "sendbret", "Return B", 0.f, 1.f, 0.7071f, PD::Lin, "", nullptr, 0, "Send bus B return level" },
    { "limiton", "Limiter", 0.f, 1.f, 1.f, PD::Discrete, "", kOffOn, 2, "Master soft limiter" },
    { "limitdrive", "Drive", 0.f, 18.f, 0.f, PD::Lin, "dB", nullptr, 0, "Gain into the limiter" },
};

// ============================================================================ registry
struct EngineInfo { const char* name; const char* key; const ParamDesc* descs; };
static const EngineInfo kEngines[Engine::count] = {
    { "Drum", "drum", kDrumParams }, { "FM", "fm", kFmParams }, { "Sampler", "sampler", kSamplerParams },
    { "Granular", "granular", kGranularParams }, { "Resonator", "resonator", kResonatorParams }, { "Synth", "synth", kSynthParams },
};
struct FxInfo { const char* name; const char* key; const ParamDesc* descs; };
static const FxInfo kFxInfos[Fx::count] = {
    { "None", "none", kFxNoneParams }, { "Stutter", "stutter", kFxStutter }, { "Reverse", "reverse", kFxReverse },
    { "Tape Stop", "tapestop", kFxTapeStop }, { "Crush", "crush", kFxCrush }, { "Distort", "distort", kFxDistort },
    { "Comb", "comb", kFxComb }, { "Resonator", "resonator", kFxResonator }, { "Freq Shift", "freqshift", kFxFreqShift },
    { "Ring Mod", "ringmod", kFxRingMod }, { "Spectral", "spectral", kFxSpectral }, { "Grain Delay", "graindelay", kFxGrainDelay },
    { "Delay", "delay", kFxDelay }, { "Plate", "plate", kFxPlate }, { "Cloud", "cloud", kFxCloud },
    { "Compressor", "compressor", kFxCompressor }, { "Filter", "filter", kFxFilter },
};

const char* engineName (int e) { return kEngines[clamp (e, 0, Engine::count - 1)].name; }
const char* engineKey (int e)  { return kEngines[clamp (e, 0, Engine::count - 1)].key; }
int engineFromKey (const char* key)
{
    for (int i = 0; i < Engine::count; ++i) if (std::strcmp (kEngines[i].key, key) == 0) return i;
    return -1;
}
const ParamDesc* engineParamDescs (int e) { return kEngines[clamp (e, 0, Engine::count - 1)].descs; }

const char* fxName (int t) { return kFxInfos[clamp (t, 0, Fx::count - 1)].name; }
const char* fxKey (int t)  { return kFxInfos[clamp (t, 0, Fx::count - 1)].key; }
int fxFromKey (const char* key)
{
    for (int i = 0; i < Fx::count; ++i) if (std::strcmp (kFxInfos[i].key, key) == 0) return i;
    return -1;
}
const ParamDesc* fxParamDescs (int t) { return kFxInfos[clamp (t, 0, Fx::count - 1)].descs; }
const ParamDesc& fxMixDesc() { return kFxMix; }

const ParamDesc& trackParamDesc (int tp, const TrackParamContext& ctx)
{
    tp = clamp (tp, 0, TP::count - 1);
    if (tp >= TP::E0 && tp <= TP::E15) return engineParamDescs (ctx.engine)[tp - TP::E0];
    if (tp >= TP::FxA0 && tp <= TP::FxA7) return fxParamDescs (ctx.fxA)[tp - TP::FxA0];
    if (tp == TP::FxAMix) return kFxMix;
    if (tp >= TP::FxB0 && tp <= TP::FxB7) return fxParamDescs (ctx.fxB)[tp - TP::FxB0];
    if (tp == TP::FxBMix) return kFxMix;
    return kTrackCommon[tp];
}

const ParamDesc& commonTrackParamDesc (int tp) { return kTrackCommon[clamp (tp, 0, TP::count - 1)]; }

const ParamDesc& globalParamDesc (int gp, const uint8_t* fxTypes)
{
    gp = clamp (gp, 0, GP::count - 1);
    if (gp < GP::SendFx0) return kGlobalCommon[gp];
    const int rel = gp - GP::SendFx0;
    const int slot = kFirstSendSlot + rel / kFxStride;
    const int idx = rel % kFxStride;
    if (idx == kFxParams) return kFxMix;
    return fxParamDescs (fxTypes ? fxTypes[slot] : 0)[idx];
}

// ---- stable keys
static char gTrackKeys[TP::count][12];
static char gGlobalKeys[GP::count][12];
static bool gKeysBuilt = false;
static void buildKeys()
{
    if (gKeysBuilt) return;
    for (int i = 0; i < TP::count; ++i)
    {
        if (i >= TP::E0 && i <= TP::E15)          std::snprintf (gTrackKeys[i], 12, "e%d", i - TP::E0);
        else if (i >= TP::FxA0 && i <= TP::FxA7)  std::snprintf (gTrackKeys[i], 12, "fxa%d", i - TP::FxA0);
        else if (i == TP::FxAMix)                 std::snprintf (gTrackKeys[i], 12, "fxamix");
        else if (i >= TP::FxB0 && i <= TP::FxB7)  std::snprintf (gTrackKeys[i], 12, "fxb%d", i - TP::FxB0);
        else if (i == TP::FxBMix)                 std::snprintf (gTrackKeys[i], 12, "fxbmix");
        else                                      std::snprintf (gTrackKeys[i], 12, "%s", kTrackCommon[i].key);
    }
    for (int i = 0; i < GP::count; ++i)
    {
        if (i < GP::SendFx0) { std::snprintf (gGlobalKeys[i], 12, "%s", kGlobalCommon[i].key); continue; }
        const int rel = i - GP::SendFx0;
        const int slot = rel / kFxStride, idx = rel % kFxStride;
        static const char* const slotNames[] = { "sa0", "sa1", "sb0", "sb1", "m0", "m1", "m2", "m3" };
        if (idx == kFxParams) std::snprintf (gGlobalKeys[i], 12, "%smix", slotNames[slot]);
        else                  std::snprintf (gGlobalKeys[i], 12, "%sp%d", slotNames[slot], idx);
    }
    gKeysBuilt = true;
}
struct KeyInit { KeyInit() { buildKeys(); } };
static KeyInit gKeyInit;

const char* trackParamKey (int tp)  { buildKeys(); return gTrackKeys[clamp (tp, 0, TP::count - 1)]; }
const char* globalParamKey (int gp) { buildKeys(); return gGlobalKeys[clamp (gp, 0, GP::count - 1)]; }
int trackParamFromKey (const char* key)
{
    buildKeys();
    for (int i = 0; i < TP::count; ++i) if (std::strcmp (gTrackKeys[i], key) == 0) return i;
    return -1;
}
int globalParamFromKey (const char* key)
{
    buildKeys();
    for (int i = 0; i < GP::count; ++i) if (std::strcmp (gGlobalKeys[i], key) == 0) return i;
    return -1;
}

bool trackParamIsStepped (int tp, const TrackParamContext& ctx) { return trackParamDesc (tp, ctx).isDiscrete(); }

// ---- sync divisions
static const SyncDiv kSyncDivs[kNumSyncDivs] = {
    { "16 bar", 64.f }, { "8 bar", 32.f }, { "4 bar", 16.f }, { "2 bar", 8.f }, { "1 bar", 4.f },
    { "1/2.", 3.f }, { "1/2", 2.f }, { "1/2T", 4.f / 3.f }, { "1/4.", 1.5f }, { "1/4", 1.f }, { "1/4T", 2.f / 3.f },
    { "1/8.", 0.75f }, { "1/8", 0.5f }, { "1/8T", 1.f / 3.f }, { "1/16.", 0.375f }, { "1/16", 0.25f }, { "1/16T", 1.f / 6.f },
    { "1/32.", 0.1875f }, { "1/32", 0.125f }, { "1/32T", 1.f / 12.f }, { "1/64", 0.0625f }, { "1/128", 0.03125f },
};
const SyncDiv& syncDiv (int i) { return kSyncDivs[clamp (i, 0, kNumSyncDivs - 1)]; }
int syncDivFromNorm (float n) { return std::min ((int) (clamp01 (n) * (float) kNumSyncDivs), kNumSyncDivs - 1); }

// ---- formatting
void formatParam (const ParamDesc& d, float norm, char* buf, int n)
{
    const float v = d.toPhys (norm);
    if (d.curve == PD::Discrete)
    {
        const int idx = (int) std::lround (v - d.min);
        if (d.labels != nullptr && idx >= 0 && idx < d.numLabels) std::snprintf (buf, (size_t) n, "%s", d.labels[idx]);
        else if (std::strcmp (d.unit, "nt") == 0)
        {
            static const char* const names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            const int note = (int) std::lround (v);
            std::snprintf (buf, (size_t) n, "%s%d", names[wrapMod (note, 12)], note / 12 - 1);
        }
        else std::snprintf (buf, (size_t) n, "%d%s%s", (int) std::lround (v), d.unit[0] ? " " : "", d.unit);
        return;
    }
    const char* u = d.unit;
    if (std::strcmp (u, "Hz") == 0)
    {
        if (v >= 1000.f) std::snprintf (buf, (size_t) n, "%.2f kHz", v / 1000.f);
        else if (v >= 100.f) std::snprintf (buf, (size_t) n, "%.0f Hz", v);
        else std::snprintf (buf, (size_t) n, "%.2f Hz", v);
    }
    else if (std::strcmp (u, "s") == 0)
    {
        if (v < 0.001f) std::snprintf (buf, (size_t) n, "%.2f ms", v * 1000.f);
        else if (v < 1.f) std::snprintf (buf, (size_t) n, "%.0f ms", v * 1000.f);
        else std::snprintf (buf, (size_t) n, "%.2f s", v);
    }
    else if (std::strcmp (u, "%") == 0) std::snprintf (buf, (size_t) n, "%.0f%%", v * 100.f);
    else if (std::strcmp (u, "nt") == 0)
    {
        static const char* const names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const int note = (int) std::lround (v);
        std::snprintf (buf, (size_t) n, "%s%d", names[wrapMod (note, 12)], note / 12 - 1);
    }
    else if (std::strcmp (u, "dB") == 0 || std::strcmp (u, "st") == 0 || std::strcmp (u, "ct") == 0)
        std::snprintf (buf, (size_t) n, "%+.1f %s", v, u);
    else if (std::strcmp (d.key, "level") == 0 || std::strcmp (d.key, "master") == 0
             || std::strcmp (d.key, "sendaret") == 0 || std::strcmp (d.key, "sendbret") == 0)
    {
        const float g = (v * 1.41421356f) * (v * 1.41421356f);
        if (g < 1e-4f) std::snprintf (buf, (size_t) n, "-inf dB");
        else std::snprintf (buf, (size_t) n, "%+.1f dB", gainToDb (g));
    }
    else if (std::strcmp (d.key, "pan") == 0)
    {
        if (std::fabs (v) < 0.005f) std::snprintf (buf, (size_t) n, "C");
        else std::snprintf (buf, (size_t) n, "%d%c", (int) std::lround (std::fabs (v) * 100.f), v < 0 ? 'L' : 'R');
    }
    else if (u[0]) std::snprintf (buf, (size_t) n, "%.2f %s", v, u);
    else std::snprintf (buf, (size_t) n, "%.2f", v);
}

bool parseParam (const ParamDesc& d, const char* text, float& out)
{
    while (*text == ' ') ++text;
    if (d.curve == PD::Discrete && d.labels != nullptr)
    {
        for (int i = 0; i < d.numLabels; ++i)
        {
            const char* a = d.labels[i]; const char* b = text;
            bool eq = true;
            for (; *a && *b; ++a, ++b) if (std::tolower ((unsigned char) *a) != std::tolower ((unsigned char) *b)) { eq = false; break; }
            if (eq && *a == 0 && (*b == 0 || *b == ' ')) { out = d.toNorm (d.min + (float) i); return true; }
        }
    }
    char* end = nullptr;
    double v = std::strtod (text, &end);
    if (end == text) return false;
    while (*end == ' ') ++end;
    if (*end == 'k' || *end == 'K') v *= 1000.0;
    if (std::strcmp (d.unit, "s") == 0 && (end[0] == 'm' && end[1] == 's')) v *= 0.001;
    if (std::strcmp (d.unit, "%") == 0 && v > 1.0) v *= 0.01;
    out = d.toNorm ((float) v);
    return true;
}

} // namespace atm
