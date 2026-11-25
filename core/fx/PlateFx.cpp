// Plate reverb.
//
// Topology after Jon Dattorro, "Effect Design, Part 1: Reverberator and Other Filters",
// J. Audio Eng. Soc. 45 (9), 1997 (figure-eight tank, the delay lengths and output taps of his
// table 1/2, referred to his 29761 Hz sample rate). Independent implementation; changes:
//
//   in (L+R)/2 -> predelay -> low cut (12 dB/oct) -> 4 input diffusion allpasses ----+
//                                                                                     v
//   +--> (+) -> mod AP -> delay -> AP -> damping -> *gA -> AP -> delay -> *gB -> (to the other half)
//   |                                                    two such halves, cross-coupled
//
//   * Every delay is scaled by sample rate and `size`. Lengths are whole samples when at rest and
//     glide (interpolated, rate-limited) when the size changes, so size sweeps bend the pitch of
//     the tail like a tape instead of clicking.
//   * Loop gains are set per segment from its length: g = 10^(-3 T_segment / RT60), so `decay`
//     is the reverberation time in seconds at any size.
//   * Damping is a one-pole low-pass per half whose loss at the damping frequency equals the
//     broadband loss of that half: the decay time at that frequency is half the nominal one
//     (and falls further above it). For very short decays it is capped to a plain low-pass.
//   * One extra allpass per half (after the first delay) and two-sine modulation of all four tank
//     allpasses raise the echo density and break up the tank's modes.
#include "Effect.h"
#include "../dsp/Delay.h"
#include "../dsp/Filters.h"
#include "../model/ParamTables.h"

namespace atm {
namespace platefx {

enum { PPredelay, PDecay, PSize, PDamp, PDiffusion, PMod, PLowcut, PWidth };
constexpr float kRefSr = 29761.f;
constexpr int kTick = 16;
constexpr float kMaxSize = 1.6f;
constexpr float kMaxModSec = 0.002f;

// A delay line whose length glides to a whole-sample target.
struct GDelay
{
    DelayLine line;
    float len = 16.f, target = 16.f, maxStep = 0.5f;
    int ilen = 16;
    bool moving = false;

