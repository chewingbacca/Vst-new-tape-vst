#include "Stretch.h"

namespace splinter
{
namespace
{
// Piecewise-linear map from output time to input time. Short regions around
// each transient run at unity speed so attacks are copied, not stretched; the
// audio between them absorbs the tempo change.
struct TimeMap
{
    std::vector<double> xo, xi;                       // breakpoints (output, input), ascending
    std::vector<std::pair<double, double>> prot;      // protected (unity speed) output intervals
    double inLen = 1.0, outLen = 1.0;

    // preWanted / postWanted: unity-speed time before / after each onset (samples)
    void build (const std::vector<double>& onsetsIn, double inLength, double outLength, double preWanted, double postWanted)
    {
        inLen = inLength; outLen = outLength;
        const double R = outLength / inLength, k = std::min (1.0, R);
        // the loop start is treated as an onset too (there is nearly always a hit on the one)
        std::vector<double> on { 0.0 };
        for (double o : onsetsIn) if (o > on.back() + 0.02 * 48000.0 * 0.5 && o < inLength - 64.0) on.push_back (o);
        xo.clear(); xi.clear(); prot.clear();
        const size_t n = on.size();
        std::vector<double> pre (n), post (n);
        for (size_t i = 0; i < n; ++i)
        {
            const double prevGap = i > 0 ? on[i] - on[i - 1] : (inLength - on[n - 1]) + on[0];
            const double nextGap = i + 1 < n ? on[i + 1] - on[i] : (inLength - on[i]) + on[0];
            pre[i] = std::min (preWanted, 0.3 * prevGap * k);
            post[i] = std::min (postWanted, 0.5 * nextGap * k);
        }
        // region 0 straddles the loop point: its tail [0, post0) opens the map and
        // its head [outLen - pre0, outLen) closes it
        xo.push_back (0.0); xi.push_back (0.0);
        xo.push_back (post[0]); xi.push_back (post[0]);
        prot.push_back ({ 0.0, post[0] });
        for (size_t i = 1; i < n; ++i)
        {
            const double a0 = on[i] * R - pre[i], a1 = on[i] * R + post[i];
            if (a0 <= xo.back() + 4.0 || on[i] - pre[i] <= xi.back() + 4.0) continue;
            if (a1 >= outLength - pre[0] - 4.0 || on[i] + post[i] >= inLength - pre[0] - 4.0) continue;
            xo.push_back (a0); xi.push_back (on[i] - pre[i]);
            xo.push_back (a1); xi.push_back (on[i] + post[i]);
            prot.push_back ({ a0, a1 });
        }
        xo.push_back (outLength - pre[0]); xi.push_back (inLength - pre[0]);
        xo.push_back (outLength); xi.push_back (inLength);
        prot.push_back ({ outLength - pre[0], outLength });
    }
    double map (double t) const
    {
        const double cyc = std::floor (t / outLen);
        t -= cyc * outLen;
        size_t i = hint < xo.size() - 1 ? hint : 0;
        if (xo[i] > t) i = 0;
        while (i + 2 < xo.size() && xo[i + 1] <= t) ++i;
        hint = i;
        const double span = xo[i + 1] - xo[i];
        const double f = span > 0.0 ? (t - xo[i]) / span : 0.0;
        return cyc * inLen + xi[i] + f * (xi[i + 1] - xi[i]);
    }
    bool isProtected (double t) const
    {
        t = floorMod (t, outLen);
        for (auto& p : prot) if (t >= p.first && t < p.second) return true;
        return false;
    }
    mutable size_t hint = 0;
};

inline float wrapRead (const std::vector<float>& v, int64_t i)
{
    const int64_t n = (int64_t) v.size();
    i %= n; if (i < 0) i += n;
    return v[(size_t) i];
}
} // namespace

// ---------------------------------------------------------------- phase vocoder (identity phase locking)
void stretchTonal (const std::vector<float> in[2], std::vector<float> out[2], int64_t outLength,
                   const std::vector<double>& onsets, double sampleRate)
{
    const int64_t inLength = (int64_t) in[0].size();
    for (int c = 0; c < 2; ++c) out[c].assign ((size_t) outLength, 0.0f);
    if (inLength < 64 || outLength < 64) return;

    const int order = sampleRate > 100000.0 ? 13 : (sampleRate > 50000.0 ? 12 : 11);
    const int n = 1 << order, half = n / 2, hs = n / 4, hr = n / 8;
    FFT fft (order);
    std::vector<float> win ((size_t) n);
    for (int i = 0; i < n; ++i) win[(size_t) i] = (float) (0.5 - 0.5 * std::cos (kTwoPi * i / n));

    TimeMap tm; tm.build (onsets, (double) inLength, (double) outLength, 0.003 * sampleRate, 0.045 * sampleRate);

    std::vector<float> re[2], im[2], pre[2], pim[2], yre[2], yim[2], norm ((size_t) outLength, 0.0f);
    for (int c = 0; c < 2; ++c) { re[c].resize ((size_t) n); im[c].resize ((size_t) n); pre[c].resize ((size_t) n); pim[c].resize ((size_t) n); yre[c].resize ((size_t) n); yim[c].resize ((size_t) n); }
    std::vector<float> mag ((size_t) half + 1), synthPhase ((size_t) half + 1, 0.0f);
    std::vector<int> peaks;
    const float expected = (float) (kTwoPi * hr / n);
    bool wasProtected = false, first = true;

    // run one extra lap of frames first so the phase state at the loop start is
    // the steady state reached at the loop end (seamless looping)
    const int64_t framesPerLap = (outLength + hs - 1) / hs;
    for (int64_t m = -std::min<int64_t> (framesPerLap, 24); m < framesPerLap + 4; ++m)
    {
        const double tOut = (double) m * hs;
        const int64_t tIn = (int64_t) std::llround (tm.map (tOut));
        const bool prot = tm.isProtected (tOut);
        for (int c = 0; c < 2; ++c)
        {
            for (int i = 0; i < n; ++i)
            {
                const float w = win[(size_t) i];
                re[c][(size_t) i] = wrapRead (in[c], tIn - half + i) * w; im[c][(size_t) i] = 0.0f;
                pre[c][(size_t) i] = wrapRead (in[c], tIn - hr - half + i) * w; pim[c][(size_t) i] = 0.0f;
            }
            fft.transform (re[c].data(), im[c].data(), false);
            fft.transform (pre[c].data(), pim[c].data(), false);
        }
        float peakMag = 0.0f;
        for (int k = 0; k <= half; ++k)
        {
            const float mr = re[0][(size_t) k] + re[1][(size_t) k], mi = im[0][(size_t) k] + im[1][(size_t) k];
            float mm = std::sqrt (mr * mr + mi * mi);
            if (mm < 1.0e-9f) mm = std::sqrt (re[0][(size_t) k] * re[0][(size_t) k] + im[0][(size_t) k] * im[0][(size_t) k]);
            mag[(size_t) k] = mm; peakMag = std::max (peakMag, mm);
        }
        peaks.clear();
        for (int k = 2; k < half - 2; ++k)
        {
            const float v = mag[(size_t) k];
            if (v > peakMag * 1.0e-5f && v > mag[(size_t) k - 1] && v >= mag[(size_t) k + 1] && v > mag[(size_t) k - 2] && v >= mag[(size_t) k + 2]) { peaks.push_back (k); ++k; }
        }
        const bool resetPhase = first || (prot && ! wasProtected);
        wasProtected = prot; first = false;

        for (int c = 0; c < 2; ++c) { std::fill (yre[c].begin(), yre[c].end(), 0.0f); std::fill (yim[c].begin(), yim[c].end(), 0.0f); }
        std::vector<float> newPhase ((size_t) half + 1, 0.0f);
        if (peaks.empty() || resetPhase)
        {
            for (int c = 0; c < 2; ++c) for (int k = 0; k <= half; ++k) { yre[c][(size_t) k] = re[c][(size_t) k]; yim[c][(size_t) k] = im[c][(size_t) k]; }
            // rotation zero everywhere
        }
        else
        {
            for (size_t p = 0; p < peaks.size(); ++p)
            {
                const int k = peaks[p];
                int lo = 0, hi = half;
                if (p > 0) { const int a = peaks[p - 1]; int best = a; float bm = mag[(size_t) a]; for (int j = a; j <= k; ++j) if (mag[(size_t) j] < bm) { bm = mag[(size_t) j]; best = j; } lo = best + 1; }
                if (p + 1 < peaks.size()) { const int b = peaks[p + 1]; int best = k; float bm = mag[(size_t) k]; for (int j = k; j <= b; ++j) if (mag[(size_t) j] < bm) { bm = mag[(size_t) j]; best = j; } hi = best; }
                // instantaneous frequency of the peak from the two analysis frames
                const float mr = re[0][(size_t) k] + re[1][(size_t) k], mi = im[0][(size_t) k] + im[1][(size_t) k];
                const float qr = pre[0][(size_t) k] + pre[1][(size_t) k], qi = pim[0][(size_t) k] + pim[1][(size_t) k];
                const float phNow = std::atan2 (mi, mr), phPrev = std::atan2 (qi, qr);
                float d = phNow - phPrev - expected * (float) k;
                d -= (float) kTwoPi * std::round (d / (float) kTwoPi);
                const float omega = ((float) k + d / expected) * (float) (kTwoPi / n);   // rad / sample
                // rotation accumulated by this partial = synth phase - analysis phase
                float rotation = synthPhase[(size_t) k] + omega * (float) hs - phNow;
                rotation -= (float) kTwoPi * std::round (rotation / (float) kTwoPi);
                const float cr = std::cos (rotation), ci = std::sin (rotation);
                for (int j = lo; j <= hi; ++j)
                    for (int c = 0; c < 2; ++c)
                    {
                        yre[c][(size_t) j] = re[c][(size_t) j] * cr - im[c][(size_t) j] * ci;
                        yim[c][(size_t) j] = re[c][(size_t) j] * ci + im[c][(size_t) j] * cr;
                    }
            }
        }
        for (int k = 0; k <= half; ++k)
        {
            const float mr = yre[0][(size_t) k] + yre[1][(size_t) k], mi = yim[0][(size_t) k] + yim[1][(size_t) k];
            newPhase[(size_t) k] = (mr * mr + mi * mi) > 1.0e-18f ? std::atan2 (mi, mr) : std::atan2 (yim[0][(size_t) k], yre[0][(size_t) k]);
        }
        synthPhase.swap (newPhase);

        if (m < 0) continue;
        for (int c = 0; c < 2; ++c)
        {
            for (int k = 1; k < half; ++k) { yre[c][(size_t) (n - k)] = yre[c][(size_t) k]; yim[c][(size_t) (n - k)] = -yim[c][(size_t) k]; }
            fft.transform (yre[c].data(), yim[c].data(), true);
        }
        const int64_t centre = m * hs;
        for (int i = 0; i < n; ++i)
        {
            int64_t j = (centre - half + i) % outLength; if (j < 0) j += outLength;
            const float w = win[(size_t) i];
            out[0][(size_t) j] += yre[0][(size_t) i] * w / (float) n;
            out[1][(size_t) j] += yre[1][(size_t) i] * w / (float) n;
            norm[(size_t) j] += w * w;
        }
    }
    for (int64_t i = 0; i < outLength; ++i)
    {
        const float g = norm[(size_t) i] > 1.0e-3f ? 1.0f / norm[(size_t) i] : 0.0f;
        out[0][(size_t) i] *= g; out[1][(size_t) i] *= g;
    }
}

// ---------------------------------------------------------------- WSOLA with protected transients
void stretchBeats (const std::vector<float> in[2], std::vector<float> out[2], int64_t outLength,
                   const std::vector<double>& onsets, double sampleRate)
{
    const int64_t inLength = (int64_t) in[0].size();
    for (int c = 0; c < 2; ++c) out[c].assign ((size_t) outLength, 0.0f);
    if (inLength < 64 || outLength < 64) return;

    const int w = std::max (64, (int) (0.024 * sampleRate)) & ~1;   // segment length
    const int hop = w / 2;
    const int search = std::max (16, (int) (0.010 * sampleRate));
    const int dec = std::max (1, (int) (sampleRate / 12000.0));     // decimation for the correlation search
    // unity regions start early enough that no searched segment can smear a hit
    TimeMap tm; tm.build (onsets, (double) inLength, (double) outLength, (double) (hop + search + 16), 0.045 * sampleRate);
    std::vector<float> mono ((size_t) inLength);
    for (int64_t i = 0; i < inLength; ++i) mono[(size_t) i] = 0.5f * (in[0][(size_t) i] + in[1][(size_t) i]);
    std::vector<float> norm ((size_t) outLength, 0.0f), win ((size_t) w);
    for (int i = 0; i < w; ++i) win[(size_t) i] = (float) (0.5 - 0.5 * std::cos (kTwoPi * (i + 0.5) / w));

    int64_t prevIn = 0; bool first = true;
    const int64_t frames = (outLength + hop - 1) / hop + 1;
    for (int64_t m = 0; m < frames; ++m)
    {
        const double tOut = (double) m * hop;
        const int64_t nominal = (int64_t) std::llround (tm.map (tOut));
        int64_t pos = nominal;
        if (! first && ! tm.isProtected (tOut))
        {
            const int64_t natural = prevIn + hop;      // where the previous segment would continue
            if (std::llabs (natural - nominal) <= 2)
                pos = natural;                          // already aligned: exact copy
            else
            {
                // pick the offset whose segment best matches the natural continuation
                double best = -1.0e30; int bestOff = 0;
                for (int off = -search; off <= search; off += dec)
                {
                    double num = 0.0, den = 1.0e-9;
                    for (int i = 0; i < w; i += dec)
                    {
                        const float a = wrapRead (mono, natural + i), b = wrapRead (mono, nominal + off + i);
                        num += (double) a * b; den += (double) b * b;
                    }
                    const double score = num / std::sqrt (den);
                    if (score > best) { best = score; bestOff = off; }
                }
                if (dec > 1)
                {
                    const int c0 = bestOff; best = -1.0e30;
                    for (int off = c0 - dec + 1; off <= c0 + dec - 1; ++off)
                    {
                        double num = 0.0, den = 1.0e-9;
                        for (int i = 0; i < w; i += dec)
                        {
                            const float a = wrapRead (mono, natural + i), b = wrapRead (mono, nominal + off + i);
                            num += (double) a * b; den += (double) b * b;
                        }
                        const double score = num / std::sqrt (den);
                        if (score > best) { best = score; bestOff = off; }
                    }
                }
                pos = nominal + bestOff;
            }
        }
        first = false; prevIn = pos;
        for (int i = 0; i < w; ++i)
        {
            int64_t j = ((int64_t) (m * hop) - hop + i) % outLength; if (j < 0) j += outLength;
            const float g = win[(size_t) i];
            out[0][(size_t) j] += wrapRead (in[0], pos - hop + i) * g;
            out[1][(size_t) j] += wrapRead (in[1], pos - hop + i) * g;
            norm[(size_t) j] += g;
        }
    }
    for (int64_t i = 0; i < outLength; ++i)
    {
        const float g = norm[(size_t) i] > 1.0e-3f ? 1.0f / norm[(size_t) i] : 0.0f;
        out[0][(size_t) i] *= g; out[1][(size_t) i] *= g;
    }
}

// ---------------------------------------------------------------- top level
void renderLoop (const AudioData& src, const LoopAnalysis& an, double targetSampleRate, double targetBpm,
                 StretchMode mode, RenderedLoop& out)
{
    const int64_t srcLen = src.length();
    const double beats = std::max (0.25, an.beats);
    const int64_t outLen = std::max<int64_t> (64, (int64_t) std::llround (beats * 60.0 / targetBpm * targetSampleRate));
    out.allocate (outLen);
    out.beats = beats; out.sampleRate = targetSampleRate; out.bpm = targetBpm;
    if (srcLen < 16) { out.fixGuards(); return; }

    // length the loop would have at the target sample rate with no stretch
    const int64_t natural = std::max<int64_t> (64, (int64_t) std::llround ((double) srcLen * targetSampleRate / src.sampleRate));
    const double ratio = (double) outLen / (double) natural;
    if (mode == StretchMode::Auto) mode = an.percussiveness > 0.55f ? StretchMode::Beats : StretchMode::Tonal;

    std::vector<float> tmp[2], res[2];
    if (mode == StretchMode::Tape || std::fabs (ratio - 1.0) < 0.002)
    {
        for (int c = 0; c < 2; ++c) resample (src.ch[c], res[c], outLen, true);
        out.pitchPreserved = std::fabs (ratio - 1.0) < 0.002;
    }
    else
    {
        for (int c = 0; c < 2; ++c) resample (src.ch[c], tmp[c], natural, true);
        std::vector<double> onsets;
        for (double t : an.onsetTimes) onsets.push_back (t * targetSampleRate);
        if (mode == StretchMode::Beats) stretchBeats (tmp, res, outLen, onsets, targetSampleRate);
        else stretchTonal (tmp, res, outLen, onsets, targetSampleRate);
        out.pitchPreserved = true;
    }
    for (int c = 0; c < 2; ++c) std::copy (res[c].begin(), res[c].end(), out.channel (c));
    out.fixGuards();
}
} // namespace splinter
