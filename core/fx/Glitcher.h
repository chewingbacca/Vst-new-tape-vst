// Per-track glitch processor driven by the Glitch macro. The sequencer decides
// deterministically (seeded, per step) when a glitch starts and what it is;
// this class performs it: buffer repeat with pitch steps, gate chop, reverse,
// crush and tape stop. Always recording, so a glitch can start on any sample.
#pragma once
#include "../dsp/Delay.h"
#include "../seq/SeqEngine.h"

namespace atm {

class Glitcher
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        const int len = (int) (sr * 2.5);
        buf[0].prepare (len); buf[1].prepare (len);
        fadeLen = std::max (8, (int) (sr * 0.0015));
        reset();
    }

    void reset()
    {
        buf[0].reset(); buf[1].reset();
        type = GlitchType::None; wet = 0.f; left = 0; holdL = holdR = 0.f; holdCount = 0;
        prevL = prevR = 0.f; dcL = dcR = 0.f; declickLeft = 0; declickArm = false;
    }

    bool active() const { return type != GlitchType::None || wet > 0.f; }

    void trigger (const GlitchCmd& c, double bpm)
    {
        if (c.type == GlitchType::None) return;
        declickArm = true;      // a new glitch may start on a different value than the one sounding now
        const double spb = sr * 60.0 / std::max (20.0, bpm);
        const int maxLen = buf[0].capacity() / 2 - 64;
        total = clamp ((int) (c.beats * spb), fadeLen * 4, maxLen);
        slice = clamp ((int) (c.sliceBeats * spb), fadeLen * 2 + 2, total);
        type = c.type; left = total; t = 0;
        start = buf[0].writePos();
        pitchStep = c.pitchStep; amount = c.amount;
        holdCount = 0;
        crushHold = 2 + (int) (amount * 22.f * (float) (sr / 48000.0));
        crushStep = 1.f / std::pow (2.f, 7.f - 4.f * amount);
        readPos = 0.0;
    }

    void process (float* l, float* r, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const float inL = l[i], inR = r[i];
            buf[0].write (inL); buf[1].write (inR);
            if (type == GlitchType::None && wet <= 0.f && declickLeft == 0) { prevL = inL; prevR = inR; continue; }

            float wl = inL, wr = inR;
            float target = 1.f;
            if (type != GlitchType::None)
            {
                switch (type)
                {
                    case GlitchType::Repeat:
                    {
                        const int k = t / slice, o = t - k * slice;
                        if (k == 0) { target = 0.f; break; }             // first pass plays through while it is captured
                        const double rate = std::exp2 ((double) (pitchStep * (float) k) / 12.0);
                        const double p = (double) o * rate;
                        if (p >= (double) (slice - 1)) { wl = wr = 0.f; break; }
                        wl = buf[0].atHermite ((double) start + p);
                        wr = buf[1].atHermite ((double) start + p);
                        // window each repeat so its edges never click
                        const double remain = ((double) slice - 1.0 - p) / rate;
                        float w = 1.f;
                        if (o < fadeLen) w = (float) o / (float) fadeLen;
                        if (remain < (double) fadeLen) w = std::min (w, (float) (remain / (double) fadeLen));
                        // slowed-down repeats run out of slot before they run out of material: fade at the slot end too
                        if (slice - 1 - o < fadeLen) w = std::min (w, (float) (slice - 1 - o) / (float) fadeLen);
                        w *= std::pow (0.92f, (float) (k - 1) * (1.f - amount * 0.5f));
                        wl *= w; wr *= w;
                        break;
                    }
                    case GlitchType::Gate:
                    {
                        const int o = t % slice, half = slice / 2;
                        float w = 1.f;
                        if (o >= half) w = 0.f;
                        else if (o > half - fadeLen) w = (float) (half - o) / (float) fadeLen;
                        else if (o < fadeLen) w = (float) o / (float) fadeLen;
                        wl = inL * w; wr = inR * w;
                        break;
                    }
                    case GlitchType::Reverse:
                    {
                        const int p = start - 1 - t;
                        wl = buf[0].at (p); wr = buf[1].at (p);
                        break;
                    }
                    case GlitchType::Crush:
                    {
                        if (holdCount <= 0)
                        {
                            holdL = std::round (inL / crushStep) * crushStep;
                            holdR = std::round (inR / crushStep) * crushStep;
                            holdCount = crushHold;
                        }
                        --holdCount;
                        wl = holdL; wr = holdR;
                        break;
                    }
                    case GlitchType::Stop:
                    {
                        // playback speed falls from 1 to 0 across the glitch
                        const float u = (float) t / (float) total;
                        const float rate = (1.f - u) * (1.f - u);
                        readPos += (double) rate;
                        wl = buf[0].atHermite ((double) start + readPos - (double) total * 0.02);
                        wr = buf[1].atHermite ((double) start + readPos - (double) total * 0.02);
                        // the buffer ahead of `start` fills as we go; reading never passes the write head because rate <= 1
                        const float g = 1.f - u * u * u;
                        wl *= g; wr *= g;
                        break;
                    }
                    default: break;
                }
                if (left <= fadeLen) target = 0.f;                       // fade back to live before the glitch ends
                ++t;
                if (--left <= 0) type = GlitchType::None;
            }
            else target = 0.f;

            // short linear crossfade between live and glitched signal
            const float step = 1.f / (float) fadeLen;
            if (wet < target) wet = std::min (target, wet + step);
            else if (wet > target) wet = std::max (target, wet - step);
            float ol = inL + (wl - inL) * wet;
            float orr = inR + (wr - inR) * wet;
            // declicker: on a retrigger, measure the jump on the first sample and fade it out
            if (declickArm) { dcL = prevL - ol; dcR = prevR - orr; declickLeft = fadeLen; declickArm = false; }
            if (declickLeft > 0)
            {
                const float gd = (float) declickLeft / (float) fadeLen;
                ol += dcL * gd; orr += dcR * gd;
                --declickLeft;
            }
            l[i] = prevL = ol;
            r[i] = prevR = orr;
        }
    }

private:
    double sr = 48000.0;
    DelayLine buf[2];
    int   type = GlitchType::None;
    int   total = 0, slice = 1, left = 0, t = 0, start = 0, fadeLen = 64;
    float pitchStep = 0.f, amount = 1.f, wet = 0.f;
    float holdL = 0.f, holdR = 0.f, crushStep = 0.01f; int holdCount = 0, crushHold = 4;
    double readPos = 0.0;
    float prevL = 0.f, prevR = 0.f, dcL = 0.f, dcR = 0.f;
    int declickLeft = 0;
    bool declickArm = false;
};

} // namespace atm
