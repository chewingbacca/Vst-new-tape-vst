// All six sound engines: block-size independence, robustness under random parameters, default
// levels, pitch accuracy, release / finished(), and retrigger clicks.
#include "TestFramework.h"
#include "voices/Voice.h"
#include "model/Model.h"
#include "model/ParamTables.h"
#include <cstring>
#include <memory>
#include <vector>
using namespace atm;

namespace {

const int kAllEngines[] = { Engine::Drum, Engine::FM, Engine::Sampler, Engine::Granular, Engine::Resonator, Engine::Synth };

struct Out { std::vector<float> l, r; };

double peakOf (const Out& o, size_t from = 0, size_t to = (size_t) -1)
{
    double m = 0;
    to = std::min (to, o.l.size());
    for (size_t i = from; i < to; ++i) m = std::max (m, (double) std::max (std::fabs (o.l[i]), std::fabs (o.r[i])));
    return m;
}

double maxStepOf (const std::vector<float>& x, size_t from, size_t to)
{
    double m = 0;
    to = std::min (to, x.size());
    for (size_t i = std::max<size_t> (from, 1); i < to; ++i) m = std::max (m, (double) std::fabs (x[i] - x[i - 1]));
    return m;
}

double dbOf (double v) { return 20.0 * std::log10 (std::max (v, 1.0e-12)); }

// A synthetic stereo drum loop for the sample engines (one second at 44.1 kHz).
const SampleData* testSample()
{
    static std::unique_ptr<SampleData> s;
    if (! s)
    {
        const int n = 44100;
        std::vector<float> l ((size_t) n, 0.f), r ((size_t) n, 0.f);
        Noise nz; nz.seed (17);
        const int onsets[] = { 0, 5512, 11025, 16537, 22050, 27562, 33075, 38587 };
        for (int k = 0; k < 8; ++k)
            for (int i = onsets[k]; i < n; ++i)
            {
                const double t = (i - onsets[k]) / 44100.0;
                float v;
                if (k % 4 == 0)      v = (float) (0.8 * std::sin (kTau * (50.0 * t + 2.5 * (1.0 - std::exp (-t * 35.0)))) * std::exp (-t * 10.0));
                else if (k % 4 == 2) v = 0.5f * nz.next() * (float) std::exp (-t * 25.0);
                else                 v = 0.2f * nz.next() * (float) std::exp (-t * 90.0);
                l[(size_t) i] += v;
                r[(size_t) i] += k % 2 ? 0.7f * v : v;
            }
        const float* ptrs[2] = { l.data(), r.data() };
        s.reset (makeSampleData (ptrs, 2, n, 44100.0, "enginetest"));
    }
    return s.get();
}

struct Params
{
    float e[kEngineParams];
    const ParamDesc* d;
    explicit Params (int engine) : d (engineParamDescs (engine))
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
    void randomise (Rng& rng)
    {
        for (float& v : e) { const float u = rng.uni(); v = u < 0.15f ? 0.f : (u > 0.85f ? 1.f : rng.uni()); }
    }
};

// Drives one engine directly the way a voice does: control() before every render() slice.
struct Rig
{
    std::unique_ptr<EngineBase> eng;
    float note = 60.f;
    Rig (int type, double sr)
    {
        eng = createEngine (type);
        eng->prepare (sr);
        eng->setSample (testSample());
    }
    void on (const Params& p, float n, uint32_t seed = 1, float vel = 1.f, bool legato = false)
    {
        note = n;
        EngineNote en; en.note = n; en.seed = seed; en.velocity = vel; en.legato = legato;
        eng->noteOn (en, p.e);
    }
    void render (Out& o, const Params& p, int frames, int block)
    {
        float bl[kMaxSlice], br[kMaxSlice];
        for (int done = 0; done < frames;)
        {
            const int n = std::min (block, frames - done);
            eng->control (p.e, note);
            eng->render (bl, br, n);
            const bool st = eng->isStereo();
            for (int i = 0; i < n; ++i) { o.l.push_back (bl[i]); o.r.push_back (st ? br[i] : bl[i]); }
            done += n;
        }
    }
};

// Two voices used the way the audio engine does for a mono track: a new note kills the
// sounding voice (3 ms fade) and starts on the other one.
struct VoiceRig
{
    Voice v[2];
    int cur = 0;
    TrackSound ts;
    VoiceRig (int engine, double sr)
    {
        initTrackSound (ts, engine);
        for (auto& x : v) { x.prepare (sr); x.setSample (testSample()); }
    }
    void setEngineParams (const Params& p) { for (int i = 0; i < kEngineParams; ++i) ts.params[TP::E0 + i] = p.e[i]; }
    void start (float note, uint32_t seed, int gateSamples, float vel = 1.f)
    {
        VoiceStart s; s.note = note; s.velocity = vel; s.seed = seed; s.gateSamples = gateSamples;
        v[cur].kill();
        cur ^= 1;
        v[cur].start (s, ts.engine, ts.params);
    }
    void render (Out& o, int frames, int block = 16)
    {
        std::vector<float> l ((size_t) kMaxSlice), r ((size_t) kMaxSlice);
        for (int done = 0; done < frames;)
        {
            const int n = std::min (block, frames - done);
            std::fill (l.begin(), l.end(), 0.f); std::fill (r.begin(), r.end(), 0.f);
            for (auto& x : v) if (x.active()) { x.control (ts.params, n); x.render (l.data(), r.data(), n); }
            for (int i = 0; i < n; ++i) { o.l.push_back (l[(size_t) i]); o.r.push_back (r[(size_t) i]); }
            done += n;
        }
    }
    bool anyActive() const { return v[0].active() || v[1].active(); }
};

// Fundamental frequency near `guess`: maximum of the Hann-windowed DFT magnitude within +-80 cents.
double measurePitch (const std::vector<float>& x, size_t from, size_t len, double sr, double guess)
{
    len = std::min (len, x.size() - from);
    auto mag = [&] (double hz)
    {
        double re = 0, im = 0;
        const double w = kTau * hz / sr;
        for (size_t i = 0; i < len; ++i)
        {
            const double win = 0.5 - 0.5 * std::cos (kTau * (double) i / (double) (len - 1));
            const double v = x[from + i] * win;
            re += v * std::cos (w * (double) i); im += v * std::sin (w * (double) i);
        }
        return std::sqrt (re * re + im * im);
    };
    double best = guess, bestM = -1;
    for (double c = -80.0; c <= 80.0; c += 2.0)
    {
        const double hz = guess * std::exp2 (c / 1200.0), m = mag (hz);
        if (m > bestM) { bestM = m; best = hz; }
    }
    double step = 1.0;                                          // cents
    for (int it = 0; it < 12; ++it, step *= 0.5)
    {
        const double up = best * std::exp2 (step / 1200.0), dn = best * std::exp2 (-step / 1200.0);
        const double mu = mag (up), md = mag (dn);
        if (mu > bestM && mu >= md) { best = up; bestM = mu; }
        else if (md > bestM) { best = dn; bestM = md; }
    }
    return best;
}

double cents (double hz, double ref) { return 1200.0 * std::log2 (hz / ref); }

// Settings to check per engine: defaults plus a few characteristic variants.
std::vector<Params> variantsFor (int engine)
{
    std::vector<Params> v;
    v.emplace_back (engine);
    switch (engine)
    {
        case Engine::Drum:
            for (int m = 1; m < 6; ++m) { Params p (engine); p.set ("model", (float) m); v.push_back (p); }
            { Params p (engine); p.set ("model", 0).set ("fm", 0.6f).set ("shape", 0.7f).set ("dirt", 0.5f).set ("hold", 0.05f); v.push_back (p); }
            { Params p (engine); p.set ("model", 3).set ("shape", 1.f).set ("spread", 1.f).set ("sweep", 1.f).set ("fm", 0.5f); v.push_back (p); }
            break;
        case Engine::FM:
            { Params p (engine); p.set ("algo", 2).set ("fb", 0.7f).set ("level2", 0.9f).set ("level3", 0.8f).set ("ratio2", 3.5f).set ("penv", 24.f); v.push_back (p); }
            { Params p (engine); p.set ("algo", 7).set ("level2", 1.f).set ("level3", 1.f).set ("level4", 1.f); v.push_back (p); }
            break;
        case Engine::Sampler:
            { Params p (engine); p.set ("mode", 2).set ("slice", 3).set ("loop", 2).set ("penv", 12.f); v.push_back (p); }
            { Params p (engine); p.set ("reverse", 1).set ("loop", 1).set ("xfade", 0.3f).set ("start", 0.1f).set ("length", 0.3f); v.push_back (p); }
            break;
        case Engine::Granular:
            { Params p (engine); p.set ("density", 200.f).set ("size", 0.02f).set ("scatter", 12.f).set ("revprob", 0.5f).set ("scan", 1.f); v.push_back (p); }
            break;
        case Engine::Resonator:
            for (int m = 1; m < 6; ++m) { Params p (engine); p.set ("model", (float) m).set ("exciter", (float) (m % 4)); v.push_back (p); }
            { Params p (engine); p.set ("exciter", 3).set ("bow", 0.8f).set ("struct", 0.8f); v.push_back (p); }
            break;
        case Engine::Synth:
            { Params p (engine); p.set ("unison", 4).set ("unidet", 60.f).set ("fm", 0.4f).set ("sync", 3.f).set ("mix", 0.5f).set ("sub", 0.5f).set ("noise", 0.2f); v.push_back (p); }
            for (int m = 1; m < 6; ++m) { Params p (engine); p.set ("mode", (float) m).set ("unison", 2).set ("wave1", 0.7f); v.push_back (p); }
            break;
    }
    return v;
}

} // namespace

