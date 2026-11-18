// Verdant — small DSP building blocks. Header-only, JUCE-free, allocation only in prepare().
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#if defined(__SSE__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 1)
 #include <xmmintrin.h>
 #define VERDANT_HAS_SSE 1
#endif

namespace verdant
{

// Turns on flush-to-zero / denormals-are-zero for the current scope, so decaying
// filter and reverb states never fall into slow denormal arithmetic. Hosts usually
// do this too, but the engine must not depend on it.
class ScopedNoDenormals
{
public:
    ScopedNoDenormals()
    {
       #if defined(VERDANT_HAS_SSE)
        saved = _mm_getcsr();
        _mm_setcsr (static_cast<unsigned int> (saved) | 0x8040u);
       #elif defined(__aarch64__)
        uint64_t fpcr;
        __asm__ __volatile__ ("mrs %0, fpcr" : "=r" (fpcr));
        saved = fpcr;
        fpcr |= (uint64_t (1) << 24);
        __asm__ __volatile__ ("msr fpcr, %0" : : "r" (fpcr));
       #endif
    }
    ~ScopedNoDenormals()
    {
       #if defined(VERDANT_HAS_SSE)
        _mm_setcsr (static_cast<unsigned int> (saved));
       #elif defined(__aarch64__)
        const uint64_t fpcr = saved;
        __asm__ __volatile__ ("msr fpcr, %0" : : "r" (fpcr));
       #endif
    }
private:
    uint64_t saved = 0;
};

inline constexpr float kPi    = 3.14159265358979323846f;
inline constexpr float kTwoPi = 6.28318530717958647692f;

inline float clampf (float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline float lerp (float a, float b, float t)     { return a + (b - a) * t; }
inline float dbToGain (float db)                  { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g)                   { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }
inline float semitonesToRatio (float st)          { return std::exp2 (st * (1.0f / 12.0f)); }
inline float smoothstep (float t)                 { t = clampf (t, 0.0f, 1.0f); return t * t * (3.0f - 2.0f * t); }

// Macro helper: m in 0..1 with 0.5 neutral -> lo..1..hi (piecewise linear).
inline float macroScale (float m, float lo, float hi)
{
    return m < 0.5f ? lerp (lo, 1.0f, m * 2.0f) : lerp (1.0f, hi, (m - 0.5f) * 2.0f);
}
// Macro helper: m in 0..1 with 0.5 neutral -> -1..0..+1.
inline float macroBipolar (float m) { return clampf (m * 2.0f - 1.0f, -1.0f, 1.0f); }

inline bool isBad (float x) { return ! (std::abs (x) < 1.0e15f); } // catches NaN, inf and absurd values

// Flush denormals without relying on FTZ/DAZ being set by the host.
inline float undenorm (float x) { return (std::abs (x) < 1.0e-20f) ? 0.0f : x; }

//==============================================================================
// Fast, deterministic RNG (xorshift32). One per module so renders are repeatable.
struct Rng
{
    uint32_t s = 0x9E3779B9u;
    void seed (uint32_t v) { s = v ? v : 0x9E3779B9u; }
    uint32_t next()
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return s;
    }
    float uni()              { return float (next() >> 8) * (1.0f / 16777216.0f); }   // [0,1)
    float bi()               { return uni() * 2.0f - 1.0f; }                            // [-1,1)
    float range (float a, float b) { return a + (b - a) * uni(); }
    int   irange (int n)     { return n <= 0 ? 0 : int (next() % uint32_t (n)); }
    bool  chance (float p)   { return uni() < p; }
};

//==============================================================================
// One-pole smoother for control values (called once per control tick).
struct Smooth
{
    float y = 0.0f, target = 0.0f, a = 1.0f;

