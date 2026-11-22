// Band-limited oscillator building blocks (polyBLEP) and a sine phasor.
#pragma once
#include "../util/Math.h"

namespace atm {

// Two-sample polynomial band-limited step residual. t = phase 0..1, dt = phase increment.
inline float polyBlep (float t, float dt)
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.f; }
    if (t > 1.f - dt) { t = (t - 1.f) / dt; return t * t + t + t + 1.f; }
    return 0.f;
}

struct Phasor
{
    float ph = 0.f;
    inline float tick (float inc) { const float p = ph; ph += inc; ph -= std::floor (ph); return p; }
    void reset (float p = 0.f) { ph = p - std::floor (p); }
};

inline float sawBlep (float ph, float inc) { return 2.f * ph - 1.f - polyBlep (ph, inc); }
inline float pulseBlep (float ph, float inc, float pw)
{
    float v = ph < pw ? 1.f : -1.f;
    v += polyBlep (ph, inc);
    float t2 = ph - pw; if (t2 < 0.f) t2 += 1.f;
    v -= polyBlep (t2, inc);
    return v - (2.f * pw - 1.f);      // remove DC for asymmetric widths
}
inline float sin01 (float ph) { return std::sin (kTauF * ph); }
inline float tri01 (float ph) { return 4.f * std::fabs (ph - 0.5f) - 1.f; }

} // namespace atm
