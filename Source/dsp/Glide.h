// Glide: the held-tremolo-arm sound. The whole signal is replayed by a
// variable-speed read head, exactly like bending strings or nudging a tape
// reel: pitch = playback speed, with no granular artefacts while it drifts.
// Only when the head wanders out of its window is it spliced back, at a
// point chosen by correlation search so the join is seamless.
//
// Three movements are summed: a slow wobble LFO, band-limited random drift,
// and an envelope-triggered dip that drops the pitch on each strum and lets
// it swim back up - the gesture of strumming with the arm in your hand.
#pragma once

#include "Util.h"

namespace swoon
{

class Glide
{
public:
    struct Params
    {
        bool on = true;
        float wobbleCents = 10.0f;
        float rateHz = 0.7f;
        float driftCents = 12.0f;
        float dipCents = 30.0f;
        float returnMs = 350.0f;
        float spread = 0.35f;
        float mix = 1.0f;
    };

    void prepare (double sampleRate)
    {
        fs = static_cast<float> (sampleRate);
        dLo = 0.0006f * fs + 6.0f;
        dHi = 0.046f * fs;
        rest = 0.010f * fs;
        searchRange = static_cast<int> (0.003f * fs);
        corrLen = static_cast<int> (0.006f * fs);
        stride = std::max (1, static_cast<int> (fs / 48000.0f + 0.5f));
        xfStep = 1.0f / (0.012f * fs);

        for (auto& h : heads)
            h.line.prepare (static_cast<int> (dHi) + 2 * searchRange + corrLen + static_cast<int> (0.01f * fs) + 64);

        gate.prepare (fs);
        wobbleS.setTime (0.06f, fs);
        driftS.setTime (0.06f, fs);
        dipS.setTime (0.06f, fs);
        spreadS.setTime (0.06f, fs);
        rateS.setTime (0.1f, fs / kChunk);
        driftCommon.setCutoff (0.28f, fs / kChunk);
        driftSide[0].setCutoff (0.33f, fs / kChunk);
        driftSide[1].setCutoff (0.24f, fs / kChunk);
        rateDrift.setCutoff (0.15f, fs / kChunk);
        for (auto& d : driftSmooth)
            d.setTime (0.03f, fs);
        envFast.setTimes (0.001f, 0.025f, fs);
        envSlow.setTimes (0.04f, 0.35f, fs);
        dipAttack = 1.0f - std::exp (-1.0f / (0.014f * fs));
        reset();
    }

    void reset() noexcept
    {
        for (auto& h : heads)
        {
            h.line.reset();
            h.delay = rest;
            h.oldDelay = rest;
            h.xf = 1.0f;
            h.xfPower = 1.0f;
        }
        gate.reset();
        envFast.reset();
        envSlow.reset();
        driftCommon.reset();
        driftSide[0].reset();
        driftSide[1].reset();
        phase = 0.0f;
        dipEnv = dipTarget = slowAtTrigger = 0.0f;
        dipHold = holdoff = 0;
        first = true;
    }

