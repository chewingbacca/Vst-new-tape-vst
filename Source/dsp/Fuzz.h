// Swoon fuzz: an original model in the spirit of a four-transistor
// "sustain" fuzz. Two cascaded clipping stages, each simulated as the ODE of
// a gain stage with a capacitor and an anti-parallel diode pair in its
// feedback loop:
//
//     du/dt = wc * (d - u - beta * sinh (u))
//
// (u = output in thermal-voltage units, d = driven input). Solved per sample
// with BDF2 + Newton at the oversampled rate. Because the capacitor slews the
// output between the diode rails the stage is strongly self-band-limiting,
// which is what gives this family of fuzz its woolly sustain - and it keeps
// aliasing far below what a static waveshaper would produce.
#pragma once

#include "Oversampler.h"
#include "Util.h"

namespace swoon
{

/** One diode-feedback clipping stage. */
struct DiodeStage
{
    float u1 = 0.0f, u2 = 0.0f;     // previous two outputs
    float c = 0.02f;                // 2 * T * wc / 3
    float beta = 0.05f;             // diode saturation current relative to the linear feedback current
    float a = 1.02f, b = 0.001f, invAB = 1.0f, invB = 1.0f;

    void set (float cornerHz, float betaIn, float rate) noexcept
    {
        c = 2.0f * kTwoPi * cornerHz / (3.0f * rate);
        beta = betaIn;
        a = 1.0f + c;
        b = c * beta;
        invAB = 1.0f / (a + b);
        invB = 1.0f / b;
    }
    void reset() noexcept { u1 = u2 = 0.0f; }

    /** d = input scaled into thermal-voltage units (already multiplied by the stage gain). */
    float process (float d) noexcept
    {
        // BDF2: a*u + b*sinh(u) = rhs
        const float rhs = (4.0f / 3.0f) * u1 - (1.0f / 3.0f) * u2 + c * d;
        const float mag = std::abs (rhs);

        // Both estimates over-shoot the root, so Newton converges monotonically from above.
        const float lin = mag * invAB;
        const float q = mag * invB;
        const float sat = std::log (q + std::sqrt (q * q + 1.0f));   // asinh
        float u = std::min (lin, sat);

        for (int it = 0; it < 3; ++it)
        {
            const float e = std::exp (u);
            const float ie = 1.0f / e;
            const float sh = 0.5f * (e - ie), ch = 0.5f * (e + ie);
            const float f = a * u + b * sh - mag;
            u -= f / (a + b * ch);
        }

        u = std::copysign (std::max (u, 0.0f), rhs);
        u2 = u1;
        u1 = u;
        return u;
    }
};

class Fuzz
{
public:
    struct Params
    {
        bool on = true;
        int type = 0;          // 0 sustain, 1 octave, 2 velcro
        int voice = 1;         // 0 flat, 1 full, 2 scoop
        float sustain = 0.6f;  // 0..1 (macro-adjusted, may exceed slightly)
        float tone = 0.5f;
        float gate = 0.15f;
        float stack = 0.3f;
        float levelDb = 0.0f;
        float mix = 1.0f;
    };

    void prepare (double sampleRate, int /*maxBlock*/)
    {
        fs = static_cast<float> (sampleRate);
        const int log2Factor = sampleRate < 70000.0 ? 2 : 1;   // 4x at 44.1/48 kHz, 2x above
        for (auto& ch : chans)
        {
            ch.os.prepare (log2Factor, kChunk);
            ch.padLine.prepare (16);
            ch.dry.prepare (256);
            ch.stackLine.prepare (static_cast<int> (0.06f * fs) + 16);
        }

        osFactor = chans[0].os.factor();
        fsOs = fs * static_cast<float> (osFactor);

        // Pad the oversampler's round trip up to a whole number of base-rate samples.
        const int topLatency = chans[0].os.latencyAtTopRate();
        padSamples = (osFactor - (topLatency % osFactor)) % osFactor;
        latency = (topLatency + padSamples) / osFactor + kStackBase;

        gate.prepare (fs);
        typeSwitch.prepare (fs);
        voiceSwitch.prepare (fs);
        envFast.setTimes (0.0008f, 0.012f, fs);
        envSlow.setTimes (0.004f, 0.25f, fs);
        gateGain.setTime (0.004f, fs);
        sustainS.setTime (0.03f, fs);
        toneS.setTime (0.03f, fs);
        levelS.setTime (0.03f, fs);
        stackS.setTime (0.08f, fs);
        for (auto& ch : chans)
            ch.stackDelay.setTime (0.05f, fs);
        driftL.setCutoff (0.35f, fs / kChunk);
        driftR.setCutoff (0.27f, fs / kChunk);
        for (auto& ch : chans)
            ch.sagEnv.setTimes (0.02f, 0.25f, fsOs);
        reset();
    }

