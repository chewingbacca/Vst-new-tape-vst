// Shared DSP building blocks for Swoon. All original code; header-only.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace swoon
{

inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr float kTwoPi = 6.28318530717958647692f;
inline constexpr int kChunk = 32;   // control-rate granularity: modules process <= kChunk samples per call

inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g) noexcept { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }
inline float centsToRatio (float cents) noexcept { return std::exp2 (cents * (1.0f / 1200.0f)); }
inline float lerp (float a, float b, float t) noexcept { return a + (b - a) * t; }
inline float clamp01 (float x) noexcept { return std::clamp (x, 0.0f, 1.0f); }
inline float smoothstep (float x) noexcept { x = clamp01 (x); return x * x * (3.0f - 2.0f * x); }

/** Exponential map of a 0..1 control onto [lo, hi]. */
inline float expMap (float t, float lo, float hi) noexcept { return lo * std::pow (hi / lo, clamp01 (t)); }

/** Smooth, bounded saturator: x / sqrt (1 + x^2). Cheap, monotonic, |y| < 1. */
inline float softSat (float x) noexcept { return x / std::sqrt (1.0f + x * x); }

/** Fast tanh-like rational approximation, accurate to about 1e-3 over [-4, 4], clamped outside. */
inline float fastTanh (float x) noexcept
{
    x = std::clamp (x, -4.97f, 4.97f);
    const float x2 = x * x;
    return x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2)))
             / (135135.0f + x2 * (62370.0f + x2 * (3150.0f + x2 * 28.0f)));
}

/** exp (x) to about 1e-7 relative error: 2^k * polynomial (2^f), f in [-0.5, 0.5]. */
inline float fastExp (float x) noexcept
{
    const float y = std::clamp (x, -80.0f, 80.0f) * 1.44269504089f;
    const int ki = static_cast<int> (y + (y < 0.0f ? -0.5f : 0.5f));   // round to nearest without a libm call
    const float k = static_cast<float> (ki);
    const float f = y - k;
    const float p = 1.0f + f * (0.693147181f + f * (0.240226507f + f * (0.0555041087f
                  + f * (0.00961812911f + f * (0.00133335581f + f * 0.000154035304f)))));
    const uint32_t bits = static_cast<uint32_t> (ki + 127) << 23;
    float scale;
    std::memcpy (&scale, &bits, sizeof (scale));
    return p * scale;
}

/** ln (x) for x > 0 to about 1e-5 absolute error. */
inline float fastLog (float x) noexcept
{
    uint32_t bits;
    std::memcpy (&bits, &x, sizeof (bits));
    const int e = static_cast<int> (bits >> 23) - 127;
    bits = (bits & 0x007FFFFFu) | 0x3F800000u;
    float m;
    std::memcpy (&m, &bits, sizeof (m));          // mantissa in [1, 2)
    const float t = (m - 1.0f) / (m + 1.0f);      // ln (m) = 2 atanh (t)
    const float t2 = t * t;
    return 0.69314718056f * static_cast<float> (e)
         + 2.0f * t * (1.0f + t2 * (0.333333333f + t2 * (0.2f + t2 * (0.142857143f + t2 * 0.111111111f))));
}

inline bool isBad (float x) noexcept { return ! (std::abs (x) < 1.0e6f); }   // catches NaN, inf and absurd values

inline int nextPow2 (int n) noexcept
{
    int p = 1;
    while (p < n)
        p <<= 1;
    return p;
}

//==============================================================================
/** One-pole parameter smoother. */
struct Smooth
{
    float y = 0.0f, target = 0.0f, coeff = 1.0f;

    void setTime (float seconds, float rate) noexcept
    {
        coeff = seconds <= 0.0f ? 1.0f : 1.0f - std::exp (-1.0f / (seconds * rate));
    }
    void snap (float v) noexcept { y = target = v; }
    void snap() noexcept { y = target; }
    float next() noexcept { y += coeff * (target - y); return y; }
    bool isSettled (float eps = 1.0e-4f) const noexcept { return std::abs (target - y) < eps; }
};

