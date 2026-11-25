// Effect tests: robustness for all 16 effects (random parameters, extremes, silence tails, block
// splitting) and one functional check per effect against what its parameters promise.
#include "TestFramework.h"
#include "fx/Effect.h"
#include "util/Rng.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <memory>
#if defined(__SSE__) || defined(_M_X64)
 #include <xmmintrin.h>
#endif
using namespace atm;

namespace {

constexpr double kSr = 48000.0;
constexpr double kBpm = 120.0;

struct Buf
{
    std::vector<float> l, r;
    Buf() = default;
    explicit Buf (size_t n) : l (n, 0.f), r (n, 0.f) {}
    size_t size() const { return l.size(); }
};

Buf noise (size_t n, float rms, uint32_t seed)
{
    Buf b (n);
    Rng g (seed);
    const float a = rms * std::sqrt (3.f);                     // uniform noise: rms = a / sqrt 3
    for (size_t i = 0; i < n; ++i) { b.l[i] = a * (2.f * hashToFloat (g.next()) - 1.f); b.r[i] = a * (2.f * hashToFloat (g.next()) - 1.f); }
    return b;
}

// kick on beats, snare on 2 and 4, hats on 8ths, peak about -6 dBFS (rms about -14 dB)
Buf drums (size_t n, double sr = kSr, double bpm = kBpm)
{
    Buf b (n);
    Rng g (77);
    const double beat = sr * 60.0 / bpm;
    for (size_t i = 0; i < n; ++i)
    {
        const double pb = std::fmod ((double) i, beat) / sr;
        const int beatIdx = (int) ((double) i / beat);
        const float nz = 2.f * hashToFloat (g.next()) - 1.f;
        float a = (float) (0.5 * std::sin (kTau * (45.0 * pb + 6.0 * (1.0 - std::exp (-pb * 40.0)))) * std::exp (-pb * 9.0));
        if (beatIdx % 2 == 1) a += 0.25f * nz * (float) std::exp (-pb * 22.0);
        const double p8 = std::fmod ((double) i, beat * 0.5) / sr;
        a += 0.08f * nz * (float) std::exp (-p8 * 90.0);
        b.l[i] = a; b.r[i] = 0.9f * a;
    }
    return b;
}

Buf sine (size_t n, double hz, float amp, double sr = kSr)
{
    Buf b (n);
    for (size_t i = 0; i < n; ++i) b.l[i] = b.r[i] = amp * (float) std::sin (kTau * hz * (double) i / sr);
    return b;
}

void defaults (int type, float* p)
{
    const ParamDesc* d = fxParamDescs (type);
    for (int i = 0; i < kFxParams; ++i) p[i] = d[i].key[0] ? d[i].defNorm() : 0.f;
}

int idx (int type, const char* key)
{
    const ParamDesc* d = fxParamDescs (type);
    for (int i = 0; i < kFxParams; ++i) if (std::strcmp (d[i].key, key) == 0) return i;
    std::printf ("  no param %s\n", key);
    return 0;
}
// set a parameter by physical value
void setPhys (int type, float* p, const char* key, float v) { const int i = idx (type, key); p[i] = fxParamDescs (type)[i].toNorm (v); }

using ParamFn = std::function<void (int64_t pos, float* p)>;

struct Rig
{
    std::unique_ptr<Effect> fx;
    double sr = kSr, bpm = kBpm;
    int64_t pos = 0;
    Rig (int type, double sampleRate = kSr) : fx (createEffect (type)), sr (sampleRate) { fx->prepare (sr); fx->reset(); }

