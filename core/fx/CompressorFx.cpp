// Feed-forward, stereo-linked compressor with a selectable sidechain.
//
//   key (self / track 1-8 / external) -> 12 dB/oct high-pass -> max (|L|, |R|) -> 10 ms peak hold
//       -> dB -> static curve (threshold, ratio, soft knee) -> attack / release smoothing of the
//       gain reduction in the log domain -> + make-up -> gain applied to both channels alike.
//
// Structure and curve follow Giannoulis, Massberg & Reiss, "Digital Dynamic Range Compressor
// Design - A Tutorial and Analysis" (JAES 2012): level detection in the log domain placed after the
// gain computer, with a branching one-pole. Attack and release are the one-pole time constants
// (63 % of a step in dB). In front of it sits a sliding-window maximum over 10 ms (monotonic deque,
// Lemire 2006): for anything above 50 Hz the rectified key reads as a constant, so the gain does not
// follow the waveform and low sines stay clean even at the fastest attack and release. The price is
// that a release starts up to 10 ms late. No look-ahead, no latency.
#include "Effect.h"
#include "../dsp/Filters.h"
#include "../model/ParamTables.h"
#include <atomic>
#include <vector>

namespace atm {

namespace {

enum { Thresh, Ratio, Attack, Release, Makeup, Knee, Source, ScHpf };

// Maximum of the last `window` values, O(1) amortised, bounded by the window per call.
class SlidingMax
{
public:
    void prepare (int windowSamples)
    {
        window = std::max (1, windowSamples);
        int n = 2; while (n < window + 1) n <<= 1;
        val.assign ((size_t) n, 0.f); idx.assign ((size_t) n, 0);
        mask = n - 1;
        reset();
    }
    void reset() { head = tail = 0; now = 0; }
    inline float process (float x)
    {
        while (tail != head && val[(size_t) ((tail - 1) & mask)] <= x) tail = (tail - 1) & mask;
        val[(size_t) tail] = x; idx[(size_t) tail] = now; tail = (tail + 1) & mask;
        if ((uint32_t) (now - idx[(size_t) head]) >= (uint32_t) window) head = (head + 1) & mask;
        ++now;
        return val[(size_t) head];
    }
private:
    std::vector<float> val; std::vector<uint32_t> idx;
    int window = 1, mask = 1, head = 0, tail = 0; uint32_t now = 0;
};

class CompressorFx final : public Effect
{
public:
    void prepare (double sampleRate) override
    {
        sr = (float) sampleRate;
        hold.prepare ((int) std::lround (0.010 * sampleRate));
        makeupCoef = onePoleCoef (0.020f, sr);
        reset();
    }

    void reset() override
    {
        hpf[0].reset(); hpf[1].reset(); hold.reset();
        env = 0.f; first = true; meter.store (0.f, std::memory_order_relaxed);
    }

    void setParams (const float* p, const FxContext&) override
    {
        thresh = kFxCompressor[Thresh].toPhys (p[Thresh]);
        slope  = 1.f - 1.f / std::max (1.f, kFxCompressor[Ratio].toPhys (p[Ratio]));
        knee   = kFxCompressor[Knee].toPhys (p[Knee]);
        attCoef = onePoleCoef (kFxCompressor[Attack].toPhys (p[Attack]), sr);
        relCoef = onePoleCoef (kFxCompressor[Release].toPhys (p[Release]), sr);
        makeupTarget = kFxCompressor[Makeup].toPhys (p[Makeup]);
        source = clamp (kFxCompressor[Source].toIndex (p[Source]), 0, kScSources - 1);
        const float hz = kFxCompressor[ScHpf].toPhys (p[ScHpf]);
        if (hz != hpfHz) { hpfHz = hz; hpf[0].setQ (hz, 0.7071f, sr); hpf[1].setQ (hz, 0.7071f, sr); }
        if (first) { first = false; makeup = makeupTarget; }
    }

    void process (float* l, float* r, int n, const FxContext& c) override
    {
        const float* kl = l; const float* kr = r;
        if (source > 0 && c.scL[source] != nullptr)
        {
            kl = c.scL[source];
            kr = c.scR[source] != nullptr ? c.scR[source] : kl;
        }
        const float halfKnee = 0.5f * knee;
        const float kneeScale = knee > 0.f ? slope / (2.f * knee) : 0.f;
        for (int i = 0; i < n; ++i)
        {
            const float a = std::max (std::fabs (hpf[0].hp (kl[i])), std::fabs (hpf[1].hp (kr[i])));
            const float peak = hold.process (a);
            const float over = 8.685889638f * std::log (std::max (peak, 1.0e-6f)) - thresh;    // dB above threshold
            float target;                                                                       // wanted reduction, dB >= 0
            if (over <= -halfKnee)      target = 0.f;
            else if (over < halfKnee)   { const float t = over + halfKnee; target = kneeScale * t * t; }
            else                        target = slope * over;
            env += (target > env ? attCoef : relCoef) * (target - env);
            if (env < 1.0e-6f) env = 0.f;
            makeup += makeupCoef * (makeupTarget - makeup);
            const float g = std::exp (0.1151292546f * (makeup - env));
            l[i] = safety (l[i] * g); r[i] = safety (r[i] * g);
        }
        hpf[0].sanitise(); hpf[1].sanitise();
        hpf[0].ic1 = undenorm (hpf[0].ic1); hpf[0].ic2 = undenorm (hpf[0].ic2);
        hpf[1].ic1 = undenorm (hpf[1].ic1); hpf[1].ic2 = undenorm (hpf[1].ic2);
        meter.store (env, std::memory_order_relaxed);
    }

    // Current gain reduction in dB (positive = that many dB down, make-up not included). For a meter;
    // safe to call from another thread.
    float gainReductionDb() const { return meter.load (std::memory_order_relaxed); }

private:
    // make-up gain on a signal below the threshold can push it far over full scale: transparent
    // below +-2, never above +-4
    static inline float safety (float y)
    {
        const float m = std::fabs (y);
        return m > 2.f ? std::copysign (2.f + 2.f * fastTanh (0.5f * (m - 2.f)), y) : y;
    }

    float sr = 48000.f;
    Svf hpf[2];
    SlidingMax hold;
    float hpfHz = -1.f;
    float thresh = -18.f, slope = 0.75f, knee = 6.f;
    float attCoef = 1.f, relCoef = 1.f;
    float makeup = 0.f, makeupTarget = 0.f, makeupCoef = 0.01f;
    float env = 0.f;
    int source = 0;
    bool first = true;
    std::atomic<float> meter { 0.f };
};

} // namespace

std::unique_ptr<Effect> createCompressorFx() { return std::make_unique<CompressorFx>(); }

} // namespace atm