// ------------------------------------------------------------------------------------------------

TEST (engines_are_block_size_independent)
{
    // A parameter jump and the note-off land on samples that every slice size reaches exactly.
    const int jumpAt = 9632, offAt = 2 * 9632, frames = 28800;    // 9632 = 7 * 1376 = 32 * 301
    Rng rng (99);
    for (int engine : kAllEngines)
    {
        std::vector<Params> sets = variantsFor (engine);
        for (int k = 0; k < 3; ++k) { Params p (engine); p.randomise (rng); sets.push_back (p); }
        for (size_t s = 0; s < sets.size(); ++s)
        {
            Params jumped = sets[s];
            for (int i = 1; i < kEngineParams; ++i) if (rng.chance (0.3f)) jumped.e[i] = rng.uni();
            Out ref;
            for (int block : { 1, 7, 32 })
            {
                Rig rig (engine, 48000.0);
                rig.on (sets[s], 57.3f, 4242);
                Out o;
                rig.render (o, sets[s], jumpAt, block);
                rig.render (o, jumped, offAt - jumpAt, block);
                rig.eng->noteOff();
                rig.render (o, jumped, frames - offAt, block);
                if (ref.l.empty()) { ref = o; continue; }
                double diff = 0; size_t first = 0;
                for (size_t i = 0; i < o.l.size(); ++i)
                {
                    const double d = std::max (std::fabs (o.l[i] - ref.l[i]), std::fabs (o.r[i] - ref.r[i]));
                    if (d > 0 && diff == 0) first = i;
                    diff = std::max (diff, d);
                }
                CHECK_MSG (diff == 0.0, "%s set %d block %d differs by %g (first at sample %d)", engineName (engine), (int) s, block, diff, (int) first);
            }
        }
    }
}

