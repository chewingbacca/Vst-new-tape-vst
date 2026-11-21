// Splinter DSP — shared maths helpers. JUCE-free, header only.
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace splinter
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

template <typename T> inline T clampv (T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }
template <typename T> inline T lerp (T a, T b, T t) { return a + (b - a) * t; }
inline float dbToGain (float db) { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g) { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }
inline double semisToRatio (double st) { return std::exp2 (st / 12.0); }
inline float smoothstep (float t) { t = clampv (t, 0.0f, 1.0f); return t * t * (3.0f - 2.0f * t); }
inline double floorMod (double a, double m) { const double r = std::fmod (a, m); return r < 0.0 ? r + m : r; }
inline int64_t floorDiv (int64_t a, int64_t b) { int64_t q = a / b; if ((a % b != 0) && ((a < 0) != (b < 0))) --q; return q; }
inline int floorModI (int64_t a, int64_t b) { return (int) (a - floorDiv (a, b) * b); }

// ---------------------------------------------------------------- hashing / RNG
// Stateless hash used for all "random" decisions, so that a given seed and song
// position always produce the same result (offline renders match playback).
inline uint32_t hash32 (uint32_t x)
{
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return x;
}
inline uint32_t hashCombine (uint32_t a, uint32_t b) { return hash32 (a ^ (b + 0x9e3779b9U + (a << 6) + (a >> 2))); }
inline uint32_t hash3 (uint32_t seed, uint32_t a, uint32_t b) { return hashCombine (hashCombine (hash32 (seed + 0x1234567U), a), b); }
inline float hashToUnit (uint32_t h) { return (float) (h >> 8) * (1.0f / 16777216.0f); } // [0,1)

struct Rng
{
    uint32_t s = 0x12345678U;
    explicit Rng (uint32_t seed = 1) : s (seed ? seed : 0x9e3779b9U) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float unit() { return (float) (next() >> 8) * (1.0f / 16777216.0f); }
    float bipolar() { return unit() * 2.0f - 1.0f; }
    int range (int n) { return n <= 0 ? 0 : (int) (next() % (uint32_t) n); }
};

// ---------------------------------------------------------------- smoothing
// Linear ramp to a target over a fixed time; cheap and click free.
struct LinearSmoother
{
    float current = 0.0f, target = 0.0f, step = 0.0f;
    int remaining = 0, rampSamples = 64;
    void setRampSamples (int n) { rampSamples = std::max (1, n); }
    void reset (float v) { current = target = v; remaining = 0; step = 0.0f; }
    void setTarget (float v)
    {
        if (v == target) return;
        target = v; remaining = rampSamples; step = (target - current) / (float) remaining;
    }
    inline float next()
    {
        if (remaining > 0) { current += step; if (--remaining == 0) current = target; }
        return current;
    }
    bool isSmoothing() const { return remaining > 0; }
};

// One-pole smoother with separate attack / release coefficients.
struct SlewAR
{
    float y = 0.0f, aUp = 1.0f, aDown = 1.0f;
    void setTimes (double sampleRate, double attackMs, double releaseMs)
    {
        aUp = attackMs <= 0.0 ? 1.0f : (float) (1.0 - std::exp (-1.0 / (0.001 * attackMs * sampleRate)));
        aDown = releaseMs <= 0.0 ? 1.0f : (float) (1.0 - std::exp (-1.0 / (0.001 * releaseMs * sampleRate)));
    }
    void reset (float v) { y = v; }
    inline float process (float x) { y += (x > y ? aUp : aDown) * (x - y); return y; }
};

// Linear slew limiter: moves at most `maxStep` per sample (exact, finite fades).
struct SlewLin
{
    float y = 0.0f, maxStep = 1.0f;
    void setTime (double sampleRate, double ms) { maxStep = (float) (1.0 / std::max (1.0, 0.001 * ms * sampleRate)); }
    void reset (float v) { y = v; }
    inline float process (float x)
    {
        const float d = x - y;
        y += d > maxStep ? maxStep : (d < -maxStep ? -maxStep : d);
        return y;
    }
};