    // processes b in place, starting at absolute sample `pos`
    void run (Buf& b, const float* base, int block = 16, const ParamFn& fn = nullptr, size_t from = 0, size_t to = (size_t) -1)
    {
        to = std::min (to, b.size());
        FxContext c; c.sampleRate = sr; c.bpm = bpm; c.playing = true;
        float scl[32], scr[32];
        for (size_t i = from; i < to;)
        {
            const int n = (int) std::min<size_t> ((size_t) block, to - i);
            float p[kFxParams]; std::memcpy (p, base, sizeof (p));
            if (fn) fn (pos, p);
            c.beat = (double) pos / sr * bpm / 60.0;
            std::memcpy (scl, b.l.data() + i, sizeof (float) * (size_t) n); std::memcpy (scr, b.r.data() + i, sizeof (float) * (size_t) n);
            for (int s = 1; s < kScSources; ++s) { c.scL[s] = scl; c.scR[s] = scr; }
            fx->setParams (p, c);
            fx->process (b.l.data() + i, b.r.data() + i, n, c);
            i += (size_t) n; pos += n;
        }
    }
};

double rms (const std::vector<float>& x, size_t a = 0, size_t b = (size_t) -1)
{
    b = std::min (b, x.size()); double s = 0; for (size_t i = a; i < b; ++i) s += (double) x[i] * x[i];
    return b > a ? std::sqrt (s / (double) (b - a)) : 0.0;
}
double peakAbs (const std::vector<float>& x, size_t a = 0, size_t b = (size_t) -1)
{
    b = std::min (b, x.size()); double m = 0; for (size_t i = a; i < b; ++i) m = std::max (m, (double) std::fabs (x[i])); return m;
}
double db (double g) { return 20.0 * std::log10 (std::max (g, 1.0e-12)); }

// amplitude of a sinusoid at hz in x[a..b) (Hann window, coherent gain corrected)
double toneAmp (const std::vector<float>& x, double hz, size_t a, size_t b, double sr = kSr)
{
    double re = 0, im = 0, wsum = 0;
    const size_t n = b - a;
    for (size_t i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (kTau * (double) i / (double) n);
        const double ph = kTau * hz * (double) i / sr;
        re += w * x[a + i] * std::cos (ph); im -= w * x[a + i] * std::sin (ph);
        wsum += w;
    }
    return 2.0 * std::sqrt (re * re + im * im) / wsum;
}

// strongest frequency near `guess` (+-range cents), found by a fine scan and parabolic refinement
double peakFreq (const std::vector<float>& x, double guess, double rangeCents, size_t a, size_t b)
{
    double best = guess, bestA = -1;
    for (double c = -rangeCents; c <= rangeCents; c += 0.5)
    {
        const double f = guess * std::exp2 (c / 1200.0), A = toneAmp (x, f, a, b);
        if (A > bestA) { bestA = A; best = f; }
    }
    const double s = std::exp2 (0.5 / 1200.0);
    const double y0 = toneAmp (x, best / s, a, b), y1 = bestA, y2 = toneAmp (x, best * s, a, b);
    const double den = y0 - 2 * y1 + y2;
    const double off = den != 0 ? 0.5 * (y0 - y2) / den : 0.0;
    return best * std::exp2 (off * 0.5 / 1200.0);
}
double cents (double f, double ref) { return 1200.0 * std::log2 (f / ref); }

bool allFinite (const Buf& b, double limit, double* worst = nullptr)
{
    double m = 0; bool ok = true;
    for (size_t i = 0; i < b.size(); ++i)
    {
        if (! std::isfinite (b.l[i]) || ! std::isfinite (b.r[i])) ok = false;
        else m = std::max ({ m, (double) std::fabs (b.l[i]), (double) std::fabs (b.r[i]) });
    }
    if (worst) *worst = m;
    return ok && m < limit;
}

// RT60 of the tail that starts at sample `from`: Schroeder integration, line fitted from -5 to -25 dB
double rt60 (const Buf& b, size_t from, double sr = kSr)
{
    const size_t n = b.size() - from;
    std::vector<double> e (n);
    double acc = 0;
    for (size_t i = n; i-- > 0;) { acc += (double) b.l[from + i] * b.l[from + i] + (double) b.r[from + i] * b.r[from + i]; e[i] = acc; }
    const double e0 = e[0];
    double sx = 0, sy = 0, sxx = 0, sxy = 0; int cnt = 0;
    for (size_t i = 0; i < n; i += 48)
    {
        const double d = 10.0 * std::log10 (std::max (e[i] / e0, 1.0e-30));
        if (d > -5.0) continue;
        if (d < -25.0) break;
        const double t = (double) i / sr;
        sx += t; sy += d; sxx += t * t; sxy += t * d; ++cnt;
    }
    if (cnt < 3) return 0.0;
    const double slope = (cnt * sxy - sx * sy) / (cnt * sxx - sx * sx);
    return slope < 0 ? -60.0 / slope : 1.0e9;
}

const char* name (int t) { return fxName (t); }

} // namespace

// ============================================================================ all effects

TEST (fx_all_exist)
{
    for (int t = 1; t < Fx::count; ++t) CHECK_MSG (createEffect (t) != nullptr, "%s", name (t));
}

TEST (fx_random_params_finite_bounded)
{
    for (int t = 1; t < Fx::count; ++t)
    {
        Rig rig (t);
        Rng g (1234u + (uint32_t) t);
        const size_t seg = 4800;                          // 0.1 s per parameter set
        const int sets = 200;
        Buf in = noise (seg * sets, 0.25f, 99u + (uint32_t) t);
        Buf dr = drums (seg * sets);
        for (size_t i = 0; i < in.size(); ++i) if ((i / (seg * 10)) & 1) { in.l[i] = dr.l[i]; in.r[i] = dr.r[i]; }
        std::vector<float> sets_ ((size_t) sets * kFxParams);
        for (auto& v : sets_)
        {
            const uint32_t h = g.next();
            const int k = (int) (h % 8u);
            v = k == 0 ? 0.f : (k == 1 ? 1.f : hashToFloat (g.next()));
        }
        float base[kFxParams] = {};
        rig.run (in, base, 16, [&] (int64_t pos, float* p) {
            const size_t s = std::min<size_t> ((size_t) (pos / (int64_t) seg), (size_t) sets - 1);
            std::memcpy (p, sets_.data() + s * kFxParams, sizeof (float) * kFxParams);
        });
        double worst = 0;
        const bool ok = allFinite (in, 8.0, &worst);
        CHECK_MSG (ok, "%s: non-finite or too loud (max %g)", name (t), worst);
    }
}

TEST (fx_extremes_all_zero_all_one)
{
    for (int t = 1; t < Fx::count; ++t)
        for (float v : { 0.f, 1.f })
            for (int src = 0; src < 2; ++src)
            {
                Rig rig (t);
                float p[kFxParams]; for (auto& x : p) x = v;
                Buf b = src ? drums (48000 * 3) : noise (48000 * 3, 0.25f, 5u);
                for (size_t i = 48000 * 2; i < b.size(); ++i) b.l[i] = b.r[i] = 0.f;
                rig.run (b, p, 32);
                double worst = 0;
                CHECK_MSG (allFinite (b, 8.0, &worst), "%s all=%g: non-finite or too loud (max %g)", name (t), v, worst);
            }
}

