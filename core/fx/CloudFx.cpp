// Cloud: a large, lush feedback-delay-network reverb for ambient washes.
//
//   in L/R -> predelay -> 4 diffusion allpasses per channel --+
//                                                             v
//   16 delay lines (modulated reads) -> damping LP/HP -> gain -> 16x16 orthogonal mix -> (+) -> lines
//          |                                                         ^
//          +--> out L/R (two orthogonal +-1 sums)                     +-- shimmer: part of the fed-back
//                                                                        energy replaced by its octave
//
// Published building blocks: the feedback delay network with a unitary matrix and per-line
// absorption proportional to line length (Stautner & Puckette 1982; Jot & Chaigne 1991), a matrix
// built from four stages of Givens rotations (the fast Walsh-Hadamard butterfly at 45 degrees).
// Independent implementation.
//
//   * Line lengths: 29.7 .. 103 ms times `size`, rounded to integers with no factor below 17 (so no
//     two lines share a period). They are whole samples at rest and glide when the size changes.
//   * `decay` is the RT60: g_i = 10^(-3 len_i / RT). At the maximum the loop is frozen: gains of
//     exactly 1, damping and modulation off (interpolated reads would bleed the top end away), the
//     input still enters. A loop-energy limiter keeps a frozen loop that is still being fed bounded.
//   * Damping is relative to the decay: at `damphi` (and `damplo`) the decay time is half the
//     nominal one, falling further beyond; for short decays it is capped to plain 3 dB corners.
//   * `diffusion` turns the matrix from the identity (16 parallel echo lines) to full Hadamard mixing
//     (reached at 0.7) and raises the input allpass coefficients.
//   * `shimmer`: two orthogonal components of the fed-back signal are rotated into octave-up copies
//     of themselves (two-grain delay-line shifters behind a 4-pole low-pass). Energy is moved up an
//     octave per pass rather than added, so the loop gain never exceeds the unshimmered one.
#include "Effect.h"
#include "../dsp/Delay.h"
#include "../dsp/Filters.h"
#include "../model/ParamTables.h"

namespace atm {
namespace cloudfx {

enum { PSize, PDecay, PDiffusion, PDampHi, PDampLo, PMod, PShimmer, PPredelay };
constexpr int kLines = 16;
constexpr int kTick = 16;
constexpr float kMaxSize = 4.f;
constexpr float kLineMs[kLines] = { 29.7f, 32.3f, 35.9f, 38.3f, 42.1f, 45.7f, 49.9f, 53.3f, 58.1f, 62.9f, 68.3f, 73.9f, 80.3f, 86.9f, 94.7f, 103.1f };
constexpr float kLfoHz[kLines] = { 0.31f, 1.13f, 0.59f, 1.61f, 0.43f, 0.97f, 0.73f, 1.37f, 0.37f, 1.21f, 0.67f, 1.49f, 0.53f, 1.03f, 0.83f, 1.69f };
constexpr float kDiffMs[2][4] = { { 4.31f, 7.13f, 11.37f, 17.93f }, { 4.93f, 7.87f, 12.71f, 19.37f } };
constexpr float kMaxDiffScale = 2.5f;
constexpr float kMaxPitchDev = 0.012f;        // modulation: +-20 cents per pass at mod = 1
// +-1 patterns chosen (by search) to be mutually orthogonal and to have flat images under the mixing
// matrix, so no single echo path dominates either output.
constexpr float kOutL[kLines] = { -1, -1, 1, 1, 1, -1, -1, 1, 1, -1, 1, -1, 1, 1, 1, 1 };
constexpr float kOutR[kLines] = { 1, 1, 1, 1, -1, 1, -1, 1, -1, 1, 1, -1, -1, -1, 1, 1 };
constexpr float kInL[kLines]  = { -1, 1, 1, -1, 1, 1, -1, -1, -1, 1, -1, 1, 1, 1, 1, 1 };
constexpr float kInR[kLines]  = { -1, -1, 1, 1, -1, 1, 1, -1, -1, -1, -1, -1, -1, 1, -1, 1 };
constexpr float kShimA[kLines] = { 1, 1, -1, -1, 1, 1, -1, -1, 1, 1, -1, -1, 1, 1, -1, -1 };
constexpr float kShimB[kLines] = { 1, -1, -1, 1, 1, -1, -1, 1, 1, -1, -1, 1, 1, -1, -1, 1 };
constexpr float kEnergyMax = 40.f;            // loop-energy ceiling (sum of squares of the 16 line outputs)
constexpr float kOutGain = 0.30f;

// A delay line whose length glides to a whole-sample target.
struct GDelay
{
    DelayLine line;
    float len = 16.f, target = 16.f, maxStep = 0.35f;
    int ilen = 16;
    bool moving = false;

