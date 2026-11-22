// Filters: TPT state-variable (Zavalishin / Simper form), a saturating 4-pole
// ladder, one-pole helpers and a DC blocker. All are safe to modulate at
// control rate without zipper artefacts (trapezoidal integrators).
#pragma once
#include "../util/Math.h"

namespace atm {

struct OnePoleLP
{
    float z = 0.f, a = 1.f;
    void setCutoff (float hz, float sr) { a = 1.f - std::exp (-kTauF * clamp (hz, 1.f, sr * 0.49f) / sr); }
    void setCoef (float c) { a = c; }
    inline float process (float x) { z += a * (x - z); return z; }
    void reset (float v = 0.f) { z = v; }
};

struct OnePoleHP
{
    OnePoleLP lp;
    void setCutoff (float hz, float sr) { lp.setCutoff (hz, sr); }
    inline float process (float x) { return x - lp.process (x); }
    void reset() { lp.reset(); }
};

struct DcBlocker
{
    float x1 = 0.f, y1 = 0.f, r = 0.9995f;
    void prepare (float sr, float hz = 5.f) { r = 1.f - kTauF * hz / sr; }
    inline float process (float x) { const float y = x - x1 + r * y1; x1 = x; y1 = undenorm (y); return y1; }
    void reset() { x1 = y1 = 0.f; }
};

// ---------------------------------------------------------------------------
// TPT SVF. setCoefs() at control rate, tick() per sample.
struct Svf
{
    float g = 0.1f, k = 1.f, a1 = 0.f, a2 = 0.f, a3 = 0.f;
    float ic1 = 0.f, ic2 = 0.f;

    // res 0..1 (1 = self oscillation edge)
    void setCoefs (float hz, float res, float sr)
    {
        g = std::tan (kPiF * clamp (hz, 5.f, sr * 0.49f) / sr);
        k = 2.f - 1.98f * clamp01 (res);
        a1 = 1.f / (1.f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    void setQ (float hz, float q, float sr)
    {
        g = std::tan (kPiF * clamp (hz, 5.f, sr * 0.49f) / sr);
        k = 1.f / std::max (q, 0.05f);
        a1 = 1.f / (1.f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    // Set the integrator gain directly (for per-sample interpolated cutoff sweeps).
    inline void setGK (float g_, float k_)
    {
        g = g_; k = k_;
        a1 = 1.f / (1.f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    static float gFor (float hz, float sr) { return std::tan (kPiF * clamp (hz, 5.f, sr * 0.49f) / sr); }
    static float kFor (float res) { return 2.f - 1.98f * clamp01 (res); }
    struct Out { float lp, bp, hp; };
    inline Out tick (float v0)
    {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.f * v1 - ic1;
        ic2 = 2.f * v2 - ic2;
        return { v2, v1, v0 - k * v1 - v2 };
    }
    inline float lp (float x) { return tick (x).lp; }
    inline float bp (float x) { return tick (x).bp; }
    inline float hp (float x) { return tick (x).hp; }
    void reset() { ic1 = ic2 = 0.f; }
    // keep the states sane if something upstream misbehaved
    void sanitise() { if (! (std::fabs (ic1) < 1.0e6f)) ic1 = 0.f; if (! (std::fabs (ic2) < 1.0e6f)) ic2 = 0.f; }
};

// ---------------------------------------------------------------------------
// 4-pole ladder, zero-delay feedback solved linearly, with a saturator at the
// input summing node. Self-oscillates cleanly near res = 1.
struct Ladder
{
    float g = 0.1f, G = 0.09f, k = 0.f, comp = 1.f;
    float s1 = 0.f, s2 = 0.f, s3 = 0.f, s4 = 0.f;

    void setCoefs (float hz, float res, float sr)
    {
        g = std::tan (kPiF * clamp (hz, 5.f, sr * 0.45f) / sr);
        G = g / (1.f + g);
        k = 4.1f * clamp01 (res);
        comp = 1.f + 0.6f * k;           // partial pass-band loss compensation
    }
    inline void setG (float g_) { g = g_; G = g / (1.f + g); }
    inline float tick (float x, float drive = 1.f)
    {
        const float G2 = G * G;
        const float S = (G2 * G * s1 + G2 * s2 + G * s3 + s4) / (1.f + g);
        float u = (x * comp * drive - k * S) / (1.f + k * G2 * G2);
        u = fastTanh (u);
        float v;
        v = (u - s1) * G; const float y1 = v + s1; s1 = y1 + v;
        v = (y1 - s2) * G; const float y2 = v + s2; s2 = y2 + v;
        v = (y2 - s3) * G; const float y3 = v + s3; s3 = y3 + v;
        v = (y3 - s4) * G; const float y4 = v + s4; s4 = y4 + v;
        return y4 / drive;
    }
    void reset() { s1 = s2 = s3 = s4 = 0.f; }
};

// ---------------------------------------------------------------------------
// RBJ biquad (transposed direct form II) for fixed or slowly changing EQ-like jobs.
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    inline float process (float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() { z1 = z2 = 0; }
    void set (float B0, float B1, float B2, float A0, float A1, float A2)
    { b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0; }
    void setLowpass (float hz, float q, float sr)
    {
        const float w = kTauF * clamp (hz, 5.f, sr * 0.49f) / sr, c = std::cos (w), al = std::sin (w) / (2 * q);
        set ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void setHighpass (float hz, float q, float sr)
    {
        const float w = kTauF * clamp (hz, 5.f, sr * 0.49f) / sr, c = std::cos (w), al = std::sin (w) / (2 * q);
        set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void setBandpass (float hz, float q, float sr)   // constant 0 dB peak gain
    {
        const float w = kTauF * clamp (hz, 5.f, sr * 0.49f) / sr, c = std::cos (w), al = std::sin (w) / (2 * q);
        set (al, 0, -al, 1 + al, -2 * c, 1 - al);
    }
    void setPeak (float hz, float q, float gainDb, float sr)
    {
        const float A = std::pow (10.f, gainDb / 40.f);
        const float w = kTauF * clamp (hz, 5.f, sr * 0.49f) / sr, c = std::cos (w), al = std::sin (w) / (2 * q);
        set (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    void setLowShelf (float hz, float gainDb, float sr)
    {
        const float A = std::pow (10.f, gainDb / 40.f);
        const float w = kTauF * clamp (hz, 5.f, sr * 0.49f) / sr, c = std::cos (w), s = std::sin (w);
        const float al = s / 2 * std::sqrt (2.f), sq = 2 * std::sqrt (A) * al;
        set (A * ((A + 1) - (A - 1) * c + sq), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sq),
             (A + 1) + (A - 1) * c + sq, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sq);
    }
    void setHighShelf (float hz, float gainDb, float sr)
    {
        const float A = std::pow (10.f, gainDb / 40.f);
        const float w = kTauF * clamp (hz, 5.f, sr * 0.49f) / sr, c = std::cos (w), s = std::sin (w);
        const float al = s / 2 * std::sqrt (2.f), sq = 2 * std::sqrt (A) * al;
        set (A * ((A + 1) + (A - 1) * c + sq), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sq),
             (A + 1) - (A - 1) * c + sq, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sq);
    }
};

// First-order allpass (for dispersion / phasing).
struct Allpass1
{
    float a = 0.f, x1 = 0.f, y1 = 0.f;
    inline float process (float x) { const float y = a * x + x1 - a * y1; x1 = x; y1 = undenorm (y); return y1; }
    void reset() { x1 = y1 = 0.f; }
};

} // namespace atm
