// Drum synth: analog-style circuit models with ranges that go well past the
// originals. Six models share one voice structure:
//
//   body  : sine oscillator(s) with pitch envelope, phase-feedback shaping and
//           cross-modulation from a second oscillator
//   metal : six band-limited square oscillators (summed or ring-modulated)
//   noise : white noise through a state-variable filter with its own envelope
//   snap  : a very short broadband click
//
// All original work; the six-oscillator metallic source follows the widely
// documented TR-808 cymbal/hat topology.
#include "EngineBase.h"
#include "../model/ParamTables.h"
#include "../dsp/Filters.h"
#include "../dsp/Envelope.h"
#include "../dsp/Osc.h"

namespace atm {

namespace {
enum P { Model, Tune, Decay, Sweep, SweepT, Tone, NoiseLv, NDecay, NColor, Snap, Fm, FmRatio, Shape, Spread, Hold, Dirt };
enum M { Kick, Snare, Hat, Clap, Tom, Metal };
}

class DrumEngine final : public EngineBase
{
public:
    void prepare (double sampleRate) override
    {
        sr = (float) sampleRate;
        dc.prepare (sr, 8.f);
        reset();
    }

    void reset() override
    {
        active = false;
        bodyEnv = noiseEnv = pitchEnv = snapEnv = 0.f;
        ph1 = ph2 = phM = 0.f;
        for (auto& p : mph) p = 0.f;
        nfL.reset(); nfR.reset(); nf2L.reset(); nf2R.reset(); mbp1.reset(); mbp2.reset(); mhp.reset();
        dc.reset(); fbZ = 0.f;
        clapStage = 0; clapLeft = 0;
    }

    void noteOn (const EngineNote& n, const float* e) override
    {
        const ParamDesc* d = kDrumParams;
        model = d[Model].toIndex (e[Model]);
        vel = n.velocity;
        noiseL.seed (n.seed * 2 + 1); noiseR.seed (n.seed * 2 + 7919);
        stereo = (model == Hat || model == Clap || model == Metal || model == Snare) && d[Spread].toPhys (e[Spread]) > 0.02f;

        if (! n.legato)
        {
            // Start oscillators at a zero crossing so there is no step at the onset.
            ph1 = 0.f; ph2 = 0.f; phM = 0.f; fbZ = 0.f; dc.reset();
            for (int i = 0; i < 6; ++i) mph[i] = hashToFloat (hashN (n.seed, (uint32_t) i, 77u));
            nfL.reset(); nfR.reset(); nf2L.reset(); nf2R.reset(); mbp1.reset(); mbp2.reset(); mhp.reset();
        }
        bodyEnv = 1.f; noiseEnv = 1.f; pitchEnv = 1.f; snapEnv = 1.f;
        holdLeft = (int) (d[Hold].toPhys (e[Hold]) * sr);
        attack = 0.f;                      // 0.3 ms anti-click ramp on everything
        attackInc = 1.f / (0.0003f * sr);
        clapStage = 0; clapLeft = 0; clapGain = 1.f;
        active = true;
        samplesSinceOn = 0;
        control (e, n.note);
    }

