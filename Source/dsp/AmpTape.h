// Clean amp + cabinet voicings, and the tape stage (saturation, wow, flutter).
// The cabinets are small hand-voiced filter sets, not impulse responses.
#pragma once

#include "Util.h"

namespace swoon
{

/** Asymmetric soft saturator with first-order antiderivative anti-aliasing.
    f(x) = softSat (x + bias) - softSat (bias). Adds exactly half a sample of delay. */
struct AdaaSat
{
    double x1 = 0.0, F1 = 0.0;
    double bias = 0.0, fBias = 0.0;

    void setBias (double b) noexcept
    {
        bias = b;
        fBias = b / std::sqrt (1.0 + b * b);
        F1 = antideriv (x1);
    }
    void reset() noexcept { x1 = 0.0; F1 = antideriv (0.0); }

    double shape (double x) const noexcept
    {
        const double v = x + bias;
        return v / std::sqrt (1.0 + v * v) - fBias;
    }
    double antideriv (double x) const noexcept
    {
        const double v = x + bias;
        return std::sqrt (1.0 + v * v) - fBias * x;
    }

    float process (float xin) noexcept
    {
        const double x = xin;
        const double F = antideriv (x);
        const double dx = x - x1;
        const double y = std::abs (dx) > 1.0e-6 ? (F - F1) / dx : shape (0.5 * (x + x1));
        x1 = x;
        F1 = F;
        return static_cast<float> (y);
    }
};

//==============================================================================
class Amp
{
public:
    struct Params
    {
        bool on = true;
        float drive = 0.3f, bass = 0.5f, treble = 0.5f;
        int cab = 1;            // 0 direct, 1 open 2x12, 2 closed 4x12, 3 chime 1x12
        float levelDb = 0.0f, mix = 1.0f;
    };

    static constexpr int kLatency = 1;   // two half-sample ADAA stages

    void prepare (double sampleRate)
    {
        fs = static_cast<float> (sampleRate);
        gate.prepare (fs);
        cabSwitch.prepare (fs);
        driveS.setTime (0.03f, fs);
        levelS.setTime (0.03f, fs);
        bassS.setTime (0.04f, fs / kChunk);
        trebleS.setTime (0.04f, fs / kChunk);
        for (auto& c : chans)
        {
            c.dry.prepare (8);
            c.inHp.setCutoff (55.0f, fs);
            c.pre.setBias (0.22);
            c.power.setBias (-0.08);
            c.mid.setPeak (480.0, 0.7, -3.5, fs);
            c.interHp.setCutoff (30.0f, fs);
        }
        reset();
    }

    void reset() noexcept
    {
        for (auto& c : chans)
        {
            c.dry.reset();
            c.inHp.reset();
            c.interHp.reset();
            c.pre.reset();
            c.power.reset();
            c.bass.reset();
            c.mid.reset();
            c.treble.reset();
            for (auto& b : c.cab)
                b.reset();
        }
        gate.reset();
        cabSwitch.reset();
        first = true;
        lastBass = lastTreble = -1.0f;
        configuredCab = -1;
    }