    void prepare (float maxSamples) { line.prepare ((int) maxSamples + 16); }
    void setTarget (float samples, float maxStepPerSample)
    {
        target = std::max (std::round (samples), 2.f);
        maxStep = maxStepPerSample;
        if (target != len) moving = true;
    }
    void snap() { len = target; ilen = (int) target; moving = false; }
    inline void glide (float k)
    {
        if (! moving) return;
        const float diff = target - len;
        if (std::fabs (diff) < 2.0e-3f) { snap(); return; }
        len += clamp (diff * k, -maxStep, maxStep);
    }
    // oldest sample of the current length (read before the write of this sample)
    inline float out() const { return moving ? line.readLinear (len - 1.f) : line.read (ilen - 1); }
    inline float outMod (float offset) const { return line.readHermite (std::max (len - 1.f + offset, 1.f)); }
    inline float tap (float frac) const { return moving ? line.readLinear (frac * len) : line.read ((int) (frac * (float) ilen)); }
    inline void write (float x) { line.write (x); }
};

// Schroeder allpass around a GDelay: H = (a z^-N - g) / (1 - g a z^-N). a = 1 is a true allpass;
// in the tank a is the absorption of N samples of travel ("absorbent allpass", Dahl & Jot, DAFx 2000),
// which makes every mode of the tank decay at the same rate however long it dwells in the allpasses.
struct GAllpass
{
    GDelay d;
    inline float process (float x, float g, float a = 1.f)
    {
        const float z = d.out() * a;
        const float w = undenorm (x + g * z);
        d.write (w);
        return z - g * w;
    }
    inline float processMod (float x, float g, float a, float offset)
    {
        const float z = d.outMod (offset) * a;
        const float w = undenorm (x + g * z);
        d.write (w);
        return z - g * w;
    }
};

// One-pole low-pass coefficient (y += a (x - y)) with a given loss in dB at a given frequency.
inline float lpCoefForLoss (float lossDb, float hz, float sr)
{
    if (lossDb < 1.0e-4f) return 1.f;
    const float g2 = std::pow (10.f, -0.1f * lossDb);
    const float c = std::cos (kTauF * std::min (hz, 0.49f * sr) / sr);
    const float p = 1.f - g2 * c, q = 1.f - g2;
    const float b = (p - std::sqrt (std::max (p * p - q * q, 0.f))) / q;
    return clamp (1.f - b, 1.0e-4f, 1.f);
}

// lengths at 29761 Hz
constexpr float kInAp[4] = { 142.f, 107.f, 379.f, 277.f };
constexpr float kInCoef[4] = { 0.80f, 0.80f, 0.70f, 0.70f };
// Tank elements per half, in signal order: modulated AP, delay, AP, (damping), AP, delay.
constexpr int kElems = 5;
constexpr float kLen[2][kElems] = { { 672.f, 4453.f, 1171.f, 1800.f, 3720.f }, { 908.f, 4217.f, 1423.f, 2656.f, 3163.f } };
constexpr float kApCoef[kElems] = { -0.70f, 0.f, 0.60f, 0.50f, 0.f };       // 0: plain delay

// Output taps. Energy in the tank is spread over all delay elements in proportion to their length
// (the allpasses hold more of it at the frequencies where they delay more), so every element is
// tapped with a weight^2 proportional to its length: that keeps the late spectrum flat.
struct Tap { int half, elem; float frac, sign; };
constexpr int kTaps = 14;
constexpr Tap kTapL[kTaps] = {
    { 0, 0, 0.43f, 1.f }, { 0, 1, 0.09f, 1.f }, { 0, 1, 0.61f, -1.f }, { 0, 2, 0.71f, -1.f }, { 0, 3, 0.23f, 1.f }, { 0, 4, 0.31f, -1.f }, { 0, 4, 0.83f, 1.f },
    { 1, 0, 0.67f, -1.f }, { 1, 1, 0.27f, -1.f }, { 1, 1, 0.79f, 1.f }, { 1, 2, 0.37f, 1.f }, { 1, 3, 0.59f, -1.f }, { 1, 4, 0.13f, 1.f }, { 1, 4, 0.53f, -1.f } };
constexpr Tap kTapR[kTaps] = {
    { 0, 0, 0.71f, -1.f }, { 0, 1, 0.33f, -1.f }, { 0, 1, 0.87f, 1.f }, { 0, 2, 0.29f, 1.f }, { 0, 3, 0.63f, -1.f }, { 0, 4, 0.07f, 1.f }, { 0, 4, 0.57f, -1.f },
    { 1, 0, 0.31f, 1.f }, { 1, 1, 0.05f, 1.f }, { 1, 1, 0.47f, -1.f }, { 1, 2, 0.77f, -1.f }, { 1, 3, 0.19f, 1.f }, { 1, 4, 0.41f, -1.f }, { 1, 4, 0.91f, 1.f } };

class PlateFx final : public Effect
{
public:
    void prepare (double sampleRate) override
    {
        sr = (float) sampleRate;
        scale = sr / kRefSr;
        pre.prepare (0.25f * sr + 8.f);
        for (int i = 0; i < 4; ++i) inAp[i].d.prepare (kInAp[i] * scale * kMaxSize);
        const float modMargin = kMaxModSec * sr + 8.f;
        for (int h = 0; h < 2; ++h)
            for (int e = 0; e < kElems; ++e)
                el[h][e].d.prepare (kLen[h][e] * scale * kMaxSize + modMargin);
        // tap weights
        float total = 0.f;
        for (int h = 0; h < 2; ++h) for (int e = 0; e < kElems; ++e) total += kLen[h][e];
        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < kTaps; ++k)
            {
                const Tap& t = (c ? kTapR : kTapL)[k];
                const float n = (t.elem == 1 || t.elem == 4) ? 2.f : 1.f;       // taps on that element
                tapW[c][k] = t.sign * std::sqrt (kLen[t.half][t.elem] / (n * total));
            }
        kGlide = onePoleCoef (0.05f, sr);
        kSmooth = onePoleCoef (0.02f, sr);
        reset();
    }

