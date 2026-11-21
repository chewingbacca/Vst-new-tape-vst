// Reverse module.
//
//  Swell: the "reverse reverb" of late-80s rack units - not reversed audio at
//         all, but a dense run of delay taps whose level rises towards the end
//         of the window and then stops dead, so every note blooms out of
//         nothing. Taps are jittered and decorrelated between left and right.
//  Tape:  true reverse delay - overlapping windowed grains played backwards.
//  Both:  the reverse delay feeding the swell.
#pragma once

#include "Util.h"

#include <array>

namespace swoon
{

class Reverse
{
public:
    struct Params
    {
        bool on = false;
        int mode = 0;            // 0 swell, 1 tape, 2 both
        float timeMs = 380.0f;
        float feedback = 0.2f;
        float smear = 0.6f;
        float tone = 0.7f;
        float mix = 0.6f;
    };

    static constexpr int kTaps = 32;
    static constexpr float kMaxSeconds = 1.6f;

    void prepare (double sampleRate)
    {
        fs = static_cast<float> (sampleRate);
        const int maxT = static_cast<int> (kMaxSeconds * fs);
        Rng rng (0x5EEDu);

        for (int ch = 0; ch < 2; ++ch)
        {
            swellLine[ch].prepare (maxT + 64);
            tapeLine[ch].prepare (2 * maxT + 64);
            regenLine[ch].prepare (maxT + 64);
            tone[ch].reset();
            fbLp[ch].setCutoff (5200.0f, fs);
            fbHp[ch].setCutoff (140.0f, fs);
            for (int k = 0; k < 3; ++k)
            {
                const int d = static_cast<int> (kApMs[ch][k] * 0.001f * fs);
                ap[ch][k].prepare (d + 8);
                ap[ch][k].setDelay (d);
            }

            // tap positions: even grid across (0, 1] with jitter; random polarity
            for (int k = 0; k < kTaps; ++k)
            {
                const float base = (static_cast<float> (k) + 1.0f) / static_cast<float> (kTaps);
                tapPos[ch][k] = std::clamp (base + 0.6f * rng.bipolar() / static_cast<float> (kTaps), 0.01f, 1.0f);
                tapSign[ch][k] = rng.uniform() < 0.5f ? -1.0f : 1.0f;
            }
        }

        gate.prepare (fs);
        modeSwitch.prepare (fs, 0.02f);
        timeS.setTime (0.25f, fs);
        fbS.setTime (0.05f, fs);
        smearS.setTime (0.05f, fs / kChunk);
        toneS.setTime (0.05f, fs / kChunk);
        reset();
    }

