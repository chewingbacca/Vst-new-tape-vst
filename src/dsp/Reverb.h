// Verdant — huge modulated FDN reverb with shimmer in the feedback path and freeze.
//
//   in ─► pre-delay ─► 4 allpass diffusers / channel ─┬────────────────────────► early
//                                                     ▼
//        ┌──────► 16 modulated delay lines ─► allpass ─► damping ─► decay gain ──┬─► output taps
//        │                                                                       ▼
//        └── soft limit ◄── + input ◄── shimmer (2 lines) ◄── Hadamard mix ◄─────┘
//
// Stability:
//  * The mixing matrix is orthonormal and every in-loop element is passive, so loop
//    gain is <= 1 by construction, including "infinite" decay.
//  * Shimmer does not ADD a feedback path: on two of the mixed channels a fraction
//    sin(theta) of the signal is REPLACED by its pitch-shifted copy
//    (y = cos(theta) x + sin(theta) shift(x)). The shifter's two windows sum to one,
//    so it never has gain, and its input is band-limited (no aliasing, no DC build-up).
//  * A tank-energy governor shortens the decay progressively if the tank gets hot
//    (continuous input into an infinite tail), and a soft limiter in front of the
//    delay lines makes the state bounded no matter what.
//
// References (ideas only, all code original): Jot & Chaigne 1991 (lossless prototype +
// absorption), Dattorro 1997 (input diffusion, modulated tank), Signalsmith "Let's
// Write A Reverb" (Hadamard / multichannel diffusion), Puckette (delay-line pitch shifter).
#pragma once

#include "Util.h"

namespace verdant
{

//==============================================================================
// Two-head delay-line pitch shifter. Hann windows half a period apart sum to 1.
class DelayShifter
{
public:
    void prepare (float sr, float windowMs, float maxRatio)
    {
        win = windowMs * 0.001f * sr;
        buf.prepare (int (win * std::max (1.0f, maxRatio - 1.0f)) + 64);
        reset();
    }
    void reset() { buf.clear(); ph = 0.0f; ratio[0] = ratio[1] = target; }
    void setRatio (float r) { target = r; }

    inline float process (float x)
    {
        buf.push (x);
        const HannTable& hann = hannTable();
        float out = 0.0f;
        for (int h = 0; h < 2; ++h)
        {
            float p = ph + 0.5f * float (h);
            if (p >= 1.0f) p -= 1.0f;
            const float r = ratio[h];
            const float sweep = std::abs (r - 1.0f) * win;
            const float delay = 2.0f + (r > 1.0f ? (1.0f - p) * sweep : p * sweep);
            out += hann (p) * buf.tapHermite (delay);
        }
        const float prev = ph;
        ph += 1.0f / win;
        // a head latches the new ratio while its window is at zero
        if (ph >= 1.0f) { ph -= 1.0f; ratio[0] = target; }
        if (prev < 0.5f && ph >= 0.5f) ratio[1] = target;
        return out;
    }

private:
    Ring buf;
    float win = 4000.0f, ph = 0.0f, target = 2.0f, ratio[2] = { 2.0f, 2.0f };
};

//==============================================================================
class Reverb
{
public:
    static constexpr int N = 16;

    struct Controls
    {
        bool  on = true;
        float size = 0.6f;
        float decaySec = 6.0f;
        bool  infinite = false;
        float predelayMs = 20.0f;
        float dampHz = 6500.0f, lowCutHz = 110.0f;
        float mod = 0.4f, modRate = 1.0f;
        float diffusion = 0.75f;
        float shimmer = 0.25f;
        int   shimmerPitch = 0;
        bool  freeze = false;
    };

