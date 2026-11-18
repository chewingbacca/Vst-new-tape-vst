// Verdant — parameter table (JUCE-free so the plugin, the offline renderer and
// the tests all share one source of truth).
#pragma once

#include <cmath>
#include <cstdio>
#include <cstring>

namespace verdant
{

enum class PType { Float, Bool, Choice, Int };

// How a value is shown to the user.
enum class PUnit { None, Percent, Semitones, Ms, Hz, Seconds, Db, Ratio, Voices, Octaves, Bipolar };

enum PID : int
{
    // ---- Macros (front page)
    pBloom = 0, pGrowth, pMoisture, pSunlight, pRoots, pMix,

    // ---- Global musical context / modulation
    pKey, pScale, pDrift, pTouch,

    // ---- Tape
    pTapeOn, pTapeMix, pTapeSpeed, pTapeStretch, pTapeWow, pTapeFlutter,
    pTapeDrive, pTapeAge, pTapeStutter, pTapeStutterLen,

    // ---- Grain cloud
    pGrainOn, pGrainMix, pGrainDensity, pGrainSize, pGrainScatter, pGrainPitch,
    pGrainSpread, pGrainReverse, pGrainPan, pGrainShape, pGrainFeedback,

    // ---- Arp / shimmer
    pArpOn, pArpMix, pArpRate, pArpPattern, pArpRange, pArpGate,
    pArpShimmer, pArpWash, pArpHold, pArpTrack,

    // ---- Chorus
    pChorusOn, pChorusMix, pChorusVoices, pChorusRate, pChorusDepth,
    pChorusMode, pChorusWidth, pChorusXover,

    // ---- Reverb
    pRevOn, pRevMix, pRevSize, pRevDecay, pRevPredelay, pRevDamping, pRevLowCut,
    pRevMod, pRevDiffusion, pRevShimmer, pRevShimmerPitch, pRevFreeze,

    // ---- Output
    pOutTilt, pOutWidth, pOutDuck, pOutGain, pBypass,

