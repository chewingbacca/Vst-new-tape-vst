// Splinter DSP — band-limited buffer reader with automatic jump crossfades.
//
// The engine asks for a source coordinate every sample. The Reader follows that
// target exactly (so playback can never drift from the host clock). When the
// target is discontinuous — a slice jump, stutter repeat, pattern switch or a
// host loop — a new tap starts at the new place and the old tap keeps coasting
// at its last speed while the two crossfade.
#pragma once
#include "Common.h"

namespace splinter
{
constexpr int kSincHalf = 8;          // zero crossings per side at unity rate
constexpr int kSincPhases = 256;
constexpr int kSincMaxStretch = 4;    // kernel widens up to this playback rate
constexpr int kSourceGuard = kSincHalf * kSincMaxStretch + 8;
constexpr int kReaderLookahead = kSourceGuard; // samples the reader needs "after" the read point

class SincTable
{
public:
    static const SincTable& get() { static const SincTable t; return t; }

    // Polyphase rows for unity-rate reads: taps n = -(kSincHalf-1) .. kSincHalf
    const float* row (int phase) const { return &poly[(size_t) (phase * taps)]; }
    static constexpr int taps = kSincHalf * 2;

    // Continuous kernel h(t), |t| < kSincHalf, linear interpolated.
    inline float kernel (float t) const
    {
        t = std::fabs (t);
        if (t >= (float) kSincHalf) return 0.0f;
        const float p = t * (float) fineRes;
        const int i = (int) p;
        return fine[(size_t) i] + (p - (float) i) * (fine[(size_t) i + 1] - fine[(size_t) i]);
    }

private:
    static constexpr int fineRes = 512;
    std::vector<float> poly, fine;

    static double bessel0 (double x)
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 32; ++k) { term *= (x / (2.0 * k)) * (x / (2.0 * k)); sum += term; }
        return sum;
    }
    static double h (double t)
    {
        const double a = std::fabs (t);
        if (a >= (double) kSincHalf) return 0.0;
        const double beta = 9.0;
        const double r = a / (double) kSincHalf;
        const double w = bessel0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / bessel0 (beta);
        const double s = a < 1.0e-9 ? 1.0 : std::sin (kPi * a) / (kPi * a);
        return s * w;
    }
    SincTable()
    {
        poly.resize ((size_t) ((kSincPhases + 1) * taps));
        for (int p = 0; p <= kSincPhases; ++p)
        {
            const double frac = (double) p / kSincPhases;
            double sum = 0.0;
            for (int k = 0; k < taps; ++k) sum += h ((double) (k - (kSincHalf - 1)) - frac);
            for (int k = 0; k < taps; ++k) // normalise each phase to unity DC gain
                poly[(size_t) (p * taps + k)] = (float) (h ((double) (k - (kSincHalf - 1)) - frac) / sum);
        }
        fine.resize ((size_t) (kSincHalf * fineRes + 2));
        for (size_t i = 0; i < fine.size(); ++i) fine[i] = (float) h ((double) i / fineRes);
    }
};

// A view of audio the reader can pull from. `ch` point at index 0 of a buffer
// that is valid (and wrapped/mirrored) for [-kSourceGuard, length + kSourceGuard).
struct SourceView
{
    const float* ch[2] = { nullptr, nullptr };
    int64_t length = 0;
    bool bounded = false;       // live ring: only [lo, hi] (unwrapped) is readable
    double lo = 0.0, hi = 0.0;
    bool valid() const { return ch[0] != nullptr && length > 0; }
};

inline void readSource (const SourceView& s, double x, double absRate, float& outL, float& outR)
{
    const double xf = std::floor (x);
    const float frac = (float) (x - xf);
    int64_t i0 = (int64_t) xf % s.length;
    if (i0 < 0) i0 += s.length;
    const float* L = s.ch[0] + i0;
    const float* R = s.ch[1] + i0;
    const auto& table = SincTable::get();
    if (absRate <= 1.02)
    {
        const float pf = frac * (float) kSincPhases;
        const int p = (int) pf;
        const float a = pf - (float) p;
        const float* r0 = table.row (p);
        const float* r1 = r0 + SincTable::taps;
        float l = 0.0f, r = 0.0f;
        for (int k = 0; k < SincTable::taps; ++k)
        {
            const float w = r0[k] + a * (r1[k] - r0[k]);
            const int n = k - (kSincHalf - 1);
            l += w * L[n]; r += w * R[n];
        }
        outL = l; outR = r;
    }
    else
    {
        // widen (low-pass) the kernel for fast playback to avoid aliasing
        const float rho = (float) std::min (absRate, (double) kSincMaxStretch);
        const float inv = 1.0f / rho;
        const int half = (int) std::ceil ((float) kSincHalf * rho);
        float l = 0.0f, r = 0.0f, wsum = 0.0f;
        for (int n = -half + 1; n <= half; ++n)
        {
            const float w = table.kernel (((float) n - frac) * inv);
            l += w * L[n]; r += w * R[n]; wsum += w;
        }
        const float norm = wsum > 1.0e-6f ? 1.0f / wsum : 0.0f;
        outL = l * norm; outR = r * norm;
    }
}

