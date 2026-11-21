// Soft focus: the classic rack "dream" patch rebuilt from parts - a four-voice
// micro-detuned chorus, two long delays at a 250 : 380 ms ratio feeding each
// other, and a dense plate-like haze over everything.
#pragma once

#include "Util.h"

namespace swoon
{

/** Compact 8-line FDN used as the haze reverb inside Soft Focus. */
class HazeVerb
{
public:
    static constexpr int kLines = 8;

    void prepare (float rate)
    {
        fs = rate;
        for (int i = 0; i < kLines; ++i)
        {
            len[i] = static_cast<int> (kMs[i] * 0.001f * fs);
            lines[i].prepare (len[i] + static_cast<int> (0.004f * fs) + 16);
            lfo[i].setRate (0.21f + 0.17f * static_cast<float> (i), fs);
            lfo[i].setPhase (0.13f * static_cast<float> (i));
        }
        for (int ch = 0; ch < 2; ++ch)
            for (int k = 0; k < 3; ++k)
            {
                const int d = static_cast<int> (kApMs[ch][k] * 0.001f * fs);
                ap[ch][k].prepare (d + 8);
                ap[ch][k].setDelay (d);
            }
        reset();
    }

    void reset() noexcept
    {
        for (int i = 0; i < kLines; ++i)
        {
            lines[i].reset();
            lpZ[i] = 0.0f;
        }
        for (auto& side : ap)
            for (auto& a : side)
                a.reset();
    }

    /** Call once per chunk. */
    void set (float rt60, float dampHz) noexcept
    {
        for (int i = 0; i < kLines; ++i)
            gain[i] = std::pow (10.0f, -3.0f * (static_cast<float> (len[i]) / fs) / std::max (0.1f, rt60));
        lpCoeff = 1.0f - std::exp (-kTwoPi * std::min (dampHz, 0.45f * fs) / fs);
        modDepth = 0.0009f * fs;
    }

    void process (float inL, float inR, float& outL, float& outR) noexcept
    {
        for (int k = 0; k < 3; ++k)
        {
            inL = ap[0][k].process (inL, 0.68f);
            inR = ap[1][k].process (inR, 0.68f);
        }

        float v[kLines];
        for (int i = 0; i < kLines; ++i)
        {
            float x;
            if (i < 4)
            {
                lfo[i].advance();
                x = lines[i].readCubic (static_cast<float> (len[i]) + modDepth * lfo[i].sine());
            }
            else
            {
                x = lines[i].read (len[i]);
            }
            lpZ[i] += lpCoeff * (x - lpZ[i]);
            v[i] = lpZ[i] * gain[i];
        }

        outL = v[0] - v[2] + v[4] - v[6];
        outR = v[1] - v[3] + v[5] - v[7];

        // Hadamard 8
        for (int h = 1; h < kLines; h <<= 1)
            for (int i = 0; i < kLines; i += h << 1)
                for (int j = i; j < i + h; ++j)
                {
                    const float a = v[j], b = v[j + h];
                    v[j] = a + b;
                    v[j + h] = a - b;
                }

        static constexpr float sign[kLines] = { 1, 1, -1, 1, -1, -1, 1, -1 };
        for (int i = 0; i < kLines; ++i)
            lines[i].push (v[i] * 0.35355339f + ((i & 1) == 0 ? inL : inR) * sign[i] * 0.5f);
    }

private:
    static constexpr float kMs[kLines] = { 31.7f, 37.1f, 43.3f, 47.9f, 53.9f, 61.3f, 67.7f, 73.1f };
    static constexpr float kApMs[2][3] = { { 3.1f, 7.3f, 11.9f }, { 3.7f, 6.7f, 12.7f } };

    DelayLine lines[kLines];
    Allpass ap[2][3];
    Lfo lfo[kLines];
    int len[kLines] {};
    float gain[kLines] {}, lpZ[kLines] {};
    float fs = 48000.0f, lpCoeff = 0.5f, modDepth = 40.0f;
};

class SoftFocus
{
public:
    struct Params
    {
        bool on = true;
        float timeMs = 250.0f;   // left delay
        float rightRatio = 1.52f; // right delay = left x this (1.52 free-running, 1.5 when tempo-synced)
        float feedback = 0.35f;
        float detuneCents = 9.0f;
        float haze = 0.5f;
        float tone = 0.6f;
        float mix = 0.35f;
    };

    static constexpr float kRightRatio = 1.52f;   // 250 ms : 380 ms

    void prepare (double sampleRate)
    {
        fs = static_cast<float> (sampleRate);
        for (int ch = 0; ch < 2; ++ch)
        {
            chorus[ch].prepare (static_cast<int> (0.03f * fs) + 16);
            delay[ch].prepare (static_cast<int> (0.9f * kRightRatio * fs) + 64);
            fbLp[ch].reset();
            fbHp[ch].setCutoff (150.0f, fs);
        }
        haze.prepare (fs);
        gate.prepare (fs);
        timeS.setTime (0.18f, fs);
        ratioS.setTime (0.18f, fs);
        ratioS.snap (kRightRatio);
        fbS.setTime (0.05f, fs);
        detuneS.setTime (0.05f, fs / kChunk);
        hazeS.setTime (0.05f, fs);
        toneS.setTime (0.05f, fs / kChunk);
        // four voices at unrelated slow rates
        static constexpr float rates[4] = { 0.83f, 1.31f, 1.07f, 1.63f };
        for (int v = 0; v < 4; ++v)
        {
            lfo[v].setRate (rates[v], fs);
            lfo[v].setPhase (0.27f * static_cast<float> (v));
        }
        reset();
    }

