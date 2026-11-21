// Modulation module: chorus, three-phase ensemble, vibrato, flanger, phaser.
#pragma once

#include "Util.h"

namespace swoon
{

class Modulation
{
public:
    struct Params
    {
        bool on = true;
        int mode = 0;           // 0 chorus, 1 ensemble, 2 vibrato, 3 flanger, 4 phaser
        float rate = 0.45f;     // Hz
        float depth = 0.5f, color = 0.3f, width = 0.8f, mix = 0.5f;
    };

    void prepare (double sampleRate)
    {
        fs = static_cast<float> (sampleRate);
        for (auto& c : chans)
        {
            c.line.prepare (static_cast<int> (0.045f * fs) + 16);
            c.tone.setCutoff (7200.0f, fs);
            c.fbHp.setCutoff (120.0f, fs);
        }
        gate.prepare (fs);
        modeSwitch.prepare (fs);
        rateS.setTime (0.08f, fs / kChunk);
        depthS.setTime (0.05f, fs);
        colorS.setTime (0.05f, fs);
        widthS.setTime (0.05f, fs);
        drift.setCutoff (0.3f, fs / kChunk);
        reset();
    }

    void reset() noexcept
    {
        for (auto& c : chans)
        {
            c.line.reset();
            c.tone.reset();
            c.fbHp.reset();
            c.fb = 0.0f;
            for (auto& z : c.apZ)
                z = 0.0f;
        }
        gate.reset();
        modeSwitch.reset();
        phase = 0.0f;
        phaseFast = 0.0f;
        first = true;
    }

