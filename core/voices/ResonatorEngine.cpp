// Resonator engine: an exciter driving a tuned resonator.
//
//   exciter   Impulse : one-sample click through a critically damped 2-pole low-pass (Exc.Color = cutoff)
//             Noise   : white-noise burst, exponential decay (Exc.Decay), same low-pass
//             Mallet  : raised-cosine force pulse whose contact time is Exc.Decay shortened by
//                       Exc.Color and (through Hardness) by velocity, plus a little contact noise
//             Bow     : no strike; level-regulated friction noise while the gate is held
//             Sustain adds the same regulated noise to any exciter while the gate is held.
//
//   String    single-delay-loop waveguide (extended Karplus-Strong): 5th-order Lagrange
//             fractional delay -> one-pole loss filter -> four first-order allpasses (stiffness /
//             dispersion) -> loop gain. Position is a comb pickup; the pickup signal is leaky-
//             integrated so an impulsive strike yields the pulse wave of a plucked string.
//   Tube      the same loop with an inverting reflection (odd harmonics, half-length delay);
//             Structure cross-fades in a full-length non-inverting path, morphing the closed
//             cylinder (odd harmonics) into an open/conical bore (all harmonics).
//   Bar, Bell, Membrane, Glass
//             modal synthesis: up to 24 coupled-form (complex one-pole rotation) resonators
//             with frequency ratios from the physics, per-mode decay, position weighting
//             sin(pi n x) and per-mode panning.
//
// The delay length is compensated for the phase delay of every filter in the loop, so the
// fundamental stays in tune for any damping / stiffness setting. Pitch (glide, pitch envelope)
// is re-evaluated on a fixed internal 1/3 ms grid counted from note-on, with per-sample ramps of
// the delay lengths, so the output does not depend on how the host slices its buffers.
//
// Parameters without effect: Partials for String/Tube; Exc.Decay for the Impulse exciter.
// Structure 0.2 is the physically "true" ratio set for the modal models.
//
// Original implementation. Background: K. Karplus & A. Strong (1983); D. Jaffe & J. O. Smith,
// "Extensions of the Karplus-Strong plucked-string algorithm" (1983); J. O. Smith, "Physical
// Audio Signal Processing" (waveguides, allpass dispersion, Lagrange interpolation); mode
// ratios after N. Fletcher & T. Rossing, "The Physics of Musical Instruments".
#include "EngineBase.h"
#include "../model/ParamTables.h"
#include "../dsp/Filters.h"
#include <vector>

