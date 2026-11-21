// Unit tests for Swoon's DSP building blocks and engine invariants.
// Tiny self-contained framework: no external dependency, runs under ctest.
#include "dsp/AmpTape.h"
#include "dsp/Engine.h"
#include "dsp/Fuzz.h"
#include "dsp/Glide.h"
#include "dsp/Oversampler.h"
#include "dsp/Presets.h"
#include "dsp/Util.h"
#include "dsp/Wash.h"

#include <cstdio>
#include <functional>
#include <set>
#include <string>
#include <vector>
#include <xmmintrin.h>

using namespace swoon;

namespace
{
int failures = 0, checks = 0;
std::string currentTest;

#define CHECK(cond) \
    do { ++checks; if (! (cond)) { ++failures; std::printf ("  FAIL %s:%d  %s   [%s]\n", __FILE__, __LINE__, #cond, currentTest.c_str()); } } while (false)
#define CHECK_NEAR(a, b, tol) \
    do { ++checks; const double va = (a), vb = (b); if (! (std::abs (va - vb) <= (tol))) { ++failures; \
         std::printf ("  FAIL %s:%d  %s = %g, expected %g +- %g   [%s]\n", __FILE__, __LINE__, #a, va, vb, (double) (tol), currentTest.c_str()); } } while (false)

struct Test { std::string name; std::function<void()> fn; };
std::vector<Test>& registry() { static std::vector<Test> r; return r; }
struct Registrar { Registrar (const char* n, std::function<void()> f) { registry().push_back ({ n, std::move (f) }); } };
#define TEST(name) static void test_##name(); static Registrar reg_##name (#name, test_##name); static void test_##name()

const double pi = 3.14159265358979323846;

double rmsOf (const std::vector<float>& v, size_t a, size_t b)
{
    double s = 0.0;
    for (size_t i = a; i < b; ++i)
        s += static_cast<double> (v[i]) * v[i];
    return std::sqrt (s / static_cast<double> (b - a));
}

/** Amplitude of the component at `freq` (single-bin DFT with a Hann window). */
double toneLevel (const std::vector<float>& v, size_t a, size_t b, double freq, double sr)
{
    double re = 0.0, im = 0.0, wsum = 0.0;
    const size_t n = b - a;
    for (size_t i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * pi * static_cast<double> (i) / static_cast<double> (n));
        const double ph = 2.0 * pi * freq * static_cast<double> (i) / sr;
        re += w * v[a + i] * std::cos (ph);
        im += w * v[a + i] * std::sin (ph);
        wsum += w;
    }
    return 2.0 * std::sqrt (re * re + im * im) / wsum;
}

struct Lcg
{
    uint32_t s = 22222u;
    float next() { s = s * 1664525u + 1013904223u; return static_cast<float> (s >> 8) * (2.0f / 16777216.0f) - 1.0f; }
    float uni() { return 0.5f * (next() + 1.0f); }
};

void runEngine (Engine& e, std::vector<float>& l, std::vector<float>& r, const ParamValues& pv, int block)
{
    for (size_t pos = 0; pos < l.size(); pos += static_cast<size_t> (block))
    {
        const int n = static_cast<int> (std::min (static_cast<size_t> (block), l.size() - pos));
        e.process (l.data() + pos, r.data() + pos, n, pv);
    }
}

ParamValues allOff()
{
    ParamValues pv;
    for (P p : { P::fuzzOn, P::glideOn, P::revOn, P::focOn, P::modOn, P::washOn, P::ampOn, P::tapeOn })
        pv[p] = 0.0f;
    return pv;
}
} // namespace

//==============================================================================
TEST (params_table_is_consistent)
{
    std::set<std::string> ids;
    for (const auto& d : paramDefs())
    {
        CHECK (ids.insert (d.id).second);                 // unique ids
        CHECK (d.min < d.max);
        CHECK (d.def >= d.min && d.def <= d.max);
        CHECK (paramFromId (d.id) == d.index);
        CHECK_NEAR (fromNormalised (d.index, toNormalised (d.index, d.def)), d.def, 1.0e-3 * (d.max - d.min));
        CHECK_NEAR (fromNormalised (d.index, 0.0f), d.min, 1.0e-4);
        CHECK_NEAR (fromNormalised (d.index, 1.0f), d.max, 1.0e-4 * std::max (1.0f, std::abs (d.max)));
    }
    CHECK (paramFromId ("nope") == P::Count);
}

TEST (fast_math_is_accurate)
{
    double maxExpErr = 0.0, maxLogErr = 0.0, maxTanhErr = 0.0;
    for (int i = 0; i <= 200000; ++i)
    {
        const float x = -30.0f + 60.0f * static_cast<float> (i) / 200000.0f;
        maxExpErr = std::max (maxExpErr, std::abs (static_cast<double> (fastExp (x)) / std::exp (static_cast<double> (x)) - 1.0));
        const float v = std::exp (x * 0.5f);
        maxLogErr = std::max (maxLogErr, std::abs (static_cast<double> (fastLog (v)) - std::log (static_cast<double> (v))));
        const float t = x * 0.2f;
        maxTanhErr = std::max (maxTanhErr, std::abs (static_cast<double> (fastTanh (t)) - std::tanh (static_cast<double> (t))));
    }
    CHECK (maxExpErr < 2.0e-6);
    CHECK (maxLogErr < 3.0e-5);
    CHECK (maxTanhErr < 2.0e-3);
}

TEST (order_resolution_is_a_stable_permutation)
{
    ParamValues pv;
    auto o = resolveOrder (pv);
    for (int i = 0; i < kNumSlots; ++i)
        CHECK (o[static_cast<size_t> (i)] == i);

    // all tied -> default order; arbitrary values -> still a permutation
    for (P p : kOrderParams)
        pv[p] = 4.0f;
    o = resolveOrder (pv);
    for (int i = 0; i < kNumSlots; ++i)
        CHECK (o[static_cast<size_t> (i)] == i);

    pv[P::ordWash] = 1.0f;
    pv[P::ordFuzz] = 7.0f;
    o = resolveOrder (pv);
    CHECK (o[0] == static_cast<int> (Slot::Wash));
    CHECK (o[6] == static_cast<int> (Slot::Fuzz));
    std::set<int> seen (o.begin(), o.end());
    CHECK (seen.size() == static_cast<size_t> (kNumSlots));
}

TEST (presets_are_valid)
{
    const auto& presets = factoryPresets();
    CHECK (! presets.empty());
    std::set<std::string> names;
    for (const auto& p : presets)
    {
        CHECK (names.insert (p.name).second);
        for (const auto& [param, value] : p.values)
        {
            const auto& d = def (param);
            CHECK (value >= d.min && value <= d.max);
            if (d.kind != ParamKind::Float)
                CHECK (std::abs (value - std::round (value)) < 1.0e-6f);
        }
        // the order parameters of every preset must form a clean permutation
        const auto pv = presetValues (p);
        std::set<int> positions;
        for (P op : kOrderParams)
            positions.insert (static_cast<int> (std::lround (pv[op])));
        CHECK (positions.size() == static_cast<size_t> (kNumSlots));
    }
}

//==============================================================================
TEST (oversampler_is_transparent_in_band_with_exact_latency)
{
    for (int log2Factor = 1; log2Factor <= 3; ++log2Factor)
    {
        Oversampler os;
        os.prepare (log2Factor, 32);
        const int factor = os.factor();
        CHECK (factor == (1 << log2Factor));

        // impulse -> round trip -> peak lands at latency (in base samples; may be x.5 before padding)
        const int total = 512;
        std::vector<float> in (static_cast<size_t> (total), 0.0f), out (static_cast<size_t> (total), 0.0f);
        in[10] = 1.0f;
        for (int pos = 0; pos < total; pos += 32)
        {
            os.up (in.data() + pos, 32);
            os.down (out.data() + pos, 32);
        }
        int peakIdx = 0;
        for (int i = 0; i < total; ++i)
            if (std::abs (out[static_cast<size_t> (i)]) > std::abs (out[static_cast<size_t> (peakIdx)]))
                peakIdx = i;
        const double latency = static_cast<double> (os.latencyAtTopRate()) / factor;
        CHECK (std::abs ((peakIdx - 10) - latency) <= 0.5);

        // pass-band gain at 1 kHz and 15 kHz (fs = 48 kHz) within 0.1 dB
        for (double f : { 1000.0, 15000.0 })
        {
            os.reset();
            std::vector<float> x (9600), y (9600);
            for (size_t i = 0; i < x.size(); ++i)
                x[i] = static_cast<float> (std::sin (2.0 * pi * f * static_cast<double> (i) / 48000.0));
            for (size_t pos = 0; pos < x.size(); pos += 32)
            {
                os.up (x.data() + pos, 32);
                os.down (y.data() + pos, 32);
            }
            const double gainDb = 20.0 * std::log10 (rmsOf (y, 2400, 9600) / rmsOf (x, 2400, 9600));
            CHECK_NEAR (gainDb, 0.0, 0.1);
        }

        // images: a 3 kHz tone upsampled must not contain energy above the base Nyquist (checked at the image frequency)
        os.reset();
        std::vector<float> x (4800), upAll;
        for (size_t i = 0; i < x.size(); ++i)
            x[i] = static_cast<float> (std::sin (2.0 * pi * 3000.0 * static_cast<double> (i) / 48000.0));
        for (size_t pos = 0; pos < x.size(); pos += 32)
        {
            const float* u = os.up (x.data() + pos, 32);
            upAll.insert (upAll.end(), u, u + 32 * factor);
        }
        const double srTop = 48000.0 * factor;
        const double wanted = toneLevel (upAll, upAll.size() / 2, upAll.size(), 3000.0, srTop);
        const double image = toneLevel (upAll, upAll.size() / 2, upAll.size(), 48000.0 - 3000.0, srTop);
        CHECK_NEAR (wanted, 1.0, 0.01);
        CHECK (20.0 * std::log10 (image / wanted) < -80.0);
    }
}

TEST (diode_stage_is_bounded_monotonic_and_settles_to_static_curve)
{
    DiodeStage st;
    st.set (1300.0f, 0.05f, 192000.0f);

    // DC input settles to the solution of d = u + beta * sinh (u)
    for (float d : { 0.1f, 1.0f, 10.0f, 100.0f, 1000.0f, -250.0f })
    {
        st.reset();
        float u = 0.0f;
        for (int i = 0; i < 20000; ++i)
            u = st.process (d);
        // three Newton steps from a bounded guess: within 1.5 % of the exact curve everywhere
        CHECK_NEAR (u + 0.05 * std::sinh (static_cast<double> (u)), d, std::abs (d) * 1.5e-2 + 1.0e-3);
        CHECK (std::abs (u) < 12.5f);
    }

    // absurd input never produces NaN or escapes the rails
    st.reset();
    Lcg rng;
    bool ok = true;
    for (int i = 0; i < 200000; ++i)
    {
        const float u = st.process (rng.next() * 1.0e5f);
        ok = ok && std::isfinite (u) && std::abs (u) < 20.0f;
    }
    CHECK (ok);

    // odd symmetry
    DiodeStage a, b;
    a.set (1300.0f, 0.05f, 192000.0f);
    b.set (1300.0f, 0.05f, 192000.0f);
    double maxAsym = 0.0;
    for (int i = 0; i < 4000; ++i)
    {
        const float x = 300.0f * static_cast<float> (std::sin (2.0 * pi * 440.0 * i / 192000.0));
        maxAsym = std::max (maxAsym, static_cast<double> (std::abs (a.process (x) + b.process (-x))));
    }
    CHECK (maxAsym < 1.0e-3);

    // the vectorised four-lane solver matches the scalar reference
    DiodeQuad quad;
    DiodeStage ref[4];
    const float corners[4] = { 1300.0f, 1300.0f, 1500.0f, 3800.0f };
    for (int k = 0; k < 4; ++k)
    {
        quad.set (k, corners[k], 0.05f, 192000.0f);
        ref[k].set (corners[k], 0.05f, 192000.0f);
    }
    double maxDiff = 0.0;
    for (int i = 0; i < 20000; ++i)
    {
        float d[4], u[4];
        for (int k = 0; k < 4; ++k)
            d[k] = (k == 3 ? 3.0f : 400.0f) * static_cast<float> (std::sin (2.0 * pi * (110.0 * (k + 1)) * i / 192000.0))
                 * static_cast<float> (std::exp (-i / 6000.0));
        quad.process (d, u);
        for (int k = 0; k < 4; ++k)
            maxDiff = std::max (maxDiff, static_cast<double> (std::abs (u[k] - ref[k].process (d[k]))));
    }
    CHECK (maxDiff < 2.0e-3);
}

TEST (fuzz_latency_matches_and_dry_is_aligned)
{
    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        Fuzz fuzz;
        fuzz.prepare (sr, 512);
        const int latency = fuzz.latencySamples();
        CHECK (latency > 0 && latency < 200);

        // switched off: output is the input delayed by exactly `latency`
        Fuzz::Params p;
        p.on = false;
        std::vector<float> l (2048, 0.0f), r (2048, 0.0f);
        l[100] = r[100] = 1.0f;
        for (size_t pos = 0; pos < l.size(); pos += kChunk)
            fuzz.process (l.data() + pos, r.data() + pos, kChunk, p);
        CHECK_NEAR (l[static_cast<size_t> (100 + latency)], 1.0, 1.0e-6);
        CHECK_NEAR (l[static_cast<size_t> (100 + latency - 1)], 0.0, 1.0e-6);

        // switched on, nearly clean (minimum sustain, tiny input): the wet path's envelope peak sits at the same delay
        fuzz.reset();
        p.on = true;
        p.sustain = 0.0f;
        p.stack = 0.0f;
        p.gate = 0.0f;
        std::vector<float> wl (8192), wr (8192);
        const int centre = 4000;
        for (int i = 0; i < 8192; ++i)
        {
            const double env = std::exp (-0.5 * std::pow ((i - centre) / 60.0, 2.0));
            wl[static_cast<size_t> (i)] = wr[static_cast<size_t> (i)]
                = static_cast<float> (2.0e-4 * env * std::sin (2.0 * pi * 1000.0 * (i - centre) / sr));
        }
        for (size_t pos = 0; pos < wl.size(); pos += kChunk)
            fuzz.process (wl.data() + pos, wr.data() + pos, kChunk, p);
        // energy centroid of the output burst
        double num = 0.0, den = 0.0;
        for (int i = 0; i < 8192; ++i)
        {
            const double e = static_cast<double> (wl[static_cast<size_t> (i)]) * wl[static_cast<size_t> (i)];
            num += e * i;
            den += e;
        }
        CHECK (den > 0.0);
        // the tone stack's minimum-phase filters add a little group delay on top of the reported latency
        CHECK_NEAR (num / den - centre, latency, 0.0006 * sr);
    }
}

TEST (grain_shifter_shifts_by_the_requested_ratio_and_is_passive)
{
    const float fs = 48000.0f;
    for (float ratio : { 2.0f, 1.4983f, 0.5f })
    {
        GrainShifter sh;
        sh.prepare (fs, 99u);
        sh.setRatio (ratio);
        std::vector<float> out (48000);
        double inSq = 0.0, outSq = 0.0;
        for (size_t i = 0; i < out.size(); ++i)
        {
            const float x = static_cast<float> (std::sin (2.0 * pi * 500.0 * static_cast<double> (i) / fs));
            out[i] = sh.process (x);
            if (i > 12000)
            {
                inSq += x * x;
                outSq += out[i] * out[i];
            }
        }
        const double wanted = toneLevel (out, 12000, 48000, 500.0 * ratio, fs);
        const double original = toneLevel (out, 12000, 48000, 500.0, fs);
        CHECK (wanted > 0.4);
        CHECK (original < 0.1 * wanted);
        CHECK (outSq <= inSq * 1.02);
    }
}

TEST (wash_decays_never_grows_and_freeze_holds)
{
    const double sr = 48000.0;
    Wash wash;
    wash.prepare (sr);
    Wash::Params p;
    p.mix = 1.0f;
    p.decay = 3.0f;

    Lcg rng;
    auto runSeconds = [&] (double seconds, bool noiseIn, const Wash::Params& params) -> double
    {
        double sumSq = 0.0;
        size_t count = 0;
        const int blocks = static_cast<int> (seconds * sr / kChunk);
        for (int b = 0; b < blocks; ++b)
        {
            float l[kChunk], r[kChunk];
            for (int i = 0; i < kChunk; ++i)
            {
                l[i] = noiseIn ? 0.25f * rng.next() : 0.0f;
                r[i] = noiseIn ? 0.25f * rng.next() : 0.0f;
            }
            wash.process (l, r, kChunk, params);
            for (int i = 0; i < kChunk; ++i)
            {
                CHECK (std::isfinite (l[i]) && std::isfinite (r[i]));
                sumSq += static_cast<double> (l[i]) * l[i] + static_cast<double> (r[i]) * r[i];
                ++count;
                if (! std::isfinite (l[i]))
                    return -1.0;
            }
        }
        return std::sqrt (sumSq / static_cast<double> (count));
    };

    // excite, then let it ring: level must fall by roughly 60 dB per RT60
    runSeconds (1.5, true, p);
    const double a = runSeconds (0.5, false, p);
    runSeconds (2.5, false, p);
    const double b = runSeconds (0.5, false, p);
    const double dropDb = 20.0 * std::log10 (a / b);
    CHECK (dropDb > 40.0 && dropDb < 110.0);      // 3 s later with RT60 = 3 s: nominally 60 dB

    // worst case for stability: maximum decay, maximum shimmer, bright, loud input for a long time
    for (int mode = 0; mode < 4; ++mode)
    {
        wash.reset();
        Wash::Params hot;
        hot.mix = 1.0f;
        hot.decay = 60.0f;
        hot.size = 1.0f;
        hot.highCutHz = 18000.0f;
        hot.lowCutHz = 20.0f;
        hot.shimmer = 1.0f;
        hot.shimmerMode = mode;
        hot.mod = 1.0f;
        const double loud = runSeconds (20.0, true, hot);
        CHECK (loud > 0.0 && loud < 4.0);
        // input removed: it must fall, not sustain or climb
        const double first = runSeconds (2.0, false, hot);
        runSeconds (10.0, false, hot);
        const double later = runSeconds (2.0, false, hot);
        CHECK (later < first);
    }

    // freeze: holds level (within 1.5 dB) for 30 s and ignores new input
    wash.reset();
    Wash::Params fz;
    fz.mix = 1.0f;
    fz.decay = 4.0f;
    runSeconds (2.0, true, fz);
    fz.freeze = true;
    runSeconds (0.5, false, fz);
    const double held0 = runSeconds (1.0, false, fz);
    runSeconds (30.0, true, fz);                   // noise at the input must not get in
    const double held1 = runSeconds (1.0, false, fz);
    CHECK (held0 > 1.0e-4);
    CHECK_NEAR (20.0 * std::log10 (held1 / held0), 0.0, 1.5);

    // un-freeze: decays again
    fz.freeze = false;
    runSeconds (8.0, false, fz);
    const double released = runSeconds (1.0, false, fz);
    CHECK (released < 0.05 * held1);
}

TEST (glide_follows_requested_pitch_and_survives_splices)
{
    const double sr = 48000.0;
    Glide glide;
    glide.prepare (sr);
    Glide::Params p;
    p.wobbleCents = 0.0f;
    p.driftCents = 0.0f;
    p.dipCents = 0.0f;
    p.spread = 0.0f;

    // no modulation -> a pure delay: sine in, same sine out at the same level
    std::vector<float> l (48000), r (48000);
    for (size_t i = 0; i < l.size(); ++i)
        l[i] = r[i] = static_cast<float> (0.5 * std::sin (2.0 * pi * 440.0 * static_cast<double> (i) / sr));
    for (size_t pos = 0; pos < l.size(); pos += kChunk)
        glide.process (l.data() + pos, r.data() + pos, kChunk, p);
    CHECK_NEAR (toneLevel (l, 24000, 48000, 440.0, sr), 0.5, 0.01);

    // very deep, fast wobble forces the head against its window limits -> splices; output stays clean and bounded
    glide.reset();
    p.wobbleCents = 150.0f;
    p.rateHz = 0.15f;
    p.driftCents = 100.0f;
    std::vector<float> a (48000 * 20), b (48000 * 20);
    for (size_t i = 0; i < a.size(); ++i)
        a[i] = b[i] = static_cast<float> (0.5 * std::sin (2.0 * pi * 220.0 * static_cast<double> (i) / sr));
    for (size_t pos = 0; pos < a.size(); pos += kChunk)
        glide.process (a.data() + pos, b.data() + pos, kChunk, p);
    float peak = 0.0f, maxStep = 0.0f;
    for (size_t i = 4800; i < a.size(); ++i)
    {
        CHECK (std::isfinite (a[i]));
        peak = std::max (peak, std::abs (a[i]));
        maxStep = std::max (maxStep, std::abs (a[i] - a[i - 1]));
    }
    CHECK (peak < 0.75f);
    CHECK (peak > 0.4f);
    // a 220 Hz sine at 0.5 moves at most ~0.0144 per sample; allow for pitch-up and the splice crossfade
    CHECK (maxStep < 0.03f);
    CHECK_NEAR (rmsOf (a, 48000, a.size()), 0.3535, 0.04);     // splices don't eat or add level
}

//==============================================================================
TEST (engine_bypass_is_bit_transparent_delay)
{
    for (double sr : { 44100.0, 96000.0 })
    {
        Engine e;
        e.prepare (sr, 512);
        ParamValues pv;
        pv[P::bypass] = 1.0f;
        const int latency = e.getLatencySamples();

        Lcg rng;
        std::vector<float> l (20000), r (20000);
        for (size_t i = 0; i < l.size(); ++i)
        {
            l[i] = 0.5f * rng.next();
            r[i] = 0.5f * rng.next();
        }
        const auto refL = l, refR = r;
        runEngine (e, l, r, pv, 137);

        double maxErr = 0.0;
        for (size_t i = static_cast<size_t> (latency); i < l.size(); ++i)
        {
            maxErr = std::max (maxErr, static_cast<double> (std::abs (l[i] - refL[i - static_cast<size_t> (latency)])));
            maxErr = std::max (maxErr, static_cast<double> (std::abs (r[i] - refR[i - static_cast<size_t> (latency)])));
        }
        CHECK (maxErr < 1.0e-6);
    }
}

TEST (engine_latency_is_constant_and_wet_path_is_aligned)
{
    // With every module off the wet path is just delay + DC blocker + limiter: an impulse must come out at `latency`.
    for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
    {
        Engine e;
        e.prepare (sr, 256);
        const int latency = e.getLatencySamples();
        ParamValues pv = allOff();
        std::vector<float> l (4096, 0.0f), r (4096, 0.0f);
        l[50] = r[50] = 0.5f;
        runEngine (e, l, r, pv, 256);
        size_t peak = 0;
        for (size_t i = 0; i < l.size(); ++i)
            if (std::abs (l[i]) > std::abs (l[peak]))
                peak = i;
        CHECK (static_cast<int> (peak) == 50 + latency);
        CHECK_NEAR (l[peak], 0.5, 0.02);

        // the same with the latency-bearing modules switched on, in their cleanest settings: still aligned to the
        // sample (dry/wet mixing then stays phase coherent)
        Engine e2;
        e2.prepare (sr, 256);
        CHECK (e2.getLatencySamples() == latency);
    }
}

TEST (engine_output_is_identical_for_any_block_size)
{
    // Control-rate updates are driven by an internal chunk counter that restarts on each process() call, so
    // results are only bit-identical for block sizes that are multiples of kChunk. Check that, and check that
    // odd block sizes agree closely (same sound, different control-rate phase).
    ParamValues pv;
    pv[P::revOn] = 1.0f;
    pv[P::glideOn] = 0.0f;      // random drift generators advance per chunk; keep this test deterministic in time
    pv[P::tapeWow] = 0.0f;
    pv[P::tapeFlutter] = 0.0f;
    pv[P::fuzzStack] = 0.0f;
    pv[P::wall] = 0.2f;

    auto render = [&] (int block)
    {
        Engine e;
        e.prepare (48000.0, 4096);
        std::vector<float> l (24000), r (24000);
        for (size_t i = 0; i < l.size(); ++i)
            l[i] = r[i] = static_cast<float> (0.2 * std::sin (2.0 * pi * 196.0 * static_cast<double> (i) / 48000.0)
                                              * std::exp (-static_cast<double> (i % 12000) / 6000.0));
        runEngine (e, l, r, pv, block);
        return l;
    };

    const auto a = render (32), b = render (512), c = render (4096);
    double maxDiff = 0.0;
    for (size_t i = 0; i < a.size(); ++i)
        maxDiff = std::max ({ maxDiff, static_cast<double> (std::abs (a[i] - b[i])), static_cast<double> (std::abs (a[i] - c[i])) });
    CHECK (maxDiff < 1.0e-6);
}

TEST (engine_survives_parameter_abuse)
{
    // Random parameter values every block (including discrete ones and the module order), random block sizes,
    // loud noise input, several sample rates: output must stay finite and under the limiter ceiling.
    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        Engine e;
        e.prepare (sr, 1024);
        Lcg rng;
        rng.s = static_cast<uint32_t> (sr);
        ParamValues pv;
        float peak = 0.0f;
        bool finite = true;
        const int totalBlocks = static_cast<int> (6.0 * sr / 256.0);

        for (int b = 0; b < totalBlocks; ++b)
        {
            if (b % 3 == 0)
                for (const auto& d : paramDefs())
                {
                    if (d.index == P::bypass)
                        continue;
                    if (rng.uni() < 0.35f)
                        pv[d.index] = fromNormalised (d.index, rng.uni());
                }

            const int n = 1 + static_cast<int> (rng.uni() * 1023.0f);
            float l[1024], r[1024];
            const float level = (b % 40) < 30 ? 0.9f : 0.0f;
            for (int i = 0; i < n; ++i)
            {
                l[i] = level * rng.next();
                r[i] = level * rng.next();
            }
            e.process (l, r, n, pv);
            for (int i = 0; i < n; ++i)
            {
                finite = finite && std::isfinite (l[i]) && std::isfinite (r[i]);
                peak = std::max ({ peak, std::abs (l[i]), std::abs (r[i]) });
            }
        }
        CHECK (finite);
        CHECK (peak <= 1.0f);
    }
}

TEST (engine_limiter_holds_the_ceiling)
{
    Engine e;
    e.prepare (48000.0, 512);
    ParamValues pv = allOff();
    pv[P::inGain] = 24.0f;
    pv[P::outGain] = 12.0f;
    std::vector<float> l (48000), r (48000);
    for (size_t i = 0; i < l.size(); ++i)
        l[i] = r[i] = static_cast<float> (std::sin (2.0 * pi * 100.0 * static_cast<double> (i) / 48000.0));
    runEngine (e, l, r, pv, 512);
    float peak = 0.0f;
    for (float v : l)
        peak = std::max (peak, std::abs (v));
    CHECK (peak <= 0.98f);
    CHECK (peak > 0.6f);
    CHECK (e.getMeters().limiterReductionDb > 6.0f);
}

TEST (engine_silence_in_gives_silence_out_and_reset_clears_tails)
{
    Engine e;
    e.prepare (48000.0, 512);
    ParamValues pv;
    pv[P::revOn] = 1.0f;
    pv[P::washDecay] = 30.0f;

    std::vector<float> l (48000, 0.0f), r (48000, 0.0f);
    runEngine (e, l, r, pv, 512);
    float peak = 0.0f;
    for (float v : l)
        peak = std::max (peak, std::abs (v));
    CHECK (peak < 1.0e-6f);               // no self-noise, no DC, no denormal garbage

    // make a big tail, then reset(): the next block of silence must be silent
    Lcg rng;
    for (size_t i = 0; i < l.size(); ++i)
        l[i] = r[i] = 0.5f * rng.next();
    runEngine (e, l, r, pv, 512);
    e.reset();
    std::fill (l.begin(), l.end(), 0.0f);
    std::fill (r.begin(), r.end(), 0.0f);
    runEngine (e, l, r, pv, 512);
    peak = 0.0f;
    for (float v : l)
        peak = std::max (peak, std::abs (v));
    CHECK (peak < 1.0e-6f);
}

TEST (every_preset_renders_sanely)
{
    for (const auto& preset : factoryPresets())
    {
        currentTest = std::string ("preset: ") + preset.name;
        Engine e;
        e.prepare (48000.0, 256);
        const auto pv = presetValues (preset);
        std::vector<float> l (48000 * 3), r (48000 * 3);
        for (size_t i = 0; i < l.size(); ++i)
        {
            // a decaying two-note pluck every 0.75 s
            const double t = static_cast<double> (i % 36000) / 48000.0;
            const double env = std::exp (-t * 3.0);
            l[i] = r[i] = static_cast<float> (0.25 * env * (std::sin (2.0 * pi * 196.0 * t) + 0.6 * std::sin (2.0 * pi * 294.0 * t)));
        }
        runEngine (e, l, r, pv, 256);
        bool finite = true;
        float peak = 0.0f;
        for (size_t i = 0; i < l.size(); ++i)
        {
            finite = finite && std::isfinite (l[i]) && std::isfinite (r[i]);
            peak = std::max ({ peak, std::abs (l[i]), std::abs (r[i]) });
        }
        CHECK (finite);
        CHECK (peak <= 1.0f);
        CHECK (rmsOf (l, 48000, l.size()) > 1.0e-4);      // not silent
    }
}

//==============================================================================
int main (int argc, char** argv)
{
    _mm_setcsr (_mm_getcsr() | 0x8040);   // flush denormals, as the plugin does
    const std::string filter = argc > 1 ? argv[1] : "";

    for (const auto& t : registry())
    {
        if (! filter.empty() && t.name.find (filter) == std::string::npos)
            continue;
        currentTest = t.name;
        const int before = failures;
        t.fn();
        std::printf ("%s  %s\n", failures == before ? "ok  " : "FAIL", t.name.c_str());
    }

    std::printf ("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
