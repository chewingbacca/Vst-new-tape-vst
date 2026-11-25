// Sampler and granular engines, plus the transient detector they slice with.
#include "TestFramework.h"
#include "voices/EngineBase.h"
#include "model/ParamTables.h"
#include <cstring>
#include <memory>
#include <vector>
using namespace atm;

namespace {

using Sample = std::unique_ptr<SampleData>;

Sample makeSample (const std::vector<float>& l, const std::vector<float>* r = nullptr, double sr = 44100.0)
{
    const float* ptrs[2] = { l.data(), r ? r->data() : l.data() };
    return Sample (makeSampleData (ptrs, r ? 2 : 1, (int) l.size(), sr, "test"));
}

std::vector<float> sineWave (int frames, double periodFrames, float amp = 0.8f)
{
    std::vector<float> x ((size_t) frames);
    for (int i = 0; i < frames; ++i) x[(size_t) i] = amp * (float) std::sin (kTau * i / periodFrames);
    return x;
}

std::vector<float> noiseWave (int frames, uint32_t seed, float amp = 0.3f)
{
    Noise nz; nz.seed (seed);
    std::vector<float> x ((size_t) frames);
    for (auto& v : x) v = amp * nz.next();
    return x;
}

// Drum-like hits at known frames: decaying sine "kicks" and noise bursts of different levels.
std::vector<float> hitsWave (int frames, const std::vector<int>& onsets, double sr = 44100.0)
{
    std::vector<float> x ((size_t) frames, 0.f);
    Noise nz; nz.seed (5);
    for (size_t k = 0; k < onsets.size(); ++k)
        for (int i = onsets[k]; i < frames; ++i)
        {
            const double t = (i - onsets[k]) / sr;
            if (k % 3 == 0)      x[(size_t) i] += (float) (0.8 * std::sin (kTau * (55.0 * t + 3.0 * (1.0 - std::exp (-t * 40.0)))) * std::exp (-t * 12.0));
            else if (k % 3 == 1) x[(size_t) i] += 0.4f * nz.next() * (float) std::exp (-t * 30.0);
            else                 x[(size_t) i] += 0.15f * nz.next() * (float) std::exp (-t * 120.0);
        }
    return x;
}

struct Params
{
    float e[kEngineParams];
    const ParamDesc* d;
    explicit Params (const ParamDesc* descs) : d (descs)
    {
        for (int i = 0; i < kEngineParams; ++i) e[i] = d[i].key[0] ? d[i].defNorm() : 0.f;
    }
    Params& set (const char* key, float phys)
    {
        for (int i = 0; i < kEngineParams; ++i)
            if (std::strcmp (d[i].key, key) == 0) { e[i] = d[i].toNorm (phys); return *this; }
        CHECK_MSG (false, "unknown parameter %s", key);
        return *this;
    }
};

struct Out { std::vector<float> l, r; };

// Drives an engine the way a voice does: control() before every render() slice.
struct Rig
{
    std::unique_ptr<EngineBase> eng;
    float note = 60.f;
    Rig (int type, double sr, const SampleData* s)
    {
        eng = createEngine (type);
        eng->prepare (sr);
        eng->setSample (s);
    }
    void on (const Params& p, float n = 60.f, uint32_t seed = 1, bool legato = false)
    {
        note = n;
        EngineNote en; en.note = n; en.seed = seed; en.legato = legato;
        eng->noteOn (en, p.e);
    }
    // block > 0: fixed slice size; block == 0: a fixed irregular sequence of sizes 1..32
    void render (Out& o, const Params& p, int frames, int block = 16)
    {
        float bl[kMaxSlice], br[kMaxSlice];
        uint32_t rnd = 777;
        for (int done = 0; done < frames;)
        {
            int n = block;
            if (block <= 0) { rnd = rnd * 1664525u + 1013904223u; n = 1 + (int) ((rnd >> 16) % 32); }
            n = std::min (n, frames - done);
            eng->control (p.e, note);
            eng->render (bl, br, n);
            const bool st = eng->isStereo();
            for (int i = 0; i < n; ++i) { o.l.push_back (bl[i]); o.r.push_back (st ? br[i] : bl[i]); }
            done += n;
        }
    }
};

double zeroCrossingHz (const std::vector<float>& x, int from, int to, double sr)
{
    double first = -1, lastZ = -1; int count = 0;
    for (int i = from; i + 1 < to; ++i)
        if (x[(size_t) i] < 0.f && x[(size_t) i + 1] >= 0.f)
        {
            const double z = i + (double) -x[(size_t) i] / ((double) x[(size_t) i + 1] - x[(size_t) i]);
            if (first < 0) first = z;
            lastZ = z; ++count;
        }
    return count > 1 ? (count - 1) / (lastZ - first) * sr : 0.0;
}

int lastNonZero (const std::vector<float>& x)
{
    for (int i = (int) x.size() - 1; i >= 0; --i) if (x[(size_t) i] != 0.f) return i;
    return -1;
}

double maxAbs (const std::vector<float>& x, size_t from = 0, size_t to = (size_t) -1)
{
    double m = 0; to = std::min (to, x.size());
    for (size_t i = from; i < to; ++i) m = std::max (m, (double) std::fabs (x[i]));
    return m;
}

double rms (const std::vector<float>& x, size_t from, size_t to)
{
    double a = 0; to = std::min (to, x.size());
    for (size_t i = from; i < to; ++i) a += (double) x[i] * x[i];
    return std::sqrt (a / (double) std::max<size_t> (1, to - from));
}

double maxStep (const std::vector<float>& x, size_t from, size_t to)
{
    double m = 0; to = std::min (to, x.size());
    for (size_t i = from + 1; i < to; ++i) m = std::max (m, (double) std::fabs (x[i] - x[i - 1]));
    return m;
}

bool allFinite (const Out& o, double limit)
{
    for (size_t i = 0; i < o.l.size(); ++i)
        if (! std::isfinite (o.l[i]) || ! std::isfinite (o.r[i]) || std::fabs (o.l[i]) > limit || std::fabs (o.r[i]) > limit) return false;
    return true;
}

double maxDiff (const Out& a, const Out& b)
{
    double m = a.l.size() == b.l.size() ? 0.0 : 1.0e9;
    for (size_t i = 0; i < std::min (a.l.size(), b.l.size()); ++i)
        m = std::max (m, std::max ((double) std::fabs (a.l[i] - b.l[i]), (double) std::fabs (a.r[i] - b.r[i])));
    return m;
}

const float kUnity = 0.7071f;      // both engines play a full-scale sample at -3 dB

} // namespace