    void prepare (float sampleRate)
    {
        sr = sampleRate;
        tickRate = sr / 32.0f;

        const float maxScale = kMinScale + kScaleRange;
        for (int i = 0; i < N; ++i)
        {
            line[i].prepare (int (kLineMs[i] * 0.001f * sr * maxScale + 0.006f * sr) + 16);
            apLen[i] = std::max (8, int ((9.1f + 1.87f * float (i)) * 0.001f * sr));
            ap[i].prepare (apLen[i] + 4);
            lfoRate[i] = 0.071f + 0.043f * float ((i * 7) % N);
            lfoPhase[i] = float ((i * 11) % N) / float (N);
        }
        static constexpr float inApMs[2][4] = { { 4.31f, 7.73f, 11.83f, 17.27f }, { 5.03f, 8.57f, 13.19f, 19.01f } };
        for (int c = 0; c < 2; ++c)
        {
            pre[c].prepare (int (0.26f * sr) + 16);
            for (int k = 0; k < 4; ++k)
            {
                inApLen[c][k] = std::max (4, int (inApMs[c][k] * 0.001f * sr));
                inAp[c][k].prepare (inApLen[c][k] + 4);
            }
            shifter[c].prepare (sr, c == 0 ? 83.0f : 97.0f, 4.0f);
            shimHp[c].setCutoff (160.0f, sr);
        }

        sScale.setTime (0.25f, tickRate);
        sPre.setTime (0.12f, tickRate);
        sFreeze.setTime (0.06f, tickRate);
        sOn.setTime (0.03f, tickRate);
        sShim.setTime (0.08f, tickRate);
        sDiff.setTime (0.08f, tickRate);
        sMod.setTime (0.1f, tickRate);
        sT60.setTime (0.08f, tickRate);
        sDamp.setTime (0.06f, tickRate);
        sLow.setTime (0.06f, tickRate);
        energySmooth = 1.0f - std::exp (-1.0f / (0.05f * tickRate));
        rng.seed (0xFDA016u);
        reset();
    }

    void reset()
    {
        for (int i = 0; i < N; ++i)
        {
            line[i].clear(); ap[i].clear();
            lpState[i] = 0.0f; lowState[i] = 0.0f;
            wander[i] = 0.0f; wanderTarget[i] = 0.0f;
        }
        for (int c = 0; c < 2; ++c)
        {
            pre[c].clear();
            for (auto& a : inAp[c]) a.clear();
            shifter[c].reset();
            shimHp[c].reset(); shimLp[c].reset();
            outHp[c].reset();
        }
        energy = 0.0f; energyAcc = 0.0f; energyCount = 0;
        first = true;
    }

