// AHDSR envelope with exponential segments. Retriggering starts the attack
// from the current level, so there is never a discontinuity.
#pragma once
#include "../util/Math.h"

namespace atm {

struct Envelope
{
    enum Stage : uint8_t { Idle, Attack, Hold, Decay, Sustain, Release, Kill };

    Stage stage = Idle;
    float level = 0.f;
    float sr = 48000.f;
    float aCoef = 1.f, dCoef = 0.01f, rCoef = 0.01f, kCoef = 0.01f, sustain = 1.f;
    int   holdSamples = 0, holdLeft = 0;

    void prepare (float sampleRate) { sr = sampleRate; kCoef = coefFor (0.003f); }

    // Time for the segment to get within ~-60 dB of its target.
    float coefFor (float seconds) const
    {
        const float n = std::max (1.f, seconds * sr);
        return 1.f - std::exp (-6.9078f / n);
    }
    void set (float a, float h, float d, float s, float r)
    {
        // attack aims above 1 so it arrives with a nearly linear ramp; scale so that `a` is the time to reach 1
        aCoef = 1.f - std::exp (-1.2017f / std::max (1.f, a * sr));     // ln(1.43/0.43): reaches 1.0 after `a` seconds
        holdSamples = (int) (h * sr);
        dCoef = coefFor (d);
        rCoef = coefFor (r);
        sustain = clamp01 (s);
    }
    void gateOn (bool legato = false)
    {
        if (legato && stage != Idle && stage != Release && stage != Kill) return;
        stage = Attack;
    }
    void gateOff() { if (stage != Idle && stage != Kill) stage = Release; }
    void kill (float seconds = 0.003f) { if (stage != Idle) { stage = Kill; kCoef = coefFor (seconds); } }
    void reset() { stage = Idle; level = 0.f; }
    bool active() const { return stage != Idle; }

    inline float tick()
    {
        switch (stage)
        {
            case Idle: return 0.f;
            case Attack:
                level += aCoef * (1.43f - level);
                if (level >= 1.f) { level = 1.f; holdLeft = holdSamples; stage = holdSamples > 0 ? Hold : Decay; }
                break;
            case Hold:
                if (--holdLeft <= 0) stage = Decay;
                break;
            case Decay:
                level += dCoef * (sustain - level);
                if (level - sustain < 1.0e-4f) { level = sustain; stage = Sustain; if (sustain <= 0.f) { stage = Idle; level = 0.f; } }
                break;
            case Sustain:
                level += 0.01f * (sustain - level);      // follow sustain changes smoothly
                break;
            case Release:
                level -= rCoef * level;
                if (level < 1.0e-4f) { level = 0.f; stage = Idle; }
                break;
            case Kill:
                level -= kCoef * level;
                if (level < 1.0e-4f) { level = 0.f; stage = Idle; }
                break;
        }
        return level;
    }
};

// Simple one-shot exponential decay (for pitch sweeps, drum bodies, noise bursts).
struct DecayEnv
{
    float level = 0.f, coef = 0.999f;
    void setTime (float seconds, float sr) { coef = std::exp (-6.9078f / std::max (1.f, seconds * sr)); }
    void trigger (float v = 1.f) { level = v; }
    inline float tick() { const float v = level; level *= coef; return v; }
    void reset() { level = 0.f; }
};

// Linear ramp smoother for gains; set a target and a ramp length in samples.
struct LinearSmooth
{
    float cur = 0.f, target = 0.f, step = 0.f; int left = 0;
    void snap (float v) { cur = target = v; left = 0; step = 0.f; }
    void set (float v, int samples)
    {
        if (v == target && left > 0) return;
        target = v;
        if (samples <= 0 || v == cur) { cur = v; left = 0; step = 0.f; return; }
        left = samples; step = (target - cur) / (float) samples;
    }
    inline float tick() { if (left > 0) { cur += step; if (--left == 0) cur = target; } return cur; }
    bool moving() const { return left > 0; }
};

} // namespace atm