    void process (float* l, float* r, int n, const Params& p) noexcept
    {
        float* io[2] = { l, r };
        float dryBuf[2][kChunk];
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
            {
                chans[ch].dry.push (io[ch][i]);
                dryBuf[ch][i] = chans[ch].dry.read (kLatency);
            }

        if (! gate.begin (p.on, p.mix, n, static_cast<int> (0.1f * fs), first))
        {
            for (int ch = 0; ch < 2; ++ch)
                std::memcpy (io[ch], dryBuf[ch], static_cast<size_t> (n) * sizeof (float));
            first = false;
            return;
        }

        driveS.target = clamp01 (p.drive);
        levelS.target = dbToGain (p.levelDb);
        bassS.target = clamp01 (p.bass);
        trebleS.target = clamp01 (p.treble);
        if (first)
        {
            driveS.snap();
            levelS.snap();
            bassS.snap();
            trebleS.snap();
        }
        cabSwitch.request (std::clamp (p.cab, 0, 3));
        if (configuredCab != cabSwitch.current)
            configureCab (cabSwitch.current);

        const float bass = bassS.next(), treble = trebleS.next();
        if (std::abs (bass - lastBass) > 1.0e-4f || std::abs (treble - lastTreble) > 1.0e-4f)
        {
            lastBass = bass;
            lastTreble = treble;
            for (auto& c : chans)
            {
                c.bass.setLowShelf (140.0, (bass - 0.5) * 18.0, fs, 0.7);
                c.treble.setHighShelf (2800.0, (treble - 0.5) * 18.0, fs, 0.7);
            }
        }

        for (int i = 0; i < n; ++i)
        {
            const float d = driveS.next();
            // preamp gain 0 .. +26 dB; output trimmed so loudness stays roughly level as drive rises
            const float preGain = dbToGain (26.0f * d) * 1.4f;
            const float makeup = 1.15f / (0.55f + 0.75f * std::pow (preGain, 0.55f));
            const float level = levelS.next();
            const float m = gate.mix.next();
            bool cabChanged = false;
            const float cabGain = cabSwitch.tick (cabChanged);
            if (cabChanged)
                configureCab (cabSwitch.current);

            for (int ch = 0; ch < 2; ++ch)
            {
                auto& c = chans[ch];
                float x = gate.flushing ? 0.0f : io[ch][i];
                x = c.inHp.hp (x);
                x = c.pre.process (x * preGain);                    // preamp triode-ish stage
                x = c.treble.process (c.mid.process (c.bass.process (x)));
                x = c.interHp.hp (x);
                x = c.power.process (x * 1.6f) * (1.0f / 1.6f);     // power stage: gentle, mostly linear
                x *= makeup;
                if (numCabStages > 0)
                {
                    for (int k = 0; k < numCabStages; ++k)
                        x = c.cab[k].process (x);
                    x *= cabMakeup;
                }
                x *= level * cabGain;
                if (isBad (x))
                {
                    x = 0.0f;
                    resetFilters();
                }
                io[ch][i] = dryBuf[ch][i] * (1.0f - m) + x * m;
            }
        }
        first = false;
    }

private:
    static constexpr int kMaxCabStages = 7;

    void configureCab (int cab) noexcept
    {
        configuredCab = cab;
        for (auto& c : chans)
        {
            auto* b = c.cab;
            switch (cab)
            {
                case 0:   // direct
                    break;
                default:
                case 1:   // open-back 2x12: wide, glassy, soft low end
                    b[0].setHighPass (72.0, 0.8, fs);
                    b[1].setPeak (115.0, 1.4, 3.0, fs);
                    b[2].setPeak (450.0, 0.9, -3.0, fs);
                    b[3].setPeak (2600.0, 1.2, 4.0, fs);
                    b[4].setPeak (4200.0, 2.0, 2.0, fs);
                    b[5].setLowPass (5400.0, 0.9, fs);
                    b[6].setLowPass (7800.0, 0.7, fs);
                    break;
                case 2:   // closed 4x12: tight thump, forward upper mids, steep top
                    b[0].setHighPass (92.0, 1.1, fs);
                    b[1].setPeak (128.0, 1.6, 4.0, fs);
                    b[2].setPeak (380.0, 1.2, -4.0, fs);
                    b[3].setPeak (1800.0, 1.0, 3.0, fs);
                    b[4].setPeak (3300.0, 1.8, 5.0, fs);
                    b[5].setLowPass (4700.0, 1.1, fs);
                    b[6].setLowPass (6200.0, 0.7, fs);
                    break;
                case 3:   // chime 1x12: bright, airy, lighter lows
                    b[0].setHighPass (85.0, 0.7, fs);
                    b[1].setPeak (165.0, 1.0, 2.0, fs);
                    b[2].setPeak (620.0, 0.8, -2.0, fs);
                    b[3].setPeak (2900.0, 1.4, 5.0, fs);
                    b[4].setPeak (5000.0, 2.0, 3.0, fs);
                    b[5].setLowPass (6400.0, 0.9, fs);
                    b[6].setLowPass (9200.0, 0.7, fs);
                    break;
            }
            for (int k = 0; k < kMaxCabStages; ++k)
                b[k].reset();
        }
        numCabStages = cab == 0 ? 0 : kMaxCabStages;
        cabMakeup = cab == 2 ? 0.85f : (cab == 3 ? 0.9f : 0.95f);
    }

