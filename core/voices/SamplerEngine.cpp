// Sampler: one-shot / gated / sliced sample playback.
//
//   region   : latched at note-on. Whole sample, a grid slice or a transient slice, then
//              start/length inside it. Played forwards or backwards, once, looped
//              (with a crossfade at the seam) or ping-pong.
//   reading  : 4-point Hermite from the mip level matching the playback rate, blending
//              two adjacent levels so pitch sweeps never switch audibly and upward
//              pitching does not alias.
//   pitch    : note (key-tracked) + exponential pitch envelope, evaluated on a fixed
//              16-sample grid counted from note-on and ramped linearly in between, so
//              the result does not depend on how the host slices its buffers.
//   edges    : 0.5 ms fade-in (skipped at the very first frame of the file, which is
//              the sample's own attack), fade-out of max (xfade * region, 1 ms).
//
// All original work. Hermite (Catmull-Rom) interpolation is textbook material.
#include "EngineBase.h"
#include "../model/ParamTables.h"

namespace atm {

namespace {
enum P { Start, Length, Reverse, Loop, XFade, Mode, Slices, Slice, PEnv, PEnvT, StartRnd, KeyTrack, Gain, SliceBy, Width };
enum { LoopOff, LoopForward, LoopPingPong };
enum { ModeOneShot, ModeGate, ModeSlice };

constexpr int   kGrid        = 16;        // pitch / mip update interval in samples
constexpr float kBaseGain    = 0.7071f;   // a full-scale sample peaks at -3 dBFS
constexpr float kFadeInSec   = 0.0005f;
constexpr float kFadeOutSec  = 0.001f;
constexpr float kMinSemis    = -96.f, kMaxSemis = 60.f;

// p must be >= -4 (true for every caller: positions are clamped to [-1, length]).
inline float readHermite (const float* d, double p)
{
    const int i = (int) (p + 4.0) - 4;
    const float t = (float) (p - (double) i);
    return hermite4 (d[i - 1], d[i], d[i + 1], d[i + 2], t);
}

// Linear up to 0 dBFS, then a smooth knee that never exceeds about +5 dBFS.
inline float softLimit (float x)
{
    const float a = std::fabs (x);
    if (a <= 1.f) return x;
    const float y = 1.f + 0.8f * fastTanh ((a - 1.f) * 1.25f);
    return x < 0.f ? -y : y;
}
}

class SamplerEngine final : public EngineBase
{
public:
    void prepare (double sampleRate) override
    {
        sr = (float) sampleRate;
        smoothCoef = 1.f - std::exp (-1.f / (0.002f * sr));
        reset();
    }

    void reset() override
    {
        active = false;
        gainCur = gainTarget; widthCur = widthTarget;
    }

    void setSample (const SampleData* s) override
    {
        smp = s;
        active = false;
        stereo = smp != nullptr && smp->numChannels > 1;
    }

    void noteOn (const EngineNote& n, const float* e) override
    {
        if (n.legato && active) { control (e, n.note); return; }     // keep playing, new pitch only

        active = false;
        control (e, n.note);
        gainCur = gainTarget; widthCur = widthTarget;
        if (smp == nullptr || smp->length < 2 || smp->numMips < 1) return;

        const ParamDesc* d = kSamplerParams;
        const double total = (double) smp->length;

        // --- region
        double b0 = 0.0, b1 = total;
        if (d[Mode].toIndex (e[Mode]) == ModeSlice)
        {
            const int k = d[Slice].toIndex (e[Slice]);
            const int nt = (int) smp->transients.size();
            if (d[SliceBy].toIndex (e[SliceBy]) == 1 && nt > 0)
            {
                const int j = clamp (k, 0, nt - 1);
                b0 = (double) clamp (smp->transients[(size_t) j], 0, smp->length);
                b1 = j + 1 < nt ? (double) clamp (smp->transients[(size_t) j + 1], 0, smp->length) : total;
                if (b1 < b0) b1 = b0;
            }
            else
            {
                const int ns = std::max (1, (int) std::lround (d[Slices].toPhys (e[Slices])));
                const int j = clamp (k, 0, ns - 1);
                b0 = total * (double) j / (double) ns;
                b1 = total * (double) (j + 1) / (double) ns;
            }
        }
        const double rs = b0 + (double) clamp01 (e[Start]) * (b1 - b0);
        const double remain = b1 - rs;
        const double minLen = std::min (remain, 0.002 * smp->sampleRate);      // length 0 still gives a 2 ms blip
        len = std::max ((double) clamp01 (e[Length]) * remain, minLen);
        if (len < 2.0) return;                                                // nothing to play
        regStart = rs; regEnd = rs + len;

        reverse  = d[Reverse].toIndex (e[Reverse]) != 0;
        loopMode = d[Loop].toIndex (e[Loop]);
        const float xf = d[XFade].toPhys (e[XFade]);
        const double xfFrames = std::min ((double) xf * len, 0.5 * len);

        // --- start position
        const float rnd = hashToFloat (hashN (n.seed, 0x5a3u));
        u = (double) (d[StartRnd].toPhys (e[StartRnd]) * rnd) * len * 0.999;
        dir = 1.0;
        age = 0;

        // --- fades
        fadeInLen = std::max (1, (int) (kFadeInSec * sr));
        if (! reverse && regStart <= 0.0 && u <= 0.0) fadeInLen = 1;           // the file's own first frame: leave its attack alone
        fadeInInv = 1.f / (float) fadeInLen;
        fadeOutFrames = xfFrames;
        fadeOutInv = xfFrames > 1.0 ? 1.0 / xfFrames : 1.0e9;
        minOutSamples = std::max (2.f, kFadeOutSec * sr);

        // --- loop crossfade
        xfLen = 0.0; period = len; xfStart = len; xfInv = 0.0; xfCorr = 0.f;
        if (loopMode == LoopForward && xfFrames >= 2.0)
        {
            xfLen = xfFrames; period = len - xfLen; xfStart = period; xfInv = 1.0 / xfLen;
            xfCorr = seamCorrelation();
        }

        // --- pitch
        srRatio = (float) (smp->sampleRate / (double) sr);
        rate = rateAt (0);
        gridLeft = 0;
        active = true;
    }

