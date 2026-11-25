// Factory sample library, synthesised on first use (nothing is shipped as audio).
// Breaks are rendered through Automata's own drum engine; textures, vowels and
// tones are synthesised directly here. Everything is deterministic and original.
#include "FactorySamples.h"
#include "../engine/OfflineRender.h"
#include "../dsp/Filters.h"
#include "../util/Math.h"
#include "../util/Rng.h"
#include <cstring>
#include <mutex>
#include <vector>

namespace atm {

namespace {

constexpr double kSr = 44100.0;

struct Buf { std::vector<float> l, r; double sr = kSr; float root = 60.f; };

void normalise (Buf& b, float peakDb = -1.f)
{
    float pk = 0.f;
    for (size_t i = 0; i < b.l.size(); ++i) pk = std::max (pk, std::max (std::fabs (b.l[i]), std::fabs (b.r[i])));
    if (pk < 1.0e-6f) return;
    const float g = dbToGain (peakDb) / pk;
    for (size_t i = 0; i < b.l.size(); ++i) { b.l[i] *= g; b.r[i] *= g; }
}

void fadeEdges (Buf& b, double inSec = 0.002, double outSec = 0.02)
{
    const size_t n = b.l.size();
    const size_t fi = std::min (n, (size_t) (inSec * b.sr)), fo = std::min (n, (size_t) (outSec * b.sr));
    for (size_t i = 0; i < fi; ++i) { const float g = (float) i / (float) fi; b.l[i] *= g; b.r[i] *= g; }
    for (size_t i = 0; i < fo; ++i) { const float g = (float) i / (float) fo; b.l[n - 1 - i] *= g; b.r[n - 1 - i] *= g; }
}

// ---------------------------------------------------------------- breaks
// A break is a little drum project rendered offline. Patterns are 32 sixteenths
// written as strings: digits 1-9 = velocity steps, '.' = rest, 'r' = 3-hit roll, 'f' = flam (2 hits).
struct BreakDef
{
    const char* name;
    double bpm;
    float swing;
    const char* kick;
    const char* snare;
    const char* ghost;
    const char* hat;
    const char* open;
    const char* perc;
};

const BreakDef kBreaks[] = {
    { "Brk_Clatter", 170, 0.54f,
      "9.....6...9.....9.....6.5.......",
      "....9.......9.......9.......9..f",
      "..3...2.3.....2...3.......3.2...",
      "6.4.7.4.6.4.7.4.6.4.7.4.6.4.7.r.",
      "...............5...............5",
      ".......3.......4.......3....3..." },
    { "Brk_Ghost", 160, 0.58f,
      "9.......5.9.....9.......5.......",
      "....9.......9.......9.....3.9...",
      ".2.3..2..3.2..3..2.3..2...2..3.2",
      "5.3.5.3.5.3.5.3.5.3.5.3.5.3.5.3.",
      "..............4.................",
      "................................" },
    { "Brk_Stumble", 92, 0.62f,
      "9.....7..9......9..6.......7....",
      "....9.......9.......9.......9...",
      "......3......2..3.....2.....3.2.",
      "6.3.6.3.6.3.6.3.6.3.6.3.6.3.6.3.",
      "......4...............4.........",
      "...4.......3...4.......3........" },
    { "Brk_Tumble", 174, 0.5f,
      "9.......9.9.....9.......9...7...",
      "....9.......9..r....9.......9.r.",
      "..2..3....2..3..2..3....2...3...",
      "7.4.7.4.7.4.7.4.7.4.7.4.7.4.7.4.",
      "....................5...........",
      ".3...3...3...3...3...3...3...3.." },
    { "Brk_Dust", 86, 0.6f,
      "9........7.9....9.........7.....",
      "....9.......9.......9.......9...",
      "...2....2....3.....2....2..3..2.",
      "5.3.5.3.5.3.5.3.5.3.5.3.5.3.5.3.",
      "...............4...............4",
      "..........3...............3....." },
    { "Brk_Lattice", 128, 0.5f,
      "9..9..9...9..9..9..9..9...9..9..",
      "....9.......9.......9.......9.9.",
      ".2...2...2...2...2...2...2...2..",
      "rr4.7.4.7.4.7.4.rr4.7.4.7.4.7.4.",
      "..6...6...6...6...6...6...6...6.",
      "...3.......3.......3.......3...." },
};

void writeLane (TrackPattern& tp, const char* s, float velScale)
{
    tp.length = 32;
    for (int i = 0; i < 32 && s[i]; ++i)
    {
        Step& st = tp.steps[i];
        const char c = s[i];
        if (c == '.') continue;
        st.on = 1; st.len = 0.5f;
        if (c >= '1' && c <= '9') st.vel = (uint8_t) clamp ((int) ((float) (c - '0') * 14.f * velScale), 1, 127);
        else if (c == 'r') { st.vel = 90; st.ratCount = 3; st.ratVel = -0.5f; }
        else if (c == 'f') { st.vel = 110; st.ratCount = 2; st.ratCurve = RatCurve::Decel; st.ratAmt = 0.9f; st.ratVel = 0.4f; }
    }
}

Buf renderBreak (const BreakDef& d)
{
    auto p = std::make_unique<Project>();
    initProject (*p);
    p->kit.gparams[GP::Swing] = globalParamDesc (GP::Swing, nullptr).toNorm (d.swing);
    struct T { const char* lane; const char* setup[10]; float vel; };
    const T tracks[6] = {
        { d.kick,  { "model=Kick", "tune=-2", "decay=0.3", "sweep=0.45", "sweept=0.025", "snap=0.4", "dirt=0.2", "", "", "" }, 1.f },
        { d.snare, { "model=Snare", "tune=1", "decay=0.14", "tone=0.5", "noise=0.6", "ndecay=0.18", "snap=0.5", "dirt=0.15", "", "" }, 1.f },
        { d.ghost, { "model=Snare", "tune=3", "decay=0.05", "tone=0.4", "noise=0.6", "ndecay=0.06", "snap=0.1", "", "", "" }, 0.8f },
        { d.hat,   { "model=Hat", "decay=0.04", "tone=0.5", "noise=0.25", "ndecay=0.03", "", "", "", "", "" }, 0.85f },
        { d.open,  { "model=Hat", "decay=0.35", "tone=0.45", "noise=0.3", "ndecay=0.25", "", "", "", "", "" }, 0.8f },
        { d.perc,  { "model=Tom", "tune=14", "decay=0.1", "sweep=0.2", "sweept=0.01", "snap=0.4", "", "", "", "" }, 0.8f },
    };
    for (int t = 0; t < 6; ++t)
    {
        TrackSound& ts = p->kit.tracks[t];
        initTrackSound (ts, Engine::Drum);
        for (const char* kv : tracks[t].setup)
        {
            if (! kv[0]) continue;
            const char* eq = std::strchr (kv, '=');
            std::string key (kv, (size_t) (eq - kv));
            setTrackParamText (ts, key.c_str(), eq + 1);
        }
        ts.params[TP::SendA] = commonTrackParamDesc (TP::SendA).toNorm (t == 1 ? 0.35f : 0.18f);
        ts.params[TP::Pan] = commonTrackParamDesc (TP::Pan).toNorm (t == 3 ? -0.25f : (t == 5 ? 0.3f : (t == 4 ? 0.2f : 0.f)));
        writeLane (p->patterns[0].tracks[t], tracks[t].lane, tracks[t].vel);
    }
    p->patterns[0].lengthBars = 2;
    setFxType (p->kit, kFirstSendSlot, Fx::Plate);
    p->kit.gparams[fxSlotParamBase (kFirstSendSlot) + 1] = fxParamDescs (Fx::Plate)[1].toNorm (0.7f);   // short room
    p->kit.gparams[GP::SendAReturn] = 0.45f;
    p->kit.gparams[GP::LimitOn] = 1.f;

    OfflineOptions o;
    o.sampleRate = kSr; o.bpm = d.bpm; o.bars = 2.0; o.tailSeconds = 0.0; o.trimTail = false;
    Buf b;
    const SampleData* none[kTracks] = {};
    renderOffline (*p, none, o, b.l, b.r);
    // gentle tape colour and glue
    for (size_t i = 0; i < b.l.size(); ++i) { b.l[i] = fastTanh (b.l[i] * 1.3f) / 1.3f; b.r[i] = fastTanh (b.r[i] * 1.3f) / 1.3f; }
    normalise (b, -1.f);
    fadeEdges (b, 0.0005, 0.005);
    return b;
}

// ---------------------------------------------------------------- direct synthesis
Buf makeNoiseTexture (int kind, double seconds)
{
    Buf b; const int n = (int) (seconds * kSr);
    b.l.assign ((size_t) n, 0.f); b.r.assign ((size_t) n, 0.f);
    Noise nl, nr; nl.seed (11u + (uint32_t) kind); nr.seed (97u + (uint32_t) kind);
    // pink-ish noise via a set of one-pole low-passes (Paul Kellet style weighting, written from the idea)
    float pl[3] = {}, pr[3] = {};
    OnePoleLP lpL, lpR; lpL.setCutoff (kind == 0 ? 9000.f : 5000.f, (float) kSr); lpR = lpL;
    OnePoleHP hpL, hpR; hpL.setCutoff (kind == 1 ? 900.f : 60.f, (float) kSr); hpR = hpL;
    Svf bpL, bpR; bpL.setCoefs (2200.f, 0.55f, (float) kSr); bpR.setCoefs (2400.f, 0.55f, (float) kSr);
    Rng rng (41u + (uint32_t) kind);
    double crackleEnv = 0.0; float crackSign = 1.f;
    for (int i = 0; i < n; ++i)
    {
        const float wl = nl.next(), wr = nr.next();
        pl[0] = 0.997f * pl[0] + 0.03f * wl; pl[1] = 0.985f * pl[1] + 0.06f * wl; pl[2] = 0.95f * pl[2] + 0.12f * wl;
        pr[0] = 0.997f * pr[0] + 0.03f * wr; pr[1] = 0.985f * pr[1] + 0.06f * wr; pr[2] = 0.95f * pr[2] + 0.12f * wr;
        float l = pl[0] + pl[1] + pl[2] + 0.1f * wl, r = pr[0] + pr[1] + pr[2] + 0.1f * wr;
        const double t = i / kSr;
        if (kind == 0)          // tape hiss with flutter
        {
            const float flutter = 1.f + 0.15f * (float) std::sin (kTau * 0.7 * t) + 0.05f * (float) std::sin (kTau * 6.3 * t);
            l = lpL.process (hpL.process (l)) * flutter; r = lpR.process (hpR.process (r)) * flutter;
        }
        else if (kind == 1)     // vinyl: crackle impulses over a faint hiss
        {
            if (rng.chance (0.0009f)) { crackleEnv = 0.4 + 0.6 * rng.uni(); crackSign = rng.chance (0.5f) ? 1.f : -1.f; }
            const float c = (float) crackleEnv * crackSign * (0.6f + 0.4f * wl);
            crackleEnv *= 0.82;
            l = hpL.process (l * 0.15f + c); r = hpR.process (r * 0.15f + c * 0.9f);
        }
        else                    // radio static: band-passed noise chopped by a stepping gate
        {
            const int seg = (int) (t * 23.0);
            const float gate = hashToFloat (hash32 ((uint32_t) seg * 7919u)) < 0.7f ? 1.f : 0.25f;
            l = bpL.bp (wl) * gate + 0.08f * wl; r = bpR.bp (wr) * gate + 0.08f * wr;
            l += 0.2f * (float) std::sin (kTau * 1000.0 * t + 3.0 * std::sin (kTau * 2.0 * t)) * gate * 0.3f;
            r += 0.2f * (float) std::sin (kTau * 1003.0 * t + 3.0 * std::sin (kTau * 2.1 * t)) * gate * 0.3f;
        }
        b.l[(size_t) i] = l; b.r[(size_t) i] = r;
    }
    normalise (b, kind == 1 ? -3.f : -6.f);
    // loopable: crossfade the end into the start
    const int xf = (int) (0.25 * kSr);
    for (int i = 0; i < xf; ++i)
    {
        const float g = (float) i / (float) xf;
        b.l[(size_t) i] = b.l[(size_t) i] * g + b.l[(size_t) (n - xf + i)] * (1.f - g);
        b.r[(size_t) i] = b.r[(size_t) i] * g + b.r[(size_t) (n - xf + i)] * (1.f - g);
    }
    b.l.resize ((size_t) (n - xf)); b.r.resize ((size_t) (n - xf));
    return b;
}

// One cycle of a band-limited spectrum (harmonic h has amplitude amp(h)), 4096 points.
struct CycleTable
{
    std::vector<float> t;
    template <class F> CycleTable (int harmonics, F amp)
    {
        t.assign (4097, 0.f);
        for (int i = 0; i < 4096; ++i)
        {
            double s = 0.0;
            for (int h = 1; h <= harmonics; ++h) s += amp (h) * std::sin (kTau * (double) i * h / 4096.0);
            t[(size_t) i] = (float) s;
        }
        t[4096] = t[0];
    }
    float read (double ph) const
    {
        const double x = (ph - std::floor (ph)) * 4096.0;
        const int i = (int) x; const float f = (float) (x - i);
        return t[(size_t) i] + (t[(size_t) i + 1] - t[(size_t) i]) * f;
    }
};

// Formant vowel: band-limited glottal source (additive, harmonics below Nyquist) through three resonators.
Buf makeVowel (const float* formants, const float* bws, const float* gains, double seconds)
{
    Buf b; b.root = 57.f;           // A3 = 220 Hz
    const int n = (int) (seconds * kSr);
    b.l.assign ((size_t) n, 0.f); b.r.assign ((size_t) n, 0.f);
    Biquad f[3][2];
    for (int k = 0; k < 3; ++k) for (int c = 0; c < 2; ++c) f[k][c].setBandpass (formants[k] * (c ? 1.015f : 1.f), formants[k] / bws[k], (float) kSr);
    double ph[2] = { 0.0, 0.31 };
    for (int i = 0; i < n; ++i)
    {
        const double t = i / kSr;
        const double vib = 1.0 + 0.006 * std::sin (kTau * 5.2 * t) * std::min (1.0, t * 2.0);
        const float env = (float) (std::min (1.0, t / 0.08) * std::min (1.0, (seconds - t) / 0.3));
        for (int c = 0; c < 2; ++c)
        {
            const double f0 = 220.0 * vib * (c ? 1.002 : 1.0);
            ph[c] += f0 / kSr; ph[c] -= std::floor (ph[c]);
            static const CycleTable glottal (80, [] (int h) { return 1.0 / std::pow ((double) h, 1.1); });   // 80 x 222 Hz < 18 kHz
            const float src = glottal.read (ph[c]);
            float y = 0.f;
            for (int k = 0; k < 3; ++k) y += f[k][c].process (src) * gains[k];
            (c ? b.r : b.l)[(size_t) i] = y * env;
        }
    }
    normalise (b, -2.f);
    return b;
}

Buf makeTone (int kind, double seconds)
{
    Buf b; const int n = (int) (seconds * kSr);
    b.l.assign ((size_t) n, 0.f); b.r.assign ((size_t) n, 0.f);
    if (kind == 0)          // warm pad: detuned saw chord (A minor 9) through a soft low-pass, additive so it is alias-free
    {
        b.root = 57.f;
        static const float notes[5] = { 57.f, 60.f, 64.f, 67.f, 71.f };
        double ph[5][3] = {};
        OnePoleLP lpL, lpR; lpL.setCutoff (1800.f, (float) kSr); lpR = lpL;
        for (int i = 0; i < n; ++i)
        {
            const double t = i / kSr;
            float l = 0.f, r = 0.f;
            for (int v = 0; v < 5; ++v)
                for (int d = 0; d < 3; ++d)
                {
                    const double f = midiToHz (notes[v]) * std::exp2 ((d - 1) * 0.08 / 12.0 + 0.003 * std::sin (kTau * (0.3 + 0.1 * d) * t));
                    ph[v][d] += f / kSr; ph[v][d] -= std::floor (ph[v][d]);
                    static const CycleTable saw40 (40, [] (int h) { return 1.0 / h; });     // 40 x 494 Hz < 20 kHz
                    const float s = saw40.read (ph[v][d]);
                    (d == 0 ? l : (d == 2 ? r : l)) += s * (d == 1 ? 0.5f : 1.f);
                    if (d == 1) r += s * 0.5f;
                }
            const float env = (float) (std::min (1.0, t / 0.6) * std::min (1.0, (seconds - t) / 0.8));
            b.l[(size_t) i] = lpL.process (l) * env; b.r[(size_t) i] = lpR.process (r) * env;
        }
    }
    else if (kind == 1)     // FM bell, A4
    {
        b.root = 69.f;
        for (int i = 0; i < n; ++i)
        {
            const double t = i / kSr;
            const double idx = 3.0 * std::exp (-t * 2.5);
            const double m = std::sin (kTau * 440.0 * 3.5 * t) * idx;
            const float env = (float) (std::exp (-t * 1.4) * std::min (1.0, t / 0.002));
            b.l[(size_t) i] = (float) std::sin (kTau * 440.0 * t + m) * env;
            b.r[(size_t) i] = (float) std::sin (kTau * 440.0 * 1.0015 * t + m) * env;
        }
    }
    else if (kind == 2)     // plain saw, A2, band-limited
    {
        b.root = 45.f;
        double ph = 0.0;
        for (int i = 0; i < n; ++i)
        {
            ph += 110.0 / kSr; ph -= std::floor (ph);
            static const CycleTable saw180 (180, [] (int h) { return 1.0 / h; });  // 180 x 110 Hz < 20 kHz
            const float s = saw180.read (ph);
            b.l[(size_t) i] = b.r[(size_t) i] = s * 0.5f;
        }
    }
    else                    // glass: inharmonic partials, long decays
    {
        b.root = 72.f;
        static const float ratio[7] = { 1.f, 2.32f, 4.25f, 6.63f, 9.38f, 12.4f, 15.8f };
        for (int i = 0; i < n; ++i)
        {
            const double t = i / kSr;
            float l = 0.f, r = 0.f;
            for (int k = 0; k < 7; ++k)
            {
                const double f = midiToHz (72.0) * ratio[k];
                if (f > kSr * 0.45) continue;
                const float a = (float) (std::exp (-t * (0.5 + 0.6 * k)) / (1.0 + k * 0.7));
                l += a * (float) std::sin (kTau * f * t);
                r += a * (float) std::sin (kTau * f * 1.0007 * t + 0.4 * k);
            }
            const float att = (float) std::min (1.0, t / 0.003);
            b.l[(size_t) i] = l * att; b.r[(size_t) i] = r * att;
        }
    }
    normalise (b, -2.f);
    fadeEdges (b, 0.001, 0.05);
    return b;
}

Buf makeHit (int kind)
{
    Buf b;
    const double seconds = kind == 0 ? 1.2 : (kind == 1 ? 0.05 : (kind == 2 ? 0.35 : 0.25));
    const int n = (int) (seconds * kSr);
    b.l.assign ((size_t) n, 0.f); b.r.assign ((size_t) n, 0.f);
    Noise nz; nz.seed (5u + (uint32_t) kind);
    Svf bp; bp.setCoefs (kind == 3 ? 9000.f : 4000.f, 0.3f, (float) kSr);
    Svf hp; hp.setCoefs (7000.f, 0.2f, (float) kSr);
    double ph = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / kSr;
        float y = 0.f;
        if (kind == 0)          // sub kick
        {
            const double f = 42.0 + 160.0 * std::exp (-t * 30.0);
            ph += f / kSr;
            y = (float) (std::sin (kTau * ph) * std::exp (-t * 3.0)) + (t < 0.004 ? 0.3f * nz.next() * (float) (1.0 - t / 0.004) : 0.f);
        }
        else if (kind == 1)     // click
            y = bp.bp (nz.next()) * (float) std::exp (-t * 400.0) * 3.f + (i < 3 ? 0.8f : 0.f);
        else if (kind == 2)     // snare
        {
            ph += (190.0 + 60.0 * std::exp (-t * 60.0)) / kSr;
            y = (float) (std::sin (kTau * ph) * std::exp (-t * 30.0)) * 0.7f + bp.hp (nz.next()) * (float) std::exp (-t * 14.0) * 0.6f;
        }
        else                    // hat
            y = hp.hp (nz.next()) * (float) std::exp (-t * 40.0);
        b.l[(size_t) i] = b.r[(size_t) i] = y;
    }
    b.root = 60.f;
    normalise (b, -1.f);
    fadeEdges (b, 0.0, 0.004);
    return b;
}

// ---------------------------------------------------------------- table
struct Entry { FactorySampleInfo info; Buf (*make)(); };

static const float kAhF[3] = { 730.f, 1090.f, 2440.f }, kAhB[3] = { 90.f, 110.f, 160.f }, kAhG[3] = { 1.f, 0.5f, 0.25f };
static const float kOoF[3] = { 300.f, 870.f, 2240.f }, kOoB[3] = { 60.f, 100.f, 150.f }, kOoG[3] = { 1.f, 0.3f, 0.12f };
static const float kEeF[3] = { 270.f, 2290.f, 3010.f }, kEeB[3] = { 60.f, 120.f, 170.f }, kEeG[3] = { 1.f, 0.35f, 0.25f };

const Entry kEntries[] = {
    { { "Brk_Clatter", "Break" }, [] { return renderBreak (kBreaks[0]); } },
    { { "Brk_Ghost",   "Break" }, [] { return renderBreak (kBreaks[1]); } },
    { { "Brk_Stumble", "Break" }, [] { return renderBreak (kBreaks[2]); } },
    { { "Brk_Tumble",  "Break" }, [] { return renderBreak (kBreaks[3]); } },
    { { "Brk_Dust",    "Break" }, [] { return renderBreak (kBreaks[4]); } },
    { { "Brk_Lattice", "Break" }, [] { return renderBreak (kBreaks[5]); } },
    { { "Hit_SubKick", "Hit" },   [] { return makeHit (0); } },
    { { "Hit_Click",   "Hit" },   [] { return makeHit (1); } },
    { { "Hit_Snare",   "Hit" },   [] { return makeHit (2); } },
    { { "Hit_Hat",     "Hit" },   [] { return makeHit (3); } },
    { { "Tex_TapeHiss", "Texture" }, [] { return makeNoiseTexture (0, 6.0); } },
    { { "Tex_Vinyl",    "Texture" }, [] { return makeNoiseTexture (1, 6.0); } },
    { { "Tex_Static",   "Texture" }, [] { return makeNoiseTexture (2, 5.0); } },
    { { "Vox_Ah",  "Vox" }, [] { return makeVowel (kAhF, kAhB, kAhG, 2.5); } },
    { { "Vox_Ooh", "Vox" }, [] { return makeVowel (kOoF, kOoB, kOoG, 2.5); } },
    { { "Vox_Ee",  "Vox" }, [] { return makeVowel (kEeF, kEeB, kEeG, 2.5); } },
    { { "Tone_WarmPad", "Tone" }, [] { return makeTone (0, 4.0); } },
    { { "Tone_FMBell",  "Tone" }, [] { return makeTone (1, 3.0); } },
    { { "Tone_Saw",     "Tone" }, [] { return makeTone (2, 1.5); } },
    { { "Tone_Glass",   "Tone" }, [] { return makeTone (3, 4.0); } },
};
constexpr int kCount = (int) (sizeof (kEntries) / sizeof (kEntries[0]));

} // namespace

int numFactorySamples() { return kCount; }
const FactorySampleInfo& factorySampleInfo (int i) { return kEntries[clamp (i, 0, kCount - 1)].info; }

const SampleData* getFactorySample (const char* name)
{
    static std::mutex m[kCount];
    static SampleData* cache[kCount] = {};
    for (int i = 0; i < kCount; ++i)
    {
        if (std::strcmp (kEntries[i].info.name, name) != 0) continue;
        std::lock_guard<std::mutex> lock (m[i]);
        if (cache[i] == nullptr)
        {
            Buf b = kEntries[i].make();
            const float* ch[2] = { b.l.data(), b.r.data() };
            const bool stereo = b.l != b.r;
            cache[i] = makeSampleData (ch, stereo ? 2 : 1, (int) b.l.size(), b.sr, kEntries[i].info.name, b.root);
        }
        return cache[i];
    }
    return nullptr;
}

} // namespace atm