//==============================================================================
/** Small deterministic RNG (xorshift32). */
struct Rng
{
    uint32_t s = 0x9E3779B9u;
    explicit Rng (uint32_t seed = 0x9E3779B9u) : s (seed ? seed : 1u) {}
    uint32_t nextU32() noexcept { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uniform() noexcept { return static_cast<float> (nextU32() >> 8) * (1.0f / 16777216.0f); }          // [0,1)
    float bipolar() noexcept { return uniform() * 2.0f - 1.0f; }                                              // [-1,1)
};

//==============================================================================
/** Sine LFO from a normalised phase accumulator; parabolic sine is plenty for modulation. */
struct Lfo
{
    float phase = 0.0f, inc = 0.0f;

    void setRate (float hz, float rate) noexcept { inc = hz / rate; }
    void setPhase (float p) noexcept { phase = p - std::floor (p); }
    void advance (int n = 1) noexcept { phase += inc * static_cast<float> (n); phase -= std::floor (phase); }

    static float sineFromPhase (float p) noexcept
    {
        p -= std::floor (p);
        const float x = 4.0f * p - 2.0f;                 // -2..2
        const float y = x * (2.0f - std::abs (x));       // parabola, -1..1
        return 0.225f * (y * std::abs (y) - y) + y;      // refine
    }
    float sine (float phaseOffset = 0.0f) const noexcept { return sineFromPhase (phase + phaseOffset + 0.5f); }
    float tri (float phaseOffset = 0.0f) const noexcept
    {
        float p = phase + phaseOffset;
        p -= std::floor (p);
        return 4.0f * std::abs (p - 0.5f) - 1.0f;
    }
};

/** Band-limited random drift: white noise through two one-pole low-passes, unit-ish variance. */
struct DriftNoise
{
    Rng rng;
    float a = 0.0f, s1 = 0.0f, s2 = 0.0f, norm = 1.0f;

    explicit DriftNoise (uint32_t seed = 1) : rng (seed) {}

    /** rate = how often tick() is called per second. */
    void setCutoff (float hz, float rate) noexcept
    {
        a = 1.0f - std::exp (-kTwoPi * hz / rate);
        a = std::clamp (a, 1.0e-6f, 1.0f);
        // two cascaded one-poles on white noise: variance = a^2 (1 + (1-a)^2) / (1 - (1-a)^2)^3 ... normalise empirically
        const float r = 1.0f - a;
        const float var = a * a * a * a * (1.0f + r * r) / std::pow (1.0f - r * r, 3.0f) / 3.0f;   // uniform(-1,1) var = 1/3
        norm = 1.0f / std::sqrt (std::max (var, 1.0e-12f));
    }
    void reset() noexcept { s1 = s2 = 0.0f; }
    float tick() noexcept
    {
        s1 += a * (rng.bipolar() - s1);
        s2 += a * (s1 - s2);
        return std::clamp (s2 * norm, -3.0f, 3.0f);
    }
};

//==============================================================================
/** One-pole low-pass / high-pass. */
struct OnePole
{
    float z = 0.0f, a = 1.0f;

    void setCutoff (float hz, float rate) noexcept
    {
        a = 1.0f - std::exp (-kTwoPi * std::min (hz, 0.49f * rate) / rate);
    }
    void reset() noexcept { z = 0.0f; }
    float lp (float x) noexcept { z += a * (x - z); return z; }
    float hp (float x) noexcept { z += a * (x - z); return x - z; }
};

/** DC blocker (one-pole high-pass around 8 Hz). */
struct DcBlock
{
    float x1 = 0.0f, y1 = 0.0f, r = 0.999f;
    void prepare (float rate, float hz = 8.0f) noexcept { r = 1.0f - kTwoPi * hz / rate; }
    void reset() noexcept { x1 = y1 = 0.0f; }
    float process (float x) noexcept
    {
        const float y = x - x1 + r * y1;
        x1 = x;
        y1 = y;
        return y;
    }
};

/** Topology-preserving state-variable filter (trapezoidal integrators); stable under fast modulation. */
struct Svf
{
    float g = 0.1f, k = 1.414f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
    float ic1 = 0.0f, ic2 = 0.0f;
    float low = 0.0f, band = 0.0f, high = 0.0f;

