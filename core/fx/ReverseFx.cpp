// Reverse: plays the most recent audio backwards in tempo-synced chunks.
//
// The input is recorded continuously. On engage a read head starts at the newest recorded sample
// and runs backwards, so the output is continuous with the live signal (it turns round on the spot,
// no fade needed and no latency). At every chunk boundary a new head starts at the then-newest
// sample: each chunk of output is the previous chunk of input, reversed. The outgoing head keeps
// running into older audio while it fades out under the new one (equal-power crossfade, `fade`).
// `speed` resamples the read (Hermite). `feedback` writes the reversed output back into the
// recording (soft-limited, loop gain < 1), so later chunks contain earlier reversed chunks.
// Release crossfades back to the live input in a few milliseconds.
#include "Effect.h"
#include "../model/ParamTables.h"
#include <vector>

namespace atm {
namespace {

inline float smooth01 (float x) { x = clamp01 (x); return x * x * (3.f - 2.f * x); }
// equal-power gain for a fade position 0..1 (smooth at both ends)
inline float powerGain (float x) { return x >= 1.f ? 1.f : (x <= 0.f ? 0.f : fastSin01 (0.25f * smooth01 (x))); }

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

class ReverseFx final : public Effect
{
public:
    enum { On, Length, Speed, Fade, Feedback };

    void prepare (double sampleRate) override
    {
        sr = sampleRate;
        // A chunk of C samples needs 2 C of history at speed 1 (the head and the recording move apart):
        // 17 s covers "2 bar" at 60 bpm. Heads that would run out of tape fade out instead.
        bufL.prepare ((int64_t) (sr * 17.0)); bufR.prepare (bufL.size());
        maxDist = (double) (bufL.size() - 16);
        minFade = std::max (4.0, sr * 0.0005);
        speedCoef = onePoleCoef (0.005f, (float) sr);
        reset();
    }

    void reset() override
    {
        bufL.clear(); bufR.clear();
        w = bufL.size();
        engaged = false;
        for (auto& v : heads) v = Head();
        numActive = 0;
        livePhase = 1.f; liveStep = 0.f;
        speed = speedTarget; te = 0; nextB = 0.0;
    }

    bool isTransparentWhenIdle() const override { return true; }

    void setParams (const float* p, const FxContext& c) override
    {
        wantOn = p[On] >= 0.5f;
        bpm = clamp (c.bpm, 20.0, 999.0);
        static constexpr double kBeats[] = { 8.0, 4.0, 2.0, 1.0, 0.5, 0.25 };
        lenBeats = kBeats[clamp (kFxReverse[Length].toIndex (p[Length]), 0, 5)];
        if (p[Speed] != lastSpeed)
        {
            lastSpeed = p[Speed];
            speedTarget = (double) kFxReverse[Speed].toPhys (lastSpeed);
            if (std::fabs (speedTarget - 1.0) < 1.0e-4) speedTarget = 1.0;
        }
        fade = clamp01 (p[Fade]);
        feedback = kFxReverse[Feedback].toPhys (p[Feedback]);
    }

