// Splinter DSP — stateless evaluation of the step sequencer.
//
// "Is lane L active at absolute step S, and with which variation?" is a pure
// function of (pattern, macros, chaos, seed, S). Consecutive active steps with
// the same variation merge into one run (one gesture).
#pragma once
#include "Model.h"

namespace splinter
{
struct SeqSettings
{
    const SeqPattern* pattern = nullptr;
    float probScale[kNumLanes];   // 0..1 scaling of programmed probability
    float inject[kNumLanes];      // 0..1 density of generated events
    float chaos = 0.0f;
    uint32_t seed = 1;
    uint32_t version = 0;         // bump whenever anything above changes
    SeqSettings() { for (int i = 0; i < kNumLanes; ++i) { probScale[i] = 1.0f; inject[i] = 0.0f; } }
};

struct StepState { bool on = false; int var = 0; };

struct LaneRun
{
    bool active = false;
    double start = 0.0;     // beats
    double length = 0.0;    // beats
    int var = 0;
    uint32_t rnd = 0;       // per-run random value
    bool held = false;      // momentary (MIDI) run with open end
};

namespace seqdetail
{
// Length (steps) of generated gestures per lane, and how keen each lane is on a
// given position in the bar. pos16 = 16th-note index within the bar (0..15).
inline int genLength (int lane, int stepsPerBeat)
{
    static const float beats[kNumLanes] = { 0.25f, 0.5f, 0.5f, 1.0f, 0.5f, 0.5f, 0.5f, 0.5f, 1.0f, 0.5f, 0.25f, 0.25f, 1.0f };
    return std::max (1, (int) std::lround (beats[lane] * (float) stepsPerBeat));
}
inline float genWeight (int lane, int pos16, int64_t bar)
{
    const int inBeat = pos16 & 3;
    const int beat = pos16 >> 2;
    const bool lastBeat = beat == 3;
    const bool phraseEnd = (floorModI (bar, 4) == 3);
    switch (lane)
    {
        case laneChop:     return pos16 == 0 ? 0.0f : (inBeat == 0 ? 0.25f : (inBeat == 2 ? 0.6f : 0.45f));
        case laneStutter:  return (lastBeat ? 0.7f : 0.12f) + (beat == 1 && inBeat >= 2 ? 0.25f : 0.0f) + (phraseEnd && lastBeat ? 0.3f : 0.0f);
        case laneReverse:  return inBeat == 2 ? (lastBeat ? 0.6f : 0.3f) : 0.05f;
        case laneTapeStop: return (lastBeat && phraseEnd) ? 0.8f : 0.0f;
        case lanePitch:    return inBeat == 2 ? 0.3f : 0.08f;
        case laneCrush:    return inBeat == 0 ? 0.15f : 0.35f;
        case laneComb:     return inBeat == 2 ? 0.3f : 0.1f;
        case laneRing:     return inBeat == 2 ? 0.25f : 0.08f;
        case laneFilter:   return (beat == 3 || beat == 1) ? 0.35f : 0.1f;
        case laneFreeze:   return inBeat == 2 ? 0.3f : 0.05f;
        case laneDelay:    return (pos16 == 6 || pos16 == 14) ? 0.7f : (inBeat == 3 ? 0.2f : 0.0f);
        case laneReverb:   return (pos16 == 4 || pos16 == 12) ? 0.6f : 0.0f;
        case laneHarmony:  return (beat == 2 || beat == 0) ? 0.4f : 0.15f;
        default:           return 0.0f;
    }
}
inline float chaosWeight (int lane)
{
    static const float w[kNumLanes] = { 0.9f, 0.8f, 0.6f, 0.5f, 0.5f, 0.5f, 0.35f, 0.3f, 0.5f, 0.4f, 0.5f, 0.4f, 0.0f };
    return w[lane];
}
// A musically sensible random variation for generated / mutated steps.
inline int randomVar (int lane, uint32_t h)
{
    const float u = hashToUnit (h);
    switch (lane)
    {
        case laneChop:     return 0;                                   // random slice
        case laneStutter:  { static const int v[] = { 2, 4, 4, 4, 6, 6, 5, 8 }; return v[(int) (u * 8.0f)]; }
        case laneTapeStop: return 1;
        case lanePitch:    { static const int v[] = { 3, 5, 6, 10, 12, 14, 1 }; return v[(int) (u * 7.0f)]; }
        case laneCrush:    return 1 + (int) (u * 4.0f);
        case laneComb:
        case laneRing:     return 1 + (int) (u * 8.0f);
        case laneFilter:   return 1 + (int) (u * 6.0f);
        case laneFreeze:   return 1 + (int) (u * 4.0f);
        case laneDelay:    return 1 + (int) (u * 5.0f);
        case laneReverb:   return 1 + (int) (u * 3.0f);
        case laneHarmony:  return (int) (u * 5.0f);
        default:           return 0;
    }
}
} // namespace seqdetail

inline StepState evalStep (const SeqSettings& s, int lane, int64_t absStep)
{
    StepState out;
    if (s.pattern == nullptr) return out;
    const SeqPattern& p = *s.pattern;
    const int idx = floorModI (absStep, std::max (1, p.numSteps));
    const Step& st = p.steps[lane][idx];
    const uint32_t laneKey = (uint32_t) lane * 7919U + 101U;
    const uint32_t h = hash3 (s.seed, laneKey, (uint32_t) (absStep & 0xffffffff));
    const bool locked = p.locked[lane] != 0;
    const float chaos = locked ? 0.0f : s.chaos;

    if (st.on && hashToUnit (h) * 100.0f < (float) st.prob * s.probScale[lane])
    {
        out.on = true; out.var = st.var;
        if (chaos > 0.0f)
        {
            if (hashToUnit (hash32 (h + 11U)) < chaos * 0.3f) out.on = false;       // thin out
            const int spb = std::max (1, p.stepsPerBeat);
            const uint32_t hg = hash3 (s.seed, laneKey + 5U, (uint32_t) (floorDiv (absStep, spb) & 0xffffffff));
            if (hashToUnit (hg) < chaos * 0.6f) out.var = seqdetail::randomVar (lane, hash32 (hg + 3U));
        }
        if (out.on) return out;
    }

    const float density = s.inject[lane] + chaos * seqdetail::chaosWeight (lane) * 0.6f;
    if (density > 0.0f)
    {
        const int spb = std::max (1, p.stepsPerBeat);
        const int glen = seqdetail::genLength (lane, spb);
        const int64_t anchor = floorDiv (absStep, glen) * glen;
        const double beat = (double) anchor / (double) spb;
        const int pos16 = (int) std::floor (floorMod (beat, 4.0) * 4.0 + 1.0e-6) & 15;
        const int64_t bar = (int64_t) std::floor (beat / 4.0 + 1.0e-9);
        const uint32_t ha = hash3 (s.seed, laneKey + 31U, (uint32_t) (anchor & 0xffffffff));
        if (hashToUnit (ha) < density * seqdetail::genWeight (lane, pos16, bar))
        {
            out.on = true;
            out.var = seqdetail::randomVar (lane, hash32 (ha + 77U));
        }
    }
    return out;
}

// Finds the run (merged steps) containing a time; caches the last answer.
class LaneEvaluator
{
public:
    void invalidate() { cachedStep = INT64_MIN; }