// ---------------------------------------------------------------------------------------- sampler

TEST (sampler_pitch_follows_note_at_every_sample_rate)
{
    const auto smp = makeSample (sineWave (44100, 100.0));          // 441 Hz at its root note
    for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (float note : { 36.f, 48.f, 60.f, 67.f, 72.f, 79.f, 84.5f })
        {
            Rig rig (Engine::Sampler, sr, smp.get());
            Params p (kSamplerParams);
            p.set ("loop", 1).set ("xfade", 0.f);
            rig.on (p, note);
            Out o; rig.render (o, p, (int) (sr * 0.3));
            const double want = 441.0 * std::exp2 ((note - 60.0) / 12.0);
            const double got = zeroCrossingHz (o.l, (int) (sr * 0.02), (int) (sr * 0.3), sr);
            CHECK_MSG (std::fabs (got / want - 1.0) < 0.0015, "sr %g note %g: %g Hz, expected %g", sr, note, got, want);
        }
}

TEST (sampler_keytrack_off_ignores_the_note)
{
    const auto smp = makeSample (sineWave (44100, 100.0));
    Rig rig (Engine::Sampler, 48000.0, smp.get());
    Params p (kSamplerParams);
    p.set ("keytrack", 0);
    rig.on (p, 79.f);
    Out o; rig.render (o, p, 12000);
    CHECK_NEAR (zeroCrossingHz (o.l, 1000, 12000, 48000.0), 441.0, 0.5);
}

TEST (sampler_one_shot_length_and_finished)
{
    const auto smp = makeSample (sineWave (22050, 100.0));
    struct Case { double sr; float note; float length; double expect; };
    for (const Case& c : { Case { 44100.0, 60.f, 1.f, 22050.0 }, Case { 48000.0, 60.f, 1.f, 24000.0 }, Case { 48000.0, 72.f, 1.f, 12000.0 },
                           Case { 96000.0, 48.f, 0.5f, 48000.0 }, Case { 44100.0, 60.f, 0.25f, 5512.5 } })
    {
        Rig rig (Engine::Sampler, c.sr, smp.get());
        Params p (kSamplerParams);
        p.set ("length", c.length);
        rig.on (p, c.note);
        CHECK (! rig.eng->finished());
        Out o; rig.render (o, p, (int) (c.expect * 1.5));
        const int end = lastNonZero (o.l) + 1;
        CHECK_MSG (std::fabs (end - c.expect) <= 3.0, "sr %g note %g length %g: ended at %d, expected %g", c.sr, c.note, c.length, end, c.expect);
        CHECK (rig.eng->finished());
    }
}

