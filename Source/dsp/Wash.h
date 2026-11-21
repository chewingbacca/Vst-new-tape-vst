// Wash: a large modulated hall built on a 16-line feedback delay network.
//
//  input -> pre-delay -> low cut -> 4 series allpass diffusers per side
//        -> 16 delay lines mixed by a Hadamard matrix (lossless), each with
//           its own damping filters and decay gain; half the lines are slowly
//           modulated for a chorused tail.
//
// Shimmer lives *inside* the loop: six of the lines add a pitch-shifted copy
// of themselves on every trip round the network, so the tail keeps climbing
// (or dropping) by the interval - the cascading octave. The copy is limited
// to the energy that line has just lost to decay, so those lines can never
// put out more than they took in: no setting can make the loop grow.
// Freeze raises the decay gains to unity and closes the input.
#pragma once

#include "Util.h"

#include <array>

namespace swoon
{

/** Two-grain delay-line pitch shifter (sin^2 windows, half-period offset). */
class GrainShifter
{
public:
    void prepare (float rate, uint32_t seed)
    {
        fs = rate;
        window = static_cast<int> (0.064f * fs);
        line.prepare (2 * window + 64);
        rng = Rng (seed);
        reset();
    }
    void reset() noexcept
    {
        line.reset();
        phase = 0.0f;
        jitter[0] = jitter[1] = 0.0f;
    }
    void setRatio (float r) noexcept { ratio = r; }

    /** Keeps the line current without producing output (used while the shifter is idle). */
    void feed (float x) noexcept { line.push (x); }

    float process (float x) noexcept
    {
        line.push (x);
        const float w = static_cast<float> (window);
        // delay ramps at (1 - ratio) samples per sample: falling for upward shifts, rising for downward
        const float slope = 1.0f - ratio;
        const float inc = std::abs (slope) / w;
        const float prev = phase;
        phase += inc;
        if (phase >= 1.0f)
            phase -= 1.0f;

        float out = 0.0f;
        for (int g = 0; g < 2; ++g)
        {
            float p = phase + 0.5f * static_cast<float> (g);
            if (p >= 1.0f)
                p -= 1.0f;
            float pPrev = prev + 0.5f * static_cast<float> (g);
            if (pPrev >= 1.0f)
                pPrev -= 1.0f;
            if (p < pPrev)   // grain restarts at its window's zero: pick a new random start offset
                jitter[g] = rng.uniform() * 0.25f * w;

            const float delay = (slope < 0.0f ? (1.0f - p) : p) * w + 2.0f + jitter[g];
            const float s = std::sin (kPi * p);
            out += s * s * line.readLinear (delay);
        }
        return out;
    }

private:
    DelayLine line;
    Rng rng { 1 };
    float fs = 48000.0f, ratio = 2.0f, phase = 0.0f;
    float jitter[2] { 0.0f, 0.0f };
    int window = 3000;
};

class Wash
{
public:
    static constexpr int kLines = 16;

    struct Params
    {
        bool on = true;
        float decay = 6.0f;        // seconds (RT60 at mid frequencies)
        float size = 0.7f;         // 0..1
        float preDelayMs = 20.0f;
        float highCutHz = 6500.0f;
        float lowCutHz = 120.0f;
        float mod = 0.45f;
        float shimmer = 0.0f;
        int shimmerMode = 0;       // 0 octave up, 1 fifth up, 2 octave down, 3 up + down
        float duck = 0.0f;
        bool freeze = false;
        float mix = 0.45f;
    };

    void prepare (double sampleRate)
    {
        fs = static_cast<float> (sampleRate);
        const int maxLine = static_cast<int> (kBaseMs[kLines - 1] * 0.001f * kMaxScale * fs) + static_cast<int> (0.006f * fs) + 16;
        for (int i = 0; i < kLines; ++i)
        {
            lines[i].prepare (maxLine);
            lfo[i].setRate (kLfoHz[i], fs);
            lfo[i].setPhase (static_cast<float> (i) * 0.37f);
            lenS[i].setTime (0.25f, fs);
        }
        for (int ch = 0; ch < 2; ++ch)
        {
            preDelay[ch].prepare (static_cast<int> (0.26f * fs) + 16);
            for (int k = 0; k < 4; ++k)
            {
                diff[ch][k].prepare (static_cast<int> (kDiffMs[ch][k] * 0.001f * fs) + 8);
                diff[ch][k].setDelay (static_cast<int> (kDiffMs[ch][k] * 0.001f * fs));
            }
        }
        for (int k = 0; k < kShimLines; ++k)
        {
            shifter[k].prepare (fs, 0xFACEu + 7919u * static_cast<uint32_t> (k));
            shiftLp[k].setLowPass (std::min (0.2 * sampleRate, 8500.0), 0.6, sampleRate);
            shiftHp[k].setCutoff (170.0f, fs);
        }

        gate.prepare (fs);
        shimModeSwitch.prepare (fs, 0.03f);
        decayS.setTime (0.1f, fs / kChunk);
        sizeS.setTime (0.1f, fs / kChunk);
        dampS.setTime (0.08f, fs / kChunk);
        lowS.setTime (0.08f, fs / kChunk);
        modS.setTime (0.1f, fs / kChunk);
        shimS.setTime (0.08f, fs);
        freezeS.setTime (0.06f, fs);
        preS.setTime (0.12f, fs);
        duckS.setTime (0.05f, fs);
        duckEnv.setTimes (0.008f, 0.35f, fs);
        drift.setCutoff (0.5f, fs / kChunk);
        reset();
    }

