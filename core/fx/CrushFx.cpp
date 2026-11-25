// Crush: bit-depth and sample-rate reduction.
//
//   in -> drive -> clip to full scale (as a converter would) -> sample & hold at `rate` -> quantiser
//      -> zero-order hold / linear interpolation (`smooth`) -> DC correction -> level trim -> tone LP
//
// The hold clock is a phase accumulator in Hz, so the pitch of the aliasing does not depend on the
// host rate; the value is taken at the clock's sub-sample position (linear interpolation of the
// input). `jitter` draws a new random period for every hold (seeded, +-1.6 octaves at maximum).
// With the rate knob at maximum the clock ticks every sample at any host rate.
//
// Quantiser, 2^bits levels with a continuous step size 2 / 2^bits:
//   Round  nearest level, two's complement range (at whole bit depths exactly 2^bits codes,
//          -2^(bits-1) .. 2^(bits-1) - 1; silence stays silence, quiet signals gate out)
//   Trunc  floor (the classic gritty truncation; any negative signal toggles the lowest bit)
//   Xor    sign/magnitude code XOR its own right shift (Gray code): a folded, non-monotonic staircase
//   And    sign/magnitude code AND NOT its right shift: runs of set bits collapse, hollow and broken
// The mean of the error the crusher adds (truncation offset, asymmetric clipping) is tracked and
// subtracted, which leaves the clean path untouched: at 16 bit / full rate the effect is transparent
// to within half an LSB.
#include "Effect.h"
#include "../model/ParamTables.h"
#include "../dsp/Filters.h"
#include "../util/Rng.h"

namespace atm {
namespace {

class CrushFx final : public Effect
{
public:
    enum { Bits, Rate, Jitter, Drive, Mode, Smooth, Tone };
    enum { Round, Trunc, Xor, And };

    void prepare (double sampleRate) override
    {
        sr = (float) sampleRate;
        gainCoef = onePoleCoef (0.003f, sr);
        dcCoef   = onePoleCoef (1.f / (kTauF * 6.f), sr);          // 6 Hz
        reset();
    }

    void reset() override
    {
        noise.seed (0x51c7a11u);
        ph = 0.f; inc = incBase;
        for (int c = 0; c < 2; ++c) { q0[c] = q1[c] = prevX[c] = dc[c] = 0.f; lp[c].reset(); }
        snap = true;
    }

    void setParams (const float* p, const FxContext&) override
    {
        if (p[Bits] != last[Bits])
        {
            last[Bits] = p[Bits];
            levels  = std::exp2 (kFxCrush[Bits].toPhys (p[Bits]));
            step    = 2.f / levels;
            invStep = 0.5f * levels;
            codeLo  = -std::floor (0.5f * levels);
            codeHi  = std::ceil (0.5f * levels) - 1.f;
            magHi   = (int) std::ceil (levels) - 1;
        }
        if (p[Rate] != last[Rate])
        {
            last[Rate] = p[Rate];
            incBase = p[Rate] >= 0.9995f ? 1.f : std::min (1.f, kFxCrush[Rate].toPhys (p[Rate]) / sr);
        }
        jitter = clamp01 (p[Jitter]);
        if (jitter <= 0.f) inc = incBase;
        if (p[Drive] != last[Drive])
        {
            last[Drive] = p[Drive];
            driveT = dbToGain (kFxCrush[Drive].toPhys (p[Drive]));
            compT  = 1.f / std::sqrt (driveT);
        }
        mode   = kFxCrush[Mode].toIndex (p[Mode]);
        smooth = clamp01 (p[Smooth]);
        if (p[Tone] != last[Tone])
        {
            last[Tone] = p[Tone];
            toneMixT = p[Tone] >= 0.999f ? 0.f : 1.f;               // fully open = out of circuit
            toneG = Svf::gFor (kFxCrush[Tone].toPhys (p[Tone]), sr);
        }
        if (snap) { snap = false; drive = driveT; comp = compT; toneMix = toneMixT; g = toneG; inc = incBase; ph = 1.f - inc; }   // first sample is a clock edge
    }

