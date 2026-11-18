// Verdant — multiband ensemble chorus, up to 24 voices.
//
//   lows  (below "Low Keep")  ────────────────────────────────► stay dry and solid
//   highs ─► delay line ─► N modulated taps, panned ─► wet ─► mixed with dry highs
//
// Smooth: 7-24 ms taps with slow sine LFOs spread in phase and rate (classic ensemble).
// Glassy: 1.5-7.5 ms taps, shallower modulation, a little resonator-style feedback and
//         a brighter, thinner wet path. Mode is a continuous morph, so switching is click-free.
#pragma once

#include "Util.h"

namespace verdant
{

class Chorus
{
public:
    static constexpr int kMaxVoices = 24;

    struct Controls
    {
        bool  on = true;
        float mix = 0.45f;
        int   voices = 8;
        float rateHz = 0.5f, depth = 0.4f;
        float glass = 0.0f;       // 0 = smooth, 1 = glassy
        float width = 1.0f;
        float xoverHz = 220.0f;
    };

    void prepare (float sampleRate)
    {
        sr = sampleRate;
        tickRate = sr / 32.0f;
        for (auto& d : dl) d.prepare (int (0.070f * sr));

        for (int i = 0; i < kMaxVoices; ++i)
        {
            Voice& v = voices[size_t (i)];
            const float fi = float (i);
            v.phase = frac (fi * 0.6180340f + 0.11f);
            v.phase2 = frac (fi * 0.4142136f + 0.47f);
            v.rateMul = 0.82f + 0.36f * frac (fi * 0.7548777f + 0.3f);
            v.baseSmoothMs = 7.0f + 17.0f * frac (fi * 0.5698403f + 0.21f);
            v.baseGlassMs = 1.5f + 6.0f * frac (fi * 0.6823278f + 0.05f);
            v.side = (i & 1) ? 1.0f : -1.0f;
            v.panAmount = 0.25f + 0.75f * frac (fi * 0.3247180f + 0.6f);
            v.src = i & 1;
            v.gain = 0.0f;
        }

        sMix.setTime (0.03f, tickRate);
        sGlass.setTime (0.15f, tickRate);
        sWidth.setTime (0.05f, tickRate);
        sXover.setTime (0.05f, tickRate);
        sRate.setTime (0.1f, tickRate);
        sDepth.setTime (0.1f, tickRate);
        voiceCoef = 1.0f - std::exp (-1.0f / (0.04f * tickRate));
        reset();
    }

    void reset()
    {
        for (auto& d : dl) d.clear();
        for (auto& f : xo1) f.reset();
        for (auto& f : xo2) f.reset();
        for (auto& f : wetHp) f.reset();
        for (auto& f : wetShelf) f.reset();
        fb[0] = fb[1] = 0.0f;
        first = true;
    }

    void control (const Controls& c)
    {
        sMix.set (c.on ? clampf (c.mix, 0.0f, 1.0f) : 0.0f);
        sGlass.set (clampf (c.glass, 0.0f, 1.0f));
        sWidth.set (clampf (c.width, 0.0f, 1.0f));
        sXover.set (clampf (c.xoverHz, 30.0f, 1200.0f));
        sRate.set (clampf (c.rateHz, 0.02f, 12.0f));
        sDepth.set (clampf (c.depth, 0.0f, 1.0f));
        if (first)
            for (auto* s : { &sMix, &sGlass, &sWidth, &sXover, &sRate, &sDepth })
                s->snap (s->target);

        const float mix = sMix.tick(), glass = sGlass.tick(), width = sWidth.tick();
        const float xover = sXover.tick(), rate = sRate.tick(), depth = sDepth.tick();

        active = mix > 1.0e-4f || sMix.target > 0.0f;

        for (int c2 = 0; c2 < 2; ++c2)
        {
            xo1[c2].setCutoff (xover, sr);
            xo2[c2].setCutoff (xover, sr);
            wetHp[c2].setCutoff (20.0f + 380.0f * glass, sr);
            wetShelf[c2].setHigh (4000.0f, 4.5f * glass, sr);
        }
        fbAmount = 0.5f * glass;

        // ---- voices
        const int nVoices = std::clamp (c.voices, 1, kMaxVoices);
        const float dt = 1.0f / tickRate;
        float power = 0.0f;
        int perSide[2] = { 0, 0 };
        numRunning = 0;

        for (int i = 0; i < kMaxVoices; ++i)
        {
            Voice& v = voices[size_t (i)];
            const float gt = i < nVoices ? 1.0f : 0.0f;
            if (first) v.gain = gt;
            v.gain += voiceCoef * (gt - v.gain);
            if (v.gain < 1.0e-4f && gt == 0.0f) { v.gain = 0.0f; }

            const float f = rate * v.rateMul;
            v.phase += f * dt;            v.phase -= std::floor (v.phase);
            v.phase2 += f * 0.271f * dt;  v.phase2 -= std::floor (v.phase2);

            // pitch deviation is what the ear hears, so depth is defined in cents and
            // converted to a delay excursion: A = dev / (2 pi f)
            const float dev = 0.012f * depth * lerp (1.0f, 0.4f, glass);
            const float capMs = lerp (4.5f, 1.1f, glass);
            const float excMs = std::min (capMs, 1000.0f * dev / (kTwoPi * f));
            const float lfo = 0.8f * fastSin01 (v.phase) + 0.2f * fastSin01 (v.phase2);
            const float baseMs = lerp (v.baseSmoothMs, v.baseGlassMs, glass);
            const float delay = std::max (2.0f, (baseMs + excMs * lfo) * 0.001f * sr);

            const float ang = (v.side * v.panAmount * width + 1.0f) * 0.25f * kPi;
            const float gl = v.gain * std::cos (ang), gr = v.gain * std::sin (ang);

            if (first) { v.rDelay.snap (delay); v.rGl.snap (gl); v.rGr.snap (gr); }
            v.rDelay.begin (delay, 32);
            v.rGl.begin (gl, 32);
            v.rGr.begin (gr, 32);

            if (v.gain > 0.0f || gt > 0.0f)
            {
                running[numRunning++] = i;
                power += v.gain * v.gain;
                ++perSide[v.src];
            }
        }

        const float norm = std::sqrt (2.0f / std::max (power, 0.25f));
        fbNorm[0] = perSide[0] > 0 ? 1.0f / float (perSide[0]) : 0.0f;
        fbNorm[1] = perSide[1] > 0 ? 1.0f / float (perSide[1]) : 0.0f;

        const float dryG = std::cos (mix * 0.5f * kPi);
        const float wetG = std::sin (mix * 0.5f * kPi) * norm;
        if (first) { rDry.snap (dryG); rWet.snap (wetG); first = false; }
        rDry.begin (dryG, 32);
        rWet.begin (wetG, 32);
    }