TEST (engines_survive_random_parameters)
{
    Rng rng (31337);
    for (int engine : kAllEngines)
        for (int round = 0; round < 200; ++round)
        {
            static constexpr double rates[] = { 44100.0, 48000.0, 96000.0, 192000.0 };
            const double sr = rates[rng.irange (0, 3)];
            Rig rig (engine, sr);
            Params p (engine);
            p.randomise (rng);
            Out o;
            rig.on (p, rng.range (0.f, 127.f), rng.next(), rng.uni());
            for (int ev = 0; ev < 6; ++ev)
            {
                const int what = rng.irange (0, 5);
                if (what <= 1)      rig.on (p, rng.range (-10.f, 140.f), rng.next(), rng.uni(), what == 1);
                else if (what == 2) rig.eng->noteOff();
                else if (what == 3) p.randomise (rng);                         // a parameter lock: everything jumps
                else                rig.note = rng.range (-20.f, 150.f);       // pitch modulation
                rig.render (o, p, rng.irange (1, 4000), rng.irange (1, 32));
            }
            double worst = 0; bool finite = true;
            for (size_t i = 0; i < o.l.size(); ++i)
            {
                if (! std::isfinite (o.l[i]) || ! std::isfinite (o.r[i])) finite = false;
                else worst = std::max (worst, (double) std::max (std::fabs (o.l[i]), std::fabs (o.r[i])));
            }
            CHECK_MSG (finite && worst < 4.0, "%s round %d: %s, peak %g", engineName (engine), round, finite ? "finite" : "NaN/Inf", worst);
            if (! finite || worst >= 4.0) break;
        }
}