    void reset() noexcept
    {
        for (auto& ch : chans)
        {
            ch.os.reset();
            ch.padLine.reset();
            ch.dry.reset();
            ch.stackLine.reset();
            ch.s1.reset();
            ch.s2.reset();
            ch.inHp.reset();
            ch.midHp.reset();
            ch.outHp.reset();
            ch.rectHp.reset();
            ch.toneLp.reset();
            ch.toneHp.reset();
            ch.body.reset();
            ch.fizz.reset();
            ch.dc.reset();
            ch.stackDelay.snap (0.0f);
        }
        gate.reset();
        typeSwitch.reset();
        voiceSwitch.reset();
        envFast.reset();
        envSlow.reset();
        for (auto& ch : chans)
            ch.sagEnv.reset();
        gateGain.snap (1.0f);
        first = true;
        configuredType = configuredVoice = -1;
    }

    int latencySamples() const noexcept { return latency; }

    /** n <= kChunk. In place. */
    void process (float* l, float* r, int n, const Params& p) noexcept
    {
        float* io[2] = { l, r };

        // The dry path is always delayed by the module latency so the plugin's
        // reported latency stays constant whether or not the fuzz is running.
        float dryBuf[2][kChunk];
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
            {
                chans[ch].dry.push (io[ch][i]);
                dryBuf[ch][i] = chans[ch].dry.read (latency);
            }

        const bool active = gate.begin (p.on, p.mix, n, static_cast<int> (0.25f * fs), first);
        if (! active)
        {
            for (int ch = 0; ch < 2; ++ch)
                std::memcpy (io[ch], dryBuf[ch], static_cast<size_t> (n) * sizeof (float));
            first = false;
            return;
        }

        // ---- control-rate updates ---------------------------------------------------
        sustainS.target = std::clamp (p.sustain, 0.0f, 1.25f);
        toneS.target = clamp01 (p.tone);
        levelS.target = dbToGain (p.levelDb);
        stackS.target = clamp01 (p.stack);
        if (first)
        {
            sustainS.snap();
            toneS.snap();
            levelS.snap();
            stackS.snap();
        }
        typeSwitch.request (std::clamp (p.type, 0, 2));
        voiceSwitch.request (std::clamp (p.voice, 0, 2));
        if (configuredType != typeSwitch.current)
            configureType (typeSwitch.current);
        if (configuredVoice != voiceSwitch.current)
            configureVoice (voiceSwitch.current);

        // gate threshold: 0 = off, 1 = about -28 dBFS
        const float gateAmt = clamp01 (p.gate);
        const float thresh = gateAmt <= 0.0f ? 0.0f : dbToGain (-78.0f + 50.0f * gateAmt);

        // stack: slowly drifting, different short delays per side (in samples)
        const float dL = driftL.tick(), dR = driftR.tick();
        chans[0].stackTarget = stackS.target * fs * (0.0045f + 0.0016f * dL);
        chans[1].stackTarget = stackS.target * fs * (0.0170f + 0.0022f * dR);

        // ---- per-sample base-rate front end ----------------------------------------
        float pre[2][kChunk];
        for (int i = 0; i < n; ++i)
        {
            const float inL = gate.flushing ? 0.0f : io[0][i];
            const float inR = gate.flushing ? 0.0f : io[1][i];

            const float peak = std::max (std::abs (inL), std::abs (inR));
            const float ef = envFast.process (peak);
            envSlow.process (peak);

            // downward expander with hysteresis-free soft knee: gain = clamp ((env / thresh)^2)
            float g = 1.0f;
            if (thresh > 0.0f)
            {
                const float ratio = std::max (ef, envSlow.env * 0.7f) / thresh;
                g = std::min (1.0f, ratio * ratio);
            }
            gateGain.target = g;
            const float gg = gateGain.next();

            const float in[2] = { inL, inR };
            for (int ch = 0; ch < 2; ++ch)
            {
                auto& c = chans[ch];
                c.stackLine.push (in[ch]);
                c.stackDelay.target = c.stackTarget;
                const float d = c.stackDelay.next() + static_cast<float> (kStackBase);
                pre[ch][i] = c.stackLine.readCubic (d) * gg;
            }
        }

        // ---- oversampled nonlinear core --------------------------------------------
        const int nOs = n * osFactor;
        const float susStart = sustainS.y;
        for (int i = 0; i < n; ++i)
            sustainS.next();
        const float susEnd = sustainS.y;

        float wet[2][kChunk];
        for (int ch = 0; ch < 2; ++ch)
        {
            auto& c = chans[ch];
            float* x = c.os.up (pre[ch], n);
            const float susStep = (susEnd - susStart) / static_cast<float> (nOs);
            float sus = susStart;

            for (int i = 0; i < nOs; ++i)
            {
                sus += susStep;
                // sustain pot: -40 dB .. 0 dB (audio taper), then the input booster
                const float drive = preGain * std::exp (4.6052f * (sus - 1.0f));
                float v = c.inHp.hp (x[i]) * drive;

                if (typeCfg.octave > 0.0f)
                {
                    // full-wave rectifier ahead of the clippers: fundamental moves up an octave
                    const float rect = c.rectHp.hp (std::abs (v));
                    v = lerp (v, 2.0f * rect, typeCfg.octave);
                }

                float u = c.s1.process (v * typeCfg.gain1);
                u = c.midHp.hp (u);

                if (typeCfg.starve > 0.0f)
                {
                    // bias-starved second stage: only peaks above a sagging bias get through
                    const float env = c.sagEnv.process (u);
                    const float bias = typeCfg.starve * (3.0f + 0.12f * env);
                    u = std::max (0.0f, u - bias) - 0.35f * std::max (0.0f, -u - bias * 1.6f);
                    u *= 2.2f;
                }

                u = c.s2.process (u * typeCfg.gain2);
                u = c.outHp.hp (u);

                // pad to a whole base-rate sample of latency
                c.padLine.push (u * outScale);
                x[i] = c.padLine.read (padSamples);
            }

            c.os.down (wet[ch], n);
        }

        // ---- base-rate tone stack, level, mix --------------------------------------
        for (int i = 0; i < n; ++i)
        {
            bool typeChanged = false, voiceChanged = false;
            const float swGain = typeSwitch.tick (typeChanged) * voiceSwitch.tick (voiceChanged);
            if (typeChanged)
                configureType (typeSwitch.current);
            if (voiceChanged)
                configureVoice (voiceSwitch.current);

            const float t = 0.06f + 0.88f * toneS.next();
            const float level = levelS.next();
            const float m = gate.mix.next();

            for (int ch = 0; ch < 2; ++ch)
            {
                auto& c = chans[ch];
                float w = wet[ch][i];
                const float lo = c.toneLp.lp (w);
                const float hi = c.toneHp.hp (w);
                w = (1.0f - t) * lo * voiceCfg.loGain + t * hi * voiceCfg.hiGain;
                w = c.body.process (w);
                w = c.fizz.lp (w);
                w = c.dc.process (w) * voiceCfg.makeup * level * swGain;
                if (isBad (w))
                {
                    w = 0.0f;
                    hardResetCore();
                }
                io[ch][i] = dryBuf[ch][i] * (1.0f - m) + w * m;
            }
        }

        first = false;
    }

private:
    static constexpr int kStackBase = 2;   // fixed samples of delay in the stack line (so cubic reads are valid)