    void control (const Controls& c)
    {
        sOn.set (c.on ? 1.0f : 0.0f);
        sFreeze.set (c.freeze ? 1.0f : 0.0f);
        sScale.set (kMinScale + kScaleRange * std::pow (clampf (c.size, 0.0f, 1.0f), 1.5f));
        sPre.set (clampf (c.predelayMs, 0.0f, 250.0f) * 0.001f * sr);
        sShim.set (clampf (c.shimmer, 0.0f, 1.0f));
        sDiff.set (clampf (c.diffusion, 0.0f, 1.0f));
        sMod.set (clampf (c.mod, 0.0f, 1.0f));
        sT60.set (c.infinite ? kInfiniteT60 : clampf (c.decaySec, 0.1f, 200.0f));
        sDamp.set (clampf (c.dampHz, 300.0f, 20000.0f));
        sLow.set (clampf (c.lowCutHz, 15.0f, 1500.0f));
        if (first)
            for (auto* s : { &sOn, &sFreeze, &sScale, &sPre, &sShim, &sDiff, &sMod, &sT60, &sDamp, &sLow })
                s->snap (s->target);

        const float on = sOn.tick(), frz = sFreeze.tick(), scale = sScale.tick(), preSamples = sPre.tick();
        const float shim = sShim.tick(), diff = sDiff.tick(), mod = sMod.tick();
        float t60 = sT60.tick();
        const float damp = sDamp.tick(), lowCut = sLow.tick();
        const bool infinite = t60 >= kInfiniteT60 * 0.99f;

        // tails ring out after the module is switched off; stop working once it is silent
        active = on > 1.0e-4f || c.on || energy > 1.0e-9f;

        // ---- tank-energy governor: a hot tank gets a progressively shorter decay
        energy += energySmooth * (energyAcc / float (std::max (1, energyCount)) - energy);
        energyAcc = 0.0f; energyCount = 0;
        const float overDb = 10.0f * std::log10 (std::max (energy, 1.0e-12f) / kEnergyLimit);
        const float over = clampf (overDb / 6.0f, 0.0f, 1.0f);
        if (over > 0.0f)
            t60 = std::exp (lerp (std::log (std::min (t60, 400.0f)), std::log (0.5f), over));

        // ---- per-line delay, modulation and decay gain
        const float modDepth = mod * 0.0020f * sr * lerp (1.0f, 0.4f, frz);
        const float dt = 1.0f / tickRate;
        for (int i = 0; i < N; ++i)
        {
            lfoPhase[i] += lfoRate[i] * c.modRate * dt;
            lfoPhase[i] -= std::floor (lfoPhase[i]);
            if (rng.chance (lfoRate[i] * c.modRate * dt * 3.0f)) wanderTarget[i] = rng.bi();
            wander[i] += clampf (kTwoPi * lfoRate[i] * c.modRate * dt, 0.0f, 1.0f) * (wanderTarget[i] - wander[i]);

            const float base = kLineMs[i] * 0.001f * sr * scale;
            const float d = std::max (4.0f, base + modDepth * (0.75f * fastSin01 (lfoPhase[i]) + 0.5f * wander[i]) + 0.0022f * sr);
            if (first) rDelay[i].snap (d);
            rDelay[i].begin (d, 32);

            const float loopSeconds = (base + float (apLen[i])) / sr;
            float g = (infinite && over <= 0.0f) ? 1.0f : std::exp (-6.9078f * loopSeconds / t60);
            g = lerp (g, 1.0f, frz);
            if (over > 0.0f) g = std::min (g, std::exp (-6.9078f * loopSeconds / t60));
            lineGain[i] = g;
        }

        // ---- damping (bypassed towards freeze; relaxed for an infinite tail)
        const float dampHz = infinite ? std::max (damp, 9000.0f) : damp;
        lpCoef = lerp (1.0f - std::exp (-kTwoPi * std::min (dampHz, sr * 0.45f) / sr), 1.0f, frz);
        // gentle in-loop bass absorption below the low cut (none when infinite / frozen)
        lowCoef = 1.0f - std::exp (-kTwoPi * lowCut * 1.5f / sr);
        lowAbsorb = (infinite ? 0.0f : 0.06f) * (1.0f - frz);

        apGain = 0.2f + 0.45f * diff;
        inApGain = 0.25f + 0.5f * diff;

        // ---- shimmer
        static constexpr float ratios[5][2] = { { 2.0f, 2.0f }, { 1.4983f, 1.4983f }, { 2.0f, 1.4983f },
                                                { 4.0f, 4.0f }, { 0.5f, 0.5f } };
        const int sp = std::clamp (c.shimmerPitch, 0, 4);
        const float sin2 = 0.95f * std::pow (shim, 1.5f) * (1.0f - frz);
        shimSin = std::sqrt (sin2);
        shimCos = std::sqrt (1.0f - sin2);
        shimOn = shimSin > 1.0e-3f;
        for (int k = 0; k < 2; ++k)
        {
            const float r = ratios[sp][k];
            shifter[k].setRatio (r);
            shimLp[k].set (std::min (9000.0f, 0.42f * sr / std::max (1.0f, r)), 0.7f, sr);
        }

        for (auto& f : outHp) f.set (lowCut, 0.7f, sr);

        // ---- levels
        const float inG = on * (1.0f - frz) * 0.35355339f;            // 1/sqrt(8) per line
        const float t60Level = std::min (sT60.get(), 60.0f);
        const float outG = 1.25f * clampf (std::pow (4.0f / t60Level, 0.3f), 0.45f, 1.6f) * 0.35355339f;
        const float earlyG = 0.22f * on * (1.0f - frz);
        if (first)
        {
            rIn.snap (inG); rOut.snap (outG); rEarly.snap (earlyG); rPre.snap (preSamples);
            first = false;
        }
        rIn.begin (inG, 32);
        rOut.begin (outG, 32);
        rEarly.begin (earlyG, 32);
        rPre.begin (preSamples, 32);
    }