    void set (float hz, float q, float rate) noexcept
    {
        g = std::tan (kPi * std::clamp (hz, 5.0f, 0.49f * rate) / rate);
        k = 1.0f / std::max (q, 0.05f);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    void reset() noexcept { ic1 = ic2 = low = band = high = 0.0f; }
    void process (float x) noexcept
    {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        low = v2;
        band = v1;
        high = x - k * v1 - v2;
    }
    float lp (float x) noexcept { process (x); return low; }
    float hp (float x) noexcept { process (x); return high; }
    float bp (float x) noexcept { process (x); return band; }
    float notch (float x) noexcept { process (x); return low + high; }
};

/** Biquad (transposed direct form II) with the usual cookbook designs. */
struct Biquad
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;

    void reset() noexcept { z1 = z2 = 0.0f; }
    float process (float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void setRaw (double nb0, double nb1, double nb2, double na0, double na1, double na2) noexcept
    {
        const double inv = 1.0 / na0;
        b0 = static_cast<float> (nb0 * inv);
        b1 = static_cast<float> (nb1 * inv);
        b2 = static_cast<float> (nb2 * inv);
        a1 = static_cast<float> (na1 * inv);
        a2 = static_cast<float> (na2 * inv);
    }

    static double clampFreq (double hz, double rate) noexcept { return std::clamp (hz, 5.0, 0.49 * rate); }

    void setPeak (double hz, double q, double gainDb, double rate) noexcept
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w = 2.0 * 3.141592653589793 * clampFreq (hz, rate) / rate;
        const double alpha = std::sin (w) / (2.0 * q), c = std::cos (w);
        setRaw (1.0 + alpha * A, -2.0 * c, 1.0 - alpha * A, 1.0 + alpha / A, -2.0 * c, 1.0 - alpha / A);
    }
    void setLowShelf (double hz, double gainDb, double rate, double slope = 0.9) noexcept
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w = 2.0 * 3.141592653589793 * clampFreq (hz, rate) / rate;
        const double c = std::cos (w), s = std::sin (w);
        const double alpha = s / 2.0 * std::sqrt ((A + 1.0 / A) * (1.0 / slope - 1.0) + 2.0);
        const double sq = 2.0 * std::sqrt (A) * alpha;
        setRaw (A * ((A + 1) - (A - 1) * c + sq), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sq),
                (A + 1) + (A - 1) * c + sq, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sq);
    }
    void setHighShelf (double hz, double gainDb, double rate, double slope = 0.9) noexcept
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w = 2.0 * 3.141592653589793 * clampFreq (hz, rate) / rate;
        const double c = std::cos (w), s = std::sin (w);
        const double alpha = s / 2.0 * std::sqrt ((A + 1.0 / A) * (1.0 / slope - 1.0) + 2.0);
        const double sq = 2.0 * std::sqrt (A) * alpha;
        setRaw (A * ((A + 1) + (A - 1) * c + sq), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sq),
                (A + 1) - (A - 1) * c + sq, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sq);
    }
    void setLowPass (double hz, double q, double rate) noexcept
    {
        const double w = 2.0 * 3.141592653589793 * clampFreq (hz, rate) / rate;
        const double alpha = std::sin (w) / (2.0 * q), c = std::cos (w);
        setRaw ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + alpha, -2 * c, 1 - alpha);
    }
    void setHighPass (double hz, double q, double rate) noexcept
    {
        const double w = 2.0 * 3.141592653589793 * clampFreq (hz, rate) / rate;
        const double alpha = std::sin (w) / (2.0 * q), c = std::cos (w);
        setRaw ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + alpha, -2 * c, 1 - alpha);
    }
};

//==============================================================================
/** Circular delay buffer with integer, linear and cubic (3rd-order Lagrange) reads. */
class DelayLine
{
public:
    void prepare (int maxDelaySamples)
    {
        const int size = nextPow2 (std::max (8, maxDelaySamples + 8));
        buffer.assign (static_cast<size_t> (size), 0.0f);
        mask = size - 1;
        write = 0;
    }
    void reset() noexcept { std::fill (buffer.begin(), buffer.end(), 0.0f); }
    int capacity() const noexcept { return mask - 6; }