    void setTime (float seconds, float tickRate)
    {
        a = seconds <= 0.0f ? 1.0f : 1.0f - std::exp (-1.0f / (seconds * tickRate));
    }
    void snap (float v)    { y = target = v; }
    void set (float v)     { target = v; }
    float tick()           { y += a * (target - y); return y; }
    float get() const      { return y; }
};

// Linear per-sample ramp across a control block; avoids zipper noise on gains.
struct Ramp
{
    float cur = 0.0f, inc = 0.0f;
    void snap (float v) { cur = v; inc = 0.0f; }
    void begin (float target, int n) { inc = n > 0 ? (target - cur) / float (n) : 0.0f; if (n <= 0) cur = target; }
    float next() { cur += inc; return cur; }
};

//==============================================================================
// One-pole filters.
struct OnePoleLP
{
    float z = 0.0f, a = 1.0f;
    void setCutoff (float hz, float sr) { a = 1.0f - std::exp (-kTwoPi * clampf (hz, 1.0f, sr * 0.49f) / sr); }
    void reset() { z = 0.0f; }
    float process (float x) { z += a * (x - z); return z; }
};

struct OnePoleHP
{
    OnePoleLP lp;
    void setCutoff (float hz, float sr) { lp.setCutoff (hz, sr); }
    void reset() { lp.reset(); }
    float process (float x) { return x - lp.process (x); }
};

struct DcBlocker
{
    float x1 = 0.0f, y1 = 0.0f, r = 0.9995f;
    void prepare (float sr) { r = 1.0f - (kTwoPi * 8.0f / sr); }
    void reset() { x1 = y1 = 0.0f; }
    float process (float x)
    {
        const float y = x - x1 + r * y1;
        x1 = x; y1 = undenorm (y);
        return y;
    }
};

//==============================================================================
// TPT state-variable filter (Zavalishin). Stable under fast modulation.
struct Svf
{
    float g = 0.1f, k = 1.4142f, a1 = 0, a2 = 0, a3 = 0;
    float ic1 = 0.0f, ic2 = 0.0f;
    float lp = 0, bp = 0, hp = 0;

    void set (float hz, float q, float sr)
    {
        g = std::tan (kPi * clampf (hz, 5.0f, sr * 0.45f) / sr);
        k = 1.0f / std::max (q, 0.05f);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    void reset() { ic1 = ic2 = 0.0f; }
    void process (float x)
    {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = undenorm (2.0f * v1 - ic1);
        ic2 = undenorm (2.0f * v2 - ic2);
        lp = v2; bp = v1; hp = x - k * v1 - v2;
    }
    float low (float x)  { process (x); return lp; }
    float high (float x) { process (x); return hp; }
};

// First-order shelving filters (matched pole/zero, 6 dB/oct) for gentle tone shaping.
struct Shelf1
{
    float b0 = 1, b1 = 0, a1 = 0, x1 = 0, y1 = 0;

    // Low shelf: gain applied below fc.
    void setLow (float fc, float gainDb, float sr)
    {
        const float G = dbToGain (gainDb);
        const float w = std::tan (kPi * clampf (fc, 10.0f, sr * 0.45f) / sr);
        const float sq = std::sqrt (G);
        // H(s) = (s + w*sqrt(G)) / (s + w/sqrt(G))   (unity at HF, G at DC)
        const float n0 = 1.0f + w * sq, n1 = w * sq - 1.0f;
        const float d0 = 1.0f + w / sq, d1 = w / sq - 1.0f;
        b0 = n0 / d0; b1 = n1 / d0; a1 = d1 / d0;
    }
    // High shelf: gain applied above fc.
    void setHigh (float fc, float gainDb, float sr)
    {
        const float G = dbToGain (gainDb);
        const float w = std::tan (kPi * clampf (fc, 10.0f, sr * 0.45f) / sr);
        const float sq = std::sqrt (G);
        // H(s) = (sqrt(G)*s + w) / (s/sqrt(G) + w)   (unity at DC, G at HF)
        const float n0 = sq + w, n1 = w - sq;
        const float d0 = 1.0f / sq + w, d1 = w - 1.0f / sq;
        b0 = n0 / d0; b1 = n1 / d0; a1 = d1 / d0;
    }
    void reset() { x1 = y1 = 0.0f; }
    float process (float x)
    {
        const float y = b0 * x + b1 * x1 - a1 * y1;
        x1 = x; y1 = undenorm (y);
        return y;
    }
};

//==============================================================================
// Interpolation helpers.
inline float hermite (float ym1, float y0, float y1, float y2, float t)
{
    const float c0 = y0;
    const float c1 = 0.5f * (y1 - ym1);
    const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
    const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
    return ((c3 * t + c2) * t + c1) * t + c0;
}

//==============================================================================
// Power-of-two circular buffer with fractional reads.
class Ring
{
public:
    void prepare (int minSize)
    {
        int n = 16;
        while (n < minSize + 8) n <<= 1;
        buf.assign (size_t (n), 0.0f);
        mask = n - 1;
        w = 0;
    }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }
    int  size() const { return mask + 1; }
    int  writePos() const { return w; }

