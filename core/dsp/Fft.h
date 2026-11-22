// Radix-2 complex FFT (in place, interleaved re/im), plus Hann window helper.
// Sizes are fixed at prepare() so no allocation happens while processing.
#pragma once
#include "../util/Math.h"
#include <vector>

namespace atm {

class Fft
{
public:
    void prepare (int size)   // power of two
    {
        n = size;
        bits = 0; while ((1 << bits) < n) ++bits;
        rev.resize ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            rev[(size_t) i] = r;
        }
        cosT.resize ((size_t) n / 2); sinT.resize ((size_t) n / 2);
        for (int i = 0; i < n / 2; ++i)
        {
            cosT[(size_t) i] = (float) std::cos (kTau * i / n);
            sinT[(size_t) i] = (float) std::sin (kTau * i / n);
        }
    }
    int size() const { return n; }

    // data: n complex values as re[i], im[i]
    void forward (float* re, float* im) const { transform (re, im, false); }
    void inverse (float* re, float* im) const   // includes 1/n scaling
    {
        transform (re, im, true);
        const float s = 1.f / (float) n;
        for (int i = 0; i < n; ++i) { re[i] *= s; im[i] *= s; }
    }

private:
    void transform (float* re, float* im, bool inv) const
    {
        for (int i = 0; i < n; ++i)
        {
            const int j = rev[(size_t) i];
            if (j > i) { std::swap (re[i], re[j]); std::swap (im[i], im[j]); }
        }
        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len >> 1, step = n / len;
            for (int i = 0; i < n; i += len)
            {
                for (int j = 0, t = 0; j < half; ++j, t += step)
                {
                    const float wr = cosT[(size_t) t];
                    const float wi = inv ? sinT[(size_t) t] : -sinT[(size_t) t];
                    const int a = i + j, b = a + half;
                    const float xr = re[b] * wr - im[b] * wi;
                    const float xi = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - xr; im[b] = im[a] - xi;
                    re[a] += xr;        im[a] += xi;
                }
            }
        }
    }
    int n = 0, bits = 0;
    std::vector<int> rev;
    std::vector<float> cosT, sinT;
};

inline void makeHann (float* w, int n, bool periodic = true)
{
    const double d = periodic ? n : n - 1;
    for (int i = 0; i < n; ++i) w[i] = (float) (0.5 - 0.5 * std::cos (kTau * i / d));
}

} // namespace atm