    void reset() override
    {
        pre.line.reset();
        for (auto& a : inAp) a.d.line.reset();
        for (auto& h : el) for (auto& e : h) e.d.line.reset();
        lp[0] = lp[1] = 0.f;
        hp.reset();
        for (int i = 0; i < kNumLfo; ++i) { ph1[i] = (float) i * 0.137f; ph2[i] = (float) i * 0.31f; modOff[i] = modInc[i] = 0.f; }
        tick = 0;
        first = true;
    }

    void setParams (const float* p, const FxContext&) override
    {
        const float size = kFxPlate[PSize].toPhys (p[PSize]);
        const float decay = kFxPlate[PDecay].toPhys (p[PDecay]);
        const float damp = clamp01 (p[PDamp]);
        const float diff = clamp01 (p[PDiffusion]);
        const float mod = clamp01 (p[PMod]);
        tWidth = clamp01 (p[PWidth]);

        if (size != cSize || first)
        {
            cSize = size;
            const float s = scale * size;
            const float rate = 4.f / sr;           // size units per sample at most
            for (int i = 0; i < 4; ++i) inAp[i].d.setTarget (kInAp[i] * s, kInAp[i] * scale * rate);
            for (int h = 0; h < 2; ++h)
                for (int e = 0; e < kElems; ++e)
                    el[h][e].d.setTarget (kLen[h][e] * s, kLen[h][e] * scale * rate);
        }
        const float preS = kFxPlate[PPredelay].toPhys (p[PPredelay]) * sr;
        pre.setTarget (std::max (preS, 2.f), 0.5f);

        // absorption per element from its length (target lengths), damping per half
        const float damphz = 18000.f * std::pow (1.f / 18.f, damp);
        for (int h = 0; h < 2; ++h)
        {
            float halfT = 0.f;
            for (int e = 0; e < kElems; ++e)
            {
                const float t = el[h][e].d.target / sr;
                halfT += t;
                tAbs[h][e] = std::pow (10.f, -3.f * t / decay);
            }
            tLpA[h] = lpCoefForLoss (std::min (60.f * halfT / decay, 3.f), damphz, sr);
        }
        const float dc = std::sqrt (diff);
        for (int i = 0; i < 4; ++i) tInCoef[i] = kInCoef[i] * dc;
        tModDepth = mod * mod * kMaxModSec * sr;
        const float lowcut = kFxPlate[PLowcut].toPhys (p[PLowcut]);
        if (lowcut != cLowcut) { cLowcut = lowcut; hp.setQ (lowcut, 0.7071f, sr); }

        if (first)
        {
            first = false;
            pre.snap();
            for (auto& a : inAp) a.d.snap();
            for (int h = 0; h < 2; ++h)
            {
                for (int e = 0; e < kElems; ++e) { el[h][e].d.snap(); absorb[h][e] = tAbs[h][e]; }
                lpA[h] = tLpA[h];
            }
            for (int i = 0; i < 4; ++i) inCoef[i] = tInCoef[i];
            modDepth = tModDepth; width = tWidth;
            tick = 0;
        }
    }