TEST (fx_silence_decays)
{
    // Denormals off for this test: a recursive path that stalls on denormals shows up as time.
#if defined(__SSE__) || defined(_M_X64)
    const unsigned int csr = _mm_getcsr();
    _mm_setcsr (csr & ~0x8040u);
#endif
    for (int t = 1; t < Fx::count; ++t)
    {
        Rig rig (t);
        float p[kFxParams]; defaults (t, p);
        if (t == Fx::Stutter || t == Fx::Reverse || t == Fx::TapeStop) p[0] = 1.f;      // engaged: buffers hold audio
        const size_t loud = 48000, maxLen = 48000 * 40;
        Buf b = noise (loud, 0.25f, 11u);
        b.l.resize (maxLen, 0.f); b.r.resize (maxLen, 0.f);
        // per-window costs; the loud part is measured as windows too, and medians are compared
        const size_t win = 4800;
        std::vector<double> loudCost, tailCost;
        for (size_t at = 0; at < loud; at += win)
        {
            const auto a0 = std::chrono::steady_clock::now();
            rig.run (b, p, 16, nullptr, at, at + win);
            loudCost.push_back (std::chrono::duration<double> (std::chrono::steady_clock::now() - a0).count());
        }
        size_t quietAt = 0;
        // engaged buffer effects keep repeating their buffer: release them after 2 s
        const bool buffer = t == Fx::Stutter || t == Fx::Reverse || t == Fx::TapeStop;
        auto fn = [&] (int64_t pos, float* q) { if (buffer && pos > (int64_t) (loud + 96000)) q[0] = 0.f; };
        size_t done = loud;
        for (size_t at = loud; at < maxLen; at += win)
        {
            const auto a0 = std::chrono::steady_clock::now();
            rig.run (b, p, 16, fn, at, at + win);
            tailCost.push_back (std::chrono::duration<double> (std::chrono::steady_clock::now() - a0).count());
            done = at + win;
            const double pk = std::max (peakAbs (b.l, at, at + win), peakAbs (b.r, at, at + win));
            if (pk < 1.0e-5) { if (! quietAt) quietAt = at; if (at > quietAt + 3 * 48000) break; }
            else quietAt = 0;
        }
        CHECK_MSG (quietAt > 0, "%s: tail did not fall below -100 dB within %g s (last peak %g)", name (t), maxLen / kSr,
                   std::max (peakAbs (b.l, done - win, done), peakAbs (b.r, done - win, done)));
        auto median = [] (std::vector<double> v) { std::sort (v.begin(), v.end()); return v.empty() ? 0.0 : v[v.size() / 2]; };
        const double mt = median (tailCost), ml = median (loudCost);
        CHECK_MSG (mt < 3.0 * ml + 2.0e-5, "%s: silent tail costs %.2f us/window vs %.2f us loud (denormals?)", name (t), mt * 1e6, ml * 1e6);
        int sub = 0;
        for (size_t i = loud; i < done; ++i)
            if ((b.l[i] != 0.f && std::fabs (b.l[i]) < 1.17549435e-38f) || (b.r[i] != 0.f && std::fabs (b.r[i]) < 1.17549435e-38f)) ++sub;
        CHECK_MSG (sub == 0, "%s: %d denormal output samples", name (t), sub);
    }
#if defined(__SSE__) || defined(_M_X64)
    _mm_setcsr (csr);
#endif
}

TEST (fx_block_split_independent)
{
    for (int t = 1; t < Fx::count; ++t)
        for (int variant = 0; variant < 2; ++variant)
        {
            float p[kFxParams]; defaults (t, p);
            if (variant == 1)
            {
                // a busier constant setting: everything moved off its default
                Rng g (500u + (uint32_t) t);
                for (int i = 0; i < kFxParams; ++i) p[i] = 0.15f + 0.7f * hashToFloat (g.next());
                if (t == Fx::Cloud) p[idx (t, "decay")] = 0.5f;
            }
            std::vector<float> ref;
            double maxDiff = 0; int worstBlock = 0;
            for (int block : { 16, 1, 7, 32 })
            {
                Rig rig (t);
                Buf b = noise (48000, 0.25f, 3u);
                Buf d = drums (48000);
                for (size_t i = 24000; i < 48000; ++i) { b.l[i] = d.l[i]; b.r[i] = d.r[i]; }
                rig.run (b, p, block);
                if (ref.empty()) { ref = b.l; ref.insert (ref.end(), b.r.begin(), b.r.end()); continue; }
                for (size_t i = 0; i < b.size(); ++i)
                {
                    const double e = std::max (std::fabs (b.l[i] - ref[i]), std::fabs (b.r[i] - ref[b.size() + i]));
                    if (e > maxDiff) { maxDiff = e; worstBlock = block; }
                }
            }
            CHECK_MSG (maxDiff < 1.0e-5, "%s variant %d: block %d differs by %g", name (t), variant, worstBlock, maxDiff);
        }
}

// ============================================================================ buffer effects

