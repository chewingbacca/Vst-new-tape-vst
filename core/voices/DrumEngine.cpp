// Drum synth: analog-style circuit models with ranges that go well past the originals.
//
//   Kick   : swept sine (808/909 bridged-T style), optional fold / cross-FM, a filtered noise
//            burst click (909 style) and a low noise thump.
//   Snare  : two drum-head modes (1 : 1.59, the upper one decaying faster) plus "wires":
//            band + high-passed noise with its own envelope, and a stick click.
//   Hat    : six detuned square oscillators at the TR-808 metallic ratios, summed, through a
//            band-pass and a high-pass. Closed vs open is the decay time.
//   Clap   : band-passed noise gated by a train of short bursts, then a diffuse tail.
//   Tom    : swept sine with a second membrane partial (x1.594) that decays faster.
//   Metal  : the same six squares ring-modulated in pairs (XOR), through two band-passes
//            (cymbal / cowbell family).
//
//   Every model: velocity adds a little click, brightness and pitch sweep; the output goes
//   through optional saturation ("dirt") and a soft knee that holds extreme settings below
//   about +5 dBFS.
//
// All original work. The six-oscillator metallic source follows the widely documented TR-808
// cymbal / hi-hat topology (oscillator ratios measured from the service-manual frequencies);
// the clap is the familiar "noise through a re-triggered envelope, then reverb tail" design.
#include "EngineBase.h"
#include "../model/ParamTables.h"
#include "../dsp/Filters.h"
#include "../dsp/Osc.h"

namespace atm {

namespace {
enum P { Model, Tune, Decay, Sweep, SweepT, Tone, NoiseLv, NDecay, NColor, Snap, Fm, FmRatio, Shape, Spread, Hold, Dirt };
enum M { Kick, Snare, Hat, Clap, Tom, Metal };

// Natural pitch of each model at MIDI note 60 (hat / metal: the lowest square oscillator).
constexpr float kBaseHz[6] = { 50.f, 180.f, 205.3f, 1000.f, 130.f, 205.3f };
// Output trims that bring every model to about -3 dBFS peak at default settings.
constexpr float kModelGain[6] = { 0.70f, 0.37f, 3.00f, 0.55f, 0.48f, 0.88f };
// TR-808 metallic oscillators: 205.3, 304.4, 369.6, 522.7, 540, 800 Hz.
constexpr float kMetalRatio[6] = { 1.f, 1.4827f, 1.8003f, 2.5460f, 2.6303f, 3.8967f };

inline float softLimit (float x)
{
    const float a = std::fabs (x);
    if (a <= 1.f) return x;
    const float y = 1.f + 0.8f * fastTanh ((a - 1.f) * 1.25f);
    return x < 0.f ? -y : y;
}

// tan(x) for 0 <= x <= ~0.65 (cutoffs up to 0.2 * sr), < 0.5 % error: per-sample filter sweeps.
inline float tanApprox (float x)
{
    const float x2 = x * x;
    return x * (1.f + x2 * (0.33333f + x2 * (0.13333f + x2 * 0.053968f)));
}

// Sine folded towards odd harmonics: amount 0 = pure sine.
inline float foldShape (float s, float amount)
{
    if (amount <= 0.f) return s;
    return s + amount * (std::sin (kPiF * 0.5f * (1.f + 4.f * amount) * s) - s);
}

inline float decayCoef (float seconds, float sr) { return std::exp (-6.9078f / std::max (4.f, seconds * sr)); }
}

class DrumEngine final : public EngineBase
{
public:
    void prepare (double sampleRate) override
    {
        sr = (float) sampleRate;
        dcL.prepare (sr, 6.f); dcR.prepare (sr, 6.f);
        reset();
    }

    void reset() override
    {
        active = false;
        bodyEnv = body2Env = noiseEnv = pitchEnv = snapEnv = clapEnv = 0.f;
        clearState();
    }