    void reset() noexcept
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            swellLine[ch].reset();
            tapeLine[ch].reset();
            regenLine[ch].reset();
            tone[ch].reset();
            fbLp[ch].reset();
            fbHp[ch].reset();
            for (auto& a : ap[ch])
                a.reset();
            tapeFb[ch] = 0.0f;
        }
        gate.reset();
        modeSwitch.reset();
        grainPhase = 0.0;
        first = true;
        lastSmear = -1.0f;
    }

    void process (float* l, float* r, int n, const Params& p) noexcept
    {
        if (! gate.begin (p.on, p.mix, n, static_cast<int> (2.2f * kMaxSeconds * fs), first))
        {
            first = false;
            return;
        }

        timeS.target = static_cast<double> (std::clamp (p.timeMs, 40.0f, kMaxSeconds * 1000.0f) * 0.001f * fs);
        fbS.target = gate.flushing ? 0.0f : std::clamp (p.feedback, 0.0f, 0.92f);
        smearS.target = clamp01 (p.smear);
        toneS.target = clamp01 (p.tone);
        if (first)
        {
            timeS.snap();
            fbS.snap();
            smearS.snap();
            toneS.snap();
        }
        modeSwitch.request (std::clamp (p.mode, 0, 2));

        const float smear = smearS.next();
        const float toneHz = expMap (toneS.next(), 900.0f, 16000.0f);
        tone[0].set (toneHz, 0.6f, fs);
        tone[1].set (toneHz, 0.6f, fs);

        if (std::abs (smear - lastSmear) > 1.0e-3f)
        {
            lastSmear = smear;
            // envelope exponent: low smear = steep, late bloom; high smear = longer, flatter rise
            const float exponent = lerp (3.2f, 1.3f, smear);
            for (int ch = 0; ch < 2; ++ch)
            {
                float energy = 0.0f;
                for (int k = 0; k < kTaps; ++k)
                {
                    tapGain[ch][k] = std::pow (tapPos[ch][k], exponent) * tapSign[ch][k];
                    energy += tapGain[ch][k] * tapGain[ch][k];
                }
                const float norm = 1.0f / std::sqrt (energy);
                for (int k = 0; k < kTaps; ++k)
                    tapGain[ch][k] *= norm;
            }
        }
        const float apGain = 0.25f + 0.5f * smear;

        float* io[2] = { l, r };
        for (int i = 0; i < n; ++i)
        {
            const float time = static_cast<float> (timeS.next());
            const float fb = fbS.next();
            const float m = gate.mix.next();
            bool changed = false;
            const float sw = modeSwitch.tick (changed);
            const int mode = modeSwitch.current;

            // reverse-grain clock (shared by both channels): two grains half a window apart
            grainPhase += 1.0 / std::max (64.0, static_cast<double> (time));   // double: no pitch error at long times
            if (grainPhase >= 1.0)
                grainPhase -= 1.0;
            const float grainPh = static_cast<float> (grainPhase);

            if (changed)
            {
                // the delay lines are kept current in every mode; only the small filters need a clean start
                for (int ch = 0; ch < 2; ++ch)
                {
                    for (auto& a : ap[ch])
                        a.reset();
                    fbLp[ch].reset();
                    fbHp[ch].reset();
                    tone[ch].reset();
                    tapeFb[ch] = 0.0f;
                }
            }

            float wet[2];
            for (int ch = 0; ch < 2; ++ch)
            {
                const float in = gate.flushing ? 0.0f : io[ch][i];
                float x = in;

                // Every line is written in every mode, so switching mode never replays old audio.
                tapeLine[ch].push (mode != 0 ? in + tapeFb[ch] * fb : in);

                if (mode != 0)
                {
                    // --- true reverse delay ---
                    float rev = 0.0f;
                    for (int g = 0; g < 2; ++g)
                    {
                        float ph = grainPh + 0.5f * static_cast<float> (g);
                        if (ph >= 1.0f)
                            ph -= 1.0f;
                        // delay grows at two samples per sample: the head moves backwards through the recording
                        const float d = 2.0f * ph * time + 2.0f;
                        const float w = std::sin (kPi * ph);
                        rev += w * w * tapeLine[ch].readLinear (d);
                    }
                    tapeFb[ch] = fbHp[ch].hp (fbLp[ch].lp (rev));
                    x = rev;
                }

                if (mode != 1)
                {
                    // --- swell: rising taps, optional regeneration through a plain delay of one window ---
                    float fed = x;
                    if (mode == 0)
                    {
                        fed += fb * fbHp[ch].hp (fbLp[ch].lp (regenLine[ch].readLinear (time)));
                        regenLine[ch].push (softSat (fed * 0.5f) * 2.0f);
                    }
                    else
                    {
                        regenLine[ch].push (fed);
                    }
                    swellLine[ch].push (fed);

                    float sum = 0.0f;
                    const float* pos = tapPos[ch].data();
                    const float* g = tapGain[ch].data();
                    for (int k = 0; k < kTaps; ++k)
                        sum += g[k] * swellLine[ch].readLinear (pos[k] * time);

                    // smear the tap pattern into a continuous bloom
                    for (int k = 0; k < 3; ++k)
                        sum = ap[ch][k].process (sum, apGain);
                    x = sum * 1.25f;
                }
                else
                {
                    swellLine[ch].push (in);
                    regenLine[ch].push (in);
                }

                wet[ch] = tone[ch].lp (x) * sw;
            }

            if (isBad (wet[0]) || isBad (wet[1]))
            {
                wet[0] = wet[1] = 0.0f;
                clearState();
            }

            const float wetGain = std::sin (m * (0.5f * kPi));
            const float dryGain = std::cos (m * (0.5f * kPi));
            l[i] = l[i] * dryGain + wet[0] * wetGain;
            r[i] = r[i] * dryGain + wet[1] * wetGain;
        }
        first = false;
    }

private:
    void clearState() noexcept
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            swellLine[ch].reset();
            tapeLine[ch].reset();
            regenLine[ch].reset();
            tone[ch].reset();
            fbLp[ch].reset();
            fbHp[ch].reset();
            for (auto& a : ap[ch])
                a.reset();
            tapeFb[ch] = 0.0f;
        }
    }

    static constexpr float kApMs[2][3] = { { 2.3f, 5.9f, 9.7f }, { 2.9f, 5.3f, 10.3f } };

    DelayLine swellLine[2], tapeLine[2], regenLine[2];
    Allpass ap[2][3];
    Svf tone[2];
    OnePole fbLp[2], fbHp[2];
    std::array<float, kTaps> tapPos[2], tapGain[2], tapSign[2];
    float tapeFb[2] { 0.0f, 0.0f };

    ModuleGate gate;
    ModeSwitch modeSwitch;
    SmoothD timeS;
    Smooth fbS, smearS, toneS;
    double grainPhase = 0.0;
    float fs = 48000.0f, lastSmear = -1.0f;
    bool first = true;
};

} // namespace swoon