    void process (float* l, float* r, int n, const Params& p) noexcept
    {
        if (! gate.begin (p.on, p.mix, n, static_cast<int> (0.12f * fs), first))
        {
            first = false;
            return;
        }

        wobbleS.target = std::clamp (p.wobbleCents, 0.0f, 150.0f);
        driftS.target = std::clamp (p.driftCents, 0.0f, 150.0f);
        dipS.target = std::clamp (p.dipCents, 0.0f, 500.0f);
        spreadS.target = clamp01 (p.spread);
        rateS.target = std::clamp (p.rateHz, 0.02f, 12.0f);
        if (first)
        {
            wobbleS.snap();
            driftS.snap();
            dipS.snap();
            spreadS.snap();
            rateS.snap();
        }

        const float rate = rateS.next() * (1.0f + 0.12f * rateDrift.tick());   // a hand is never metronomic
        const float phaseInc = rate / fs;
        const float common = driftCommon.tick();
        const float sideL = driftSide[0].tick(), sideR = driftSide[1].tick();
        const float spreadNow = spreadS.target;
        const float cw = std::sqrt (1.0f - 0.75f * spreadNow), sw = std::sqrt (0.75f * spreadNow);
        driftSmooth[0].target = cw * common + sw * sideL;
        driftSmooth[1].target = cw * common + sw * sideR;
        const float releaseCoeff = 1.0f - std::exp (-1.0f / (std::clamp (p.returnMs, 20.0f, 4000.0f) * 0.001f * 0.45f * fs));

        float* io[2] = { l, r };
        for (int i = 0; i < n; ++i)
        {
            const float inL = gate.flushing ? 0.0f : io[0][i];
            const float inR = gate.flushing ? 0.0f : io[1][i];

            // ---- strum detector -> dip envelope ----------------------------------------
            const float peak = std::max (std::abs (inL), std::abs (inR));
            const float slowBefore = envSlow.env;
            const float fast = envFast.process (peak);
            envSlow.process (peak);
            if (holdoff > 0)
                --holdoff;
            else if (fast > 0.004f && fast > 1.7f * slowBefore + 0.002f)
            {
                slowAtTrigger = slowBefore;
                dipHold = static_cast<int> (0.03f * fs);
                holdoff = static_cast<int> (0.09f * fs);
            }
            if (dipHold > 0)
            {
                // keep measuring while the attack develops: harder strums (relative to what was ringing) dip further
                const float strength = clamp01 (0.55f + 0.45f * (fast / (slowAtTrigger + 0.02f) - 1.7f) / 2.5f);
                dipTarget = std::max (dipTarget, strength);
                --dipHold;
                dipEnv += dipAttack * (dipTarget - dipEnv);
            }
            else
            {
                dipTarget = 0.0f;
                dipEnv -= releaseCoeff * dipEnv;
            }

            phase += phaseInc;
            phase -= std::floor (phase);

            const float wobble = wobbleS.next();
            const float drift = driftS.next();
            const float dip = dipS.next();
            const float spread = spreadS.next();
            const float m = gate.mix.next();
            const float maxPull = 2.5f + 0.1f * (wobble + drift + dip);

            const float in[2] = { inL, inR };
            for (int ch = 0; ch < 2; ++ch)
            {
                auto& h = heads[ch];
                h.line.push (in[ch]);

                const float lfo = Lfo::sineFromPhase (phase + (ch == 0 ? 0.0f : 0.25f * spread));
                // weak spring towards the rest delay: sharpens very slightly if the head has fallen behind
                const float pull = std::clamp ((h.delay - rest) * (1000.0f / fs), -maxPull, maxPull);
                const float cents = wobble * lfo + drift * 0.6f * driftSmooth[ch].next() - dip * dipEnv + pull;

                // ratio - 1 for small intervals: exp (x) - 1 ~ x + x^2 / 2
                const float x = cents * (0.69314718f / 1200.0f);
                const float speedErr = x + 0.5f * x * x;      // ratio - 1
                h.delay -= speedErr;

                float out;
                if (h.xf < 1.0f)
                {
                    h.oldDelay -= speedErr;
                    h.oldDelay = std::clamp (h.oldDelay, 2.0f, static_cast<float> (h.line.capacity() - 4));
                    h.xf = std::min (1.0f, h.xf + xfStep);
                    const float a = h.line.readCubic (h.oldDelay);
                    const float b = h.line.readCubic (std::clamp (h.delay, 2.0f, static_cast<float> (h.line.capacity() - 4)));
                    const float t = smoothstep (h.xf);
                    out = a * std::pow (1.0f - t, h.xfPower) + b * std::pow (t, h.xfPower);
                }
                else
                {
                    if (h.delay > dHi)
                        splice (h, rest);
                    else if (h.delay < dLo)
                        splice (h, rest + 0.012f * fs);
                    out = h.line.readCubic (std::clamp (h.xf < 1.0f ? h.oldDelay : h.delay, 2.0f,
                                                        static_cast<float> (h.line.capacity() - 4)));
                }

                io[ch][i] = io[ch][i] * (1.0f - m) + out * m;
            }
        }
        first = false;
    }

private:
    struct Head
    {
        DelayLine line;
        float delay = 480.0f, oldDelay = 480.0f, xf = 1.0f, xfPower = 1.0f;
    };

    /** Starts a crossfade to a new head near `target`, at the lag whose recent audio best matches the current head's. */
    void splice (Head& h, float target) noexcept
    {
        const int cur = static_cast<int> (h.delay);
        const int centre = static_cast<int> (target);
        int best = centre;
        float bestScore = -2.0f;

        float energyA = 1.0e-12f;
        for (int j = 0; j < corrLen; j += stride)
        {
            const float a = h.line.read (cur + j);
            energyA += a * a;
        }

        for (int lag = centre - searchRange; lag <= centre + searchRange; lag += stride)
        {
            if (lag < 4)
                continue;
            float dot = 0.0f, energyB = 1.0e-12f;
            for (int j = 0; j < corrLen; j += stride)
            {
                const float a = h.line.read (cur + j);
                const float b = h.line.read (lag + j);
                dot += a * b;
                energyB += b * b;
            }
            const float score = dot / std::sqrt (energyA * energyB);
            if (score > bestScore)
            {
                bestScore = score;
                best = lag;
            }
        }

        h.oldDelay = h.delay;
        h.delay = static_cast<float> (best) + (h.delay - static_cast<float> (cur));
        h.xf = 0.0f;
        // well-matched material sums in phase (linear fade); unmatched material wants an equal-power fade
        h.xfPower = lerp (0.5f, 1.0f, clamp01 (bestScore));
    }

    Head heads[2];
    ModuleGate gate;
    Smooth wobbleS, driftS, dipS, spreadS, rateS;
    Smooth driftSmooth[2];
    DriftNoise driftCommon { 0x6111DEu }, rateDrift { 0x7A7Eu };
    DriftNoise driftSide[2] { DriftNoise (0x1EF7u), DriftNoise (0x819470u) };
    EnvFollower envFast, envSlow;
    float fs = 48000.0f, dLo = 32.0f, dHi = 2200.0f, rest = 480.0f, xfStep = 0.002f;
    float phase = 0.0f, dipEnv = 0.0f, dipTarget = 0.0f, dipAttack = 0.01f, slowAtTrigger = 0.0f;
    int searchRange = 144, corrLen = 288, stride = 1, dipHold = 0, holdoff = 0;
    bool first = true;
};

} // namespace swoon
