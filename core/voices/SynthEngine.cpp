// Synth engine: the oscillator section of an analog-style / wavetable mono
// synth voice (the host voice adds filter, drive and envelopes).
//
//   up to 7 unison voices, each:  osc 1 (+ phase modulation from osc 2's sine)
//                                 osc 2 (pitch offset, detune, hard sync to its own base pitch)
//   + sub oscillator (one octave down, rounded square) + white noise
//   -> per-voice / per-oscillator equal-power pan -> DC blocker -> out
//
// Analog mode. One waveform family covers the whole Wave control:
//   V(ph; w) = a trapezoid that rises for a fraction w of the cycle and falls for the rest.
//   w = 0.5 is a triangle; at w = 1 - (one sample) it is a saw whose flyback lasts one sample.
//   Past the saw a second, phase-shifted saw is subtracted: V(ph) - a V(ph - width), which is
//   the classic saw -> pulse morph (a = 1: square / pulse of that width).
//   The waveform is continuous, so only its corners need correcting: a two-sample polyBLAMP
//   residual per corner (which makes the result the waveform convolved with a triangular
//   kernel: sinc^3 roll-off for the saw edge, better than plain polyBLEP). Hard sync adds a
//   step at the reset, corrected with a polyBLEP of the actual step height plus a BLAMP for the
//   slope change. Corrections reach one sample back, so the engine runs with one sample of
//   latency. Phase modulation may run the phase backwards (through zero); corners are found
//   in both directions.
//
// Wavetable modes. Five sets of 16 single-cycle frames are built additively / by FFT once
// (shared by all instances) with one band-limited mip level per octave. Playback interpolates
// linearly inside a table, between the two neighbouring frames and between two mip levels;
// the mip pair is chosen from the highest instantaneous frequency (pitch, unison detune, phase
// warp, FM), so nothing above the fold-over point is ever read.
//
// Every parameter is smoothed on a fixed internal grid counted from note-on, so the output
// does not depend on how the host slices its buffers.
//
// Original work. PolyBLEP/polyBLAMP residuals follow V. Valimaki et al., "Alias-Suppressed
// Oscillators Based on Differentiated Polynomial Waveforms" (IEEE TASLP 2010) and
// F. Esqueda et al., "Rounding Corners with BLAMP" (DAFx-16). Mip-mapped wavetables are the
// standard technique.
#include "EngineBase.h"
#include "../model/ParamTables.h"
#include "../dsp/Filters.h"
#include "../dsp/Fft.h"
#include <complex>
#include <vector>

namespace atm {

namespace {

enum P { Wave1, Mode, Wave2, Pitch2, Detune, Mix, Sub, NoiseLv, PW, Sync, FM, Unison, UniDet, Drift, Width, PhaseMode };

constexpr int   kMaxUni  = 7;
constexpr int   kUniCounts[5] = { 1, 2, 3, 5, 7 };
constexpr float kOutGain = 0.68f;                 // default patch peaks at about -3 dBFS
constexpr float kMaxFmCycles = 1.2f;              // phase-modulation depth at FM = 1 (7.5 rad)

// Unison layout, outermost voice = +-1. Voice 0 is the centre for odd counts.
struct UniLayout { float det[kMaxUni]; float pan[kMaxUni]; float spread; };
constexpr UniLayout kUniLayout[5] = {
    { { 0.f }, { 0.f }, 0.f },
    { { -1.f, 1.f }, { -1.f, 1.f }, 0.5f },
    { { 0.f, -1.f, 0.96f }, { 0.f, -1.f, 1.f }, 0.8f },
    { { 0.f, -0.44f, 0.48f, -1.f, 0.97f }, { 0.f, 0.5f, -0.5f, -1.f, 1.f }, 1.f },
    { { 0.f, -0.25f, 0.29f, -0.6f, 0.65f, -1.f, 0.97f }, { 0.f, 0.35f, -0.35f, -0.7f, 0.7f, 1.f, -1.f }, 1.f },
};
// Start phases for Phase = Reset: the same on every note, but not all equal, so a unison stack
// does not start with one huge in-phase peak.
constexpr float kResetPhase[kMaxUni] = { 0.f, 0.37f, 0.71f, 0.19f, 0.53f, 0.83f, 0.29f };

// ---------------------------------------------------------------------------- sine table
constexpr int kSineSize = 2048;
struct SineTable
{
    float v[kSineSize + 2];
    SineTable() { for (int i = 0; i < kSineSize + 2; ++i) v[i] = (float) std::sin (kTau * (double) i / (double) kSineSize); }
};
const float* sineTable() { static const SineTable t; return t.v; }
inline float sinTab (const float* tab, float ph01)      // ph01 in [0, 1]
{
    const float x = ph01 * (float) kSineSize;
    const int i = (int) x;
    return tab[i] + (x - (float) i) * (tab[i + 1] - tab[i]);
}
inline float floorF (float x) { const float t = (float) (int) x; return t > x ? t - 1.f : t; }

// ---------------------------------------------------------------------------- wavetables
constexpr int kWtSets = 5, kWtFrames = 16, kWtLevels = 10, kWtHarm = 512;
// level m holds harmonics 1 .. (512 >> m)
constexpr int kWtSize[kWtLevels] = { 4096, 2048, 2048, 1024, 512, 256, 128, 64, 32, 32 };

class WtBank
{
public:
    WtBank() { build(); }
    const float* level (int set, int lvl) const { return data.data() + offset[set][lvl]; }

private:
    using Spec = std::vector<std::complex<double>>;     // index = harmonic; value a: wave = Re sum a_h e^(j 2 pi h t)

    static void setSin (Spec& s, int h, double amp) { if (h >= 1 && h <= kWtHarm) s[(size_t) h] += std::complex<double> (0.0, -amp); }

