// Swoon parameter table. Plain C++ (no JUCE) so the DSP engine, the offline
// render harness, the factory presets and the plugin all share one source of
// truth for IDs, ranges and defaults.
#pragma once

#include <array>
#include <cstddef>
#include <string>

namespace swoon
{

// X-macro: SWOON_PARAM(enumName, "id", "Display name", module, kind, min, max, default, skewMid, "unit", choices)
//  - kind: Float / Int / Bool / Choice
//  - skewMid: value that sits at the knob's centre (0 = linear)
//  - choices: '|' separated list for Choice parameters
#define SWOON_PARAM_LIST(X) \
    /* ---- macros / global -------------------------------------------------------------- */ \
    X(wall,        "wall",        "Wall",            Global,  Float, 0.f, 1.f, 0.5f, 0.f, "%", "") \
    X(wash,        "wash",        "Wash",            Global,  Float, 0.f, 1.f, 0.5f, 0.f, "%", "") \
    X(glide,       "glide",       "Glide",           Global,  Float, 0.f, 1.f, 0.5f, 0.f, "%", "") \
    X(blur,        "blur",        "Blur",            Global,  Float, 0.f, 1.f, 0.5f, 0.f, "%", "") \
    X(warmth,      "warmth",      "Warmth",          Global,  Float, 0.f, 1.f, 0.5f, 0.f, "%", "") \
    X(mix,         "mix",         "Mix",             Global,  Float, 0.f, 1.f, 1.0f, 0.f, "%", "") \
    X(bypass,      "bypass",      "Bypass",          Global,  Bool,  0.f, 1.f, 0.f,  0.f, "",  "") \
    X(inGain,      "inGain",      "Input",           Global,  Float, -24.f, 24.f, 0.f, 0.f, "dB", "") \
    X(inSwell,     "inSwell",     "Swell",           Global,  Float, 0.f, 1.f, 0.f,  0.f, "%", "") \
    /* ---- module order (1 = first). Ties resolve by default order. -------------------- */ \
    X(ordGlide,    "ordGlide",    "Glide Position",  Global,  Int,   1.f, 7.f, 1.f,  0.f, "",  "") \
    X(ordFuzz,     "ordFuzz",     "Fuzz Position",   Global,  Int,   1.f, 7.f, 2.f,  0.f, "",  "") \
    X(ordAmp,      "ordAmp",      "Amp Position",    Global,  Int,   1.f, 7.f, 3.f,  0.f, "",  "") \
    X(ordMod,      "ordMod",      "Mod Position",    Global,  Int,   1.f, 7.f, 4.f,  0.f, "",  "") \
    X(ordFocus,    "ordFocus",    "Focus Position",  Global,  Int,   1.f, 7.f, 5.f,  0.f, "",  "") \
    X(ordReverse,  "ordReverse",  "Reverse Position",Global,  Int,   1.f, 7.f, 6.f,  0.f, "",  "") \
    X(ordWash,     "ordWash",     "Wash Position",   Global,  Int,   1.f, 7.f, 7.f,  0.f, "",  "") \
    /* ---- fuzz ------------------------------------------------------------------------- */ \
    X(fuzzOn,      "fuzzOn",      "Fuzz On",         Fuzz,    Bool,  0.f, 1.f, 1.f,  0.f, "",  "") \
    X(fuzzType,    "fuzzType",    "Fuzz Type",       Fuzz,    Choice,0.f, 2.f, 0.f,  0.f, "",  "Sustain|Octave|Velcro") \
    X(fuzzVoice,   "fuzzVoice",   "Fuzz Voice",      Fuzz,    Choice,0.f, 2.f, 1.f,  0.f, "",  "Flat|Full|Scoop") \
    X(fuzzSustain, "fuzzSustain", "Fuzz Sustain",    Fuzz,    Float, 0.f, 1.f, 0.6f, 0.f, "%", "") \
    X(fuzzTone,    "fuzzTone",    "Fuzz Tone",       Fuzz,    Float, 0.f, 1.f, 0.5f, 0.f, "%", "") \
    X(fuzzGate,    "fuzzGate",    "Fuzz Gate",       Fuzz,    Float, 0.f, 1.f, 0.15f,0.f, "%", "") \
    X(fuzzStack,   "fuzzStack",   "Fuzz Stack",      Fuzz,    Float, 0.f, 1.f, 0.25f, 0.f, "%", "") \
    X(fuzzLevel,   "fuzzLevel",   "Fuzz Level",      Fuzz,    Float, -24.f, 12.f, 0.f, 0.f, "dB", "") \
    X(fuzzMix,     "fuzzMix",     "Fuzz Mix",        Fuzz,    Float, 0.f, 1.f, 1.f,  0.f, "%", "") \
    /* ---- glide ------------------------------------------------------------------------ */ \
    X(glideOn,     "glideOn",     "Glide On",        Glide,   Bool,  0.f, 1.f, 1.f,  0.f, "",  "") \
    X(glideWobble, "glideWobble", "Glide Wobble",    Glide,   Float, 0.f, 60.f, 10.f, 0.f, "ct", "") \
    X(glideRate,   "glideRate",   "Glide Rate",      Glide,   Float, 0.05f, 8.f, 0.7f, 0.7f, "Hz", "") \
    X(glideDrift,  "glideDrift",  "Glide Drift",     Glide,   Float, 0.f, 60.f, 12.f, 0.f, "ct", "") \
    X(glideDip,    "glideDip",    "Glide Dip",       Glide,   Float, 0.f, 200.f, 30.f, 60.f, "ct", "") \
    X(glideReturn, "glideReturn", "Glide Return",    Glide,   Float, 40.f, 2500.f, 350.f, 400.f, "ms", "") \
    X(glideSpread, "glideSpread", "Glide Spread",    Glide,   Float, 0.f, 1.f, 0.25f, 0.f, "%", "") \
    X(glideMix,    "glideMix",    "Glide Mix",       Glide,   Float, 0.f, 1.f, 1.f,  0.f, "%", "") \
    /* ---- reverse ---------------------------------------------------------------------- */ \
    X(revOn,       "revOn",       "Reverse On",      Reverse, Bool,  0.f, 1.f, 0.f,  0.f, "",  "") \
    X(revMode,     "revMode",     "Reverse Mode",    Reverse, Choice,0.f, 2.f, 0.f,  0.f, "",  "Swell|Tape|Both") \
    X(revTime,     "revTime",     "Reverse Time",    Reverse, Float, 60.f, 1600.f, 380.f, 400.f, "ms", "") \
    X(revSync,     "revSync",     "Reverse Sync",    Reverse, Choice,0.f, 6.f, 0.f,  0.f, "",  "Free|1/16|1/8|1/8 dot|1/4|1/4 dot|1/2") \
    X(revFeedback, "revFeedback", "Reverse Feedback",Reverse, Float, 0.f, 0.9f, 0.2f, 0.f, "%", "") \
    X(revSmear,    "revSmear",    "Reverse Smear",   Reverse, Float, 0.f, 1.f, 0.6f, 0.f, "%", "") \
    X(revTone,     "revTone",     "Reverse Tone",    Reverse, Float, 0.f, 1.f, 0.7f, 0.f, "%", "") \
    X(revMix,      "revMix",      "Reverse Mix",     Reverse, Float, 0.f, 1.f, 0.6f, 0.f, "%", "") \
    /* ---- soft focus ------------------------------------------------------------------- */ \
    X(focOn,       "focOn",       "Focus On",        Focus,   Bool,  0.f, 1.f, 1.f,  0.f, "",  "") \
    X(focTime,     "focTime",     "Focus Time",      Focus,   Float, 60.f, 900.f, 250.f, 280.f, "ms", "") \
    X(focSync,     "focSync",     "Focus Sync",      Focus,   Choice,0.f, 6.f, 0.f,  0.f, "",  "Free|1/16|1/8|1/8 dot|1/4|1/4 dot|1/2") \
    X(focFeedback, "focFeedback", "Focus Feedback",  Focus,   Float, 0.f, 0.9f, 0.35f,0.f, "%", "") \
    X(focDetune,   "focDetune",   "Focus Detune",    Focus,   Float, 0.f, 40.f, 9.f, 0.f, "ct", "") \
    X(focHaze,     "focHaze",     "Focus Haze",      Focus,   Float, 0.f, 1.f, 0.5f, 0.f, "%", "") \
    X(focTone,     "focTone",     "Focus Tone",      Focus,   Float, 0.f, 1.f, 0.6f, 0.f, "%", "") \
    X(focMix,      "focMix",      "Focus Mix",       Focus,   Float, 0.f, 1.f, 0.35f,0.f, "%", "") \
    /* ---- modulation ------------------------------------------------------------------- */ \
    X(modOn,       "modOn",       "Mod On",          Mod,     Bool,  0.f, 1.f, 1.f,  0.f, "",  "") \
    X(modMode,     "modMode",     "Mod Mode",        Mod,     Choice,0.f, 4.f, 0.f,  0.f, "",  "Chorus|Ensemble|Vibrato|Flanger|Phaser") \
    X(modRate,     "modRate",     "Mod Rate",        Mod,     Float, 0.02f, 10.f, 0.45f, 0.6f, "Hz", "") \
    X(modDepth,    "modDepth",    "Mod Depth",       Mod,     Float, 0.f, 1.f, 0.5f, 0.f, "%", "") \
    X(modColor,    "modColor",    "Mod Color",       Mod,     Float, 0.f, 1.f, 0.3f, 0.f, "%", "") \
    X(modWidth,    "modWidth",    "Mod Width",       Mod,     Float, 0.f, 1.f, 0.8f, 0.f, "%", "") \
    X(modMix,      "modMix",      "Mod Mix",         Mod,     Float, 0.f, 1.f, 0.5f, 0.f, "%", "") \
    /* ---- wash ------------------------------------------------------------------------- */ \
    X(washOn,      "washOn",      "Wash On",         Wash,    Bool,  0.f, 1.f, 1.f,  0.f, "",  "") \
    X(washDecay,   "washDecay",   "Wash Decay",      Wash,    Float, 0.3f, 60.f, 6.f, 5.f, "s", "") \
    X(washSize,    "washSize",    "Wash Size",       Wash,    Float, 0.f, 1.f, 0.7f, 0.f, "%", "") \
    X(washPre,     "washPre",     "Wash Pre-Delay",  Wash,    Float, 0.f, 250.f, 20.f, 60.f, "ms", "") \
    X(washDamp,    "washDamp",    "Wash High Cut",   Wash,    Float, 800.f, 18000.f, 6500.f, 4500.f, "Hz", "") \
    X(washLow,     "washLow",     "Wash Low Cut",    Wash,    Float, 20.f, 1000.f, 120.f, 160.f, "Hz", "") \
    X(washMod,     "washMod",     "Wash Mod",        Wash,    Float, 0.f, 1.f, 0.45f,0.f, "%", "") \
    X(washShimmer, "washShimmer", "Wash Shimmer",    Wash,    Float, 0.f, 1.f, 0.f,  0.f, "%", "") \
    X(washShimMode,"washShimMode","Shimmer Interval",Wash,    Choice,0.f, 3.f, 0.f,  0.f, "",  "Octave Up|Fifth Up|Octave Down|Up + Down") \
    X(washDuck,    "washDuck",    "Wash Duck",       Wash,    Float, 0.f, 1.f, 0.f,  0.f, "%", "") \
    X(washFreeze,  "washFreeze",  "Wash Freeze",     Wash,    Bool,  0.f, 1.f, 0.f,  0.f, "",  "") \
    X(washMix,     "washMix",     "Wash Mix",        Wash,    Float, 0.f, 1.f, 0.45f,0.f, "%", "") \
    /* ---- amp -------------------------------------------------------------------------- */ \
    X(ampOn,       "ampOn",       "Amp On",          Amp,     Bool,  0.f, 1.f, 1.f,  0.f, "",  "") \
    X(ampDrive,    "ampDrive",    "Amp Drive",       Amp,     Float, 0.f, 1.f, 0.3f, 0.f, "%", "") \
    X(ampBass,     "ampBass",     "Amp Bass",        Amp,     Float, 0.f, 1.f, 0.5f, 0.f, "%", "") \
    X(ampTreble,   "ampTreble",   "Amp Treble",      Amp,     Float, 0.f, 1.f, 0.5f, 0.f, "%", "") \
    X(ampCab,      "ampCab",      "Amp Cabinet",     Amp,     Choice,0.f, 3.f, 1.f,  0.f, "",  "Direct|Open 2x12|Closed 4x12|Chime 1x12") \
    X(ampLevel,    "ampLevel",    "Amp Level",       Amp,     Float, -24.f, 12.f, 0.f, 0.f, "dB", "") \
    X(ampMix,      "ampMix",      "Amp Mix",         Amp,     Float, 0.f, 1.f, 1.f,  0.f, "%", "") \
    /* ---- tape ------------------------------------------------------------------------- */ \
    X(tapeOn,      "tapeOn",      "Tape On",         Tape,    Bool,  0.f, 1.f, 1.f,  0.f, "",  "") \
    X(tapeDrive,   "tapeDrive",   "Tape Saturation", Tape,    Float, 0.f, 1.f, 0.3f, 0.f, "%", "") \
    X(tapeWow,     "tapeWow",     "Tape Wow",        Tape,    Float, 0.f, 1.f, 0.2f, 0.f, "%", "") \
    X(tapeFlutter, "tapeFlutter", "Tape Flutter",    Tape,    Float, 0.f, 1.f, 0.15f,0.f, "%", "") \
    X(tapeAge,     "tapeAge",     "Tape Age",        Tape,    Float, 0.f, 1.f, 0.3f, 0.f, "%", "") \
    X(tapeMix,     "tapeMix",     "Tape Mix",        Tape,    Float, 0.f, 1.f, 1.f,  0.f, "%", "") \
    /* ---- output ----------------------------------------------------------------------- */ \
    X(outTilt,     "outTilt",     "Tilt",            Output,  Float, -1.f, 1.f, 0.f,  0.f, "",  "") \
    X(outWidth,    "outWidth",    "Width",           Output,  Float, 0.f, 2.f, 1.f,  0.f, "%", "") \
    X(outGain,     "outGain",     "Output",          Output,  Float, -24.f, 12.f, 0.f, 0.f, "dB", "")

enum class ParamKind { Float, Int, Bool, Choice };
enum class Module { Global, Glide, Fuzz, Amp, Mod, Focus, Reverse, Wash, Tape, Output };

enum class P : int
{
#define X(name, id, label, module, kind, lo, hi, def, mid, unit, choices) name,
    SWOON_PARAM_LIST(X)
#undef X
    Count
};

inline constexpr int kNumParams = static_cast<int> (P::Count);

struct ParamDef
{
    P           index;
    const char* id;
    const char* name;
    Module      module;
    ParamKind   kind;
    float       min, max, def;
    float       skewMid;   // 0 = linear, else the value at the knob's centre
    const char* unit;
    const char* choices;   // '|' separated
};

const std::array<ParamDef, kNumParams>& paramDefs();
inline const ParamDef& def (P p) { return paramDefs()[static_cast<size_t> (p)]; }

/** Real (denormalised) parameter values, indexed by P. */
struct ParamValues
{
    std::array<float, kNumParams> v {};