TEST (fx_buffer_effects_transparent_idle_and_after_release)
{
    for (int t : { (int) Fx::Stutter, (int) Fx::Reverse, (int) Fx::TapeStop })
        for (double onAt : { 0.5, 0.5 + 13.0 / 48000.0 })
        {
            Rig rig (t);
            float p[kFxParams]; defaults (t, p);
            Buf in = drums (96000);
            Buf n2 = noise (96000, 0.1f, 8u);
            for (size_t i = 0; i < in.size(); ++i) { in.l[i] += n2.l[i]; in.r[i] += n2.r[i]; }
            Buf b = in;
            const int64_t on0 = (int64_t) (onAt * kSr), on1 = on0 + 7200 + 5;
            rig.run (b, p, 1, [&] (int64_t pos, float* q) { q[0] = (pos >= on0 && pos < on1) ? 1.f : 0.f; });
            int bad = 0;
            for (int64_t i = 0; i < on0; ++i) if (b.l[(size_t) i] != in.l[(size_t) i] || b.r[(size_t) i] != in.r[(size_t) i]) ++bad;
            CHECK_MSG (bad == 0, "%s: %d samples altered before engage", name (t), bad);
            int changed = 0;
            for (int64_t i = on0; i < on1; ++i) if (b.l[(size_t) i] != in.l[(size_t) i]) ++changed;
            CHECK_MSG (changed > 1000, "%s: engaged but output unchanged", name (t));
            bad = 0;
            const int64_t back = on1 + (int64_t) (0.25 * kSr);
            for (int64_t i = back; i < (int64_t) in.size(); ++i) if (b.l[(size_t) i] != in.l[(size_t) i] || b.r[(size_t) i] != in.r[(size_t) i]) ++bad;
            CHECK_MSG (bad == 0, "%s: %d samples altered 250 ms after release", name (t), bad);
        }
}

TEST (fx_stutter_repeat_period)
{
    const int t = Fx::Stutter;
    for (double bpm : { 120.0, 133.0 })
        for (const char* len : { "1/16", "1/8T", "1/4" })
        {
            Rig rig (t); rig.bpm = bpm;
            float p[kFxParams]; defaults (t, p);
            float n; parseParam (fxParamDescs (t)[1], len, n); p[1] = n;
            double beats = std::strcmp (len, "1/16") == 0 ? 0.25 : (std::strcmp (len, "1/8T") == 0 ? 1.0 / 3.0 : 1.0);
            const double P = beats * 60.0 / bpm * kSr;
            Buf in = noise (48000 * 5, 0.2f, 21u);
            Buf b = in;
            const int64_t E = 24007;
            rig.run (b, p, 7, [&] (int64_t pos, float* q) { q[0] = pos >= E ? 1.f : 0.f; });
            double maxErr = 0; int checked = 0;
            double acc = P;                                  // slot starts accumulate like a sequencer grid
            for (int k = 1; k <= 4 && E + (int64_t) ((k + 1) * P) < (int64_t) b.size(); ++k, acc += P)
            {
                const int64_t b0 = E + std::llround (acc), b1 = E + std::llround (acc + P);
                for (int64_t i = b0 + 200; i < b1 - 200; ++i)
                {
                    const double e = std::fabs (b.l[(size_t) i] - in.l[(size_t) (E + (i - b0))]);
                    if (e > 1.0e-5 && maxErr <= 1.0e-5) std::printf ("    first error: repeat %d sample %d of slot\n", k, (int) (i - b0));
                    maxErr = std::max (maxErr, e); ++checked;
                }
            }
            CHECK_MSG (checked > 1000 && maxErr < 1.0e-5, "stutter %s at %g bpm: repeat differs from the captured slice by %g", len, bpm, maxErr);
        }
}

TEST (fx_reverse_plays_chunk_backwards)
{
    const int t = Fx::Reverse;
    Rig rig (t);
    float p[kFxParams]; defaults (t, p);
    Buf in = noise (48000 * 4, 0.2f, 31u);
    Buf b = in;
    const int64_t E = 48000 + 13;
    rig.run (b, p, 7, [&] (int64_t pos, float* q) { q[0] = pos >= E ? 1.f : 0.f; });
    const int64_t C = 24000;                                // 1/4 at 120 bpm
    double maxErr = 0;
    for (int64_t k = 0; k < C; ++k) maxErr = std::max (maxErr, (double) std::fabs (b.l[(size_t) (E + k)] - in.l[(size_t) (E - 1 - k)]));
    CHECK_MSG (maxErr < 1.0e-6, "first chunk is not the reversed input (err %g)", maxErr);
    maxErr = 0;
    for (int64_t k = C / 2; k < C; ++k) maxErr = std::max (maxErr, (double) std::fabs (b.l[(size_t) (E + C + k)] - in.l[(size_t) (E + C - 1 - k)]));
    CHECK_MSG (maxErr < 1.0e-6, "second chunk is not the reversed input (err %g)", maxErr);
}

TEST (fx_tapestop_stops_and_restarts)
{
    const int t = Fx::TapeStop;
    Rig rig (t);
    float p[kFxParams]; defaults (t, p);                    // 0.25 s stop
    Buf b = sine (48000 * 2, 440.0, 0.5f);
    rig.run (b, p, 16, [&] (int64_t pos, float* q) { q[0] = (pos >= 24000 && pos < 48000) ? 1.f : 0.f; });
    CHECK (rms (b.l, 24000 + 14400, 48000) < 1.0e-4);       // stopped and silent after the stop time
    CHECK (std::fabs (db (rms (b.l, 48000 + 9600, 96000)) - db (0.5 / std::sqrt (2.0))) < 0.1);
}