namespace atm {

namespace {

enum P { PModel, PExciter, PExcDecay, PExcColor, PDecay, PDamp, PStruct, PPosition, PSpread, PBow, PPenv, PPenvT, PPartials, PHard };
enum M { String, Tube, Bar, Bell, Membrane, Glass };
enum X { Impulse, NoiseBurst, Mallet, Bow };

constexpr int    kMaxModes = 24;
constexpr double kMinHz    = 8.0;
constexpr int    kDispersion = 4;          // allpass sections in the string loop
constexpr float  kNoiseSigma = 0.57735f;   // rms of the uniform noise source

// Free-free beam: (beta_n / beta_1)^2 with cos(b) cosh(b) = 1.
const float kBarRatios[kMaxModes] = {
    1.0000f, 2.7565f, 5.4039f, 8.9330f, 13.3443f, 18.6379f, 24.8138f, 31.8719f, 39.8123f, 48.6350f, 58.3399f, 68.9271f,
    80.3966f, 92.7483f, 105.9823f, 120.0986f, 135.0971f, 150.9779f, 167.7410f, 185.3863f, 203.9139f, 223.3238f, 243.6159f, 264.7903f };
// Ideal circular membrane: zeros of the Bessel functions J_m, sorted, relative to the (0,1) mode.
const float kMembraneRatios[kMaxModes] = {
    1.0000f, 1.5933f, 2.1355f, 2.2954f, 2.6531f, 2.9173f, 3.1555f, 3.5001f, 3.5985f, 3.6475f, 4.0589f, 4.1317f,
    4.2304f, 4.6010f, 4.6101f, 4.8319f, 4.9033f, 5.0836f, 5.1308f, 5.4121f, 5.5404f, 5.5531f, 5.6508f, 5.9765f };
// Church bell relative to the prime (strike note): hum, prime, tierce, quint, nominal, then the upper partials.
const float kBellRatios[kMaxModes] = {
    0.5000f, 1.0000f, 1.1830f, 1.5060f, 2.0000f, 2.5140f, 2.6620f, 3.0110f, 4.1660f, 5.4330f, 6.7960f, 8.2150f,
    9.7000f, 11.2400f, 12.8300f, 14.4700f, 16.1500f, 17.8700f, 19.6300f, 21.4200f, 23.2400f, 25.0900f, 26.9700f, 28.8700f };
const float kBellAmps[8] = { 0.70f, 1.00f, 0.85f, 0.45f, 1.00f, 0.60f, 0.40f, 0.70f };
// Glass (wine-glass ring modes), each split into a slowly beating doublet.
const float kGlassBase[kMaxModes / 2] = { 1.0000f, 2.3200f, 4.2500f, 6.6300f, 9.3800f, 12.9600f, 16.9700f, 21.5300f, 26.6300f, 32.2600f, 38.4400f, 45.1600f };
const float kGlassSplit[kMaxModes / 2] = { 0.0031f, 0.0019f, 0.0043f, 0.0013f, 0.0027f, 0.0037f, 0.0011f, 0.0023f, 0.0041f, 0.0017f, 0.0029f, 0.0035f };

inline float softLimit (float x)                 // linear to 0 dBFS, then a soft knee to +6 dBFS
{
    const float a = std::fabs (x);
    if (a <= 1.f) return x;
    return std::copysign (1.f + fastTanh (a - 1.f), x);
}

} // namespace

class ResonatorEngine final : public EngineBase
{
public:
    void prepare (double sampleRate) override
    {
        sr = sampleRate;
        tickLen = 16 * std::max (1, (int) std::lround (sr / 48000.0));
        int n = 64;
        while (n < (int) (sr / kMinHz) + 64) n <<= 1;
        buf.assign ((size_t) n, 0.f);
        mask = (unsigned) n - 1u;
        for (int k = 0; k < kMaxModes; ++k)
            panPattern[k] = (k == 0) ? 0.f : ((k & 1) ? -1.f : 1.f) * (0.35f + 0.65f * hashToFloat (hash32 ((uint32_t) k * 7919u + 13u)));
        envRel = (float) std::exp (-6.9078 / (0.25 * sr));
        drvCoef = (float) (1.0 - std::exp (-1.0 / (0.004 * sr)));
        outHp = (float) (1.0 - std::exp (-kTau * 12.0 / sr));
        bufDirty = true;
        reset();
    }

    void reset() override
    {
        active = false; gate = false;
        clearState();
    }

    void noteOn (const EngineNote& n, const float* e) override
    {
        std::copy (e, e + kEngineParams, pNorm);
        noteIn = n.note;
        if (n.legato && active) return;                 // slide: the resonator keeps ringing, pitch follows control()

        clearState();
        const ParamDesc* d = kResonatorParams;
        model   = d[PModel].toIndex (e[PModel]);
        exciter = d[PExciter].toIndex (e[PExciter]);
        vel = clamp01 (n.velocity);
        strikeNoise.seed (n.seed * 2u + 1u);
        susNoise.seed (n.seed * 2u + 7919u);
        mapParams();
        penvLevel = (std::fabs (penv) > 1.0e-4f) ? 1.f : 0.f;
        noteCur = noteIn + penv * penvLevel;
        updateResonator (true);
        setupStrike();
        gate = true; active = true;
        susOn = susLevel > 0.f;
        gateEnv = 0.f; drive = driveMax * 0.5f; driveTarget = driveMax;
        untilTick = tickLen;
        elapsed = 0;
    }

    void noteOff() override { gate = false; }

    void control (const float* e, float note) override
    {
        std::copy (e, e + kEngineParams, pNorm);
        noteIn = note;
    }

    void render (float* outL, float* outR, int n) override
    {
        if (! active) { for (int i = 0; i < n; ++i) { outL[i] = 0.f; outR[i] = 0.f; } return; }
        int done = 0;
        while (done < n)
        {
            if (untilTick <= 0)
            {
                tick(); untilTick = tickLen;
                if (! active) { for (int i = done; i < n; ++i) { outL[i] = 0.f; outR[i] = 0.f; } return; }
            }
            const int m = std::min (n - done, untilTick);
            float x[kMaxSlice];
            genExciter (x, m);
            if (model <= Tube) processWave (x, outL + done, outR + done, m);
            else               processModal (x, outL + done, outR + done, m);
            untilTick -= m; done += m; elapsed += m;
        }
    }