    void noteOn (const EngineNote& n, const float* e) override
    {
        const ParamDesc* d = kDrumParams;
        model = d[Model].toIndex (e[Model]);
        vel = clamp01 (n.velocity);
        noiseL.seed (n.seed * 2u + 1u); noiseR.seed (n.seed * 2u + 7919u);
        stereo = (model == Hat || model == Clap || model == Metal || model == Snare) && d[Spread].toPhys (e[Spread]) > 0.02f;

        if (! n.legato || ! active)
        {
            clearState();
            // squares start at seeded phases (like free-running analog oscillators); sines at 0
            for (int i = 0; i < 6; ++i) mph[i] = hashToFloat (hashN (n.seed, (uint32_t) i, 77u));
            clapJitterSeed = hashN (n.seed, 0xc1a9u);
        }
        bodyEnv = 1.f; noiseEnv = 1.f; pitchEnv = 1.f; snapEnv = 1.f;
        body2Env = (model == Snare || model == Tom) ? 1.f : 0.f;            // second head mode
        holdLeft = (int) (d[Hold].toPhys (e[Hold]) * sr);
        attack = 0.f;
        attackInc = 1.f / std::max (1.f, 0.0003f * sr);            // 0.3 ms anti-click ramp
        clapStage = 0; clapLeft = 0; clapEnv = 0.f; clapGain = 0.f;
        clapBursts = 3 + (int) std::lround (d[Shape].toPhys (e[Shape]) * 4.f);   // bursts before the tail (3..7)
        age = 0;
        active = true;
        control (e, n.note);
    }

    void control (const float* e, float note) override
    {
        const ParamDesc* d = kDrumParams;
        const float tune   = d[Tune].toPhys (e[Tune]);
        const float decay  = d[Decay].toPhys (e[Decay]);
        const float sweepT = d[SweepT].toPhys (e[SweepT]);
        const float nDecay = d[NDecay].toPhys (e[NDecay]);
        const float nColor = d[NColor].toPhys (e[NColor]);
        const float vk = vel;

        sweep   = d[Sweep].toPhys (e[Sweep]) * (0.75f + 0.25f * vk);
        tone    = clamp01 (d[Tone].toPhys (e[Tone]) - 0.15f * (1.f - vk));
        noiseLv = d[NoiseLv].toPhys (e[NoiseLv]);
        snap    = d[Snap].toPhys (e[Snap]) * (0.45f + 0.55f * vk);
        fm      = d[Fm].toPhys (e[Fm]);
        fmRatio = d[FmRatio].toPhys (e[FmRatio]);
        shape   = d[Shape].toPhys (e[Shape]);
        spread  = d[Spread].toPhys (e[Spread]);
        dirt    = d[Dirt].toPhys (e[Dirt]);

        const float pitchRatio = std::exp2 ((note - 60.f + tune) * (1.f / 12.f));
        f0 = clamp (kBaseHz[model] * pitchRatio, 8.f, sr * 0.45f);

        bodyCoef  = decayCoef (decay, sr);
        noiseCoef = decayCoef (nDecay, sr);
        pitchCoef = decayCoef (sweepT, sr);
        const float nHz = 150.f * std::pow (100.f, nColor);          // 150 Hz .. 15 kHz
        const float ny = sr * 0.45f;

        switch (model)
        {
            case Kick: case Tom:
            {
                // click: noise burst, 2.5 ms (kick) / 4 ms (tom), brighter with tone
                snapCoef = decayCoef (model == Kick ? 0.0025f : 0.004f, sr);
                clickF.setCoefs (clamp (700.f * std::pow (20.f, tone), 200.f, ny), 0.35f, sr);
                nfL.setCoefs (clamp (nHz, 40.f, ny), 0.15f, sr);
                body2Coef = decayCoef (decay * 0.45f, sr);
                break;
            }
            case Snare:
            {
                snapCoef = decayCoef (0.006f, sr);
                bodyCoef  = decayCoef (decay * 0.6f, sr);                // heads ring shorter than the wires
                body2Coef = decayCoef (decay * 0.35f, sr);
                const float w = clamp (nHz * 1.4f, 400.f, ny);
                nfL.setCoefs (w, 0.2f, sr);
                nfR.setCoefs (clamp (w * (1.f + 0.15f * spread), 400.f, ny), 0.2f, sr);
                clickF.setCoefs (clamp (2500.f * std::pow (3.f, tone), 500.f, ny), 0.2f, sr);
                break;
            }
            case Hat: case Metal:
            {
                snapCoef = decayCoef (0.003f, sr);
                const float tf = std::exp2 ((tone - 0.5f) * 2.f);       // tone: +-1 octave on the filters
                if (model == Hat)
                {
                    mbp1.setCoefs (clamp (8000.f * tf, 500.f, ny), 0.45f, sr);
                    mbp2.setCoefs (clamp (11000.f * tf, 500.f, ny), 0.4f, sr);
                    mhp.setCoefs  (clamp (5500.f * tf, 200.f, ny), 0.1f, sr);
                    mbp1R.setCoefs (clamp (8000.f * tf * (1.f + 0.12f * spread), 500.f, ny), 0.45f, sr);
                }
                else
                {
                    mbp1.setCoefs (clamp (3440.f * tf, 300.f, ny), 0.55f, sr);
                    mbp2.setCoefs (clamp (7100.f * tf, 300.f, ny), 0.5f, sr);
                    mhp.setCoefs  (clamp (1400.f * tf, 100.f, ny), 0.1f, sr);
                    mbp1R.setCoefs (clamp (3440.f * tf * (1.f + 0.12f * spread), 300.f, ny), 0.55f, sr);
                }
                mhpR.setGK (mhp.g, mhp.k);                              // coefficients only, never state
                nfL.setCoefs (clamp (nHz * 4.f, 1500.f, ny), 0.1f, sr);
                nfR.setGK (nfL.g, nfL.k);
                clickF.setCoefs (clamp (6000.f * tf, 1000.f, ny), 0.1f, sr);
                break;
            }
            case Clap:
            {
                clapHz = 1000.f * std::exp2 ((tone - 0.5f) * 3.f) * pitchRatio;   // 350 Hz .. 2.8 kHz at note 60
                nfL.setCoefs (clamp (nHz * 2.f, 300.f, ny), 0.1f, sr);
                nfR.setGK (nfL.g, nfL.k);
                break;
            }
        }
    }

