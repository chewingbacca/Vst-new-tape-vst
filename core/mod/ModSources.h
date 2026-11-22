// Modulation sources: LFO, chaos, envelope follower. All run at control rate.
#pragma once
#include "../model/Model.h"

namespace atm {

struct LfoParams
{
    float rate = 0.5f;      // normalised
    int   shape = 0;
    bool  sync = true;
    float phase = 0.f;      // 0..1
    int   mode = 0;         // 0 free, 1 trig, 2 one-shot
};

inline float lfoRateHz (float norm) { return 0.01f * std::pow (20000.f, clamp01 (norm)); }   // 0.01 .. 200 Hz

class Lfo
{
public:
    void reset (uint32_t seed_) { seed = seed_; phase = 0.0; cycle = 0; done = false; }
    void retrigger() { phase = 0.0; cycle = 0; done = false; }

    // Advance by dt seconds and return the value (-1..1). beat = song position for position-locked sync.
    float tick (const LfoParams& p, const float* draw, double dt, double bpm, double beat, bool transportRunning)
    {
        double ph; int64_t cyc;
        if (p.sync && p.mode == 0 && transportRunning)
        {
            // locked to the song position: deterministic wherever playback starts
            const double beats = syncDiv (syncDivFromNorm (1.f - p.rate)).beats;   // higher rate value = shorter division
            const double x = beat / beats;
            const double fl = std::floor (x);
            ph = x - fl; cyc = (int64_t) fl;
        }
        else
        {
            const double hz = p.sync ? (bpm / 60.0) / syncDiv (syncDivFromNorm (1.f - p.rate)).beats : (double) lfoRateHz (p.rate);
            if (! done) phase += hz * dt;
            if (phase >= 1.0)
            {
                if (p.mode == 2) { phase = 1.0; done = true; }
                else { const double fl = std::floor (phase); cycle += (int64_t) fl; phase -= fl; }
            }
            ph = phase; cyc = cycle;
        }
        double x = ph + (double) p.phase;
        if (! (p.mode == 2 && done)) { const double fl = std::floor (x); cyc += (int64_t) fl; x -= fl; }
        else x = std::min (x, 0.999999);
        return shapeValue (p.shape, (float) x, cyc, draw);
    }

    float shapeValue (int shape, float ph, int64_t cyc, const float* draw) const
    {
        switch (shape)
        {
            default:
            case 0: return std::sin (kTauF * ph);
            case 1: return ph < 0.25f ? ph * 4.f : (ph < 0.75f ? 2.f - ph * 4.f : ph * 4.f - 4.f);
            case 2: return 1.f - 2.f * ph;
            case 3: return 2.f * ph - 1.f;
            case 4: return ph < 0.5f ? 1.f : -1.f;
            case 5: return hashToBipolar (hashN (seed, 0x5A5Au, (uint32_t) (uint64_t) cyc));
            case 6:
            {
                const float a = hashToBipolar (hashN (seed, 0x5A5Au, (uint32_t) (uint64_t) cyc));
                const float b = hashToBipolar (hashN (seed, 0x5A5Au, (uint32_t) (uint64_t) (cyc + 1)));
                const float t = 0.5f - 0.5f * std::cos (kPiF * ph);
                return a + (b - a) * t;
            }
            case 7:
            {
                if (draw == nullptr) return 0.f;
                const float x = ph * (float) kLfoDrawPts;
                const int i = std::min ((int) x, kLfoDrawPts - 1);
                const float f = x - (float) i;
                const float a = draw[i], b = draw[(i + 1) % kLfoDrawPts];
                return a + (b - a) * f;
            }
        }
    }

private:
    uint32_t seed = 1;
    double phase = 0.0;
    int64_t cycle = 0;
    bool done = false;
};

// Lorenz attractor or logistic map. Deterministic: reset() at transport start.
class Chaos
{
public:
    void reset() { x = 0.1; y = 0.0; z = 20.0; lx = 0.37f; lPrev = 0.37f; lPhase = 0.0; outX = outY = 0.f; }
    void tick (int type, float rateHz, float shape, double dt)
    {
        if (type == 0)
        {
            // classic sigma/beta, rho swept through the interesting range
            const double sigma = 10.0, beta = 8.0 / 3.0, rho = 20.0 + 26.0 * (double) shape;
            double remaining = dt * (double) rateHz * 0.9;        // time scale: rate 1 Hz ~ one lobe orbit per second
            while (remaining > 0.0)
            {
                const double h = std::min (remaining, 0.004);
                const double dx = sigma * (y - x), dy = x * (rho - z) - y, dz = x * y - beta * z;
                x += dx * h; y += dy * h; z += dz * h;
                remaining -= h;
            }
            if (! (std::fabs (x) < 200.0)) reset();
            outX = clamp ((float) (x / 22.0), -1.f, 1.f);
            outY = clamp ((float) ((z - 25.0) / 25.0), -1.f, 1.f);
        }
        else
        {
            const float r = 3.5f + 0.5f * shape;
            lPhase += dt * (double) rateHz;
            while (lPhase >= 1.0) { lPhase -= 1.0; lPrev = lx; lx = clamp (r * lx * (1.f - lx), 0.0001f, 0.9999f); }
            const float t = 0.5f - 0.5f * std::cos (kPiF * (float) lPhase);
            outX = (lPrev + (lx - lPrev) * t) * 2.f - 1.f;
            outY = lx * 2.f - 1.f;
        }
    }
    float outX = 0.f, outY = 0.f;

private:
    double x = 0.1, y = 0.0, z = 20.0;
    float lx = 0.37f, lPrev = 0.37f; double lPhase = 0.0;
};

class EnvFollower
{
public:
    void set (float attack, float release, float gainDb, float sr)
    {
        atk = std::exp (-1.f / std::max (1.f, attack * sr));
        rel = std::exp (-1.f / std::max (1.f, release * sr));
        gain = dbToGain (gainDb);
    }
    inline void process (const float* l, const float* r, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const float in = std::max (std::fabs (l[i]), r ? std::fabs (r[i]) : 0.f) * gain;
            env = in > env ? atk * env + (1.f - atk) * in : rel * env + (1.f - rel) * in;
        }
        env = undenorm (env);
    }
    void decay (int n) { env *= std::pow (rel, (float) n); env = undenorm (env); }
    float value() const { return clamp01 (env); }
    void reset() { env = 0.f; }
private:
    float atk = 0.9f, rel = 0.999f, gain = 1.f, env = 0.f;
};

} // namespace atm
