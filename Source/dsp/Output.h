// Output stage: tilt EQ, stereo width, gain, and the safety limiter.
#pragma once

#include "Util.h"

namespace swoon
{

/** Zero-latency two-part limiter: a gain rider for sustained overs followed by a
    soft-knee clipper that guarantees the ceiling. */
class SoftLimiter
{
public:
    void prepare (float rate) noexcept
    {
        attack = 1.0f - std::exp (-1.0f / (0.0015f * rate));
        release = 1.0f - std::exp (-1.0f / (0.16f * rate));
        meterDecay = std::exp (-1.0f / (0.04f * rate));
        reset();
    }
    void reset() noexcept { env = 0.0f; reductionDb = 0.0f; }

    void process (float& l, float& r) noexcept
    {
        const float peak = std::max (std::abs (l), std::abs (r));
        env += (peak > env ? attack : release) * (peak - env);

        float g = 1.0f;
        if (env > kThreshold)
            g = kThreshold / env;

        l = clip (l * g);
        r = clip (r * g);
        reductionDb = std::max (reductionDb * meterDecay, -gainToDb (g));
    }

    float getReductionDb() const noexcept { return reductionDb; }

    static constexpr float kThreshold = 0.70f;   // rider target, about -3 dBFS
    static constexpr float kKnee = 0.75f;        // clipper is linear below this
    static constexpr float kCeiling = 0.977f;    // about -0.2 dBFS, never exceeded

private:
    static float clip (float x) noexcept
    {
        const float a = std::abs (x);
        if (a <= kKnee)
            return x;
        const float range = kCeiling - kKnee;
        return std::copysign (kKnee + range * std::tanh ((a - kKnee) / range), x);
    }

    float env = 0.0f, attack = 0.1f, release = 0.001f, reductionDb = 0.0f, meterDecay = 0.9995f;
};

class OutputStage
{
public:
    struct Params
    {
        float tilt = 0.0f;      // -1..1 (about +-6 dB per side around 700 Hz)
        float width = 1.0f;     // 0 mono .. 2 wide
        float gainDb = 0.0f;
        float warmShelfDb = 0.0f;
    };

    void prepare (double sampleRate)
    {
        fs = static_cast<float> (sampleRate);
        tiltS.setTime (0.05f, fs / kChunk);
        warmS.setTime (0.05f, fs / kChunk);
        widthS.setTime (0.03f, fs);
        gainS.setTime (0.03f, fs);
        for (auto& d : dc)
            d.prepare (fs, 6.0f);
        reset();
    }

    void reset() noexcept
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            lowShelf[ch].reset();
            highShelf[ch].reset();
            warm[ch].reset();
            dc[ch].reset();
        }
        sideHp.reset();
        first = true;
        lastTilt = lastWarm = 1.0e9f;
    }

    void process (float* l, float* r, int n, const Params& p) noexcept
    {
        tiltS.target = std::clamp (p.tilt, -1.5f, 1.5f);
        warmS.target = p.warmShelfDb;
        widthS.target = std::clamp (p.width, 0.0f, 2.0f);
        gainS.target = dbToGain (p.gainDb);
        if (first)
        {
            tiltS.snap();
            warmS.snap();
            widthS.snap();
            gainS.snap();
            first = false;
        }

        const float tilt = tiltS.next();
        const float warmDb = warmS.next();
        if (std::abs (tilt - lastTilt) > 1.0e-4f)
        {
            lastTilt = tilt;
            for (int ch = 0; ch < 2; ++ch)
            {
                lowShelf[ch].setLowShelf (700.0, -6.0 * tilt, fs, 0.5);
                highShelf[ch].setHighShelf (700.0, 6.0 * tilt, fs, 0.5);
            }
        }
        if (std::abs (warmDb - lastWarm) > 1.0e-3f)
        {
            lastWarm = warmDb;
            for (int ch = 0; ch < 2; ++ch)
                warm[ch].setLowShelf (220.0, warmDb, fs, 0.7);
            sideHp.setCutoff (140.0f, fs);
        }

        for (int i = 0; i < n; ++i)
        {
            float a = dc[0].process (l[i]);
            float b = dc[1].process (r[i]);
            a = warm[0].process (highShelf[0].process (lowShelf[0].process (a)));
            b = warm[1].process (highShelf[1].process (lowShelf[1].process (b)));

            const float w = widthS.next();
            const float mid = 0.5f * (a + b);
            float side = 0.5f * (a - b);
            if (w > 1.0f)
            {
                // widen only above the bass so the low end stays centred
                const float sideHigh = sideHp.hp (side);
                side += (w - 1.0f) * sideHigh;
            }
            else
            {
                sideHp.hp (side);
                side *= w;
            }

            const float g = gainS.next();
            l[i] = (mid + side) * g;
            r[i] = (mid - side) * g;
        }
    }

private:
    Biquad lowShelf[2], highShelf[2], warm[2];
    DcBlock dc[2];
    OnePole sideHp;
    Smooth tiltS, warmS, widthS, gainS;
    float fs = 48000.0f, lastTilt = 1.0e9f, lastWarm = 1.0e9f;
    bool first = true;
};

} // namespace swoon