class Reader
{
public:
    static constexpr int kMaxTaps = 6;

    void reset() { for (auto& t : taps) t = Tap(); cur = -1; }
    bool isSilent() const
    {
        if (cur >= 0) return false;
        for (auto& t : taps) if (t.active) return false;
        return true;
    }
    double currentPosition() const { return cur >= 0 ? taps[cur].x : lastPos; }
    double currentRate() const { return cur >= 0 ? taps[cur].dx : 0.0; }

    // Let everything that is sounding fade away; the next process() call starts
    // a new tap and crossfades into it.
    void release (int fadeSamples) { cur = -1; setFade (fadeSamples); }

    // Render one sample reading at `target` (unwrapped source samples).
    // nominalRate is the expected playback speed, used when a tap starts.
    // Starting from silence is instantaneous: fade-ins belong to the caller.
    inline void process (const SourceView& src, double target, double nominalRate, int fadeSamples, float& outL, float& outR)
    {
        outL = outR = 0.0f;
        if (! src.valid()) return;
        if (src.bounded) target = clampv (target, src.lo, src.hi);

        if (cur < 0)
            startTap (target, nominalRate, fadeSamples);
        else
        {
            Tap& c = taps[cur];
            const double delta = target - c.x;
            const double err = std::fabs (delta - c.dx);
            if (c.fresh && std::fabs (delta) <= 2.0 * kSincMaxStretch)
            {
                c.fresh = false; c.dx = delta; c.x = target;   // first step tells us the real speed
            }
            // a jump in position *or* an abrupt change of speed (which would be a
            // kink in the waveform) is bridged by a crossfade
            else if (err > 0.05 * std::max (1.0, std::fabs (c.dx)))
            {
                c.dx = clampv (c.dx, -(double) kSincMaxStretch, (double) kSincMaxStretch);
                startTap (target, nominalRate, fadeSamples);
            }
            else { c.dx = delta; c.x = target; }
        }
        lastPos = target;

        // Every tap has its own gain: a constant-rate ramp towards 1 (newest tap)
        // or 0 (the others), rounded by a one-pole so reversing a fade half way is
        // still smooth. The gains are then normalised to sum to one, so a burst of
        // jumps closer together than the fade time neither dips nor bumps.
        float sum = 0.0f;
        for (int i = 0; i < kMaxTaps; ++i)
        {
            Tap& t = taps[i];
            if (! t.active) continue;
            if (i == cur) t.lin = std::min (1.0f, t.lin + step);
            else
            {
                t.lin = std::max (0.0f, t.lin - step);
                t.x += t.dx;
                if (src.bounded)
                {
                    if (t.x > src.hi) { t.x = src.hi; t.dx = 0.0; }
                    if (t.x < src.lo) { t.x = src.lo; t.dx = 0.0; }
                }
            }
            t.y += pole * (t.lin - t.y);
            if (i != cur && t.lin <= 0.0f && t.y < 1.0e-5f) { t.active = false; continue; }
            sum += t.y;
        }
        if (sum <= 1.0e-9f) return;
        const float norm = cur >= 0 ? 1.0f / sum : 1.0f;
        for (int i = 0; i < kMaxTaps; ++i)
        {
            const Tap& t = taps[i];
            if (! t.active) continue;
            float l, r;
            readSource (src, t.x, std::fabs (t.dx), l, r);
            const float g = t.y * norm;
            outL += g * l; outR += g * r;
        }
    }

private:
    struct Tap
    {
        double x = 0.0, dx = 1.0;
        float lin = 0.0f, y = 0.0f;
        bool active = false, fresh = false;
    };
    Tap taps[kMaxTaps];
    int cur = -1;
    float step = 1.0f / 192.0f, pole = 0.1f;
    int fadeSet = 0;
    double lastPos = 0.0;