TEST (sampler_grid_slices_start_where_they_should)
{
    // a ramp: the value read tells which frame was played
    const int len = 32000;
    std::vector<float> ramp ((size_t) len);
    for (int i = 0; i < len; ++i) ramp[(size_t) i] = 0.9f * (float) i / (float) len;
    const auto smp = makeSample (ramp);
    for (int slices : { 1, 8, 16, 64 })
        for (int slice : { 0, 3, 7, 63 })
        {
            Rig rig (Engine::Sampler, 44100.0, smp.get());
            Params p (kSamplerParams);
            p.set ("mode", 2).set ("slices", (float) slices).set ("slice", (float) slice).set ("xfade", 0.f);
            rig.on (p);
            const int k = std::min (slice, slices - 1);
            const double start = (double) len * k / slices, sliceLen = (double) len / slices;
            Out o; rig.render (o, p, (int) sliceLen + 200);
            for (int n : { 100, 200, (int) sliceLen - 100 })
                CHECK_MSG (std::fabs (o.l[(size_t) n] / kUnity - 0.9 * (start + n) / len) < 2.0e-5, "slices %d slice %d: wrong frame at output sample %d", slices, slice, n);
            CHECK_MSG (std::abs (lastNonZero (o.l) + 1 - (int) sliceLen) <= 2, "slices %d slice %d: length %d", slices, slice, lastNonZero (o.l) + 1);
            CHECK (rig.eng->finished());

            // start / length apply inside the slice
            Rig rig2 (Engine::Sampler, 44100.0, smp.get());
            p.set ("start", 0.5f).set ("length", 0.5f);
            rig2.on (p);
            Out o2; rig2.render (o2, p, (int) sliceLen);
            CHECK (std::fabs (o2.l[60] / kUnity - 0.9 * (start + 0.5 * sliceLen + 60) / len) < 2.0e-5);
            CHECK (std::abs (lastNonZero (o2.l) + 1 - (int) (sliceLen * 0.25)) <= 2);
        }
}

TEST (transients_are_found_within_a_millisecond)
{
    for (double sr : { 44100.0, 96000.0 })
    {
        std::vector<int> onsets;
        for (double t : { 0.0, 0.18, 0.36, 0.45, 0.54, 0.72, 0.81, 0.99, 1.08, 1.26, 1.44, 1.53 }) onsets.push_back ((int) (t * sr));
        const auto smp = makeSample (hitsWave ((int) (1.8 * sr), onsets, sr), nullptr, sr);
        const auto& tr = smp->transients;
        CHECK_MSG (tr.size() == onsets.size(), "sr %g: %d transients, expected %d", sr, (int) tr.size(), (int) onsets.size());
        CHECK (! tr.empty() && tr[0] == 0);
        for (size_t i = 0; i < std::min (tr.size(), onsets.size()); ++i)
        {
            const double errMs = (tr[i] - onsets[i]) * 1000.0 / sr;
            CHECK_MSG (errMs <= 0.3 && errMs >= -1.5, "sr %g onset %d: %d vs %d (%.2f ms)", sr, (int) i, tr[i], onsets[i], errMs);
        }
    }
    // nothing but the start in steady material
    CHECK (makeSample (noiseWave (88200, 3))->transients.size() == 1);
    CHECK (makeSample (sineWave (88200, 1102.5))->transients.size() == 1);          // 40 Hz
    std::vector<float> chord (88200);
    for (size_t i = 0; i < chord.size(); ++i)
        for (double hz : { 130.8, 164.8, 196.0, 246.9 }) chord[i] += 0.2f * (float) std::sin (kTau * hz * (double) i / 44100.0);
    CHECK (makeSample (chord)->transients.size() == 1);
}

TEST (sampler_transient_slices_follow_the_detected_onsets)
{
    std::vector<int> onsets;
    for (double t : { 0.0, 0.2, 0.33, 0.5, 0.71, 0.9 }) onsets.push_back ((int) (t * 44100.0));
    const std::vector<float> wave = hitsWave (52920, onsets);
    const auto smp = makeSample (wave);
    const auto& tr = smp->transients;
    CHECK (tr.size() == onsets.size());
    if (tr.size() < 3) return;
    for (int slice : { 0, 1, 2, (int) tr.size() - 1, 63 })
    {
        Rig rig (Engine::Sampler, 44100.0, smp.get());
        Params p (kSamplerParams);
        p.set ("mode", 2).set ("sliceby", 1).set ("slice", (float) slice).set ("xfade", 0.f);
        rig.on (p);
        const int k = std::min (slice, (int) tr.size() - 1);
        const int start = tr[(size_t) k], end = k + 1 < (int) tr.size() ? tr[(size_t) k + 1] : smp->length;
        Out o; rig.render (o, p, end - start + 100);
        double err = 0;
        for (int n = 40; n < end - start - 60; ++n) err = std::max (err, (double) std::fabs (o.l[(size_t) n] - kUnity * wave[(size_t) (start + n)]));
        CHECK_MSG (err < 1.0e-5, "slice %d does not play frames %d..%d (max error %g)", slice, start, end, err);
        CHECK (std::abs (lastNonZero (o.l) + 1 - (end - start)) <= 2);
    }
}

