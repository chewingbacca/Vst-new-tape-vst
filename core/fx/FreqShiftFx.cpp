// Frequency shifter (Bode style single-sideband modulator).
//
//   in + feedback -> Hilbert pair (two all-pass chains, 90 degrees apart) -> I*cos(th) - Q*sin(th) -> out
//                 ^                                                                             |
//                 +---- gain <- soft limit <- delay (1 ms .. 0.5 s) <---------------------------+
//
// The 90-degree network is the classical elliptic phase-difference design (Darlington 1950,
// Orchard 1950, Weaver 1954): 2 x 8 first-order all-pass sections whose analogue poles are
//     p_r = wl * sc ((4r + 1) K(k') / (2n), k'),  r = 0 .. n-1,  n = 16,  k = wl / wh,
// positive values going to one chain and the negated negative ones to the other. The network is
// derived again for every sample rate in prepare(): the band edges 20 Hz and min (0.4535 sr, 22 kHz)
// are pre-warped (w = tan (pi f / sr)) and the poles mapped with the bilinear transform, which keeps
// the equiripple phase error exactly: 0.08 degrees = 63 dB of unwanted-sideband rejection at
// 44.1/48 kHz, better at higher rates. The Jacobi functions are computed with the AGM method
// (Abramowitz & Stegun 16.4). No code was taken from anywhere.
//
// Shift mapping (bipolar, exponential):  x = 2 * norm - 1,
//     hz = sign (x) * 5000 * (10^(3 |x|) - 1) / 999        (0.5 -> 0 Hz, +-1 -> +-5 kHz)
// The LFO is added to x before the mapping (depth 1 = the whole range). The right channel shifts by
// hz * (1 - 2 * spread): 0 = same direction, 0.5 = right channel unshifted, 1 = mirrored.
#include "Effect.h"
#include "../dsp/Delay.h"
#include "../model/ParamTables.h"
#include <vector>

namespace atm {

namespace {

enum { Shift, Feedback, DelayTime, Spread, LfoRate, LfoDepth };

constexpr int kSections = 8;          // first-order sections per chain
constexpr int kTick = 16;             // internal control interval (independent of the host's slicing)
constexpr int kSinSize = 2048;

// All-pass coefficients of both chains for the band fl..fh at sample rate sr.
// Chain 0 lags chain 1 by 90 degrees inside the band.
void designHilbert (double sr, double fl, double fh, float coefs[2][kSections])
{
    const int n = 2 * kSections;
    const double wl = std::tan (kPi * fl / sr), wh = std::tan (kPi * fh / sr);
    const double k = wl / wh;
    // AGM for the parameter m = k'^2 = 1 - k^2
    double a[40], c[40];
    a[0] = 1.0; c[0] = std::sqrt (1.0 - k * k);
    double b = k;
    int steps = 0;
    while (std::fabs (c[steps]) > 1.0e-16 && steps < 38)
    {
        const double an = 0.5 * (a[steps] + b);
        c[steps + 1] = 0.5 * (a[steps] - b);
        b = std::sqrt (a[steps] * b);
        a[steps + 1] = an;
        ++steps;
    }
    const double K = kPi / (2.0 * a[steps]);
    int count[2] = { 0, 0 };
    for (int r = 0; r < n; ++r)
    {
        const double u = (4.0 * r + 1.0) * K / (2.0 * n);
        double phi = std::ldexp (a[steps] * u, steps);
        for (int i = steps; i > 0; --i) phi = 0.5 * (phi + std::asin (clamp (c[i] * std::sin (phi) / a[i], -1.0, 1.0)));
        const double p = wl * std::tan (phi);             // sc = sn / cn
        const int chain = p > 0.0 ? 0 : 1;
        const double ap = std::fabs (p);
        // (p - s) / (p + s) with s = (1 - z^-1) / (1 + z^-1)  ->  (cf + z^-1) / (1 + cf z^-1)
        if (count[chain] < kSections) coefs[chain][count[chain]++] = (float) ((ap - 1.0) / (ap + 1.0));
    }
    for (int ch = 0; ch < 2; ++ch)                        // cannot happen for a sane band; stay defined anyway
        for (int i = count[ch]; i < kSections; ++i) coefs[ch][i] = 0.f;
}

struct AllpassChain
{
    float x1[kSections] = {}, y1[kSections] = {};
    inline float process (float x, const float* cf)
    {
        for (int i = 0; i < kSections; ++i)
        {
            const float y = cf[i] * (x - y1[i]) + x1[i];
            x1[i] = x; y1[i] = y; x = y;
        }
        return x;
    }
    void reset() { for (int i = 0; i < kSections; ++i) x1[i] = y1[i] = 0.f; }
    void flush() { for (int i = 0; i < kSections; ++i) { x1[i] = undenorm (x1[i]); y1[i] = undenorm (y1[i]); } }
};

class FreqShiftFx final : public Effect
{
public:
    void prepare (double sampleRate) override
    {
        sr = sampleRate; invSr = 1.0 / sr;
        designHilbert (sr, 20.0, std::min (0.4535 * sr, 22000.0), cf);
        for (int i = 0; i <= kSinSize; ++i) sinTab[i] = (float) std::sin (kTau * i / kSinSize);
        for (auto& d : dly) d.prepare ((int) (0.5 * sr) + 8);
        hzCoef  = onePoleCoef (0.008f, (float) sr);
        fbCoef  = onePoleCoef (0.010f, (float) sr);
        dlyCoef = onePoleCoef (0.040f, (float) sr);
        reset();
    }

