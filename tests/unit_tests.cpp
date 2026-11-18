// Verdant unit tests — tiny self-contained framework (no external dependencies).
#include <cstdio>
#include <functional>
#include <limits>
#include <string>
#include <vector>

#include "TestSignals.h"
#include "dsp/Arp.h"
#include "dsp/Chorus.h"
#include "dsp/Engine.h"
#include "dsp/GrainCloud.h"
#include "dsp/Modulation.h"
#include "dsp/Output.h"
#include "dsp/Params.h"
#include "dsp/PitchTracker.h"
#include "dsp/Presets.h"
#include "dsp/Reverb.h"
#include "dsp/Tape.h"
#include "dsp/Util.h"

using namespace verdant;

namespace
{
int gChecks = 0, gFailures = 0;
std::string gCurrent;

void check (bool cond, const char* expr, const char* file, int line)
{
    ++gChecks;
    if (! cond)
    {
        ++gFailures;
        std::printf ("  FAILED [%s] %s  (%s:%d)\n", gCurrent.c_str(), expr, file, line);
    }
}

#define CHECK(x) check ((x), #x, __FILE__, __LINE__)
#define CHECK_NEAR(a, b, tol) check (std::abs (double (a) - double (b)) <= double (tol), #a " ~= " #b, __FILE__, __LINE__)

struct Test { const char* name; std::function<void()> fn; };
std::vector<Test>& tests() { static std::vector<Test> t; return t; }
struct Reg { Reg (const char* n, std::function<void()> f) { tests().push_back ({ n, std::move (f) }); } };
#define TEST(name) static void name(); static Reg reg_##name (#name, name); static void name()

//==============================================================================
TEST (params_table_is_consistent)
{
    for (int i = 0; i < kNumParams; ++i)
    {
        const auto& p = kParams[i];
        CHECK (p.min < p.max);
        CHECK (p.def >= p.min && p.def <= p.max);
        CHECK (findParam (p.id) == i);
        // normalisation round trip
        for (float n : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            const float v = fromNormalised (p, n);
            CHECK (v >= p.min - 1.0e-4f && v <= p.max + 1.0e-4f);
            if (p.type == PType::Float)
                CHECK_NEAR (toNormalised (p, v), n, 1.0e-3);
        }
        char buf[64];
        CHECK (formatValue (p, p.def, buf, sizeof (buf)) > 0);
    }
}

TEST (presets_are_valid)
{
    const auto& ps = factoryPresets();
    CHECK (! ps.empty());
    for (size_t a = 0; a < ps.size(); ++a)
    {
        for (const auto& v : ps[a].values)
        {
            CHECK (v.pid >= 0 && v.pid < kNumParams);
            CHECK (v.value >= kParams[v.pid].min && v.value <= kParams[v.pid].max);
            CHECK (v.pid != pBypass);
        }
        for (size_t b = a + 1; b < ps.size(); ++b)
            CHECK (std::string (ps[a].name) != ps[b].name);
    }
}

TEST (limiter_never_exceeds_ceiling)
{
    for (float sr : { 44100.0f, 96000.0f, 192000.0f })
    {
        Limiter lim;
        lim.prepare (sr);
        Rng rng;
        float worst = 0.0f;
        for (int i = 0; i < int (sr) * 2; ++i)
        {
            // nasty: noise with random huge spikes and sudden level changes
            float l = rng.bi() * (i % 5000 < 2500 ? 0.2f : 3.0f);
            float r = rng.bi() * 0.5f;
            if (rng.chance (0.001f)) l = 50.0f * rng.bi();
            lim.process (l, r);
            worst = std::max (worst, std::max (std::abs (l), std::abs (r)));
        }
        CHECK (worst <= 0.9661f);
    }
}

TEST (limiter_is_transparent_below_ceiling)
{
    Limiter lim;
    lim.prepare (48000.0f);
    const int D = lim.latency();
    std::vector<float> in (4800);
    for (size_t i = 0; i < in.size(); ++i)
        in[i] = 0.5f * std::sin (0.05f * float (i));
    for (size_t i = 0; i < in.size(); ++i)
    {
        float l = in[i], r = in[i];
        lim.process (l, r);
        if (int (i) >= D)
            CHECK_NEAR (l, in[i - size_t (D)], 1.0e-6);
    }
}

TEST (engine_reports_latency_and_mix_zero_is_a_pure_delay)
{
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        Engine e;
        e.prepare (sr, 512);
        ParamValues p;
        p[pMix] = 0.0f;
        e.setParams (p);
        auto s = vtest::phrase (sr, 2.0);
        const auto ref = s;
        Transport tr;
        e.process (s.l.data(), s.r.data(), int (s.size()), tr);
        const int lat = e.latencySamples();
        CHECK (lat > 0);
        CHECK (lat == Engine::latencyForSampleRate (sr));
        double err = 0.0;
        for (size_t i = size_t (lat); i < s.size(); ++i)
            err = std::max (err, double (std::abs (s.l[i] - ref.l[i - size_t (lat)])));
        CHECK (err < 1.0e-5);
    }
}

TEST (engine_bypass_is_a_pure_delay)
{
    Engine e;
    e.prepare (48000.0, 512);
    ParamValues p;
    p[pBypass] = 1.0f;
    p[pMix] = 1.0f;
    e.setParams (p);
    auto s = vtest::phrase (48000.0, 2.0);
    const auto ref = s;
    Transport tr;
    e.process (s.l.data(), s.r.data(), int (s.size()), tr);
    const int lat = e.latencySamples();
    double err = 0.0;
    for (size_t i = size_t (lat); i < s.size(); ++i)
        err = std::max (err, double (std::abs (s.l[i] - ref.l[i - size_t (lat)])));
    CHECK (err < 1.0e-6);
}

TEST (engine_is_block_size_invariant)
{
    // The control grid is internal, so the output must not depend on host block size.
    auto render = [] (int block)
    {
        Engine e;
        e.prepare (48000.0, 4096);
        ParamValues p = factoryPresets().front().resolve();
        e.setParams (p);
        auto s = vtest::phrase (48000.0, 3.0);
        Transport tr;
        tr.playing = true;
        size_t pos = 0;
        while (pos < s.size())
        {
            const int n = int (std::min (size_t (block), s.size() - pos));
            tr.ppq = double (pos) / 48000.0 * 2.0;
            e.process (s.l.data() + pos, s.r.data() + pos, n, tr);
            pos += size_t (n);
        }
        return s;
    };
    const auto a = render (32), b = render (1), c = render (1000);
    double errB = 0.0, errC = 0.0;
    for (size_t i = 0; i < a.size(); ++i)
    {
        errB = std::max (errB, double (std::abs (a.l[i] - b.l[i])));
        errC = std::max (errC, double (std::abs (a.l[i] - c.l[i])));
    }
    CHECK (errB < 1.0e-4);
    CHECK (errC < 1.0e-4);
}


//==============================================================================
// Helpers