TEST (sampler_slice_edges_are_faded)
{
    // a constant: any step at a region edge would be as large as the signal itself
    const auto smp = makeSample (std::vector<float> (20000, 0.8f));
    for (double sr : { 44100.0, 96000.0 })
        for (float note : { 48.f, 60.f, 84.f })
        {
            Rig rig (Engine::Sampler, sr, smp.get());
            Params p (kSamplerParams);
            p.set ("mode", 2).set ("slices", 8).set ("slice", 3).set ("xfade", 0.f);
            rig.on (p, note);
            Out o; rig.render (o, p, (int) (sr * 0.5), 0);
            CHECK (rig.eng->finished());
            CHECK (std::fabs (o.l[0]) < 0.05f);
            const double biggest = maxStep (o.l, 0, o.l.size());
            // 0.5 ms raised-cosine in, 1 ms out: the steepest step is pi/2 * level / fade samples
            CHECK_MSG (biggest < 1.6 * 0.8 * kUnity / (0.0005 * sr) * 1.1, "sr %g note %g: step %g", sr, note, biggest);
            CHECK (maxAbs (o.l) > 0.8 * kUnity * 0.99);
        }
}

TEST (sampler_reverse_is_the_forward_region_backwards)
{
    const std::vector<float> wave = noiseWave (20000, 11, 0.5f);
    const auto smp = makeSample (wave);
    for (int mode = 0; mode < 2; ++mode)
    {
        Params p (kSamplerParams);
        p.set ("xfade", 0.f);
        if (mode == 1) p.set ("mode", 2).set ("slices", 4).set ("slice", 2);
        else           p.set ("start", 0.25f).set ("length", 0.4f);
        Rig fwd (Engine::Sampler, 44100.0, smp.get()), rev (Engine::Sampler, 44100.0, smp.get());
        fwd.on (p);
        Out a; fwd.render (a, p, 8000);
        p.set ("reverse", 1);
        rev.on (p);
        Out b; rev.render (b, p, 8000, 7);
        const int len = lastNonZero (a.l) + 1;
        CHECK (len == lastNonZero (b.l) + 1);
        CHECK (len == (mode == 1 ? 5000 : 6000));
        double err = 0;
        for (int n = 60; n < len - 60; ++n) err = std::max (err, (double) std::fabs (b.l[(size_t) n] - a.l[(size_t) (len - 1 - n)]));
        CHECK_MSG (err < 1.0e-6, "mode %d: reverse differs from mirrored forward by %g", mode, err);
    }
}

TEST (sampler_forward_loop_has_no_step_at_the_seam)
{
    // 80 periods of 100 frames; the region holds a whole number of periods, so a correct loop
    // (with or without crossfade) is an unbroken sine
    const auto smp = makeSample (sineWave (8000, 100.0, 0.8f));
    for (float xfade : { 0.f, 0.1f, 0.5f })
        for (int reverse = 0; reverse < 2; ++reverse)
        {
            Rig rig (Engine::Sampler, 44100.0, smp.get());
            Params p (kSamplerParams);
            p.set ("start", 0.25f).set ("length", 1.f / 3.f).set ("loop", 1).set ("xfade", xfade).set ("reverse", (float) reverse);
            rig.on (p);
            Out o; rig.render (o, p, 30000, 0);                       // region = 2000 frames: many passes
            CHECK (! rig.eng->finished());
            double err = 0;
            const double sign = reverse ? -1.0 : 1.0, phase0 = reverse ? -1.0 : 0.0;     // reverse starts on the last frame
            for (int n = 100; n < 30000; ++n)
                err = std::max (err, std::fabs (o.l[(size_t) n] - kUnity * 0.8 * sign * std::sin (kTau * (n + sign * phase0) / 100.0)));
            CHECK_MSG (err < 2.0e-3, "xfade %g reverse %d: loop deviates from a continuous sine by %g", xfade, reverse, err);
        }
}