    void resetFilters() noexcept
    {
        for (auto& c : chans)
        {
            c.inHp.reset();
            c.interHp.reset();
            c.pre.reset();
            c.power.reset();
            c.bass.reset();
            c.mid.reset();
            c.treble.reset();
            for (auto& b : c.cab)
                b.reset();
        }
    }

    struct Channel
    {
        DelayLine dry;
        OnePole inHp, interHp;
        AdaaSat pre, power;
        Biquad bass, mid, treble;
        Biquad cab[kMaxCabStages];
    };

    Channel chans[2];
    ModuleGate gate;
    ModeSwitch cabSwitch;
    Smooth driveS, levelS, bassS, trebleS;
    float fs = 48000.0f, lastBass = -1.0f, lastTreble = -1.0f, cabMakeup = 1.0f;
    int configuredCab = -1, numCabStages = 0;
    bool first = true;
};

//==============================================================================
class Tape
{
public:
    struct Params
    {
        bool on = true;
        float drive = 0.3f, wow = 0.2f, flutter = 0.15f, age = 0.3f, mix = 1.0f;
    };

    void prepare (double sampleRate)
    {
        fs = static_cast<float> (sampleRate);
        baseDelay = static_cast<int> (std::ceil (0.0035f * fs));
        for (auto& c : chans)
        {
            c.line.prepare (2 * baseDelay + 16);
            c.dry.prepare (baseDelay + 8);
            c.sat.setBias (0.06);
            c.outSat.setBias (0.0);
        }
        gate.prepare (fs);
        driveS.setTime (0.03f, fs);
        wowS.setTime (0.1f, fs / kChunk);
        flutterS.setTime (0.1f, fs / kChunk);
        ageS.setTime (0.05f, fs / kChunk);
        delayS.setTime (0.004f, fs);
        wowLfo.setRate (0.47f, fs / kChunk);
        flutLfoA.setRate (7.3f, fs / kChunk);
        flutLfoB.setRate (12.9f, fs / kChunk);
        wowDrift.setCutoff (0.6f, fs / kChunk);
        flutDrift.setCutoff (18.0f, fs / kChunk);
        ampDrift.setCutoff (4.0f, fs / kChunk);
        ampS.setTime (0.01f, fs);
        reset();
    }

    void reset() noexcept
    {
        for (auto& c : chans)
        {
            c.line.reset();
            c.dry.reset();
            c.sat.reset();
            c.outSat.reset();
            c.preEmph.reset();
            c.deEmph.reset();
            c.bump.reset();
            c.lp.reset();
        }
        gate.reset();
        wowDrift.reset();
        flutDrift.reset();
        ampDrift.reset();
        delayS.snap (static_cast<float> (baseDelay));
        ampS.snap (1.0f);
        first = true;
        lastAge = -1.0f;
    }

    /** Base transport delay + the two half-sample ADAA stages. */
    int latencySamples() const noexcept { return baseDelay + 1; }