    void render (float* outL, float* outR, int n) override
    {
        // Work in pieces that end on a 32-sample grid counted from note-on, and decide whether the
        // note has ended only on that grid, so the result does not depend on the host's slicing.
        int done = 0;
        while (done < n && active)
        {
            const int seg = std::min (n - done, 32 - (age & 31));
            renderModel (outL + done, outR + done, seg);
            age += seg;
            done += seg;
            if ((age & 31) == 0 && age > 64)
            {
                const float th = 2.0e-5f;
                const bool quiet = bodyEnv < th && body2Env < th && noiseEnv < th && snapEnv < th;
                if (quiet && (model != Clap || clapStage > clapBursts)) active = false;
            }
        }
        for (int i = done; i < n; ++i) { outL[i] = 0.f; if (stereo) outR[i] = 0.f; }
    }

    bool isStereo() const override { return stereo; }
    bool finished() const override { return ! active; }

private:
    void renderModel (float* outL, float* outR, int n)
    {
        switch (model)
        {
            case Kick:  renderBody (outL, n, false); break;
            case Tom:   renderBody (outL, n, true); break;
            case Snare: renderSnare (outL, outR, n); break;
            case Hat:   renderMetal (outL, outR, n, false); break;
            case Metal: renderMetal (outL, outR, n, true); break;
            case Clap:  renderClap (outL, outR, n); break;
            default:    for (int i = 0; i < n; ++i) { outL[i] = 0.f; if (stereo) outR[i] = 0.f; } break;
        }

        // output: trim, saturation, DC removal, soft knee
        const float g = kModelGain[model];
        const float drive = 1.f + dirt * dirt * 24.f;
        const float comp = dirt > 0.001f ? 0.7f / fastTanh (std::min (drive * 0.7f, 4.f)) : 1.f;
        for (int i = 0; i < n; ++i)
        {
            float l = outL[i] * g;
            if (dirt > 0.001f) l = fastTanh (l * drive) * comp;
            outL[i] = softLimit (dcL.process (l));
            if (stereo)
            {
                float r = outR[i] * g;
                if (dirt > 0.001f) r = fastTanh (r * drive) * comp;
                outR[i] = softLimit (dcR.process (r));
            }
        }
    }