    void reset() noexcept
    {
        for (int i = 0; i < kLines; ++i)
        {
            lines[i].reset();
            lpZ[i] = hpZ[i] = 0.0f;
        }
        for (int ch = 0; ch < 2; ++ch)
        {
            preDelay[ch].reset();
            for (auto& d : diff[ch])
                d.reset();
            inHpZ[ch] = 0.0f;
        }
        for (int k = 0; k < kShimLines; ++k)
        {
            shifter[k].reset();
            shiftLp[k].reset();
            shiftHp[k].reset();
        }
        gate.reset();
        shimModeSwitch.reset();
        duckEnv.reset();
        first = true;
    }

    void process (float* l, float* r, int n, const Params& p) noexcept
    {
        // flush for long enough that a forced-short tail has died away
        if (! gate.begin (p.on, p.mix, n, static_cast<int> (1.5f * fs), first))
        {
            first = false;
            return;
        }

        // ---- control rate -------------------------------------------------------------
        decayS.target = std::clamp (p.decay, 0.2f, 120.0f);
        sizeS.target = clamp01 (p.size);
        dampS.target = std::clamp (p.highCutHz, 500.0f, 20000.0f);
        lowS.target = std::clamp (p.lowCutHz, 15.0f, 1200.0f);
        modS.target = clamp01 (p.mod);
        shimS.target = clamp01 (p.shimmer);
        freezeS.target = p.freeze ? 1.0f : 0.0f;
        preS.target = static_cast<double> (std::clamp (p.preDelayMs, 0.0f, 250.0f) * 0.001f * fs + 1.0f);
        if (gate.flushing)
            freezeS.snap (0.0f);     // a held tail must not survive being switched off
        duckS.target = clamp01 (p.duck);
        if (first)
        {
            decayS.snap(); sizeS.snap(); dampS.snap(); lowS.snap(); modS.snap();
            shimS.snap(); freezeS.snap(); preS.snap(); duckS.snap();
        }
        shimModeSwitch.request (std::clamp (p.shimmerMode, 0, 3));

        // while flushing, force a short decay so the network empties quickly
        const float rt60 = gate.flushing ? 0.25f : decayS.next();
        const float size = sizeS.next();
        const float scale = kMinScale * std::pow (kMaxScale / kMinScale, size);
        const float damp = dampS.next();
        const float lowCut = lowS.next();
        const float modAmt = modS.next();
        const float wander = drift.tick();

        const float lpCoeff = 1.0f - std::exp (-kTwoPi * std::min (damp, 0.45f * fs) / fs);
        // lows are thinned inside the loop at a quarter of the low-cut frequency (so they decay faster, not vanish)
        const float hpCoeff = 1.0f - std::exp (-kTwoPi * (lowCut * 0.25f) / fs);
        const float inHpCoeff = 1.0f - std::exp (-kTwoPi * lowCut / fs);

        float lineGain[kLines];
        for (int i = 0; i < kLines; ++i)
        {
            // unmodulated (odd) lines settle on whole samples so their reads are lossless
            const float exact = kBaseMs[i] * 0.001f * scale * fs;
            const float len = (i & 1) == 0 ? exact : std::round (exact);
            lenS[i].target = static_cast<double> (len);
            if (first)
                lenS[i].snap();
            lineGain[i] = std::pow (10.0f, -3.0f * (len / fs) / rt60);
        }
        const float modDepth = modAmt * modAmt * 0.0016f * fs * (0.5f + 0.5f * scale) * (1.0f + 0.2f * wander);

        // wet level: keep loudness in the same region as decay time and size change
        const float wetNorm = 0.7f / std::sqrt (std::max (0.4f, std::min (rt60, 20.0f) / (0.9f * scale)));

        // ---- audio rate ---------------------------------------------------------------
        for (int s = 0; s < n; ++s)
        {
            const float dryL = l[s], dryR = r[s];
            const float frz = gate.flushing ? 0.0f : freezeS.next();
            const float inGain = (gate.flushing ? 0.0f : 1.0f) * (1.0f - frz);
            const float shimmer = shimS.next() * (1.0f - frz);
            const float m = gate.mix.next();

            bool shimChanged = false;
            const float shimSw = shimModeSwitch.tick (shimChanged);
            if (shimChanged || first)
                applyShimmerMode (shimModeSwitch.current);

            // pre-delay and input low cut
            preDelay[0].push (dryL * inGain);
            preDelay[1].push (dryR * inGain);
            const float pd = static_cast<float> (preS.next());
            float inL = preDelay[0].readLinear (pd);
            float inR = preDelay[1].readLinear (pd);
            inHpZ[0] += inHpCoeff * (inL - inHpZ[0]);
            inHpZ[1] += inHpCoeff * (inR - inHpZ[1]);
            inL -= inHpZ[0];
            inR -= inHpZ[1];

            // input diffusion
            for (int k = 0; k < 4; ++k)
            {
                inL = diff[0][k].process (inL, kDiffGain[k]);
                inR = diff[1][k].process (inR, kDiffGain[k]);
            }

            // read the lines
            float v[kLines];
            for (int i = 0; i < kLines; ++i)
            {
                const float len = static_cast<float> (lenS[i].next());
                float x;
                if ((i & 1) == 0)
                {
                    lfo[i].advance();
                    x = lines[i].readCubic (std::max (2.0f, len + modDepth * lfo[i].sine()));
                }
                else
                {
                    // settles on a whole sample (the smoother lands exactly), where this read is lossless
                    x = lines[i].readLinear (len);
                }
                // frozen: fade every line to a whole-sample read so the held sound loses nothing per pass
                if (frz > 0.0f)
                    x = lerp (x, lines[i].read (static_cast<int> (len + 0.5f)), frz);

                // damping: low-pass, and a gentle low thinning; both open up fully when frozen
                lpZ[i] += lpCoeff * (x - lpZ[i]);
                x = lerp (lpZ[i], x, frz);
                hpZ[i] += hpCoeff * (x - hpZ[i]);
                x -= hpZ[i] * (1.0f - frz);

                v[i] = x * lerp (lineGain[i], 1.0f, frz);
            }

            // outputs are tapped before the matrix
            float outL = 0.0f, outR = 0.0f;
            for (int i = 0; i < kLines; ++i)
            {
                outL += v[i] * kOutL[i];
                outR += v[i] * kOutR[i];
            }

            // shimmer: six lines add a pitch-shifted copy of themselves. The copy's energy is capped at what
            // that line has just lost to its decay gain, so each of those lines stays passive (never more out
            // than in) however the controls are set, while the un-shifted tail keeps its body.
            // The shifters' delay lines are fed all the time, so raising Shimmer never plays stale audio.
            if (shimmer > 1.0e-4f)
            {
                const float amt = shimmer * shimSw;
                for (int k = 0; k < kShimLines; ++k)
                {
                    const int idx = kShimLine[k];
                    const float g = lerp (lineGain[idx], 1.0f, frz);
                    // v = g * x  ->  v + s * shift (v) has energy g^2 (1 + s^2) x^2 <= x^2  when  s^2 <= 1 / g^2 - 1
                    const float sMax2 = std::min (4.0f, 1.0f / std::max (g * g, 0.05f) - 1.0f);
                    const float sGain = std::sqrt (std::max (0.0f, amt * sMax2));
                    const float shifted = shiftHp[k].hp (shifter[k].process (shiftLp[k].process (v[idx])));
                    v[idx] += sGain * shifted;
                }
            }
            else
            {
                for (int k = 0; k < kShimLines; ++k)
                    shifter[k].feed (shiftLp[k].process (v[kShimLine[k]]));
            }

            hadamard16 (v);

            for (int i = 0; i < kLines; ++i)
            {
                const float in = ((i & 1) == 0 ? inL : inR) * kInSign[i] * 0.3536f;   // 1 / sqrt (8)
                float w = v[i] * 0.25f + in;
                // transparent below +-4, bounded at +-8: only ever acts as a safety net
                if (std::abs (w) > 4.0f)
                    w = std::copysign (4.0f + 4.0f * softSat ((std::abs (w) - 4.0f) * 0.25f), w);
                lines[i].push (w);
            }

            // ducking keyed from the dry input
            const float env = duckEnv.process (std::max (std::abs (dryL), std::abs (dryR)));
            const float duck = 1.0f - duckS.next() * (env / (env + 0.06f)) * 0.92f;

            float wetL = outL * wetNorm * duck;
            float wetR = outR * wetNorm * duck;
            if (isBad (wetL) || isBad (wetR))
            {
                wetL = wetR = 0.0f;
                clearNetwork();
            }

            const float wetGain = std::sin (m * (0.5f * kPi));
            const float dryGain = std::cos (m * (0.5f * kPi));
            l[s] = dryL * dryGain + wetL * wetGain;
            r[s] = dryR * dryGain + wetR * wetGain;
            first = false;
        }
    }

private:
    static void hadamard16 (float* v) noexcept
    {
        for (int h = 1; h < kLines; h <<= 1)
            for (int i = 0; i < kLines; i += h << 1)
                for (int j = i; j < i + h; ++j)
                {
                    const float a = v[j], b = v[j + h];
                    v[j] = a + b;
                    v[j + h] = a - b;
                }
    }

