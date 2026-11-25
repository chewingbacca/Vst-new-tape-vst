// Ring modulator with a modulated carrier.
//
//   carrier pitch = freq + LFO (sine, up to +-3 octaves) + envelope follower (sqrt of the input
//   peak envelope, up to +-4 octaves), smoothed in the pitch domain (4 ms) and interpolated per
//   sample, so frequency jumps glide instead of clicking.
//   out = in * (carrier (1 - am / 2) + am / 2)      am 0: ring (bipolar), am 1: tremolo-style AM
//
// Carriers: sine; triangle (2-point polyBLAMP corners); square (polyBLEP edges); sample & hold
// (a new random level every carrier period, slewed over 0.15 ms). The right channel's carrier is
// offset by up to half a cycle (`phase`). The output is trimmed by the carrier's RMS (at most +3 dB)
// so the waves are about equally loud.
#include "Effect.h"
#include "../model/ParamTables.h"
#include "../dsp/Osc.h"
#include "../util/Rng.h"

namespace atm {
namespace {

class RingModFx final : public Effect
{
public:
    enum { Freq, Wave, LfoRate, LfoDepth, Follow, Phase, Am };
    enum { Sine, Triangle, Square, SampleHold };
    static constexpr int kCtl = 16;                 // internal control period (samples)

    void prepare (double sampleRate) override
    {
        sr = sampleRate;
        atk = onePoleCoef (0.004f, (float) sr); rel = onePoleCoef (0.12f, (float) sr);
        coef = onePoleCoef (0.004f, (float) sr);
        pitchCoef = 1.0 - std::exp (-(double) kCtl / (0.004 * sr));
        slew = std::max (2, (int) (sr * 0.00015));
        reset();
    }

    void reset() override
    {
        ph = 0.0; lfoPh = 0.0; env = 0.f; ctl = 0; inc = incStep = 0.0;
        for (int c = 0; c < 2; ++c)
        {
            shIdx[c] = 0; shCur[c] = shTarget[c] = hashToBipolar (hashN (0x5a17u, 0u));
            shStep[c] = 0.f; shLeft[c] = 0; lastPh[c] = 0.f;
        }
        snap = true;
    }

    void setParams (const float* p, const FxContext&) override
    {
        if (p[Freq] != last[Freq]) { last[Freq] = p[Freq]; basePitch = std::log2 ((double) kFxRingMod[Freq].toPhys (p[Freq])); }
        wave = kFxRingMod[Wave].toIndex (p[Wave]);
        if (p[LfoRate] != last[LfoRate]) { last[LfoRate] = p[LfoRate]; lfoInc = (double) kFxRingMod[LfoRate].toPhys (p[LfoRate]) * kCtl / sr; }
        lfoOct    = 3.0 * (double) kFxRingMod[LfoDepth].toPhys (p[LfoDepth]);
        followOct = 4.0 * (double) kFxRingMod[Follow].toPhys (p[Follow]);
        offsetT   = 0.5f * clamp01 (p[Phase]);
        amT       = clamp01 (p[Am]);
        static constexpr float kRms2[] = { 0.5f, 1.f / 3.f, 1.f, 1.f / 3.f };
        const float k = 1.f - 0.5f * amT;
        trimT = std::min (1.41421356f, 1.f / std::sqrt (k * k * kRms2[clamp (wave, 0, 3)] + 0.25f * amT * amT));
        if (snap)
        {
            snap = false; pitch = basePitch; offset = offsetT; am = amT; trim = trimT;
            inc = freqFor (pitch) / sr; incStep = 0.0;
        }
    }

    void process (float* l, float* r, int n, const FxContext&) override
    {
        for (int i = 0; i < n; ++i)
        {
            const float inL = l[i], inR = r[i];
            const float a = std::max (std::fabs (inL), std::fabs (inR));
            env = undenorm (env + (a > env ? atk : rel) * (a - env));

            if (ctl == 0)
            {
                const double lfo = std::sin (kTau * lfoPh);
                lfoPh += lfoInc; lfoPh -= std::floor (lfoPh);
                const double target = basePitch + lfoOct * lfo + followOct * std::sqrt ((double) std::min (1.f, env));
                pitch += pitchCoef * (target - pitch);
                incStep = (freqFor (pitch) / sr - inc) * (1.0 / kCtl);
            }
            ctl = (ctl + 1) & (kCtl - 1);
            inc += incStep;

            offset += coef * (offsetT - offset);
            am     += coef * (amT - am);
            trim   += coef * (trimT - trim);

            const float dt = (float) inc;
            const float pL = (float) ph;
            float pR = pL + offset; if (pR >= 1.f) pR -= 1.f;
            ph += inc; if (ph >= 1.0) ph -= 1.0;

            const float cL = carrier (0, pL, dt), cR = carrier (1, pR, dt);
            const float k = (1.f - 0.5f * am) * trim, o = 0.5f * am * trim;
            l[i] = inL * (cL * k + o);
            r[i] = inR * (cR * k + o);
        }
    }

private:
    double freqFor (double log2Hz) const { return clamp (std::exp2 (log2Hz), 0.05, std::min (20000.0, 0.45 * sr)); }

    static inline float blamp (float d) { d = 1.f - d; return d * d * d * (1.f / 6.f); }

    inline float carrier (int c, float p, float dt)
    {
        const bool wrapped = p < lastPh[c];
        lastPh[c] = p;
        switch (wave)
        {
            case Triangle:
            {
                float y = 4.f * std::fabs (p - 0.5f) - 1.f;
                const float inv = 1.f / dt, s = 8.f * dt;
                float d = p * inv;                  if (d < 1.f) y -= s * blamp (d);
                d = (1.f - p) * inv;                if (d < 1.f) y -= s * blamp (d);
                d = std::fabs (p - 0.5f) * inv;     if (d < 1.f) y += s * blamp (d);
                return y;
            }
            case Square: return pulseBlep (p, dt, 0.5f);
            case SampleHold:
                if (wrapped)
                {
                    shTarget[c] = hashToBipolar (hashN (0x5a17u, ++shIdx[c]));
                    shLeft[c] = slew; shStep[c] = (shTarget[c] - shCur[c]) / (float) slew;
                }
                if (shLeft[c] > 0) { shCur[c] += shStep[c]; if (--shLeft[c] == 0) shCur[c] = shTarget[c]; }
                return shCur[c];
            default: return std::sin (kTauF * p);
        }
    }

    double sr = 48000.0;
    float  atk = 0.01f, rel = 0.001f, coef = 0.01f;
    double pitchCoef = 0.1;
    int    slew = 8;

    float  last[kFxParams] = { -1.f, -1.f, -1.f, -1.f, -1.f, -1.f, -1.f, -1.f };
    double basePitch = 7.78, lfoInc = 0.0, lfoOct = 0.0, followOct = 0.0;
    float  offsetT = 0.f, amT = 0.f, trimT = 1.41421356f;
    int    wave = Sine;
    bool   snap = true;

    double ph = 0.0, lfoPh = 0.0, pitch = 7.78, inc = 0.0, incStep = 0.0;
    float  env = 0.f, offset = 0.f, am = 0.f, trim = 1.f;
    int    ctl = 0;
    uint32_t shIdx[2] = {};
    float  shCur[2] = {}, shTarget[2] = {}, shStep[2] = {}, lastPh[2] = {};
    int    shLeft[2] = {};
};

} // namespace

std::unique_ptr<Effect> createRingModFx() { return std::make_unique<RingModFx>(); }

} // namespace atm
