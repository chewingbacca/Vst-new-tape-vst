// 2x oversampling with polyphase all-pass half-band filters (about 69 dB
// stop-band rejection, ~2.7 samples of low-frequency group delay round trip).
// Coefficients are the 8-coefficient / 0.01 transition-band set published with
// Laurent de Soras' HIIR library (WTFPL).
#pragma once
#include "../util/Math.h"

namespace atm {

struct HalfbandPath
{
    static constexpr int kN = 4;
    float a[kN] = {}, x1[kN] = {}, y1[kN] = {};
    inline float process (float x)
    {
        for (int i = 0; i < kN; ++i)
        {
            const float y = a[i] * (x - y1[i]) + x1[i];
            x1[i] = x; y1[i] = undenorm (y); x = y;
        }
        return x;
    }
    void reset() { for (int i = 0; i < kN; ++i) x1[i] = y1[i] = 0.f; }
};

class Oversampler2x
{
public:
    Oversampler2x()
    {
        static constexpr float c[8] = { 0.0771150798324f, 0.265968526521f, 0.482070625768f, 0.665104153263f,
                                        0.796820471934f, 0.884101508195f, 0.941251427774f, 0.982005414188f };
        for (int i = 0; i < 4; ++i) { upA.a[i] = dnA.a[i] = c[i * 2]; upB.a[i] = dnB.a[i] = c[i * 2 + 1]; }
    }
    void reset() { upA.reset(); upB.reset(); dnA.reset(); dnB.reset(); }

    // one input sample -> two samples at the double rate
    inline void up (float x, float& y0, float& y1) { y0 = upA.process (x); y1 = upB.process (x); }
    // two samples at the double rate -> one output sample
    inline float down (float x0, float x1) { return 0.5f * (dnA.process (x1) + dnB.process (x0)); }

    // convenience: run a shaper functor at 2x
    template <class F> inline float process (float x, F&& f)
    {
        float a, b; up (x, a, b);
        return down (f (a), f (b));
    }

private:
    HalfbandPath upA, upB, dnA, dnB;
};

} // namespace atm