    // Wet only. outL/outR are overwritten.
    void process (const float* inL, const float* inR, float* outL, float* outR, int n)
    {
        if (! active)
        {
            for (int i = 0; i < n; ++i)
            {
                outL[i] = outR[i] = 0.0f;
                rIn.next(); rOut.next(); rEarly.next(); rPre.next();
            }
            for (auto& r : rDelay) for (int i = 0; i < n; ++i) r.next();
            return;
        }

        float f[N];
        float acc = 0.0f;

        for (int s = 0; s < n; ++s)
        {
            // ---- pre-delay + input diffusion
            pre[0].push (inL[s]);
            pre[1].push (inR[s]);
            const float pd = rPre.next();
            float x[2] = { pre[0].tapLinear (pd), pre[1].tapLinear (pd) };
            for (int c = 0; c < 2; ++c)
                for (int k = 0; k < 4; ++k)
                    x[c] = allpass (inAp[c][k], inApLen[c][k], inApGain, x[c]);

            // ---- read the lines
            for (int i = 0; i < N; ++i)
            {
                float v = line[i].tapHermite (rDelay[i].next());
                v = allpass (ap[i], apLen[i], apGain, v);
                lpState[i] += lpCoef * (v - lpState[i]);                 // high damping
                v = lpState[i];
                lowState[i] += lowCoef * (v - lowState[i]);              // bass absorption
                v -= lowAbsorb * lowState[i];
                f[i] = v * lineGain[i];
                acc += f[i] * f[i];
            }

            // ---- output taps: even lines left, odd lines right (decorrelated, wide)
            float oL = 0.0f, oR = 0.0f;
            for (int i = 0; i < N; i += 2)
            {
                const float sg = kTapSign[i >> 1];
                oL += sg * f[i];
                oR += sg * f[i + 1];
            }
            const float og = rOut.next(), eg = rEarly.next();
            outL[s] = outHp[0].high (oL * og + x[0] * eg);
            outR[s] = outHp[1].high (oR * og + x[1] * eg);

            // ---- orthonormal mix
            hadamard16 (f);

            // ---- shimmer: replace part of two channels with their pitch-shifted copy
            if (shimOn)
            {
                for (int k = 0; k < 2; ++k)
                {
                    const float ps = shifter[k].process (shimLp[k].low (shimHp[k].process (f[k])));
                    f[k] = shimCos * f[k] + shimSin * ps;
                }
            }

            // ---- inject input and write back
            const float ig = rIn.next();
            const float iL = x[0] * ig, iR = x[1] * ig;
            for (int i = 0; i < N; i += 2)
            {
                const float sg = kInSign[i >> 1];
                line[i].push (softLimit (f[i] + sg * iL));
                line[i + 1].push (softLimit (f[i + 1] + sg * iR));
            }
        }

        energyAcc += acc / float (N);
        energyCount += n;
    }

    float tankEnergy() const { return energy; }

private:
    static inline float allpass (Ring& r, int len, float g, float x)
    {
        const float d = r.tap (len - 1);
        const float v = x - g * d;
        r.push (v);
        return d + g * v;
    }

    // Transparent below +-2, bounded at +-3.
    static inline float softLimit (float x)
    {
        const float a = std::abs (x);
        if (a <= 2.0f) return x;
        const float y = 2.0f + std::tanh (a - 2.0f);
        return x > 0.0f ? y : -y;
    }

    static inline void hadamard16 (float* v)
    {
        for (int len = 1; len < N; len <<= 1)
            for (int i = 0; i < N; i += len << 1)
                for (int j = i; j < i + len; ++j)
                {
                    const float a = v[j], b = v[j + len];
                    v[j] = a + b;
                    v[j + len] = a - b;
                }
        for (int i = 0; i < N; ++i) v[i] *= 0.25f;
    }

    static constexpr float kLineMs[N] = { 41.3f, 47.9f, 55.1f, 62.3f, 71.9f, 81.7f, 93.1f, 104.3f,
                                          118.9f, 133.7f, 151.1f, 169.9f, 193.3f, 221.7f, 258.1f, 307.3f };
    static constexpr float kTapSign[8] = { 1, -1, 1, 1, -1, 1, -1, -1 };
    static constexpr float kInSign[8]  = { 1, 1, -1, 1, -1, -1, 1, -1 };
    static constexpr float kMinScale = 0.22f, kScaleRange = 1.5f;
    static constexpr float kInfiniteT60 = 1000.0f;
    static constexpr float kEnergyLimit = 0.06f;  // mean-square per line (about -12 dBFS RMS)

    float sr = 48000.0f, tickRate = 1500.0f;
    Rng rng;

    Ring line[N], ap[N], pre[2], inAp[2][4];
    int apLen[N] = {}, inApLen[2][4] = {};
    float lfoRate[N] = {}, lfoPhase[N] = {}, wander[N] = {}, wanderTarget[N] = {};
    float lpState[N] = {}, lowState[N] = {}, lineGain[N] = {};
    Ramp rDelay[N], rIn, rOut, rEarly, rPre;
    float lpCoef = 1.0f, lowCoef = 0.0f, lowAbsorb = 0.0f, apGain = 0.5f, inApGain = 0.5f;

    DelayShifter shifter[2];
    OnePoleHP shimHp[2];
    Svf shimLp[2], outHp[2];
    float shimSin = 0.0f, shimCos = 1.0f;
    bool shimOn = false;

    float energy = 0.0f, energyAcc = 0.0f, energySmooth = 0.1f;
    int energyCount = 0;

    Smooth sScale, sPre, sFreeze, sOn, sShim, sDiff, sMod, sT60, sDamp, sLow;
    bool first = true, active = true;
};

} // namespace verdant