    ParamValues();                       // initialises to defaults
    float  operator[] (P p) const { return v[static_cast<size_t> (p)]; }
    float& operator[] (P p)       { return v[static_cast<size_t> (p)]; }
    bool   on (P p) const         { return v[static_cast<size_t> (p)] > 0.5f; }
    int    choice (P p) const     { return static_cast<int> (v[static_cast<size_t> (p)] + 0.5f); }
};

/** Looks a parameter up by its string id; returns P::Count if unknown. */
P paramFromId (const std::string& id);

/** Normalised (0..1) <-> real value conversion honouring skewMid. */
float toNormalised (P p, float realValue);
float fromNormalised (P p, float normalised);

// ----- reorderable modules ---------------------------------------------------------------
inline constexpr int kNumSlots = 7;
enum class Slot : int { Glide = 0, Fuzz, Amp, Mod, Focus, Reverse, Wash };
inline constexpr std::array<P, kNumSlots> kOrderParams { P::ordGlide, P::ordFuzz, P::ordAmp, P::ordMod,
                                                         P::ordFocus, P::ordReverse, P::ordWash };
const char* slotName (Slot s);

/** Resolves the seven position parameters into a processing order (stable on ties). */
std::array<int, kNumSlots> resolveOrder (const ParamValues& pv);

} // namespace swoon