    // --- set 0: sine > triangle > saw > square > narrow pulse
    static void basicFrame (int k, Spec& s)
    {
        auto key = [] (int which, int h) -> std::complex<double>
        {
            const double pi = kPi;
            switch (which)
            {
                case 0: return h == 1 ? std::complex<double> (0, -1) : 0.0;                                               // sine
                case 1: return (h & 1) ? std::complex<double> (0, -(8.0 / (pi * pi)) * (((h - 1) / 2) & 1 ? -1.0 : 1.0) / ((double) h * h)) : 0.0;   // triangle
                case 2: return std::complex<double> (0, -(2.0 / pi) * ((h & 1) ? 1.0 : -1.0) / h);                        // saw
                default: return (h & 1) ? std::complex<double> (0, -(4.0 / pi) / h) : 0.0;                                // square
            }
        };
        if (k <= 12)
        {
            const int seg = std::min (k / 4, 2); const double t = (k - seg * 4) / 4.0;
            for (int h = 1; h <= kWtHarm; ++h) s[(size_t) h] = key (seg, h) * (1.0 - t) + key (seg + 1, h) * t;
        }
        else
        {
            const double d = 0.5 * std::pow (0.2, (k - 12) / 3.0);          // duty 0.5 -> 0.1
            for (int h = 1; h <= kWtHarm; ++h)
            {
                const double c = (2.0 / (kPi * h)) * std::sin (kTau * h * d), sn = (2.0 / (kPi * h)) * (1.0 - std::cos (kTau * h * d));
                s[(size_t) h] = std::complex<double> (c, -sn);
            }
        }
    }

    // --- set 1: a saw-like source through five formant resonators, sweeping u > o > a > e > i
    static void vowelFrame (int k, Spec& s)
    {
        static const double vowels[5][3] = { { 300, 870, 2240 }, { 480, 800, 2400 }, { 760, 1150, 2500 }, { 520, 1850, 2550 }, { 280, 2300, 3050 } };
        static const double bw[5] = { 80, 100, 130, 180, 250 };
        const double p = k * 4.0 / 15.0; const int i0 = std::min ((int) p, 3); const double t = p - i0;
        double F[5];
        for (int j = 0; j < 3; ++j) F[j] = vowels[i0][j] * std::pow (vowels[i0 + 1][j] / vowels[i0][j], t);
        F[3] = 3500.0; F[4] = 4600.0;
        const double f0 = 110.0;                                             // formants sit at these frequencies when the note is A2
        for (int h = 1; h <= kWtHarm; ++h)
        {
            const double f = f0 * h;
            std::complex<double> a (0.0, -1.0 / h);
            for (int j = 0; j < 5; ++j)
            {
                const double r = f / F[j], q = F[j] / bw[j];
                a /= std::complex<double> (1.0 - r * r, r / q);
            }
            s[(size_t) h] = a;
        }
    }

    // --- set 4: drawbar organ, from a single flute to full organ with mixtures
    static void organFrame (int k, Spec& s)
    {
        struct Bar { int h; double from, to, level; };
        static const Bar bars[] = { { 1, -1, -0.5, 1.0 }, { 2, 0.04, 0.3, 0.75 }, { 3, 0.14, 0.42, 0.6 }, { 4, 0.28, 0.55, 0.55 }, { 6, 0.4, 0.68, 0.45 },
                                    { 8, 0.5, 0.8, 0.42 }, { 5, 0.6, 0.88, 0.3 }, { 10, 0.7, 1.0, 0.28 }, { 12, 0.76, 1.0, 0.26 }, { 16, 0.82, 1.0, 0.22 } };
        const double p = k / 15.0;
        int i = 0;
        for (const Bar& b : bars)
        {
            const double g = clamp ((p - b.from) / (b.to - b.from), 0.0, 1.0) * b.level;
            const double phase = kPi * 0.37 * i * i;                         // spread phases: lower crest factor
            s[(size_t) b.h] += std::polar (g, phase - kPi * 0.5);
            ++i;
        }
    }

    // --- sets 2 and 3 are defined as waveforms
    static double metalWave (int k, double t)
    {
        // phase modulation with two high-ratio modulators: sparse clusters of upper partials
        const double p = k / 15.0;
        const double a = 3.2 * std::pow (p, 1.2), b = 1.6 * p * p;
        return std::sin (kTau * t + a * std::sin (kTau * 7.0 * t) + b * std::sin (kTau * 4.0 * t));
    }
    static double digitalWave (int k, double t)
    {
        // a squared-off oscillator hard-synced to the cycle and faded out along it: a formant sweep
        const double r = std::pow (12.0, k / 15.0);
        const double sl = std::tanh (2.2 * std::sin (kTau * r * t)) / std::tanh (2.2);
        return sl * std::pow (1.0 - t, 1.5);
    }

