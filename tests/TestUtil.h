// Tiny test framework + signal measurement helpers shared by the unit tests
// and the render harness. Header only, no dependencies.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace testutil
{
struct Registry
{
    struct Case { std::string name; std::function<void()> fn; };
    std::vector<Case> cases;
    int failures = 0, checks = 0;
    std::string current;
    static Registry& get() { static Registry r; return r; }
};
struct Registrar { Registrar (const char* n, std::function<void()> f) { Registry::get().cases.push_back ({ n, std::move (f) }); } };

#define TEST_CASE(name) \
    static void test_##name(); \
    static testutil::Registrar reg_##name (#name, test_##name); \
    static void test_##name()

#define CHECK(cond) \
    do { ++testutil::Registry::get().checks; if (! (cond)) { ++testutil::Registry::get().failures; \
        std::printf ("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

#define CHECK_MSG(cond, ...) \
    do { ++testutil::Registry::get().checks; if (! (cond)) { ++testutil::Registry::get().failures; \
        std::printf ("  FAIL %s:%d  %s  -- ", __FILE__, __LINE__, #cond); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

inline int runAll (const char* filter = nullptr)
{
    auto& r = Registry::get();
    for (auto& c : r.cases)
    {
        if (filter && c.name.find (filter) == std::string::npos) continue;
        const int before = r.failures;
        r.current = c.name;
        std::printf ("[ RUN ] %s\n", c.name.c_str());
        std::fflush (stdout);
        c.fn();
        std::printf ("[ %s ] %s\n", r.failures == before ? " OK " : "FAIL", c.name.c_str());
    }
    std::printf ("\n%d checks, %d failures\n", r.checks, r.failures);
    return r.failures == 0 ? 0 : 1;
}

// ---------------------------------------------------------------- measurements
inline bool hasNonFinite (const std::vector<float>& v)
{
    for (float x : v) if (! std::isfinite (x)) return true;
    return false;
}
inline float peakAbs (const std::vector<float>& v, size_t from = 0, size_t to = SIZE_MAX)
{
    float p = 0.0f;
    for (size_t i = from; i < std::min (to, v.size()); ++i) p = std::max (p, std::fabs (v[i]));
    return p;
}
inline double rms (const std::vector<float>& v, size_t from = 0, size_t to = SIZE_MAX)
{
    double s = 0.0; size_t n = 0;
    for (size_t i = from; i < std::min (to, v.size()); ++i) { s += (double) v[i] * v[i]; ++n; }
    return n ? std::sqrt (s / (double) n) : 0.0;
}
inline double dcOffset (const std::vector<float>& v)
{
    double s = 0.0;
    for (float x : v) s += x;
    return v.empty() ? 0.0 : s / (double) v.size();
}

// Click detector. Works on the second difference d2 of the signal, which is
// tiny for smooth audio and spikes at a discontinuity or a kink. Two views:
//   isolated: d2 against its immediate neighbours (a few samples away) — catches
//             kinks and jumps even inside loud, bright material;
//   wide:     d2 against the typical d2 of the sounding parts of the signal —
//             catches band-limited steps whose energy is spread over a few samples.
// The score is max (isolated, wide / 4). Smooth audio scores ~1-3, a kink from a
// badly shaped fade ~10+, a hard cut 100+.
struct ClickResult { double worstRatio = 0.0; size_t position = 0; double worstValue = 0.0; };
inline ClickResult detectClicks (const std::vector<float>& v, double sampleRate, size_t from = 0, size_t to = SIZE_MAX)
{
    ClickResult res;
    to = std::min (to, v.size());
    if (to < from + 256) return res;
    const size_t n = to - from;
    std::vector<float> d2 (n, 0.0f);
    double gsum = 0.0; size_t gcount = 0;
    for (size_t i = 2; i < n; ++i)
    {
        d2[i] = std::fabs (v[from + i] - 2.0f * v[from + i - 1] + v[from + i - 2]);
        if (std::fabs (v[from + i]) > 1.0e-3f) { gsum += d2[i]; ++gcount; }
    }
    const double gm = gcount > 0 ? gsum / (double) gcount : 0.0;
    std::vector<double> prefix (n + 1, 0.0);
    for (size_t i = 0; i < n; ++i) prefix[i + 1] = prefix[i] + d2[i];
    auto mean = [&] (size_t a, size_t b) { return (prefix[b] - prefix[a]) / (double) (b - a); };   // [a, b)
    const size_t w = (size_t) std::max (64, (int) (0.010 * sampleRate));
    const size_t nearIn = 4, nearOut = (size_t) std::max (24, (int) (0.0005 * sampleRate));
    for (size_t i = w; i + w < n; ++i)
    {
        const double neighbours = 0.5 * (mean (i - nearOut, i - nearIn + 1) + mean (i + nearIn, i + nearOut + 1));
        const double iso = d2[i] / (neighbours + 0.25 * gm + 1.0e-7);
        const double wide = d2[i] / (std::max (mean (i - w, i + w), gm) + 1.0e-7);
        const double score = std::max (iso, wide * 0.25);
        if (score > res.worstRatio) { res.worstRatio = score; res.position = from + i; res.worstValue = d2[i]; }
    }
    return res;
}

// Dominant frequency by counting phase of a windowed DFT peak with parabolic
// refinement, precise to well under a cent for clean tones.
inline double dominantFrequency (const std::vector<float>& v, double sampleRate, size_t from, size_t count, double fLo = 30.0, double fHi = 8000.0)
{
    if (from + count > v.size()) count = v.size() - from;
    auto power = [&] (double f)
    {
        double re = 0.0, im = 0.0;
        const double w = 6.283185307179586 * f / sampleRate;
        for (size_t i = 0; i < count; ++i)
        {
            const double win = 0.5 - 0.5 * std::cos (6.283185307179586 * (double) i / (double) count);
            re += win * v[from + i] * std::cos (w * (double) i); im -= win * v[from + i] * std::sin (w * (double) i);
        }
        return re * re + im * im;
    };
    // coarse scan on a log grid, then golden-section refine
    double bestF = fLo, bestP = -1.0;
    const double step = std::pow (2.0, 1.0 / 48.0);
    for (double f = fLo; f <= fHi; f *= step) { const double p = power (f); if (p > bestP) { bestP = p; bestF = f; } }
    double lo = bestF / step, hi = bestF * step;
    for (int it = 0; it < 40; ++it)
    {
        const double m1 = lo + (hi - lo) * 0.382, m2 = lo + (hi - lo) * 0.618;
        if (power (m1) > power (m2)) hi = m2; else lo = m1;
    }
    return 0.5 * (lo + hi);
}
inline double centsBetween (double f, double ref) { return 1200.0 * std::log2 (f / ref); }

// ---------------------------------------------------------------- WAV out (16-bit PCM)
inline bool writeWav (const std::string& path, const std::vector<float>& l, const std::vector<float>& r, double sampleRate)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f) return false;
    const uint32_t n = (uint32_t) std::min (l.size(), r.size());
    const uint32_t dataBytes = n * 4, sr = (uint32_t) sampleRate, byteRate = sr * 4;
    const uint16_t fmt = 1, ch = 2, bits = 16, align = 4;
    const uint32_t riff = 36 + dataBytes, fmtLen = 16;
    std::fwrite ("RIFF", 1, 4, f); std::fwrite (&riff, 4, 1, f); std::fwrite ("WAVEfmt ", 1, 8, f);
    std::fwrite (&fmtLen, 4, 1, f); std::fwrite (&fmt, 2, 1, f); std::fwrite (&ch, 2, 1, f);
    std::fwrite (&sr, 4, 1, f); std::fwrite (&byteRate, 4, 1, f); std::fwrite (&align, 2, 1, f); std::fwrite (&bits, 2, 1, f);
    std::fwrite ("data", 1, 4, f); std::fwrite (&dataBytes, 4, 1, f);
    for (uint32_t i = 0; i < n; ++i)
    {
        const int16_t s[2] = { (int16_t) std::lrint (std::fmax (-1.0f, std::fmin (1.0f, l[i])) * 32767.0f),
                               (int16_t) std::lrint (std::fmax (-1.0f, std::fmin (1.0f, r[i])) * 32767.0f) };
        std::fwrite (s, 2, 2, f);
    }
    std::fclose (f);
    return true;
}
} // namespace testutil
