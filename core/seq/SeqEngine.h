// Sequencer step evaluation.
//
// Everything in this file is a pure function of (pattern data, seeds, song
// position, macro values). There is no running state, which is what makes host
// loops, jumps and offline bounces land on identical events.
#pragma once
#include "../model/Model.h"
#include "Scale.h"

namespace atm {

struct SeqMacros
{
    float density    = 0.f;     // -1..1
    float complexity = 0.f;     // 0..1
    float mutation   = 0.f;     // 0..1
    float chaos      = 0.f;     // 0..1
    float glitch     = 0.f;     // 0..1
    bool  fill       = false;
};

// Where a pattern sits in time.
struct SeqFrame
{
    const Pattern* pattern = nullptr;
    double origin      = 0.0;   // beat at which free-running tracks count from
    double cycleBeats  = 4.0;   // pattern cycle length (lengthBars * beats per bar)
    double globalSwing = 0.5;
    uint32_t seed      = 1;     // project seed, mixed into every hash
    const Kit* kit     = nullptr; // optional: lets macro variation respect track roles
};

struct SeqHit
{
    double  beat;               // absolute time of the hit
    double  gateBeats;          // gate length
    float   velocity;           // 0..1
    float   pitch;              // step pitch (+ ratchet ramp), in scale units
    bool    slide;
    uint8_t ratIndex;
    uint32_t seed;              // per-hit seed for the voice
};

namespace GlitchType { enum : uint8_t { None, Repeat, Reverse, Stop, Crush, Gate, count }; }
struct GlitchCmd
{
    uint8_t type = GlitchType::None;
    float   beats = 0.f;        // how long the glitch lasts
    float   sliceBeats = 0.f;   // repeat/gate slice length
    float   pitchStep = 0.f;    // semitones per repeat
    float   amount = 1.f;       // intensity 0..1
};

// One evaluated step.
struct StepEval
{
    int64_t abs = 0;            // absolute step counter within its cycle (or since origin when free-running)
    int64_t cycle = 0;
    int     index = 0;          // which Step of the pattern
    int64_t loop = 0;
    double  nominal = 0.0;      // grid time in beats
    double  time = 0.0;         // event time (swing, groove, micro applied)
    double  stepBeats = 0.25;
    bool    trig = false;       // plays a note
    Step    step;               // the step after macro variation
    float   velScale = 1.f;     // groove / chaos velocity factor
    GlitchCmd glitch;
};

constexpr int kMaxRatchets = 16;

class SeqEngine
{
public:
    // Number of absolute steps in one pattern cycle for a (non free-running) track.
    static int64_t stepsPerCycle (const TrackPattern& tp, double cycleBeats);

    // Which Step index plays at absolute counter `abs`, and the loop number.
    static int directionIndex (const TrackPattern& tp, int64_t abs, uint32_t seed, int64_t& loopOut);

    // Evaluate a step: macro variation, conditions, timing. (cycle, abs) identify it.
    static void evalStep (const SeqFrame& f, const SeqMacros& m, int track, int64_t cycle, int64_t abs, float glitchSens, StepEval& out);

    // Ratchet expansion: fills hits (count returned), times relative to the step's event time.
    static int expandHits (const StepEval& se, uint32_t seed, SeqHit* hits);

    // Fraction (0..1) of the step at which ratchet hit i of n starts.
    static double ratchetPos (int i, int n, int curve, float amount);

    // Enumerate every (cycle, abs) of a track whose nominal time lies in [t0, t1).
    // Calls fn(cycle, abs). Bounded by maxSteps to stay real-time safe.
    template <class Fn>
    static void forEachStepInRange (const SeqFrame& f, const TrackPattern& tp, double t0, double t1, Fn&& fn, int maxSteps = 512)
    {
        const double d = tp.stepBeats();
        if (d <= 0.0) return;
        int emitted = 0;
        if (tp.freeRun)
        {
            int64_t a = (int64_t) std::ceil ((t0 - f.origin) / d - 1.0e-9);
            for (; f.origin + (double) a * d < t1 - 1.0e-12 && emitted < maxSteps; ++a, ++emitted) fn ((int64_t) 0, a);
            return;
        }
        const int64_t spc = stepsPerCycle (tp, f.cycleBeats);
        int64_t c = (int64_t) std::floor ((t0 - f.origin) / f.cycleBeats);
        for (;; ++c)
        {
            const double cs = f.origin + (double) c * f.cycleBeats;
            if (cs >= t1) break;
            int64_t a = (int64_t) std::ceil ((t0 - cs) / d - 1.0e-9);
            if (a < 0) a = 0;
            for (; a < spc && cs + (double) a * d < t1 - 1.0e-12 && emitted < maxSteps; ++a, ++emitted) fn (c, a);
            if (emitted >= maxSteps) break;
        }
    }

    static double nominalTime (const SeqFrame& f, const TrackPattern& tp, int64_t cycle, int64_t abs)
    {
        return f.origin + (tp.freeRun ? 0.0 : (double) cycle * f.cycleBeats) + (double) abs * tp.stepBeats();
    }

    // How far (in steps) an event can sit from its nominal grid time.
    static constexpr double kMaxEarlySteps = 1.0;
    static constexpr double kMaxLateSteps  = 2.5;

private:
    struct Resolved { int index; int64_t loop; Step step; };
    static Resolved resolve (const SeqFrame& f, const SeqMacros& m, int track, int64_t cycle, int64_t abs);
    static bool passes (const SeqFrame& f, const SeqMacros& m, int track, int64_t cycle, int64_t abs, const Resolved& r, int depth);
};

} // namespace atm