    void build()
    {
        size_t total = 0;
        for (int set = 0; set < kWtSets; ++set)
            for (int l = 0; l < kWtLevels; ++l) { offset[set][l] = (int) total; total += (size_t) kWtFrames * (size_t) (kWtSize[l] + 1); }
        data.assign (total, 0.f);

        const int big = 16384;
        Fft bigFft; bigFft.prepare (big);
        std::vector<float> re ((size_t) big), im ((size_t) big);
        Fft fft[kWtLevels];
        for (int l = 0; l < kWtLevels; ++l) fft[l].prepare (kWtSize[l]);

        for (int set = 0; set < kWtSets; ++set)
            for (int k = 0; k < kWtFrames; ++k)
            {
                Spec s ((size_t) kWtHarm + 1, 0.0);
                switch (set)
                {
                    case 0: basicFrame (k, s); break;
                    case 1: vowelFrame (k, s); break;
                    case 4: organFrame (k, s); break;
                    default:
                    {
                        for (int i = 0; i < big; ++i)
                        {
                            const double t = (double) i / big;
                            re[(size_t) i] = (float) (set == 2 ? metalWave (k, t) : digitalWave (k, t)); im[(size_t) i] = 0.f;
                        }
                        bigFft.forward (re.data(), im.data());
                        for (int h = 1; h <= kWtHarm; ++h) s[(size_t) h] = std::complex<double> (re[(size_t) h], im[(size_t) h]) * (2.0 / big);
                        break;
                    }
                }
                // full-band table first, to measure the frame
                float scale = 1.f;
                for (int l = 0; l < kWtLevels; ++l)
                {
                    const int n = kWtSize[l], maxH = kWtHarm >> l;
                    for (int i = 0; i < n; ++i) { re[(size_t) i] = 0.f; im[(size_t) i] = 0.f; }
                    for (int h = 1; h <= maxH; ++h)
                    {
                        // inverse() divides by n; a_h and its conjugate at the mirror bin give Re(a_h e^(jwt))
                        re[(size_t) h] = (float) (s[(size_t) h].real() * 0.5 * n);       im[(size_t) h] = (float) (s[(size_t) h].imag() * 0.5 * n);
                        re[(size_t) (n - h)] = (float) (s[(size_t) h].real() * 0.5 * n); im[(size_t) (n - h)] = (float) (-s[(size_t) h].imag() * 0.5 * n);
                    }
                    fft[l].inverse (re.data(), im.data());
                    if (l == 0)
                    {
                        // level: halfway between peak- and RMS-normalised, with a ceiling on the peak
                        double peak = 0.0, sq = 0.0;
                        for (int i = 0; i < n; ++i) { peak = std::max (peak, (double) std::fabs (re[(size_t) i])); sq += (double) re[(size_t) i] * re[(size_t) i]; }
                        const double rms = std::sqrt (sq / n);
                        double g = 1.0 / std::sqrt (std::max (1.0e-9, peak * rms * 1.75));
                        g = std::min (g, 1.35 / std::max (peak, 1.0e-9));
                        scale = (float) g;
                    }
                    float* dst = data.data() + offset[set][l] + (size_t) k * (size_t) (n + 1);
                    for (int i = 0; i < n; ++i) dst[i] = re[(size_t) i] * scale;
                    dst[n] = dst[0];
                }
            }
    }

    std::vector<float> data;
    int offset[kWtSets][kWtLevels] = {};
};
const WtBank& wtBank() { static const WtBank b; return b; }

// What an oscillator needs to read its wavetable set for the current grid interval.
struct WtView
{
    const float* base[2] = { nullptr, nullptr };    // two neighbouring mip levels
    float size[2] = { 32.f, 32.f };
    int   stride[2] = { 33, 33 };
    float mix = 0.f;                                // weight of base[1]
};
inline float wtRead (const WtView& v, float ph, float pos)
{
    int j = (int) pos; if (j > kWtFrames - 2) j = kWtFrames - 2;
    const float ff = pos - (float) j;
    float o[2];
    for (int l = 0; l < 2; ++l)
    {
        const float x = ph * v.size[l];
        const int i = (int) x; const float fr = x - (float) i;
        const float* a = v.base[l] + j * v.stride[l] + i;
        const float* b = a + v.stride[l];
        const float sa = a[0] + fr * (a[1] - a[0]), sb = b[0] + fr * (b[1] - b[0]);
        o[l] = sa + ff * (sb - sa);
    }
    return o[0] + v.mix * (o[1] - o[0]);
}

// ---------------------------------------------------------------------------- band-limiting residuals
// ts = time since the event in samples (0..1). cur corrects the current sample, prev the one before.
inline void blampAdd (float ts, float slopeStep, float& cur, float& prev)
{
    ts = clamp (ts, 0.f, 1.f);
    const float u = 1.f - ts;
    cur += slopeStep * u * u * u * (1.f / 6.f);
    prev += slopeStep * ts * ts * ts * (1.f / 6.f);
}
inline void blepAdd (float ts, float step, float& cur, float& prev)
{
    ts = clamp (ts, 0.f, 1.f);
    const float u = 1.f - ts;
    cur -= step * u * u * 0.5f;
    prev += step * ts * ts * 0.5f;
}

// The analog waveform for one sample: trapezoid parameters and the pulse part.
struct Shape { float w, rw, rf, a, d; };

inline float trapezoid (float ph, const Shape& s) { return ph < s.w ? 2.f * ph * s.rw - 1.f : 2.f * (1.f - ph) * s.rf - 1.f; }
inline float trapSlope (float ph, const Shape& s) { return ph < s.w ? 2.f * s.rw : -2.f * s.rf; }
inline float shapeValue (float ph, const Shape& s)
{
    float v = trapezoid (ph, s);
    if (s.a != 0.f) { float q = ph - s.d; if (q < 0.f) q += 1.f; v -= s.a * trapezoid (q, s); }
    return v;
}
inline float shapeSlope (float ph, const Shape& s)
{
    float v = trapSlope (ph, s);
    if (s.a != 0.f) { float q = ph - s.d; if (q < 0.f) q += 1.f; v -= s.a * trapSlope (q, s); }
    return v;
}

// Advance a phase forwards by dphi (0 <= dphi < 1) at `inc` per sample, the move ending tEnd samples
// before the current sample, and collect the corner corrections on the way.
inline void advanceShape (float& ph, float dphi, float inc, float tEnd, const Shape& s, float& cur, float& prev)
{
    const float p0 = ph;
    float p1 = p0 + dphi;
    const bool wrapped = p1 >= 1.f;
    if (wrapped) p1 -= 1.f;
    ph = p1;
    const float k = 2.f * (s.rw + s.rf) * inc;                       // slope change per sample at a corner
    auto corner = [&] (float c, float step)
    {
        const bool hit = wrapped ? (c > p0 || c <= p1) : (c > p0 && c <= p1);
        if (hit)
        {
            float d = p1 - c; if (d < 0.f) d += 1.f;
            blampAdd (tEnd + d / inc, step, cur, prev);
        }
    };
    corner (0.f, k);
    corner (s.w, -k);
    if (s.a != 0.f)
    {
        corner (s.d, -s.a * k);
        float c2 = s.d + s.w; if (c2 >= 1.f) c2 -= 1.f;
        corner (c2, s.a * k);
    }
}

// The same for a phase that moved from p0 to p1 (unwrapped, either direction, |p1 - p0| <= 0.5).
inline void cornersBothWays (float p0, float p1, const Shape& s, float& cur, float& prev)
{
    const float delta = p1 - p0, ad = std::fabs (delta);
    if (ad < 1.0e-9f) return;
    const float k = 2.f * (s.rw + s.rf) * ad;
    auto corner = [&] (float c, float step)
    {
        const float a0 = p0 - c, a1 = p1 - c;
        const float f0 = floorF (a0), f1 = floorF (a1);
        if (f1 > f0) blampAdd ((a1 - f1) / ad, step, cur, prev);
        else if (f1 < f0) blampAdd ((f0 - a1) / ad, step, cur, prev);
    };
    corner (0.f, k);
    corner (s.w, -k);
    if (s.a != 0.f)
    {
        corner (s.d, -s.a * k);
        corner (s.d + s.w, s.a * k);
    }
}

struct Ramp
{
    float v = 0.f, step = 0.f, next = 0.f;
    void snapTo (float x) { v = next = x; step = 0.f; }
    void land() { v = next; }
    void aim (float target, float invK) { next = target; step = (next - v) * invK; }
    void advance (int n) { for (int i = 0; i < n; ++i) v += step; }
};

} // namespace

class SynthEngine final : public EngineBase
{
public:
    void prepare (double sampleRate) override
    {
        sr = (float) sampleRate;
        sine = sineTable();
        bank = &wtBank();
        grid = std::max (1, (int) std::lround (sampleRate / 3000.0));
        invGrid = 1.f / (float) grid;
        gridDt = (float) grid / sr;
        smoothCoef = 1.f - std::exp (-(float) grid / (0.002f * sr));
        fadeInc = 1.f / std::max (2.f, 0.0007f * sr);
        wtAllowed = std::max (0.5f * sr, sr - 20000.f);
        dcL.prepare (sr, 3.f); dcR.prepare (sr, 3.f);
        for (auto& v : lastE) v = -1.f;
        reset();
    }

