// FM engine: four sine operators, through-zero phase modulation, eight
// algorithms, feedback on operator 4.
//
//   op n : 32-bit phase accumulator -> table sine, own decay envelope
//   level: modulation index for a modulator (0..4*pi rad, squared law), output
//          amplitude for a carrier. Velocity scales the indices (50 % depth).
//   out  : sum of the carriers / sqrt(number of carriers) -> DC blocker
//
// The operators run at twice the sample rate (at 44.1/48 kHz) and are decimated
// with a half-band filter. On top of that an "alias guard" estimates each
// operator's bandwidth with Carson's rule once per control grid and pulls the
// indices back just enough to keep the sidebands below the fold-over point, so
// high notes get darker instead of dirty.
//
// Everything that can jump (levels, ratios, algorithm, feedback) is smoothed on
// a fixed internal grid counted from note-on, so the output does not depend on
// how the host slices its buffers.
//
// Original work. Phase-modulation synthesis follows J. Chowning, "The Synthesis
// of Complex Audio Spectra by Means of Frequency Modulation" (JAES 1973); the
// two-sample feedback average is the well-known trick from the DX-series
// operators. The decimator is atm::Oversampler2x (HIIR coefficients, see there).
#include "EngineBase.h"
#include "../model/ParamTables.h"
#include "../dsp/Filters.h"
#include "../dsp/Oversample.h"

namespace atm {

namespace {

enum P { Algo, Fb, Ratio1, Ratio2, Ratio3, Ratio4, Level1, Level2, Level3, Level4,
         Decay1, Decay2, Decay3, Decay4, PEnv, PEnvT };

constexpr int   kOps       = 4;
constexpr int   kSineBits  = 11;
constexpr int   kSineSize  = 1 << kSineBits;
constexpr float kPhaseUnit = 4294967296.f;                 // one cycle in accumulator units
constexpr float kRadToUnit = kPhaseUnit / kTauF;
constexpr float kMaxIndex  = 4.f * kPiF;                   // modulation index (rad) at level 1
constexpr float kOutGain   = 0.71f;                        // one carrier at level 1 peaks at about -3 dBFS

struct SineTable
{
    float v[kSineSize + 1];
    SineTable() { for (int i = 0; i <= kSineSize; ++i) v[i] = (float) std::sin (kTau * (double) i / (double) kSineSize); }
};
const float* sineTable() { static const SineTable t; return t.v; }

// Connections (modulator -> target), fixed order so the inner loop is branch-free.
enum Conn { C43, C42, C41, C32, C31, C21, kConns };
constexpr int kConnSrc[kConns] = { 3, 3, 3, 2, 2, 1 };     // zero-based operator numbers
constexpr int kConnDst[kConns] = { 2, 1, 0, 1, 0, 0 };

struct AlgoDef { uint8_t conn[kConns]; uint8_t carrier[kOps]; };
constexpr AlgoDef kAlgos[8] = {
    //  4>3 4>2 4>1 3>2 3>1 2>1     carriers 1 2 3 4
    { { 1,  0,  0,  1,  0,  1 }, { 1, 0, 0, 0 } },   // 0  4>3>2>1 stack
    { { 1,  0,  0,  0,  0,  1 }, { 1, 0, 1, 0 } },   // 1  (4>3) + (2>1)
    { { 0,  0,  1,  0,  1,  1 }, { 1, 0, 0, 0 } },   // 2  4, 3, 2 all into 1
    { { 1,  0,  0,  0,  1,  1 }, { 1, 0, 0, 0 } },   // 3  4>3>1 and 2>1
    { { 1,  1,  1,  0,  0,  0 }, { 1, 1, 1, 0 } },   // 4  4 into 1, 2 and 3
    { { 1,  0,  0,  1,  0,  0 }, { 1, 1, 0, 0 } },   // 5  4>3>2 stack + carrier 1
    { { 1,  0,  0,  0,  0,  0 }, { 1, 1, 1, 0 } },   // 6  4>3 + carriers 2 and 1
    { { 0,  0,  0,  0,  0,  0 }, { 1, 1, 1, 1 } },   // 7  additive
};

// Ratios snap softly to the harmonic grid (multiples of 0.5 from 1 up, 0.25 below 1): inside
// +-1.5 % of a grid point the ratio is exact, then it blends back so sweeps stay continuous.
float snapRatio (float r)
{
    const float grid = r < 0.875f ? 0.25f : 0.5f;
    const float u = r / grid;
    const float n = std::max (1.f, std::floor (u + 0.5f));
    const float d = u - n;
    const float w = std::min (0.015f * n, 0.2f);
    const float ad = std::fabs (d);
    if (ad <= w) return n * grid;
    if (ad >= 2.f * w) return r;
    return (n + (d < 0.f ? -1.f : 1.f) * (ad - w) * 2.f) * grid;
}

} // namespace

class FmEngine final : public EngineBase
{
public:
    void prepare (double sampleRate) override
    {
        sr = (float) sampleRate;
        os = sampleRate < 70000.0 ? 2 : 1;
        rate = sr * (float) os;
        sine = sineTable();
        grid = std::max (1, (int) std::lround (sampleRate / 6000.0));
        atkInc = 1.f / std::max (2.f, 0.0007f * sr);
        smoothCoef = 1.f - std::exp (-(float) grid / (0.0015f * sr));
        dc.prepare (sr, 4.f);
        for (auto& v : lastE) v = -1.f;
        reset();
    }