    bool isStereo() const override { return true; }
    bool finished() const override { return ! active; }

private:
    // ------------------------------------------------------------------ state management
    void clearState()
    {
        if (bufDirty) { std::fill (buf.begin(), buf.end(), 0.f); bufDirty = false; }
        wp = 0;
        for (int k = 0; k < kMaxModes; ++k) { re[k] = im[k] = 0.f; cr[k] = si[k] = gin[k] = gl[k] = gr[k] = 0.f; }
        nProc = 0;
        lpz = 0.f; intL = intR_ = 0.f;
        for (auto& a : ap) a.reset();
        excLP.reset(); susLP.reset(); susHP.reset();
        strikeLeft = 0; strikeFirst = false; nEnv = 0.f; mPos = 0;
        gateEnv = 0.f; drive = 0.f; driveTarget = 0.f; susOn = false;
        env = 0.f; hpL = hpR = 0.f;
        dCur = dTgt = 8.0; dInc = 0.0; d2Cur = d2Tgt = 16.0; d2Inc = 0.0; perCur = perTgt = 16.0; perInc = 0.0;
        std::memset (pMapped, 0xff, sizeof (pMapped));
        lastStruct = -1.f;
    }

    void mapParams()
    {
        const ParamDesc* d = kResonatorParams;
        excDecay = d[PExcDecay].toPhys (pNorm[PExcDecay]);
        excColor = d[PExcColor].toPhys (pNorm[PExcColor]);
        decay    = d[PDecay].toPhys (pNorm[PDecay]);
        damp     = d[PDamp].toPhys (pNorm[PDamp]);
        strct    = d[PStruct].toPhys (pNorm[PStruct]);
        position = d[PPosition].toPhys (pNorm[PPosition]);
        spread   = d[PSpread].toPhys (pNorm[PSpread]);
        bow      = d[PBow].toPhys (pNorm[PBow]);
        penv     = d[PPenv].toPhys (pNorm[PPenv]);
        penvT    = d[PPenvT].toPhys (pNorm[PPenvT]);
        partials = clamp ((int) std::lround (d[PPartials].toPhys (pNorm[PPartials])), 2, kMaxModes);
        hard     = d[PHard].toPhys (pNorm[PHard]);
        std::memcpy (pMapped, pNorm, sizeof (pMapped));

        // brightness of the excitation: colour, pushed up or down by velocity through Hardness
        bright = clamp01 (excColor + hard * 0.8f * (vel - 0.75f));
        colorHz = std::min (120.f * std::pow (150.f, bright), (float) sr * 0.45f);
        susLP.setQ (colorHz, 0.5f, (float) sr);
        susLevel = (exciter == Bow) ? 0.6f + 0.4f * bow : bow;
        // bow attack follows Exc.Decay; the Sustain add-on of the other exciters fades in quickly
        const double atk = (exciter == Bow) ? std::max (0.002, (double) excDecay) : 0.004;
        gateAtk = (float) (1.0 - std::exp (-3.0 / (atk * sr)));
        gateRel = (float) (1.0 - std::exp (-6.9078 / (0.04 * sr)));
        beta = 0.02f + 0.96f * position;
    }

    static float colourResponse (float f, float fc) { const float x = f / fc; return 1.f / (1.f + x * x); }

    // Magnitude of the strike spectrum at frequency f for a unit-area (impulse-like) excitation.
    float strikeSpectrum (float f) const
    {
        switch (exciter)
        {
            case Mallet:
            {
                const float x = f * malletTime;
                float p;
                if (x < 1.0e-3f) p = 1.f;
                else if (std::fabs (x - 1.f) < 1.0e-3f) p = 0.5f;
                else p = std::fabs (std::sin (kPiF * x) / (kPiF * x * (1.f - x * x)));
                return std::sqrt (p * p + malletNoiseRel * malletNoiseRel);
            }
            default: return colourResponse (f, colorHz);
        }
    }

    // ------------------------------------------------------------------ resonator coefficients
    void updateResonator (bool snap)
    {
        f0 = 440.0 * std::exp2 (((double) noteCur - 69.0) / 12.0);
        if (model <= Tube) computeWave (snap); else computeModes();
    }

