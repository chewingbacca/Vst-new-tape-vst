// Linear-phase half-band oversampler (2x / 4x / 8x), one channel.
// Kaiser-windowed half-band FIRs designed at prepare() time; polyphase
// implementation so only the non-zero taps are computed.
#pragma once

#include "Util.h"

#include <array>

namespace swoon
{

namespace detail
{
inline double besselI0 (double x) noexcept
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 40; ++k)
    {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
        if (term < 1.0e-14 * sum)
            break;
    }
    return sum;
}
} // namespace detail

/** One half-band stage: length N = 4k + 3, centre tap 0.5, odd-offset taps t[j] (already doubled). */
class HalfbandStage
{
public:
    void design (int k, double kaiserBeta, int maxInputSamples)
    {
        numPairs = k + 1;
        taps.resize (static_cast<size_t> (numPairs));
        const int centre = 2 * k + 1;
        const int length = 4 * k + 3;
        const double i0b = detail::besselI0 (kaiserBeta);

        for (int j = 0; j < numPairs; ++j)
        {
            const int offset = 2 * j + 1;                       // distance from the centre tap
            const double x = 3.141592653589793 * offset / 2.0;
            const double sinc = std::sin (x) / x;               // sinc (offset / 2)
            const double r = static_cast<double> (offset) / (centre + 1);
            const double w = detail::besselI0 (kaiserBeta * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0b;
            taps[static_cast<size_t> (j)] = static_cast<float> (0.5 * sinc * w * 2.0);   // doubled: upsampler gain
        }

        // Normalise DC gain of the interpolated (even) phase to exactly 1.
        double sum = 0.0;
        for (float t : taps)
            sum += 2.0 * t;
        for (float& t : taps)
            t = static_cast<float> (t / sum);

        (void) length;
        histLen = 2 * numPairs;
        upHist.assign (static_cast<size_t> (histLen + maxInputSamples), 0.0f);
        downEven.assign (static_cast<size_t> (histLen + maxInputSamples), 0.0f);
        downOdd.assign (static_cast<size_t> (histLen + maxInputSamples), 0.0f);
        delaySamples = centre;
    }

    void reset() noexcept
    {
        std::fill (upHist.begin(), upHist.end(), 0.0f);
        std::fill (downEven.begin(), downEven.end(), 0.0f);
        std::fill (downOdd.begin(), downOdd.end(), 0.0f);
    }

    /** Group delay of one pass (up or down), in samples at this stage's higher rate. */
    int delayAtHighRate() const noexcept { return delaySamples; }

    /** n input samples -> 2n output samples. */
    void up (const float* in, float* out, int n) noexcept
    {
        float* h = upHist.data();
        std::memcpy (h + histLen, in, static_cast<size_t> (n) * sizeof (float));
        const int k = numPairs - 1;

        for (int m = 0; m < n; ++m)
        {
            const float* x = h + histLen + m;   // x[0] = newest input for this output pair
            float acc = 0.0f;
            for (int j = 0; j < numPairs; ++j)
                acc += taps[static_cast<size_t> (j)] * (x[-k - j - 1] + x[-k + j]);
            out[2 * m] = acc;
            out[2 * m + 1] = x[-k];
        }

        std::memmove (h, h + n, static_cast<size_t> (histLen) * sizeof (float));
    }

    /** 2n input samples -> n output samples. */
    void down (const float* in, float* out, int n) noexcept
    {
        float* e = downEven.data();
        float* o = downOdd.data();
        for (int m = 0; m < n; ++m)
        {
            e[histLen + m] = in[2 * m];
            o[histLen + m] = in[2 * m + 1];
        }
        const int k = numPairs - 1;

        for (int m = 0; m < n; ++m)
        {
            const float* ev = e + histLen + m;
            float acc = 0.0f;
            for (int j = 0; j < numPairs; ++j)
                acc += taps[static_cast<size_t> (j)] * (ev[-k - 1 - j] + ev[-k + j]);
            out[m] = 0.5f * (acc + o[histLen + m - k - 1]);
        }

        std::memmove (e, e + n, static_cast<size_t> (histLen) * sizeof (float));
        std::memmove (o, o + n, static_cast<size_t> (histLen) * sizeof (float));
    }

private:
    std::vector<float> taps, upHist, downEven, downOdd;
    int numPairs = 0, histLen = 0, delaySamples = 0;
};

/** Cascade of up to three half-band stages. */
class Oversampler
{
public:
    /** log2Factor: 1 = 2x, 2 = 4x, 3 = 8x. maxBlock = largest base-rate block passed to up()/down(). */
    void prepare (int log2Factor, int maxBlock)
    {
        numStages = std::clamp (log2Factor, 1, 3);
        // Stage 1 does the hard work next to the audio band; later stages have wide transition bands.
        static constexpr int kPerStage[3] = { 19, 5, 3 };
        static constexpr double betaPerStage[3] = { 9.0, 8.0, 8.0 };

        int rateMul = 1;
        int latencyTop = 0;
        const int top = 1 << numStages;

        for (int s = 0; s < numStages; ++s)
        {
            stages[static_cast<size_t> (s)].design (kPerStage[s], betaPerStage[s], maxBlock * rateMul);
            rateMul *= 2;
            // up + down, converted to samples at the top rate
            latencyTop += 2 * stages[static_cast<size_t> (s)].delayAtHighRate() * (top / rateMul);
        }

        latencyTopRate = latencyTop;
        for (auto& b : work)
            b.assign (static_cast<size_t> (maxBlock * top), 0.0f);
    }

    void reset() noexcept
    {
        for (int s = 0; s < numStages; ++s)
            stages[static_cast<size_t> (s)].reset();
    }

    int factor() const noexcept { return 1 << numStages; }

    /** Round-trip group delay in samples at the oversampled rate. */
    int latencyAtTopRate() const noexcept { return latencyTopRate; }

    /** Upsamples n base-rate samples; returns a buffer of n * factor() samples to process in place. */
    float* up (const float* in, int n) noexcept
    {
        const float* src = in;
        int len = n;
        for (int s = 0; s < numStages; ++s)
        {
            float* dst = work[static_cast<size_t> (s)].data();
            stages[static_cast<size_t> (s)].up (src, dst, len);
            src = dst;
            len *= 2;
        }
        return work[static_cast<size_t> (numStages - 1)].data();
    }

    /** Downsamples the buffer returned by up() (after in-place processing) into out[n]. */
    void down (float* out, int n) noexcept
    {
        int len = n << numStages;
        for (int s = numStages - 1; s >= 0; --s)
        {
            len /= 2;
            const float* src = work[static_cast<size_t> (s)].data();
            float* dst = s == 0 ? out : work[static_cast<size_t> (s - 1)].data();
            stages[static_cast<size_t> (s)].down (src, dst, len);
        }
    }

private:
    std::array<HalfbandStage, 3> stages;
    std::array<std::vector<float>, 3> work;
    int numStages = 1, latencyTopRate = 0;
};

} // namespace swoon
