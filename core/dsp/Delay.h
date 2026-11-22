// Power-of-two ring buffer delay line with linear and Hermite reads.
#pragma once
#include "../util/Math.h"
#include <vector>

namespace atm {

class DelayLine
{
public:
    void prepare (int maxSamples)
    {
        int n = 16;
        while (n < maxSamples + 8) n <<= 1;
        buf.assign ((size_t) n, 0.f);
        mask = n - 1;
        w = 0;
    }
    void reset() { std::fill (buf.begin(), buf.end(), 0.f); }
    int capacity() const { return mask + 1; }

    inline void write (float x) { buf[(size_t) w] = x; w = (w + 1) & mask; }
    // d = delay in samples behind the most recently written sample (0 = last written)
    inline float read (int d) const { return buf[(size_t) ((w - 1 - d) & mask)]; }
    inline float readLinear (float d) const
    {
        const int i = (int) d; const float f = d - (float) i;
        const float a = read (i), b = read (i + 1);
        return a + (b - a) * f;
    }
    inline float readHermite (float d) const
    {
        const int i = (int) d; const float f = d - (float) i;
        return hermite4 (read (i - 1), read (i), read (i + 1), read (i + 2), f);
    }
    // absolute access for buffer-style effects
    inline int writePos() const { return w; }
    inline float at (int pos) const { return buf[(size_t) (pos & mask)]; }
    inline float& at (int pos) { return buf[(size_t) (pos & mask)]; }
    inline float atHermite (double pos) const
    {
        const double fl = std::floor (pos);
        const int i = (int) (int64_t) fl; const float f = (float) (pos - fl);
        return hermite4 (at (i - 1), at (i), at (i + 1), at (i + 2), f);
    }

private:
    std::vector<float> buf;
    int mask = 0, w = 0;
};

} // namespace atm