// ============================================================================ per effect

TEST (fx_crush_transparent_at_16_bit)
{
    const int t = Fx::Crush;
    Rig rig (t);
    float p[kFxParams]; defaults (t, p);
    Buf in = noise (48000, 0.2f, 41u);
    Buf b = in;
    rig.run (b, p);
    double e = 0;
    for (size_t i = 0; i < b.size(); ++i) e = std::max (e, (double) std::fabs (b.l[i] - in.l[i]));
    CHECK_MSG (e <= 1.0 / 32768.0, "crush at 16 bit / full rate differs by %g", e);
}

TEST (fx_distort_no_dc)
{
    const int t = Fx::Distort;
    for (int type = 0; type < 7; ++type)
        for (float bias : { 0.f, 0.6f, -0.6f })
            for (float drive : { 0.f, 12.f, 36.f })
            {
                Rig rig (t);
                float p[kFxParams]; defaults (t, p);
                setPhys (t, p, "type", (float) type); setPhys (t, p, "bias", bias); setPhys (t, p, "drive", drive);
                Buf b = sine (48000 * 3, 110.0, 0.5f);
                rig.run (b, p);
                double m = 0; for (size_t i = 96000; i < b.size(); ++i) m += b.l[i];
                m /= (double) (b.size() - 96000);
                CHECK_MSG (std::fabs (m) < 2.0e-3, "distort type %d bias %g drive %g: DC %g", type, bias, drive, m);
            }
}

TEST (fx_filter_magnitude_at_cutoff)
{
    const int t = Fx::Filter;
    // type, expected dB at the cutoff with resonance 0 (Peak with full resonance)
    struct C { int type; double res, want, tol; } cases[] = {
        { 0, 0.0, -3.01, 0.5 },   // LP12 Butterworth
        { 1, 0.0, -12.0, 1.0 },   // LP24 ladder (four -3 dB poles)
        { 2, 0.0, 0.0, 0.5 },     // BP unity at the centre
        { 3, 0.0, -3.01, 0.5 },   // HP12
        { 4, 0.0, -3.01, 0.5 },   // HP24 Butterworth pair
        { 5, 0.0, -40.0, 0.0 },   // Notch: below -40
        { 6, 1.0, 18.0, 1.0 },    // Peak +18 dB at full resonance
    };
    for (double fc : { 1000.0, 5000.0 })
        for (auto& cs : cases)
        {
            Rig rig (t);
            float p[kFxParams]; defaults (t, p);
            setPhys (t, p, "type", (float) cs.type); p[idx (t, "res")] = (float) cs.res; setPhys (t, p, "cutoff", (float) fc);
            const float a = 0.05f;
            Buf b = sine (48000, fc, a);
            rig.run (b, p);
            const double g = db (toneAmp (b.l, fc, 24000, 48000) / a);
            if (cs.type == 5) CHECK_MSG (g < -40.0, "filter notch at %g Hz: %g dB", fc, g);
            else CHECK_MSG (std::fabs (g - cs.want) <= cs.tol, "filter type %d at %g Hz: %g dB, want %g", cs.type, fc, g, cs.want);
        }
    // slopes an octave into the stop band: LP12 about -12 dB, LP24 at least -24 dB
    for (int type : { 0, 1, 3, 4 })
    {
        Rig rig (t);
        float p[kFxParams]; defaults (t, p);
        setPhys (t, p, "type", (float) type); p[idx (t, "res")] = 0.f; setPhys (t, p, "cutoff", 1000.f);
        const double f = (type == 0 || type == 1) ? 4000.0 : 250.0;
        Buf b = sine (48000, f, 0.05f);
        rig.run (b, p);
        const double g = db (toneAmp (b.l, f, 24000, 48000) / 0.05);
        const double want = (type == 0 || type == 3) ? -24.0 : -48.0;     // two octaves away
        CHECK_MSG (g < want + 3.0, "filter type %d two octaves out: %g dB (want about %g)", type, g, want);
    }
}

TEST (fx_ringmod_sum_and_difference)
{
    const int t = Fx::RingMod;
    Rig rig (t);
    float p[kFxParams]; defaults (t, p);
    setPhys (t, p, "freq", 220.f);
    Buf b = sine (48000, 1000.0, 0.5f);
    rig.run (b, p);
    const double s = toneAmp (b.l, 1220.0, 12000, 48000), d = toneAmp (b.l, 780.0, 12000, 48000);
    const double in = toneAmp (b.l, 1000.0, 12000, 48000), car = toneAmp (b.l, 220.0, 12000, 48000);
    // sidebands of 0.25 each, trimmed by the sine carrier's rms (+3 dB) so the level matches the input
    const double want = 0.25 * std::sqrt (2.0);
    CHECK_MSG (std::fabs (db (s / want)) < 1.0 && std::fabs (db (d / want)) < 1.0, "ring mod sidebands %g / %g dB", db (s), db (d));
    CHECK_MSG (db (in / s) < -40.0 && db (car / s) < -40.0, "ring mod leaks input %g dB carrier %g dB", db (in / s), db (car / s));
    double m = 0; for (size_t i = 12000; i < b.size(); ++i) m += b.l[i];
    CHECK (std::fabs (m / 36000.0) < 1.0e-3);
}

