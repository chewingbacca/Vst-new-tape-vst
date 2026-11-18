// Verdant — monophonic pitch tracker (YIN: de Cheveigné & Kawahara, JASA 2002),
// run on a decimated copy of the input so it costs well under 1 % CPU.
#pragma once

#include "Util.h"

namespace verdant
{

class PitchTracker
{
public:
    void prepare (float sampleRate)
    {
        sr = sampleRate;
        decim = std::max (1, int (std::lround (sr / 12000.0f)));
        dRate = sr / float (decim);
        lp.set (std::min (2600.0f, dRate * 0.4f), 0.7f, sr);

        tauMax = int (dRate / 58.0f) + 2;
        tauMin = std::max (4, int (dRate / 1200.0f));
        window = 1024;
        integ = window - tauMax;
        hop = 384;
        buf.assign (size_t (window), 0.0f);
        lin.assign (size_t (window), 0.0f);
        diff.assign (size_t (tauMax + 1), 0.0f);
        holdSamples = int (0.6f * dRate);
        reset();
    }

    void reset()
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        lp.reset();
        wpos = 0; phase = 0; sinceHop = 0;
        midi = -1.0f; hz = 0.0f; confidence = 0.0f; sinceConfident = 1 << 28;
    }

    inline void push (float x)
    {
        const float f = lp.low (x);
        if (++phase < decim) return;
        phase = 0;
        buf[size_t (wpos)] = f;
        wpos = (wpos + 1) % window;
        if (sinceConfident < (1 << 28)) ++sinceConfident;
        if (++sinceHop >= hop)
        {
            sinceHop = 0;
            analyse();
        }
    }

    // MIDI note (fractional) of the most recent confident estimate, or < 0 if none is fresh.
    float currentMidi() const { return sinceConfident < holdSamples ? midi : -1.0f; }
    float currentHz() const   { return sinceConfident < holdSamples ? hz : 0.0f; }

private:
    void analyse()
    {
        // unwrap ring into a linear frame, oldest first
        double energy = 0.0;
        for (int i = 0; i < window; ++i)
        {
            lin[size_t (i)] = buf[size_t ((wpos + i) % window)];
            energy += double (lin[size_t (i)]) * lin[size_t (i)];
        }
        if (energy / double (window) < 4.0e-6)   // below about -54 dBFS RMS
        {
            confidence = 0.0f;
            return;
        }

        // difference function + cumulative mean normalisation
        float running = 0.0f;
        diff[0] = 1.0f;
        const float* x = lin.data();
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            float d = 0.0f;
            for (int j = 0; j < integ; ++j)
            {
                const float e = x[j] - x[j + tau];
                d += e * e;
            }
            running += d;
            diff[size_t (tau)] = running > 0.0f ? d * float (tau) / running : 1.0f;
        }

        // first dip below the threshold, followed to its local minimum
        int best = -1;
        for (int tau = tauMin; tau < tauMax; ++tau)
        {
            if (diff[size_t (tau)] < 0.12f)
            {
                while (tau + 1 < tauMax && diff[size_t (tau + 1)] < diff[size_t (tau)]) ++tau;
                best = tau;
                break;
            }
        }
        if (best < 0)
        {
            confidence = 0.0f;
            return;
        }

        // parabolic refinement
        float tauF = float (best);
        if (best > 1 && best < tauMax)
        {
            const float a = diff[size_t (best - 1)], b = diff[size_t (best)], c = diff[size_t (best + 1)];
            const float den = a - 2.0f * b + c;
            if (std::abs (den) > 1.0e-9f)
                tauF += clampf (0.5f * (a - c) / den, -0.5f, 0.5f);
        }

        const float f0 = dRate / tauF;
        const float m = 69.0f + 12.0f * std::log2 (f0 / 440.0f);
        confidence = 1.0f - diff[size_t (best)];

        // small moves are smoothed (vibrato), jumps are taken immediately
        if (midi >= 0.0f && sinceConfident < holdSamples && std::abs (m - midi) < 0.5f)
            midi += 0.6f * (m - midi);
        else
            midi = m;
        hz = 440.0f * std::exp2 ((midi - 69.0f) / 12.0f);
        sinceConfident = 0;
    }

    float sr = 48000.0f, dRate = 12000.0f;
    int decim = 4, tauMin = 10, tauMax = 208, window = 1024, integ = 816, hop = 384;
    int wpos = 0, phase = 0, sinceHop = 0, holdSamples = 7200, sinceConfident = 1 << 28;
    float midi = -1.0f, hz = 0.0f, confidence = 0.0f;
    Svf lp;
    std::vector<float> buf, lin, diff;
};

} // namespace verdant
