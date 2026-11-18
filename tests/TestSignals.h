// Synthesised test signals + minimal WAV I/O for the offline renderer and tests.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "dsp/Util.h"

namespace vtest
{

struct Stereo
{
    std::vector<float> l, r;
    void resize (size_t n) { l.assign (n, 0.0f); r.assign (n, 0.0f); }
    size_t size() const { return l.size(); }
};

//==============================================================================
inline bool writeWav (const std::string& path, const Stereo& s, double sr)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (f == nullptr) return false;

    const uint32_t n = uint32_t (s.size());
    const uint32_t dataBytes = n * 2 * 4;
    auto w32 = [f] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
    auto w16 = [f] (uint16_t v) { std::fwrite (&v, 2, 1, f); };

    std::fwrite ("RIFF", 1, 4, f); w32 (36 + dataBytes);
    std::fwrite ("WAVE", 1, 4, f);
    std::fwrite ("fmt ", 1, 4, f); w32 (16);
    w16 (3);                      // IEEE float
    w16 (2);
    w32 (uint32_t (sr));
    w32 (uint32_t (sr) * 2 * 4);
    w16 (8); w16 (32);
    std::fwrite ("data", 1, 4, f); w32 (dataBytes);

    std::vector<float> inter (size_t (n) * 2);
    for (uint32_t i = 0; i < n; ++i) { inter[2 * i] = s.l[i]; inter[2 * i + 1] = s.r[i]; }
    std::fwrite (inter.data(), 4, inter.size(), f);
    std::fclose (f);
    return true;
}

//==============================================================================
// Signal generators. All return `seconds` of audio; the sound itself may be shorter.

inline Stereo silence (double sr, double seconds)
{
    Stereo s; s.resize (size_t (sr * seconds));
    return s;
}

inline Stereo impulse (double sr, double seconds)
{
    Stereo s; s.resize (size_t (sr * seconds));
    const size_t at = size_t (0.05 * sr);
    s.l[at] = s.r[at] = 0.9f;
    return s;
}

inline Stereo sineSweep (double sr, double seconds, double sweepSeconds = 4.0, float amp = 0.25f)
{
    Stereo s; s.resize (size_t (sr * seconds));
    const double f0 = 20.0, f1 = std::min (20000.0, sr * 0.45);
    const double k = std::log (f1 / f0);
    const size_t n = std::min (s.size(), size_t (sweepSeconds * sr));
    for (size_t i = 0; i < n; ++i)
    {
        const double t = double (i) / sr;
        const double ph = 2.0 * M_PI * f0 * sweepSeconds / k * (std::exp (t / sweepSeconds * k) - 1.0);
        float env = 1.0f;
        const float fade = float (0.01 * sr);
        if (float (i) < fade) env = float (i) / fade;
        if (float (n - i) < fade) env = float (n - i) / fade;
        s.l[i] = s.r[i] = amp * env * float (std::sin (ph));
    }
    return s;
}

inline Stereo noiseBurst (double sr, double seconds, double burstSeconds = 0.25, float amp = 0.25f)
{
    Stereo s; s.resize (size_t (sr * seconds));
    verdant::Rng rng; rng.seed (1234);
    const size_t start = size_t (0.05 * sr);
    const size_t n = size_t (burstSeconds * sr);
    for (size_t i = 0; i < n && start + i < s.size(); ++i)
    {
        float env = 1.0f;
        const float fade = float (0.005 * sr);
        if (float (i) < fade) env = float (i) / fade;
        if (float (n - i) < fade) env = float (n - i) / fade;
        s.l[start + i] = amp * env * rng.bi();
        s.r[start + i] = amp * env * rng.bi();
    }
    return s;
}

// Continuous steady sine, for latency / gain / distortion measurements.
inline Stereo sine (double sr, double seconds, double freq, float amp = 0.25f)
{
    Stereo s; s.resize (size_t (sr * seconds));
    for (size_t i = 0; i < s.size(); ++i)
        s.l[i] = s.r[i] = amp * float (std::sin (2.0 * M_PI * freq * double (i) / sr));
    return s;
}