// Strongest sinusoidal component between fmin and fmax (brute-force DFT on a log grid + refinement).
double dominantHz (const float* x, int n, double sr, double fmin, double fmax)
{
    auto power = [&] (double f)
    {
        double re = 0.0, im = 0.0;
        const double w = 2.0 * M_PI * f / sr;
        for (int i = 0; i < n; ++i)
        {
            const double win = 0.5 - 0.5 * std::cos (2.0 * M_PI * double (i) / double (n - 1));
            re += win * x[i] * std::cos (w * i);
            im -= win * x[i] * std::sin (w * i);
        }
        return re * re + im * im;
    };
    double best = fmin, bestP = -1.0;
    const int steps = 400;
    for (int k = 0; k <= steps; ++k)
    {
        const double f = fmin * std::pow (fmax / fmin, double (k) / steps);
        const double p = power (f);
        if (p > bestP) { bestP = p; best = f; }
    }
    double span = best * (std::pow (fmax / fmin, 1.0 / steps) - 1.0);
    for (int it = 0; it < 24; ++it)
    {
        const double lo = power (best - span), hi = power (best + span);
        if (lo > bestP && lo >= hi) { best -= span; bestP = lo; }
        else if (hi > bestP)        { best += span; bestP = hi; }
        span *= 0.6;
    }
    return best;
}

// Power-weighted mean frequency in [flo, fhi] (1 Hz grid). Robust for granular pitch
// shifters, whose energy sits in a cluster of lines around the target rather than on one line.
double centroidHz (const float* x, int n, double sr, double flo, double fhi)
{
    double num = 0.0, den = 0.0;
    for (double f = flo; f <= fhi; f += 1.0)
    {
        double re = 0.0, im = 0.0;
        const double w = 2.0 * M_PI * f / sr;
        for (int i = 0; i < n; ++i)
        {
            const double win = 0.5 - 0.5 * std::cos (2.0 * M_PI * double (i) / double (n - 1));
            re += win * x[i] * std::cos (w * i);
            im -= win * x[i] * std::sin (w * i);
        }
        const double p = re * re + im * im;
        num += f * p; den += p;
    }
    return den > 0.0 ? num / den : 0.0;
}

double cents (double f, double ref) { return 1200.0 * std::log2 (f / ref); }

double rmsOf (const float* x, int n)
{
    double s = 0.0;
    for (int i = 0; i < n; ++i) s += double (x[i]) * x[i];
    return std::sqrt (s / std::max (1, n));
}

// Runs fn(l, r, n) over a signal in 32-sample control blocks, calling ctl() before each.
template <typename Ctl, typename Proc>
void runBlocks (int total, Ctl&& ctl, Proc&& proc)
{
    for (int pos = 0; pos < total; pos += 32)
    {
        ctl (pos);
        proc (pos, std::min (32, total - pos));
    }
}

//==============================================================================
TEST (ring_interpolation_is_exact_on_samples_and_ramps)
{
    Ring r;
    r.prepare (256);
    for (int i = 0; i < 200; ++i) r.push (float (i) * 0.5f);     // a ramp
    CHECK_NEAR (r.tap (0), 99.5, 1.0e-6);
    CHECK_NEAR (r.tap (10), 94.5, 1.0e-6);
    CHECK_NEAR (r.tapHermite (10.0f), 94.5, 1.0e-5);
    CHECK_NEAR (r.tapHermite (10.5f), 94.25, 1.0e-4);            // cubic reproduces a ramp exactly
    CHECK_NEAR (r.tapLinear (10.25f), 94.375, 1.0e-4);
    CHECK_NEAR (r.readHermite (150.25), 75.125, 1.0e-4);
}

TEST (filters_have_the_designed_response)
{
    const float sr = 48000.0f;
    auto gainAt = [sr] (auto&& process, float hz)
    {
        double in = 0.0, out = 0.0;
        for (int i = 0; i < 48000; ++i)
        {
            const float x = std::sin (kTwoPi * hz * float (i) / sr);
            const float y = process (x);
            if (i > 24000) { in += double (x) * x; out += double (y) * y; }
        }
        return 10.0 * std::log10 (out / in);
    };

    Shelf1 lo; lo.setLow (200.0f, 6.0f, sr);
    CHECK_NEAR (gainAt ([&] (float x) { return lo.process (x); }, 20.0f), 6.0, 0.3);
    lo.reset();
    CHECK_NEAR (gainAt ([&] (float x) { return lo.process (x); }, 8000.0f), 0.0, 0.2);

    Shelf1 hi; hi.setHigh (2000.0f, -6.0f, sr);
    CHECK_NEAR (gainAt ([&] (float x) { return hi.process (x); }, 50.0f), 0.0, 0.2);
    hi.reset();
    CHECK_NEAR (gainAt ([&] (float x) { return hi.process (x); }, 16000.0f), -6.0, 0.4);

    Svf svf; svf.set (1000.0f, 0.7071f, sr);
    CHECK_NEAR (gainAt ([&] (float x) { return svf.low (x); }, 1000.0f), -3.0, 0.2);
    svf.reset();
    CHECK_NEAR (gainAt ([&] (float x) { return svf.low (x); }, 100.0f), 0.0, 0.1);
    svf.reset();
    CHECK_NEAR (gainAt ([&] (float x) { return svf.high (x); }, 100.0f), -40.0, 1.0);
}

TEST (adaa_tanh_tracks_tanh_and_stays_bounded)
{
    AdaaTanh a;
    a.reset();
    double err = 0.0;
    float worst = 0.0f;
    for (int i = 0; i < 48000; ++i)
    {
        // slow signal: ADAA must equal the memoryless curve (averaged over one sample)
        const float x = 3.0f * std::sin (kTwoPi * 50.0f * float (i) / 48000.0f);
        const float y = a.process (x);
        if (i > 10) err = std::max (err, double (std::abs (y - std::tanh (x))));
        worst = std::max (worst, std::abs (y));
    }
    CHECK (err < 0.02);
    CHECK (worst <= 1.001f);

    // hostile: full-scale square at a high frequency must stay bounded
    a.reset();
    worst = 0.0f;
    for (int i = 0; i < 48000; ++i)
        worst = std::max (worst, std::abs (a.process ((i / 3) % 2 ? 40.0f : -40.0f)));
    CHECK (worst < 41.0f);
}

TEST (drift_lfo_is_bounded_smooth_and_wanders)
{
    DriftLfo d;
    d.prepare (1500.0f, 1234u, 0.1f);
    float lo = 1.0f, hi = -1.0f, maxStep = 0.0f, prev = d.tick (1.0f);
    for (int i = 0; i < 1500 * 120; ++i)
    {
        const float v = d.tick (1.0f);
        lo = std::min (lo, v); hi = std::max (hi, v);
        maxStep = std::max (maxStep, std::abs (v - prev));
        prev = v;
    }
    CHECK (lo >= -1.0f && hi <= 1.0f);
    CHECK (hi - lo > 0.8f);          // it actually moves
    CHECK (maxStep < 0.01f);         // and never jumps
}

