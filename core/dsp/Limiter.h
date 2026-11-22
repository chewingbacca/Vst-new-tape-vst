// Zero-latency stereo soft limiter: a fast gain computer followed by a soft
// clip that guarantees the ceiling.
#pragma once
#include "../util/Math.h"

namespace atm {

class SoftLimiter
{
public:
    void prepare (float sampleRate)
    {
        sr = sampleRate;
        relCoef = std::exp (-1.f / (0.08f * sr));
        atkCoef = std::exp (-1.f / (0.0004f * sr));
        reset();
    }
    void reset() { env = 0.f; gainRed = 1.f; }
    float gainReduction() const { return gainRed; }

    inline void process (float& l, float& r, float drive)
    {
        l *= drive; r *= drive;
        const float pk = std::max (std::fabs (l), std::fabs (r));
        env = pk > env ? atkCoef * env + (1.f - atkCoef) * pk : relCoef * env + (1.f - relCoef) * pk;
        const float over = std::max (env, pk * 0.7f);
        float g = over > kThresh ? kThresh / over : 1.f;
        gainRed = g;
        l = clip (l * g); r = clip (r * g);
    }

private:
    // transparent below the knee, then a smooth approach to the ceiling
    static inline float clip (float x)
    {
        const float a = std::fabs (x);
        if (a <= kKnee) return x;
        const float t = (a - kKnee) / (kCeil - kKnee);
        const float y = kKnee + (kCeil - kKnee) * (t / (1.f + t));
        return x < 0.f ? -y : y;
    }
    static constexpr float kThresh = 0.89f, kKnee = 0.8f, kCeil = 0.985f;
    float sr = 48000.f, env = 0.f, gainRed = 1.f, relCoef = 0.999f, atkCoef = 0.5f;
};

} // namespace atm