TEST (engine_default_levels)
{
    // Through a full voice with default track settings, velocity 1, half-second gate.
    for (int engine : kAllEngines)
    {
        const int models = engine == Engine::Drum ? 6 : 1;
        for (int m = 0; m < models; ++m)
            for (float note : { 48.f, 60.f })
                for (double sr : { 44100.0, 96000.0 })
                {
                    VoiceRig vr (engine, sr);
                    Params p (engine);
                    if (engine == Engine::Drum) p.set ("model", (float) m);
                    vr.setEngineParams (p);
                    vr.start (note, 1234, (int) (0.5 * sr));
                    Out o; vr.render (o, (int) (1.5 * sr));
                    const double db = dbOf (peakOf (o));
                    CHECK_MSG (db > -7.0 && db < 0.0, "%s model %d note %g sr %g: peak %.1f dBFS", engineName (engine), m, note, sr, db);
                }
    }
}

TEST (drum_models_peak_near_minus_3_and_stay_bounded)
{
    for (int m = 0; m < 6; ++m)
    {
        // defaults: -3 dBFS +-2 dB at note 60 (the drum track's base note), across seeds
        double lo = 1e9, hi = -1e9;
        for (uint32_t seed = 1; seed <= 8; ++seed)
        {
            Rig rig (Engine::Drum, 48000.0);
            Params p (Engine::Drum); p.set ("model", (float) m);
            rig.on (p, 60.f, seed);
            Out o; rig.render (o, p, 24000, 16);
            const double db = dbOf (peakOf (o));
            lo = std::min (lo, db); hi = std::max (hi, db);
        }
        CHECK_MSG (lo > -5.0 && hi < -1.0, "drum model %d: default peaks %.1f .. %.1f dBFS", m, lo, hi);

        // extremes: below +6 dBFS
        Rng rng (5u + (uint32_t) m);
        double worst = 0;
        for (int round = 0; round < 150; ++round)
        {
            Params p (Engine::Drum);
            for (float& v : p.e) { const float u = rng.uni(); v = u < 0.4f ? 0.f : (u < 0.8f ? 1.f : rng.uni()); }
            p.set ("model", (float) m);
            Rig rig (Engine::Drum, 48000.0);
            const float note = rng.range (24.f, 96.f);
            rig.on (p, note, (uint32_t) round + 1);
            Out o; rig.render (o, p, 24000, 32);
            worst = std::max (worst, peakOf (o));
        }
        CHECK_MSG (dbOf (worst) < 6.0, "drum model %d: extreme settings peak %.1f dBFS", m, dbOf (worst));
    }
}

