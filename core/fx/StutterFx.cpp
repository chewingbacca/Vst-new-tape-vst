// Stutter: tempo-synced buffer repeat with a per-repeat pitch ramp, level decay, shrinking
// ("bouncing ball") slots, direction and gate.
//
// The input is recorded continuously. When `on` goes high the slice that starts at that sample is
// heard live while it is captured (the output stays bit-identical to the input), then repeated.
// Every repeat is a windowed "grain" read from the captured slice with Hermite interpolation:
//
//   * forward grains fade in during a pre-roll that plays the audio just before the slice, so the
//     grain is at full level exactly on its slot boundary and the slice's own transient is intact;
//   * reversed grains fade in on a mirrored pre-roll (the slice tail played forwards into the
//     turn-around point), which makes the forward -> reverse hand-over of Alt mode seamless;
//   * the outgoing grain (or the live signal, for the first repeat) fades out over exactly the same
//     samples with the complementary gain, so a steady signal keeps its level across the boundary.
//
// Slot boundaries are accumulated in double precision and rounded per repeat, so repeats stay on
// the tempo grid; at pitch 0 the grains read integer positions and reproduce the slice exactly.
// Releasing `on` crossfades back to the live input in 1..3 ms.
#include "Effect.h"
#include "../model/ParamTables.h"
#include <vector>

namespace atm {
namespace {

inline float smooth01 (float x) { x = clamp01 (x); return x * x * (3.f - 2.f * x); }

// Ring buffer addressed by an absolute (ever increasing) sample index.
struct History
{
    std::vector<float> d; int64_t mask = 0;
    void prepare (int64_t minLen) { size_t n = 1024; while ((int64_t) n < minLen) n <<= 1; d.assign (n, 0.f); mask = (int64_t) n - 1; }
    void clear() { std::fill (d.begin(), d.end(), 0.f); }
    int64_t size() const { return mask + 1; }
    inline float& operator[] (int64_t i) { return d[(size_t) (i & mask)]; }
    inline float at (int64_t i) const { return d[(size_t) (i & mask)]; }
    inline float hermite (double pos) const
    {
        const double fl = std::floor (pos);
        const int64_t i = (int64_t) fl; const float f = (float) (pos - fl);
        return hermite4 (at (i - 1), at (i), at (i + 1), at (i + 2), f);
    }
};

class StutterFx final : public Effect
{
public:
    enum { On, Length, Pitch, Decay, Shrink, Dir, Gate, Smooth };
    enum { Fwd, Alt, Rev };

    void prepare (double sampleRate) override
    {
        sr = sampleRate;
        maxSlice = sr * 4.0;                                   // a half note down to 30 bpm
        preRoll  = sr * 0.05;
        const int64_t guard = (int64_t) (sr * 0.06) + 64;      // pre-roll + release tail that must survive a frozen recording
        bufL.prepare ((int64_t) maxSlice + guard + 16); bufR.prepare (bufL.size());
        maxRec  = bufL.size() - guard;
        minSlot = sr * 0.002;
        reset();
    }

    void reset() override
    {
        bufL.clear(); bufR.clear();
        w = bufL.size(); histStart = 0;
        engaged = frozen = false;
        for (auto& g : grains) g = Grain();
        livePhase = liveTarget = 1.f; liveStep = 0.f;
        te = 0; nextSpawn = 0; rep = 0; numActive = 0;
    }

    bool isTransparentWhenIdle() const override { return true; }

    void setParams (const float* p, const FxContext& c) override
    {
        wantOn = p[On] >= 0.5f;
        bpm = clamp (c.bpm, 20.0, 999.0);
        static constexpr double kBeats[] = { 2.0, 1.0, 2.0 / 3.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125, 1.0 / 12.0, 0.0625, 0.03125 };
        lenBeats = kBeats[clamp (kFxStutter[Length].toIndex (p[Length]), 0, 10)];
        pitch  = (double) kFxStutter[Pitch].toPhys (p[Pitch]);
        decay  = clamp01 (p[Decay]);
        shrink = (double) kFxStutter[Shrink].toPhys (p[Shrink]);
        dir    = kFxStutter[Dir].toIndex (p[Dir]);
        gate   = (double) kFxStutter[Gate].toPhys (p[Gate]);
        if (p[Smooth] != lastSmooth)
        {
            lastSmooth = p[Smooth];
            fadeT = sr * 0.0005 * std::pow (80.0, (double) clamp01 (lastSmooth));     // 0.5 ms .. 40 ms
        }
    }