    void reset() override
    {
        active = false; gate = false;
        for (int k = 0; k < kOps; ++k) { ph[k] = 0; inc[k] = 0; incStep[k] = 0; incTarget[k] = 0; env[k] = 0.f; amp[k] = ampStep[k] = ampNext[k] = 0.f; }
        for (int c = 0; c < kConns; ++c) m[c] = mStep[c] = mNext[c] = 0.f;
        fbG = fbStep = fbNext = 0.f; fb1 = fb2 = 0.f;
        attack = 1.f; pe = 0.f; gridLeft = 0; snap = true; sinceOn = 0;
        dn.reset(); dc.reset();
    }

    void noteOn (const EngineNote& n, const float* e) override
    {
        vel = clamp01 (n.velocity);
        if (! n.legato || ! active)
        {
            for (int k = 0; k < kOps; ++k) { ph[k] = 0; env[k] = 1.f; }
            fb1 = fb2 = 0.f;
            attack = 0.f; pe = 1.f;
            gridLeft = 0; snap = true; sinceOn = 0; done = false;
            dn.reset(); dc.reset();
            active = true;
        }
        gate = true;
        control (e, n.note);
    }

    void noteOff() override { gate = false; }

    void control (const float* e, float note) override
    {
        const ParamDesc* d = kFmParams;
        noteCur = note;
        algo = clamp (d[Algo].toIndex (e[Algo]), 0, 7);
        if (e[Fb] != lastE[Fb])
        {
            lastE[Fb] = e[Fb];
            const float f = clamp01 (e[Fb]);
            const float f2 = f * f;
            fbBeta = 2.2f * f + 4.8f * f2 * f2 * f;      // rad: saw-like at 0.5 (1.25), brightest stable tone near 0.75, chaos above ~0.8
        }
        for (int k = 0; k < kOps; ++k)
        {
            if (e[Ratio1 + k] != lastE[Ratio1 + k]) { lastE[Ratio1 + k] = e[Ratio1 + k]; ratio[k] = snapRatio (d[Ratio1 + k].toPhys (e[Ratio1 + k])); }
            if (e[Level1 + k] != lastE[Level1 + k])
            {
                lastE[Level1 + k] = e[Level1 + k];
                const float l = clamp01 (e[Level1 + k]);
                index[k] = kMaxIndex * l * l;
                carAmp[k] = l * std::sqrt (l);
            }
            if (e[Decay1 + k] != lastE[Decay1 + k])
            {
                lastE[Decay1 + k] = e[Decay1 + k];
                // the top of the range means "hold while the note lasts" (the voice's amp envelope ends it)
                decCoef[k] = e[Decay1 + k] >= 0.995f ? 1.f : std::exp (-6.9078f / std::max (4.f, d[Decay1 + k].toPhys (e[Decay1 + k]) * sr));
            }
        }
        penv = d[PEnv].toPhys (e[PEnv]);
        if (e[PEnvT] != lastE[PEnvT])
        {
            lastE[PEnvT] = e[PEnvT];
            peCoef = std::exp (-6.9078f * (float) grid / std::max (4.f, d[PEnvT].toPhys (e[PEnvT]) * sr));
        }
    }