//==============================================================================
TEST (tape_neutral_is_a_pure_delay)
{
    for (float sr : { 44100.0f, 48000.0f, 96000.0f })
    {
        Tape t;
        t.prepare (sr);
        CHECK (t.nominalDelay() == 4 + int (std::lround (0.003f * sr)));

        Tape::Controls c;
        c.wow = c.flutter = c.drive = c.age = 0.0f;
        const int N = int (sr);
        std::vector<float> in (static_cast<size_t> (N)), oL (static_cast<size_t> (N)), oR (static_cast<size_t> (N));
        for (int i = 0; i < N; ++i) in[size_t (i)] = 0.05f * std::sin (kTwoPi * 440.0f * float (i) / sr);
        runBlocks (N, [&] (int pos) { c.beat = double (pos) / double (sr) * 2.0; t.control (c); },
                   [&] (int pos, int n) { t.process (in.data() + pos, in.data() + pos, oL.data() + pos, oR.data() + pos, n); });
        const int D = t.nominalDelay();
        double err = 0.0;
        for (int i = D; i < N; ++i)     // from the very first sample: no fade-in after a reset
            err = std::max (err, double (std::abs (oL[size_t (i)] - in[size_t (i - D)])));
        // only the phase of the 21 kHz tone filter and the 8 Hz DC blocker differs
        CHECK (err < 0.05 * 0.08);
        CHECK_NEAR (20.0 * std::log10 (rmsOf (oL.data() + N / 2, N / 2) / rmsOf (in.data() + N / 2, N / 2)), 0.0, 0.1);

        // module off: bit-exact delay
        Tape t2;
        t2.prepare (sr);
        c.on = false;
        runBlocks (N, [&] (int) { t2.control (c); },
                   [&] (int pos, int n) { t2.process (in.data() + pos, in.data() + pos, oL.data() + pos, oR.data() + pos, n); });
        err = 0.0;
        for (int i = D; i < N; ++i)
            err = std::max (err, double (std::abs (oL[size_t (i)] - in[size_t (i - D)])));
        CHECK (err < 1.0e-7);
    }
}

TEST (tape_speed_up_is_band_limited)
{
    // +12 st turns 15 kHz into 30 kHz, which would alias to 18 kHz at 48 kHz.
    const float sr = 48000.0f;
    const int N = int (sr) * 2;
    auto outRms = [&] (float hz)
    {
        Tape t;
        t.prepare (sr);
        Tape::Controls c;
        c.wow = c.flutter = c.drive = c.age = 0.0f; c.speedSemis = 12.0f;
        std::vector<float> in (static_cast<size_t> (N)), oL (static_cast<size_t> (N)), oR (static_cast<size_t> (N));
        for (int i = 0; i < N; ++i) in[size_t (i)] = 0.3f * std::sin (kTwoPi * hz * float (i) / sr);
        runBlocks (N, [&] (int pos) { c.beat = double (pos) / 24000.0; t.control (c); },
                   [&] (int pos, int n) { t.process (in.data() + pos, in.data() + pos, oL.data() + pos, oR.data() + pos, n); });
        return rmsOf (oL.data() + N / 2, N / 2);
    };
    const double ok = outRms (5000.0f), aliasing = outRms (15000.0f);
    CHECK (ok > 0.1);
    CHECK (20.0 * std::log10 (aliasing / ok) < -20.0);
}

TEST (tape_speed_transposes_and_stretch_stays_bounded)
{
    const float sr = 48000.0f;
    const int N = int (sr) * 3;
    std::vector<float> in (static_cast<size_t> (N)), oL (static_cast<size_t> (N)), oR (static_cast<size_t> (N));
    for (int i = 0; i < N; ++i) in[size_t (i)] = 0.3f * std::sin (kTwoPi * 330.0f * float (i) / sr);

    for (float semis : { -12.0f, -5.0f, 7.0f, 12.0f })
    {
        Tape t;
        t.prepare (sr);
        Tape::Controls c;
        c.wow = c.flutter = 0.0f;
        c.speedSemis = semis;
        runBlocks (N, [&] (int pos) { c.beat = double (pos) / double (sr) * 2.0; t.control (c); },
                   [&] (int pos, int n) { t.process (in.data() + pos, in.data() + pos, oL.data() + pos, oR.data() + pos, n); });
        // thanks to the waveform-similarity alignment a pure tone comes out as a pure tone
        const double target = 330.0 * std::pow (2.0, semis / 12.0);
        const double f = dominantHz (oL.data() + N / 2, 16384, sr, 100.0, 1500.0);
        CHECK (std::abs (cents (f, target)) < 10.0);
        CHECK_NEAR (20.0 * std::log10 (rmsOf (oL.data() + N / 2, N / 2) / rmsOf (in.data() + N / 2, N / 2)), 0.0, 2.5);
    }

    Tape t;
    t.prepare (sr);
    Tape::Controls c;
    c.stretch = 8.0f; c.stutter = 1.0f; c.wow = c.flutter = c.drive = c.age = 1.0f;
    float peak = 0.0f;
    runBlocks (N, [&] (int pos) { c.beat = double (pos) / double (sr) * 2.0; t.control (c); },
               [&] (int pos, int n)
               {
                   t.process (in.data() + pos, in.data() + pos, oL.data() + pos, oR.data() + pos, n);
                   for (int i = 0; i < n; ++i) peak = std::max (peak, std::abs (oL[size_t (pos + i)]));
               });
    CHECK (peak < 1.5f && peak > 0.01f);
    CHECK (t.stutterEvents > 0);
}

//==============================================================================
TEST (grain_cloud_is_silent_for_silence_and_loudness_is_normalised)
{
    const float sr = 48000.0f;
    const int N = int (sr) * 4;
    std::vector<float> in (static_cast<size_t> (N), 0.0f), oL (static_cast<size_t> (N)), oR (static_cast<size_t> (N));

    {
        GrainCloud g;
        g.prepare (sr);
        GrainCloud::Controls c;
        c.density = 40.0f; c.feedback = 1.0f;
        runBlocks (N, [&] (int) { g.control (c); },
                   [&] (int pos, int n) { g.process (in.data() + pos, in.data() + pos, oL.data() + pos, oR.data() + pos, n); });
        CHECK (rmsOf (oL.data(), N) == 0.0);
        CHECK (g.spawnCount > 100);
    }

    Rng rng;
    for (auto& v : in) v = 0.2f * rng.bi();
    const double inRms = rmsOf (in.data(), N);

    // from sparse droplets to the thickest foliage the level must stay in a musical window
    const float settings[][3] = { { 1.0f, 60.0f, 0.0f }, { 6.0f, 80.0f, 0.3f }, { 14.0f, 160.0f, 0.65f },
                                  { 40.0f, 300.0f, 1.0f }, { 80.0f, 800.0f, 1.0f }, { 80.0f, 20.0f, 0.0f } };
    for (const auto& st : settings)
    {
        GrainCloud g;
        g.prepare (sr);
        GrainCloud::Controls c;
        c.density = st[0]; c.sizeMs = st[1]; c.shape = st[2]; c.spread = 0.0f; c.feedback = 0.5f;
        runBlocks (N, [&] (int) { g.control (c); },
                   [&] (int pos, int n) { g.process (in.data() + pos, in.data() + pos, oL.data() + pos, oR.data() + pos, n); });
        const double rel = 20.0 * std::log10 (rmsOf (oL.data() + N / 2, N / 2) / inRms);
        CHECK (rel > -22.0 && rel < 5.0);
        float peak = 0.0f;
        for (auto v : oL) peak = std::max (peak, std::abs (v));
        CHECK (peak < 2.5f);
    }
}