    void clearState()
    {
        ph1 = ph2 = ph3 = 0.f;
        for (auto& p : mph) p = 0.f;
        for (Svf* f : { &nfL, &nfR, &clickF, &mbp1, &mbp2, &mhp, &mbp1R, &mhpR, &clapL, &clapR }) f->reset();
        dcL.reset(); dcR.reset();
        clapStage = 0; clapLeft = 0;
    }

    inline float tickAttack() { if (attack < 1.f) attack = std::min (1.f, attack + attackInc); return attack; }
    inline float tickBodyEnv()
    {
        const float v = bodyEnv;
        if (holdLeft > 0) --holdLeft; else bodyEnv *= bodyCoef;
        return v;
    }
    inline float tick (float& env, float coef) { const float v = env; env *= coef; return v; }

    // Kick / tom: swept sine with fold and cross-FM, second partial (tom), click and noise thump.
    void renderBody (float* out, int n, bool tom)
    {
        const float inc0 = f0 / sr;
        const float depth = std::exp2 (sweep * (tom ? 2.f : 5.f)) - 1.f;      // frequency ratio above f0 at t = 0
        const float fmIdx = fm * 6.f;
        const float p2 = tom ? 0.2f + 0.6f * tone : 0.f;
        const float clickLv = snap * (tom ? 3.f : 5.f);
        const float thump = noiseLv * (tom ? 1.2f : 1.0f);
        for (int i = 0; i < n; ++i)
        {
            const float pe = tick (pitchEnv, pitchCoef);
            const float be = tickBodyEnv();
            const float inc = std::min (inc0 * (1.f + depth * pe), 0.45f);
            ph2 += std::min (inc * fmRatio, 0.45f); ph2 -= std::floor (ph2);
            const float mod = fmIdx * std::sin (kTauF * ph2) * (0.3f + 0.7f * be);
            ph1 += inc; ph1 -= std::floor (ph1);
            float y = foldShape (std::sin (kTauF * ph1 + mod), shape) * be;
            if (tom)
            {
                const float b2 = tick (body2Env, body2Coef);
                ph3 += std::min (inc * 1.594f, 0.45f); ph3 -= std::floor (ph3);
                y += p2 * b2 * std::sin (kTauF * ph3 + 0.6f * mod);
            }
            else body2Env = 0.f;
            const float ne = tick (noiseEnv, noiseCoef);
            const float se = tick (snapEnv, snapCoef);
            const float nz = noiseL.next();
            if (thump > 0.f) y += nfL.lp (nz) * ne * thump;
            if (clickLv > 0.f)
            {
                const auto c = clickF.tick (noiseR.next());
                y += (c.bp * 1.6f + c.lp * 0.6f) * se * clickLv;
            }
            out[i] = y * tickAttack();
        }
    }

    // Snare: two head modes + wires + stick click.
    void renderSnare (float* outL, float* outR, int n)
    {
        const float inc0 = f0 / sr;
        const float depth = std::exp2 (sweep * 1.5f) - 1.f;
        const float fmIdx = fm * 5.f;
        const float m2 = 0.3f + 0.6f * tone;                        // upper head mode
        const float wire = 2.6f * std::sqrt (noiseLv);              // "snappy"
        const float hpMix = 0.25f + 0.75f * tone, bpMix = 1.f;
        const float clickLv = snap * 0.9f;
        for (int i = 0; i < n; ++i)
        {
            const float pe = tick (pitchEnv, pitchCoef);
            const float be = tickBodyEnv();
            const float b2 = tick (body2Env, body2Coef);
            const float inc = std::min (inc0 * (1.f + depth * pe), 0.4f);
            ph2 += std::min (inc * fmRatio, 0.45f); ph2 -= std::floor (ph2);
            const float mod = std::sin (kTauF * ph2) * fmIdx * be;
            ph1 += inc; ph1 -= std::floor (ph1);
            ph3 += std::min (inc * 1.59f, 0.45f); ph3 -= std::floor (ph3);
            const float body = foldShape (std::sin (kTauF * ph1 + mod), shape) * be
                             + m2 * b2 * std::sin (kTauF * ph3 + 0.5f * mod);
            const float ne = tick (noiseEnv, noiseCoef);
            const float se = tick (snapEnv, snapCoef);
            const float nl = noiseL.next();
            const auto fl = nfL.tick (nl);
            const float wl = (fl.hp * hpMix + fl.bp * bpMix) * ne * wire;
            const float click = clickF.hp (noiseR.next()) * se * clickLv;
            const float a = tickAttack();
            if (stereo)
            {
                const auto fr = nfR.tick (noiseR.next());
                const float wr = (fr.hp * hpMix + fr.bp * bpMix) * ne * wire;
                const float mid = 0.5f * (wl + wr), side = 0.5f * (wl - wr) * spread;
                outL[i] = (body + mid + side + click) * a;
                outR[i] = (body + mid - side + click) * a;
            }
            else outL[i] = (body + wl + click) * a;
        }
    }

