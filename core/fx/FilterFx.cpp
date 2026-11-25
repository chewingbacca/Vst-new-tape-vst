// Filter: multimode filter with drive, LFO, envelope follower and stereo cutoff offset.
//
//   in -> drive (2 tanh (g x / 2), level trimmed by g^-0.7) -> filter -> soft limiter (clean below
//   +-1.5, never above +-4)
//
//   LP12 / BP / HP12 / Notch / Peak   one TPT state-variable filter (Zavalishin / Simper form); the
//          response is a smoothed mix of its outputs, so these types can be switched without clicks
//   LP24   the saturating 4-pole ladder from dsp/Filters.h (resonance kept below self-oscillation)
//   HP24   two cascaded SVF high-passes (Butterworth pair at res 0, resonance on the second)
//
//   Q = 0.707 * (25 / 0.707)^res. LP12 / HP12 are trimmed by (Q / 0.707)^-0.25 so resonance does
//   not only add level; BP has gain 1.19 sqrt (Q) at the cutoff (unity at res 0); Peak is a bell of
//   +18 dB * res with Q = 1 + 7 res.
//
// Cutoff = knob + LFO (sine, +-4 oct * depth) + follower (sqrt of the input peak envelope,
// +-4 oct * follow) -/+ spread octaves (left lower, right higher: 2 * spread octaves apart).
// The sum is smoothed in the pitch domain (2 ms) every 8 samples and the integrator gain is
// interpolated per sample, so sweeps and parameter-lock jumps are free of zipper noise.
#include "Effect.h"
#include "../model/ParamTables.h"
#include "../dsp/Filters.h"

namespace atm {
namespace {

class FilterFx final : public Effect
{
public:
    enum { Type, Cutoff, Res, Drive, LfoRate, LfoDepth, Follow, Spread };
    enum { LP12, LP24, BP, HP12, HP24, Notch, Peak };
    static constexpr int kCtl = 8;

    void prepare (double sampleRate) override
    {
        sr = (float) sampleRate;
        atk = onePoleCoef (0.003f, sr); rel = onePoleCoef (0.12f, sr);
        coef = onePoleCoef (0.003f, sr);
        pitchCoef = 1.f - std::exp (-(float) kCtl / (0.002f * sr));
        reset();
    }

    void reset() override
    {
        for (int c = 0; c < 2; ++c) { a[c].reset(); b[c].reset(); lad[c].reset(); }
        env = 0.f; lfoPh = 0.0; ctl = 0; snap = true; curType = -1;
    }

    void setParams (const float* p, const FxContext&) override
    {
        type = kFxFilter[Type].toIndex (p[Type]);
        if (p[Cutoff] != last[Cutoff]) { last[Cutoff] = p[Cutoff]; pitchT = std::log2 (kFxFilter[Cutoff].toPhys (p[Cutoff])); }
        if (p[Res] != last[Res] || type != lastType)
        {
            last[Res] = p[Res]; lastType = type;
            const float res = clamp01 (p[Res]);
            const float q = 0.70711f * std::pow (25.f / 0.70711f, res);
            const float trim = std::pow (q / 0.70711f, -0.25f);
            kT = 1.f / q; k2T = 1.f; postT = 1.f;
            mixT[0] = mixT[1] = mixT[2] = mixT[3] = 0.f;                 // in, lp, bp, hp
            switch (type)
            {
                case LP12:  mixT[1] = trim; break;
                case HP12:  mixT[3] = trim; break;
                case BP:    mixT[2] = 1.19f * std::sqrt (kT); break;
                case Notch: mixT[0] = 1.f; mixT[2] = -kT; break;
                case Peak:  kT = 1.f / (1.f + 7.f * res); mixT[0] = 1.f; mixT[2] = (dbToGain (18.f * res) - 1.f) * kT; break;
                case HP24:
                {
                    const float q2 = 1.3066f * std::pow (25.f / 1.3066f, res);
                    kT = 1.f / 0.5412f; k2T = 1.f / q2; mixT[3] = 1.f; postT = std::pow (q2 / 1.3066f, -0.25f);
                    break;
                }
                default: break;                                          // LP24: the ladder
            }
            ladK = 3.6f * res;
        }
        if (p[Drive] != last[Drive])
        {
            last[Drive] = p[Drive];
            const float g = dbToGain (kFxFilter[Drive].toPhys (p[Drive]));
            driveT = 0.5f * g; trimT = 2.f * std::pow (g, -0.7f);
        }
        if (p[LfoRate] != last[LfoRate]) { last[LfoRate] = p[LfoRate]; lfoInc = (double) kFxFilter[LfoRate].toPhys (p[LfoRate]) * kCtl / (double) sr; }
        lfoOct    = 4.f * clamp01 (p[LfoDepth]);
        followOct = 4.f * kFxFilter[Follow].toPhys (p[Follow]);
        spreadT   = kFxFilter[Spread].toPhys (p[Spread]);
        if (snap)
        {
            snap = false; prime = true;
            pitch = pitchT; spread = spreadT; k = kT; k2 = k2T; post = postT; drive = driveT; trim = trimT;
            for (int i = 0; i < 4; ++i) mix[i] = mixT[i];
        }
    }