TEST (grain_upward_transposition_is_band_limited)
{
    // +24 semitones turns 9 kHz into 36 kHz, which cannot exist at 48 kHz: it must be filtered
    // out before the grain is read instead of folding back down as an inharmonic alias.
    const float sr = 48000.0f;
    const int N = int (sr) * 3;
    auto cloudRms = [&] (float hz)
    {
        GrainCloud g;
        g.prepare (sr);
        GrainCloud::Controls c;
        c.pitchSemis = 24.0f; c.spread = 0.0f; c.reverse = 0.0f; c.density = 20.0f; c.sizeMs = 200.0f; c.shape = 1.0f;
        std::vector<float> in (static_cast<size_t> (N)), oL (static_cast<size_t> (N)), oR (static_cast<size_t> (N));
        for (int i = 0; i < N; ++i) in[size_t (i)] = 0.3f * std::sin (kTwoPi * hz * float (i) / sr);
        runBlocks (N, [&] (int) { g.control (c); },
                   [&] (int pos, int n) { g.process (in.data() + pos, in.data() + pos, oL.data() + pos, oR.data() + pos, n); });
        return rmsOf (oL.data() + N / 2, N / 2);
    };
    const double ok = cloudRms (3000.0f), aliasing = cloudRms (9000.0f);
    CHECK (ok > 0.05);                                   // 3 kHz -> 12 kHz comes through
    CHECK (20.0 * std::log10 (aliasing / ok) < -18.0);   // 9 kHz -> would-be alias is suppressed
}

//==============================================================================
TEST (pitch_tracker_is_accurate_and_ignores_noise)
{
    for (float sr : { 44100.0f, 48000.0f, 96000.0f })
    {
        for (float hz : { 82.4f, 110.0f, 196.0f, 261.6f, 440.0f, 659.3f, 987.8f })
        {
            PitchTracker pt;
            pt.prepare (sr);
            double ph = 0.0;
            for (int i = 0; i < int (sr); ++i)
            {
                // harmonically rich tone (sawtooth-ish), the hard case for octave errors
                float x = 0.0f;
                for (int h = 1; h <= 6; ++h) x += std::sin (float (2.0 * M_PI * ph * h)) / float (h);
                ph += hz / sr; if (ph >= 1.0) ph -= 1.0;
                pt.push (0.2f * x);
            }
            CHECK (pt.currentHz() > 0.0f);
            CHECK (std::abs (cents (pt.currentHz(), hz)) < 10.0);
        }

        PitchTracker pt;
        pt.prepare (sr);
        Rng rng;
        int reported = 0;
        for (int i = 0; i < int (sr) * 2; ++i)
        {
            pt.push (0.2f * rng.bi());
            if (i % 1000 == 0 && pt.currentHz() > 0.0f) ++reported;
        }
        CHECK (reported < 10);       // white noise: (almost) never a confident pitch
    }
}

// Renders the arp alone on a steady tone and returns the pitch of each of the first `steps` notes.
std::vector<double> arpNotePitches (Arp::Controls c, float inputHz, int steps)
{
    const float sr = 48000.0f;
    Arp arp;
    arp.prepare (sr);
    c.rate = 3;                 // 1/8 at 120 bpm = 12000 samples
    c.gate = 0.9f; c.sparkle = 0.0f; c.hold = 1.0f;
    c.samplesPerBeat = 24000.0;
    const int warm = 48000;     // one second so the capture buffer and the tracker have material
    const int N = warm + steps * 12000 + 2000;
    std::vector<float> in (static_cast<size_t> (N)), oL (static_cast<size_t> (N)), oR (static_cast<size_t> (N));
    for (int i = 0; i < N; ++i) in[size_t (i)] = 0.3f * std::sin (kTwoPi * inputHz * float (i) / sr);
    runBlocks (N, [&] (int pos) { c.beat = double (pos) / 24000.0; arp.control (c); },
               [&] (int pos, int n) { arp.process (in.data() + pos, in.data() + pos, oL.data() + pos, oR.data() + pos, n); });
    std::vector<double> out;
    for (int k = 0; k < steps; ++k)
    {
        const int start = warm + k * 12000 + 1500;     // inside the note, after its attack
        std::vector<float> mono (6000);
        for (int i = 0; i < 6000; ++i) mono[size_t (i)] = oL[size_t (start + i)] + oR[size_t (start + i)];
        out.push_back (dominantHz (mono.data(), 6000, sr, 60.0, 4000.0));
    }
    return out;
}

TEST (arp_follows_scale_intervals_without_tracking)
{
    Arp::Controls c;
    c.track = false; c.pattern = 0; c.range = 2; c.scale = 13;     // Fifths, up
    const auto p = arpNotePitches (c, 220.0f, 5);
    // the pattern position comes from the beat clock; step 4 (beat 2.0) is ladder index 4 % 5 = 4, ...
    const double expect[5] = { 4, 0, 1, 2, 3 };
    const double semis[5] = { 0, 7, 12, 19, 24 };
    for (int k = 0; k < 5; ++k)
        CHECK (std::abs (cents (p[size_t (k)], 220.0 * std::pow (2.0, semis[int (expect[k])] / 12.0))) < 15.0);
}

TEST (arp_lands_on_key_and_scale_when_tracking)
{
    // Input: an A that is 40 cents sharp. Key C, major pentatonic (C D E G A).
    // Every arp note must be an equal-tempered pentatonic tone, i.e. the detune is corrected.
    Arp::Controls c;
    c.track = true; c.pattern = 0; c.range = 1; c.scale = 5; c.key = 0;
    const float inHz = 440.0f * std::pow (2.0f, 40.0f / 1200.0f);
    const auto p = arpNotePitches (c, inHz, 8);
    const int pent[5] = { 0, 2, 4, 7, 9 };
    for (double f : p)
    {
        const double midi = 69.0 + 12.0 * std::log2 (f / 440.0);
        const double nearest = std::round (midi);
        CHECK (std::abs (midi - nearest) < 0.15);                  // in tune
        bool inScale = false;
        for (int d : pent) inScale = inScale || ((int (nearest) % 12) == d);
        CHECK (inScale);
    }
    // and the pattern really moves (not one repeated note)
    double lo = 1.0e9, hi = 0.0;
    for (double f : p) { lo = std::min (lo, f); hi = std::max (hi, f); }
    CHECK (hi / lo > 1.4);
}

TEST (arp_lingers_then_fades_after_input_stops)
{
    const float sr = 48000.0f;
    for (float hold : { 0.0f, 0.5f, 1.0f })
    {
        Arp arp;
        arp.prepare (sr);
        Arp::Controls c;
        c.pattern = 0; c.rate = 5; c.hold = hold; c.track = false;
        const int N = int (sr) * 12;
        std::vector<float> in (static_cast<size_t> (N), 0.0f), oL (static_cast<size_t> (N)), oR (static_cast<size_t> (N));
        for (int i = 0; i < int (sr); ++i) in[size_t (i)] = 0.3f * std::sin (kTwoPi * 330.0f * float (i) / sr);
        runBlocks (N, [&] (int pos) { c.beat = double (pos) / 24000.0; arp.control (c); },
                   [&] (int pos, int n) { arp.process (in.data() + pos, in.data() + pos, oL.data() + pos, oR.data() + pos, n); });
        const double early = rmsOf (oL.data() + int (sr) * 2, int (sr));
        const double late = rmsOf (oL.data() + int (sr) * 10, int (sr));
        if (hold == 0.0f)      CHECK (early < 1.0e-3);                 // stops with the input
        else if (hold == 0.5f) { CHECK (early > 1.0e-3); CHECK (late < early * 0.05); }
        else                   { CHECK (late > early * 0.5); }         // linger = 1: keeps going
    }
}