    void prepare (float maxSamples) { line.prepare ((int) maxSamples + 16); }
    void setTarget (float samples)
    {
        target = std::max (std::round (samples), 2.f);
        if (target != len) moving = true;
    }
    void snap() { len = target; ilen = (int) target; moving = false; }
    inline void glide (float k)
    {
        if (! moving) return;
        const float diff = target - len;
        if (std::fabs (diff) < 2.0e-3f) { snap(); return; }
        len += clamp (diff * k, -maxStep, maxStep);
    }
    inline float out() const { return moving ? line.readLinear (len - 1.f) : line.read (ilen - 1); }
    inline float outMod (float offset) const { return line.readHermite (std::max (len - 1.f + offset, 1.f)); }
    inline void write (float x) { line.write (x); }
};

struct GAllpass
{
    GDelay d;
    inline float process (float x, float g)
    {
        const float z = d.out();
        const float w = undenorm (x + g * z);
        d.write (w);
        return z - g * w;
    }
};

// One-pole low-pass coefficient (y += a (x - y)) with a given loss in dB at a given frequency.
inline float lpCoefForLoss (float lossDb, float hz, float sr)
{
    if (lossDb < 1.0e-5f) return 1.f;
    const float g2 = std::pow (10.f, -0.1f * lossDb);
    const float c = std::cos (kTauF * std::min (hz, 0.49f * sr) / sr);
    const float p = 1.f - g2 * c, q = 1.f - g2;
    const float b = (p - std::sqrt (std::max (p * p - q * q, 0.f))) / q;
    return clamp (1.f - b, 1.0e-4f, 1.f);
}

// nearest integer >= x with no prime factor below 17
inline float roughPrime (float x)
{
    int n = std::max (5, (int) std::lround (x)) | 1;
    while (n % 3 == 0 || n % 5 == 0 || n % 7 == 0 || n % 11 == 0 || n % 13 == 0) n += 2;
    return (float) n;
}

// Octave-up shifter: two taps sweep a delay line at twice the write speed (whole-sample reads),
// cross-faded with complementary Hann windows; a 4-pole low-pass in front keeps the doubled
// spectrum below Nyquist.
struct Shifter
{
    DelayLine d;
    Svf f1, f2;
    int W = 4096, k = 0;
    void prepare (float sr, float seconds)
    {
        W = ((int) (seconds * sr)) & ~1;
        d.prepare (W + 8);
        const float hz = std::min (6500.f, 0.2f * sr);
        f1.setQ (hz, 0.54f, sr); f2.setQ (hz, 1.31f, sr);
        reset();
    }
    void reset() { d.reset(); f1.reset(); f2.reset(); k = 0; }
    inline float process (float x)
    {
        const int kb = (k + W / 2) % W;
        const float wa = 0.5f + 0.5f * fastSin01 ((float) k / (float) W - 0.25f);
        const float y = wa * d.read (W - 1 - k) + (1.f - wa) * d.read (W - 1 - kb);
        d.write (f2.lp (f1.lp (x)));
        if (++k >= W) k = 0;
        return y;
    }
    inline void idle() { d.write (0.f); if (++k >= W) k = 0; }
};

struct Line
{
    GDelay d;
    float lp = 0.f, hp = 0.f;
    float g = 0.f, gT = 0.f, lpA = 1.f, lpAT = 1.f, hpA = 0.f, hpAT = 0.f;
    float mod = 0.f, modInc = 0.f, phase = 0.f;
};

class CloudFx final : public Effect
{
public:
    void prepare (double sampleRate) override
    {
        sr = (float) sampleRate;
        const float modMargin = kMaxPitchDev * sr / (kTauF * 0.31f) + 8.f;
        for (int i = 0; i < kLines; ++i) line[i].d.prepare (kLineMs[i] * 0.001f * sr * kMaxSize + modMargin + 32.f);
        for (int c = 0; c < 2; ++c)
        {
            pre[c].prepare (0.5f * sr + 8.f);
            for (int k = 0; k < 4; ++k) diff[c][k].d.prepare (kDiffMs[c][k] * 0.001f * sr * kMaxDiffScale + 8.f);
        }
        shim[0].prepare (sr, 0.083f);
        shim[1].prepare (sr, 0.097f);
        kGlide = onePoleCoef (0.08f, sr);
        kSmooth = onePoleCoef (0.02f, sr);
        kEnv = onePoleCoef (0.003f, sr);
        kTickSmooth = 1.f - std::exp (-(float) kTick / (0.03f * sr));
        reset();
    }