TEST (fx_spectral_neutral_is_delayed_input)
{
    const int t = Fx::Spectral;
    Rig rig (t);
    float p[kFxParams]; defaults (t, p);
    p[idx (t, "blur")] = 0.f;
    Buf in = noise (48000 * 2, 0.2f, 51u);
    Buf d = drums (48000 * 2);
    for (size_t i = 48000; i < in.size(); ++i) { in.l[i] = d.l[i]; in.r[i] = d.r[i] * 0.5f; }
    Buf b = in;
    rig.run (b, p);
    const int L = 2048;
    double e = 0, s = 0;
    for (size_t i = 8192; i < b.size(); ++i)
    {
        const double dl = b.l[i] - in.l[i - L], dr = b.r[i] - in.r[i - L];
        e += dl * dl + dr * dr; s += (double) in.l[i - L] * in.l[i - L] + (double) in.r[i - L] * in.r[i - L];
    }
    CHECK_MSG (10.0 * std::log10 (e / s) < -60.0, "spectral neutral residual %g dB", 10.0 * std::log10 (e / s));
    // the default (blur 0.5) keeps the level near the input on drums
    Rig r2 (t); defaults (t, p);
    Buf b2 = drums (48000 * 4); const double inR = rms (b2.l);
    r2.run (b2, p);
    CHECK_MSG (std::fabs (db (rms (b2.l) / inR)) < 4.0, "spectral default level %+.1f dB", db (rms (b2.l) / inR));
}

TEST (fx_freqshift_single_sideband)
{
    const int t = Fx::FreqShift;
    for (double hz : { 100.0, -100.0, 1000.0 })
    {
        Rig rig (t);
        float p[kFxParams]; defaults (t, p);
        const double x = std::log10 (1.0 + std::fabs (hz) * 999.0 / 5000.0) / 3.0;
        setPhys (t, p, "shift", (float) (hz < 0 ? -x : x));
        Buf b = sine (48000, 1000.0, 0.5f);
        rig.run (b, p);
        const double want = toneAmp (b.l, 1000.0 + hz, 12000, 48000), image = toneAmp (b.l, 1000.0 - hz, 12000, 48000);
        CHECK_MSG (std::fabs (db (want / 0.5)) < 1.0, "freq shift %+g Hz: wanted sideband %g dB", hz, db (want / 0.5));
        CHECK_MSG (db (image / want) < -35.0, "freq shift %+g Hz: image only %g dB down", hz, db (image / want));
        const double pf = peakFreq (b.l, 1000.0 + hz, 20.0, 12000, 48000);
        CHECK_MSG (std::fabs (pf - (1000.0 + hz)) < 0.5, "freq shift %+g Hz: peak at %g Hz", hz, pf);
    }
}

TEST (fx_compressor_static_curve)
{
    const int t = Fx::Compressor;
    struct C { float thr, ratio, knee, inDb; } cases[] = {
        { -20.f, 4.f, 0.f, -4.f }, { -20.f, 4.f, 6.f, -4.f }, { -30.f, 2.f, 0.f, -6.f }, { -20.f, 4.f, 0.f, -30.f }, { -10.f, 20.f, 0.f, 0.f },
    };
    for (auto& cs : cases)
    {
        Rig rig (t);
        float p[kFxParams]; defaults (t, p);
        setPhys (t, p, "thresh", cs.thr); setPhys (t, p, "ratio", cs.ratio); setPhys (t, p, "knee", cs.knee);
        setPhys (t, p, "attack", 0.002f); setPhys (t, p, "release", 0.05f);
        setPhys (t, p, "makeup", 0.f);
        const float a = dbToGain (cs.inDb);
        Buf b = sine (48000, 1000.0, a);
        rig.run (b, p);
        const double out = db (peakAbs (b.l, 24000, 48000));
        const double over = cs.inDb - cs.thr;
        const double want = over > 0 ? cs.thr + over / cs.ratio : cs.inDb;
        CHECK_MSG (std::fabs (out - want) < 1.0, "compressor thr %g ratio %g in %g: out %g dB want %g", cs.thr, cs.ratio, cs.inDb, out, want);
    }
}

TEST (fx_delay_echo_position)
{
    const int t = Fx::Delay;
    for (int sync = 0; sync < 2; ++sync)
        for (float width : { 0.f, 0.7f })
            for (double bpm : { 120.0, 97.0 })
            {
                Rig rig (t); rig.bpm = bpm;
                float p[kFxParams]; defaults (t, p);
                p[idx (t, "sync")] = sync ? 1.f : 0.f;
                double sec;
                if (sync) { p[idx (t, "time")] = (5.f + 0.5f) / 15.f; sec = 60.0 / bpm; }            // 1/4
                else { sec = 0.3; p[idx (t, "time")] = (float) (std::log (sec / 0.001) / std::log (2000.0)); }
                p[idx (t, "feedback")] = 0.f; p[idx (t, "moddepth")] = 0.f;
                setPhys (t, p, "width", width);
                Buf b (48000 * 2);
                const size_t at = 4800;
                b.l[at] = b.r[at] = 0.5f;
                rig.run (b, p);
                size_t best = 0; double bv = 0;
                for (size_t i = at + 10; i < b.size(); ++i) { const double v = std::fabs (b.l[i]) + std::fabs (b.r[i]); if (v > bv) { bv = v; best = i; } }
                const double want = (double) at + sec * kSr;
                CHECK_MSG (std::fabs ((double) best - want) <= 2.0, "delay sync %d width %g bpm %g: echo at %d, want %g", sync, width, bpm, (int) best, want);
            }
}