    void process (float* l, float* r, int n, const Params& p) noexcept
    {
        float* io[2] = { l, r };
        const int latency = latencySamples();
        float dryBuf[2][kChunk];
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
            {
                chans[ch].dry.push (io[ch][i]);
                dryBuf[ch][i] = chans[ch].dry.read (latency);
            }

        if (! gate.begin (p.on, p.mix, n, static_cast<int> (0.05f * fs), first))
        {
            for (int ch = 0; ch < 2; ++ch)
                std::memcpy (io[ch], dryBuf[ch], static_cast<size_t> (n) * sizeof (float));
            first = false;
            return;
        }

        driveS.target = clamp01 (p.drive);
        wowS.target = clamp01 (p.wow);
        flutterS.target = clamp01 (p.flutter);
        ageS.target = clamp01 (p.age);
        if (first)
        {
            driveS.snap();
            wowS.snap();
            flutterS.snap();
            ageS.snap();
        }
        const float wow = wowS.next(), flutter = flutterS.next(), age = ageS.next();

        if (std::abs (age - lastAge) > 1.0e-3f)
        {
            lastAge = age;
            const double lpHz = 19000.0 * std::pow (5200.0 / 19000.0, static_cast<double> (age));
            for (auto& c : chans)
            {
                c.preEmph.setHighShelf (3800.0, 5.0, fs, 0.6);
                c.deEmph.setHighShelf (3800.0, -5.0, fs, 0.6);
                c.bump.setPeak (78.0, 0.9, 1.2 + 2.2 * age, fs);
                c.lp.setLowPass (lpHz, 0.6, fs);
            }
        }

        // Transport speed error -> delay offset (in samples), common to both channels.
        wowLfo.advance();
        flutLfoA.advance();
        flutLfoB.advance();
        const float wowSig = 0.7f * wowLfo.sine() + 0.45f * wowDrift.tick();
        const float flutSig = 0.5f * flutLfoA.sine() + 0.3f * flutLfoB.sine() + 0.35f * flutDrift.tick();
        // wow: up to ~2.4 ms swing (about +-0.6 % speed); flutter: up to ~0.05 ms (about +-0.25 %)
        const float wowDepth = wow * wow * (0.0024f * fs) * (1.0f + 0.5f * age);
        const float flutDepth = flutter * (0.00005f * fs) * (1.0f + age);
        delayS.target = static_cast<float> (baseDelay)
                      + std::clamp (wowSig * wowDepth + flutSig * flutDepth,
                                    -static_cast<float> (baseDelay) + 3.0f, static_cast<float> (baseDelay) - 3.0f);
        // worn tape: slight level instability
        ampS.target = 1.0f + 0.06f * age * age * ampDrift.tick();

        for (int i = 0; i < n; ++i)
        {
            const float d = driveS.next();
            const float gain = dbToGain (-3.0f + 21.0f * d);
            const float makeup = std::pow (gain, -0.8f);   // unity for small signals at low drive, level-held when pushed
            const float delay = delayS.next();
            const float amp = ampS.next();
            const float m = gate.mix.next();

            for (int ch = 0; ch < 2; ++ch)
            {
                auto& c = chans[ch];
                c.line.push (gate.flushing ? 0.0f : io[ch][i]);
                float x = c.line.readCubic (delay);
                x = c.preEmph.process (x);
                x = c.sat.process (x * gain) * makeup;
                x = c.deEmph.process (x);
                x = c.bump.process (x);
                x = c.lp.process (x);
                x = c.outSat.process (x * 0.5f) * 2.0f * amp;       // playback amp: nearly linear
                if (isBad (x))
                {
                    x = 0.0f;
                    resetFilters();
                }
                io[ch][i] = dryBuf[ch][i] * (1.0f - m) + x * m;
            }
        }
        first = false;
    }

private:
    void resetFilters() noexcept
    {
        for (auto& c : chans)
        {
            c.sat.reset();
            c.outSat.reset();
            c.preEmph.reset();
            c.deEmph.reset();
            c.bump.reset();
            c.lp.reset();
        }
    }

    struct Channel
    {
        DelayLine line, dry;
        AdaaSat sat, outSat;
        Biquad preEmph, deEmph, bump, lp;
    };

    Channel chans[2];
    ModuleGate gate;
    Smooth driveS, wowS, flutterS, ageS, delayS, ampS;
    Lfo wowLfo, flutLfoA, flutLfoB;
    DriftNoise wowDrift { 0xA11CEu }, flutDrift { 0xB0B0u }, ampDrift { 0xC0FFEEu };
    float fs = 48000.0f, lastAge = -1.0f;
    int baseDelay = 168;
    bool first = true;
};

} // namespace swoon