    // In place.
    void process (float* l, float* r, int n)
    {
        if (! active)
        {
            // keep the delay lines fed so re-enabling is seamless
            for (int i = 0; i < n; ++i)
            {
                float x[2] = { l[i], r[i] };
                for (int c = 0; c < 2; ++c)
                {
                    const float low = xo2[c].process (xo1[c].process (x[c]));
                    dl[c].push (x[c] - low);
                }
            }
            for (int k = 0; k < numRunning; ++k)
            {
                Voice& v = voices[size_t (running[k])];
                for (int i = 0; i < n; ++i) { v.rDelay.next(); v.rGl.next(); v.rGr.next(); }
            }
            for (int i = 0; i < n; ++i) { rDry.next(); rWet.next(); }
            return;
        }

        for (int i = 0; i < n; ++i)
        {
            float x[2] = { l[i], r[i] }, low[2], high[2];
            for (int c = 0; c < 2; ++c)
            {
                low[c] = xo2[c].process (xo1[c].process (x[c]));
                high[c] = x[c] - low[c];
                dl[c].push (high[c] + fbAmount * softClip (fb[c]));
            }

            float wl = 0.0f, wr = 0.0f, fbSum[2] = { 0.0f, 0.0f };
            for (int k = 0; k < numRunning; ++k)
            {
                Voice& v = voices[size_t (running[k])];
                const float tap = dl[v.src].tapHermite (v.rDelay.next());
                wl += tap * v.rGl.next();
                wr += tap * v.rGr.next();
                fbSum[v.src] += tap * v.gain;
            }
            fb[0] = fbSum[0] * fbNorm[0];
            fb[1] = fbSum[1] * fbNorm[1];

            wl = wetShelf[0].process (wetHp[0].process (wl));
            wr = wetShelf[1].process (wetHp[1].process (wr));

            const float dg = rDry.next(), wg = rWet.next();
            l[i] = low[0] + high[0] * dg + wl * wg;
            r[i] = low[1] + high[1] * dg + wr * wg;
        }
    }

private:
    static float frac (float x) { return x - std::floor (x); }

    struct Voice
    {
        float phase = 0, phase2 = 0, rateMul = 1, baseSmoothMs = 12, baseGlassMs = 3;
        float side = 1, panAmount = 1, gain = 0;
        int src = 0;
        Ramp rDelay, rGl, rGr;
    };

    float sr = 48000.0f, tickRate = 1500.0f;
    Ring dl[2];
    OnePoleLP xo1[2], xo2[2];
    OnePoleHP wetHp[2];
    Shelf1 wetShelf[2];
    std::array<Voice, kMaxVoices> voices;
    int running[kMaxVoices] = {};
    int numRunning = 0;
    float fb[2] = { 0, 0 }, fbNorm[2] = { 0, 0 }, fbAmount = 0.0f, voiceCoef = 0.1f;
    Smooth sMix, sGlass, sWidth, sXover, sRate, sDepth;
    Ramp rDry, rWet;
    bool first = true, active = true;
};

} // namespace verdant