TEST (fx_delay_feedback_above_one_stays_bounded)
{
    const int t = Fx::Delay;
    Rig rig (t);
    float p[kFxParams]; defaults (t, p);
    p[idx (t, "feedback")] = 1.f;
    Buf b = drums (48000 * 20);
    for (size_t i = 48000; i < b.size(); ++i) b.l[i] = b.r[i] = 0.f;
    rig.run (b, p);
    double worst = 0;
    CHECK_MSG (allFinite (b, 3.0, &worst), "delay feedback 1.05: max %g", worst);
    const double late = peakAbs (b.l, 48000 * 15), mid = peakAbs (b.l, 48000 * 5, 48000 * 10);
    CHECK_MSG (late < 1.5 * mid + 1.0e-3, "delay feedback 1.05 still growing: %g -> %g", mid, late);
}

TEST (fx_reverb_rt60)
{
    struct C { int type; float decay; } cases[] = { { Fx::Plate, 1.0f }, { Fx::Plate, 4.0f }, { Fx::Cloud, 2.0f }, { Fx::Cloud, 6.0f } };
    for (auto& cs : cases)
    {
        Rig rig (cs.type);
        float p[kFxParams]; defaults (cs.type, p);
        setPhys (cs.type, p, "decay", cs.decay);
        setPhys (cs.type, p, "predelay", 0.f);
        const size_t burst = 24000;
        Buf b = noise (burst, 0.25f, 61u);
        b.l.resize ((size_t) (burst + kSr * cs.decay * 1.5), 0.f); b.r.resize (b.l.size(), 0.f);
        rig.run (b, p);
        const double r = rt60 (b, burst);
        if (std::getenv ("FX_VERBOSE")) std::printf ("    %s decay %g s -> RT60 %.2f s\n", name (cs.type), cs.decay, r);
        CHECK_MSG (std::fabs (r / cs.decay - 1.0) < 0.3, "%s decay %g s: measured RT60 %g s", name (cs.type), cs.decay, r);
    }
}

TEST (fx_cloud_freeze_holds)
{
    const int t = Fx::Cloud;
    Rig rig (t);
    float p[kFxParams]; defaults (t, p);
    p[idx (t, "decay")] = 1.f;
    Buf b = noise (48000, 0.25f, 71u);
    b.l.resize (48000 * 12, 0.f); b.r.resize (b.l.size(), 0.f);
    rig.run (b, p);
    const double ref = db (rms (b.l, 48000 * 2, 48000 * 3) + rms (b.r, 48000 * 2, 48000 * 3));
    for (int s = 3; s < 12; ++s)
    {
        const double v = db (rms (b.l, 48000 * s, 48000 * (s + 1)) + rms (b.r, 48000 * s, 48000 * (s + 1)));
        CHECK_MSG (std::fabs (v - ref) < 1.5, "cloud freeze: second %d at %+.2f dB vs second 2", s, v - ref);
    }
    CHECK (ref > -40.0);
}

TEST (fx_comb_and_resonator_pitch)
{
    for (double note : { 57.0, 81.0, 40.0 })
        for (float damp : { 0.f, 0.3f, 0.7f })
        {
            {
                const int t = Fx::Comb;
                Rig rig (t);
                float p[kFxParams]; defaults (t, p);
                setPhys (t, p, "note", (float) note); setPhys (t, p, "feedback", 0.95f);
                p[idx (t, "damp")] = damp; p[idx (t, "spread")] = 0.f; p[idx (t, "moddepth")] = 0.f;
                Buf b (48000 * 2); b.l[100] = b.r[100] = 0.5f;
                rig.run (b, p);
                const double f0 = midiToHz (note);
                const double f = peakFreq (b.l, f0, 60.0, 2000, 2000 + 48000);
                CHECK_MSG (std::fabs (cents (f, f0)) < 10.0, "comb note %g damp %g: %g Hz = %+.1f cents", note, damp, f, cents (f, f0));
                // negative feedback: the octave-down comb (lowest resonance f/2)
                Rig r2 (t);
                setPhys (t, p, "feedback", -0.95f);
                Buf b2 (48000 * 2); b2.l[100] = b2.r[100] = 0.5f;
                r2.run (b2, p);
                const double fn = peakFreq (b2.l, f0 * 0.5, 60.0, 2000, 2000 + 48000);
                CHECK_MSG (std::fabs (cents (fn, f0 * 0.5)) < 10.0, "comb -fb note %g damp %g: %g Hz = %+.1f cents", note, damp, fn, cents (fn, f0 * 0.5));
            }
            {
                const int t = Fx::Resonator;
                Rig rig (t);
                float p[kFxParams]; defaults (t, p);
                setPhys (t, p, "note", (float) note); setPhys (t, p, "chord", 0.f); setPhys (t, p, "count", 1.f);
                p[idx (t, "damp")] = damp; p[idx (t, "detune")] = 0.f;
                Buf b (48000 * 2); b.l[100] = b.r[100] = 0.5f;
                rig.run (b, p);
                const double f0 = midiToHz (note);
                const double f = peakFreq (b.l, f0, 60.0, 2000, 2000 + 48000);
                CHECK_MSG (std::fabs (cents (f, f0)) < 10.0, "resonator note %g damp %g: %g Hz = %+.1f cents", note, damp, f, cents (f, f0));
            }
        }
}