    const LaneRun& query (const SeqSettings& s, int lane, double beat)
    {
        if (s.pattern == nullptr) { run = LaneRun(); return run; }
        const int spb = std::max (1, s.pattern->stepsPerBeat);
        const int64_t step = (int64_t) std::floor (beat * (double) spb + 1.0e-9);
        if (step == cachedStep && s.version == cachedVersion) return run;
        cachedStep = step; cachedVersion = s.version;

        const StepState here = evalStep (s, lane, step);
        run = LaneRun();
        if (! here.on) return run;
        int64_t first = step, last = step;
        for (int i = 0; i < kMaxSteps; ++i)
        {
            const StepState b = evalStep (s, lane, first - 1);
            if (! b.on || b.var != here.var) break;
            --first;
        }
        for (int i = 0; i < kMaxSteps; ++i)
        {
            const StepState f = evalStep (s, lane, last + 1);
            if (! f.on || f.var != here.var) break;
            ++last;
        }
        run.active = true;
        run.start = (double) first / (double) spb;
        run.length = (double) (last - first + 1) / (double) spb;
        run.var = here.var;
        run.rnd = hash3 (s.seed, (uint32_t) lane * 131U + 9U, (uint32_t) (first & 0xffffffff));
        return run;
    }

private:
    int64_t cachedStep = INT64_MIN;
    uint32_t cachedVersion = 0;
    LaneRun run;
};
} // namespace splinter