    void control (const float* e, float note) override
    {
        const ParamDesc* d = kDrumParams;
        const float tune   = d[Tune].toPhys (e[Tune]);
        const float decay  = d[Decay].toPhys (e[Decay]);
        sweep  = d[Sweep].toPhys (e[Sweep]);
        const float sweepT = d[SweepT].toPhys (e[SweepT]);
        tone   = d[Tone].toPhys (e[Tone]);
        noiseLv = d[NoiseLv].toPhys (e[NoiseLv]);
        const float nDecay = d[NDecay].toPhys (e[NDecay]);
        const float nColor = d[NColor].toPhys (e[NColor]);
        snap   = d[Snap].toPhys (e[Snap]);
        fm     = d[Fm].toPhys (e[Fm]);
        fmRatio = d[FmRatio].toPhys (e[FmRatio]);
        shape  = d[Shape].toPhys (e[Shape]);
        spread = d[Spread].toPhys (e[Spread]);
        dirt   = d[Dirt].toPhys (e[Dirt]);

        static constexpr float baseHz[6] = { 49.f, 185.f, 410.f, 1100.f, 110.f, 525.f };
        f0 = baseHz[model] * std::exp2 ((note - 60.f + tune) * (1.f / 12.f));
        f0 = clamp (f0, 8.f, sr * 0.45f);

        bodyCoef  = std::exp (-6.9078f / std::max (8.f, decay * sr));
        noiseCoef = std::exp (-6.9078f / std::max (8.f, nDecay * sr));
        pitchCoef = std::exp (-6.9078f / std::max (8.f, sweepT * sr));
        snapCoef  = std::exp (-6.9078f / (0.004f * sr));

        // noise filter
        const float nHz = 150.f * std::pow (100.f, nColor);          // 150 Hz .. 15 kHz
        switch (model)
        {
            case Kick: case Tom:
                nfL.setCoefs (nHz, 0.2f, sr); nfR = nfL; break;
            case Snare:
                nfL.setCoefs (clamp (nHz * 1.4f, 300.f, 14000.f), 0.25f, sr);
                nfR.setCoefs (clamp (nHz * 1.4f * (1.f + 0.12f * spread), 300.f, 14000.f), 0.25f, sr);
                break;
            case Clap:
            {
                const float c = 500.f * std::pow (8.f, tone) * std::exp2 ((note - 60.f + tune) * (1.f / 12.f));
                nfL.setCoefs (clamp (c, 150.f, 12000.f), 0.62f, sr);
                nfR.setCoefs (clamp (c * (1.f + 0.2f * spread), 150.f, 12000.f), 0.62f, sr);
                nf2L.setCoefs (clamp (nHz * 2.f, 400.f, 15000.f), 0.1f, sr); nf2R = nf2L;
                break;
            }
            case Hat: case Metal:
            {
                const float hp = (model == Hat ? 3000.f : 900.f) * std::pow (4.f, tone);
                mhp.setCoefs (clamp (hp, 100.f, sr * 0.4f), 0.15f, sr);
                mbp1.setCoefs (clamp (f0 * 8.4f, 200.f, sr * 0.42f), 0.55f, sr);
                mbp2.setCoefs (clamp (f0 * 17.3f, 200.f, sr * 0.42f), 0.5f, sr);
                nfL.setCoefs (clamp (nHz * 4.f, 1000.f, sr * 0.42f), 0.1f, sr); nfR = nfL;
                break;
            }
        }
    }

    void render (float* outL, float* outR, int n) override
    {
        if (! active) { for (int i = 0; i < n; ++i) { outL[i] = 0.f; if (stereo) outR[i] = 0.f; } return; }
        switch (model)
        {
            case Kick:  renderBody (outL, n, 1.f, 0.f, 5.f); break;
            case Tom:   renderBody (outL, n, 1.f, 0.35f, 2.2f); break;
            case Snare: renderSnare (outL, outR, n); break;
            case Hat:   renderMetal (outL, outR, n, false); break;
            case Metal: renderMetal (outL, outR, n, true); break;
            case Clap:  renderClap (outL, outR, n); break;
        }
        // internal saturation + DC removal (shared)
        if (dirt > 0.001f)
        {
            const float g = 1.f + dirt * dirt * 24.f;
            const float comp = 1.f / fastTanh (std::min (g * 0.7f, 4.f)) * 0.7f;
            for (int i = 0; i < n; ++i)
            {
                outL[i] = fastTanh (outL[i] * g) * comp;
                if (stereo) outR[i] = fastTanh (outR[i] * g) * comp;
            }
        }
        samplesSinceOn += n;
        if (bodyEnv < 1.0e-4f && noiseEnv < 1.0e-4f && snapEnv < 1.0e-4f && clapStage >= 4 && samplesSinceOn > 64) active = false;
        if (model != Clap && bodyEnv < 1.0e-4f && noiseEnv < 1.0e-4f && snapEnv < 1.0e-4f && samplesSinceOn > 64) active = false;
    }

    bool isStereo() const override { return stereo; }
    bool finished() const override { return ! active; }

private:
    inline float tickAttack() { if (attack < 1.f) { attack = std::min (1.f, attack + attackInc); } return attack; }
    inline float tickBodyEnv()
    {
        const float v = bodyEnv;
        if (holdLeft > 0) --holdLeft; else bodyEnv *= bodyCoef;
        return v;
    }