    void computeWave (bool snap)
    {
        const bool tube = model == Tube;
        const double f = clamp (f0, kMinHz, sr / 12.0);
        const double w0 = kTau * f / sr;
        const double trips = tube ? 2.0 * f : f;                    // loop round trips per second
        const double base = 6.9078 / ((double) decay * trips);      // nepers lost per trip by the fundamental

        // loss filter: one-pole low-pass whose corner follows pitch and decay so that
        // T60(n) ~ decay / (1 + c (n^2 - 1)) for the low harmonics
        const double d3 = (double) damp * damp * damp;
        const double c = tube ? 0.15 + 0.85 * d3 : d3;
        double a = 0.0;
        if (c > 1.0e-6) a = std::min (0.9999, std::exp (-kTau * (f / std::sqrt (2.0 * c * base)) / sr));
        const double magLP = (1.0 - a) / std::sqrt (1.0 - 2.0 * a * std::cos (w0) + a * a);
        const double tauLP = std::atan2 (a * std::sin (w0), 1.0 - a * std::cos (w0)) / w0;

        const double N = tube ? sr / (2.0 * f) : sr / f;
        double p = 0.0, tauAP = 0.0;
        if (! tube)
        {
            // stiffness: allpass corner at f0 / q; q -> 0 leaves four plain unit delays
            double q = 0.42 * std::pow ((double) strct, 1.2);
            for (int it = 0; it < 8; ++it)
            {
                p = 0.0;
                if (q > 1.0e-4)
                {
                    const double wc = w0 / q;
                    if (wc < 0.5 * kPi) { const double t = std::tan (0.5 * wc); p = (1.0 - t) / (1.0 + t); }
                }
                tauAP = kDispersion * (1.0 + 2.0 * std::atan2 (p * std::sin (w0), 1.0 - p * std::cos (w0)) / w0);
                if (N - tauLP - tauAP >= 3.0) break;
                q *= 0.6;                                         // not enough delay left: back the stiffness off
            }
        }
        const double D = std::max (3.0, N - tauLP - tauAP);

        double g;
        if (tube)
        {
            mix = strct;
            const double y = std::exp (base);
            g = 1.0 / ((1.0 - mix) * y + mix * y * y);
        }
        else g = std::exp (-base);
        g = std::min (0.999995, g / magLP);

        loopG = (float) g;
        lpA = (float) (1.0 - a);
        intR = (float) std::exp (-kTau * (f / 8.0) / sr);
        apP = (float) p;
        const double per = sr / f;
        dTgt = D; d2Tgt = std::max (3.0, 2.0 * N - tauLP); perTgt = per;
        if (snap) { dCur = dTgt; d2Cur = d2Tgt; perCur = perTgt; dInc = d2Inc = perInc = 0.0; setLagrange (dCur, i1, h1); setLagrange (d2Cur, i2, h2); }
        else
        {
            const double inv = 1.0 / (double) tickLen;
            dInc = (dTgt - dCur) * inv; d2Inc = (d2Tgt - d2Cur) * inv; perInc = (perTgt - perCur) * inv;
        }
        // regulated-noise authority: enough to reach the target level in roughly 20 ms
        const float frac = clamp (1.5708f * colorHz / (float) sr, 0.01f, 1.f);
        driveMax = 0.7f / (kNoiseSigma * std::sqrt (frac) * std::sqrt (std::max (1.f, (float) f * 0.02f)) * (float) per * 0.25f);
        waveHz = (float) f;
        susHP.setQ ((float) f * 0.5f, 0.7071f, (float) sr);
    }

