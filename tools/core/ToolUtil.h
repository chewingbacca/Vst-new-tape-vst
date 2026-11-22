// Shared helpers for the offline core tools.
#pragma once
#include "util/Wav.h"
#include "util/Math.h"
#include "util/Rng.h"
#include "voices/SampleData.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace tool {

struct Args
{
    std::vector<std::string> positional;
    std::vector<std::pair<std::string, std::string>> kv;      // in order
    std::string get (const std::string& k, const std::string& def = "") const
    {
        for (auto& p : kv) if (p.first == k) return p.second;
        return def;
    }
    double num (const std::string& k, double def) const { const auto s = get (k); return s.empty() ? def : std::atof (s.c_str()); }
    bool has (const std::string& k) const { for (auto& p : kv) if (p.first == k) return true; return false; }
};

inline Args parseArgs (int argc, char** argv)
{
    Args a;
    for (int i = 1; i < argc; ++i)
    {
        const std::string s = argv[i];
        const size_t eq = s.find ('=');
        if (eq == std::string::npos) a.positional.push_back (s);
        else a.kv.push_back ({ s.substr (0, eq), s.substr (eq + 1) });
    }
    return a;
}

struct Stats
{
    double peak = 0, rms = 0, dc = 0, maxDiff = 0, tail = 0; bool nan = false; int clickCount = 0;
};

// maxDiff: largest sample-to-sample jump. clickCount: jumps that are far larger than the local signal
// would explain (a crude click detector: |d| > 0.25 and > 6x the median |d| of the surrounding 2 ms).
inline Stats analyse (const std::vector<float>& x, double sr)
{
    Stats s;
    const size_t n = x.size();
    if (n == 0) return s;
    double sum = 0, sq = 0;
    for (size_t i = 0; i < n; ++i)
    {
        const float v = x[i];
        if (! std::isfinite (v)) { s.nan = true; continue; }
        s.peak = std::max (s.peak, (double) std::fabs (v)); sum += v; sq += (double) v * v;
        if (i > 0) s.maxDiff = std::max (s.maxDiff, (double) std::fabs (v - x[i - 1]));
    }
    s.dc = sum / (double) n; s.rms = std::sqrt (sq / (double) n);
    const size_t tailN = std::min (n, (size_t) (sr * 0.05));
    double tsq = 0; for (size_t i = n - tailN; i < n; ++i) tsq += (double) x[i] * x[i];
    s.tail = std::sqrt (tsq / (double) tailN);
    return s;
}

inline double toDb (double v) { return 20.0 * std::log10 (std::max (v, 1.0e-12)); }

inline void printStats (const char* label, const Stats& s)
{
    std::printf ("%-6s peak %6.1f dB  rms %6.1f dB  dc %+.5f  maxStep %.3f  tail %6.1f dB%s\n", label,
                 toDb (s.peak), toDb (s.rms), s.dc, s.maxDiff, toDb (s.tail), s.nan ? "  ** NaN/Inf **" : "");
}

// Built-in test material (no files needed). Returns stereo.
inline void makeTestSignal (const std::string& kind, double sr, double seconds, double bpm, std::vector<float>& l, std::vector<float>& r)
{
    const int n = (int) (sr * seconds);
    l.assign ((size_t) n, 0.f); r.assign ((size_t) n, 0.f);
    atm::Noise nz; nz.seed (99);
    const double beat = sr * 60.0 / bpm;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        float a = 0, b = 0;
        if (kind == "sine") a = b = 0.5f * (float) std::sin (atm::kTau * 220.0 * t);
        else if (kind == "saw") { const double ph = std::fmod (110.0 * t, 1.0); a = b = (float) (0.4 * (2.0 * ph - 1.0)); }
        else if (kind == "noise") { a = 0.3f * nz.next(); b = 0.3f * nz.next(); }
        else if (kind == "impulse") { a = b = (i % (int) (sr * 0.5) == 0) ? 0.9f : 0.f; }
        else if (kind == "sweep") { const double f = 30.0 * std::pow (600.0, t / seconds); static double ph = 0; ph += f / sr; a = b = 0.5f * (float) std::sin (atm::kTau * ph); }
        else if (kind == "chord")
        {
            for (double f : { 130.81, 155.56, 196.0, 233.08 }) { const double ph = std::fmod (f * t, 1.0); a += (float) (0.15 * (2.0 * ph - 1.0)); }
            b = a;
        }
        else // "drums": kick on beats, snare on 2 and 4, hats on 8ths: crude but rhythmic and broadband
        {
            const double pb = std::fmod ((double) i, beat) / sr;                    // seconds since the beat
            const int beatIdx = (int) (i / beat);
            a += (float) (0.8 * std::sin (atm::kTau * (45.0 * pb + 30.0 * (1.0 - std::exp (-pb * 40.0)) / 40.0 * 8.0)) * std::exp (-pb * 9.0));
            if (beatIdx % 2 == 1) a += 0.35f * nz.next() * (float) std::exp (-pb * 22.0);
            const double p8 = std::fmod ((double) i, beat * 0.5) / sr;
            a += 0.12f * nz.next() * (float) std::exp (-p8 * 90.0);
            b = a;
        }
        l[(size_t) i] = a; r[(size_t) i] = b;
    }
}

inline atm::SampleData* loadSample (const std::string& path)
{
    std::vector<std::vector<float>> ch; int sr = 0;
    if (! atm::readWav (path, ch, sr) || ch.empty()) return nullptr;
    const float* ptrs[2] = { ch[0].data(), ch.size() > 1 ? ch[1].data() : ch[0].data() };
    return atm::makeSampleData (ptrs, (int) std::min<size_t> (2, ch.size()), (int) ch[0].size(), sr, path.c_str());
}

// A synthetic "break" as sample material when no file is given: 2 bars of drums at 170 bpm.
inline atm::SampleData* makeTestSample (double sr = 44100.0)
{
    std::vector<float> l, r;
    makeTestSignal ("drums", sr, 60.0 / 170.0 * 8.0, 170.0, l, r);
    const float* ptrs[2] = { l.data(), r.data() };
    return atm::makeSampleData (ptrs, 2, (int) l.size(), sr, "testbreak");
}

struct Timer
{
    std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    double seconds() const { return std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count(); }
};

} // namespace tool