TEST (fx_default_loudness)
{
    for (int t = 1; t < Fx::count; ++t)
    {
        Rig rig (t);
        float p[kFxParams]; defaults (t, p);
        Buf b = drums (48000 * 4);
        const double in = rms (b.l) + rms (b.r);
        rig.run (b, p);
        const double g = db ((rms (b.l) + rms (b.r)) / in);
        if (std::getenv ("FX_VERBOSE")) std::printf ("    %-11s default level %+.1f dB\n", name (t), g);
        const bool space = t == Fx::Delay || t == Fx::Plate || t == Fx::Cloud || t == Fx::GrainDelay;
        CHECK_MSG (g < 4.0 && g > (space ? -10.0 : -4.0), "%s default level %+.1f dB", name (t), g);
    }
}

TEST (fx_format_custom)
{
    char buf[40];
    float p[kFxParams];
    auto fmt = [&] (int type, int i) { buf[0] = 0; const bool ok = formatFxParamCustom (type, i, p, buf, (int) sizeof (buf)); return ok ? std::string (buf) : std::string ("<default>"); };

    defaults (Fx::Delay, p);
    p[0] = 5.5f / 15.f;                                         CHECK (fmt (Fx::Delay, 0) == "1/4");
    p[0] = 0.f;                                                 CHECK (fmt (Fx::Delay, 0) == "1/1");
    p[0] = 1.f;                                                 CHECK (fmt (Fx::Delay, 0) == "1/64");
    p[1] = 0.f; p[0] = (float) (std::log (300.0) / std::log (2000.0));
    CHECK_MSG (fmt (Fx::Delay, 0) == "300 ms", "%s", buf);
    p[0] = 1.f;                                                 CHECK_MSG (fmt (Fx::Delay, 0) == "2.00 s", "%s", buf);
    p[0] = 0.f;                                                 CHECK_MSG (fmt (Fx::Delay, 0) == "1.0 ms", "%s", buf);
    CHECK (fmt (Fx::Delay, 2) == "<default>");

    defaults (Fx::FreqShift, p);
    setPhys (Fx::FreqShift, p, "shift", (float) (std::log10 (1.0 + 100.0 * 999.0 / 5000.0) / 3.0));
    CHECK_MSG (fmt (Fx::FreqShift, 0) == "+100 Hz", "%s", buf);
    p[0] = 0.f;                                                 CHECK_MSG (fmt (Fx::FreqShift, 0) == "-5.00 kHz", "%s", buf);
    p[0] = 0.5f;                                                CHECK_MSG (fmt (Fx::FreqShift, 0) == "0 Hz", "%s", buf);

    defaults (Fx::TapeStop, p);
    CHECK (fmt (Fx::TapeStop, 1) == "<default>");
    p[4] = 1.f; p[1] = 0.5f;                                    CHECK_MSG (fmt (Fx::TapeStop, 1) == "1/4", "%s", buf);
    p[1] = 1.f;                                                 CHECK_MSG (fmt (Fx::TapeStop, 1) == "1 bar", "%s", buf);

    defaults (Fx::Spectral, p);
    p[2] = 1.f;                                                 CHECK_MSG (fmt (Fx::Spectral, 2) == "+3.00 kHz", "%s", buf);
    p[2] = 0.25f;                                               CHECK_MSG (fmt (Fx::Spectral, 2) == "-750 Hz", "%s", buf);
    CHECK (fmt (Fx::Filter, 1) == "<default>");
}

TEST (fx_sample_rates)
{
    for (int t = 1; t < Fx::count; ++t)
    {
        double ref = 0;
        for (double sr : { 48000.0, 44100.0, 96000.0, 192000.0 })
        {
            // defaults: level close to the 48 kHz level
            Rig rig (t, sr);
            float p[kFxParams]; defaults (t, p);
            Buf b = drums ((size_t) (sr * 3), sr);
            const double in = rms (b.l);
            rig.run (b, p, 32);
            const double g = db (rms (b.l) / in);
            if (sr == 48000.0) ref = g;
            else CHECK_MSG (std::fabs (g - ref) < 1.5, "%s at %g Hz: default level %+.1f dB vs %+.1f dB at 48 kHz", name (t), sr, g, ref);
            // random settings stay finite and bounded
            Rig r2 (t, sr);
            Rng rg (77u + (uint32_t) t);
            const size_t seg = (size_t) (sr * 0.1);
            Buf nb = noise (seg * 30, 0.25f, 9u);
            std::vector<float> sets (30 * kFxParams);
            for (auto& v : sets) { const uint32_t h = rg.next() % 6u; v = h == 0 ? 0.f : (h == 1 ? 1.f : rg.uni()); }
            float base[kFxParams] = {};
            r2.run (nb, base, 32, [&] (int64_t pos, float* q) {
                std::memcpy (q, sets.data() + std::min<size_t> ((size_t) pos / seg, 29) * kFxParams, sizeof (float) * kFxParams);
            });
            double worst = 0;
            CHECK_MSG (allFinite (nb, 8.0, &worst), "%s at %g Hz: random settings max %g", name (t), sr, worst);
        }
    }
}
