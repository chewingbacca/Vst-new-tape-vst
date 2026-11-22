// Small math helpers shared by the whole core. No dependencies beyond <cmath>.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <limits>

namespace atm {

constexpr double kPi  = 3.14159265358979323846;
constexpr double kTau = 6.28318530717958647692;
constexpr float  kPiF  = 3.14159265358979323846f;
constexpr float  kTauF = 6.28318530717958647692f;

template <class T> constexpr T clamp (T x, T lo, T hi) { return x < lo ? lo : (x > hi ? hi : x); }
constexpr float clamp01 (float x) { return x < 0.f ? 0.f : (x > 1.f ? 1.f : x); }
template <class T> constexpr T lerp (T a, T b, T t) { return a + (b - a) * t; }

inline float dbToGain (float db) { return std::pow (10.f, db * 0.05f); }
inline float gainToDb (float g)  { return 20.f * std::log10 (std::max (g, 1.0e-9f)); }
inline float semitonesToRatio (float st) { return std::exp2 (st * (1.f / 12.f)); }
inline double midiToHz (double note) { return 440.0 * std::exp2 ((note - 69.0) / 12.0); }

// One-pole coefficient for a time constant in seconds at a given update rate.
inline float onePoleCoef (float seconds, float rate)
{
    if (seconds <= 0.f) return 1.f;
    return 1.f - std::exp (-1.f / (seconds * rate));
}

// Cheap tanh-like saturator, exact slope 1 at 0, limits to +-1. Smooth (C1).
inline float softClip (float x)
{
    x = clamp (x, -3.f, 3.f);
    return x * (27.f + x * x) / (27.f + 9.f * x * x);
}

// Pade tanh, accurate to ~1e-3 over the useful range, clamps beyond.
inline float fastTanh (float x)
{
    x = clamp (x, -4.97f, 4.97f);
    const float x2 = x * x;
    return x * (135135.f + x2 * (17325.f + x2 * (378.f + x2)))
             / (135135.f + x2 * (62370.f + x2 * (3150.f + x2 * 28.f)));
}

// Parabolic sine, input phase 0..1, error < 0.1%.
inline float fastSin01 (float ph)
{
    ph -= std::floor (ph);
    float x = ph * 2.f - 1.f;                   // -1..1 covers one cycle, shifted half a period
    float y = 4.f * x * (1.f - std::fabs (x));  // parabola
    y = 0.225f * (y * std::fabs (y) - y) + y;   // refine
    return -y;
}

inline bool isFinite (float x) { return std::isfinite (x); }

// Flush tiny values: avoids denormal stalls in feedback paths when FTZ is not set.
inline float undenorm (float x) { return (std::fabs (x) < 1.0e-18f) ? 0.f : x; }

inline int floorToInt (double x) { return (int) std::floor (x); }
inline int64_t floorToInt64 (double x) { return (int64_t) std::floor (x); }
inline int wrapMod (int a, int n) { int r = a % n; return r < 0 ? r + n : r; }
inline int64_t wrapMod64 (int64_t a, int64_t n) { int64_t r = a % n; return r < 0 ? r + n : r; }
inline int64_t floorDiv64 (int64_t a, int64_t n) { int64_t q = a / n; if ((a % n != 0) && ((a < 0) != (n < 0))) --q; return q; }

// 4-point, 3rd-order Hermite interpolation (Catmull-Rom). t in 0..1 between y1 and y2.
inline float hermite4 (float ym1, float y0, float y1, float y2, float t)
{
    const float c0 = y0;
    const float c1 = 0.5f * (y1 - ym1);
    const float c2 = ym1 - 2.5f * y0 + 2.f * y1 - 0.5f * y2;
    const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
    return ((c3 * t + c2) * t + c1) * t + c0;
}

} // namespace atm