TEST (drum_velocity_brightens_and_every_parameter_acts)
{
    // spectral centroid of the first 50 ms rises with velocity (engine level, no amp scaling)
    auto centroid = [] (const Out& o, size_t n)
    {
        double num = 0, den = 0;
        for (size_t i = 1; i < n; ++i) { const double d = o.l[i] - o.l[i - 1]; num += d * d; den += (double) o.l[i] * o.l[i]; }
        return std::sqrt (num / std::max (den, 1e-20));                // rms(derivative) / rms: a brightness measure
    };
    for (int m = 0; m < 6; ++m)
    {
        Params p (Engine::Drum); p.set ("model", (float) m);
        Out soft, hard;
        { Rig r (Engine::Drum, 48000.0); r.on (p, 60.f, 3, 0.3f); r.render (soft, p, 2400, 16); }
        { Rig r (Engine::Drum, 48000.0); r.on (p, 60.f, 3, 1.0f); r.render (hard, p, 2400, 16); }
        CHECK_MSG (centroid (hard, 2400) > centroid (soft, 2400) * 1.02, "drum model %d: velocity does not brighten (%g vs %g)", m, centroid (hard, 2400), centroid (soft, 2400));
    }

    // Each parameter changes the output of every model, except those documented as inactive.
    const ParamDesc* d = kDrumParams;
    for (int m = 0; m < 6; ++m)
        for (int k = 1; k < kEngineParams; ++k)
        {
            if (d[k].key[0] == 0) continue;
            const bool inactive = (std::strcmp (d[k].key, "spread") == 0 && (m == 0 || m == 4));   // kick, tom: mono
            Params a (Engine::Drum); a.set ("model", (float) m);
            Params b = a;
            // fm ratio only matters with some fm
            if (std::strcmp (d[k].key, "fmratio") == 0) { a.set ("fm", 0.5f); b.set ("fm", 0.5f); }
            a.e[k] = 0.1f; b.e[k] = 0.9f;
            Out oa, ob;
            { Rig r (Engine::Drum, 48000.0); r.on (a, 60.f, 9); r.render (oa, a, 24000, 16); }
            { Rig r (Engine::Drum, 48000.0); r.on (b, 60.f, 9); r.render (ob, b, 24000, 16); }
            double diff = 0, ref = 0;
            for (size_t i = 0; i < oa.l.size(); ++i)
            {
                diff += (double) (oa.l[i] - ob.l[i]) * (oa.l[i] - ob.l[i]) + (double) (oa.r[i] - ob.r[i]) * (oa.r[i] - ob.r[i]);
                ref += (double) oa.l[i] * oa.l[i] + (double) ob.l[i] * ob.l[i];
            }
            const double rel = std::sqrt (diff / std::max (ref, 1e-20));
            if (inactive) CHECK_MSG (rel < 1e-6, "drum model %d: %s documented inactive but changes the sound", m, d[k].key);
            else CHECK_MSG (rel > 0.05, "drum model %d: %s barely changes the sound (%.3f)", m, d[k].key, rel);
        }
}

TEST (engine_pitch_is_accurate)
{
    struct Case { int engine; const char* label; };
    for (double sr : { 44100.0, 96000.0 })
        for (float note : { 36.f, 60.f, 84.f })
            for (int which = 0; which < 4; ++which)
            {
                int engine = Engine::FM;
                Params p (Engine::FM);
                const char* label = "FM sine";
                if (which == 0) p.set ("algo", 0).set ("level2", 0.f).set ("level3", 0.f).set ("level4", 0.f).set ("decay1", 10.f);
                else if (which == 1)
                {
                    engine = Engine::Synth; label = "Synth analog triangle";
                    p = Params (engine);
                    p.set ("mode", 0).set ("wave1", 0.f).set ("mix", 0.f).set ("sub", 0.f).set ("noise", 0.f).set ("drift", 0.f).set ("unison", 0);
                }
                else
                {
                    engine = Engine::Resonator; label = which == 2 ? "Resonator string" : "Resonator tube";
                    p = Params (engine);
                    p.set ("model", (float) (which - 2)).set ("decay", 6.f);
                }
                Rig rig (engine, sr);
                rig.on (p, note, 1);
                Out o; rig.render (o, p, (int) (0.9 * sr), 16);
                const double want = midiToHz (note);
                const double got = measurePitch (o.l, (size_t) (0.1 * sr), (size_t) (0.8 * sr), sr, want);
                CHECK_MSG (std::fabs (cents (got, want)) < 5.0, "%s note %g sr %g: %.2f Hz, expected %.2f (%+.1f cents)", label, note, sr, got, want, cents (got, want));
            }
    // the string stays in tune when damping and stiffness move
    for (float damp : { 0.f, 0.9f })
        for (float st : { 0.f, 0.6f })
        {
            Params p (Engine::Resonator);
            p.set ("model", 0).set ("decay", 6.f).set ("damp", damp).set ("struct", st);
            Rig rig (Engine::Resonator, 48000.0);
            rig.on (p, 48.f, 1);
            Out o; rig.render (o, p, 43200, 16);
            const double want = midiToHz (48.0);
            const double got = measurePitch (o.l, 4800, 38400, 48000.0, want);
            CHECK_MSG (std::fabs (cents (got, want)) < 5.0, "string damp %g struct %g: %+.1f cents", damp, st, cents (got, want));
        }
}