//==============================================================================
TEST (chorus_is_transparent_at_zero_mix_and_keeps_the_lows)
{
    const float sr = 48000.0f;
    const int N = int (sr);
    std::vector<float> l (static_cast<size_t> (N)), r (static_cast<size_t> (N)), ref (static_cast<size_t> (N));
    Rng rng;
    for (int i = 0; i < N; ++i) ref[size_t (i)] = 0.3f * rng.bi();

    {
        Chorus ch; ch.prepare (sr);
        Chorus::Controls c; c.mix = 0.0f;
        l = ref; r = ref;
        runBlocks (N, [&] (int) { ch.control (c); }, [&] (int pos, int n) { ch.process (l.data() + pos, r.data() + pos, n); });
        double err = 0.0;
        for (int i = 0; i < N; ++i) err = std::max (err, double (std::abs (l[size_t (i)] - ref[size_t (i)])));
        CHECK (err < 1.0e-6);
    }
    for (float glass : { 0.0f, 1.0f })
    {
        // a 55 Hz tone is far below the 220 Hz crossover: it must come out essentially untouched
        Chorus ch; ch.prepare (sr);
        Chorus::Controls c; c.mix = 1.0f; c.voices = 24; c.glass = glass; c.depth = 1.0f;
        for (int i = 0; i < N; ++i) ref[size_t (i)] = 0.3f * std::sin (kTwoPi * 55.0f * float (i) / sr);
        l = ref; r = ref;
        runBlocks (N, [&] (int) { ch.control (c); }, [&] (int pos, int n) { ch.process (l.data() + pos, r.data() + pos, n); });
        const double rel = 20.0 * std::log10 (rmsOf (l.data() + N / 2, N / 2) / rmsOf (ref.data() + N / 2, N / 2));
        CHECK (std::abs (rel) < 1.0);
    }
}

TEST (chorus_voice_count_and_mode_changes_do_not_click)
{
    const float sr = 48000.0f;
    const int N = int (sr) * 4;
    std::vector<float> l (static_cast<size_t> (N)), r (static_cast<size_t> (N));
    for (int i = 0; i < N; ++i) l[size_t (i)] = r[size_t (i)] = 0.3f * std::sin (kTwoPi * 400.0f * float (i) / sr);
    Chorus ch; ch.prepare (sr);
    Chorus::Controls c; c.mix = 1.0f;
    runBlocks (N, [&] (int pos)
               {
                   const int k = pos / 6000;                 // change something 8 times a second
                   c.voices = (k % 2) ? 24 : 2;
                   c.glass = (k % 3) ? 1.0f : 0.0f;
                   c.on = (k % 5) != 0;
                   ch.control (c);
               },
               [&] (int pos, int n) { ch.process (l.data() + pos, r.data() + pos, n); });
    // second difference of a 400 Hz tone at 0.3 is about 8e-4; a click would be orders larger
    float worst = 0.0f;
    for (int i = 2; i < N; ++i)
        worst = std::max (worst, std::abs (l[size_t (i)] - 2.0f * l[size_t (i - 1)] + l[size_t (i - 2)]));
    CHECK (worst < 0.02f);
}

//==============================================================================
TEST (delay_shifter_transposes)
{
    const float sr = 48000.0f;
    for (float ratio : { 0.5f, 1.4983f, 2.0f, 4.0f })
    {
        DelayShifter s;
        s.prepare (sr, 83.0f, 4.0f);
        s.reset();
        s.setRatio (ratio);
        s.snap();
        std::vector<float> out (48000);
        float peak = 0.0f;
        for (int i = 0; i < 48000; ++i)
        {
            out[size_t (i)] = s.process (0.3f * std::sin (kTwoPi * 220.0f * float (i) / sr));
            peak = std::max (peak, std::abs (out[size_t (i)]));
        }
        // a free-running granular shifter puts its energy in a cluster of lines around the
        // target (that roughness is smeared by the reverb), so measure the cluster's centre
        const double f = centroidHz (out.data() + 16000, 16384, sr, 220.0 * ratio * 0.7, 220.0 * ratio * 1.4);
        CHECK (std::abs (cents (f, 220.0 * ratio)) < 50.0);
        CHECK (peak <= 0.31f);        // two windows summing to one: never any gain
    }
}

struct ReverbRun { std::vector<float> l, r; };
ReverbRun runReverb (Reverb::Controls c, float sr, double seconds, double inputSeconds, bool noise, double freezeAt = -1.0)
{
    Reverb rv;
    rv.prepare (sr);
    const int N = int (seconds * sr);
    ReverbRun out;
    out.l.assign (size_t (N), 0.0f); out.r.assign (size_t (N), 0.0f);
    std::vector<float> in (static_cast<size_t> (N), 0.0f);
    Rng rng;
    if (noise) for (int i = 0; i < int (inputSeconds * sr); ++i) in[size_t (i)] = 0.25f * rng.bi();
    else in[100] = 1.0f;
    runBlocks (N, [&] (int pos) { if (freezeAt >= 0.0) c.freeze = pos >= int (freezeAt * sr); rv.control (c); },
               [&] (int pos, int n) { rv.process (in.data() + pos, in.data() + pos, out.l.data() + pos, out.r.data() + pos, n); });
    return out;
}

TEST (reverb_decay_time_matches_the_control)
{
    for (float sr : { 44100.0f, 96000.0f })
        for (float t60 : { 1.0f, 3.0f, 8.0f })
        {
            Reverb::Controls c;
            c.decaySec = t60; c.shimmer = 0.0f; c.dampHz = 20000.0f; c.lowCutHz = 20.0f; c.mod = 0.3f;
            const auto run = runReverb (c, sr, double (t60) * 1.2 + 1.0, 0.3, true);
            // level drop between two windows after the input ends -> T60
            const int w = int (0.2f * sr);
            const double t1 = 0.6, t2 = 0.6 + double (t60) * 0.7;
            const double d1 = 20.0 * std::log10 (rmsOf (run.l.data() + int (t1 * sr), w));
            const double d2 = 20.0 * std::log10 (rmsOf (run.l.data() + int (t2 * sr), w));
            const double measured = 60.0 * (t2 - t1) / (d1 - d2);
            CHECK (measured > t60 * 0.7 && measured < t60 * 1.3);
        }
}

TEST (reverb_infinite_and_freeze_hold_without_growing)
{
    const float sr = 48000.0f;
    const int w = int (sr);

    // infinite decay, no shimmer: the tail must neither die nor grow
    Reverb::Controls c;
    c.infinite = true; c.shimmer = 0.0f;
    auto run = runReverb (c, sr, 40.0, 1.0, true);
    double a = rmsOf (run.l.data() + 3 * w, w), b = rmsOf (run.l.data() + 38 * w, w);
    CHECK (b > a * 0.25);      // high damping still shapes it, but it sustains
    CHECK (b < a * 1.05);

    // freeze engaged at 1.5 s on a normal 2 s tail: holds for half a minute
    Reverb::Controls f;
    f.decaySec = 2.0f; f.shimmer = 0.5f;
    run = runReverb (f, sr, 32.0, 1.0, true, 1.5);
    a = rmsOf (run.l.data() + 3 * w, w); b = rmsOf (run.l.data() + 30 * w, w);
    CHECK (b > a * 0.6);
    CHECK (b < a * 1.05);
}