    void process (float* l, float* r, int n, const FxContext&) override
    {
        if (wantOn != engaged) { if (wantOn) engage(); else release(); }

        for (int i = 0; i < n; ++i)
        {
            const float inL = l[i], inR = r[i];
            if (! frozen)
            {
                bufL[w] = inL; bufR[w] = inR; ++w;
                if (engaged && w - start >= maxRec) frozen = true;     // keep the captured slice during very long holds
            }
            if (engaged)
            {
                if (te == nextSpawn) spawn();
                ++te;
            }
            if (numActive == 0 && livePhase >= 1.f) continue;          // idle or first pass: untouched

            if (livePhase != liveTarget)
                livePhase = liveTarget > livePhase ? std::min (liveTarget, livePhase + liveStep) : std::max (liveTarget, livePhase - liveStep);
            const float lg = smooth01 (livePhase);
            float oL = inL * lg, oR = inR * lg;

            const double hi = (double) (w - 3);                        // newest position a 4-point read may touch
            int alive = 0;
            for (auto& g : grains)
            {
                if (! g.active) continue;
                double p = g.reverse ? g.anchor - std::fabs ((double) g.o) * g.rate : g.anchor + (double) g.o * g.rate;
                if (p > hi) p = hi;
                if (p < g.lo) p = g.lo;
                float wgt = g.gain;
                if (g.o < 0)                 wgt *= smooth01 ((float) (g.o + g.fadeIn + 1) / (float) g.fadeIn);
                else if (g.o >= g.outStart)  wgt *= 1.f - smooth01 ((float) (g.o - g.outStart + 1) / (float) g.fadeOut);
                if (g.killStep > 0.f) { g.kill -= g.killStep; wgt *= smooth01 (g.kill); }
                oL += wgt * bufL.hermite (p);
                oR += wgt * bufR.hermite (p);
                ++g.o;
                if (g.o >= g.outStart + g.fadeOut || g.kill <= 0.f) g.active = false; else ++alive;
            }
            numActive = alive;
            l[i] = oL; r[i] = oR;
        }
    }

private:
    struct Grain
    {
        bool   active = false, reverse = false;
        double anchor = 0.0;            // absolute source position played at o == 0
        double rate = 1.0;
        double lo = 0.0;                // oldest readable position
        int    o = 0;                   // output samples since the slot start (negative during the pre-roll)
        int    fadeIn = 1, outStart = 0, fadeOut = 1;
        float  gain = 1.f, kill = 1.f, killStep = 0.f;
    };

    double lenSamples() const { return clamp (lenBeats * 60.0 / bpm * sr, minSlot * 2.0, maxSlice); }
    double rateFor (int k) const { return clamp (std::exp2 ((double) k * pitch / 12.0), 1.0 / 16.0, 16.0); }
    // how long a grain sounds: its gated slot, or less if the pitched read runs out of slice first
    double sounding (double slot, double len, double rate) const { return std::max (4.0, std::min (gate >= 0.999 ? slot : gate * slot, (len - 1.0) / rate)); }
    int boundaryFade (double dA, double dB) const { return std::max (1, (int) std::min ({ fadeT, 0.45 * dA, 0.45 * dB })); }

