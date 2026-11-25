// Distort: seven waveshapers behind a low cut, with bias, tilt tone and automatic loudness trim.
//
//   in -> low cut (12 dB/oct HP) -> drive -> + bias -> 2x oversampled shaper -> - shaper(bias)
//      -> DC blocker -> tilt EQ (first-order, pivot 700 Hz, +-9 dB) -> compensation * output
//      -> safety soft limit (transparent below +-2, ceiling +-4)
//
// Shapers (u = driven input):
//   Tanh      soft clip
//   Hard      clamp to +-1
//   Fold      triangle wavefolder; the input is soft-limited to +-5 so there are at most two full
//             folds per side and extreme drive ends on a peak (a square), not on an arbitrary level
//   SineFold  sin (pi/2 u) with the same limit: rounder, FM-like folds
//   Tube      different knees for the two half waves (soft x / (1 + x / 1.5) above, tighter tanh at
//             0.75 below): even harmonics at every level
//   Rectify   3/4 full-wave rectified + 1/4 direct, soft-clipped: octave-up buzz
//   Fuzz      asymmetric dead zone, then a lot of gain into unequal clip levels: gated and splatty,
//             notes sputter out as they decay
// Subtracting the shaper's value at the bias point keeps silence silent however the bias is set.
// Loudness: about -15 tanh (drive / 15) + 0.1 drive dB, plus a per-type trim, so that typical
// material stays near unity while the drive is swept.
#include "Effect.h"
#include "../model/ParamTables.h"
#include "../dsp/Filters.h"
#include "../dsp/Oversample.h"

namespace atm {
namespace {

class DistortFx final : public Effect
{
public:
    enum { Drive, Type, Tone, Bias, LowCut, Output };
    enum { Tanh, Hard, Fold, SineFold, Tube, Rectify, Fuzz };

    void prepare (double sampleRate) override
    {
        sr = (float) sampleRate;
        coef = onePoleCoef (0.003f, sr);
        for (auto& c : ch) { c.dc.prepare (sr, 12.f); c.tilt.setCutoff (700.f, sr); }
        reset();
    }

    void reset() override
    {
        for (auto& c : ch) { c.hp.reset(); c.os.reset(); c.dc.reset(); c.tilt.reset(); }
        snap = true;
    }

    void setParams (const float* p, const FxContext&) override
    {
        type = kFxDistort[Type].toIndex (p[Type]);
        if (p[Drive] != last[Drive] || p[Output] != last[Output] || type != lastType)
        {
            last[Drive] = p[Drive]; last[Output] = p[Output]; lastType = type;
            static constexpr float kTrimDb[] = { 0.f, 0.f, 0.f, -2.f, 0.f, 6.f, -1.f };
            const float d = kFxDistort[Drive].toPhys (p[Drive]);
            driveT = dbToGain (d);
            gainT  = dbToGain (-15.f * std::tanh (d / 15.f) + 0.1f * d + kTrimDb[clamp (type, 0, 6)] + kFxDistort[Output].toPhys (p[Output]));
        }
        if (p[Tone] != last[Tone])
        {
            last[Tone] = p[Tone];
            const float t = kFxDistort[Tone].toPhys (p[Tone]);
            lowT = dbToGain (-9.f * t); highT = dbToGain (9.f * t);
        }
        biasT = 0.8f * kFxDistort[Bias].toPhys (p[Bias]);
        if (p[LowCut] != last[LowCut]) { last[LowCut] = p[LowCut]; hpGT = Svf::gFor (kFxDistort[LowCut].toPhys (p[LowCut]), sr); }
        if (snap) { snap = false; drive = driveT; gain = gainT; low = lowT; high = highT; bias = biasT; hpG = hpGT; }
    }

    void process (float* l, float* r, int n, const FxContext&) override
    {
        float* io[2] = { l, r };
        const int t = type;
        for (int i = 0; i < n; ++i)
        {
            drive += coef * (driveT - drive);
            gain  += coef * (gainT - gain);
            bias  += coef * (biasT - bias);
            low   += coef * (lowT - low);
            high  += coef * (highT - high);
            hpG   += coef * (hpGT - hpG);
            const float rest = shape (t, bias);
            for (int c = 0; c < 2; ++c)
            {
                Channel& s = ch[c];
                s.hp.setGK (hpG, 1.41421356f);
                const float x = s.hp.hp (io[c][i]) * drive;
                float a, b;
                s.os.up (x, a, b);
                float y = s.os.down (shape (t, a + bias) - rest, shape (t, b + bias) - rest);
                y = s.dc.process (y);
                const float lo = s.tilt.process (y);
                y = (low * lo + high * (y - lo)) * gain;
                // safety stage for extreme output / tone settings: clean below +-2, never above +-4
                const float m = std::fabs (y);
                if (m > 2.f) y = std::copysign (2.f + 2.f * fastTanh (0.5f * (m - 2.f)), y);
                io[c][i] = y;
            }
        }
        for (auto& c : ch)
        {
            c.hp.sanitise(); c.hp.ic1 = undenorm (c.hp.ic1); c.hp.ic2 = undenorm (c.hp.ic2);
            c.tilt.z = undenorm (c.tilt.z);
        }
    }

private:
    static inline float foldLimit (float u) { return 5.f * fastTanh (0.2f * u); }

    static inline float shape (int type, float u)
    {
        switch (type)
        {
            case Hard: return clamp (u, -1.f, 1.f);
            case Fold:
            {
                float ph = (foldLimit (u) + 3.f) * 0.25f; ph -= std::floor (ph);
                return std::fabs (4.f * ph - 2.f) - 1.f;
            }
            case SineFold: return fastSin01 (0.25f * foldLimit (u));
            case Tube:     return u >= 0.f ? 1.5f * u / (1.5f + u) : 0.75f * fastTanh (u * (1.f / 0.75f));
            case Rectify:  { const float s = fastTanh (u); return 0.75f * std::fabs (s) + 0.25f * s; }
            case Fuzz:
            {
                const float d = u > 0.12f ? u - 0.12f : (u < -0.24f ? u + 0.24f : 0.f);
                return d >= 0.f ? softClip (20.f * d) : -0.6f * softClip (-33.f * d);
            }
            default: return fastTanh (u);
        }
    }

    struct Channel { Svf hp; Oversampler2x os; DcBlocker dc; OnePoleLP tilt; };
    Channel ch[2];

    float sr = 48000.f, coef = 0.01f;
    float last[kFxParams] = { -1.f, -1.f, -1.f, -1.f, -1.f, -1.f, -1.f, -1.f };
    int   type = Tanh, lastType = -1;
    float driveT = 1.f, gainT = 1.f, lowT = 1.f, highT = 1.f, biasT = 0.f, hpGT = 0.01f;
    float drive = 1.f, gain = 1.f, low = 1.f, high = 1.f, bias = 0.f, hpG = 0.01f;
    bool  snap = true;
};

} // namespace

std::unique_ptr<Effect> createDistortFx() { return std::make_unique<DistortFx>(); }

} // namespace atm