    void push (float x) { buf[size_t (w)] = x; w = (w + 1) & mask; }
    void addAt (int pos, float x) { buf[size_t (pos & mask)] += x; }
    void setAt (int pos, float x) { buf[size_t (pos & mask)] = x; }
    float at (int pos) const { return buf[size_t (pos & mask)]; }
    float atAbs (int64_t pos) const { return buf[size_t (pos & int64_t (mask))]; }

    // Delay measured in samples behind the most recently written sample (0 = newest).
    float tap (int delay) const { return buf[size_t ((w - 1 - delay) & mask)]; }

    float tapLinear (float delay) const
    {
        const int   i = int (delay);
        const float f = delay - float (i);
        const int   p = w - 1 - i;
        return lerp (buf[size_t (p & mask)], buf[size_t ((p - 1) & mask)], f);
    }

    float tapHermite (float delay) const
    {
        const int   i = int (delay);
        const float f = delay - float (i);
        const int   p = w - 1 - i;
        return hermite (buf[size_t ((p + 1) & mask)], buf[size_t (p & mask)],
                        buf[size_t ((p - 1) & mask)], buf[size_t ((p - 2) & mask)], f);
    }

    // Absolute-position fractional read (position grows with time).
    float readHermite (double pos) const
    {
        const double fl = std::floor (pos);
        const int    i  = int (int64_t (fl) & int64_t (mask));
        const float  f  = float (pos - fl);
        return hermite (buf[size_t ((i - 1) & mask)], buf[size_t (i)],
                        buf[size_t ((i + 1) & mask)], buf[size_t ((i + 2) & mask)], f);
    }
    float readLinear (double pos) const
    {
        const double fl = std::floor (pos);
        const int    i  = int (int64_t (fl) & int64_t (mask));
        const float  f  = float (pos - fl);
        return lerp (buf[size_t (i)], buf[size_t ((i + 1) & mask)], f);
    }

private:
    std::vector<float> buf;
    int mask = 0, w = 0;
};

//==============================================================================
// Saturation.

// Cheap, smooth, bounded: transparent near zero, limits to +-1.
inline float softClip (float x)
{
    x = clampf (x, -1.5f, 1.5f);
    return x - (4.0f / 27.0f) * x * x * x;
}

// tanh with first-order antiderivative anti-aliasing (Parker, Zavalishin & Le Bivic, DAFx-16).
// ADAA is applied to the non-linear residual tanh(x) - x only; the linear part passes
// untouched, so the half-sample averaging inherent to ADAA does not dull the top end.
struct AdaaTanh
{
    double x1 = 0.0, F1 = 0.0;

    // antiderivative of tanh(x) - x  =  log(cosh(x)) - x^2/2
    static double F (double x)
    {
        const double ax = std::abs (x);
        return ax + std::log1p (std::exp (-2.0 * ax)) - 0.6931471805599453 - 0.5 * x * x;
    }
    void reset() { x1 = 0.0; F1 = F (0.0); }
    void prime (double x) { x1 = x; F1 = F (x); }
    float process (float xin)
    {
        const double x = double (xin);
        const double Fx = F (x);
        const double d = x - x1;
        double resid;
        if (std::abs (d) < 1.0e-4)
        {
            const double m = 0.5 * (x + x1);
            resid = std::tanh (m) - m;
        }
        else
            resid = (Fx - F1) / d;
        x1 = x; F1 = Fx;
        return float (x + resid);
    }
};

//==============================================================================
// Window lookup shared by the granular modules.
struct HannTable
{
    static constexpr int N = 1024;
    float t[N + 2];
    HannTable()
    {
        for (int i = 0; i < N + 2; ++i)
            t[i] = 0.5f - 0.5f * std::cos (kTwoPi * float (std::min (i, N)) / float (N));
    }
    // phase 0..1
    float operator() (float ph) const
    {
        const float x = clampf (ph, 0.0f, 1.0f) * float (N);
        const int   i = int (x);
        return lerp (t[i], t[i + 1], x - float (i));
    }
};
inline const HannTable& hannTable() { static const HannTable h; return h; }

// Fast sine for LFOs: phase 0..1, parabolic approximation with correction (error < 0.1 %).
inline float fastSin01 (float ph)
{
    ph -= std::floor (ph);
    float x = ph * 2.0f - 1.0f;                // -1..1
    float y = 4.0f * x * (1.0f - std::abs (x)); // parabola (inverted phase)
    y = 0.225f * (y * std::abs (y) - y) + y;
    return -y;
}

} // namespace verdant