    void control (const float* e, float n) override
    {
        const ParamDesc* d = kSamplerParams;
        note = n;
        keytrack = d[KeyTrack].toIndex (e[KeyTrack]) != 0;
        penv = d[PEnv].toPhys (e[PEnv]);
        penvK = -6.9078f / (std::max (0.001f, d[PEnvT].toPhys (e[PEnvT])) * sr);
        gainTarget = kBaseGain * dbToGain (d[Gain].toPhys (e[Gain]));
        widthTarget = d[Width].toPhys (e[Width]);
    }

    void render (float* outL, float* outR, int n) override
    {
        int i = 0;
        if (active)
        {
            for (; i < n; ++i)
            {
                if (gridLeft == 0) updateGrid();
                --gridLeft;

                float l, r;
                readFrame (positionOf (u), l, r);
                float g = 1.f;

                if (u > xfStart)                                   // forward loop: blend in the start of the region
                {
                    const float t = (float) ((u - xfStart) * xfInv);
                    float l2, r2;
                    readFrame (positionOf (u - period), l2, r2);
                    // gains for constant power given how correlated the two sides of the seam are
                    const float norm = 1.f / std::sqrt (std::max (0.25f, 1.f - 2.f * t * (1.f - t) * (1.f - xfCorr)));
                    l = (l + t * (l2 - l)) * norm;
                    r = (r + t * (r2 - r)) * norm;
                }

                if (age < fadeInLen)
                {
                    const float f = (float) (age + 1) * fadeInInv;
                    g *= f * f * (3.f - 2.f * f);
                }
                if (loopMode == LoopOff)
                {
                    const double rem = len - u;
                    const double zoneT = (double) (rate * minOutSamples);
                    if (rem < fadeOutFrames || rem < zoneT)
                    {
                        const float f = (float) std::min (1.0, std::min (rem * fadeOutInv, rem / zoneT));
                        g *= f * f * (3.f - 2.f * f);
                    }
                }

                gainCur += smoothCoef * (gainTarget - gainCur);
                g *= gainCur;
                if (stereo)
                {
                    widthCur += smoothCoef * (widthTarget - widthCur);
                    const float mid = 0.5f * (l + r), side = 0.5f * (l - r) * widthCur;
                    outL[i] = softLimit ((mid + side) * g);
                    outR[i] = softLimit ((mid - side) * g);
                }
                else outL[i] = softLimit (l * g);

                // --- advance
                ++age;
                const double step = (double) rate;
                rate += rateStep;
                if (loopMode == LoopOff)
                {
                    u += step;
                    if (u >= len) { active = false; ++i; break; }
                }
                else if (loopMode == LoopForward)
                {
                    u += step;
                    if (u >= len)
                    {
                        u -= period;
                        if (u >= len) u = xfLen + std::fmod (u - xfLen, period);
                    }
                }
                else
                {
                    u += dir * step;
                    if (u >= len)     { u = 2.0 * len - u; dir = -1.0; if (u < 0.0) u = 0.0; }
                    else if (u < 0.0) { u = -u; dir = 1.0; if (u > len) u = len; }
                }
            }
        }
        for (; i < n; ++i) { outL[i] = 0.f; if (stereo) outR[i] = 0.f; }
    }

    bool isStereo() const override { return stereo; }
    bool finished() const override { return ! active; }

private:
    // Phase u (0..len, always measured in the direction of play) -> frame position at mip level 0.
    inline double positionOf (double uu) const
    {
        const double p = reverse ? regEnd - 1.0 - uu : regStart + uu;
        return clamp (p, -1.0, (double) smp->length);
    }