    void reset() override
    {
        active = false; stereo = false; snap = true; gridLeft = 0; fade = 0.f;
        carryL = carryR = 0.f;
        dcL.reset(); dcR.reset();
        for (auto& u : uni) u = UVoice{};
        subPh = 0.f;
    }

    void noteOn (const EngineNote& n, const float* e) override
    {
        const bool fresh = ! n.legato || ! active;
        if (fresh)
        {
            const ParamDesc* d = kSynthParams;
            const bool resetPhase = d[PhaseMode].toIndex (e[PhaseMode]) == 1;
            const int count = kUniCounts[clamp (d[Unison].toIndex (e[Unison]), 0, 4)];
            const float mixN = clamp01 (e[Mix]);
            stereo = clamp01 (e[Width]) > 0.005f && (count > 1 || (mixN > 0.001f && mixN < 0.999f) || clamp01 (e[NoiseLv]) > 0.001f);
            for (int v = 0; v < kMaxUni; ++v)
            {
                UVoice& u = uni[v];
                u = UVoice{};
                if (resetPhase) { u.ph1 = kResetPhase[v]; u.ph2 = kResetPhase[v]; }
                else
                {
                    // "free running": every note starts somewhere else in the cycle, repeatably
                    u.ph1 = hashToFloat (hashN (n.seed, (uint32_t) v, 0x51u));
                    u.ph2 = hashToFloat (hashN (n.seed, (uint32_t) v, 0x52u));
                }
                u.phM = u.ph2;
                for (int j = 0; j < 3; ++j)
                {
                    static constexpr float lo[3] = { 0.07f, 0.37f, 0.13f }, hi[3] = { 0.23f, 1.1f, 0.55f };
                    u.dPh[j] = hashToFloat (hashN (n.seed, (uint32_t) v, 0x60u + (uint32_t) j));
                    u.dInc[j] = lerp (lo[j], hi[j], hashToFloat (hashN (n.seed, (uint32_t) v, 0x70u + (uint32_t) j))) * gridDt;
                }
            }
            subPh = resetPhase ? 0.f : hashToFloat (hashN (n.seed, 0x99u));
            wowPh = hashToFloat (hashN (n.seed, 0x9au)); flutPh = hashToFloat (hashN (n.seed, 0x9bu)); flut2Ph = hashToFloat (hashN (n.seed, 0x9cu));
            noiseA.seed (n.seed * 2u + 11u); noiseB.seed (n.seed * 2u + 4241u);
            carryL = carryR = 0.f;
            dcL.reset(); dcR.reset();
            fade = 0.f; snap = true; gridLeft = 0;
            active = true;
        }
        control (e, n.note);
    }

    void control (const float* e, float note) override
    {
        const ParamDesc* d = kSynthParams;
        noteCur = note;
        mode = clamp (d[Mode].toIndex (e[Mode]), 0, kWtSets);
        uniIdx = clamp (d[Unison].toIndex (e[Unison]), 0, 4);
        wave[0] = clamp01 (e[Wave1]); wave[1] = clamp01 (e[Wave2]);
        pw = clamp01 (e[PW]);
        if (e[Pitch2] != lastE[Pitch2] || e[Detune] != lastE[Detune])
        {
            lastE[Pitch2] = e[Pitch2]; lastE[Detune] = e[Detune];
            ratio2 = std::exp2 ((d[Pitch2].toPhys (e[Pitch2]) + d[Detune].toPhys (e[Detune]) * 0.01f) * (1.f / 12.f));
        }
        if (e[Sync] != lastE[Sync]) { lastE[Sync] = e[Sync]; syncRatio = d[Sync].toPhys (e[Sync]); if (syncRatio < 1.004f) syncRatio = 1.f; }
        mix = clamp01 (e[Mix]);
        subLv = clamp01 (e[Sub]); noiseLv = clamp01 (e[NoiseLv]);
        fmAmt = d[FM].toPhys (e[FM]) * kMaxFmCycles;
        uniDet = d[UniDet].toPhys (e[UniDet]);
        drift = clamp01 (e[Drift]);
        width = clamp01 (e[Width]);
    }