    void process (float* l, float* r, int n, const FxContext&) override
    {
        if (type != curType)
        {
            if (type == LP24) { lad[0].reset(); lad[1].reset(); }
            if (type == HP24) { b[0].reset(); b[1].reset(); }
            curType = type;
        }
        const int t = curType;
        float* io[2] = { l, r };
        for (int i = 0; i < n; ++i)
        {
            const float in[2] = { l[i], r[i] };
            const float pk = std::max (std::fabs (in[0]), std::fabs (in[1]));
            env = undenorm (env + (pk > env ? atk : rel) * (pk - env));

            if (ctl == 0)
            {
                const float lfo = (float) std::sin (kTau * lfoPh);
                lfoPh += lfoInc; lfoPh -= std::floor (lfoPh);
                const float target = pitchT + lfoOct * lfo + followOct * std::sqrt (std::min (1.f, env));
                pitch  += pitchCoef * (target - pitch);
                spread += pitchCoef * (spreadT - spread);
                const float top = std::min (20000.f, 0.45f * sr);
                for (int c = 0; c < 2; ++c)
                {
                    const float hz = clamp (std::exp2 (pitch + (c == 0 ? -spread : spread)), 16.f, top);
                    const float gt = std::tan (kPiF * hz / sr);
                    if (prime) { g[c] = gt; gStep[c] = 0.f; }
                    else gStep[c] = (gt - g[c]) * (1.f / kCtl);
                }
                prime = false;
                if (t == LP24) for (auto& f : lad) { f.k = ladK; f.comp = 1.f + 0.6f * ladK; }
            }
            ctl = (ctl + 1) & (kCtl - 1);

            k     += coef * (kT - k);
            k2    += coef * (k2T - k2);
            post  += coef * (postT - post);
            drive += coef * (driveT - drive);
            trim  += coef * (trimT - trim);
            for (int j = 0; j < 4; ++j) mix[j] += coef * (mixT[j] - mix[j]);

            for (int c = 0; c < 2; ++c)
            {
                g[c] += gStep[c];
                const float x = fastTanh (in[c] * drive) * trim;
                float y;
                if (t == LP24)
                {
                    lad[c].setG (g[c]);
                    y = lad[c].tick (x, 0.5f);
                }
                else
                {
                    a[c].setGK (g[c], k);
                    const Svf::Out o = a[c].tick (x);
                    y = mix[0] * x + mix[1] * o.lp + mix[2] * o.bp + mix[3] * o.hp;
                    if (t == HP24)
                    {
                        b[c].setGK (g[c], k2);
                        y = b[c].hp (y) * post;
                    }
                }
                const float m = std::fabs (y);
                if (m > 1.5f) y = std::copysign (1.5f + 2.5f * fastTanh ((m - 1.5f) * 0.4f), y);
                io[c][i] = y;
            }
        }
        for (int c = 0; c < 2; ++c)
        {
            a[c].ic1 = undenorm (a[c].ic1); a[c].ic2 = undenorm (a[c].ic2); a[c].sanitise();
            b[c].ic1 = undenorm (b[c].ic1); b[c].ic2 = undenorm (b[c].ic2); b[c].sanitise();
            lad[c].s1 = undenorm (lad[c].s1); lad[c].s2 = undenorm (lad[c].s2); lad[c].s3 = undenorm (lad[c].s3); lad[c].s4 = undenorm (lad[c].s4);
        }
    }

private:
    float sr = 48000.f, atk = 0.01f, rel = 0.001f, coef = 0.01f, pitchCoef = 0.1f;
    float last[kFxParams] = { -1.f, -1.f, -1.f, -1.f, -1.f, -1.f, -1.f, -1.f };
    int   type = LP12, lastType = -1, curType = -1;

    float pitchT = 10.f, kT = 1.4f, k2T = 1.f, postT = 1.f, mixT[4] = { 0.f, 1.f, 0.f, 0.f }, driveT = 0.5f, trimT = 2.f, ladK = 0.f;
    float lfoOct = 0.f, followOct = 0.f, spreadT = 0.f;
    double lfoInc = 0.0, lfoPh = 0.0;

    float pitch = 10.f, spread = 0.f, k = 1.4f, k2 = 1.f, post = 1.f, mix[4] = { 0.f, 1.f, 0.f, 0.f }, drive = 0.5f, trim = 2.f;
    float g[2] = { 0.1f, 0.1f }, gStep[2] = {};
    float env = 0.f;
    int   ctl = 0;
    bool  snap = true, prime = true;

    Svf    a[2], b[2];
    Ladder lad[2];
};

} // namespace

std::unique_ptr<Effect> createFilterFx() { return std::make_unique<FilterFx>(); }

} // namespace atm