    void render (float* outL, float* /*outR*/, int n) override
    {
        if (! active) { for (int i = 0; i < n; ++i) outL[i] = 0.f; return; }
        const float* const tab = sine;
        auto sinQ = [tab] (uint32_t p) -> float
        {
            const uint32_t i = p >> (32 - kSineBits);
            const float f = (float) (int32_t) (p & ((1u << (32 - kSineBits)) - 1u)) * (1.f / (float) (1u << (32 - kSineBits)));
            return tab[i] + f * (tab[i + 1] - tab[i]);
        };
        auto toU = [] (float x) -> uint32_t { return (uint32_t) (int64_t) x; };

        for (int i = 0; i < n; ++i)
        {
            if (gridLeft == 0) { gridUpdate(); gridLeft = grid; }
            --gridLeft;

            // envelopes: a short smooth attack shared by all operators, then each decays on its own
            float a = attack;
            if (attack < 1.f) attack = std::min (1.f, attack + atkInc);
            else for (int k = 0; k < kOps; ++k) env[k] *= decCoef[k];
            a = a * a * (3.f - 2.f * a);
            const float e2 = env[1] * a, e3 = env[2] * a, e4 = env[3] * a;

            for (int c = 0; c < kConns; ++c) m[c] += mStep[c];
            for (int k = 0; k < kOps; ++k) amp[k] += ampStep[k];
            fbG += fbStep;

            const float m43 = m[C43] * e4, m42 = m[C42] * e4, m41 = m[C41] * e4;
            const float m32 = m[C32] * e3, m31 = m[C31] * e3, m21 = m[C21] * e2;
            const float a1 = amp[0] * env[0] * a, a2 = amp[1] * e2, a3 = amp[2] * e3, a4 = amp[3] * e4;
            const float fb = fbG * e4;

            float y[2] = { 0.f, 0.f };
            for (int s = 0; s < os; ++s)
            {
                for (int k = 0; k < kOps; ++k) { inc[k] += (uint32_t) incStep[k]; ph[k] += inc[k]; }
                const float s4 = sinQ (ph[3] + toU (fb * (fb1 + fb2)));
                fb2 = fb1; fb1 = s4;
                const float s3 = sinQ (ph[2] + toU (m43 * s4));
                const float s2 = sinQ (ph[1] + toU (m32 * s3 + m42 * s4));
                const float s1 = sinQ (ph[0] + toU (m21 * s2 + m31 * s3 + m41 * s4));
                y[s] = a1 * s1 + a2 * s2 + a3 * s3 + a4 * s4;
            }
            const float o = os == 2 ? dn.down (y[0], y[1]) : y[0];
            outL[i] = dc.process (o);
        }
        sinceOn += n;
        for (int k = 0; k < kOps; ++k) env[k] = undenorm (env[k]);
        // done when every carrier of the current algorithm has decayed away
        if (sinceOn > 256)
        {
            bool any = false;
            for (int k = 0; k < kOps; ++k) if (kAlgos[algo].carrier[k] && env[k] > 1.0e-4f) any = true;
            if (! any) { done = true; }
            else done = false;
        }
        else done = false;
    }

    bool finished() const override { return ! active || done; }

private:
    // Operator increments for a pitch-envelope value.
    void incsFor (float pitchEnv, uint32_t* dst, float* hz) const
    {
        const float f0 = 440.f * std::exp2 ((noteCur + penv * pitchEnv - 69.f) * (1.f / 12.f));
        for (int k = 0; k < kOps; ++k)
        {
            const float f = clamp (f0 * ratio[k], 0.f, 0.45f * rate);
            hz[k] = f;
            dst[k] = (uint32_t) ((double) f / (double) rate * 4294967296.0);
        }
    }

    // Gain targets for the current parameters: connection indices (in accumulator units),
    // carrier amplitudes and feedback, including the alias guard.
    void gainTargets (const float* hz, float* mT, float* aT, float& fbT) const
    {
        const AlgoDef& al = kAlgos[algo];
        const float velIdx = 0.5f + 0.5f * vel;
        // The guard looks at the envelopes as they will be once the 0.7 ms attack is over: judging
        // the indices by the attack ramp itself let them through unscaled for the first grid steps,
        // and the guard then caught up only through the 1.5 ms smoothing (a noisy burst at the
        // onset of high notes).
        const float a = 1.f;
        float idxNow[kOps], scale[kOps] = { 1.f, 1.f, 1.f, 1.f };
        for (int k = 0; k < kOps; ++k) idxNow[k] = index[k] * velIdx * env[k] * a;

        // Alias guard, evaluated from the top of the stack down. dev[k] is the highest instantaneous
        // frequency operator k reaches (its own frequency plus index times the modulators' peak
        // frequencies); beyond that the sidebands fall away over about 3.3 * index^0.25 times the
        // modulator frequency (a fit to the Bessel functions at -60 dB, which tracks measured
        // spectra of nested stacks within a factor of 1 to 1.8). If the total passes the
        // fold-over limit, the indices into that operator are scaled back.
        const float limit = rate - 18000.f;                      // anything above this folds below 18 kHz
        float dev[kOps];
        // feedback turns operator 4 into a saw-like wave with roughly (1 + 3 beta) harmonics that matter
        dev[3] = hz[3] * (1.f + 3.f * std::min (fbBeta * env[3] * a, 1.6f));
        for (int k = 2; k >= 0; --k)
        {
            const float room = std::max (0.f, limit - hz[k]);
            float lin = 0.f;
            for (int c = 0; c < kConns; ++c)
                if (al.conn[c] && kConnDst[c] == k) lin += idxNow[kConnSrc[c]] * dev[kConnSrc[c]];
            float s = 1.f;
            if (lin > 0.f)
                for (int it = 0; it < 5; ++it)
                {
                    float tail = 0.f;
                    for (int c = 0; c < kConns; ++c)
                        if (al.conn[c] && kConnDst[c] == k)
                            tail = std::max (tail, 3.3f * std::sqrt (std::sqrt (idxNow[kConnSrc[c]] * s)) * dev[kConnSrc[c]]);
                    const float total = s * lin + tail;
                    if (total <= room) break;
                    s *= room / total;
                }
            scale[k] = s;
            dev[k] = hz[k] + s * lin;
        }

        int carriers = 0;
        for (int k = 0; k < kOps; ++k) carriers += al.carrier[k];
        static constexpr float kCarrierNorm[5] = { 1.f, 1.f, 0.758f, 0.644f, 0.574f };     // n^-0.4: similar loudness, bounded peak
        const float norm = kOutGain * kCarrierNorm[carriers];
        for (int c = 0; c < kConns; ++c)
            mT[c] = al.conn[c] ? index[kConnSrc[c]] * velIdx * scale[kConnDst[c]] * kRadToUnit : 0.f;
        for (int k = 0; k < kOps; ++k) aT[k] = al.carrier[k] ? carAmp[k] * norm : 0.f;
        fbT = fbBeta * 0.5f * kRadToUnit;
    }

