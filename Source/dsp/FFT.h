// Splinter DSP — minimal radix-2 FFT (own implementation).
#pragma once
#include "Common.h"
#include <complex>

namespace splinter
{
class FFT
{
public:
    explicit FFT (int order = 10) { setOrder (order); }

    void setOrder (int order)
    {
        ord = order; n = 1 << order;
        rev.resize ((size_t) n);
        cosT.resize ((size_t) n / 2); sinT.resize ((size_t) n / 2);
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < order; ++b) if (i & (1 << b)) r |= 1 << (order - 1 - b);
            rev[(size_t) i] = r;
        }
        for (int i = 0; i < n / 2; ++i)
        {
            cosT[(size_t) i] = (float) std::cos (kTwoPi * i / n);
            sinT[(size_t) i] = (float) std::sin (kTwoPi * i / n);
        }
    }
    int size() const { return n; }

    // In-place complex transform on split re/im arrays. inverse is unnormalised.
    void transform (float* re, float* im, bool inverse) const
    {
        for (int i = 0; i < n; ++i)
        {
            const int j = rev[(size_t) i];
            if (j > i) { std::swap (re[i], re[j]); std::swap (im[i], im[j]); }
        }
        for (int len = 2, stride = n / 2; len <= n; len <<= 1, stride >>= 1)
        {
            const int half = len >> 1;
            for (int start = 0; start < n; start += len)
            {
                for (int k = 0, t = 0; k < half; ++k, t += stride)
                {
                    const float wr = cosT[(size_t) t];
                    const float wi = inverse ? sinT[(size_t) t] : -sinT[(size_t) t];
                    const int a = start + k, b = a + half;
                    const float xr = re[b] * wr - im[b] * wi;
                    const float xi = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - xr; im[b] = im[a] - xi;
                    re[a] += xr; im[a] += xi;
                }
            }
        }
    }

private:
    int ord = 0, n = 0;
    std::vector<int> rev;
    std::vector<float> cosT, sinT;
};
} // namespace splinter