// One synth note: two detuned saws -> one-pole lowpass with decaying cutoff -> ADSR.
inline void addSynthNote (Stereo& s, double sr, double start, double dur, double midi, float amp)
{
    const double f = 440.0 * std::pow (2.0, (midi - 69.0) / 12.0);
    const size_t i0 = size_t (start * sr);
    const size_t n = size_t ((dur + 0.4) * sr);
    double ph1 = 0.0, ph2 = 0.3;
    float lp = 0.0f;
    for (size_t i = 0; i < n && i0 + i < s.size(); ++i)
    {
        const double t = double (i) / sr;
        // PolyBLEP-free but band-limited enough for a test: sum of harmonics up to Nyquist/2
        float saw = 0.0f;
        const int maxH = std::min (24, int (sr * 0.4 / f));
        for (int h = 1; h <= maxH; ++h)
            saw += float (std::sin (2.0 * M_PI * (ph1 * h)) / h + std::sin (2.0 * M_PI * (ph2 * h)) / h);
        saw *= 0.3f;
        ph1 += f / sr;           if (ph1 >= 1.0) ph1 -= 1.0;
        ph2 += f * 1.004 / sr;   if (ph2 >= 1.0) ph2 -= 1.0;

        const float cutoff = float (600.0 + 3500.0 * std::exp (-t * 6.0));
        const float a = 1.0f - std::exp (-2.0f * float (M_PI) * cutoff / float (sr));
        lp += a * (saw - lp);

        float env;
        if (t < 0.005)      env = float (t / 0.005);
        else if (t < dur)   env = float (0.35 + 0.65 * std::exp (-(t - 0.005) * 5.0));
        else                env = float ((0.35 + 0.65 * std::exp (-(dur - 0.005) * 5.0)) * std::exp (-(t - dur) * 14.0));

        const float v = amp * env * lp;
        s.l[i0 + i] += v;
        s.r[i0 + i] += v;
    }
}

// A short dry synth phrase in C major at 120 bpm (4 s of notes).
inline Stereo phrase (double sr, double seconds)
{
    Stereo s; s.resize (size_t (sr * seconds));
    const double notes[][3] = {
        { 0.0, 0.45, 60 }, { 0.5, 0.45, 64 }, { 1.0, 0.45, 67 }, { 1.5, 0.9, 72 },
        { 2.5, 0.45, 69 }, { 3.0, 0.9, 67 },
    };
    for (const auto& n : notes)
        addSynthNote (s, sr, 0.05 + n[0], n[1], n[2], 0.5f);
    return s;
}

// Karplus-Strong plucked string: "a single guitar note".
inline Stereo pluck (double sr, double seconds, double midi = 52.0, float amp = 0.6f)
{
    Stereo s; s.resize (size_t (sr * seconds));
    const double f = 440.0 * std::pow (2.0, (midi - 69.0) / 12.0);
    const int N = std::max (2, int (sr / f));
    std::vector<float> line (static_cast<size_t> (N));
    verdant::Rng rng; rng.seed (99);
    float lpn = 0.0f;
    for (auto& v : line) { lpn += 0.5f * (rng.bi() - lpn); v = lpn; }
    const size_t start = size_t (0.05 * sr);
    int idx = 0;
    float prev = 0.0f;
    const float damp = float (std::pow (0.001, 1.0 / (2.5 * f)));  // ~2.5 s decay
    for (size_t i = start; i < s.size(); ++i)
    {
        const float cur = line[size_t (idx)];
        const float nv = damp * 0.5f * (cur + prev);
        prev = cur;
        line[size_t (idx)] = nv;
        idx = (idx + 1) % N;
        s.l[i] = s.r[i] = amp * cur;
    }
    return s;
}