    void process (float* l, float* r, int n, const FxContext&) override
    {
        for (int i = 0; i < n; ++i)
        {
            if (tick == 0) controlTick();
            if (++tick >= kTick) tick = 0;

            width += kSmooth * (tWidth - width);
            for (int k = 0; k < 4; ++k) inCoef[k] += kSmooth * (tInCoef[k] - inCoef[k]);
            for (int k = 0; k < kNumLfo; ++k) modOff[k] += modInc[k];

            // ---- input
            pre.glide (kGlide);
            const float pd = pre.out();
            pre.write (0.5f * (l[i] + r[i]));
            float x = hp.hp (pd);
            for (int k = 0; k < 4; ++k) { inAp[k].d.glide (kGlide); x = inAp[k].process (x, inCoef[k]); }

            // ---- tank
            for (int h = 0; h < 2; ++h)
                for (int e = 0; e < kElems; ++e)
                {
                    absorb[h][e] += kSmooth * (tAbs[h][e] - absorb[h][e]);
                    el[h][e].d.glide (kGlide);
                }
            const float cross[2] = { el[1][4].d.out() * absorb[1][4], el[0][4].d.out() * absorb[0][4] };
            for (int h = 0; h < 2; ++h)
            {
                GAllpass* t = el[h];
                const float* a = absorb[h];
                lpA[h] += kSmooth * (tLpA[h] - lpA[h]);
                float s = x + cross[h];
                s = modActive ? t[0].processMod (s, kApCoef[0], a[0], modOff[h * 3]) : t[0].process (s, kApCoef[0], a[0]);
                const float d1 = t[1].d.out() * a[1];
                t[1].d.write (s);
                s = modActive ? t[2].processMod (d1, kApCoef[2], a[2], modOff[h * 3 + 1]) : t[2].process (d1, kApCoef[2], a[2]);
                lp[h] = undenorm (lp[h] + lpA[h] * (s - lp[h]));
                s = lp[h];
                s = modActive ? t[3].processMod (s, kApCoef[3], a[3], modOff[h * 3 + 2]) : t[3].process (s, kApCoef[3], a[3]);
                t[4].d.write (s);
            }

            // ---- output taps
            float yl = 0.f, yr = 0.f;
            for (int k = 0; k < kTaps; ++k)
            {
                yl += tapW[0][k] * el[kTapL[k].half][kTapL[k].elem].d.tap (kTapL[k].frac);
                yr += tapW[1][k] * el[kTapR[k].half][kTapR[k].elem].d.tap (kTapR[k].frac);
            }
            const float m = 0.5f * (yl + yr), sd = 0.5f * (yl - yr) * width;
            l[i] = (m + sd) * kOutGain;
            r[i] = (m - sd) * kOutGain;
        }
    }

private:
    static constexpr int kNumLfo = 6;
    static constexpr float kOutGain = 2.f;

    void controlTick()
    {
        modDepth += (tModDepth - modDepth) * 0.05f;
        modActive = modDepth > 0.02f;
        // two incommensurate sines per allpass
        static constexpr float rate1[kNumLfo] = { 0.71f, 0.53f, 0.43f, 0.83f, 0.47f, 0.61f };
        static constexpr float rate2[kNumLfo] = { 1.93f, 2.31f, 1.57f, 2.11f, 1.73f, 2.57f };
        for (int k = 0; k < kNumLfo; ++k)
        {
            ph1[k] += rate1[k] * (float) kTick / sr; if (ph1[k] >= 1.f) ph1[k] -= 1.f;
            ph2[k] += rate2[k] * (float) kTick / sr; if (ph2[k] >= 1.f) ph2[k] -= 1.f;
            const float v = modActive ? modDepth * (0.7f * fastSin01 (ph1[k]) + 0.3f * fastSin01 (ph2[k])) : 0.f;
            modInc[k] = (v - modOff[k]) * (1.f / (float) kTick);
        }
    }

    float sr = 48000.f, scale = 1.f;
    GDelay pre;
    GAllpass inAp[4];
    GAllpass el[2][kElems];             // plain delays use only .d
    float absorb[2][kElems] = {}, tAbs[2][kElems] = {};
    float lp[2] = {}, lpA[2] = { 1.f, 1.f }, tLpA[2] = { 1.f, 1.f };
    float tapW[2][kTaps] = {};
    Svf hp;
    float kGlide = 0.001f, kSmooth = 0.001f;
    float cSize = -1.f, cLowcut = -1.f;
    float tInCoef[4] = {}, inCoef[4] = {};
    float tModDepth = 0.f, modDepth = 0.f, tWidth = 1.f, width = 1.f;
    float ph1[kNumLfo] = {}, ph2[kNumLfo] = {}, modOff[kNumLfo] = {}, modInc[kNumLfo] = {};
    bool modActive = false;
    int tick = 0;
    bool first = true;
};

} // namespace platefx

std::unique_ptr<Effect> createPlateFx() { return std::make_unique<platefx::PlateFx>(); }

} // namespace atm