TEST (sampler_loop_crossfade_keeps_the_level_of_noise)
{
    const auto smp = makeSample (noiseWave (44100, 21, 0.3f));
    Rig rig (Engine::Sampler, 44100.0, smp.get());
    Params p (kSamplerParams);
    p.set ("loop", 1).set ("xfade", 0.5f);                            // half of every pass is crossfade
    rig.on (p);
    Out o; rig.render (o, p, 44100 * 3);
    const double ref = kUnity * 0.3 / std::sqrt (3.0);                // rms of uniform noise
    for (int k = 2; k < 12; ++k)
    {
        const double r = rms (o.l, (size_t) k * 11025, (size_t) (k + 1) * 11025);
        CHECK_MSG (std::fabs (20.0 * std::log10 (r / ref)) < 0.7, "quarter %d: %.2f dB", k, 20.0 * std::log10 (r / ref));
    }
}

TEST (sampler_ping_pong_turns_around_inside_the_region)
{
    const int len = 20000;
    std::vector<float> ramp ((size_t) len);
    for (int i = 0; i < len; ++i) ramp[(size_t) i] = 0.9f * (float) i / (float) len;
    const auto smp = makeSample (ramp);
    Rig rig (Engine::Sampler, 44100.0, smp.get());
    Params p (kSamplerParams);
    p.set ("start", 0.5f).set ("length", 0.1f).set ("loop", 2);       // frames 10000..11000
    rig.on (p);
    Out o; rig.render (o, p, 6000, 7);
    CHECK (! rig.eng->finished());
    const double lo = kUnity * 0.9 * 10000.0 / len, hi = kUnity * 0.9 * 11000.0 / len;
    for (int n = 100; n < 6000; ++n) CHECK (o.l[(size_t) n] > lo - 1.0e-3 && o.l[(size_t) n] < hi + 1.0e-3);
    CHECK_NEAR (o.l[500], kUnity * 0.9 * 10500.0 / len, 1.0e-4);
    CHECK_NEAR (o.l[1500], kUnity * 0.9 * 10500.0 / len, 1.0e-4);      // on the way back
    CHECK_NEAR (o.l[2250], kUnity * 0.9 * 10250.0 / len, 1.0e-4);      // and forward again
    CHECK (maxStep (o.l, 100, 6000) < 1.0e-4);
}

TEST (sampler_legato_keeps_playing_and_start_random_is_seeded)
{
    const auto smp = makeSample (sineWave (44100, 100.0));
    Rig rig (Engine::Sampler, 44100.0, smp.get());
    Params p (kSamplerParams);
    rig.on (p, 60.f);
    Out o; rig.render (o, p, 4000);
    rig.on (p, 72.f, 2, true);                                         // legato: no restart, new pitch
    rig.render (o, p, 4000);
    CHECK (maxStep (o.l, 3900, 4200) < 0.8 * kUnity * kTau / 100.0 * 2.2);
    CHECK_NEAR (zeroCrossingHz (o.l, 4500, 8000, 44100.0), 882.0, 2.0);
    // 4000 frames at 1x + 4000 at 2x (minus the 16-sample glide of the rate) leaves the rest of the sample
    Out rest; rig.render (rest, p, 44100);
    CHECK (std::abs (lastNonZero (rest.l) + 1 - (44100 - 12000) / 2) < 40);

    Params q (kSamplerParams);
    q.set ("startrnd", 1.f);
    Out a, b, c;
    { Rig r1 (Engine::Sampler, 44100.0, smp.get()); r1.on (q, 60.f, 5); r1.render (a, q, 2000); }
    { Rig r2 (Engine::Sampler, 44100.0, smp.get()); r2.on (q, 60.f, 5); r2.render (b, q, 2000); }
    { Rig r3 (Engine::Sampler, 44100.0, smp.get()); r3.on (q, 60.f, 6); r3.render (c, q, 2000); }
    CHECK (maxDiff (a, b) == 0.0);
    CHECK (maxDiff (a, c) > 0.01);
}

TEST (sampler_stereo_width_and_gain)
{
    const std::vector<float> l = sineWave (20000, 100.0, 0.5f), r = sineWave (20000, 63.0, 0.5f);
    const auto smp = makeSample (l, &r);
    auto run = [&] (float width, float gainDb)
    {
        Rig rig (Engine::Sampler, 44100.0, smp.get());
        Params p (kSamplerParams);
        p.set ("width", width).set ("gain", gainDb);
        rig.on (p);
        CHECK (rig.eng->isStereo());
        Out o; rig.render (o, p, 4000);
        return o;
    };
    const Out as = run (1.f, 0.f), mono = run (0.f, 0.f), loud = run (1.f, 6.f);
    for (int n : { 500, 1234, 3000 })
    {
        CHECK_NEAR (as.l[(size_t) n], kUnity * l[(size_t) n], 1.0e-5);
        CHECK_NEAR (as.r[(size_t) n], kUnity * r[(size_t) n], 1.0e-5);
        CHECK_NEAR (mono.l[(size_t) n], kUnity * 0.5f * (l[(size_t) n] + r[(size_t) n]), 1.0e-5);
        CHECK_NEAR (mono.l[(size_t) n], mono.r[(size_t) n], 1.0e-7);
        CHECK_NEAR (loud.l[(size_t) n], 1.9953f * as.l[(size_t) n], 1.0e-4);
    }
}