    void push (float x) noexcept
    {
        write = (write + 1) & mask;
        buffer[static_cast<size_t> (write)] = x;
    }

    /** delay 0 = the sample just pushed. */
    float read (int delay) const noexcept { return buffer[static_cast<size_t> ((write - delay) & mask)]; }

    float readLinear (float delay) const noexcept
    {
        const int i = static_cast<int> (delay);
        const float f = delay - static_cast<float> (i);
        const float a = read (i), b = read (i + 1);
        return a + f * (b - a);
    }

    /** 3rd-order Lagrange; passive (|H| <= 1), needs delay >= 1. */
    float readCubic (float delay) const noexcept
    {
        const int i = static_cast<int> (delay);
        const float f = delay - static_cast<float> (i);
        const float xm1 = read (i - 1), x0 = read (i), x1 = read (i + 1), x2 = read (i + 2);
        const float c0 = -f * (f - 1.0f) * (f - 2.0f) * (1.0f / 6.0f);
        const float c1 = (f + 1.0f) * (f - 1.0f) * (f - 2.0f) * 0.5f;
        const float c2 = -(f + 1.0f) * f * (f - 2.0f) * 0.5f;
        const float c3 = (f + 1.0f) * f * (f - 1.0f) * (1.0f / 6.0f);
        return c0 * xm1 + c1 * x0 + c2 * x1 + c3 * x2;
    }

    /** Adds to a sample already in the buffer (delay 0 = most recent). */
    void addAt (int delay, float x) noexcept { buffer[static_cast<size_t> ((write - delay) & mask)] += x; }

private:
    std::vector<float> buffer;
    int mask = 0, write = 0;
};

/** Schroeder allpass with an integer delay, for diffusion. */
class Allpass
{
public:
    void prepare (int maxDelaySamples) { line.prepare (maxDelaySamples); }
    void reset() noexcept { line.reset(); }
    void setDelay (int samples) noexcept { delay = std::clamp (samples, 1, line.capacity()); }

    float process (float x, float g) noexcept
    {
        const float d = line.read (delay - 1);
        const float v = x + g * d;
        line.push (v);
        return d - g * v;
    }

    /** Variant with a (slowly) modulated fractional delay. */
    float processMod (float x, float g, float delaySamples) noexcept
    {
        const float d = line.readLinear (std::clamp (delaySamples, 1.0f, static_cast<float> (line.capacity())) - 1.0f);
        const float v = x + g * d;
        line.push (v);
        return d - g * v;
    }

private:
    DelayLine line;
    int delay = 1;
};

//==============================================================================
/** Peak envelope follower with separate attack / release. */
struct EnvFollower
{
    float env = 0.0f, att = 0.0f, rel = 0.0f;

    void setTimes (float attackSec, float releaseSec, float rate) noexcept
    {
        att = 1.0f - std::exp (-1.0f / (std::max (attackSec, 1.0e-5f) * rate));
        rel = 1.0f - std::exp (-1.0f / (std::max (releaseSec, 1.0e-5f) * rate));
    }
    void reset() noexcept { env = 0.0f; }
    float process (float x) noexcept
    {
        const float a = std::abs (x);
        env += (a > env ? att : rel) * (a - env);
        return env;
    }
};

//==============================================================================
/** Common wet/dry, bypass and sleep handling shared by all modules.

    A module is "awake" while its smoothed mix is above zero. Once the mix has
    faded to nothing the module keeps running with silent input for
    `flushSamples` so its memory empties, then sleeps (costing nothing) until
    it is switched on again. That makes bypass click-free and guarantees no
    stale tail bursts out when a module is re-enabled. */
struct ModuleGate
{
    Smooth mix;
    int flushRemaining = 0, warmRemaining = 0;
    bool flushing = false, asleep = true, woke = false;

    void prepare (float rate) noexcept
    {
        mix.setTime (0.012f, rate);
        reset();
    }

    void reset() noexcept
    {
        mix.snap (0.0f);
        flushRemaining = warmRemaining = 0;
        flushing = false;
        asleep = true;
        woke = false;
    }

