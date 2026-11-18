// Verdant — organic modulation sources: drifting random LFOs and input followers.
#pragma once

#include "Util.h"

namespace verdant
{

//==============================================================================
// A slow, never-repeating modulator in -1..1: two incommensurate sines plus a
// band-limited random walk. Ticked at control rate.
class DriftLfo
{
public:
    void prepare (float tickRate_, uint32_t seed, float baseHz_)
    {
        tickRate = tickRate_;
        baseHz = baseHz_;
        rng.seed (seed);
        reset();
    }

    void reset()
    {
        ph1 = rng.uni(); ph2 = rng.uni();
        walkTarget = rng.bi(); walk1 = walk2 = walkTarget * 0.5f;
        countdown = 0.0f;
        value = 0.0f;
    }

    // rateMul: Growth-scaled speed multiplier.
    float tick (float rateMul)
    {
        const float f = baseHz * rateMul;
        ph1 += f / tickRate;            if (ph1 >= 1.0f) ph1 -= 1.0f;
        ph2 += f * 0.3819660f / tickRate; if (ph2 >= 1.0f) ph2 -= 1.0f;

        countdown -= f / tickRate;
        if (countdown <= 0.0f)
        {
            walkTarget = rng.bi();
            countdown = rng.range (0.35f, 1.1f);
        }
        // two cascaded one-poles: smooth acceleration, no corners
        const float a = clampf (kTwoPi * f * 1.6f / tickRate, 0.0f, 1.0f);
        walk1 += a * (walkTarget - walk1);
        walk2 += a * (walk1 - walk2);

        value = clampf (0.45f * fastSin01 (ph1) + 0.25f * fastSin01 (ph2) + 0.75f * walk2, -1.0f, 1.0f);
        return value;
    }

    float get() const { return value; }

private:
    Rng rng;
    float tickRate = 1500.0f, baseHz = 0.1f;
    float ph1 = 0, ph2 = 0, walkTarget = 0, walk1 = 0, walk2 = 0, countdown = 0, value = 0;
};

//==============================================================================
// Input followers (per-sample). `env` is a fast musical envelope (0..~1), `slow`
// a long-term energy estimate, `active` a gate with hysteresis and hang time.
class Follower
{
public:
    void prepare (float sr)
    {
        atk  = 1.0f - std::exp (-1.0f / (0.004f * sr));
        rel  = 1.0f - std::exp (-1.0f / (0.280f * sr));
        slowAtk = 1.0f - std::exp (-1.0f / (0.35f * sr));
        slowRel = 1.0f - std::exp (-1.0f / (2.5f * sr));
        hangSamples = int (0.06f * sr);
        reset();
    }

    void reset() { env = slow = 0.0f; hang = 0; active = false; }

    inline void process (float l, float r)
    {
        const float x = std::max (std::abs (l), std::abs (r));
        env  += (x > env ? atk : rel) * (x - env);
        slow += (env > slow ? slowAtk : slowRel) * (env - slow);

        if (env > 0.004f)            { active = true; hang = hangSamples; }   // about -48 dBFS
        else if (env < 0.0015f)      { if (hang > 0) --hang; else active = false; }
    }

    // Perceptually scaled 0..1 (about -50 dBFS .. 0 dBFS -> 0..1)
    float shaped() const { return clampf (1.0f + gainToDb (env + 1.0e-6f) / 50.0f, 0.0f, 1.0f); }

    float env = 0.0f, slow = 0.0f;
    bool active = false;

private:
    float atk = 0.1f, rel = 0.001f, slowAtk = 0.001f, slowRel = 0.0001f;
    int hang = 0, hangSamples = 2400;
};

} // namespace verdant