TEST (sampler_pitching_up_does_not_alias)
{
    // a 9 kHz tone played two octaves up would land on 36 kHz, i.e. fold to 8.1 kHz at 44.1 kHz.
    // The mip levels remove it instead; a 1 kHz tone played the same way must come through.
    auto level = [] (float hz)
    {
        const auto smp = makeSample (sineWave (88200, 44100.0 / hz, 0.8f));
        Rig rig (Engine::Sampler, 44100.0, smp.get());
        Params p (kSamplerParams);
        rig.on (p, 84.f);
        Out o; rig.render (o, p, 8000);
        return rms (o.l, 2000, 8000);
    };
    CHECK (level (9000.f) < 0.8 * kUnity * 0.7071 * 0.003);            // more than 50 dB down
    CHECK (level (1000.f) > 0.8 * kUnity * 0.7071 * 0.95);
}

// --------------------------------------------------------------------------------------- granular

TEST (granular_periodic_stream_matches_the_ideal_grain_sum)
{
    // On a constant sample every grain is just its window, so the output can be predicted exactly:
    // Hann grains of `size`, one every sr / density samples (not a whole number: onsets must be
    // sub-sample accurate), scaled by the documented normalisation.
    const auto smp = makeSample (std::vector<float> (44100, 1.f));
    const double sr = 48000.0, density = 97.0, size = 0.03;
    Rig rig (Engine::Granular, sr, smp.get());
    Params p (kGranularParams);
    p.set ("pos", 0.5f).set ("size", (float) size).set ("density", (float) density).set ("spray", 0).set ("scatter", 0)
     .set ("scan", 0).set ("revprob", 0).set ("shape", 0.5f).set ("spread", 0).set ("jitter", 0);
    rig.on (p);
    Out o; rig.render (o, p, 24000, 0);
    const double nSamp = std::floor (size * sr + 0.5), period = sr / density;
    const double pw = density * size * 0.375, amp = kUnity / std::sqrt (std::sqrt (1.0 + pw * pw));
    double err = 0;
    for (int n = 0; n < 24000; ++n)
    {
        double want = 0;
        for (int k = (int) std::floor (n / period); k >= 0 && n - k * period < nSamp; --k)
        {
            const int first = (int) std::ceil (k * period - 1.0e-9);                // the sample the grain starts on
            want += 0.5 - 0.5 * std::cos (kTau * (n - first + (first - k * period)) / nSamp);
        }
        err = std::max (err, std::fabs (o.l[(size_t) n] - amp * want));
    }
    CHECK_MSG (err < 2.0e-3, "max deviation from the ideal grain sum: %g", err);
    CHECK (o.l[0] == 0.f);                                                          // windows start at zero
    CHECK (maxDiff (o, Out { o.r, o.l }) < 1.0e-6);                                 // spread 0: centred
}

TEST (granular_grains_finish_after_gate_off_then_engine_is_finished)
{
    const auto smp = makeSample (noiseWave (44100, 9));
    for (float shape : { 0.f, 0.5f, 1.f })
    {
        Rig rig (Engine::Granular, 48000.0, smp.get());
        Params p (kGranularParams);
        p.set ("size", 0.1f).set ("density", 40.f).set ("shape", shape);
        rig.on (p);
        Out o; rig.render (o, p, 9600);
        CHECK (! rig.eng->finished());
        rig.eng->noteOff();
        Out tail; rig.render (tail, p, 9600, 7);
        CHECK (maxAbs (tail.l, 0, 2000) > 0.01);                      // grains play out ...
        CHECK (lastNonZero (tail.l) < 4800 + 2);                      // ... but no new ones start: silent after one grain length
        CHECK (rig.eng->finished());
        // every grain window ends at zero: no step where the last one stops
        const int end = lastNonZero (tail.l);
        CHECK (end < 0 || std::fabs (tail.l[(size_t) end]) < 0.02);
    }
}

