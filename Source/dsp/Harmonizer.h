// Splinter DSP — real-time polyphonic harmoniser.
//
// A phase vocoder that shifts whole spectral-peak regions (after Laroche &
// Dolson, "New phase-vocoder techniques for pitch-shifting, harmonizing and
// other exotic effects", 1999 — technique only, implementation is original).
// Moving each peak's region rigidly keeps the phase relationships around the
// peak intact, so there is very little of the usual vocoder "phasiness". All
// voices are summed in the frequency domain, so the cost is two inverse FFTs
// per hop regardless of voice count. Formants are kept by re-imposing the
// cepstrally smoothed spectral envelope of the input.
//
// Latency is exactly fftSize() samples.
#pragma once
#include "FFT.h"
#include "Model.h"

namespace splinter
{
class Harmonizer
{
public:
    struct Voice
    {
        float ratio = 1.0f;     // frequency ratio
        float gainL = 0.0f, gainR = 0.0f;
    };

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        order = sampleRate > 100000.0 ? 13 : (sampleRate > 50000.0 ? 12 : 11);
        n = 1 << order; hop = n / 4; half = n / 2;
        fft.setOrder (order);
        window.resize ((size_t) n);
        for (int i = 0; i < n; ++i) window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (kTwoPi * i / n));
        for (auto* v : { &inL, &inR }) v->assign ((size_t) n, 0.0f);
        for (auto* v : { &outL, &outR }) v->assign ((size_t) (2 * n), 0.0f);
        for (auto* v : { &reL, &imL, &reR, &imR, &yReL, &yImL, &yReR, &yImR, &cRe, &cIm }) v->assign ((size_t) n, 0.0f);
        for (auto* v : { &mag, &env, &prevMag, &prevPhase }) v->assign ((size_t) half + 1, 0.0f);
        peakOf.assign ((size_t) half + 1, 0);
        peaks.assign ((size_t) half / 2 + 2, Peak());
        for (auto& r : rot) r.assign ((size_t) half + 1, 0.0f);
        for (auto& r : rotNew) r.assign ((size_t) half + 1, 0.0f);
        lifterLen = clampv ((int) (sampleRate / 650.0), 16, half / 2);
        reset();
    }

    void reset()
    {
        for (auto* v : { &inL, &inR, &outL, &outR, &prevMag, &prevPhase }) std::fill (v->begin(), v->end(), 0.0f);
        for (auto& r : rot) std::fill (r.begin(), r.end(), 0.0f);
        std::fill (std::begin (chroma), std::end (chroma), 0.0f);
        pos = 0; hopCount = 0; pitchClass = -1; confidence = 0.0f;
    }

    int fftSize() const { return n; }
    void setVoice (int i, const Voice& v) { voices[i] = v; }
    void setFormant (float amount) { formant = clampv (amount, 0.0f, 1.0f); }
    // While inactive the input history is still tracked but no FFT work is done.
    void setActive (bool a) { active = a; }
    bool isActive() const { return active; }

    int detectedPitchClass() const { return pitchClass; }   // 0..11 (C=0) or -1
    float pitchConfidence() const { return confidence; }

    inline void process (float l, float r, float& oL, float& oR)
    {
        const int ip = (int) (pos & (int64_t) (n - 1));
        inL[(size_t) ip] = l; inR[(size_t) ip] = r;
        const int op = (int) (pos & (int64_t) (2 * n - 1));
        oL = outL[(size_t) op]; oR = outR[(size_t) op];
        outL[(size_t) op] = 0.0f; outR[(size_t) op] = 0.0f;
        ++pos;
        if (++hopCount >= hop)
        {
            hopCount = 0;
            if (active) frame();
        }
    }