    void gridUpdate()
    {
        uint32_t incT[kOps]; float hz[kOps], mT[kConns], aT[kOps], fbT;
        if (snap)
        {
            // first sample of a note: everything starts on its target
            incsFor (pe, incT, hz);
            gainTargets (hz, mT, aT, fbT);
            for (int k = 0; k < kOps; ++k) { inc[k] = incTarget[k] = incT[k]; amp[k] = ampNext[k] = aT[k]; }
            for (int c = 0; c < kConns; ++c) m[c] = mNext[c] = mT[c];
            fbG = fbNext = fbT;
            snap = false;
        }
        else
        {
            for (int k = 0; k < kOps; ++k) { inc[k] = incTarget[k]; amp[k] = ampNext[k]; }
            for (int c = 0; c < kConns; ++c) m[c] = mNext[c];
            fbG = fbNext;
        }
        // where everything should be one grid step from now
        pe *= peCoef; pe = undenorm (pe);
        incsFor (pe, incT, hz);
        gainTargets (hz, mT, aT, fbT);
        const float inv = 1.f / (float) grid;
        const int subSteps = grid * os;
        for (int k = 0; k < kOps; ++k)
        {
            incTarget[k] = incT[k];
            incStep[k] = (int32_t) (((int64_t) incT[k] - (int64_t) inc[k]) / subSteps);
            ampNext[k] = amp[k] + smoothCoef * (aT[k] - amp[k]);
            ampStep[k] = (ampNext[k] - amp[k]) * inv;
        }
        for (int c = 0; c < kConns; ++c)
        {
            mNext[c] = m[c] + smoothCoef * (mT[c] - m[c]);
            if (std::fabs (mNext[c]) < 1.f && mT[c] == 0.f) mNext[c] = 0.f;
            mStep[c] = (mNext[c] - m[c]) * inv;
        }
        fbNext = fbG + smoothCoef * (fbT - fbG);
        if (std::fabs (fbNext) < 1.f && fbT == 0.f) fbNext = 0.f;
        fbStep = (fbNext - fbG) * inv;
    }

    float sr = 48000.f, rate = 96000.f;
    int   os = 2, grid = 8;
    const float* sine = nullptr;
    bool  active = false, gate = false, done = false, snap = true;
    float vel = 1.f, noteCur = 60.f;

    // parameters (physical), refreshed in control()
    float lastE[kEngineParams];
    int   algo = 0;
    float fbBeta = 0.f, penv = 0.f, peCoef = 0.9f;
    float ratio[kOps] = { 1.f, 1.f, 1.f, 1.f }, index[kOps] = {}, carAmp[kOps] = {}, decCoef[kOps] = { 1.f, 1.f, 1.f, 1.f };

    // running state
    uint32_t ph[kOps] = {}, inc[kOps] = {}, incTarget[kOps] = {};
    int32_t  incStep[kOps] = {};
    float env[kOps] = {}, attack = 1.f, atkInc = 0.03f, pe = 0.f;
    float m[kConns] = {}, mStep[kConns] = {}, mNext[kConns] = {};
    float amp[kOps] = {}, ampStep[kOps] = {}, ampNext[kOps] = {};
    float fbG = 0.f, fbStep = 0.f, fbNext = 0.f, fb1 = 0.f, fb2 = 0.f, smoothCoef = 0.1f;
    int   gridLeft = 0, sinceOn = 0;
    Oversampler2x dn;
    DcBlocker dc;
};

std::unique_ptr<EngineBase> createFmEngine() { return std::make_unique<FmEngine>(); }

} // namespace atm