TEST (granular_same_seed_same_output_other_seed_other_output)
{
    const std::vector<float> l = noiseWave (30000, 1), r = noiseWave (30000, 2);
    const auto smp = makeSample (l, &r);
    Params p (kGranularParams);
    p.set ("spray", 0.5f).set ("scatter", 7.f).set ("revprob", 0.5f).set ("jitter", 1.f).set ("spread", 1.f).set ("scan", 0.5f);
    auto run = [&] (uint32_t seed, int block)
    {
        Rig rig (Engine::Granular, 48000.0, smp.get());
        rig.on (p, 60.f, seed);
        Out o; rig.render (o, p, 24000, block);
        return o;
    };
    const Out a = run (42, 16), b = run (42, 16), c = run (43, 16);
    CHECK (maxDiff (a, b) == 0.0);
    CHECK (maxDiff (a, c) > 0.01);
    // a new note on the same engine must not depend on what was played before
    Rig rig (Engine::Granular, 48000.0, smp.get());
    rig.on (p, 67.f, 7);
    Out junk; rig.render (junk, p, 5000);
    rig.on (p, 60.f, 42);
    Out again; rig.render (again, p, 24000);
    CHECK (maxDiff (a, again) == 0.0);
}

TEST (granular_level_is_steady_across_density_and_size)
{
    const std::vector<float> l = noiseWave (88200, 31, 0.5f), r = noiseWave (88200, 32, 0.5f);
    const auto smp = makeSample (l, &r);
    const double sampleRms = 0.5 / std::sqrt (3.0);
    struct Case { float density, size, shape; };
    for (const Case& c : { Case { 5.f, 0.2f, 0.5f }, Case { 30.f, 0.08f, 0.5f }, Case { 100.f, 0.1f, 0.5f }, Case { 250.f, 0.05f, 0.5f },
                           Case { 250.f, 1.f, 0.5f }, Case { 200.f, 0.1f, 0.f }, Case { 200.f, 0.1f, 1.f }, Case { 60.f, 0.5f, 0.2f } })
    {
        Rig rig (Engine::Granular, 48000.0, smp.get());
        Params p (kGranularParams);
        p.set ("density", c.density).set ("size", c.size).set ("shape", c.shape).set ("spray", 1.f).set ("jitter", 1.f);
        rig.on (p);
        Out o; rig.render (o, p, 48000 * 4);
        const double db = 20.0 * std::log10 (rms (o.l, 96000, 192000) / (kUnity * sampleRms));
        // dense clouds sit at the level of the sample itself; sparse grains can only be quieter
        CHECK_MSG (db < 1.5 && db > (c.density * c.size > 3.f ? -2.5 : -9.0), "density %g size %g shape %g: %.1f dB re sample", c.density, c.size, c.shape, db);
        CHECK (maxAbs (o.l) < 1.81 && maxAbs (o.r) < 1.81);
    }
}

TEST (granular_scatter_quantised_to_octaves_keeps_the_pitch_class)
{
    const auto smp = makeSample (sineWave (44100, 100.0));
    Rig rig (Engine::Granular, 44100.0, smp.get());
    Params p (kGranularParams);
    p.set ("scatter", 24.f).set ("quant", 1).set ("density", 20.f).set ("size", 0.04f).set ("jitter", 0.f).set ("spray", 0.3f).set ("spread", 0.f);
    rig.on (p);
    Out o; rig.render (o, p, 44100 * 2);
    // every grain is 441 Hz times a power of two: project onto those and onto frequencies in between
    auto tone = [&] (double hz)
    {
        double re = 0, im = 0;
        for (size_t n = 0; n < o.l.size(); ++n) { re += o.l[n] * std::cos (kTau * hz * (double) n / 44100.0); im += o.l[n] * std::sin (kTau * hz * (double) n / 44100.0); }
        return std::sqrt (re * re + im * im) / (double) o.l.size();
    };
    const double octaves = tone (110.25) + tone (220.5) + tone (441.0) + tone (882.0) + tone (1764.0);
    const double between = tone (311.8) + tone (330.4) + tone (623.7) + tone (660.8) + tone (1247.0);
    CHECK_MSG (octaves > 40.0 * between, "octaves %g, in between %g", octaves, between);
}

// ------------------------------------------------------------------------------------------- both

