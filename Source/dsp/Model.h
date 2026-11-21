// Splinter DSP — pattern / curve / slice data model. Plain structs with fixed
// storage so they can be copied to the audio thread without allocating.
#pragma once
#include "Common.h"

namespace splinter
{
constexpr int kNumSlots = 12;       // per bank: time curves, volume curves, sequences
constexpr int kMaxSteps = 32;
constexpr int kMaxNodes = 96;
constexpr int kMaxSlices = 64;
constexpr int kNumHarmVoices = 4;
constexpr int kMaxSliceVoices = 8;

enum Lane : int
{
    laneChop = 0, laneStutter, laneReverse, laneTapeStop, lanePitch,
    laneCrush, laneComb, laneRing, laneFilter, laneFreeze,
    laneDelay, laneReverb, laneHarmony, kNumLanes
};

inline const char* laneName (int lane)
{
    static const char* names[kNumLanes] = { "Chop", "Stutter", "Reverse", "Tape Stop", "Pitch", "Crush", "Comb",
                                            "Ring", "Filter", "Freeze", "Delay", "Reverb", "Harmony" };
    return names[clampv (lane, 0, kNumLanes - 1)];
}
inline const char* laneId (int lane)
{
    static const char* ids[kNumLanes] = { "chop", "stutter", "reverse", "tapestop", "pitch", "crush", "comb",
                                          "ring", "filter", "freeze", "delay", "reverb", "harmony" };
    return ids[clampv (lane, 0, kNumLanes - 1)];
}
// Number of meaningful "variation" values per lane (0 always = lane default).
inline int laneVarCount (int lane)
{
    static const int counts[kNumLanes] = { kMaxSlices + 1, 10, 1, 4, 15, 5, 9, 9, 7, 5, 7, 4, 9 };
    return counts[clampv (lane, 0, kNumLanes - 1)];
}

// Stutter divisions, in beats.
constexpr int kNumStutterDivs = 9;
inline double stutterDivBeats (int index)
{
    static const double b[kNumStutterDivs] = { 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125, 1.0 / 12.0, 0.0625, 0.03125 };
    return b[clampv (index, 0, kNumStutterDivs - 1)];
}
inline const char* stutterDivName (int index)
{
    static const char* n[kNumStutterDivs] = { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T", "1/64", "1/128" };
    return n[clampv (index, 0, kNumStutterDivs - 1)];
}

// ---------------------------------------------------------------- curves
struct CurveNode
{
    float x = 0.0f;        // 0..1 across the pattern
    float y = 0.0f;        // 0..1
    float tension = 0.0f;  // -1..1 shaping of the segment that starts here
    uint8_t hold = 0;      // 1: stay at y until the next node (step)
};

struct Curve
{
    int numNodes = 0;
    CurveNode nodes[kMaxNodes];
    float lengthBeats = 4.0f;   // pattern length
    float rangeBeats = 4.0f;    // time curves: y=1 means this many beats behind
    char name[24] = {};

    void clear() { numNodes = 0; }
    bool add (float x, float y, float tension = 0.0f, bool hold = false)
    {
        if (numNodes >= kMaxNodes) return false;
        nodes[numNodes++] = { x, y, tension, (uint8_t) (hold ? 1 : 0) };
        return true;
    }
    void setName (const char* n) { std::strncpy (name, n, sizeof (name) - 1); name[sizeof (name) - 1] = 0; }

    static float shape (float t, float tension)
    {
        if (tension == 0.0f) return t;
        const float k = std::exp2 (std::fabs (tension) * 3.0f); // exponent 1..8
        return tension > 0.0f ? std::pow (t, k) : 1.0f - std::pow (1.0f - t, k);
    }

    // Value at x in [0,1). Coincident nodes produce a jump: the later node wins
    // from its x onwards. Before the first / after the last node the edge value holds.
    float eval (double x) const
    {
        if (numNodes <= 0) return 0.0f;
        if (x < nodes[0].x) return nodes[0].y;
        // last node with node.x <= x
        int i = hint < numNodes ? hint : 0;
        if (nodes[i].x > x) i = 0;
        while (i + 1 < numNodes && nodes[i + 1].x <= x) ++i;
        hint = i;
        const CurveNode& a = nodes[i];
        if (i + 1 >= numNodes || a.hold) return a.y;
        const CurveNode& b = nodes[i + 1];
        const float span = b.x - a.x;
        if (span <= 1.0e-9f) return b.y;
        const float t = (float) ((x - a.x) / span);
        return a.y + (b.y - a.y) * shape (t, a.tension);
    }
    mutable int hint = 0;
};

// ---------------------------------------------------------------- step sequencer
struct Step
{
    uint8_t on = 0;
    uint8_t prob = 100;   // percent
    uint8_t var = 0;      // lane specific variation, 0 = lane default
};

struct SeqPattern
{
    int numSteps = 16;
    int stepsPerBeat = 4;            // 4 = 1/16 steps; 2 = 1/8; 8 = 1/32; 3 / 6 = triplets
    Step steps[kNumLanes][kMaxSteps];
    uint8_t locked[kNumLanes] = {};  // protected from Chaos / Generate
    char name[24] = {};
    void setName (const char* n) { std::strncpy (name, n, sizeof (name) - 1); name[sizeof (name) - 1] = 0; }
    void clear() { for (auto& l : steps) for (auto& s : l) s = Step(); }
    bool laneEmpty (int lane) const
    {
        for (int i = 0; i < numSteps; ++i) if (steps[lane][i].on) return false;
        return true;
    }
};

// ---------------------------------------------------------------- slices
struct SliceSet
{
    int count = 0;
    float startBeat[kMaxSlices] = {};   // ascending, within [0, loopBeats)
    float loopBeats = 4.0f;

    float sliceEnd (int i) const { return i + 1 < count ? startBeat[i + 1] : loopBeats; }
    int sliceAt (double beat) const
    {
        int r = 0;
        for (int i = 0; i < count; ++i) if (startBeat[i] <= beat) r = i; else break;
        return r;
    }
    void makeGrid (float beats, int divisionsPerBeat)
    {
        loopBeats = beats;
        int n = (int) std::lround (beats * (float) divisionsPerBeat);
        int div = divisionsPerBeat;
        while (n > kMaxSlices && div > 1) { div /= 2; n = (int) std::lround (beats * (float) div); }
        count = clampv (n, 1, kMaxSlices);
        for (int i = 0; i < count; ++i) startBeat[i] = beats * (float) i / (float) count;
    }
};
} // namespace splinter
