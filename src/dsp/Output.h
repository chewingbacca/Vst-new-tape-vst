// Verdant — output stage: tone (tilt + roots shelf), stereo width, look-ahead limiter.
#pragma once

#include "Util.h"

namespace verdant
{

//==============================================================================
// Wet-signal tone and width. Coefficients are set at control rate.
class OutputTone
{
public:
    void prepare (float sampleRate)
    {
        sr = sampleRate;
        reset();
        setTone (0.0f, 0.0f);
    }

    void reset()
    {
        for (auto& s : tiltLo) s.reset();
        for (auto& s : tiltHi) s.reset();
        for (auto& s : roots)  s.reset();
        sideHp.reset();
    }

    // tiltDb: + = brighter, rootsDb: low shelf gain.
    void setTone (float tiltDb, float rootsDb)
    {
        for (int c = 0; c < 2; ++c)
        {
            tiltLo[c].setLow  (650.0f, -tiltDb, sr);
            tiltHi[c].setHigh (650.0f * 2.2f, tiltDb, sr);
            roots[c].setLow   (240.0f, rootsDb, sr);
        }
        sideHp.setCutoff (180.0f, sr);
    }

    // width: 0 = mono, 1 = unchanged, 2 = extra wide (only above ~180 Hz).
    inline void process (float& l, float& r, float width)
    {
        l = roots[0].process (tiltHi[0].process (tiltLo[0].process (l)));
        r = roots[1].process (tiltHi[1].process (tiltLo[1].process (r)));

        const float m = 0.5f * (l + r);
        float s = 0.5f * (l - r);
        const float sHigh = sideHp.process (s);
        if (width <= 1.0f) s *= width;
        else               s += (width - 1.0f) * sHigh;
        l = m + s;
        r = m - s;
    }

private:
    float sr = 48000.0f;
    Shelf1 tiltLo[2], tiltHi[2], roots[2];
    OnePoleHP sideHp;
};

//==============================================================================
// Stereo-linked look-ahead brickwall limiter.
//
//   g_raw  = min(1, ceiling / peak)
//   g_rel  = instant attack, exponential release
//   g_min  = sliding minimum over W samples
//   g_box  = moving average over W samples
//   out    = in delayed by (W-1) * g_box
//
// With delay D = W-1 every term of the moving average is <= g_raw[n-D], so the
// output can never exceed the ceiling (see NOTES.md for the proof).
class Limiter
{
public:
    void prepare (float sampleRate, float lookaheadMs = 2.0f)
    {
        sr = sampleRate;
        W = std::max (8, int (std::lround (lookaheadMs * 0.001f * sr)));
        D = W - 1;
        delayL.assign (size_t (W), 0.0f);
        delayR.assign (size_t (W), 0.0f);
        box.assign (size_t (W), 1.0f);
        dqVal.assign (size_t (W + 1), 1.0f);
        dqIdx.assign (size_t (W + 1), 0);
        relCoef = 1.0f - std::exp (-1.0f / (0.120f * sr));
        reset();
    }

    void reset()
    {
        std::fill (delayL.begin(), delayL.end(), 0.0f);
        std::fill (delayR.begin(), delayR.end(), 0.0f);
        std::fill (box.begin(), box.end(), 1.0f);
        boxSum = double (W);
        dPos = bPos = 0;
        dqHead = dqTail = 0; dqCount = 0;
        n = 0;
        gRel = 1.0f;
        gr = 1.0f;
    }

    int latency() const { return D; }
    float gainReduction() const { return gr; }  // most recent applied gain (1 = none)
    void setCeiling (float c) { ceiling = c; }

    inline void process (float& l, float& r)
    {
        // --- gain computer
        const float peak = std::max (std::abs (l), std::abs (r));
        const float gRaw = peak > ceiling ? ceiling / peak : 1.0f;
        gRel = std::min (gRaw, gRel + (1.0f - gRel) * relCoef);

        // --- sliding minimum (monotonic deque over the last W samples)
        const int cap = W + 1;
        while (dqCount > 0)
        {
            const int last = (dqTail + cap - 1) % cap;
            if (dqVal[size_t (last)] >= gRel) { dqTail = last; --dqCount; }
            else break;
        }
        dqVal[size_t (dqTail)] = gRel; dqIdx[size_t (dqTail)] = n;
        dqTail = (dqTail + 1) % cap; ++dqCount;
        if (n - dqIdx[size_t (dqHead)] >= W) { dqHead = (dqHead + 1) % cap; --dqCount; }
        const float gMin = dqVal[size_t (dqHead)];
        ++n;
        if (n > (1 << 30)) rebaseIndices();

        // --- moving average
        boxSum += double (gMin) - double (box[size_t (bPos)]);
        box[size_t (bPos)] = gMin;
        bPos = (bPos + 1) % W;
        float g = float (boxSum / double (W));
        g = std::min (g, 1.0f);

        // --- delay (D = W-1 samples)
        float dl, dr;
        if (D > 0)
        {
            dl = delayL[size_t (dPos)]; dr = delayR[size_t (dPos)];
            delayL[size_t (dPos)] = l;  delayR[size_t (dPos)] = r;
            dPos = (dPos + 1) % D;
        }
        else { dl = l; dr = r; }

        gr = g;
        // The final clamp only ever catches floating-point rounding in the moving average.
        l = clampf (dl * g, -ceiling, ceiling);
        r = clampf (dr * g, -ceiling, ceiling);
    }

private:
    void rebaseIndices()
    {
        const int cap = W + 1;
        const int base = dqIdx[size_t (dqHead)];
        for (int i = 0, p = dqHead; i < dqCount; ++i, p = (p + 1) % cap)
            dqIdx[size_t (p)] -= base;
        n -= base;
    }

    float sr = 48000.0f, ceiling = 0.966f, relCoef = 0.001f, gRel = 1.0f, gr = 1.0f;
    int W = 96, D = 95, dPos = 0, bPos = 0, dqHead = 0, dqTail = 0, dqCount = 0, n = 0;
    double boxSum = 0.0;
    std::vector<float> delayL, delayR, box, dqVal;
    std::vector<int> dqIdx;
};

} // namespace verdant