    void engage()
    {
        engaged = true; frozen = false;
        start = w; te = 0; rep = 0; shrinkPow = 1.0; decayGain = 1.f;
        const double len = lenSamples();
        nextB = len;
        pendingFade = boundaryFade (len, sounding (std::max (minSlot, len * shrink), len, rateFor (1)));
        nextSpawn = (int64_t) std::llround (nextB) - pendingFade;
        liveTarget = 1.f;                                              // first pass: the live input, while it is captured
        if (livePhase < 1.f && liveStep <= 0.f) liveStep = 1.f / (float) releaseLen();
    }

    void release()
    {
        engaged = false;
        const float step = 1.f / (float) releaseLen();
        liveTarget = 1.f; liveStep = step;
        for (auto& g : grains) if (g.active && g.killStep <= 0.f) g.killStep = step;
        if (frozen) { frozen = false; histStart = w; }                 // the history restarts here
    }

    int releaseLen() const { return (int) clamp (fadeT, sr * 0.001, sr * 0.003); }

    Grain& alloc()
    {
        for (auto& g : grains) if (! g.active) return g;
        Grain* best = &grains[0];                                      // all busy (does not happen in practice): take the most faded
        for (auto& g : grains) if (g.kill < best->kill) best = &g;
        return *best;
    }

    void spawn()
    {
        ++rep;
        const int64_t ib = (int64_t) std::llround (nextB);             // slot start, in samples since the engage
        const double recorded = (double) std::min (ib, maxRec - 8);    // what will be on tape when the slot starts
        const double len = std::min (lenSamples(), recorded);
        shrinkPow = std::max (shrinkPow * shrink, 1.0e-9);
        decayGain *= 1.f - decay;
        const double rate = rateFor (rep);
        const double slot = std::max (minSlot, len * shrinkPow);
        const double dur  = sounding (slot, len, rate);

        Grain& g = alloc();
        g = Grain();
        g.active  = true;
        g.reverse = dir == Rev || (dir == Alt && (rep & 1) != 0);
        g.rate    = rate;
        g.gain    = decayGain;
        g.anchor  = g.reverse ? (double) start + len - 1.0 : (double) start;
        g.lo      = std::max ((double) start - preRoll, (double) histStart + 1.0);
        g.fadeIn  = pendingFade;
        g.o       = -pendingFade;

        nextB += slot;
        const int64_t ibNext = (int64_t) std::llround (nextB);
        const int slotLen = (int) (ibNext - ib);
        const int fb = boundaryFade (dur, sounding (std::max (minSlot, len * shrinkPow * shrink), len, rateFor (rep + 1)));
        if (dur >= (double) (slotLen - fb)) { g.outStart = slotLen - fb; g.fadeOut = fb; }      // plays into the next boundary: crossfade
        else
        {
            const int fo = std::max (1, (int) std::min (fadeT, 0.5 * dur));                       // gated or ran out of slice: own fade
            g.outStart = std::max (0, (int) dur - fo); g.fadeOut = fo;
        }
        pendingFade = fb;
        nextSpawn = ibNext - fb;
        ++numActive;
        if (rep == 1) { liveTarget = 0.f; liveStep = 1.f / (float) g.fadeIn; }                   // live hands over to the first repeat
    }

    double sr = 48000.0, maxSlice = 192000.0, preRoll = 2400.0, minSlot = 96.0;
    History bufL, bufR;
    int64_t w = 0, start = 0, histStart = 0, maxRec = 0;
    bool wantOn = false, engaged = false, frozen = false;

    // parameters
    double bpm = 120.0, lenBeats = 0.25, pitch = 0.0, shrink = 1.0, gate = 1.0, fadeT = 90.0;
    float  decay = 0.f, lastSmooth = -1.f;
    int    dir = Fwd;

    // engagement state
    int64_t te = 0, nextSpawn = 0;
    double  nextB = 0.0, shrinkPow = 1.0;
    int     rep = 0, pendingFade = 1;
    float   decayGain = 1.f;

    Grain grains[6];
    int   numActive = 0;
    float livePhase = 1.f, liveTarget = 1.f, liveStep = 0.f;
};

} // namespace

std::unique_ptr<Effect> createStutterFx() { return std::make_unique<StutterFx>(); }

} // namespace atm