    void reset() override
    {
        for (auto& ch : chans) { ch.i.reset(); ch.q.reset(); ch.phase = 0.0; ch.hz = 0.f; }
        for (auto& d : dly) d.reset();
        lfoPhase = 0.0; tick = 0; first = true;
        fb = 0.f; hzTarget[0] = hzTarget[1] = 0.f;
    }

    void setParams (const float* p, const FxContext&) override
    {
        x        = kFxFreqShift[Shift].toPhys (p[Shift]);
        fbTarget = kFxFreqShift[Feedback].toPhys (p[Feedback]);
        spread   = kFxFreqShift[Spread].toPhys (p[Spread]);
        lfoInc   = (double) kFxFreqShift[LfoRate].toPhys (p[LfoRate]) / sr;
        lfoDepth = kFxFreqShift[LfoDepth].toPhys (p[LfoDepth]);
        // settle on a whole number of samples: the read is then exact (no interpolation loss in the loop)
        const float d = std::round (kFxFreqShift[DelayTime].toPhys (p[DelayTime]) * (float) sr);
        dlyTarget = clamp (d, 4.f, (float) (0.5 * sr));
        if (first)
        {
            first = false;
            fb = fbTarget; dlySamples = dlyTarget;
            updateTargets();
            chans[0].hz = hzTarget[0]; chans[1].hz = hzTarget[1];
        }
    }

    void process (float* l, float* r, int n, const FxContext&) override
    {
        float* io[2] = { l, r };
        for (int s = 0; s < n; ++s)
        {
            if (tick == 0) updateTargets();
            if (++tick >= kTick) tick = 0;
            lfoPhase += lfoInc; if (lfoPhase >= 1.0) lfoPhase -= 1.0;
            fb += fbCoef * (fbTarget - fb);
            dlySamples += dlyCoef * (dlyTarget - dlySamples);
            if (std::fabs (dlyTarget - dlySamples) < 1.0e-3f) dlySamples = dlyTarget;
            const int di = (int) dlySamples;
            const bool whole = (float) di == dlySamples;

            for (int c = 0; c < 2; ++c)
            {
                Chan& ch = chans[c];
                ch.hz += hzCoef * (hzTarget[c] - ch.hz);
                ch.phase += (double) ch.hz * invSr;
                if (ch.phase >= 1.0) ch.phase -= 1.0; else if (ch.phase < 0.0) ch.phase += 1.0;
                const float ph = (float) ch.phase * (float) kSinSize;
                const float sn = sinLookup (ph), cs = sinLookup (ph + (float) (kSinSize / 4));

                // read before write: read (d - 1) is the output d samples ago
                const float del = whole ? dly[c].read (di - 1) : dly[c].readHermite (dlySamples - 1.f);
                const float in = io[c][s] + fb * 2.f * softClip (0.5f * del);       // loop limited to +-2
                const float q = ch.q.process (in, cf[0]);                           // 90 degrees behind i
                const float i = ch.i.process (in, cf[1]);
                const float y = i * cs - q * sn;
                dly[c].write (undenorm (y));
                io[c][s] = y;
            }
        }
        for (auto& ch : chans) { ch.i.flush(); ch.q.flush(); }
    }

private:
    struct Chan { AllpassChain i, q; double phase = 0.0; float hz = 0.f; };

    static float mapShift (float v)
    {
        const float a = std::fabs (v);
        const float hz = 5000.f * (std::pow (10.f, 3.f * a) - 1.f) / 999.f;
        return v < 0.f ? -hz : hz;
    }

    void updateTargets()
    {
        float v = x;
        if (lfoDepth > 0.f) v = clamp (v + lfoDepth * (float) std::sin (kTau * lfoPhase), -1.f, 1.f);
        const float hz = mapShift (v);
        hzTarget[0] = hz;
        hzTarget[1] = hz * (1.f - 2.f * spread);
    }

    inline float sinLookup (float pos) const       // pos in table steps, 0 .. 1.25 * size
    {
        const int i = (int) pos; const float f = pos - (float) i;
        const int j = i & (kSinSize - 1);
        return sinTab[j] + (sinTab[j + 1] - sinTab[j]) * f;
    }

    double sr = 48000.0, invSr = 1.0 / 48000.0;
    float cf[2][kSections] = {};
    float sinTab[kSinSize + 1] = {};
    Chan chans[2];
    DelayLine dly[2];
    double lfoPhase = 0.0, lfoInc = 0.0;
    int tick = 0;
    bool first = true;
    float x = 0.f, spread = 0.f, lfoDepth = 0.f;
    float hzTarget[2] = { 0.f, 0.f };
    float fb = 0.f, fbTarget = 0.f, fbCoef = 0.01f;
    float dlySamples = 100.f, dlyTarget = 100.f, dlyCoef = 0.001f;
    float hzCoef = 0.01f;
};

} // namespace

std::unique_ptr<Effect> createFreqShiftFx() { return std::make_unique<FreqShiftFx>(); }

} // namespace atm