TEST (reverb_cannot_run_away)
{
    const float sr = 48000.0f;
    // worst case: infinite decay, full shimmer, biggest size, continuous loud input for 30 s
    for (int pitch = 0; pitch < 5; ++pitch)
    {
        Reverb::Controls c;
        c.infinite = true; c.shimmer = 1.0f; c.shimmerPitch = pitch; c.size = 1.0f; c.mod = 1.0f;
        c.dampHz = 20000.0f; c.lowCutHz = 20.0f; c.diffusion = 1.0f;
        Reverb rv;
        rv.prepare (sr);
        const int N = int (sr) * 30;
        std::vector<float> in (32), oL (32), oR (32);
        Rng rng;
        float peak = 0.0f;
        double lateSq = 0.0;
        for (int pos = 0; pos < N; pos += 32)
        {
            for (auto& v : in) v = 0.9f * rng.bi();
            rv.control (c);
            rv.process (in.data(), in.data(), oL.data(), oR.data(), 32);
            for (int i = 0; i < 32; ++i)
            {
                peak = std::max (peak, std::max (std::abs (oL[size_t (i)]), std::abs (oR[size_t (i)])));
                if (pos > N - int (sr) * 5) lateSq += double (oL[size_t (i)]) * oL[size_t (i)];
            }
        }
        CHECK (peak < 4.0f);                                         // bounded
        CHECK (std::sqrt (lateSq / (sr * 5.0)) < 0.7);               // and governed well below that
        CHECK (rv.tankEnergy() < 0.5f);
    }
}

//==============================================================================
//==============================================================================
// Regression tests for defects found by an independent code review.

TEST (tape_varispeed_stays_clean_after_the_capture_ring_wraps)
{
    // The alignment search once compared against samples ahead of the write head; it only
    // "worked" until the 10.9 s ring wrapped. Check a pure tone is still pure after 13 s.
    for (float sr : { 44100.0f, 48000.0f })
    {
        Tape t;
        t.prepare (sr);
        Tape::Controls c;
        c.wow = c.flutter = c.drive = c.age = 0.0f;
        c.speedSemis = -12.0f;
        const int N = int (sr) * 14;
        std::vector<float> in (static_cast<size_t> (N)), oL (static_cast<size_t> (N)), oR (static_cast<size_t> (N));
        for (int i = 0; i < N; ++i) in[size_t (i)] = 0.3f * std::sin (kTwoPi * 330.0f * float (i) / sr);
        runBlocks (N, [&] (int pos) { c.beat = double (pos) / double (sr) * 2.0; t.control (c); },
                   [&] (int pos, int n) { t.process (in.data() + pos, in.data() + pos, oL.data() + pos, oR.data() + pos, n); });
        const float* late = oL.data() + N - 20000;
        CHECK (std::abs (cents (dominantHz (late, 16384, sr, 100.0, 400.0), 165.0)) < 10.0);
        // steady level: no beating between two neighbouring lines
        double lo = 1.0e9, hi = 0.0;
        for (int k = 0; k < 8; ++k)
        {
            const double r = rmsOf (late + k * 2000, 2000);
            lo = std::min (lo, r); hi = std::max (hi, r);
        }
        CHECK (hi / lo < 1.15);
    }
}

TEST (reverb_decay_jumps_do_not_click)
{
    const float sr = 48000.0f;
    const int N = int (sr) * 6;
    Reverb rv;
    rv.prepare (sr);
    Reverb::Controls c;
    c.shimmer = 0.0f; c.mod = 0.0f; c.decaySec = 0.3f;
    std::vector<float> in (static_cast<size_t> (N)), oL (static_cast<size_t> (N)), oR (static_cast<size_t> (N));
    for (int i = 0; i < N; ++i) in[size_t (i)] = 0.25f * std::sin (kTwoPi * 100.0f * float (i) / sr);
    runBlocks (N, [&] (int pos)
               {
                   const int k = pos / 24000;      // every half second jump the decay somewhere extreme
                   c.infinite = (k % 4) == 1;
                   c.decaySec = (k % 4) == 2 ? 60.0f : ((k % 4) == 3 ? 2.0f : 0.3f);
                   rv.control (c);
               },
               [&] (int pos, int n) { rv.process (in.data() + pos, in.data() + pos, oL.data() + pos, oR.data() + pos, n); });
    float worst = 0.0f;
    for (int i = 2; i < N; ++i)
        worst = std::max (worst, std::abs (oL[size_t (i)] - 2.0f * oL[size_t (i - 1)] + oL[size_t (i - 2)]));
    CHECK (worst < 2.0e-3f);     // was 6e-2 before the line gains were ramped
}

TEST (reverb_switched_back_on_does_not_replay_old_audio)
{
    const float sr = 48000.0f;
    const int N = int (sr) * 10;
    for (float pre : { 20.0f, 250.0f })
    {
        Reverb rv;
        rv.prepare (sr);
        Reverb::Controls c;
        c.predelayMs = pre; c.decaySec = 1.0f;
        std::vector<float> in (static_cast<size_t> (N), 0.0f), oL (static_cast<size_t> (N)), oR (static_cast<size_t> (N));
        for (int i = 0; i < int (sr) * 6; ++i) in[size_t (i)] = 0.3f * std::sin (kTwoPi * 440.0f * float (i) / sr);
        runBlocks (N, [&] (int pos) { c.on = pos < int (sr) || pos >= int (sr) * 8; rv.control (c); },
                   [&] (int pos, int n) { rv.process (in.data() + pos, in.data() + pos, oL.data() + pos, oR.data() + pos, n); });
        float peak = 0.0f;       // input has been silent since 6 s; re-enabled at 8 s
        for (int i = int (sr) * 8; i < N; ++i) peak = std::max (peak, std::abs (oL[size_t (i)]));
        CHECK (peak < 1.0e-4f);
    }
}

TEST (grain_cloud_is_block_size_invariant_when_saturated)
{
    // More grains requested than the pool holds: slot reuse must still be sample-accurate.
    auto render = [] (int block)
    {
        const float sr = 48000.0f;
        GrainCloud g;
        g.prepare (sr);
        GrainCloud::Controls c;
        c.density = 120.0f; c.sizeMs = 900.0f; c.feedback = 0.5f;
        const int N = int (sr) * 3;
        std::vector<float> in (static_cast<size_t> (N)), oL (static_cast<size_t> (N)), oR (static_cast<size_t> (N));
        Rng rng;
        for (auto& v : in) v = 0.2f * rng.bi();
        int chunkPos = 0;
        for (int pos = 0; pos < N;)
        {
            if (chunkPos == 0) g.control (c);
            const int n = std::min ({ block, 32 - chunkPos, N - pos });
            g.process (in.data() + pos, in.data() + pos, oL.data() + pos, oR.data() + pos, n);
            pos += n; chunkPos = (chunkPos + n) % 32;
        }
        return oL;
    };
    const auto a = render (32), b = render (7), c = render (1);
    double errB = 0.0, errC = 0.0;
    for (size_t i = 0; i < a.size(); ++i)
    {
        errB = std::max (errB, double (std::abs (a[i] - b[i])));
        errC = std::max (errC, double (std::abs (a[i] - c[i])));
    }
    CHECK (errB == 0.0);
    CHECK (errC == 0.0);
}