    kNumParams
};

struct ParamDef
{
    PID          pid;
    const char*  id;        // stable automation / state id
    const char*  name;      // display name
    const char*  group;     // module / page grouping
    PType        type;
    float        min, max, def;
    float        centre;    // skew midpoint (0 = linear)
    PUnit        unit;
    const char* const* choices;
    int          numChoices;
    const char*  help;      // one-line description for tooltips / README
};

inline constexpr const char* kKeyNames[]   = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
inline constexpr const char* kScaleNames[] = { "Major", "Minor", "Dorian", "Lydian", "Mixolydian",
                                               "Major Pentatonic", "Minor Pentatonic", "Whole Tone",
                                               "Major Triad", "Minor Triad", "Major 7", "Minor 7",
                                               "Sus 2/4", "Fifths", "Octaves" };
inline constexpr const char* kDivNames[]   = { "1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32" };
inline constexpr const char* kStutterNames[] = { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32", "Random" };
inline constexpr const char* kPatternNames[] = { "Up", "Down", "Up-Down", "Random", "Bloom" };
inline constexpr const char* kChorusModes[]  = { "Smooth", "Glassy" };
inline constexpr const char* kShimmerPitches[] = { "Octave", "Fifth", "Octave + Fifth", "Two Octaves", "Octave Down" };

// Beats (quarter notes) per step for kDivNames.
inline constexpr double kDivBeats[] = { 2.0, 1.0, 2.0 / 3.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
// Beats per slice for kStutterNames (last = random, handled by the module).
inline constexpr double kStutterBeats[] = { 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125, 0.0 };

inline constexpr float kRevDecayInf = 60.0f; // revDecay above this value means "infinite"

#define VD_F(pid, id, name, group, mn, mx, df, ctr, unit, help) \
    ParamDef { pid, id, name, group, PType::Float, mn, mx, df, ctr, unit, nullptr, 0, help }
#define VD_B(pid, id, name, group, df, help) \
    ParamDef { pid, id, name, group, PType::Bool, 0.0f, 1.0f, df, 0.0f, PUnit::None, nullptr, 0, help }
#define VD_C(pid, id, name, group, arr, df, help) \
    ParamDef { pid, id, name, group, PType::Choice, 0.0f, float (sizeof (arr) / sizeof (arr[0]) - 1), df, 0.0f, PUnit::None, arr, int (sizeof (arr) / sizeof (arr[0])), help }
#define VD_I(pid, id, name, group, mn, mx, df, unit, help) \
    ParamDef { pid, id, name, group, PType::Int, mn, mx, df, 0.0f, unit, nullptr, 0, help }

inline constexpr ParamDef kParams[kNumParams] = {
    // ------------------------------------------------------------------ Macros
    VD_F (pBloom,    "bloom",    "Bloom",    "Macro", 0, 1, 0.5f, 0, PUnit::Percent, "Overall intensity: how much grain, arp, chorus, tape colour and shimmer there is"),
    VD_F (pGrowth,   "growth",   "Growth",   "Macro", 0, 1, 0.5f, 0, PUnit::Percent, "Speed of evolution: drift rates, chorus and reverb motion, grain flux"),
    VD_F (pMoisture, "moisture", "Moisture", "Macro", 0, 1, 0.5f, 0, PUnit::Percent, "Wetness: reverb level, decay and size, how much the arp washes out"),
    VD_F (pSunlight, "sunlight", "Sunlight", "Macro", 0, 1, 0.5f, 0, PUnit::Percent, "Brightness: tilt, reverb damping, shimmer and upward grain sparkle"),
    VD_F (pRoots,    "roots",    "Roots",    "Macro", 0, 1, 0.5f, 0, PUnit::Percent, "Low-end warmth: low shelf, reverb bass, octave-down grains, tape body"),
    VD_F (pMix,      "mix",      "Mix",      "Macro", 0, 1, 0.5f, 0, PUnit::Percent, "Global dry / wet"),

    // ------------------------------------------------------------------ Global
    VD_C (pKey,   "key",   "Key",   "Global", kKeyNames,   0, "Root note of the scale (used when Pitch Track is on)"),
    VD_C (pScale, "scale", "Scale", "Global", kScaleNames, 13, "Scale / chord the arp notes are drawn from"),
    VD_F (pDrift, "drift", "Drift", "Global", 0, 1, 0.5f, 0, PUnit::Percent, "Depth of the slow organic random modulators"),
    VD_F (pTouch, "touch", "Touch", "Global", 0, 1, 0.5f, 0, PUnit::Percent, "How strongly your playing dynamics (envelope follower) move the sound"),

    // ------------------------------------------------------------------ Tape
    VD_B (pTapeOn,      "tapeOn",      "Tape",        "Tape", 1, "Tape module on / off"),
    VD_F (pTapeMix,     "tapeMix",     "Tape Mix",    "Tape", 0, 1, 1.0f, 0, PUnit::Percent, "Blend between the clean input and the tape path"),
    VD_F (pTapeSpeed,   "tapeSpeed",   "Speed",       "Tape", -12, 12, 0.0f, 0, PUnit::Semitones, "Varispeed pitch (granular, stays in time)"),
    VD_F (pTapeStretch, "tapeStretch", "Stretch",     "Tape", 1, 8, 1.0f, 2.5f, PUnit::Ratio, "Granular time-stretch: the tape lags behind and smears, then catches up"),
    VD_F (pTapeWow,     "tapeWow",     "Wow",         "Tape", 0, 1, 0.30f, 0, PUnit::Percent, "Slow pitch drift"),
    VD_F (pTapeFlutter, "tapeFlutter", "Flutter",     "Tape", 0, 1, 0.20f, 0, PUnit::Percent, "Fast pitch shiver"),
    VD_F (pTapeDrive,   "tapeDrive",   "Drive",       "Tape", 0, 1, 0.30f, 0, PUnit::Percent, "Soft tape saturation"),
    VD_F (pTapeAge,     "tapeAge",     "Age",         "Tape", 0, 1, 0.20f, 0, PUnit::Percent, "Worn tape: darker top, head bump, small dropouts"),
    VD_F (pTapeStutter, "tapeStutter", "Stutter",     "Tape", 0, 1, 0.0f, 0, PUnit::Percent, "Chance of an occasional tape stutter / repeat"),
    VD_C (pTapeStutterLen, "tapeStutterLen", "Stutter Size", "Tape", kStutterNames, 3, "Length of the repeated slice"),

    // ------------------------------------------------------------------ Grain cloud
    VD_B (pGrainOn,       "grainOn",       "Grains",     "Grain", 1, "Grain cloud on / off"),
    VD_F (pGrainMix,      "grainMix",      "Grain Mix",  "Grain", 0, 1, 0.45f, 0, PUnit::Percent, "Level of the grain cloud"),
    VD_F (pGrainDensity,  "grainDensity",  "Density",    "Grain", 0.5f, 80, 12.0f, 10.0f, PUnit::Hz, "Grains per second: sparse dew drops to thick foliage"),
    VD_F (pGrainSize,     "grainSize",     "Size",       "Grain", 10, 800, 160.0f, 120.0f, PUnit::Ms, "Grain length"),
    VD_F (pGrainScatter,  "grainScatter",  "Scatter",    "Grain", 0, 1, 0.45f, 0, PUnit::Percent, "How far back in time grains are picked from"),
    VD_F (pGrainPitch,    "grainPitch",    "Pitch",      "Grain", -24, 24, 0.0f, 0, PUnit::Semitones, "Base grain transposition"),
    VD_F (pGrainSpread,   "grainSpread",   "Harmonics",  "Grain", 0, 1, 0.40f, 0, PUnit::Percent, "Chance of grains jumping to octaves, fifths and scale tones"),
    VD_F (pGrainReverse,  "grainReverse",  "Reverse",    "Grain", 0, 1, 0.30f, 0, PUnit::Percent, "Probability a grain plays backwards"),
    VD_F (pGrainPan,      "grainPan",      "Pan Spread", "Grain", 0, 1, 0.85f, 0, PUnit::Percent, "Random stereo placement of grains"),
    VD_F (pGrainShape,    "grainShape",    "Shape",      "Grain", 0, 1, 0.65f, 0, PUnit::Percent, "Grain envelope: plucked droplets to soft swells"),
    VD_F (pGrainFeedback, "grainFeedback", "Regrow",     "Grain", 0, 1, 0.15f, 0, PUnit::Percent, "Feeds the cloud back into its own buffer for cascading layers"),

    // ------------------------------------------------------------------ Arp / shimmer
    VD_B (pArpOn,      "arpOn",      "Arp",        "Arp", 1, "Arp / shimmer module on / off"),
    VD_F (pArpMix,     "arpMix",     "Arp Mix",    "Arp", 0, 1, 0.30f, 0, PUnit::Percent, "Level of the arpeggiated layer"),
    VD_C (pArpRate,    "arpRate",    "Rate",       "Arp", kDivNames, 3, "Tempo-synced step length"),
    VD_C (pArpPattern, "arpPattern", "Pattern",    "Arp", kPatternNames, 4, "Note order: up, down, up-down, random or bloom (an opening chord)"),
    VD_I (pArpRange,   "arpRange",   "Range",      "Arp", 1, 4, 2, PUnit::Octaves, "Octave span of the pattern"),
    VD_F (pArpGate,    "arpGate",    "Gate",       "Arp", 0.1f, 4, 1.2f, 1.0f, PUnit::Ratio, "Note length relative to the step"),
    VD_F (pArpShimmer, "arpShimmer", "Sparkle",    "Arp", 0, 1, 0.30f, 0, PUnit::Percent, "Octave-up doubling on each note"),
    VD_F (pArpWash,    "arpWash",    "Wash",       "Arp", 0, 1, 0.70f, 0, PUnit::Percent, "0 = arp heard directly, 100 = arp only feeds the reverb"),
    VD_F (pArpHold,    "arpHold",    "Linger",     "Arp", 0, 1, 0.55f, 0, PUnit::Percent, "How long the arp keeps playing the captured sound after the input stops"),
    VD_B (pArpTrack,   "arpTrack",   "Pitch Track", "Arp", 0, "On: detect the input note and place arp notes exactly on the Key / Scale. Off: Scale is applied as intervals above whatever you play"),

    // ------------------------------------------------------------------ Chorus
    VD_B (pChorusOn,     "chorusOn",     "Chorus",     "Chorus", 1, "Chorus on / off"),
    VD_F (pChorusMix,    "chorusMix",    "Chorus Mix", "Chorus", 0, 1, 0.40f, 0, PUnit::Percent, "Chorus dry / wet"),
    VD_I (pChorusVoices, "chorusVoices", "Voices",     "Chorus", 2, 24, 8, PUnit::Voices, "Number of chorus voices"),
    VD_F (pChorusRate,   "chorusRate",   "Rate",       "Chorus", 0.05f, 8, 0.50f, 0.8f, PUnit::Hz, "Modulation rate"),
    VD_F (pChorusDepth,  "chorusDepth",  "Depth",      "Chorus", 0, 1, 0.40f, 0, PUnit::Percent, "Modulation depth"),
    VD_C (pChorusMode,   "chorusMode",   "Mode",       "Chorus", kChorusModes, 0, "Smooth ensemble or bright, glassy sheen"),
    VD_F (pChorusWidth,  "chorusWidth",  "Width",      "Chorus", 0, 1, 1.0f, 0, PUnit::Percent, "Stereo spread of the voices"),
    VD_F (pChorusXover,  "chorusXover",  "Low Keep",   "Chorus", 40, 1000, 220.0f, 220.0f, PUnit::Hz, "Everything below this stays unchorused and solid"),

    // ------------------------------------------------------------------ Reverb
    VD_B (pRevOn,        "revOn",        "Reverb",      "Reverb", 1, "Reverb on / off"),
    VD_F (pRevMix,       "revMix",       "Reverb Mix",  "Reverb", 0, 1, 0.45f, 0, PUnit::Percent, "Reverb dry / wet"),
    VD_F (pRevSize,      "revSize",      "Size",        "Reverb", 0, 1, 0.60f, 0, PUnit::Percent, "Size of the space"),
    VD_F (pRevDecay,     "revDecay",     "Decay",       "Reverb", 0.3f, 61, 6.0f, 6.0f, PUnit::Seconds, "Decay time; fully clockwise is infinite"),
    VD_F (pRevPredelay,  "revPredelay",  "Pre-delay",   "Reverb", 0, 250, 20.0f, 50.0f, PUnit::Ms, "Gap before the reverb speaks"),
    VD_F (pRevDamping,   "revDamping",   "Damping",     "Reverb", 500, 20000, 6500.0f, 4000.0f, PUnit::Hz, "High-frequency damping inside the tail"),
    VD_F (pRevLowCut,    "revLowCut",    "Low Cut",     "Reverb", 20, 1000, 110.0f, 150.0f, PUnit::Hz, "Low-frequency damping inside the tail"),
    VD_F (pRevMod,       "revMod",       "Motion",      "Reverb", 0, 1, 0.40f, 0, PUnit::Percent, "Depth of delay-line modulation in the tail"),
    VD_F (pRevDiffusion, "revDiffusion", "Diffusion",   "Reverb", 0, 1, 0.75f, 0, PUnit::Percent, "Smear of the early build-up"),
    VD_F (pRevShimmer,   "revShimmer",   "Shimmer",     "Reverb", 0, 1, 0.25f, 0, PUnit::Percent, "Pitch-shifted feedback inside the tail"),
    VD_C (pRevShimmerPitch, "revShimmerPitch", "Shimmer Pitch", "Reverb", kShimmerPitches, 0, "Interval of the shimmer"),
    VD_B (pRevFreeze,    "revFreeze",    "Freeze",      "Reverb", 0, "Hold the current tail forever"),

    // ------------------------------------------------------------------ Output
    VD_F (pOutTilt,  "outTilt",  "Tilt",     "Output", -1, 1, 0.0f, 0, PUnit::Bipolar, "Tilt EQ: darker to brighter"),
    VD_F (pOutWidth, "outWidth", "Width",    "Output", 0, 2, 1.15f, 0, PUnit::Percent, "Stereo width of the wet signal (lows stay centred)"),
    VD_F (pOutDuck,  "outDuck",  "Clearing", "Output", 0, 1, 0.25f, 0, PUnit::Percent, "Ducks the wet signal while you play so it blooms in the gaps"),
    VD_F (pOutGain,  "outGain",  "Output",   "Output", -24, 12, 0.0f, 0, PUnit::Db, "Output level (before the limiter)"),
    VD_B (pBypass,   "bypass",   "Bypass",   "Output", 0, "Click-free bypass"),
};

#undef VD_F
#undef VD_B
#undef VD_C
#undef VD_I

// Compile-time check that the table is in enum order.
constexpr bool paramsInOrder()
{
    for (int i = 0; i < kNumParams; ++i)
        if (int (kParams[i].pid) != i)
            return false;
    return true;
}
static_assert (paramsInOrder(), "kParams must be listed in PID order");

inline int findParam (const char* id)
{
    for (int i = 0; i < kNumParams; ++i)
        if (std::strcmp (kParams[i].id, id) == 0)
            return i;
    return -1;
}

// --- skewed normalisation (same maths as juce::NormalisableRange::setSkewForCentre)
inline float skewFor (const ParamDef& p)
{
    if (p.centre <= p.min || p.centre >= p.max) return 1.0f;
    return std::log (0.5f) / std::log ((p.centre - p.min) / (p.max - p.min));
}

inline float toNormalised (const ParamDef& p, float v)
{
    float n = (v - p.min) / (p.max - p.min);
    n = n < 0 ? 0 : (n > 1 ? 1 : n);
    const float s = skewFor (p);
    return s > 0.999f && s < 1.001f ? n : std::pow (n, s);
}

inline float fromNormalised (const ParamDef& p, float n)
{
    n = n < 0 ? 0 : (n > 1 ? 1 : n);
    const float s = skewFor (p);
    if (s < 0.999f || s > 1.001f) n = std::pow (n, 1.0f / s);
    float v = p.min + n * (p.max - p.min);
    if (p.type != PType::Float) v = std::round (v);
    return v;
}

// Formats a plain value for display. Returns chars written.
inline int formatValue (const ParamDef& p, float v, char* out, size_t n)
{
    if (p.type == PType::Bool)   return std::snprintf (out, n, "%s", v >= 0.5f ? "On" : "Off");
    if (p.type == PType::Choice)
    {
        int i = int (std::lround (v));
        i = i < 0 ? 0 : (i >= p.numChoices ? p.numChoices - 1 : i);
        return std::snprintf (out, n, "%s", p.choices[i]);
    }
    switch (p.unit)
    {
        case PUnit::Percent:   return std::snprintf (out, n, "%d %%", int (std::lround (v * 100.0f)));
        case PUnit::Bipolar:   return std::snprintf (out, n, "%+d", int (std::lround (v * 100.0f)));
        case PUnit::Semitones: return std::snprintf (out, n, "%+.1f st", double (v));
        case PUnit::Ms:        return std::snprintf (out, n, v < 100.0f ? "%.1f ms" : "%.0f ms", double (v));
        case PUnit::Hz:
            if (v >= 1000.0f)  return std::snprintf (out, n, "%.2f kHz", double (v) / 1000.0);
            return std::snprintf (out, n, v < 10.0f ? "%.2f Hz" : "%.0f Hz", double (v));
        case PUnit::Seconds:
            if (p.pid == pRevDecay && v > kRevDecayInf) return std::snprintf (out, n, "Infinite");
            return std::snprintf (out, n, v < 10.0f ? "%.2f s" : "%.1f s", double (v));
        case PUnit::Db:        return std::snprintf (out, n, "%+.1f dB", double (v));
        case PUnit::Ratio:     return std::snprintf (out, n, "%.2fx", double (v));
        case PUnit::Voices:    return std::snprintf (out, n, "%d", int (std::lround (v)));
        case PUnit::Octaves:   return std::snprintf (out, n, "%d oct", int (std::lround (v)));
        case PUnit::None:      break;
    }
    return std::snprintf (out, n, "%.2f", double (v));
}

// A full set of plain parameter values.
struct ParamValues
{
    float v[kNumParams];

    ParamValues() { setDefaults(); }
    void setDefaults() { for (int i = 0; i < kNumParams; ++i) v[i] = kParams[i].def; }
    float  operator[] (int i) const { return v[i]; }
    float& operator[] (int i)       { return v[i]; }
};

} // namespace verdant