// A sung "ah" with vibrato: glottal pulse train through three formant resonators.
inline Stereo voice (double sr, double seconds, double noteSeconds = 2.5, double midi = 57.0, float amp = 0.35f)
{
    Stereo s; s.resize (size_t (sr * seconds));
    const double f0 = 440.0 * std::pow (2.0, (midi - 69.0) / 12.0);
    verdant::Svf f1, f2, f3;
    f1.set (700.0f, 8.0f, float (sr));
    f2.set (1220.0f, 10.0f, float (sr));
    f3.set (2600.0f, 12.0f, float (sr));
    const size_t start = size_t (0.05 * sr);
    const size_t n = size_t (noteSeconds * sr);
    double ph = 0.0;
    for (size_t i = 0; i < n && start + i < s.size(); ++i)
    {
        const double t = double (i) / sr;
        const double vib = 1.0 + 0.006 * std::sin (2.0 * M_PI * 5.2 * t) * std::min (1.0, t * 2.0);
        ph += f0 * vib / sr; if (ph >= 1.0) ph -= 1.0;
        // band-limited-ish glottal pulse: harmonics with -12 dB/oct tilt
        float src = 0.0f;
        const int maxH = std::min (30, int (sr * 0.4 / f0));
        for (int h = 1; h <= maxH; ++h)
            src += float (std::sin (2.0 * M_PI * ph * h)) / float (h * h) * 3.0f;
        f1.process (src); f2.process (src); f3.process (src);
        float v = 0.5f * f1.bp + 0.35f * f2.bp + 0.2f * f3.bp;
        float env = 1.0f;
        if (t < 0.08) env = float (t / 0.08);
        if (noteSeconds - t < 0.25) env = float ((noteSeconds - t) / 0.25);
        v *= amp * env;
        s.l[start + i] = s.r[start + i] = v;
    }
    return s;
}

// Hostile input: full-scale square wave + DC + a hard discontinuity.
inline Stereo hostile (double sr, double seconds)
{
    Stereo s; s.resize (size_t (sr * seconds));
    const size_t n = std::min (s.size(), size_t (2.0 * sr));
    for (size_t i = 0; i < n; ++i)
    {
        const double t = double (i) / sr;
        const float sq = (std::fmod (t * 110.0, 1.0) < 0.5) ? 1.0f : -1.0f;
        s.l[i] = sq * 1.0f + 0.2f;
        s.r[i] = -sq * 1.0f + 0.2f;
        if (t > 1.0 && t < 1.2) { s.l[i] = 4.0f; s.r[i] = -4.0f; } // way over full scale
    }
    return s;
}

inline Stereo makeSignal (const std::string& name, double sr, double seconds)
{
    if (name == "impulse") return impulse (sr, seconds);
    if (name == "sweep")   return sineSweep (sr, seconds);
    if (name == "noise")   return noiseBurst (sr, seconds);
    if (name == "phrase")  return phrase (sr, seconds);
    if (name == "pluck")   return pluck (sr, seconds);
    if (name == "voice")   return voice (sr, seconds);
    if (name == "hostile") return hostile (sr, seconds);
    if (name == "sine")    return sine (sr, seconds, 440.0);
    return silence (sr, seconds);
}

//==============================================================================
struct Stats
{
    double peak = 0, rms = 0, dc = 0, tailRms = 0, headRms = 0;
    size_t bad = 0;
    double correlation = 0; // L/R correlation, 1 = mono
};

inline Stats analyse (const Stereo& s, double sr, double tailSeconds = 1.0)
{
    Stats st;
    const size_t n = s.size();
    if (n == 0) return st;
    double sum = 0, sumSq = 0, lr = 0, ll = 0, rr = 0;
    const size_t tailStart = n > size_t (tailSeconds * sr) ? n - size_t (tailSeconds * sr) : 0;
    double tailSq = 0;
    for (size_t i = 0; i < n; ++i)
    {
        const float a = s.l[i], b = s.r[i];
        if (! std::isfinite (a) || ! std::isfinite (b)) { ++st.bad; continue; }
        st.peak = std::max (st.peak, double (std::max (std::abs (a), std::abs (b))));
        sum += a + b;
        sumSq += double (a) * a + double (b) * b;
        lr += double (a) * b; ll += double (a) * a; rr += double (b) * b;
        if (i >= tailStart) tailSq += double (a) * a + double (b) * b;
    }
    st.dc = sum / double (2 * n);
    st.rms = std::sqrt (sumSq / double (2 * n));
    st.tailRms = std::sqrt (tailSq / double (2 * (n - tailStart)));
    st.correlation = (ll > 0 && rr > 0) ? lr / std::sqrt (ll * rr) : 1.0;
    return st;
}

inline double toDb (double x) { return 20.0 * std::log10 (std::max (x, 1.0e-12)); }

} // namespace vtest