    // Kick / tom: swept sine with phase feedback and cross-mod, plus noise thump and click.
    void renderBody (float* out, int n, float bodyGain, float partial2, float sweepOct)
    {
        const float inc0 = f0 / sr;
        const float depth = std::exp2 (sweep * sweepOct) - 1.f;           // ratio above f0 at t=0
        const float fbAmt = shape * shape * 1.6f;
        const float fmIdx = fm * 6.f;
        for (int i = 0; i < n; ++i)
        {
            const float pe = pitchEnv; pitchEnv *= pitchCoef;
            const float be = tickBodyEnv();
            const float inc = std::min (inc0 * (1.f + depth * pe), 0.45f);
            // modulator
            ph2 += inc * fmRatio; ph2 -= std::floor (ph2);
            const float mod = std::sin (kTauF * ph2) * fmIdx * (0.25f + 0.75f * be);
            ph1 += inc; ph1 -= std::floor (ph1);
            float s = std::sin (kTauF * ph1 + mod + fbZ * fbAmt);
            fbZ = 0.5f * (fbZ + s);                                         // averaged feedback: stable harmonics, no squeal
            if (partial2 > 0.f)
            {
                phM += inc * 1.52f; phM -= std::floor (phM);
                s += partial2 * be * std::sin (kTauF * phM);
            }
            float y = s * be * bodyGain;
            // noise thump
            const float ne = noiseEnv; noiseEnv *= noiseCoef;
            if (noiseLv > 0.f) y += nfL.lp (noiseL.next()) * ne * noiseLv * 0.9f;
            // click: a few ms of high-passed noise plus the pitch transient itself
            const float se = snapEnv; snapEnv *= snapCoef;
            if (snap > 0.f) y += (noiseR.next() * 0.5f + 0.5f * std::sin (kTauF * ph1 * 7.f)) * se * snap * 0.7f;
            out[i] = dc.process (y) * tickAttack() * 0.9f;
        }
        fbZ = undenorm (fbZ);
    }

    void renderSnare (float* outL, float* outR, int n)
    {
        const float inc0 = f0 / sr;
        const float depth = std::exp2 (sweep * 2.5f) - 1.f;
        const float fbAmt = shape * shape * 1.4f;
        const float fmIdx = fm * 5.f;
        const float m2 = 0.25f + 0.6f * tone;                 // level of the upper drum mode
        for (int i = 0; i < n; ++i)
        {
            const float pe = pitchEnv; pitchEnv *= pitchCoef;
            const float be = tickBodyEnv();
            const float inc = std::min (inc0 * (1.f + depth * pe), 0.4f);
            ph2 += inc * fmRatio; ph2 -= std::floor (ph2);
            const float mod = std::sin (kTauF * ph2) * fmIdx * be;
            ph1 += inc; ph1 -= std::floor (ph1);
            phM += inc * 1.593f; phM -= std::floor (phM);
            float s = std::sin (kTauF * ph1 + mod + fbZ * fbAmt);
            fbZ = 0.5f * (fbZ + s);
            const float body = (s + m2 * std::sin (kTauF * phM + mod * 0.5f) * be) * be * 0.7f;
            const float ne = noiseEnv; noiseEnv *= noiseCoef;
            const float se = snapEnv; snapEnv *= snapCoef;
            const float nl = noiseL.next(), nr = stereo ? noiseR.next() : nl;
            // wires: band + high mix, brighter with tone
            auto fl = nfL.tick (nl);
            const float wl = (fl.hp * (0.35f + 0.5f * tone) + fl.bp * 0.9f) * ne * noiseLv * 1.1f;
            const float click = nl * se * snap * 0.6f;
            const float a = tickAttack();
            outL[i] = (body + wl + click) * a;
            if (stereo)
            {
                auto fr = nfR.tick (nr);
                const float wr = (fr.hp * (0.35f + 0.5f * tone) + fr.bp * 0.9f) * ne * noiseLv * 1.1f;
                const float mid = 0.5f * (wl + wr), side = 0.5f * (wl - wr) * spread;
                outL[i] = (body + mid + side + click) * a;
                outR[i] = (body + mid - side + click) * a;
            }
        }
        fbZ = undenorm (fbZ);
    }