    void setFade (int fadeSamples)
    {
        if (fadeSamples == fadeSet) return;
        fadeSet = fadeSamples;
        const float n = (float) std::max (4, fadeSamples);
        step = 1.0f / (0.8f * n);
        pole = 1.0f - std::exp (-1.0f / (0.07f * n));
    }

    void startTap (double target, double rate, int fadeSamples)
    {
        setFade (fadeSamples);
        int slot = -1; bool anyActive = false;
        float quietest = 3.0f;
        for (int i = 0; i < kMaxTaps; ++i)
        {
            if (! taps[i].active) { if (slot < 0 || taps[slot].active) slot = i; continue; }
            anyActive = true;
            if ((slot < 0 || taps[slot].active) && taps[i].y < quietest) { quietest = taps[i].y; slot = i; }
        }
        Tap& t = taps[slot];
        t.active = true;
        t.lin = t.y = anyActive ? 0.0f : 1.0f;     // from silence: open immediately
        t.x = target; t.dx = rate; t.fresh = true;
        cur = slot;
    }
};

// ---------------------------------------------------------------- sources
// Rolling capture of the plugin input. Allocated once; written every sample.
class LiveRing
{
public:
    void prepare (int capacitySamples)
    {
        length = std::max (1024, capacitySamples);
        for (auto& d : data) d.assign ((size_t) (length + 2 * kSourceGuard), 0.0f);
        writeAbs = 0;
    }
    void clear() { for (auto& d : data) std::fill (d.begin(), d.end(), 0.0f); }

    inline void write (float l, float r)
    {
        const int64_t wa = writeAbs.load (std::memory_order_relaxed);
        const int64_t i = wa % length;
        put (0, i, l); put (1, i, r);
        writeAbs.store (wa + 1, std::memory_order_relaxed);
    }
    int64_t written() const { return writeAbs.load (std::memory_order_relaxed); }   // abs index of the next sample to write
    int64_t capacity() const { return length; }

    SourceView view() const
    {
        SourceView v;
        v.ch[0] = data[0].data() + kSourceGuard; v.ch[1] = data[1].data() + kSourceGuard;
        v.length = length; v.bounded = true;
        const int64_t wa = writeAbs.load (std::memory_order_relaxed);
        v.hi = (double) (wa - 1 - kReaderLookahead);
        v.lo = (double) (wa - length + kSourceGuard + 4);
        return v;
    }
    float sampleAbs (int ch, int64_t absIndex) const
    {
        int64_t i = absIndex % length; if (i < 0) i += length;
        return data[(size_t) ch][(size_t) (i + kSourceGuard)];
    }

private:
    std::vector<float> data[2];
    int64_t length = 0;
    std::atomic<int64_t> writeAbs { 0 };
    inline void put (int ch, int64_t i, float v)
    {
        float* d = data[ch].data() + kSourceGuard;
        d[i] = v;
        if (i < kSourceGuard) d[i + length] = v;
        if (i >= length - kSourceGuard) d[i - length] = v;
    }
};

// A loop rendered at a given tempo and sample rate (built off the audio thread).
struct RenderedLoop
{
    std::vector<float> data[2];
    int64_t length = 0;          // samples in one loop cycle
    double beats = 4.0;          // musical length
    double sampleRate = 48000.0;
    double bpm = 120.0;          // tempo this render is exact for
    uint32_t generation = 0;     // identifies the source material
    bool pitchPreserved = true;

    void allocate (int64_t len)
    {
        length = std::max<int64_t> (1, len);
        for (auto& d : data) d.assign ((size_t) (length + 2 * kSourceGuard), 0.0f);
    }
    float* channel (int ch) { return data[ch].data() + kSourceGuard; }
    const float* channel (int ch) const { return data[ch].data() + kSourceGuard; }
    double samplesPerBeat() const { return (double) length / beats; }
    void fixGuards()
    {
        for (auto& d : data)
        {
            float* p = d.data() + kSourceGuard;
            for (int i = 0; i < kSourceGuard; ++i)
            {
                p[length + i] = p[i % length];
                p[-1 - i] = p[(length - 1 - (i % length))];
            }
        }
    }
    SourceView view() const
    {
        SourceView v;
        v.ch[0] = channel (0); v.ch[1] = channel (1); v.length = length; v.bounded = false;
        return v;
    }
};
} // namespace splinter