    void reset() override
    {
        for (int i = 0; i < kLines; ++i)
        {
            Line& q = line[i];
            q.d.line.reset(); q.lp = q.hp = 0.f; q.mod = q.modInc = 0.f;
            q.phase = (float) i * 0.618034f; q.phase -= std::floor (q.phase);
        }
        for (int c = 0; c < 2; ++c)
        {
            pre[c].line.reset();
            for (auto& a : diff[c]) a.d.line.reset();
            shim[c].reset();
        }
        energy = 0.f; tick = 0; first = true;
    }

    void setParams (const float* p, const FxContext&) override
    {
        const float size = kFxCloud[PSize].toPhys (p[PSize]);
        const bool freeze = p[PDecay] >= 0.9995f;
        const float decay = kFxCloud[PDecay].toPhys (p[PDecay]);
        const float diffusion = clamp01 (p[PDiffusion]);
        const float dampHi = kFxCloud[PDampHi].toPhys (p[PDampHi]);
        const float dampLo = kFxCloud[PDampLo].toPhys (p[PDampLo]);
        const float mod = clamp01 (p[PMod]);
        const float shimmer = clamp01 (p[PShimmer]);

        if (size != cSize || first)
        {
            cSize = size;
            for (int i = 0; i < kLines; ++i) line[i].d.setTarget (roughPrime (kLineMs[i] * 0.001f * sr * size));
            const float ds = std::min (size, kMaxDiffScale);
            for (int c = 0; c < 2; ++c)
                for (int k = 0; k < 4; ++k) diff[c][k].d.setTarget (roughPrime (kDiffMs[c][k] * 0.001f * sr * ds));
        }
        for (int c = 0; c < 2; ++c) pre[c].setTarget (std::max (kFxCloud[PPredelay].toPhys (p[PPredelay]) * sr, 2.f));

        if (size != dSize || decay != dDecay || dampHi != dHi || dampLo != dLo || freeze != dFreeze)
        {
            dSize = size; dDecay = decay; dHi = dampHi; dLo = dampLo; dFreeze = freeze;
            float sum = 0.f;
            for (int i = 0; i < kLines; ++i)
            {
                Line& q = line[i];
                const float t = q.d.target / sr;
                sum += t;
                if (freeze) { q.gT = 1.f; q.lpAT = 1.f; q.hpAT = 0.f; continue; }
                const float lossDb = 60.f * t / decay;
                q.gT = std::pow (10.f, -0.05f * lossDb);
                const float capped = std::min (lossDb, 3.f);
                q.lpAT = lpCoefForLoss (capped, dampHi, sr);
                const float g2 = std::pow (10.f, -0.1f * capped);
                q.hpAT = 1.f - std::exp (-kTauF * dampLo * std::sqrt ((1.f - g2) / g2) / sr);
            }
            avgLineSec = sum / (float) kLines;
            tHpMix = freeze ? 0.f : 1.f;
            // tail energy grows with the decay time: take half of that (in dB) back at the input
            tInGain = freeze ? 0.5f : clamp (std::pow (8.f / decay, 0.25f), 0.5f, 2.f);
        }
        frozen = freeze;

        const float mix = std::min (1.f, diffusion / 0.7f);
        const float theta = 0.25f * kPiF * mix * std::sqrt (mix);
        tCos = std::cos (theta); tSin = std::sin (theta);
        if (mix >= 1.f) tCos = tSin = 0.70710678f;
        tDiffCoef = 0.72f * diffusion;
        tModDev = freeze ? 0.f : mod * mod * kMaxPitchDev;
        // fraction of each shimmer component's energy converted per pass, scaled with the line time so
        // the halo builds at a similar speed at any size
        const float frac = std::min (1.f, shimmer * shimmer * 12.f * avgLineSec);
        tShimSin = std::sqrt (frac);
        tShimCos = std::sqrt (1.f - frac);

        if (first)
        {
            first = false;
            for (auto& q : line) { q.d.snap(); q.g = q.gT; q.lpA = q.lpAT; q.hpA = q.hpAT; }
            for (int c = 0; c < 2; ++c) { pre[c].snap(); for (auto& a : diff[c]) a.d.snap(); }
            mc = tCos; ms = tSin; diffCoef = tDiffCoef; modDev = tModDev; hpMix = tHpMix; inGain = tInGain;
            shimSin = tShimSin; shimCos = tShimCos;
            tick = 0;
        }
    }