    inline void readFrame (double p0, float& l, float& r) const
    {
        const double pa = p0 * scaleA;
        l = readHermite (dA[0], pa);
        if (stereo) r = readHermite (dA[1], pa); else r = l;
        if (mixB > 0.f)
        {
            const double pb = p0 * scaleB;
            const float lb = readHermite (dB[0], pb);
            l += mixB * (lb - l);
            if (stereo) { const float rb = readHermite (dB[1], pb); r += mixB * (rb - r); } else r = l;
        }
    }

    // Playback rate in level-0 frames per output sample at `samples` after note-on.
    float rateAt (int samples) const
    {
        float st = keytrack ? note - smp->rootNote : 0.f;
        if (penv != 0.f) st += penv * std::exp (penvK * (float) samples);
        return srRatio * std::exp2 (clamp (st, kMinSemis, kMaxSemis) * (1.f / 12.f));
    }

    void updateGrid()
    {
        const float target = rateAt (age + kGrid);
        rateStep = (target - rate) * (1.f / (float) kGrid);
        gridLeft = kGrid;

        // Mip levels for this segment. Level k is band-limited to 1/2^k of the original, so it can
        // be read at up to 2^k frames per sample. Stay on the lower level for the first part of
        // each octave (its aliases fold back only into the very top of the spectrum there) and
        // move to the next level before they could reach further down.
        const float lam = std::log2 (std::max (std::max (rate, target), 1.0e-6f));
        const int top = smp->numMips - 1;
        int a = 0; float w = 0.f;
        if (lam > 0.f)
        {
            a = std::min ((int) lam, top);
            const float f = clamp01 ((lam - (float) a - 0.15f) * (1.f / 0.55f));
            w = f * f * (3.f - 2.f * f);
            if (a >= top) w = 0.f;
            else if (w >= 0.999f) { ++a; w = 0.f; }
        }
        setLevels (a, w);
    }

    void setLevels (int a, float w)
    {
        const int b = std::min (a + 1, smp->numMips - 1);
        dA[0] = smp->data (a, 0); dA[1] = smp->data (a, 1);
        dB[0] = smp->data (b, 0); dB[1] = smp->data (b, 1);
        scaleA = 1.0 / (double) (1 << a); scaleB = 1.0 / (double) (1 << b);
        mixB = w;
    }

    // Normalised correlation between the two stretches that overlap in the loop crossfade,
    // estimated from at most 128 evenly spaced frames. 1 = identical (a steady tone looped on
    // its period), 0 = unrelated (noise, drums).
    float seamCorrelation() const
    {
        const int k = (int) std::min (128.0, xfLen);
        const float* d0 = smp->data (0, 0);
        const float* d1 = smp->data (0, 1);
        double ab = 0, aa = 0, bb = 0;
        for (int j = 0; j < k; ++j)
        {
            const double o = ((double) j + 0.5) / (double) k * xfLen;
            const int ia = clamp ((int) positionOf (xfStart + o), 0, smp->length - 1);
            const int ib = clamp ((int) positionOf (o), 0, smp->length - 1);
            const double a = (double) d0[ia] + d1[ia], b = (double) d0[ib] + d1[ib];
            ab += a * b; aa += a * a; bb += b * b;
        }
        const double den = std::sqrt (aa * bb);
        return den > 1.0e-12 ? (float) clamp (ab / den, 0.0, 1.0) : 0.f;
    }

    float sr = 48000.f;
    const SampleData* smp = nullptr;
    bool  active = false, stereo = false;

    // latched at note-on
    double regStart = 0, regEnd = 0, len = 0;
    bool   reverse = false;
    int    loopMode = LoopOff;
    double xfLen = 0, period = 0, xfStart = 0, xfInv = 0;
    float  xfCorr = 0;
    int    fadeInLen = 1; float fadeInInv = 1.f;
    double fadeOutFrames = 0, fadeOutInv = 0; float minOutSamples = 48.f;
    float  srRatio = 1.f;

    // running
    double u = 0, dir = 1;
    int    age = 0, gridLeft = 0;
    float  rate = 1.f, rateStep = 0.f;
    const float* dA[2] = { nullptr, nullptr };
    const float* dB[2] = { nullptr, nullptr };
    double scaleA = 1, scaleB = 1; float mixB = 0;

    // control
    float note = 60.f, penv = 0.f, penvK = -1.f;
    bool  keytrack = true;
    float gainTarget = kBaseGain, gainCur = kBaseGain, widthTarget = 1.f, widthCur = 1.f, smoothCoef = 0.01f;
};

std::unique_ptr<EngineBase> createSamplerEngine() { return std::make_unique<SamplerEngine>(); }

} // namespace atm