TEST (grain_regrow_jumps_do_not_click)
{
    const float sr = 48000.0f;
    const int N = int (sr) * 6;
    GrainCloud g;
    g.prepare (sr);
    GrainCloud::Controls c;
    c.shape = 1.0f; c.sizeMs = 400.0f; c.density = 20.0f; c.spread = 0.0f; c.reverse = 0.0f;
    std::vector<float> in (static_cast<size_t> (N)), oL (static_cast<size_t> (N)), oR (static_cast<size_t> (N));
    for (int i = 0; i < N; ++i) in[size_t (i)] = 0.25f * std::sin (kTwoPi * 100.0f * float (i) / sr);
    auto run = [&] (bool jump)
    {
        g.reset();
        runBlocks (N, [&] (int pos) { c.feedback = (jump && (pos / 24000) % 2 == 1) ? 1.0f : 0.0f; g.control (c); },
                   [&] (int pos, int n) { g.process (in.data() + pos, in.data() + pos, oL.data() + pos, oR.data() + pos, n); });
        float worst = 0.0f;
        for (int i = 2; i < N; ++i)
            worst = std::max (worst, std::abs (oL[size_t (i)] - 2.0f * oL[size_t (i - 1)] + oL[size_t (i - 2)]));
        return worst;
    };
    const float base = run (false), jumping = run (true);
    CHECK (jumping < base * 4.0f + 1.0e-3f);      // was x160 with the un-smoothed gain
}

TEST (arp_recovers_after_the_input_gets_quieter)
{
    // Loud note, then the same note 28 dB quieter and sustained: the arp must keep playing.
    const float sr = 48000.0f;
    Arp arp;
    arp.prepare (sr);
    Arp::Controls c;
    c.pattern = 0; c.rate = 5; c.hold = 1.0f; c.track = false;
    const int N = int (sr) * 16;
    std::vector<float> in (static_cast<size_t> (N)), oL (static_cast<size_t> (N)), oR (static_cast<size_t> (N));
    for (int i = 0; i < N; ++i)
        in[size_t (i)] = (i < int (sr) * 2 ? 0.5f : 0.02f) * std::sin (kTwoPi * 330.0f * float (i) / sr);
    runBlocks (N, [&] (int pos) { c.beat = double (pos) / 24000.0; arp.control (c); },
               [&] (int pos, int n) { arp.process (in.data() + pos, in.data() + pos, oL.data() + pos, oR.data() + pos, n); });
    for (int sec = 9; sec < 16; ++sec)
        CHECK (rmsOf (oL.data() + int (sr) * sec, int (sr)) > 1.0e-3);
}

TEST (engine_reset_is_exact_with_stopped_transport_and_any_shimmer_interval)
{
    Engine e;
    e.prepare (48000.0, 512);
    ParamValues p;
    p[pRevShimmerPitch] = 1.0f; p[pRevShimmer] = 0.8f; p[pTapeStutter] = 0.5f;
    auto render = [&]
    {
        e.setParams (p);
        auto s = vtest::phrase (48000.0, 3.0);
        Transport tr;                      // not playing: the engine free-runs its own clock
        e.process (s.l.data(), s.r.data(), int (s.size()), tr);
        return s;
    };
    const auto a = render();
    e.reset();
    const auto b = render();
    double err = 0.0;
    for (size_t i = 0; i < a.size(); ++i) err = std::max (err, double (std::abs (a.l[i] - b.l[i])));
    CHECK (err == 0.0);
}

TEST (engine_is_block_size_invariant_at_very_slow_tempo_and_with_a_saturated_cloud)
{
    auto render = [] (int block, double bpm)
    {
        Engine e;
        e.prepare (48000.0, 4096);
        ParamValues p;
        p[pTapeStutter] = 1.0f; p[pTapeStretch] = 4.0f;
        p[pGrainDensity] = 80.0f; p[pGrainSize] = 800.0f; p[pBloom] = 1.0f; p[pGrowth] = 1.0f;
        e.setParams (p);
        auto s = vtest::phrase (48000.0, 6.0);
        Transport tr;
        tr.playing = true; tr.bpm = bpm;
        size_t pos = 0;
        while (pos < s.size())
        {
            const int n = int (std::min (size_t (block), s.size() - pos));
            tr.ppq = double (pos) / 48000.0 * bpm / 60.0;
            e.process (s.l.data() + pos, s.r.data() + pos, n, tr);
            pos += size_t (n);
        }
        return s;
    };
    for (double bpm : { 5.0, 12.0, 137.0 })
    {
        const auto a = render (32, bpm), b = render (7, bpm), c = render (4096, bpm);
        double errB = 0.0, errC = 0.0;
        for (size_t i = 0; i < a.size(); ++i)
        {
            errB = std::max (errB, double (std::abs (a.l[i] - b.l[i])));
            errC = std::max (errC, double (std::abs (a.l[i] - c.l[i])));
        }
        CHECK (errB < 1.0e-4);
        CHECK (errC < 1.0e-4);
    }
}

TEST (engine_dry_and_wet_paths_are_time_aligned)
{
    // With every module off the wet path is just the direct signal. Mixed 50/50 with the dry
    // path it must add perfectly in phase (no comb filtering) at every sample rate.
    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        Engine e;
        e.prepare (sr, 512);
        ParamValues p;
        for (PID m : { pTapeOn, pGrainOn, pArpOn, pChorusOn, pRevOn }) p[m] = 0.0f;
        p[pMix] = 0.5f; p[pOutWidth] = 1.0f; p[pOutDuck] = 0.0f; p[pDrift] = 0.0f;
        e.setParams (p);
        auto s = vtest::noiseBurst (sr, 1.0, 0.8, 0.2f);
        const auto ref = s;
        Transport tr;
        e.process (s.l.data(), s.r.data(), int (s.size()), tr);
        const int lat = e.latencySamples();
        // expected gain: (cos + sin)(pi/4) * centre trim
        const float g = 2.0f * 0.70710678f * (1.0f - 0.16f);
        double err = 0.0;
        for (size_t i = size_t (lat); i < s.size(); ++i)
            err = std::max (err, double (std::abs (s.l[i] - g * ref.l[i - size_t (lat)])));
        CHECK (err < 2.0e-4);
    }
}

TEST (engine_ducking_clears_space_while_playing)
{
    // Clearing = 1: the ambience is pushed down while the input sounds and returns afterwards.
    auto wetDuring = [] (float duck)
    {
        Engine e;
        e.prepare (48000.0, 512);
        ParamValues p;
        p[pOutDuck] = duck; p[pMix] = 1.0f; p[pTapeOn] = 0.0f; p[pArpOn] = 0.0f; p[pChorusOn] = 0.0f;
        p[pGrainMix] = 1.0f; p[pRevMix] = 1.0f; p[pTouch] = 0.0f; p[pDrift] = 0.0f;
        e.setParams (p);
        auto s = vtest::sine (48000.0, 6.0, 330.0, 0.3f);
        for (size_t i = 48000 * 3; i < s.size(); ++i) s.l[i] = s.r[i] = 0.0f;   // input stops at 3 s
        Transport tr;
        e.process (s.l.data(), s.r.data(), int (s.size()), tr);
        return std::pair<double, double> { rmsOf (s.l.data() + 96000, 40000), rmsOf (s.l.data() + 200000, 40000) };   // 4.2 s: after the duck has released
    };
    const auto open = wetDuring (0.0f), ducked = wetDuring (1.0f);
    CHECK (ducked.first < open.first * 0.6);        // clearly quieter while playing
    CHECK (ducked.second > open.second * 0.8);      // but the tail afterwards is (almost) untouched
}