    struct TypeCfg { float gain1, gain2, octave, starve, corner1, corner2, inHp, midHp; };
    struct VoiceCfg { float lpHz, hpHz, loGain, hiGain, bodyHz, bodyDb, bodyQ, makeup; };

    void configureType (int type) noexcept
    {
        configuredType = type;
        switch (type)
        {
            default:
            case 0: typeCfg = { 15.0f, 15.0f, 0.0f, 0.0f, 1300.0f, 1500.0f, 75.0f, 95.0f }; break;    // sustain
            case 1: typeCfg = { 12.0f, 10.0f, 0.85f, 0.0f, 2600.0f, 3200.0f, 180.0f, 160.0f }; break; // octave
            case 2: typeCfg = { 5.0f, 14.0f, 0.0f, 1.0f, 2200.0f, 3800.0f, 110.0f, 60.0f }; break;    // velcro
        }
        for (auto& c : chans)
        {
            c.s1.set (typeCfg.corner1, 0.05f, fsOs);
            c.s2.set (typeCfg.corner2, 0.05f, fsOs);
            c.inHp.setCutoff (typeCfg.inHp, fsOs);
            c.midHp.setCutoff (typeCfg.midHp, fsOs);
            c.outHp.setCutoff (45.0f, fsOs);
            c.rectHp.setCutoff (30.0f, fsOs);
        }
    }