    void computeModes()
    {
        const float fsr = (float) sr;
        const float f = (float) clamp (f0, kMinHz, sr * 0.4);
        if (strct != lastStruct)
        {
            // structure: ratio exponent 0.5 (compressed) .. 1 (physical, at 0.2) .. 2 (stretched)
            const float ex = strct < 0.2f ? 0.5f + 2.5f * strct : 1.f + (strct - 0.2f) * 1.25f;
            for (int k = 0; k < kMaxModes; ++k) ratio[k] = std::pow (baseRatio (k), ex);
            lastStruct = strct;
        }
        static constexpr float kDampMax[6] = { 0.f, 0.f, 5.f, 2.5f, 4.f, 0.5f };
        static constexpr float kTilt[6]    = { 0.f, 0.f, 0.6f, 0.25f, 0.5f, 0.5f };
        const float dmp = kDampMax[model] * damp * damp;
        const float rmin = ratio[0];
        const int n = partials;
        nProc = std::max (nProc, (n + 3) & ~3);
        float wsq = 0.f;
        for (int k = 0; k < nProc; ++k)
        {
            const float rho = ratio[k] / rmin;
            const float t60 = std::max (0.002f, decay / (1.f + dmp * (rho - 1.f)));
            const float r = std::exp (-6.9078f / (t60 * fsr));
            const float fk = f * ratio[k];
            const float x = fk / fsr;
            const float fade = clamp ((0.45f - x) * 20.f, 0.f, 1.f);     // modes fade out between 0.40 and 0.45 fs
            const float th = kTauF * std::min (x, 0.45f);
            cr[k] = r * std::cos (th); si[k] = r * std::sin (th);
            float w = 0.f;
            if (k < n)
            {
                const int order = (model == Glass) ? k / 2 + 1 : k + 1;
                float amp = std::pow (rho, -kTilt[model]);
                if (model == Bell && k < 8) amp *= kBellAmps[k];
                if (model == Glass && (k & 1)) amp *= 0.6f;
                w = std::sin (kPiF * (float) order * beta) * amp * fade;
            }
            gin[k] = w;
            const float pan = spread * panPattern[k];
            const float ang = 0.7853982f * (1.f + pan);
            gl[k] = 1.41421356f * std::cos (ang) * fade;
            gr[k] = 1.41421356f * std::sin (ang) * fade;
            modeHz[k] = fk;
            const float s = w * colourResponse (fk, colorHz);
            wsq += s * s;
        }
        driveMax = 0.7f / (kNoiseSigma * std::max (std::sqrt (wsq), 0.02f) * std::sqrt (0.02f * fsr));
        susHP.setQ (f * ratio[0] * 0.5f, 0.7071f, fsr);
    }

    float baseRatio (int k) const
    {
        switch (model)
        {
            case Bar:      return kBarRatios[k];
            case Bell:     return kBellRatios[k];
            case Membrane: return kMembraneRatios[k];
            default:       return kGlassBase[k / 2] * ((k & 1) ? 1.f + kGlassSplit[k / 2] : 1.f);
        }
    }

    // ------------------------------------------------------------------ strike set-up (note-on only)
    void setupStrike()
    {
        const float fsr = (float) sr;
        const bool wave = model <= Tube;
        const float fPitch = wave ? waveHz : (float) clamp (f0, kMinHz, sr * 0.4);
        excLP.setQ (colorHz, 0.5f, fsr);
        strikeGain = 0.f; strikeLeft = 0;

        // contact time: Exc.Decay shortened by brightness, never longer than one period of the note
        malletTime = clamp (excDecay * std::exp2 (0.5f - 6.f * bright), 2.f / fsr, 1.f / fPitch);
        const float mLen = std::max (2.f, malletTime * fsr);
        malletNoiseRel = 0.f;
        float areaNorm = 1.f;                                 // gain that makes the strike impulse-equivalent (unit area)
        float width = 1.f;                                    // equivalent pulse width in samples (area / peak)
        switch (exciter)
        {
            case Impulse:
            {
                // impulse response peak of the colour filter (bounded search)
                Svf f = excLP; f.reset();
                float pk = std::fabs (f.lp (1.f));
                const int maxN = (int) (2.5f * fsr / (kTauF * colorHz)) + 8;
                for (int i = 0; i < maxN; ++i) pk = std::max (pk, std::fabs (f.lp (0.f)));
                width = 1.f / std::max (pk, 1.0e-4f);
                areaNorm = 1.f;
                strikeFirst = true;
                strikeLeft = (int) (20.f * fsr / (kTauF * colorHz)) + 16;
                break;
            }
            case NoiseBurst:
            {
                nCoef = std::exp (-6.9078f / std::max (4.f, excDecay * fsr));
                nEnv = 1.f;
                // energy a resonance collects from the burst while it decays itself
                const float rdec = std::exp (-6.9078f / (decay * fsr));
                const float sumEnv2 = 1.f / (1.f - nCoef * nCoef * rdec * rdec);
                areaNorm = 1.f / (kNoiseSigma * std::sqrt (sumEnv2));
                strikeLeft = (int) (excDecay * fsr * 2.f) + (int) (20.f * fsr / (kTauF * colorHz)) + 16;
                break;
            }
            case Mallet:
            {
                mPos = 0; mInvLen = 1.f / mLen;
                strikeLeft = (int) std::ceil (mLen);
                areaNorm = 2.f / mLen;
                width = 0.5f * mLen;
                // contact noise relative to the pulse, as seen by a mode (roughly sample-rate independent)
                mNoise = (0.1f + 0.3f * bright) * std::sqrt (fsr / 48000.f);
                malletNoiseRel = mNoise * kNoiseSigma * 1.2247f / std::sqrt (mLen);
                break;
            }
            default: break;                                    // Bow: no strike
        }
        if (exciter == Bow) return;

        if (wave)
        {
            // the pickup signal is integrated, so a unit-area strike gives a pulse wave of unit height
            // (a pulse wider than the pickup spacing partly cancels: compensate, within limits)
            const float per = fsr / fPitch;
            const float overlap = std::min (1.f, std::min (beta, 1.f - beta) * per / width);
            strikeGain = (exciter == NoiseBurst ? 0.55f : 0.7f / std::sqrt (std::max (overlap, 0.1f))) * areaNorm;
        }
        else
        {
            // expected peak of the mode sum; a downward pitch bend moves the modes into the strike's band
            const float bend = std::min (1.f, std::exp2 (-penv * (1.f / 12.f)));
            float sum = 0.f, sq = 0.f, flat = 0.f;
            for (int k = 0; k < nProc; ++k)
            {
                const float w = std::fabs (gin[k]) * std::max (gl[k], gr[k]);
                const float a = w * strikeSpectrum (modeHz[k] * bend);
                sum += a; sq += a * a; flat += w;
            }
            float est = (exciter == NoiseBurst) ? 1.6f * std::sqrt (sq) : 0.75f * sum + 0.25f * std::sqrt (sq);
            est = std::max (est, 0.12f * flat);                 // never boost a badly matched strike by more than ~18 dB
            strikeGain = 0.7f * areaNorm / std::max (est, 0.02f);
        }
    }