TEST (engine_is_deterministic_and_resets_cleanly)
{
    Engine e;
    e.prepare (48000.0, 512);
    const ParamValues p = factoryPresets()[1].resolve();
    auto render = [&]
    {
        e.setParams (p);
        auto s = vtest::phrase (48000.0, 3.0);
        Transport tr; tr.playing = true;
        size_t pos = 0;
        while (pos < s.size())
        {
            const int n = int (std::min (size_t (512), s.size() - pos));
            tr.ppq = double (pos) / 24000.0;
            e.process (s.l.data() + pos, s.r.data() + pos, n, tr);
            pos += size_t (n);
        }
        return s;
    };
    const auto a = render();
    e.reset();
    const auto b = render();
    double err = 0.0;
    for (size_t i = 0; i < a.size(); ++i) err = std::max (err, double (std::abs (a.l[i] - b.l[i])));
    CHECK (err == 0.0);
}

TEST (engine_silence_in_gives_silence_out)
{
    for (const auto& preset : factoryPresets())
    {
        Engine e;
        e.prepare (48000.0, 512);
        e.setParams (preset.resolve());
        auto s = vtest::silence (48000.0, 2.0);
        Transport tr;
        e.process (s.l.data(), s.r.data(), int (s.size()), tr);
        float peak = 0.0f;
        for (size_t i = 0; i < s.size(); ++i) peak = std::max (peak, std::max (std::abs (s.l[i]), std::abs (s.r[i])));
        CHECK (peak == 0.0f);
    }
}

TEST (engine_recovers_from_non_finite_input)
{
    Engine e;
    e.prepare (48000.0, 512);
    e.setParams (factoryPresets().front().resolve());
    auto s = vtest::phrase (48000.0, 3.0);
    s.l[20000] = std::numeric_limits<float>::quiet_NaN();
    s.r[20500] = std::numeric_limits<float>::infinity();
    Transport tr;
    size_t pos = 0;
    bool allFinite = true;
    double lateEnergy = 0.0;
    while (pos < s.size())
    {
        const int n = int (std::min (size_t (256), s.size() - pos));
        e.process (s.l.data() + pos, s.r.data() + pos, n, tr);
        for (int i = 0; i < n; ++i)
        {
            allFinite = allFinite && std::isfinite (s.l[pos + size_t (i)]) && std::isfinite (s.r[pos + size_t (i)]);
            if (pos > 96000) lateEnergy += double (s.l[pos + size_t (i)]) * s.l[pos + size_t (i)];
        }
        pos += size_t (n);
    }
    CHECK (allFinite);
    CHECK (lateEnergy > 1.0e-3);     // it came back to life afterwards
}

TEST (engine_toggles_and_jumps_do_not_click)
{
    // A pure 100 Hz tone has a tiny second difference (about 4e-5 at this level), so any
    // discontinuity introduced by a switch shows up immediately. Grains / arp / stutter are off
    // because they are transient by nature; every other switch and jump is exercised.
    Engine e;
    e.prepare (48000.0, 512);
    ParamValues p;
    p[pGrainOn] = 0.0f; p[pArpOn] = 0.0f; p[pTapeStutter] = 0.0f; p[pTapeWow] = 0.0f; p[pTapeFlutter] = 0.0f;
    p[pChorusMix] = 0.3f; p[pMix] = 0.5f;
    const int N = 48000 * 12;
    std::vector<float> l (static_cast<size_t> (N)), r (static_cast<size_t> (N));
    for (int i = 0; i < N; ++i) l[size_t (i)] = r[size_t (i)] = 0.25f * std::sin (kTwoPi * 100.0f * float (i) / 48000.0f);
    Transport tr;
    Rng rng;
    for (int pos = 0; pos < N; pos += 256)
    {
        if (pos % 12032 == 0)        // roughly four changes a second
        {
            switch ((pos / 12032) % 12)
            {
                case 0: p[pBypass] = 1.0f - p[pBypass]; break;
                case 1: p[pTapeOn] = 1.0f - p[pTapeOn]; break;
                case 2: p[pChorusOn] = 1.0f - p[pChorusOn]; break;
                case 3: p[pRevOn] = 1.0f - p[pRevOn]; break;
                case 4: p[pMix] = p[pMix] > 0.5f ? 0.0f : 1.0f; break;
                case 5: p[pOutGain] = p[pOutGain] > 0.0f ? -12.0f : 6.0f; break;
                case 6: p[pRevFreeze] = 1.0f - p[pRevFreeze]; break;
                case 7: p[pOutTilt] = rng.bi(); p[pOutWidth] = rng.range (0.0f, 2.0f); break;
                case 8: p[pBloom] = rng.uni(); p[pSunlight] = rng.uni(); p[pRoots] = rng.uni(); p[pMoisture] = rng.uni(); break;
                case 9: p[pChorusVoices] = float (2 + rng.irange (23)); p[pChorusMode] = float (rng.irange (2)); break;
                case 10: p[pRevMix] = rng.uni(); p[pTapeMix] = rng.uni(); p[pChorusMix] = rng.uni(); break;
                default: p[pTapeDrive] = rng.uni(); p[pTapeAge] = rng.uni(); p[pRevDamping] = rng.range (500.0f, 20000.0f);
                         p[pDrift] = p[pDrift] > 0.5f ? 0.0f : 1.0f; p[pRevDecay] = p[pRevDecay] > 30.0f ? 0.3f : 61.0f; break;
            }
        }
        e.setParams (p);
        e.process (l.data() + pos, r.data() + pos, std::min (256, N - pos), tr);
    }
    float worst = 0.0f;
    int worstAt = 0;
    for (int i = 2; i < N; ++i)
    {
        const float d2 = std::abs (l[size_t (i)] - 2.0f * l[size_t (i - 1)] + l[size_t (i - 2)]);
        if (d2 > worst) { worst = d2; worstAt = i; }
    }
    if (worst >= 0.004f) std::printf ("  worst second difference %.5f at %.3f s\n", double (worst), worstAt / 48000.0);
    CHECK (worst < 0.004f);
}

TEST (engine_output_never_exceeds_ceiling_on_hostile_input)
{
    for (double sr : { 44100.0, 192000.0 })
        for (size_t pi = 0; pi < factoryPresets().size(); pi += 5)
        {
            Engine e;
            e.prepare (sr, 512);
            ParamValues p = factoryPresets()[pi].resolve();
            p[pOutGain] = 12.0f;
            e.setParams (p);
            auto s = vtest::hostile (sr, 4.0);
            Transport tr;
            e.process (s.l.data(), s.r.data(), int (s.size()), tr);
            float peak = 0.0f;
            for (size_t i = 0; i < s.size(); ++i) peak = std::max (peak, std::max (std::abs (s.l[i]), std::abs (s.r[i])));
            CHECK (peak <= 0.9661f);
        }
}

} // namespace

int main (int argc, char** argv)
{
    const std::string filter = argc > 1 ? argv[1] : "";
    int ran = 0;
    for (auto& t : tests())
    {
        if (! filter.empty() && std::string (t.name).find (filter) == std::string::npos)
            continue;
        gCurrent = t.name;
        const int before = gFailures;
        t.fn();
        std::printf ("%s %s\n", gFailures == before ? "ok  " : "FAIL", t.name);
        ++ran;
    }
    std::printf ("%d tests, %d checks, %d failures\n", ran, gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