    void process (float* l, float* r, int n, const FxContext&) override
    {
        for (int i = 0; i < n; ++i)
        {
            if (tick == 0) controlTick();
            if (++tick >= kTick) tick = 0;

            diffCoef += kSmooth * (tDiffCoef - diffCoef);
            inGain += kSmooth * (tInGain - inGain);
            hpMix += kSmooth * (tHpMix - hpMix);
            shimSin += kSmooth * (tShimSin - shimSin);
            shimCos += kSmooth * (tShimCos - shimCos);

            // ---- input: predelay and diffusion per channel
            float in[2] = { l[i], r[i] };
            for (int c = 0; c < 2; ++c)
            {
                pre[c].glide (kGlide);
                float x = pre[c].out();
                pre[c].write (in[c]);
                for (int k = 0; k < 4; ++k) { diff[c][k].d.glide (kGlide); x = diff[c][k].process (x, diffCoef); }
                in[c] = x * inGain * 0.25f;
            }

            // ---- read the lines, damp, tap the outputs
            const float agc = energy > kEnergyMax ? std::sqrt (kEnergyMax / energy) : 1.f;
            float v[kLines];
            float yl = 0.f, yr = 0.f, e = 0.f;
            for (int k = 0; k < kLines; ++k)
            {
                Line& q = line[k];
                q.d.glide (kGlide);
                float o;
                if (modActive) { q.mod += q.modInc; o = q.d.outMod (q.mod); }
                else o = q.d.out();
                yl += kOutL[k] * o; yr += kOutR[k] * o; e += o * o;
                q.lp = undenorm (q.lp + q.lpA * (o - q.lp));
                q.hp = undenorm (q.hp + q.hpA * (q.lp - q.hp));
                v[k] = (q.lp - hpMix * q.hp) * q.g * agc;
            }
            energy += kEnv * (e - energy);

            // ---- orthogonal mix: four stages of rotations (Hadamard at 45 degrees)
            for (int span = 1; span < kLines; span <<= 1)
                for (int a = 0; a < kLines; ++a)
                {
                    if (a & span) continue;
                    const float xa = v[a], xb = v[a + span];
                    v[a] = mc * xa + ms * xb;
                    v[a + span] = mc * xb - ms * xa;
                }

            // ---- shimmer: rotate two components into their octave-up versions
            if (shimSin > 1.0e-4f)
            {
                float ma = 0.f, mb = 0.f;
                for (int k = 0; k < kLines; ++k) { ma += kShimA[k] * v[k]; mb += kShimB[k] * v[k]; }
                ma *= 0.25f; mb *= 0.25f;
                const float da = 0.25f * ((shimCos - 1.f) * ma + shimSin * shim[0].process (ma));
                const float db = 0.25f * ((shimCos - 1.f) * mb + shimSin * shim[1].process (mb));
                for (int k = 0; k < kLines; ++k) v[k] += kShimA[k] * da + kShimB[k] * db;
            }
            else { shim[0].idle(); shim[1].idle(); }

            for (int k = 0; k < kLines; ++k)
                line[k].d.write (undenorm (v[k] + kInL[k] * in[0] + kInR[k] * in[1]));

            l[i] = yl * kOutGain;
            r[i] = yr * kOutGain;
        }
    }

private:
    void controlTick()
    {
        modDev += kTickSmooth * (tModDev - modDev);
        modActive = modDev > 1.0e-6f;
        mc += kTickSmooth * (tCos - mc); if (std::fabs (tCos - mc) < 1.0e-6f) mc = tCos;
        ms += kTickSmooth * (tSin - ms); if (std::fabs (tSin - ms) < 1.0e-6f) ms = tSin;
        for (int k = 0; k < kLines; ++k)
        {
            Line& q = line[k];
            q.g += kTickSmooth * (q.gT - q.g);       if (std::fabs (q.gT - q.g) < 1.0e-7f) q.g = q.gT;
            q.lpA += kTickSmooth * (q.lpAT - q.lpA); if (std::fabs (q.lpAT - q.lpA) < 1.0e-6f) q.lpA = q.lpAT;
            q.hpA += kTickSmooth * (q.hpAT - q.hpA); if (std::fabs (q.hpAT - q.hpA) < 1.0e-8f) q.hpA = q.hpAT;
            q.phase += kLfoHz[k] * (float) kTick / sr; if (q.phase >= 1.f) q.phase -= 1.f;
            if (modActive)
            {
                const float depth = std::min (modDev * sr / (kTauF * kLfoHz[k]), 0.25f * q.d.len);
                q.modInc = (depth * fastSin01 (q.phase) - q.mod) * (1.f / (float) kTick);
            }
            else { q.mod = 0.f; q.modInc = 0.f; }
        }
    }

    float sr = 48000.f;
    Line line[kLines];
    GDelay pre[2];
    GAllpass diff[2][4];
    Shifter shim[2];
    float kGlide = 0.001f, kSmooth = 0.001f, kEnv = 0.01f, kTickSmooth = 0.01f;
    float cSize = -1.f, dSize = -1.f, dDecay = -1.f, dHi = -1.f, dLo = -1.f;
    bool dFreeze = false, frozen = false;
    float avgLineSec = 0.1f;
    float tCos = 0.7071f, tSin = 0.7071f, mc = 0.7071f, ms = 0.7071f;
    float tDiffCoef = 0.f, diffCoef = 0.f, tModDev = 0.f, modDev = 0.f, tHpMix = 1.f, hpMix = 1.f;
    float tInGain = 1.f, inGain = 1.f, tShimSin = 0.f, shimSin = 0.f, tShimCos = 1.f, shimCos = 1.f;
    float energy = 0.f;
    bool modActive = false;
    int tick = 0;
    bool first = true;
};

} // namespace cloudfx

std::unique_ptr<Effect> createCloudFx() { return std::make_unique<cloudfx::CloudFx>(); }

} // namespace atm
