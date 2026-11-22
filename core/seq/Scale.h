// Scales and tunings. A scale is a list of cents within a period; step pitches
// on a scale-quantised track are scale degrees, so every value is in key and
// microtonal (Scala) tunings need no special casing.
#pragma once
#include "../model/Model.h"
#include <string>

namespace atm {

struct ScaleDef { const char* name; uint16_t mask; };   // bit i = semitone i above the root is in the scale
constexpr int kNumScales = 20;
const ScaleDef& scaleDef (int i);

// Flattened, audio-thread-friendly form of a Tuning.
struct ScaleTable
{
    int   count = 12;                       // notes per period
    float period = 1200.f;                  // cents
    float cents[kMaxScaleNotes] = {};       // cents[0] = 0
    int   root = 0;                         // pitch class of the tonic (MIDI note root + 12k)

    void build (const Tuning& t);
    // MIDI (fractional) note of scale tone k, where tone 0 is the tonic at MIDI note `root`.
    float toneToNote (int k) const
    {
        const int oct = (int) floorDiv64 (k, count);
        const int deg = k - oct * count;
        return (float) root + (period * (float) oct + cents[deg]) * 0.01f;
    }
    // Largest tone whose note is <= the given MIDI note.
    int toneAtOrBelow (float note) const;
    // Note for a step pitch on a track: degrees from the track's base note when
    // scaleOn, plain semitones otherwise.
    float pitchToNote (int baseNote, float pitch, bool scaleOn) const
    {
        if (! scaleOn) return (float) baseNote + pitch;
        const int k0 = toneAtOrBelow ((float) baseNote + 0.001f);
        const float fl = std::floor (pitch);
        const int d = (int) fl;
        const float a = toneToNote (k0 + d);
        const float frac = pitch - fl;
        return frac > 0.f ? a + (toneToNote (k0 + d + 1) - a) * frac : a;
    }
};

// Parse the text of a Scala .scl file into a Tuning (custom scale). Returns false and an error on failure.
bool parseScala (const std::string& text, Tuning& out, std::string& error);

} // namespace atm
