#include "SampleData.h"
#include "../util/Math.h"
#include <cstdio>

namespace atm {

namespace {
// 31-tap half-band low-pass (Kaiser-windowed sinc), used for the 2:1 decimation of mip levels.
struct HalfBand
{
    static constexpr int kTaps = 31;
    float h[kTaps];
    HalfBand()
    {
        const int mid = kTaps / 2;
        const double beta = 7.0;
        auto bessel0 = [] (double x) { double s = 1, t = 1; for (int k = 1; k < 30; ++k) { t *= (x / (2.0 * k)) * (x / (2.0 * k)); s += t; } return s; };
        double sum = 0;
        for (int i = 0; i < kTaps; ++i)
        {
            const int n = i - mid;
            const double sinc = n == 0 ? 0.5 : std::sin (kPi * 0.5 * n) / (kPi * n);
            const double r = (double) n / (double) mid;
            const double w = bessel0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / bessel0 (beta);
            h[i] = (float) (sinc * w); sum += h[i];
        }
        for (auto& v : h) v = (float) (v / sum);
    }
};
}

static void buildMips (SampleData& s)
{
    static const HalfBand hb;
    s.numMips = 1;
    for (int level = 1; level < SampleData::kMaxMips; ++level)
    {
        const SampleData::Mip& src = s.mip[level - 1];
        if (src.length < 64) break;
        SampleData::Mip& dst = s.mip[level];
        dst.length = src.length / 2;
        for (int c = 0; c < s.numChannels; ++c)
        {
            dst.ch[c].assign ((size_t) (dst.length + 2 * SampleData::kGuard), 0.f);
            const float* in = src.ch[c].data() + SampleData::kGuard;
            for (int i = 0; i < dst.length; ++i)
            {
                double acc = 0;
                for (int k = 0; k < HalfBand::kTaps; ++k)
                {
                    const int j = 2 * i + k - HalfBand::kTaps / 2;
                    if (j >= 0 && j < src.length) acc += (double) in[j] * hb.h[k];
                }
                dst.ch[c][(size_t) (i + SampleData::kGuard)] = (float) acc;
            }
        }
        s.numMips = level + 1;
    }
}

// Energy-rise onset detector: good enough to find the hits of a drum loop.
static void detectTransients (SampleData& s)
{
    s.transients.clear();
    s.transients.push_back (0);
    const int n = s.length;
    if (n < 2048) return;
    const int hop = std::max (16, (int) (s.sampleRate * 0.0015));       // ~1.5 ms
    const int frames = n / hop;
    std::vector<float> env ((size_t) frames, 0.f);
    const float* l = s.data (0, 0);
    const float* r = s.data (0, s.numChannels > 1 ? 1 : 0);
    float prev = 0.f;
    for (int f = 0; f < frames; ++f)
    {
        double e = 0;
        for (int i = 0; i < hop; ++i)
        {
            const float x = 0.5f * (l[f * hop + i] + r[f * hop + i]);
            const float d = x - prev; prev = x;                          // first difference: emphasise attacks
            e += (double) d * d + 0.05 * (double) x * x;
        }
        env[(size_t) f] = (float) std::sqrt (e / hop);
    }
    float peak = 0.f; for (float v : env) peak = std::max (peak, v);
    if (peak <= 1.0e-6f) return;
    const int minGap = std::max (1, (int) (s.sampleRate * 0.035) / hop);
    const int back = std::max (2, (int) (s.sampleRate * 0.012) / hop);
    int last = -minGap;
    float slow = env[0];
    for (int f = 1; f < frames; ++f)
    {
        // compare against a slow-moving average of what came before
        float local = 0.f;
        for (int k = 1; k <= back; ++k) local = std::max (local, env[(size_t) std::max (0, f - k)]);
        slow += 0.05f * (env[(size_t) f] - slow);
        const bool rise = env[(size_t) f] > local * 1.8f + peak * 0.04f && env[(size_t) f] > slow * 1.3f;
        if (rise && f - last >= minGap)
        {
            // walk back to where the rise starts
            int g = f;
            while (g > 0 && env[(size_t) (g - 1)] < env[(size_t) g] * 0.6f && f - g < 4) --g;
            const int pos = std::max (0, g * hop - hop / 2);
            if (pos > s.transients.back() + minGap * hop / 2) s.transients.push_back (pos);
            last = f;
        }
    }
}

SampleData* makeSampleData (const float* const* channels, int numChannels, int length, double sampleRate, const char* name, float rootNote)
{
    auto* s = new SampleData();
    s->numChannels = clamp (numChannels, 1, 2);
    s->length = std::max (0, length);
    s->sampleRate = sampleRate > 1000.0 ? sampleRate : 44100.0;
    s->rootNote = rootNote;
    std::snprintf (s->name, sizeof (s->name), "%s", name ? name : "");
    float pk = 0.f;
    s->mip[0].length = s->length;
    for (int c = 0; c < s->numChannels; ++c)
    {
        s->mip[0].ch[c].assign ((size_t) (s->length + 2 * SampleData::kGuard), 0.f);
        for (int i = 0; i < s->length; ++i)
        {
            float v = channels[c][i];
            if (! std::isfinite (v)) v = 0.f;
            s->mip[0].ch[c][(size_t) (i + SampleData::kGuard)] = v;
            pk = std::max (pk, std::fabs (v));
        }
    }
    s->peak = pk;
    buildMips (*s);
    detectTransients (*s);
    return s;
}

} // namespace atm