    void render (float* outL, float* outR, int n) override
    {
        if (! active)
        {
            for (int i = 0; i < n; ++i) { outL[i] = 0.f; if (stereo) outR[i] = 0.f; }
            return;
        }
        int done = 0;
        while (done < n)
        {
            if (gridLeft == 0) { gridUpdate(); gridLeft = grid; }
            const int seg = std::min (n - done, gridLeft);

            accL[0] = carryL; accR[0] = carryR;
            for (int i = 1; i <= seg; ++i) accL[i] = accR[i] = 0.f;
            // corrections that reach back one sample are summed separately, so the order of the
            // additions (and with it the rounding) is the same however the buffer is sliced
            for (int i = 0; i < seg; ++i) preL[i] = preR[i] = 0.f;

            for (int v = 0; v < kMaxUni; ++v)
                if (uni[v].running) renderVoice (uni[v], seg);
            renderSubNoise (seg);

            // shared ramps move on, one add per sample so the result does not depend on the slicing
            for (auto* r : { &aR[0], &aR[1], &dR, &posR[0], &posR[1], &warpR, &fmR, &syncR, &sgR[0], &sgR[1], &subR, &noiseR }) r->advance (seg);

            for (int i = 0; i < seg; ++i)
            {
                float f = fade;
                if (fade < 1.f) { fade = std::min (1.f, fade + fadeInc); f = f * f * (3.f - 2.f * f); }
                outL[done + i] = dcL.process ((accL[i] + preL[i]) * f * kOutGain);
                if (stereo) outR[done + i] = dcR.process ((accR[i] + preR[i]) * f * kOutGain);
            }
            carryL = accL[seg]; carryR = accR[seg];
            gridLeft -= seg; done += seg;
        }
    }

    bool isStereo() const override { return stereo; }

private:
    struct UVoice
    {
        bool  running = false;
        float ph1 = 0.f, ph2 = 0.f, phM = 0.f, modPrev = 0.f;
        Ramp  inc1, inc2;                       // osc 1; osc 2 base (= sync master)
        Ramp  g[4];                             // osc1 L, osc1 R, osc2 L, osc2 R
        Ramp  w[2], rw[2], rf[2];               // trapezoid of osc 1 / osc 2
        float dPh[3] = {}, dInc[3] = {};        // drift LFOs (phase 0..1, increment per grid step)
    };

    // ------------------------------------------------------------------ per-sample work
    void renderVoice (UVoice& u, int seg)
    {
        const bool wt = mode > 0;
        const bool fmOn = fmR.v != 0.f || fmR.next != 0.f || u.modPrev != 0.f;
        const bool osc2On = fmOn || u.g[2].v != 0.f || u.g[2].next != 0.f || u.g[3].v != 0.f || u.g[3].next != 0.f;
        const bool syncOn = syncR.v > 1.f || syncR.next > 1.f;
        const bool warpOn = warpR.v != 0.f || warpR.next != 0.f;
        const float* const tab = sine;
        const WtView& v1 = view[0]; const WtView& v2 = view[1];

        float ph1 = u.ph1, ph2 = u.ph2, phM = u.phM, modPrev = u.modPrev;
        float inc1 = u.inc1.v, inc2 = u.inc2.v;
        float g1L = u.g[0].v, g1R = u.g[1].v, g2L = u.g[2].v, g2R = u.g[3].v;
        Shape s1 { u.w[0].v, u.rw[0].v, u.rf[0].v, aR[0].v, dR.v };
        Shape s2 { u.w[1].v, u.rw[1].v, u.rf[1].v, aR[1].v, dR.v };
        float pos1 = posR[0].v, pos2 = posR[1].v, warp = warpR.v, fmI = fmR.v, syncMul = syncR.v, sg1 = sgR[0].v, sg2 = sgR[1].v;

        auto wtWarped = [tab, warpOn] (const WtView& vw, float ph, float pos, float b) -> float
        {
            if (warpOn) ph = clamp (ph - b * sinTab (tab, ph), 0.f, 0.99999994f);
            return wtRead (vw, ph, pos);
        };

        for (int i = 0; i < seg; ++i)
        {
            float o1, o2 = 0.f, c1 = 0.f, p1 = 0.f, c2 = 0.f, p2 = 0.f, sinM = 0.f;

            // ---- osc 2 (and the modulator sine, which follows osc 2's base pitch)
            if (osc2On)
            {
                if (syncOn)
                {
                    const float incS = std::min (inc2 * syncMul, 0.45f);
                    phM += inc2;
                    if (phM >= 1.f)
                    {
                        phM -= 1.f;
                        const float t = clamp (phM / inc2, 0.f, 1.f);          // samples since the master wrapped
                        if (wt)
                        {
                            float pb = ph2 + incS * (1.f - t); if (pb >= 1.f) pb -= 1.f;
                            blepAdd (t, wtWarped (v2, 0.f, pos2, warp) - wtWarped (v2, std::min (pb, 0.99999994f), pos2, warp), c2, p2);
                            ph2 = incS * t;
                        }
                        else
                        {
                            advanceShape (ph2, incS * (1.f - t), incS, t, s2, c2, p2);
                            blepAdd (t, shapeValue (0.f, s2) - shapeValue (ph2, s2), c2, p2);
                            blampAdd (t, (shapeSlope (0.f, s2) - shapeSlope (ph2, s2)) * incS, c2, p2);
                            ph2 = 0.f;
                            advanceShape (ph2, incS * t, incS, 0.f, s2, c2, p2);
                        }
                    }
                    else if (wt) { ph2 += incS; if (ph2 >= 1.f) ph2 -= 1.f; }
                    else advanceShape (ph2, incS, incS, 0.f, s2, c2, p2);
                    if (fmOn) sinM = sinTab (tab, phM);
                }
                else
                {
                    if (wt) { ph2 += inc2; if (ph2 >= 1.f) ph2 -= 1.f; }
                    else advanceShape (ph2, inc2, inc2, 0.f, s2, c2, p2);
                    phM = ph2;
                    if (fmOn) sinM = sinTab (tab, ph2);
                }
                o2 = wt ? wtWarped (v2, ph2, pos2, warp) : shapeValue (ph2, s2);
            }

            // ---- osc 1
            if (fmOn)
            {
                float mod = fmI * sinM;
                const float p0 = ph1 + modPrev;
                float delta = inc1 + (mod - modPrev);
                if (! wt && std::fabs (delta) > 0.5f)                          // slew limit: keeps the corner search valid
                {
                    delta = delta > 0.f ? 0.5f : -0.5f;
                    mod = modPrev + delta - inc1;
                }
                ph1 += inc1;
                const float pu = ph1 + mod;
                if (ph1 >= 1.f) ph1 -= 1.f;
                modPrev = mod;
                const float pf = clamp (pu - floorF (pu), 0.f, 0.99999994f);
                if (wt) o1 = wtWarped (v1, pf, pos1, warp);
                else
                {
                    cornersBothWays (p0, p0 + delta, s1, c1, p1);
                    o1 = shapeValue (pf, s1);
                }
            }
            else if (wt)
            {
                ph1 += inc1; if (ph1 >= 1.f) ph1 -= 1.f;
                o1 = wtWarped (v1, ph1, pos1, warp);
            }
            else
            {
                advanceShape (ph1, inc1, inc1, 0.f, s1, c1, p1);
                o1 = shapeValue (ph1, s1);
            }

            // ---- mix into the (one sample delayed) sum
            const float e1L = g1L * sg1, e2L = g2L * sg2;
            accL[i + 1] += e1L * (o1 + c1) + e2L * (o2 + c2);
            preL[i]     += e1L * p1 + e2L * p2;
            if (stereo)
            {
                const float e1R = g1R * sg1, e2R = g2R * sg2;
                accR[i + 1] += e1R * (o1 + c1) + e2R * (o2 + c2);
                preR[i]     += e1R * p1 + e2R * p2;
            }

            // ---- ramps
            inc1 += u.inc1.step; inc2 += u.inc2.step;
            g1L += u.g[0].step; g1R += u.g[1].step; g2L += u.g[2].step; g2R += u.g[3].step;
            s1.w += u.w[0].step; s1.rw += u.rw[0].step; s1.rf += u.rf[0].step;
            s2.w += u.w[1].step; s2.rw += u.rw[1].step; s2.rf += u.rf[1].step;
            s1.a += aR[0].step; s2.a += aR[1].step; s1.d += dR.step; s2.d = s1.d;
            pos1 += posR[0].step; pos2 += posR[1].step; warp += warpR.step; fmI += fmR.step; syncMul += syncR.step;
            sg1 += sgR[0].step; sg2 += sgR[1].step;
        }

        u.ph1 = ph1; u.ph2 = ph2; u.phM = phM; u.modPrev = (fmR.next == 0.f && std::fabs (modPrev) < 1.0e-7f) ? 0.f : modPrev;
        u.inc1.v = inc1; u.inc2.v = inc2;
        u.g[0].v = g1L; u.g[1].v = g1R; u.g[2].v = g2L; u.g[3].v = g2R;
        u.w[0].v = s1.w; u.rw[0].v = s1.rw; u.rf[0].v = s1.rf;
        u.w[1].v = s2.w; u.rw[1].v = s2.rw; u.rf[1].v = s2.rf;
    }

