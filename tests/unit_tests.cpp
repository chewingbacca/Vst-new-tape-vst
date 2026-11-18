// Verdant unit tests — tiny self-contained framework (no external dependencies).
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "TestSignals.h"
#include "dsp/Engine.h"
#include "dsp/Output.h"
#include "dsp/Params.h"
#include "dsp/Presets.h"
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