    // Hat / metal: six squares. Hat sums them; metal ring-modulates pairs.
    void renderMetal (float* outL, float* outR, int n, bool ring)
    {
        float incs[6];
        const float sp = 0.4f + 2.f * spread;                       // 1.0 at the default spread 0.3 = 808 tuning
        for (int k = 0; k < 6; ++k) incs[k] = f0 * (1.f + (kMetalRatio[k] - 1.f) * sp) / sr;
        const float depth = std::exp2 (sweep * (ring ? 1.f : 0.5f)) - 1.f;
        const float fmIdx = fm * 0.9f;
        const float nl = noiseLv * (ring ? 0.5f : 0.4f);
        const float clickLv = snap * 0.6f;
        const float bp2Lv = ring ? 0.7f : 0.5f;
        for (int i = 0; i < n; ++i)
        {
            const float pe = tick (pitchEnv, pitchCoef);
            const float be = tickBodyEnv();
            const float ne = tick (noiseEnv, noiseCoef);
            const float se = tick (snapEnv, snapCoef);
            const float pm = 1.f + depth * pe;
            // cross-mod: an extra sine at fmRatio x the lowest square bends all six
            ph2 += std::min (incs[0] * fmRatio * pm, 0.45f); ph2 -= std::floor (ph2);
            const float fmod = 1.f + fmIdx * std::sin (kTauF * ph2);
            float sq[6];
            for (int k = 0; k < 6; ++k)
            {
                const float inc = clamp (incs[k] * pm * fmod, 1.0e-5f, 0.45f);
                mph[k] += inc; mph[k] -= std::floor (mph[k]);
                sq[k] = pulseBlep (mph[k], inc, 0.5f);
            }
            float m;
            if (ring)
            {
                const float pairs = (sq[0] * sq[1] + sq[2] * sq[3] + sq[4] * sq[5]) * (1.f / 3.f);
                const float group = (sq[0] + sq[2] + sq[4]) * (sq[1] + sq[3] + sq[5]) * (1.f / 9.f);   // denser cross products
                m = pairs + shape * (group - pairs);
            }
            else
            {
                const float sum = (sq[0] + sq[1] + sq[2] + sq[3] + sq[4] + sq[5]) * (1.f / 6.f);
                const float xr = (sq[0] * sq[3] + sq[1] * sq[4] + sq[2] * sq[5]) * (1.f / 3.f);
                m = sum + shape * (xr - sum);
            }
            const float b = mbp1.bp (m) + bp2Lv * mbp2.bp (m);
            const float y = mhp.hp (b) * be;
            const float nzL = noiseL.next();
            const float nfl = nfL.hp (nzL);
            const float click = clickF.hp (nzL) * se * clickLv;
            const float a = tickAttack();
            outL[i] = (y + nfl * ne * nl + click) * a;
            if (stereo)
            {
                // decorrelated noise layer, and the metal slightly re-filtered for width
                const float nfr = nfR.hp (noiseR.next());
                const float yr = y + spread * 0.5f * (mhpR.hp (mbp1R.bp (m)) * be - y * 0.5f);
                outR[i] = (yr + (nfl + spread * (nfr - nfl)) * ne * nl + click) * a;
            }
        }
    }