    void applyShimmerMode (int mode) noexcept
    {
        std::array<float, kShimLines> ratios {};
        switch (mode)
        {
            default:
            case 0: ratios.fill (2.0f); break;
            case 1: ratios.fill (1.4983f); break;
            case 2: ratios.fill (0.5f); break;
            case 3: ratios = { 2.0f, 0.5f, 2.0f, 0.5f, 2.0f, 0.5f }; break;
        }
        for (int k = 0; k < kShimLines; ++k)
            shifter[k].setRatio (ratios[static_cast<size_t> (k)]);
    }

    void clearNetwork() noexcept
    {
        for (int i = 0; i < kLines; ++i)
        {
            lines[i].reset();
            lpZ[i] = hpZ[i] = 0.0f;
        }
        for (int ch = 0; ch < 2; ++ch)
        {
            preDelay[ch].reset();
            for (auto& d : diff[ch])
                d.reset();
            inHpZ[ch] = 0.0f;
        }
        for (int k = 0; k < kShimLines; ++k)
        {
            shifter[k].reset();
            shiftLp[k].reset();
            shiftHp[k].reset();
        }
    }

    // line lengths in ms at scale 1 (mutually incommensurate), scaled 0.35x .. 2x by Size
    static constexpr float kBaseMs[kLines] = { 23.1f, 26.3f, 29.9f, 33.7f, 37.3f, 41.9f, 46.1f, 50.3f,
                                               55.7f, 60.1f, 65.3f, 70.9f, 76.3f, 82.7f, 89.3f, 96.7f };
    static constexpr float kMinScale = 0.35f, kMaxScale = 2.0f;
    static constexpr float kLfoHz[kLines] = { 0.11f, 0.0f, 0.37f, 0.0f, 0.19f, 0.0f, 0.53f, 0.0f,
                                              0.29f, 0.0f, 0.71f, 0.0f, 0.13f, 0.0f, 0.43f, 0.0f };
    static constexpr float kDiffMs[2][4] = { { 4.77f, 3.59f, 12.73f, 9.31f }, { 5.29f, 4.13f, 11.87f, 8.69f } };
    static constexpr float kDiffGain[4] = { 0.72f, 0.72f, 0.62f, 0.62f };
    static constexpr float kInSign[kLines] = { 1, 1, -1, -1, 1, -1, 1, -1, -1, 1, 1, -1, -1, -1, 1, 1 };
    static constexpr float kOutL[kLines] = { 1, 0, -1, 0, 1, 0, 1, 0, -1, 0, -1, 0, 1, 0, -1, 0.0f };
    static constexpr float kOutR[kLines] = { 0, 1, 0, -1, 0, -1, 0, 1, 0, 1, 0, -1, 0, -1, 0, 1.0f };
    static constexpr int kShimLines = 6;
    static constexpr int kShimLine[kShimLines] = { 2, 5, 7, 8, 11, 13 };

    DelayLine lines[kLines];
    Lfo lfo[kLines];
    SmoothD lenS[kLines];
    float lpZ[kLines] {}, hpZ[kLines] {};
    DelayLine preDelay[2];
    Allpass diff[2][4];
    GrainShifter shifter[kShimLines];
    Biquad shiftLp[kShimLines];
    OnePole shiftHp[kShimLines];
    float inHpZ[2] { 0.0f, 0.0f };

    ModuleGate gate;
    ModeSwitch shimModeSwitch;
    Smooth decayS, sizeS, dampS, lowS, modS, shimS, freezeS, duckS;
    SmoothD preS;
    EnvFollower duckEnv;
    DriftNoise drift { 0xD1FFu };
    float fs = 48000.0f;
    bool first = true;
};

} // namespace swoon