    // ------------------------------------------------------------------ control tick (fixed grid from note-on)
    void tick()
    {
        flushState();
        const bool pchg = std::memcmp (pNorm, pMapped, sizeof (pNorm)) != 0;
        if (pchg) mapParams();
        if (penvLevel > 0.f)
        {
            penvLevel *= std::exp (-6.9078f * (float) tickLen / (std::max (penvT, 0.001f) * (float) sr));
            if (penvLevel < 1.0e-4f) penvLevel = 0.f;
        }
        // finish the previous ramp exactly
        if (model <= Tube) { dCur = dTgt; d2Cur = d2Tgt; perCur = perTgt; if (dInc != 0.0 || d2Inc != 0.0) { setLagrange (dCur, i1, h1); setLagrange (d2Cur, i2, h2); } dInc = d2Inc = perInc = 0.0; }
        const float note = noteIn + penv * penvLevel;
        if (pchg || note != noteCur) { noteCur = note; updateResonator (false); }

        // level-regulated sustain: push harder the further the output is below the target
        const float target = 0.65f * susLevel;
        susOn = (gate && susLevel > 0.f) || gateEnv > 1.0e-5f;
        if (! susOn) gateEnv = 0.f;
        driveTarget = (target > 1.0e-4f) ? driveMax * clamp (3.f * (1.f - env / target), 0.f, 1.f) : 0.f;

        // free the voice once everything has died away
        if (! susOn && strikeLeft <= 0 && env < 2.0e-5f && elapsed > tickLen * 4) active = false;
        if (! (env < 64.f)) { clearState(); active = false; }           // numerical safety net
    }

    // ------------------------------------------------------------------ exciter
    void genExciter (float* x, int m)
    {
        for (int i = 0; i < m; ++i) x[i] = 0.f;
        if (strikeLeft > 0)
        {
            const int c = std::min (m, strikeLeft);
            switch (exciter)
            {
                case Impulse:
                    for (int i = 0; i < c; ++i) { x[i] = excLP.lp (strikeFirst ? strikeGain : 0.f); strikeFirst = false; }
                    break;
                case NoiseBurst:
                    for (int i = 0; i < c; ++i) { x[i] = excLP.lp (strikeNoise.next() * nEnv) * strikeGain; nEnv *= nCoef; }
                    break;
                case Mallet:
                    for (int i = 0; i < c; ++i)
                    {
                        const float p = 0.5f - 0.5f * std::cos (kTauF * ((float) mPos + 0.5f) * mInvLen);
                        x[i] = p * (1.f + mNoise * strikeNoise.next()) * strikeGain;
                        ++mPos;
                    }
                    break;
                default: break;
            }
            strikeLeft -= c;
        }
        if (susOn)
        {
            for (int i = 0; i < m; ++i)
            {
                gateEnv += gate ? gateAtk * (susLevel - gateEnv) : -gateRel * gateEnv;
                drive += (driveTarget < drive ? drvCoef * 4.f : drvCoef) * (driveTarget - drive);
                x[i] += susHP.hp (susLP.lp (susNoise.next())) * drive * (gateEnv / std::max (susLevel, 1.0e-3f));
            }
        }
    }