    // Clap: band-passed noise shaped by a train of bursts, then a diffuse tail.
    void renderClap (float* outL, float* outR, int n)
    {
        const float gap = (0.004f + 0.026f * spread) * sr;          // burst spacing
        const float burstCoef = decayCoef (4.f * gap / sr, sr);      // each burst falls ~15 dB before the next
        const float depth = std::exp2 (sweep * 2.f) - 1.f;           // band-pass sweeps down from up to 4x
        const float k = 2.f - 1.98f * 0.6f;
        const float ny = sr * 0.2f;
        const float fmInc = std::min (clapHz * 0.25f * fmRatio / sr, 0.45f);      // FM: ring modulation of the bursts
        for (int i = 0; i < n; ++i)
        {
            if (clapLeft <= 0 && clapStage <= clapBursts)
            {
                const float j = 1.f + 0.2f * hashToBipolar (hashN (clapJitterSeed, (uint32_t) clapStage));
                clapLeft = clapStage < clapBursts ? std::max (1, (int) (gap * j)) : 0x7fffffff;
                clapEnv = 1.f;
                clapGain = clapStage == 0 ? 0.8f + snap : 1.f;
                if (clapStage == clapBursts) bodyEnv = 1.f;             // the tail starts here
                ++clapStage;
            }
            --clapLeft;
            const float pe = tick (pitchEnv, pitchCoef);
            float env;
            if (clapStage <= clapBursts) { env = clapEnv * clapGain; clapEnv *= burstCoef; }
            else env = 0.8f * tickBodyEnv();
            const float ne = tick (noiseEnv, noiseCoef);
            snapEnv = 0.f;
            const float hz = std::min (clapHz * (1.f + depth * pe), ny);
            const float g = tanApprox (kPiF * hz / sr);
            clapL.setGK (g, k);
            const float nl = noiseL.next(), nr = noiseR.next();
            float a = tickAttack();
            if (fm > 0.f)
            {
                ph2 += fmInc; ph2 -= std::floor (ph2);
                a *= 1.f + fm * (std::sin (kTauF * ph2) - 1.f) * 0.9f;
            }
            outL[i] = (clapL.bp (nl) * env * 2.2f + nfL.hp (nl) * ne * noiseLv * 0.6f) * a;
            if (stereo)
            {
                clapR.setGK (g * (1.f + 0.12f * spread), k);
                outR[i] = (clapR.bp (lerp (nl, nr, spread)) * env * 2.2f + nfR.hp (lerp (nl, nr, spread)) * ne * noiseLv * 0.6f) * a;
            }
        }
    }

    float sr = 48000.f;
    int   model = Kick;
    bool  active = false, stereo = false;
    float vel = 1.f;
    float f0 = 50.f, sweep = 0, tone = 0.5f, noiseLv = 0, snap = 0, fm = 0, fmRatio = 1.5f, shape = 0, spread = 0, dirt = 0;
    float clapHz = 1000.f;
    float bodyEnv = 0, body2Env = 0, noiseEnv = 0, pitchEnv = 0, snapEnv = 0;
    float bodyCoef = 0.999f, body2Coef = 0.999f, noiseCoef = 0.999f, pitchCoef = 0.99f, snapCoef = 0.99f;
    int   holdLeft = 0, age = 0;
    float attack = 1.f, attackInc = 0.1f;
    float ph1 = 0, ph2 = 0, ph3 = 0;
    float mph[6] = {};
    int   clapStage = 0, clapLeft = 0, clapBursts = 3; float clapEnv = 0, clapGain = 0;
    uint32_t clapJitterSeed = 1;
    Noise noiseL, noiseR;
    Svf   nfL, nfR, clickF, mbp1, mbp2, mhp, mbp1R, mhpR, clapL, clapR;
    DcBlocker dcL, dcR;
};

std::unique_ptr<EngineBase> createDrumEngine() { return std::make_unique<DrumEngine>(); }

} // namespace atm