TEST (sample_engines_are_block_size_independent)
{
    const std::vector<float> l = hitsWave (30000, { 0, 5000, 9000, 16000, 22000 }), r = noiseWave (30000, 4);
    const auto smp = makeSample (l, &r);
    for (int type : { Engine::Sampler, Engine::Granular })
        for (int variant = 0; variant < 4; ++variant)
        {
            Params p (type == Engine::Sampler ? kSamplerParams : kGranularParams);
            if (type == Engine::Sampler)
            {
                if (variant == 1) p.set ("mode", 2).set ("slice", 5).set ("loop", 1).set ("xfade", 0.3f).set ("penv", 24.f).set ("penvt", 0.2f);
                if (variant == 2) p.set ("loop", 2).set ("reverse", 1).set ("start", 0.2f).set ("length", 0.1f).set ("penv", -36.f).set ("startrnd", 0.5f);
                if (variant == 3) p.set ("mode", 2).set ("sliceby", 1).set ("slice", 2).set ("width", 2.f).set ("gain", 12.f).set ("penv", 48.f).set ("penvt", 0.002f);
            }
            else
            {
                if (variant == 1) p.set ("density", 250.f).set ("size", 1.f).set ("spray", 1.f).set ("scatter", 24.f).set ("revprob", 0.5f).set ("scan", 2.f).set ("jitter", 1.f);
                if (variant == 2) p.set ("density", 187.f).set ("size", 0.004f).set ("jitter", 0.f).set ("shape", 0.f).set ("quant", 2).set ("scatter", 12.f);
                if (variant == 3) p.set ("density", 2.f).set ("size", 0.7f).set ("shape", 1.f).set ("scan", -2.f).set ("spread", 1.f);
            }
            Out ref;
            for (int block : { 1, 7, 32, 0 })
            {
                Rig rig (type, 48000.0, smp.get());
                rig.on (p, 64.3f, 99);
                Out o; rig.render (o, p, 20000, block);
                if (ref.l.empty()) { ref = o; CHECK (maxAbs (o.l) > 0.001); continue; }
                CHECK_MSG (maxDiff (ref, o) == 0.0, "engine %d variant %d block %d differs by %g", type, variant, block, maxDiff (ref, o));
            }
        }
}

TEST (sample_engines_are_silent_and_finished_without_a_sample)
{
    const auto real = makeSample (noiseWave (5000, 1));
    for (int type : { Engine::Sampler, Engine::Granular })
    {
        Params p (type == Engine::Sampler ? kSamplerParams : kGranularParams);
        Rig rig (type, 48000.0, nullptr);
        CHECK (rig.eng->finished());
        rig.on (p);
        Out o; rig.render (o, p, 2000, 0);
        CHECK (maxAbs (o.l) == 0.0 && maxAbs (o.r) == 0.0);
        CHECK (rig.eng->finished());
        // a sample arrives, then is taken away again
        rig.eng->reset(); rig.eng->setSample (real.get());
        rig.on (p);
        Out o2; rig.render (o2, p, 2000);
        CHECK (maxAbs (o2.l) > 0.01);
        rig.eng->reset(); rig.eng->setSample (nullptr);
        rig.on (p);
        Out o3; rig.render (o3, p, 2000);
        CHECK (maxAbs (o3.l) == 0.0 && rig.eng->finished());
    }
}

TEST (sample_engines_survive_random_parameters)
{
    const std::vector<float> l = hitsWave (40000, { 0, 7000, 15000, 26000, 33000 }), r = noiseWave (40000, 8, 1.f);
    std::vector<Sample> samples;
    samples.push_back (makeSample (l, &r));
    samples.push_back (makeSample (noiseWave (20000, 2, 1.f), nullptr, 96000.0));
    samples.push_back (makeSample (sineWave (40, 20.0)));                 // shorter than one mip level
    samples.push_back (makeSample (std::vector<float> { 0.5f, -0.5f, 0.25f }));
    samples.push_back (makeSample (std::vector<float> { 1.f }));
    samples.push_back (makeSample (std::vector<float>()));
    Rng rng (2024);
    for (int type : { Engine::Sampler, Engine::Granular })
        for (int round = 0; round < 300; ++round)
        {
            static constexpr double rates[] = { 44100.0, 48000.0, 96000.0, 192000.0 };
            const double sr = rates[rng.irange (0, 3)];
            const SampleData* smp = rng.chance (0.1f) ? nullptr : samples[(size_t) rng.irange (0, (int) samples.size() - 1)].get();
            Rig rig (type, sr, smp);
            Params p (type == Engine::Sampler ? kSamplerParams : kGranularParams);
            auto randomise = [&]
            {
                for (float& v : p.e) { const float u = rng.uni(); v = u < 0.15f ? 0.f : (u > 0.85f ? 1.f : rng.uni()); }
            };
            randomise();
            Out o;
            for (int ev = 0; ev < 6; ++ev)
            {
                const int what = rng.irange (0, 5);
                if (what <= 1)      rig.on (p, rng.range (-10.f, 140.f), (uint32_t) rng.next(), what == 1);
                else if (what == 2) rig.eng->noteOff();
                else if (what == 3) randomise();                           // a parameter lock: everything jumps
                else                rig.note = rng.range (0.f, 127.f);     // pitch modulation
                rig.render (o, p, rng.irange (1, 3000), rng.chance (0.5f) ? 0 : rng.irange (1, 32));
            }
            const bool ok = allFinite (o, 1.81);
            CHECK_MSG (ok, "engine %d round %d: non-finite or runaway output", type, round);
            if (! ok) return;
        }
}