    void renderSubNoise (int seg)
    {
        const bool subOn = subR.v != 0.f || subR.next != 0.f;
        const bool noiseOn = noiseR.v != 0.f || noiseR.next != 0.f;
        if (! subOn && ! noiseOn) return;
        float sg = subR.v, ng = noiseR.v;
        for (int i = 0; i < seg; ++i)
        {
            float l = 0.f, r = 0.f;
            if (subOn)
            {
                subPh += subInc; if (subPh >= 1.f) subPh -= 1.f;
                // rounded square: fundamental plus a little 3rd and 5th
                float p3 = subPh * 3.f; p3 -= (float) (int) p3;
                float p5 = subPh * 5.f; p5 -= (float) (int) p5;
                const float s = sinTab (sine, subPh) + subH3 * sinTab (sine, p3) + subH5 * sinTab (sine, p5);
                l = r = s * sg;
            }
            if (noiseOn)
            {
                const float a = noiseA.next();
                l += a * ng;
                if (stereo) r += (a + noiseWidth * (noiseB.next() - a)) * ng;
            }
            accL[i + 1] += l;
            if (stereo) accR[i + 1] += r;
            sg += subR.step; ng += noiseR.step;
        }
    }

    // ------------------------------------------------------------------ control grid
    struct Targets
    {
        float a[2], d, pos[2], warp, fm, sync, sg[2], sub, noise;
        float inc1[kMaxUni], inc2[kMaxUni], g[kMaxUni][4], fall[kMaxUni][2];
    };