    /** Returns false when the module can be skipped for this chunk. `woke` is true for the first chunk after sleeping.
        warmupSamples: how long a module with internal latency must run before its wet signal is valid;
        the mix is held at zero for that long after waking so the wet path never starts mid-air. */
    bool begin (bool on, float mixTarget, int numSamples, int flushSamples, bool snapNow, int warmupSamples = 0) noexcept
    {
        const float target = on ? clamp01 (mixTarget) : 0.0f;

        woke = false;
        if (target > 0.0f)
        {
            if (asleep && ! snapNow)
                warmRemaining = warmupSamples;
            woke = asleep;
            asleep = false;
            flushing = false;
            flushRemaining = flushSamples;

            if (warmRemaining > 0)
            {
                warmRemaining -= numSamples;
                mix.target = 0.0f;
            }
            else
            {
                mix.target = target;
            }
            if (snapNow)
                mix.snap (target);
            return true;
        }

        mix.target = 0.0f;
        if (snapNow)
            mix.snap();
        warmRemaining = 0;

        if (mix.y > 1.0e-4f)
        {
            flushing = false;
            return true;   // still fading out
        }

        mix.y = 0.0f;
        if (! asleep && flushRemaining > 0)
        {
            flushRemaining -= numSamples;
            flushing = true;
            return true;
        }
        flushing = false;
        asleep = true;
        return false;
    }
};

/** Auto-swell: on each detected note onset the gain drops and fades back in, softening pick attacks
    like a volume pedal rocked by hand. amount = 0 disables it (gain stays at exactly 1). */
struct AutoSwell
{
    EnvFollower fast, slow;
    float gainTarget = 1.0f, gain = 1.0f, fall = 0.01f, rate = 48000.0f;
    int holdoff = 0;

    void prepare (float fs) noexcept
    {
        rate = fs;
        fast.setTimes (0.0008f, 0.02f, fs);
        slow.setTimes (0.03f, 0.3f, fs);
        fall = 1.0f - std::exp (-1.0f / (0.003f * fs));
        reset();
    }
    void reset() noexcept
    {
        fast.reset();
        slow.reset();
        gainTarget = gain = 1.0f;
        holdoff = 0;
    }

    float process (float peak, float amount) noexcept
    {
        const float slowBefore = slow.env;
        const float f = fast.process (peak);
        slow.process (peak);

        if (amount <= 0.0005f)
        {
            gainTarget = 1.0f;
        }
        else
        {
            if (holdoff > 0)
                --holdoff;
            else if (f > 0.004f && f > 1.8f * slowBefore + 0.002f)
            {
                gainTarget = std::min (gainTarget, 1.0f - 0.97f * std::sqrt (amount));
                holdoff = static_cast<int> (0.08f * rate);
            }
            // rise time 25 ms .. 700 ms
            const float riseSeconds = 0.025f + 0.675f * amount * amount;
            gainTarget += (1.0f - gainTarget) * (1.0f - std::exp (-1.0f / (riseSeconds * 0.4f * rate)));
        }
        gain += fall * (gainTarget - gain);
        return gain;
    }
};

/** Click-free switching of a discrete mode: dips a gain to zero, swaps, comes back. */
struct ModeSwitch
{
    int current = -1, pending = -1;
    float gain = 1.0f, step = 0.01f;

    void prepare (float rate, float fadeSeconds = 0.008f) noexcept { step = 1.0f / (fadeSeconds * rate); }
    void reset() noexcept { current = pending = -1; gain = 1.0f; }

    /** Call once per chunk with the requested mode. */
    void request (int mode) noexcept
    {
        if (current < 0)
            current = mode;
        pending = mode;
    }

    /** Call per sample; returns the gain to apply to the mode-dependent signal.
        `changed` is set on the sample where the mode actually swaps (so state can be cleared). */
    float tick (bool& changed) noexcept
    {
        changed = false;
        if (pending != current)
        {
            gain -= step;
            if (gain <= 0.0f)
            {
                gain = 0.0f;
                current = pending;
                changed = true;
            }
        }
        else if (gain < 1.0f)
        {
            gain = std::min (1.0f, gain + step);
        }
        return gain;
    }
};

} // namespace swoon
