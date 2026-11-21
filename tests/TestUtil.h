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

// Click detector. A click is a sample whose high-passed (2nd difference) value
// towers over the local level of the same quantity. Returns the worst ratio and
// its position. Works on tonal or band-limited material; for noisy drums use
// compareAtBoundaries style checks instead.
struct ClickResult { double worstRatio = 0.0; size_t position = 0; double worstValue = 0.0; };
inline ClickResult detectClicks (const std::vector<float>& v, double sampleRate, size_t from = 0, size_t to = SIZE_MAX)
{
    ClickResult res;
    to = std::min (to, v.size());
    if (to < from + 64) return res;
    const size_t n = to - from;
    std::vector<float> d2 (n, 0.0f);
    for (size_t i = 2; i < n; ++i) d2[i] = std::fabs (v[from + i] - 2.0f * v[from + i - 1] + v[from + i - 2]);
    // local reference: mean |d2| over +-10 ms, excluding the +-0.3 ms around the sample
    const int w = std::max (32, (int) (0.010 * sampleRate)), ex = std::max (3, (int) (0.0003 * sampleRate));
    std::vector<double> prefix (n + 1, 0.0);
    for (size_t i = 0; i < n; ++i) prefix[i + 1] = prefix[i] + d2[i];
    double globalMean = prefix[n] / (double) n;
    for (size_t i = (size_t) w; i + (size_t) w < n; ++i)
    {
        const double total = prefix[i + (size_t) w] - prefix[i - (size_t) w];
        const double mid = prefix[i + (size_t) ex + 1] - prefix[i - (size_t) ex];
        const double ref = (total - mid) / (double) (2 * w - 2 * ex - 1);
        const double floor_ = std::max (ref, globalMean * 0.05) + 1.0e-7;
        const double ratio = d2[i] / floor_;
        if (ratio > res.worstRatio) { res.worstRatio = ratio; res.position = from + i; res.worstValue = d2[i]; }
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
        const double w = 2.0 * M_PI * f / sampleRate;
        for (size_t i = 0; i < count; ++i)
        {
            const double win = 0.5 - 0.5 * std::cos (2.0 * M_PI * (double) i / (double) count);
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