    // Everything the per-sample code needs, for the parameters as they are now. `advanceDrift`
    // moves the drift LFOs on by one grid step first.
    void computeTargets (Targets& t, bool advanceDrift)
    {
        const UniLayout& lay = kUniLayout[uniIdx];
        const int count = kUniCounts[uniIdx];
        const bool wt = mode > 0;

        // --- waveform
        for (int o = 0; o < 2; ++o)
        {
            const float x = wave[o] * 3.f;
            t.a[o] = wt ? 0.f : clamp (x - 1.f, 0.f, 1.f);
            const float tri = clamp01 (x);
            shapeW[o] = 1.f - 0.5f * (1.f - tri) * std::sqrt (1.f - tri);        // 0.5 (triangle) .. 1 (saw), quick at first
            t.pos[o] = wave[o] * (float) (kWtFrames - 1);
            t.sg[o] = wt ? 1.f : 1.f - 0.29f * t.a[o];                           // a square is much louder than a saw: even it out a little
        }
        {
            const float narrow = clamp (std::max (wave[0], wave[1]) * 3.f - 2.f, 0.f, 1.f);
            t.d = clamp ((0.03f + 0.94f * pw) * (1.f - 0.88f * narrow), 0.02f, 0.98f);
        }
        t.warp = wt ? (pw - 0.5f) * 1.8f * (1.f / kTauF) : 0.f;                  // phase offset = -warp * sin (2 pi ph)
        t.sync = syncRatio;

        // --- pitch
        const float f0 = 440.f * std::exp2 ((noteCur - 69.f) * (1.f / 12.f));
        const float base = f0 / sr;
        if (advanceDrift)
        {
            wowPh += 0.55f * gridDt; wowPh -= (float) (int) wowPh;
            flutPh += 6.8f * gridDt; flutPh -= (float) (int) flutPh;
            flut2Ph += 11.3f * gridDt; flut2Ph -= (float) (int) flut2Ph;
        }
        const float wow = 9.f * sinTab (sine, wowPh), flutter = 2.6f * sinTab (sine, flutPh) + 1.2f * sinTab (sine, flut2Ph);
        const float spread = uniDet * lay.spread;
        float maxInc1 = 0.f, maxInc2 = 0.f;
        float drift0 = 0.f;
        for (int v = 0; v < kMaxUni; ++v)
        {
            UVoice& u = uni[v];
            float cents = v < count ? spread * lay.det[v] : 0.f, cents2 = 0.f;
            if (advanceDrift)
                for (int j = 0; j < 3; ++j) { u.dPh[j] += u.dInc[j]; u.dPh[j] -= (float) (int) u.dPh[j]; }
            if (drift > 0.f)
            {
                // slow wander per voice, plus wow and flutter shared by the "tape" (slightly offset per voice)
                float w2 = wowPh + 0.03f * (float) v; w2 -= (float) (int) w2;
                const float dv = drift * (24.f * (0.65f * sinTab (sine, u.dPh[0]) + 0.35f * sinTab (sine, u.dPh[1])) + (v == 0 ? wow : 9.f * sinTab (sine, w2)) + flutter);
                cents += dv;
                cents2 = drift * 7.f * sinTab (sine, u.dPh[2]);
                if (v == 0) drift0 = dv;
            }
            const float i1 = clamp (base * std::exp2 (cents * (1.f / 1200.f)), 1.0e-6f, 0.45f);
            const float i2 = clamp (i1 * ratio2 * (1.f + cents2 * 0.00057762f), 1.0e-6f, 0.45f);
            t.inc1[v] = i1; t.inc2[v] = i2;
            if (v < count) { maxInc1 = std::max (maxInc1, i1); maxInc2 = std::max (maxInc2, i2); }
        }
        subInc = clamp (0.5f * base * (1.f + drift0 * 0.00057762f), 1.0e-7f, 0.2f);
        subH3 = 0.28f * clamp ((0.42f - 3.f * subInc) * 10.f, 0.f, 1.f);
        subH5 = 0.10f * clamp ((0.42f - 5.f * subInc) * 10.f, 0.f, 1.f);

        // --- FM depth (cycles). In analog mode keep the modulated phase from moving more than half a cycle per sample.
        float fm = fmAmt;
        if (! wt && fm > 0.f) fm = std::min (fm, std::max (0.f, 0.48f - maxInc1) / (kTauF * maxInc2));
        t.fm = fm;
        const float fmSpeed = kTauF * fm * maxInc2;                              // peak extra phase speed per sample

        // --- per-voice trapezoids: the flyback lasts as long as the phase needs for one sample at full speed
        const float slaveMul = std::max (syncRatio, 1.f);
        for (int v = 0; v < kMaxUni; ++v)
        {
            const float fall1 = clamp (t.inc1[v] + fmSpeed, 1.0e-6f, 0.5f);
            const float fall2 = clamp (std::min (t.inc2[v] * slaveMul, 0.45f), 1.0e-6f, 0.5f);
            t.fall[v][0] = fall1; t.fall[v][1] = fall2;
        }

        // --- wavetable views
        if (wt)
        {
            const float warpSlope = 1.f + std::fabs (pw - 0.5f) * 1.8f;
            setView (view[0], maxInc1 * warpSlope + fmSpeed * warpSlope);
            setView (view[1], std::min (maxInc2 * slaveMul, 0.45f) * warpSlope);
        }

        // --- levels
        const float uniGain = 1.f / std::sqrt ((float) count);
        const float m1 = std::cos (mix * (0.5f * kPiF)), m2 = std::sin (mix * (0.5f * kPiF));
        const float oscSpread = stereo ? 0.45f * width * (1.f - std::fabs (2.f * mix - 1.f)) : 0.f;
        for (int v = 0; v < kMaxUni; ++v)
        {
            if (v >= count) { t.g[v][0] = t.g[v][1] = t.g[v][2] = t.g[v][3] = 0.f; continue; }
            const float pan = stereo ? width * lay.pan[v] : 0.f;
            const float a1 = (clamp (pan - oscSpread, -1.f, 1.f) + 1.f) * (0.25f * kPiF), a2 = (clamp (pan + oscSpread, -1.f, 1.f) + 1.f) * (0.25f * kPiF);
            t.g[v][0] = uniGain * m1 * 1.41421356f * std::cos (a1); t.g[v][1] = uniGain * m1 * 1.41421356f * std::sin (a1);
            t.g[v][2] = uniGain * m2 * 1.41421356f * std::cos (a2); t.g[v][3] = uniGain * m2 * 1.41421356f * std::sin (a2);
            if (mix <= 0.f) t.g[v][2] = t.g[v][3] = 0.f;
        }
        t.sub = subLv * 0.75f;
        t.noise = noiseLv * 0.55f;
        noiseWidth = stereo ? width : 0.f;
    }

    void setView (WtView& vw, float incEff) const
    {
        // highest harmonic that stays below the fold-over point -> mip pair
        const float x = std::log2 (std::max (1.0e-9f, (float) kWtHarm * incEff * sr / wtAllowed));
        const int l0 = clamp ((int) std::ceil (x), 0, kWtLevels - 1);
        const int l1 = std::min (l0 + 1, kWtLevels - 1);
        const int set = mode - 1;
        vw.base[0] = bank->level (set, l0); vw.size[0] = (float) kWtSize[l0]; vw.stride[0] = kWtSize[l0] + 1;
        vw.base[1] = bank->level (set, l1); vw.size[1] = (float) kWtSize[l1]; vw.stride[1] = kWtSize[l1] + 1;
        vw.mix = 1.f - clamp ((float) l0 - x, 0.f, 1.f);
    }