    // Hat / metal: six squares. Hat sums them; Metal ring-modulates pairs for denser, bell-like spectra.
    void renderMetal (float* outL, float* outR, int n, bool ring)
    {
        static constexpr float ratios[6] = { 1.f, 1.3420f, 1.2312f, 1.6532f, 1.9523f, 2.1523f };
        float incs[6];
        const float sp = 0.4f + spread * 1.6f;                 // 1.0 at spread 0.375 = the classic tuning
        for (int k = 0; k < 6; ++k)
            incs[k] = std::min (f0 * (1.f + (ratios[k] - 1.f) * sp) / sr, 0.49f);
        const float depth = std::exp2 (sweep * 3.f) - 1.f;
        const float fmIdx = fm * 0.9f;
        const float nl = noiseLv * 0.8f;
        for (int i = 0; i < n; ++i)
        {
            const float pe = pitchEnv; pitchEnv *= pitchCoef;
            const float be = tickBodyEnv();
            const float ne = noiseEnv; noiseEnv *= noiseCoef;
            const float se = snapEnv; snapEnv *= snapCoef;
            const float pm = 1.f + depth * pe;
            float sq[6];
            // cross-mod: oscillator 6 bends the others' frequencies
            ph2 += std::min (incs[0] * fmRatio * pm, 0.49f); ph2 -= std::floor (ph2);
            const float fmod = 1.f + fmIdx * std::sin (kTauF * ph2);
            for (int k = 0; k < 6; ++k)
            {
                const float inc = clamp (incs[k] * pm * fmod, 1.0e-5f, 0.49f);
                mph[k] += inc; mph[k] -= std::floor (mph[k]);
                sq[k] = pulseBlep (mph[k], inc, 0.5f);
            }
            float m;
            if (ring) m = (sq[0] * sq[1] + sq[2] * sq[3] + sq[4] * sq[5]) * 0.45f + shape * (sq[0] * sq[3] * sq[4]) * 0.5f;
            else      m = (sq[0] + sq[1] + sq[2] + sq[3] + sq[4] + sq[5]) * 0.22f + shape * (sq[0] * sq[2] + sq[1] * sq[4]) * 0.3f;
            const float b = mbp1.bp (m) + 0.8f * mbp2.bp (m);
            float y = mhp.hp (b * (ring ? 1.3f : 1.6f) + m * (ring ? 0.35f : 0.1f));
            y *= be;
            const float nz = noiseL.next();
            const float nzf = nfL.hp (nz);
            y += nzf * ne * nl * 0.5f + nz * se * snap * 0.35f;
            const float a = tickAttack();
            outL[i] = y * a;
            if (stereo)
            {
                // decorrelate with the second noise source and a slightly different band mix
                const float nzr = nfR.hp (noiseR.next());
                const float yr = y + (nzr - nzf) * ne * nl * 0.5f * spread;
                outR[i] = yr * a;
            }
        }
    }

    // Clap: band-passed noise shaped by a train of short bursts, then a diffuse tail.
    void renderClap (float* outL, float* outR, int n)
    {
        const float gap = (0.004f + 0.028f * spread) * sr;        // burst spacing
        const float burstCoef = std::exp (-6.9078f / std::max (8.f, 0.009f * sr + gap * 0.35f));
        for (int i = 0; i < n; ++i)
        {
            if (clapLeft <= 0 && clapStage < 4)
            {
                // deterministic slight irregularity in the spacing
                const float j = 1.f + 0.25f * hashToBipolar (hashN (noiseL.s, (uint32_t) clapStage));
                clapLeft = (clapStage < 3) ? (int) (gap * j) : 0x7fffffff;
                clapEnv = 1.f; clapGain = (clapStage == 0) ? 1.f + snap : (clapStage < 3 ? 0.85f : 0.9f);
                ++clapStage;
            }
            --clapLeft;
            const float be = tickBodyEnv();
            float env;
            if (clapStage < 4) { env = clapEnv * clapGain; clapEnv *= burstCoef; }
            else env = be * clapGain;                           // tail
            const float ne = noiseEnv; noiseEnv *= noiseCoef;
            const float nl = noiseL.next(), nr = noiseR.next();
            const float a = tickAttack();
            const float l = nfL.bp (nl) * env * 2.2f + nf2L.hp (nl) * ne * noiseLv * 0.5f;
            outL[i] = l * a;
            if (stereo)
            {
                const float r = nfR.bp (lerp (nl, nr, spread)) * env * 2.2f + nf2R.hp (nr) * ne * noiseLv * 0.5f;
                outR[i] = r * a;
            }
        }
        if (clapStage < 4) bodyEnv = 1.f;      // the tail's decay starts after the last burst
        snapEnv = 0.f;
    }

    float sr = 48000.f;
    int   model = Kick;
    bool  active = false, stereo = false;
    float vel = 1.f;
    float f0 = 50.f, sweep = 0, tone = 0.5f, noiseLv = 0, snap = 0, fm = 0, fmRatio = 1.5f, shape = 0, spread = 0, dirt = 0;
    float bodyEnv = 0, noiseEnv = 0, pitchEnv = 0, snapEnv = 0;
    float bodyCoef = 0.999f, noiseCoef = 0.999f, pitchCoef = 0.99f, snapCoef = 0.99f;
    int   holdLeft = 0, samplesSinceOn = 0;
    float attack = 1.f, attackInc = 0.1f;
    float ph1 = 0, ph2 = 0, phM = 0, fbZ = 0;
    float mph[6] = {};
    int   clapStage = 0, clapLeft = 0; float clapEnv = 0, clapGain = 1;
    Noise noiseL, noiseR;
    Svf   nfL, nfR, nf2L, nf2R, mbp1, mbp2, mhp;
    DcBlocker dc;
};

std::unique_ptr<EngineBase> createDrumEngine() { return std::make_unique<DrumEngine>(); }

} // namespace atm