TEST (engines_fall_silent_and_finish_after_release)
{
    // Voice level, defaults: after the gate ends the voice frees itself and outputs exact silence.
    for (int engine : kAllEngines)
        for (const Params& p : variantsFor (engine))
        {
            VoiceRig vr (engine, 48000.0);
            vr.setEngineParams (p);
            vr.start (55.f, 77, 9600);                          // 0.2 s gate
            Out o;
            int t = 0;
            while (vr.anyActive() && t < 48000 * 40) { vr.render (o, 480); t += 480; }
            CHECK_MSG (! vr.anyActive(), "%s: voice still active after 40 s", engineName (engine));
            Out after; vr.render (after, 4800);
            CHECK (peakOf (after) == 0.0);
        }

    // Engine level, for engines that end on their own: finished() within the decay, then silence.
    struct Case { int engine; float model; double limit; };
    for (const Case& c : { Case { Engine::Drum, 0, 1.0 }, Case { Engine::Drum, 1, 1.0 }, Case { Engine::Drum, 2, 1.0 }, Case { Engine::Drum, 3, 1.5 },
                           Case { Engine::Drum, 4, 1.0 }, Case { Engine::Drum, 5, 1.0 }, Case { Engine::Resonator, 0, 4.0 }, Case { Engine::Resonator, 3, 4.0 },
                           Case { Engine::FM, -1, 2.0 }, Case { Engine::Granular, -1, 1.0 }, Case { Engine::Sampler, -1, 2.0 } })
    {
        Params p (c.engine);
        if (c.model >= 0) p.set ("model", c.model);
        if (c.engine == Engine::FM) p.set ("decay1", 0.5f);
        Rig rig (c.engine, 48000.0);
        rig.on (p, 60.f, 5);
        CHECK (! rig.eng->finished());
        Out o; rig.render (o, p, 4800, 16);
        rig.eng->noteOff();
        int t = 4800;
        while (! rig.eng->finished() && t < (int) (c.limit * 48000.0)) { rig.render (o, p, 16, 16); t += 16; }
        CHECK_MSG (rig.eng->finished(), "%s model %g: not finished %g s after note-on", engineName (c.engine), c.model, c.limit);
        Out after; rig.render (after, p, 4800, 16);
        CHECK_MSG (peakOf (after) < 1.0e-4, "%s model %g: %g after finished()", engineName (c.engine), c.model, peakOf (after));
    }
}

TEST (engine_retrigger_does_not_click)
{
    // Same note, same seed: every onset is identical, so anything larger than the steps of a single
    // note comes from the hand-over (kill fade of the old voice + start of the new one).
    for (int engine : kAllEngines)
        for (const Params& p : variantsFor (engine))
            for (double period : { 0.03, 0.11 })
            {
                const double sr = 48000.0;
                const int per = (int) (period * sr);
                Out single;
                { VoiceRig vr (engine, sr); vr.setEngineParams (p); vr.start (50.f, 3, per / 2); vr.render (single, per * 3); }
                const double ref = maxStepOf (single.l, 0, single.l.size());
                VoiceRig vr (engine, sr);
                vr.setEngineParams (p);
                Out o;
                for (int k = 0; k < 8; ++k) { vr.start (50.f, 3, per / 2); vr.render (o, per); }
                double worst = 0;
                for (int k = 1; k < 8; ++k)
                    worst = std::max (worst, maxStepOf (o.l, (size_t) (k * per - 8), (size_t) (k * per + 0.006 * sr)));
                CHECK_MSG (worst <= 1.5 * ref + 2.0e-3, "%s period %g: retrigger step %.4f vs single-note step %.4f (model/variant e0=%.2f)",
                           engineName (engine), period, worst, ref, p.e[0]);
            }
}