private:
    struct Peak { int bin = 0, lo = 0, hi = 0; float freq = 0.0f; };

    void frame()
    {
        // ---- analysis: frame = the most recent n input samples
        const int start = (int) (pos & (int64_t) (n - 1)); // oldest sample
        for (int i = 0; i < n; ++i)
        {
            const int j = (start + i) & (n - 1);
            const float w = window[(size_t) i];
            reL[(size_t) i] = inL[(size_t) j] * w; imL[(size_t) i] = 0.0f;
            reR[(size_t) i] = inR[(size_t) j] * w; imR[(size_t) i] = 0.0f;
        }
        fft.transform (reL.data(), imL.data(), false);
        fft.transform (reR.data(), imR.data(), false);

        float flux = 0.0f, total = 0.0f, peakMag = 0.0f;
        for (int k = 0; k <= half; ++k)
        {
            const float ml = std::sqrt (reL[(size_t) k] * reL[(size_t) k] + imL[(size_t) k] * imL[(size_t) k]);
            const float mr = std::sqrt (reR[(size_t) k] * reR[(size_t) k] + imR[(size_t) k] * imR[(size_t) k]);
            const float m = ml + mr;
            mag[(size_t) k] = m;
            const float d = m - prevMag[(size_t) k];
            if (d > 0.0f) flux += d;
            total += m;
            peakMag = std::max (peakMag, m);
        }
        const bool transient = flux > 0.55f * total && total > 1.0e-3f * (float) n;
        if (peakMag < 1.0e-7f * (float) n)
        {
            std::copy (mag.begin(), mag.end(), prevMag.begin());
            return; // silence in, silence out
        }

        // ---- peaks and regions of influence
        int numPeaks = 0;
        const float floorMag = peakMag * 1.0e-5f;
        for (int k = 2; k < half - 2; ++k)
        {
            const float m = mag[(size_t) k];
            if (m > floorMag && m > mag[(size_t) k - 1] && m >= mag[(size_t) k + 1] && m > mag[(size_t) k - 2] && m >= mag[(size_t) k + 2])
            {
                if (numPeaks < (int) peaks.size()) peaks[(size_t) numPeaks++].bin = k;
                ++k;
            }
        }
        const float binToHz = (float) (sr / n);
        const float expectedScale = (float) (kTwoPi * hop / n);
        for (int p = 0; p < numPeaks; ++p)
        {
            Peak& pk = peaks[(size_t) p];
            int lo = 0, hi = half;
            if (p > 0)
            {
                const int a = peaks[(size_t) p - 1].bin; int best = a; float bm = mag[(size_t) a];
                for (int k = a; k <= pk.bin; ++k) if (mag[(size_t) k] < bm) { bm = mag[(size_t) k]; best = k; }
                lo = best + 1;
                peaks[(size_t) p - 1].hi = best;
            }
            pk.lo = lo; pk.hi = hi;
            // true frequency from the phase advance of (L+R) since the last frame
            const int k = pk.bin;
            float mr = reL[(size_t) k] + reR[(size_t) k], mi = imL[(size_t) k] + imR[(size_t) k];
            if (mr * mr + mi * mi < 1.0e-12f) { mr = reL[(size_t) k]; mi = imL[(size_t) k]; }
            const float ph = std::atan2 (mi, mr);
            float d = ph - prevPhase[(size_t) k] - expectedScale * (float) k;
            d -= (float) kTwoPi * std::round (d / (float) kTwoPi);
            pk.freq = (float) k + d / expectedScale;
            if (transient || std::fabs (pk.freq - (float) k) > 1.5f)
            {
                // unreliable phase history: fall back to parabolic interpolation
                const float a = std::log (mag[(size_t) k - 1] + 1.0e-12f), b = std::log (mag[(size_t) k] + 1.0e-12f), c = std::log (mag[(size_t) k + 1] + 1.0e-12f);
                const float den = a - 2.0f * b + c;
                pk.freq = (float) k + (std::fabs (den) > 1.0e-9f ? clampv (0.5f * (a - c) / den, -0.5f, 0.5f) : 0.0f);
            }
        }
        for (int k = 0; k <= half; ++k)
        {
            float mr = reL[(size_t) k] + reR[(size_t) k], mi = imL[(size_t) k] + imR[(size_t) k];
            if (mr * mr + mi * mi < 1.0e-12f) { mr = reL[(size_t) k]; mi = imL[(size_t) k]; }
            prevPhase[(size_t) k] = std::atan2 (mi, mr);
            prevMag[(size_t) k] = mag[(size_t) k];
        }

        trackPitch (numPeaks, binToHz);

        // ---- spectral envelope (cepstral smoothing) for formant preservation
        const bool useFormant = formant > 0.001f;
        if (useFormant)
        {
            for (int k = 0; k <= half; ++k)
            {
                const float lg = std::log (mag[(size_t) k] + peakMag * 1.0e-4f);
                cRe[(size_t) k] = lg; cIm[(size_t) k] = 0.0f;
                if (k > 0 && k < half) { cRe[(size_t) (n - k)] = lg; cIm[(size_t) (n - k)] = 0.0f; }
            }
            fft.transform (cRe.data(), cIm.data(), true);
            const float invN = 1.0f / (float) n;
            for (int q = 0; q < n; ++q)
            {
                const int qa = q <= half ? q : n - q;
                float w = 0.0f;
                if (qa < lifterLen) w = 0.5f + 0.5f * std::cos ((float) kPi * (float) qa / (float) lifterLen);
                cRe[(size_t) q] *= w * invN; cIm[(size_t) q] = 0.0f;
            }
            fft.transform (cRe.data(), cIm.data(), false);
            for (int k = 0; k <= half; ++k) env[(size_t) k] = cRe[(size_t) k]; // log envelope
        }

        // ---- synthesis: shift every peak region for every voice
        std::fill (yReL.begin(), yReL.end(), 0.0f); std::fill (yImL.begin(), yImL.end(), 0.0f);
        std::fill (yReR.begin(), yReR.end(), 0.0f); std::fill (yImR.begin(), yImR.end(), 0.0f);
        bool any = false;
        for (int v = 0; v < kNumHarmVoices; ++v)
        {
            const Voice& vc = voices[v];
            auto& rv = rot[v];
            auto& rn = rotNew[v];
            if (vc.gainL == 0.0f && vc.gainR == 0.0f) { if (voiceWasOn[v]) { std::fill (rv.begin(), rv.end(), 0.0f); voiceWasOn[v] = false; } continue; }
            voiceWasOn[v] = true; any = true;
            const float beta = vc.ratio;
            std::fill (rn.begin(), rn.end(), 0.0f);
            for (int p = 0; p < numPeaks; ++p)
            {
                const Peak& pk = peaks[(size_t) p];
                const float shiftBins = (beta - 1.0f) * pk.freq;
                const int s = (int) std::lround (shiftBins);
                float angle = transient ? 0.0f : rv[(size_t) pk.bin] + expectedScale * shiftBins;
                angle -= (float) kTwoPi * std::round (angle / (float) kTwoPi);
                const float cr = std::cos (angle), ci = std::sin (angle);
                for (int k = pk.lo; k <= pk.hi; ++k)
                {
                    rn[(size_t) k] = angle;
                    const int dst = k + s;
                    if (dst < 1 || dst >= half) continue;
                    float g = 1.0f;
                    if (useFormant)
                        g = std::exp (clampv ((env[(size_t) dst] - env[(size_t) k]) * formant, -2.5f, 2.5f));
                    const float gl = g * vc.gainL, gr = g * vc.gainR;
                    yReL[(size_t) dst] += gl * (reL[(size_t) k] * cr - imL[(size_t) k] * ci);
                    yImL[(size_t) dst] += gl * (reL[(size_t) k] * ci + imL[(size_t) k] * cr);
                    yReR[(size_t) dst] += gr * (reR[(size_t) k] * cr - imR[(size_t) k] * ci);
                    yImR[(size_t) dst] += gr * (reR[(size_t) k] * ci + imR[(size_t) k] * cr);
                }
            }
            std::swap (rv, rn);
        }
        if (! any) return;

        for (int k = 1; k < half; ++k)
        {
            yReL[(size_t) (n - k)] = yReL[(size_t) k]; yImL[(size_t) (n - k)] = -yImL[(size_t) k];
            yReR[(size_t) (n - k)] = yReR[(size_t) k]; yImR[(size_t) (n - k)] = -yImR[(size_t) k];
        }
        fft.transform (yReL.data(), yImL.data(), true);
        fft.transform (yReR.data(), yImR.data(), true);

        // overlap-add; frame sample i belongs to time (pos - n + i), which is
        // read back exactly n samples later.
        const float norm = 1.0f / ((float) n * 1.5f); // Hann^2 at 75% overlap sums to 1.5
        const int64_t base = pos; // time (pos - n + i) + latency n = pos + i
        for (int i = 0; i < n; ++i)
        {
            const int j = (int) ((base + i) & (int64_t) (2 * n - 1));
            const float w = window[(size_t) i] * norm;
            outL[(size_t) j] += yReL[(size_t) i] * w;
            outR[(size_t) j] += yReR[(size_t) i] * w;
        }
    }

    void trackPitch (int numPeaks, float binToHz)
    {
        float frameChroma[12] = {};
        float sum = 0.0f;
        for (int p = 0; p < numPeaks; ++p)
        {
            const float f = peaks[(size_t) p].freq * binToHz;
            if (f < 60.0f || f > 2500.0f) continue;
            const float note = 69.0f + 12.0f * std::log2 (f / 440.0f);
            const int pc = floorModI ((int64_t) std::lround (note), 12);
            const float w = mag[(size_t) peaks[(size_t) p].bin] / (1.0f + f / 800.0f);
            frameChroma[pc] += w; sum += w;
        }
        const float decay = (float) std::exp (-(double) hop / (0.12 * sr));
        float best = 0.0f, tot = 0.0f; int bi = -1;
        for (int i = 0; i < 12; ++i)
        {
            chroma[i] = chroma[i] * decay + (sum > 0.0f ? frameChroma[i] / sum : 0.0f) * (1.0f - decay);
            tot += chroma[i];
            // hysteresis in favour of the current pitch class
            const float c = chroma[i] * (i == pitchClass ? 1.25f : 1.0f);
            if (c > best) { best = c; bi = i; }
        }
        confidence = tot > 1.0e-6f && bi >= 0 ? chroma[bi] / tot : 0.0f;
        pitchClass = confidence > 0.3f ? bi : -1;
    }

    double sr = 48000.0;
    int order = 11, n = 2048, hop = 512, half = 1024, lifterLen = 64;
    FFT fft;
    std::vector<float> window, inL, inR, outL, outR;
    std::vector<float> reL, imL, reR, imR, yReL, yImL, yReR, yImR, cRe, cIm;
    std::vector<float> mag, env, prevMag, prevPhase;
    std::vector<int> peakOf;
    std::vector<Peak> peaks;
    std::vector<float> rot[kNumHarmVoices], rotNew[kNumHarmVoices];
    Voice voices[kNumHarmVoices];
    bool voiceWasOn[kNumHarmVoices] = {};
    float formant = 1.0f;
    bool active = false;
    int64_t pos = 0;
    int hopCount = 0;
    float chroma[12] = {};
    int pitchClass = -1;
    float confidence = 0.0f;
};
} // namespace splinter