    // ------------------------------------------------------------------ modal bank
    void processModal (const float* x, float* outL, float* outR, int m)
    {
        const int n = nProc;
        float e = env;
        for (int i = 0; i < m; ++i)
        {
            const float in = x[i];
            float accL[4] = { 0.f, 0.f, 0.f, 0.f }, accR[4] = { 0.f, 0.f, 0.f, 0.f };
            for (int k = 0; k < n; k += 4)
                for (int j = 0; j < 4; ++j)
                {
                    const float a = re[k + j], b = im[k + j];
                    const float na = cr[k + j] * a - si[k + j] * b + gin[k + j] * in;
                    const float nb = si[k + j] * a + cr[k + j] * b;
                    re[k + j] = na; im[k + j] = nb;
                    accL[j] += gl[k + j] * nb; accR[j] += gr[k + j] * nb;
                }
            const float l = (accL[0] + accL[1]) + (accL[2] + accL[3]);
            const float r = (accR[0] + accR[1]) + (accR[2] + accR[3]);
            const float a = 0.5f * std::fabs (l + r);
            e = a > e ? a : e * envRel;
            outL[i] = softLimit (l); outR[i] = softLimit (r);
        }
        env = e;
    }

    // ------------------------------------------------------------------ waveguide
    // Lagrange coefficients for a delay of d samples (d >= 3): taps at delays i-2 .. i+3.
    static void setLagrange (double d, int& i, float* h)
    {
        i = (int) d;
        const float x = (float) (d - (double) i) + 2.f;
        const float d0 = x, d1 = x - 1.f, d2 = x - 2.f, d3 = x - 3.f, d4 = x - 4.f, d5 = x - 5.f;
        h[0] = d1 * d2 * d3 * d4 * d5 * (-1.f / 120.f);
        h[1] = d0 * d2 * d3 * d4 * d5 * (1.f / 24.f);
        h[2] = d0 * d1 * d3 * d4 * d5 * (-1.f / 12.f);
        h[3] = d0 * d1 * d2 * d4 * d5 * (1.f / 12.f);
        h[4] = d0 * d1 * d2 * d3 * d5 * (-1.f / 24.f);
        h[5] = d0 * d1 * d2 * d3 * d4 * (1.f / 120.f);
    }
    inline float readLagrange (int i, const float* h) const
    {
        const unsigned rp = wp - (unsigned) i + 2u;
        return h[0] * buf[rp & mask] + h[1] * buf[(rp - 1u) & mask] + h[2] * buf[(rp - 2u) & mask]
             + h[3] * buf[(rp - 3u) & mask] + h[4] * buf[(rp - 4u) & mask] + h[5] * buf[(rp - 5u) & mask];
    }
    // After the write: delay 0 = the sample just written. Requires d >= 1.
    inline float readTap (float d) const
    {
        const int i = (int) d; const float f = d - (float) i;
        const unsigned rp = wp - 1u - (unsigned) i;
        return hermite4 (buf[(rp + 1u) & mask], buf[rp & mask], buf[(rp - 1u) & mask], buf[(rp - 2u) & mask], f);
    }

    void processWave (const float* x, float* outL, float* outR, int m)
    {
        const bool tube = model == Tube;
        const bool ramp = dInc != 0.0 || d2Inc != 0.0;
        bufDirty = true;
        const float g = loopG, a = lpA, p = apP, mx = mix, side = 0.5f * spread, ir = intR;
        float e = env;
        for (int i = 0; i < m; ++i)
        {
            if (ramp)
            {
                dCur += dInc; perCur += perInc;
                setLagrange (dCur, i1, h1);
                if (tube) { d2Cur += d2Inc; setLagrange (d2Cur, i2, h2); }
            }
            float v = readLagrange (i1, h1);
            if (tube) v = mx * readLagrange (i2, h2) - (1.f - mx) * v;
            lpz += a * (v - lpz); v = lpz;
            if (! tube)
                for (auto& s : ap) { const float y = s.x1 - p * (v - s.y1); s.x1 = v; s.y1 = y; v = y; }
            const float w = clamp (v * g + x[i], -16.f, 16.f);
            buf[wp & mask] = w; ++wp;

            const float per = (float) perCur;
            const float mid = w - readTap (std::max (1.f, beta * per));
            const float sd = side * (readTap (std::max (1.f, 0.113f * per)) - readTap (std::max (1.f, 0.187f * per)));
            // leaky integration (corner at f0/8): velocity-like pulses become the displacement-like pulse wave
            intL = intL * ir + (mid + sd); intR_ = intR_ * ir + (mid - sd);
            float l = intL, r = intR_;
            hpL += outHp * (l - hpL); l -= hpL;
            hpR += outHp * (r - hpR); r -= hpR;
            const float am = 0.5f * std::fabs (l + r);
            e = am > e ? am : e * envRel;
            outL[i] = softLimit (l); outR[i] = softLimit (r);
        }
        env = e;
    }

