// Modulated stereo delay ("tape" style: time changes glide, so they swoop in pitch).
//
//   in --(+)--> drive/soft clip --> tone (LP + HP, the HP is also the DC blocker) --> delay line --+--> out
//         ^                                                                                        |
//         +--------------------- feedback * [ (1-w) own channel + w other channel ] <--------------+
//
//   * width w: 0 = two identical independent delays, 1 = ping-pong (the input is summed into the
//     left line, the lines feed each other). In between the right output tap is also offset by a
//     few milliseconds (outside the feedback loop, so the rhythm never drifts).
//   * The saturator has unity small-signal gain, so `feedback` is the true loop gain for quiet
//     signals; above 1 the loop grows into the soft clipper and settles at a bounded level.
//   * The LFO moves the read position (sine, quadrature between the channels once width > 0.5).
//
// Synced time table (time norm 0..1 -> index min (int (norm * 15), 14), LONGEST FIRST; "1/4" is one beat):
//   0 1/1   1 1/2.  2 1/2   3 1/2T  4 1/4.  5 1/4   6 1/4T  7 1/8.
//   8 1/8   9 1/8T  10 1/16.  11 1/16  12 1/16T  13 1/32  14 1/64
// Unsynced: 1 ms .. 2 s exponential (0.001 * 2000^norm). Always clamped to 4 s.
#include "Effect.h"
#include "../dsp/Delay.h"
#include "../model/ParamTables.h"

namespace atm {
namespace delayfx {

constexpr int kNumDivs = 15;
constexpr float kDivBeats[kNumDivs] = {
    4.f, 3.f, 2.f, 4.f / 3.f, 1.5f, 1.f, 2.f / 3.f, 0.75f, 0.5f, 1.f / 3.f, 0.375f, 0.25f, 1.f / 6.f, 0.125f, 0.0625f
};
constexpr float kMaxDelaySec = 4.f;
constexpr float kMaxModSec   = 0.010f;
constexpr float kMaxOffsetSec = 0.012f;

enum { PTime, PSync, PFeedback, PModRate, PModDepth, PTone, PWidth, PDrive };

class DelayFx final : public Effect
{
public:
    void prepare (double sampleRate) override
    {
        sr = (float) sampleRate;
        const int maxSamples = (int) std::ceil ((kMaxDelaySec + kMaxModSec + kMaxOffsetSec) * sr) + 16;
        for (auto& l : line) l.prepare (maxSamples);
        maxDelay = kMaxDelaySec * sr;
        kGlide  = onePoleCoef (0.022f, sr);     // two cascaded poles: ~80 ms to settle
        kSmooth = onePoleCoef (0.010f, sr);
        reset();
    }

    void reset() override
    {
        for (auto& l : line) l.reset();
        for (int c = 0; c < 2; ++c) lpZ[c] = hpZ[c] = 0.f;
        phase = 0.f;
        first = true;
    }

    void setParams (const float* p, const FxContext& c) override
    {
        float sec;
        if (kFxDelay[PSync].toIndex (p[PSync]) != 0)
        {
            const int idx = std::min ((int) (clamp01 (p[PTime]) * (float) kNumDivs), kNumDivs - 1);
            sec = kDivBeats[idx] * 60.f / (float) clamp (c.bpm, 20.0, 999.0);
        }
        else
            sec = 0.001f * std::pow (2000.f, clamp01 (p[PTime]));
        sec = clamp (sec, 0.001f, kMaxDelaySec);
        tDelay = std::max (sec * sr, 2.f);

        tFb = kFxDelay[PFeedback].toPhys (p[PFeedback]);
        const float rate = kFxDelay[PModRate].toPhys (p[PModRate]);
        lfoInc = rate / sr;
        // depth: at most 10 ms, 45 % of the delay time, and +-25 % of tape speed
        const float depthSec = kFxDelay[PModDepth].toPhys (p[PModDepth]) * std::min ({ kMaxModSec, 0.45f * sec, 0.25f / (kTauF * rate) });
        tDepth = depthSec * sr;

        const float tone = kFxDelay[PTone].toPhys (p[PTone]);
        const float lpHz = 20000.f * std::pow (0.015f, std::max (0.f, -tone));      // 20 kHz .. 300 Hz
        const float hpHz = 10.f * std::pow (200.f, std::max (0.f, tone));           // 10 Hz .. 2 kHz
        tLpA = 1.f - std::exp (-kTauF * std::min (lpHz, 0.49f * sr) / sr);
        tHpA = 1.f - std::exp (-kTauF * hpHz / sr);

        tWidth = clamp01 (kFxDelay[PWidth].toPhys (p[PWidth]));
        tOffset = 4.f * tWidth * (1.f - tWidth) * std::min (kMaxOffsetSec, 0.25f * sec) * sr;
        tDrive = dbToGain (18.f * clamp01 (p[PDrive]));

        if (first)
        {
            first = false;
            d1 = d2 = tDelay; fb = tFb; depth = tDepth; lpA = tLpA; hpA = tHpA; width = tWidth; offs = tOffset; drive = tDrive;
        }
    }