    void gridUpdate()
    {
        Targets t;
        const float c = smoothCoef, ik = invGrid;
        if (snap)
        {
            // first sample of a note: everything starts on its target
            computeTargets (t, false);
            for (int o = 0; o < 2; ++o) { aR[o].snapTo (t.a[o]); posR[o].snapTo (t.pos[o]); sgR[o].snapTo (t.sg[o]); }
            dR.snapTo (t.d); warpR.snapTo (t.warp); fmR.snapTo (t.fm); syncR.snapTo (t.sync); subR.snapTo (t.sub); noiseR.snapTo (t.noise);
            for (int v = 0; v < kMaxUni; ++v)
            {
                UVoice& u = uni[v];
                u.inc1.snapTo (t.inc1[v]); u.inc2.snapTo (t.inc2[v]);
                for (int j = 0; j < 4; ++j) u.g[j].snapTo (t.g[v][j]);
                for (int o = 0; o < 2; ++o)
                {
                    const float w = std::min (shapeW[o], 1.f - t.fall[v][o]);
                    u.w[o].snapTo (w); u.rw[o].snapTo (1.f / w); u.rf[o].snapTo (1.f / (1.f - w));
                }
            }
            shapeSm[0] = shapeW[0]; shapeSm[1] = shapeW[1];
            snap = false;
        }
        else
        {
            for (auto* r : { &aR[0], &aR[1], &dR, &posR[0], &posR[1], &warpR, &fmR, &syncR, &sgR[0], &sgR[1], &subR, &noiseR }) r->land();
            for (auto& u : uni)
            {
                u.inc1.land(); u.inc2.land();
                for (auto& r : u.g) r.land();
                for (int o = 0; o < 2; ++o) { u.w[o].land(); u.rw[o].land(); u.rf[o].land(); }
            }
        }

        // where everything should be one grid step from now
        computeTargets (t, true);
        auto glide = [c, ik] (Ramp& r, float target, float eps)
        {
            float nx = r.v + c * (target - r.v);
            if (std::fabs (nx - target) < eps) nx = target;
            r.aim (nx, ik);
        };
        for (int o = 0; o < 2; ++o) { glide (aR[o], t.a[o], 1.0e-5f); glide (posR[o], t.pos[o], 1.0e-4f); glide (sgR[o], t.sg[o], 1.0e-5f); }
        glide (dR, t.d, 1.0e-5f); glide (warpR, t.warp, 1.0e-6f); glide (fmR, t.fm, 1.0e-5f); glide (syncR, t.sync, 1.0e-4f);
        glide (subR, t.sub, 1.0e-5f); glide (noiseR, t.noise, 1.0e-5f);
        for (int o = 0; o < 2; ++o) { shapeSm[o] += c * (shapeW[o] - shapeSm[o]); if (std::fabs (shapeSm[o] - shapeW[o]) < 1.0e-5f) shapeSm[o] = shapeW[o]; }
        for (int v = 0; v < kMaxUni; ++v)
        {
            UVoice& u = uni[v];
            u.inc1.aim (t.inc1[v], ik); u.inc2.aim (t.inc2[v], ik);
            bool any = false;
            for (int j = 0; j < 4; ++j) { glide (u.g[j], t.g[v][j], 1.0e-5f); any = any || u.g[j].v != 0.f || u.g[j].next != 0.f; }
            u.running = any || (v == 0);                         // voice 0 always runs: it carries the FM / sub reference
            for (int o = 0; o < 2; ++o)
            {
                // the shape glides like every other parameter; the flyback limit follows the pitch directly
                const float w = std::min (shapeSm[o], 1.f - t.fall[v][o]);
                u.w[o].aim (w, ik); u.rw[o].aim (1.f / w, ik); u.rf[o].aim (1.f / (1.f - w), ik);
            }
        }
    }

    float sr = 48000.f;
    const float* sine = nullptr;
    const WtBank* bank = nullptr;
    int   grid = 16, gridLeft = 0;
    float invGrid = 1.f / 16.f, gridDt = 16.f / 48000.f, smoothCoef = 0.1f, fadeInc = 0.03f, wtAllowed = 30000.f;
    bool  active = false, stereo = false, snap = true;

    // parameters (refreshed in control())
    float lastE[kEngineParams];
    float noteCur = 60.f;
    int   mode = 0, uniIdx = 0;
    float wave[2] = { 0.4f, 0.4f }, pw = 0.5f, ratio2 = 1.f, syncRatio = 1.f, mix = 0.f, subLv = 0.f, noiseLv = 0.f;
    float fmAmt = 0.f, uniDet = 0.f, drift = 0.f, width = 0.5f;

    // shared smoothed values
    Ramp  aR[2], dR, posR[2], warpR, fmR, syncR, sgR[2], subR, noiseR;
    float shapeW[2] = { 1.f, 1.f }, shapeSm[2] = { 1.f, 1.f };
    WtView view[2];
    float noiseWidth = 0.f;

    // running state
    UVoice uni[kMaxUni];
    float subPh = 0.f, subInc = 0.f, subH3 = 0.f, subH5 = 0.f;
    float wowPh = 0.f, flutPh = 0.f, flut2Ph = 0.f;
    Noise noiseA, noiseB;
    float fade = 0.f, carryL = 0.f, carryR = 0.f;
    float accL[kMaxSlice + 2], accR[kMaxSlice + 2], preL[kMaxSlice + 2], preR[kMaxSlice + 2];
    DcBlocker dcL, dcR;
};

std::unique_ptr<EngineBase> createSynthEngine() { return std::make_unique<SynthEngine>(); }

} // namespace atm