    // Denormal flushing happens on the control grid (not at the end of each host slice), so the
    // output stays bit-identical for any slicing.
    void flushState()
    {
        for (int k = 0; k < nProc; ++k) { re[k] = undenorm (re[k]); im[k] = undenorm (im[k]); }
        lpz = undenorm (lpz); intL = undenorm (intL); intR_ = undenorm (intR_); hpL = undenorm (hpL); hpR = undenorm (hpR);
        for (auto& s : ap) { s.x1 = undenorm (s.x1); s.y1 = undenorm (s.y1); }
    }

    // ------------------------------------------------------------------ members
    double sr = 48000.0;
    int    tickLen = 16, untilTick = 0, elapsed = 0;
    int    model = String, exciter = Mallet;
    bool   active = false, gate = false;
    float  vel = 1.f;
    float  pNorm[kEngineParams] = {}, pMapped[kEngineParams] = {};
    float  noteIn = 60.f, noteCur = 60.f;
    double f0 = 261.6;

    // mapped parameters
    float excDecay = 0.01f, excColor = 0.6f, decay = 1.5f, damp = 0.4f, strct = 0.2f, position = 0.3f, spread = 0.3f, bow = 0.f;
    float penv = 0.f, penvT = 0.05f, hard = 0.5f, bright = 0.6f, colorHz = 2000.f, beta = 0.3f;
    int   partials = 12;
    float penvLevel = 0.f;

    // exciter
    Noise strikeNoise, susNoise;
    Svf   excLP, susLP, susHP;
    int   strikeLeft = 0, mPos = 0;
    bool  strikeFirst = false;
    float strikeGain = 0.f, nEnv = 0.f, nCoef = 0.99f, mInvLen = 0.1f, mNoise = 0.f, malletTime = 0.001f, malletNoiseRel = 0.f;
    bool  susOn = false;
    float susLevel = 0.f, gateEnv = 0.f, gateAtk = 0.01f, gateRel = 0.001f;
    float drive = 0.f, driveTarget = 0.f, driveMax = 0.f, drvCoef = 0.01f;
    float env = 0.f, envRel = 0.999f;

    // modal bank
    alignas (16) float re[kMaxModes] = {}, im[kMaxModes] = {}, cr[kMaxModes] = {}, si[kMaxModes] = {}, gin[kMaxModes] = {}, gl[kMaxModes] = {}, gr[kMaxModes] = {};
    float ratio[kMaxModes] = {}, modeHz[kMaxModes] = {}, panPattern[kMaxModes] = {};
    float lastStruct = -1.f;
    int   nProc = 0;

    // waveguide
    std::vector<float> buf;
    unsigned mask = 0, wp = 0;
    bool   bufDirty = true;
    double dCur = 8.0, dTgt = 8.0, dInc = 0.0, d2Cur = 16.0, d2Tgt = 16.0, d2Inc = 0.0, perCur = 16.0, perTgt = 16.0, perInc = 0.0;
    int    i1 = 8, i2 = 16;
    float  h1[6] = {}, h2[6] = {};
    float  loopG = 0.f, lpA = 1.f, apP = 0.f, mix = 0.f, waveHz = 261.6f, intR = 0.99f;
    float  lpz = 0.f, intL = 0.f, intR_ = 0.f, hpL = 0.f, hpR = 0.f, outHp = 0.001f;
    Allpass1 ap[kDispersion];
};

std::unique_ptr<EngineBase> createResonatorEngine() { return std::make_unique<ResonatorEngine>(); }

} // namespace atm