    void process (float* l, float* r, int n, const FxContext&) override
    {
        float* io[2] = { l, r };
        for (int i = 0; i < n; ++i)
        {
            drive += gainCoef * (driveT - drive);
            comp  += gainCoef * (compT - comp);
            const float x[2] = { clamp (l[i] * drive, -1.f, 1.f), clamp (r[i] * drive, -1.f, 1.f) };

            ph += inc;
            if (ph >= 1.f)
            {
                ph -= 1.f;
                const float a = clamp01 (ph / inc);                 // how far into this sample period the clock edge lies behind us
                for (int c = 0; c < 2; ++c) { q0[c] = q1[c]; q1[c] = quantise (x[c] + (prevX[c] - x[c]) * a); }
                if (jitter > 0.f)
                {
                    const float rnd = noise.next() * 2.f * jitter;  // period factor 1/3 .. 3 at full jitter
                    inc = std::min (1.f, rnd >= 0.f ? incBase / (1.f + rnd) : incBase * (1.f - rnd));
                }
            }

            if (toneMix != toneMixT)
            {
                toneMix += gainCoef * (toneMixT - toneMix);
                if (std::fabs (toneMix - toneMixT) < 1.0e-4f) toneMix = toneMixT;
            }
            if (toneMix > 0.f) g += gainCoef * (toneG - g);

            const float hold = smooth * (1.f - ph);
            for (int c = 0; c < 2; ++c)
            {
                prevX[c] = x[c];
                const float raw = q1[c] - hold * (q1[c] - q0[c]);
                dc[c] = undenorm (dc[c] + dcCoef * ((raw - x[c]) - dc[c]));
                float y = (raw - dc[c]) * comp;
                if (toneMix > 0.f)
                {
                    lp[c].setGK (g, 1.41421356f);
                    y += (lp[c].lp (y) - y) * toneMix;
                }
                io[c][i] = y;
            }
            if (toneMix <= 0.f && (lp[0].ic1 != 0.f || lp[0].ic2 != 0.f || lp[1].ic1 != 0.f || lp[1].ic2 != 0.f)) { lp[0].reset(); lp[1].reset(); g = toneG; }
        }
    }

private:
    inline float quantise (float s) const
    {
        switch (mode)
        {
            case Trunc: return clamp (std::floor (s * invStep), codeLo, codeHi) * step;
            case Xor: case And:
            {
                const int m = std::min ((int) (std::fabs (s) * levels), magHi);
                const int o = mode == Xor ? (m ^ (m >> 1)) : (m & ~(m >> 1));
                const float v = std::min (1.f, (float) o / levels);
                return s < 0.f ? -v : v;
            }
            default: return clamp (std::floor (s * invStep + 0.5f), codeLo, codeHi) * step;
        }
    }

    float sr = 48000.f, gainCoef = 0.01f, dcCoef = 0.001f;
    float last[kFxParams] = { -1.f, -1.f, -1.f, -1.f, -1.f, -1.f, -1.f, -1.f };
    float levels = 65536.f, step = 2.f / 65536.f, invStep = 32768.f, codeLo = -32768.f, codeHi = 32767.f;
    int   magHi = 65535, mode = Round;
    float incBase = 1.f, inc = 1.f, jitter = 0.f, smooth = 0.f;
    float driveT = 1.f, compT = 1.f, drive = 1.f, comp = 1.f;
    float toneMixT = 0.f, toneMix = 0.f, toneG = 1.f, g = 1.f;
    bool  snap = true;

    Noise noise;
    float ph = 1.f;
    float q0[2] = {}, q1[2] = {}, prevX[2] = {}, dc[2] = {};
    Svf   lp[2];
};

} // namespace

std::unique_ptr<Effect> createCrushFx() { return std::make_unique<CrushFx>(); }

} // namespace atm