// Gain fader: constant-rate ramp followed by a short one-pole that rounds the
// corners, so fades have no slope discontinuity (which would be a faint click).
struct Fader
{
    float lin = 0.0f, y = 0.0f, maxStep = 1.0f, a = 1.0f;
    void setTime (double sampleRate, double ms)
    {
        const double n = std::max (2.0, 0.001 * ms * sampleRate);
        maxStep = (float) (1.0 / (0.75 * n));
        a = (float) (1.0 - std::exp (-1.0 / (0.08 * n)));
    }
    void reset (float v) { lin = y = v; }
    inline float process (float x)
    {
        const float d = x - lin;
        lin += d > maxStep ? maxStep : (d < -maxStep ? -maxStep : d);
        y += a * (lin - y);
        if (lin == x && std::fabs (y - x) < 1.0e-5f) y = x;
        return y;
    }
};

// ---------------------------------------------------------------- lock-free SPSC queue
template <typename T, size_t Capacity>
class SpscQueue
{
public:
    bool push (const T& v)
    {
        const size_t w = writeIdx.load (std::memory_order_relaxed);
        const size_t n = (w + 1) % Capacity;
        if (n == readIdx.load (std::memory_order_acquire)) return false;
        items[w] = v;
        writeIdx.store (n, std::memory_order_release);
        return true;
    }
    bool pop (T& out)
    {
        const size_t r = readIdx.load (std::memory_order_relaxed);
        if (r == writeIdx.load (std::memory_order_acquire)) return false;
        out = items[r];
        readIdx.store ((r + 1) % Capacity, std::memory_order_release);
        return true;
    }
private:
    std::array<T, Capacity> items {};
    std::atomic<size_t> writeIdx { 0 }, readIdx { 0 };
};

// ---------------------------------------------------------------- musical scales
constexpr int kNumScales = 12;
struct ScaleDef { const char* name; int count; int steps[12]; };
inline const ScaleDef& scaleDef (int index)
{
    static const ScaleDef defs[kNumScales] = {
        { "Major",          7, { 0, 2, 4, 5, 7, 9, 11 } },
        { "Minor",          7, { 0, 2, 3, 5, 7, 8, 10 } },
        { "Dorian",         7, { 0, 2, 3, 5, 7, 9, 10 } },
        { "Phrygian",       7, { 0, 1, 3, 5, 7, 8, 10 } },
        { "Lydian",         7, { 0, 2, 4, 6, 7, 9, 11 } },
        { "Mixolydian",     7, { 0, 2, 4, 5, 7, 9, 10 } },
        { "Harmonic Minor", 7, { 0, 2, 3, 5, 7, 8, 11 } },
        { "Melodic Minor",  7, { 0, 2, 3, 5, 7, 9, 11 } },
        { "Major Pentatonic", 5, { 0, 2, 4, 7, 9 } },
        { "Minor Pentatonic", 5, { 0, 3, 5, 7, 10 } },
        { "Blues",          6, { 0, 3, 5, 6, 7, 10 } },
        { "Chromatic",     12, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } },
    };
    return defs[clampv (index, 0, kNumScales - 1)];
}

// Semitone offset (relative to the key root) of a scale degree; degrees outside
// 0..count-1 wrap into neighbouring octaves.
inline int degreeToSemis (int scale, int degree)
{
    const auto& s = scaleDef (scale);
    const int oct = (int) floorDiv (degree, s.count);
    const int idx = degree - oct * s.count;
    return oct * 12 + s.steps[idx];
}

// Nearest scale degree (may be <0 or >=count) for a semitone offset from the root.
inline int semisToNearestDegree (int scale, int semisFromRoot)
{
    const auto& s = scaleDef (scale);
    const int oct = (int) floorDiv (semisFromRoot, 12);
    const int pc = semisFromRoot - oct * 12;
    int best = 0, bestDist = 99;
    for (int i = 0; i <= s.count; ++i)
    {
        const int st = i == s.count ? 12 : s.steps[i];
        const int d = std::abs (st - pc);
        if (d < bestDist) { bestDist = d; best = i; }
    }
    return oct * s.count + best;
}

// Shift (semitones) that moves a note `semisFromRoot` by `degreeShift` scale
// steps and lands exactly on the scale (out-of-scale notes are pulled in).
inline int diatonicShift (int scale, int semisFromRoot, int degreeShift)
{
    const int deg = semisToNearestDegree (scale, semisFromRoot);
    return degreeToSemis (scale, deg + degreeShift) - semisFromRoot;
}
} // namespace splinter