    void process (float* l, float* r, int n, const FxContext&) override
    {
        for (int i = 0; i < n; ++i)
        {
            d1 += (double) kGlide * ((double) tDelay - d1);
            d2 += (double) kGlide * (d1 - d2);
            fb += kSmooth * (tFb - fb);
            depth += kSmooth * (tDepth - depth);
            lpA += kSmooth * (tLpA - lpA);
            hpA += kSmooth * (tHpA - hpA);
            width += kSmooth * (tWidth - width);
            offs += kSmooth * (tOffset - offs);
            drive += kSmooth * (tDrive - drive);

            phase += lfoInc; if (phase >= 1.f) phase -= 1.f;
            const float quad = 0.25f * std::min (1.f, 2.f * width);
            const double dl = std::max (d2 + (double) (depth * fastSin01 (phase)), 2.0);
            const double dr = std::max (d2 + (double) (depth * fastSin01 (phase + quad)), 2.0);

            // The line is read before this sample is written, so the newest sample (at wp - 1) is
            // already one sample old. Positions are doubles: a float cannot resolve fractions of a
            // sample at 4 s / 192 kHz.
            const double wp = (double) line[0].writePos();
            const float yl = line[0].atHermite (wp - dl);
            const float yr = line[1].atHermite (wp - dr);

            const float xl = l[i], xr = r[i];
            const float inL = (xl + width * 0.5f * (xr - xl)) * (1.f + 0.4142f * width);
            const float inR = xr * (1.f - width);
            float wl = inL + fb * (yl + width * (yr - yl));
            float wr = inR + fb * (yr + width * (yl - yr));

            const float invDrive = 1.f / drive;
            wl = softClip (wl * drive) * invDrive;
            wr = softClip (wr * drive) * invDrive;
            lpZ[0] = undenorm (lpZ[0] + lpA * (wl - lpZ[0])); wl = lpZ[0];
            lpZ[1] = undenorm (lpZ[1] + lpA * (wr - lpZ[1])); wr = lpZ[1];
            hpZ[0] = undenorm (hpZ[0] + hpA * (wl - hpZ[0])); wl -= hpZ[0];
            hpZ[1] = undenorm (hpZ[1] + hpA * (wr - hpZ[1])); wr -= hpZ[1];
            line[0].write (wl);
            line[1].write (wr);

            // the loop is limited to +-1/drive: give part of that back at the output
            const float makeup = std::sqrt (drive);
            l[i] = yl * makeup;
            r[i] = (offs > 1.0e-3f ? line[1].atHermite (wp - dr - (double) offs) : yr) * makeup;
        }
    }

private:
    DelayLine line[2];
    float sr = 48000.f, maxDelay = 0.f;
    float kGlide = 0.001f, kSmooth = 0.002f;
    float tDelay = 4800.f, tFb = 0.f, tDepth = 0.f, tLpA = 1.f, tHpA = 0.001f, tWidth = 0.f, tOffset = 0.f, tDrive = 1.f;
    double d1 = 4800.0, d2 = 4800.0;
    float fb = 0.f, depth = 0.f, lpA = 1.f, hpA = 0.001f, width = 0.f, offs = 0.f, drive = 1.f;
    float lfoInc = 0.f, phase = 0.f;
    float lpZ[2] = {}, hpZ[2] = {};
    bool first = true;
};

} // namespace delayfx

std::unique_ptr<Effect> createDelayFx() { return std::make_unique<delayfx::DelayFx>(); }

} // namespace atm