    void process (float* l, float* r, int n, const Params& p) noexcept
    {
        if (! gate.begin (p.on, p.mix, n, static_cast<int> (0.08f * fs), first))
        {
            first = false;
            return;
        }

        rateS.target = std::clamp (p.rate, 0.01f, 12.0f);
        depthS.target = clamp01 (p.depth);
        colorS.target = clamp01 (p.color);
        widthS.target = clamp01 (p.width);
        if (first)
        {
            rateS.snap();
            depthS.snap();
            colorS.snap();
            widthS.snap();
        }
        modeSwitch.request (std::clamp (p.mode, 0, 4));

        const float rate = rateS.next() * (1.0f + 0.04f * drift.tick());   // a little rate wander
        const float inc = rate / fs;
        const float incFast = 5.9f / fs;
        float* io[2] = { l, r };

        for (int i = 0; i < n; ++i)
        {
            phase += inc;
            phase -= std::floor (phase);
            phaseFast += incFast;
            phaseFast -= std::floor (phaseFast);

            bool changed = false;
            const float sw = modeSwitch.tick (changed);
            if (changed)
                clearState();
            const int mode = modeSwitch.current;

            const float depth = depthS.next();
            const float color = colorS.next();
            const float width = widthS.next();
            const float m = gate.mix.next();

            for (int ch = 0; ch < 2; ++ch)
            {
                auto& c = chans[ch];
                const float in = gate.flushing ? 0.0f : io[ch][i];
                // right channel LFO leads by up to a quarter cycle (half for the sweeps) as width opens
                const float side = ch == 0 ? 0.0f : width;
                float wet = 0.0f;

                switch (mode)
                {
                    default:
                    case 0:   // chorus: two voices per side, opposite phase, BBD-ish tone
                    {
                        const float ph = phase + 0.25f * side;
                        const float fbk = color * 0.45f;
                        c.line.push (in + c.fbHp.hp (c.fb) * fbk);
                        const float d1 = fs * (0.0085f + 0.0034f * depth * Lfo::sineFromPhase (ph));
                        const float d2 = fs * (0.0132f + 0.0041f * depth * Lfo::sineFromPhase (ph + 0.5f));
                        const float v1 = c.line.readCubic (d1);
                        const float v2 = c.line.readCubic (d2);
                        c.fb = v1;
                        wet = c.tone.lp (0.75f * (v1 + v2));
                        break;
                    }
                    case 1:   // ensemble: three voices, 120 degrees apart, slow + fast LFO pair
                    {
                        c.line.push (in);
                        float sum = 0.0f;
                        for (int v = 0; v < 3; ++v)
                        {
                            const float off = static_cast<float> (v) / 3.0f + 0.1667f * side;
                            const float lfo = Lfo::sineFromPhase (phase + off)
                                            + 0.11f * Lfo::sineFromPhase (phaseFast + off);
                            const float d = fs * (0.0105f + 0.0012f * static_cast<float> (v) + 0.0046f * depth * lfo * 0.9f);
                            // spread the voices: each side hears two strongly and one weakly
                            const float g = (v == (ch == 0 ? 2 : 0)) ? 0.35f + 0.65f * (1.0f - width) : 1.0f;
                            sum += g * c.line.readCubic (d);
                        }
                        wet = c.tone.lp (sum * 0.62f) * (1.0f + 0.25f * color) + in * (0.2f * color);
                        break;
                    }
                    case 2:   // vibrato: one voice, pure pitch modulation
                    {
                        c.line.push (in);
                        const float ph = phase + 0.25f * side;
                        const float d = fs * (0.0060f + 0.0028f * depth * Lfo::sineFromPhase (ph));
                        wet = lerp (c.line.readCubic (d), c.tone.lp (c.line.readCubic (d)), 0.5f * color);
                        break;
                    }
                    case 3:   // flanger: short swept delay with feedback
                    {
                        const float ph = phase + 0.5f * side;
                        const float fbk = 0.25f + 0.68f * color;
                        c.line.push (in + c.fbHp.hp (c.fb) * fbk);
                        const float sweep = 0.5f + 0.5f * Lfo::sineFromPhase (ph);
                        // exponential sweep 0.35 ms .. up to 7 ms
                        const float ms = 0.35f * std::pow (1.0f + 19.0f * depth, sweep);
                        const float v = c.line.readCubic (std::max (2.0f, fs * 0.001f * ms));
                        c.fb = softSat (v * 0.7f) * (1.0f / 0.7f);
                        c.tone.lp (v);
                        wet = v;
                        break;
                    }
                    case 4:   // phaser: six first-order allpasses, exponential sweep, feedback
                    {
                        const float ph = phase + 0.5f * side;
                        const float sweep = 0.5f + 0.5f * Lfo::sineFromPhase (ph);
                        const float hz = 240.0f * std::pow (1.0f + 14.0f * depth, sweep);
                        const float t = std::tan (kPi * std::min (hz, 0.45f * fs) / fs);
                        const float a = (t - 1.0f) / (t + 1.0f);
                        float x = in + c.fb * (0.15f + 0.72f * color);
                        for (auto& z : c.apZ)
                        {
                            const float y = a * x + z;
                            z = x - a * y;
                            x = y;
                        }
                        c.fb = softSat (x * 0.7f) * (1.0f / 0.7f);
                        wet = x;
                        break;
                    }
                }

                wet *= sw;
                if (isBad (wet))
                {
                    wet = 0.0f;
                    clearState();
                }

                // Equal-power blend: the voices are decorrelated from the dry signal, so a linear
                // crossfade would dip 3 dB at the centre. Mix = 1 is fully wet (true vibrato).
                const float wetGain = std::sin (m * (0.5f * kPi));
                const float dryGain = std::cos (m * (0.5f * kPi));
                io[ch][i] = io[ch][i] * dryGain + wet * wetGain;
            }
        }
        first = false;
    }

private:
    void clearState() noexcept
    {
        for (auto& c : chans)
        {
            c.fb = 0.0f;
            c.tone.reset();
            c.fbHp.reset();
            for (auto& z : c.apZ)
                z = 0.0f;
        }
    }

    struct Channel
    {
        DelayLine line;
        OnePole tone, fbHp;
        float fb = 0.0f;
        float apZ[6] {};
    };

    Channel chans[2];
    ModuleGate gate;
    ModeSwitch modeSwitch;
    Smooth rateS, depthS, colorS, widthS;
    DriftNoise drift { 0x51DEu };
    float fs = 48000.0f, phase = 0.0f, phaseFast = 0.0f;
    bool first = true;
};

} // namespace swoon