    void process (float* l, float* r, int n, const FxContext&) override
    {
        if (wantOn != engaged) { if (wantOn) engage(); else release(); }

        for (int i = 0; i < n; ++i)
        {
            const float inL = l[i], inR = r[i];
            bufL[w] = inL; bufR[w] = inR;                              // w = index of the current sample
            if (engaged)
            {
                if (te >= nextBI) newChunk();
                ++te;
            }
            if (numActive == 0 && livePhase >= 1.f) { ++w; continue; } // idle: untouched

            if (speed != speedTarget)
            {
                speed += (double) speedCoef * (speedTarget - speed);
                if (std::fabs (speed - speedTarget) < 1.0e-7) speed = speedTarget;
            }
            if (livePhase < 1.f && liveStep > 0.f) livePhase = std::min (1.f, livePhase + liveStep);
            const float lg = powerGain (livePhase);
            float oL = inL * lg, oR = inR * lg, fL = 0.f, fR = 0.f;

            int alive = 0;
            for (auto& v : heads)
            {
                if (! v.active) continue;
                if (v.in < 1.f)  v.in = std::min (1.f, v.in + v.inStep);
                if (v.outStep > 0.f) v.out -= v.outStep;
                // never read tape that has been overwritten: fade the head out before it gets there
                const float room = (float) ((maxDist - ((double) w - v.pos)) * limitSlope);
                if (v.out <= 0.f || room <= 0.f) { v.active = false; continue; }
                const float sL = bufL.hermite (v.pos), sR = bufR.hermite (v.pos);
                const float lim = std::min (1.f, room);
                const float gp = powerGain (v.in) * powerGain (v.out) * lim;       // heard: equal power
                const float gl = smooth01 (v.in) * smooth01 (v.out) * lim;         // fed back: gains sum to <= 1
                oL += gp * sL; oR += gp * sR;
                fL += gl * sL; fR += gl * sR;
                v.pos -= speed;
                ++alive;
            }
            numActive = alive;
            if (feedback > 0.f)
            {
                bufL[w] = undenorm (inL + 4.f * softClip (0.25f * feedback * fL));
                bufR[w] = undenorm (inR + 4.f * softClip (0.25f * feedback * fR));
            }
            ++w;
            l[i] = oL; r[i] = oR;
        }
    }

private:
    struct Head
    {
        bool   active = false;
        double pos = 0.0;
        float  in = 1.f, inStep = 0.f;      // fade-in position
        float  out = 1.f, outStep = 0.f;    // fade-out position (runs 1 -> 0 once outStep is set)
    };

    double chunkSamples() const { return std::max (sr * 0.02, lenBeats * 60.0 / bpm * sr); }

    Head& alloc()
    {
        for (auto& v : heads) if (! v.active) return v;
        Head* best = &heads[0];
        for (auto& v : heads) if (v.out < best->out) best = &v;
        return *best;
    }

    void engage()
    {
        engaged = true; te = 0; nextB = 0.0; nextBI = 0;
        firstChunk = true;
        if (numActive == 0) speed = speedTarget;                       // nothing sounding: no glide from an old setting
    }

    void newChunk()
    {
        const double chunk = chunkSamples();
        const float f = (float) (minFade + (double) fade * std::max (0.0, 0.5 * chunk - minFade));
        Head& v = alloc();
        const bool wasFree = ! v.active;
        v = Head();
        v.active = true;
        v.pos = (double) (w - 1);                                     // newest complete sample: continuous with the live signal
        if (firstChunk)
        {
            // take over from the live signal on the spot (and inherit its gain if it was still fading in)
            v.in = livePhase; v.inStep = liveStep;
            livePhase = 0.f; liveStep = 0.f;
            firstChunk = false;
        }
        else
        {
            v.in = 0.f; v.inStep = 1.f / f;
            for (auto& o : heads) if (o.active && &o != &v && o.outStep <= 0.f) o.outStep = 1.f / f;
        }
        if (wasFree) ++numActive;
        limitSlope = 1.0 / (sr * 0.02 * (1.0 + speedTarget));
        nextB += chunk;
        nextBI = (int64_t) std::llround (nextB);
    }

    void release()
    {
        engaged = false;
        const float step = 1.f / (float) (sr * (0.0015 + 0.0045 * (double) fade));
        for (auto& v : heads) if (v.active && v.outStep <= 0.f) v.outStep = step;
        liveStep = step;
    }

    double sr = 48000.0, maxDist = 0.0, minFade = 24.0, limitSlope = 1.0e-3;
    History bufL, bufR;
    int64_t w = 0;
    bool wantOn = false, engaged = false, firstChunk = false;

    double bpm = 120.0, lenBeats = 1.0, speed = 1.0, speedTarget = 1.0;
    float  fade = 0.3f, feedback = 0.f, lastSpeed = -1.f, speedCoef = 0.01f;

    int64_t te = 0, nextBI = 0;
    double  nextB = 0.0;
    Head  heads[4];
    int   numActive = 0;
    float livePhase = 1.f, liveStep = 0.f;
};

} // namespace

std::unique_ptr<Effect> createReverseFx() { return std::make_unique<ReverseFx>(); }

} // namespace atm