    void reset() noexcept
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            chorus[ch].reset();
            delay[ch].reset();
            fbLp[ch].reset();
            fbHp[ch].reset();
            hazeLp[ch].reset();
        }
        haze.reset();
        gate.reset();
        first = true;
    }

    void process (float* l, float* r, int n, const Params& p) noexcept
    {
        // long enough to empty the delays at maximum feedback-free length plus the haze
        if (! gate.begin (p.on, p.mix, n, static_cast<int> (2.5f * fs), first))
        {
            for (int i = 0; i < n; ++i)
            {
                chorus[0].push (l[i]);                        // keep the chorus lines current so waking is seamless
                chorus[1].push (r[i]);
            }
            first = false;
            return;
        }

        timeS.target = std::clamp (p.timeMs, 40.0f, 900.0f) * 0.001f * fs;
        ratioS.target = std::clamp (p.rightRatio, 1.0f, kRightRatio);
        fbS.target = gate.flushing ? 0.0f : std::clamp (p.feedback, 0.0f, 0.92f);
        detuneS.target = std::clamp (p.detuneCents, 0.0f, 50.0f);
        hazeS.target = clamp01 (p.haze);
        toneS.target = clamp01 (p.tone);
        if (first)
        {
            timeS.snap();
            fbS.snap();
            detuneS.snap();
            hazeS.snap();
            toneS.snap();
        }

        const float detune = detuneS.next();
        const float tone = toneS.next();
        const float toneHz = expMap (tone, 1400.0f, 15000.0f);
        for (int ch = 0; ch < 2; ++ch)
        {
            fbLp[ch].setCutoff (toneHz, fs);
            hazeLp[ch].setCutoff (toneHz * 1.2f, fs);
        }
        const float hazeNow = hazeS.target;
        haze.set (gate.flushing ? 0.2f : 1.2f + 4.0f * hazeNow, toneHz * 0.9f);

        // LFO depth (in samples) giving a peak pitch deviation of `detune` cents for each voice
        float depth[4];
        static constexpr float rates[4] = { 0.83f, 1.31f, 1.07f, 1.63f };
        const float dev = std::exp2 (detune / 1200.0f) - 1.0f;
        for (int v = 0; v < 4; ++v)
            depth[v] = dev * fs / (kTwoPi * rates[v]);
        static constexpr float baseMs[4] = { 9.3f, 13.7f, 11.1f, 15.9f };

        for (int i = 0; i < n; ++i)
        {
            const float dryL = l[i], dryR = r[i];
            const float inL = dryL, inR = dryR;
            const float feed = gate.flushing ? 0.0f : 1.0f;
            const float time = static_cast<float> (timeS.next());
            const float fb = fbS.next();
            const float hz = hazeS.next();
            const float m = gate.mix.next();

            // --- four-voice detuned chorus (two voices per side) ---
            chorus[0].push (inL);
            chorus[1].push (inR);
            float voice[4];
            for (int v = 0; v < 4; ++v)
            {
                lfo[v].advance();
                const float d = baseMs[v] * 0.001f * fs + depth[v] * lfo[v].sine();
                voice[v] = chorus[v & 1].readCubic (std::max (2.0f, d));
            }
            const float cL = feed * 0.6f * (voice[0] + 0.7f * voice[2] + 0.3f * voice[3]);
            const float cR = feed * 0.6f * (voice[1] + 0.7f * voice[3] + 0.3f * voice[2]);

            // --- two long delays, cross-fed, filtered in the loop ---
            const float dL = delay[0].readCubic (std::max (4.0f, time));
            const float dR = delay[1].readCubic (std::max (4.0f, time * ratioS.next()));
            const float fL = fbHp[0].hp (fbLp[0].lp (dL));
            const float fR = fbHp[1].hp (fbLp[1].lp (dR));
            // keep the loop honest: saturate gently on the way back in
            delay[0].push (cL + softSat ((0.75f * fL + 0.25f * fR) * fb * 0.5f) * 2.0f);
            delay[1].push (cR + softSat ((0.75f * fR + 0.25f * fL) * fb * 0.5f) * 2.0f);

            // --- haze ---
            float hL = 0.0f, hR = 0.0f;
            haze.process (0.55f * cL + 0.8f * fL, 0.55f * cR + 0.8f * fR, hL, hR);
            hL = hazeLp[0].lp (hL);
            hR = hazeLp[1].lp (hR);

            float wetL = 0.45f * cL + 0.62f * fL + hz * 0.5f * hL;
            float wetR = 0.45f * cR + 0.62f * fR + hz * 0.5f * hR;
            if (isBad (wetL) || isBad (wetR))
            {
                wetL = wetR = 0.0f;
                const bool wasFirst = first;
                reset();
                first = wasFirst;
                gate.mix.snap (m);
            }

            const float wetGain = std::sin (m * (0.5f * kPi));
            const float dryGain = std::cos (m * (0.5f * kPi));
            l[i] = dryL * dryGain + wetL * wetGain;
            r[i] = dryR * dryGain + wetR * wetGain;
        }
        first = false;
    }

private:
    DelayLine chorus[2], delay[2];
    OnePole fbLp[2], fbHp[2], hazeLp[2];
    HazeVerb haze;
    Lfo lfo[4];
    ModuleGate gate;
    SmoothD timeS;
    Smooth fbS, detuneS, hazeS, toneS, ratioS;
    float fs = 48000.0f;
    bool first = true;
};

} // namespace swoon