    void configureVoice (int voice) noexcept
    {
        configuredVoice = voice;
        switch (voice)
        {
            case 0:  voiceCfg = { 408.0f, 723.0f, 1.0f, 1.0f, 500.0f, 0.0f, 0.7f, 1.55f }; break;    // flat
            default:
            case 1:  voiceCfg = { 900.0f, 480.0f, 1.45f, 0.85f, 520.0f, 2.5f, 0.7f, 1.0f }; break;   // full
            case 2:  voiceCfg = { 408.0f, 1809.0f, 1.0f, 1.0f, 900.0f, -2.0f, 1.0f, 2.1f }; break;   // scoop
        }
        for (auto& c : chans)
        {
            c.toneLp.setCutoff (voiceCfg.lpHz, fs);
            c.toneHp.setCutoff (voiceCfg.hpHz, fs);
            c.body.setPeak (voiceCfg.bodyHz, voiceCfg.bodyQ, voiceCfg.bodyDb, fs);
            c.fizz.setCutoff (9500.0f, fs);
            c.dc.prepare (fs, 12.0f);
        }
    }

    void hardResetCore() noexcept
    {
        for (auto& c : chans)
        {
            c.s1.reset();
            c.s2.reset();
            c.inHp.reset();
            c.midHp.reset();
            c.outHp.reset();
            c.rectHp.reset();
            c.toneLp.reset();
            c.toneHp.reset();
            c.body.reset();
            c.fizz.reset();
            c.dc.reset();
        }
    }

    struct Channel
    {
        Oversampler os;
        DelayLine padLine, dry, stackLine;
        DiodeStage s1, s2;
        OnePole inHp, midHp, outHp, rectHp, toneLp, toneHp, fizz;
        Biquad body;
        DcBlock dc;
        Smooth stackDelay;
        EnvFollower sagEnv;
        float stackTarget = 0.0f;
    };

    Channel chans[2];
    ModuleGate gate;
    ModeSwitch typeSwitch, voiceSwitch;
    EnvFollower envFast, envSlow;
    Smooth gateGain, sustainS, toneS, levelS, stackS;
    DriftNoise driftL { 0x1234567u }, driftR { 0x7654321u };
    TypeCfg typeCfg {};
    VoiceCfg voiceCfg {};
    int configuredType = -1, configuredVoice = -1;

    float fs = 48000.0f, fsOs = 192000.0f;
    int osFactor = 4, padSamples = 0, latency = 0;
    bool first = true;

    // input booster (x9.6) into thermal-voltage units (1.0 full scale ~ 1 V, Vt ~ 45 mV)
    static constexpr float preGain = 9.6f / 0.045f;
    static constexpr float outScale = 0.042f;         // diode rails sit near u = +-10; lands a full wall near -16 dBFS RMS
};

} // namespace swoon
