// Resonator bank: up to six tuned waveguide (comb) resonators per channel, pitched as a chord.
//
// Each resonator:   y[n] = in + limit (k * HP (LP (y[n - D])))      output = the filtered loop signal
//
//   * D is set from the exact phase of the two loop filters at the fundamental, so the pitch is
//     accurate; pitch changes glide over ~20 ms.
//   * LP = damping (one pole, never below 3x the fundamental so the fundamental itself survives).
//     The loop gain k is corrected for the filters' loss at the fundamental, so every resonator's
//     fundamental rings for `decay` seconds (RT60) whatever its pitch and damping; the harmonics
//     die faster as `damp` rises.
//   * HP (one pole at f/20) keeps the comb's 0 Hz resonance out of the way, which is what allows
//     k to exceed 1 when the damping filter is eating into the fundamental.
//   * A soft limiter in the loop bounds the level when a sustained tone sits on a resonance.
//
// Chord tables (semitones above the root, the first `count` are used):
//   Unison 0 +.1 -.1 +.2 -.2 +.05 | Octaves 0 12 -12 24 36 -24 | Fifth 0 7 12 19 24 -12
//   Minor 0 3 7 12 15 19 | Major 0 4 7 12 16 19 | Sus4 0 5 7 12 17 19 | Min7 0 3 7 10 12 15
//   Maj7 0 4 7 11 12 16 | Min9 0 3 7 10 14 17 | Whole 0 2 4 6 8 10 | Cluster 0 1 2 3 4 5
//   Fourths 0 5 10 15 20 25
// Notes that would land above ~10 kHz are folded down by octaves.
#include "Effect.h"
#include "../dsp/Delay.h"
#include "../model/ParamTables.h"

namespace atm {
namespace resofx {

enum { PNote, PChord, PDecay, PDamp, PDetune, PInput, PCount };
constexpr int kVoices = 6;
constexpr int kChordCount = 12;
constexpr float kChordTable[kChordCount][kVoices] = {
    { 0.f, 0.1f, -0.1f, 0.2f, -0.2f, 0.05f },     // Unison (a slightly chorused string course)
    { 0.f, 12.f, -12.f, 24.f, 36.f, -24.f },      // Octaves
    { 0.f, 7.f, 12.f, 19.f, 24.f, -12.f },        // Fifth
    { 0.f, 3.f, 7.f, 12.f, 15.f, 19.f },          // Minor
    { 0.f, 4.f, 7.f, 12.f, 16.f, 19.f },          // Major
    { 0.f, 5.f, 7.f, 12.f, 17.f, 19.f },          // Sus4
    { 0.f, 3.f, 7.f, 10.f, 12.f, 15.f },          // Min7
    { 0.f, 4.f, 7.f, 11.f, 12.f, 16.f },          // Maj7
    { 0.f, 3.f, 7.f, 10.f, 14.f, 17.f },          // Min9
    { 0.f, 2.f, 4.f, 6.f, 8.f, 10.f },            // Whole tone
    { 0.f, 1.f, 2.f, 3.f, 4.f, 5.f },             // Cluster
    { 0.f, 5.f, 10.f, 15.f, 20.f, 25.f },         // Fourths
};
constexpr float kMaxDetuneSt = 0.25f;    // +-25 cents
constexpr float kMinHz = 14.f;
constexpr float kLimit = 2.f;            // loop limiter ceiling

struct Res
{
    DelayLine line;
    float d = 100.f, dT = 100.f;         // delay (samples), smoothed and target
    float lpA = 1.f, hpA = 0.01f, k = 0.f;
    float lpAT = 1.f, hpAT = 0.01f, kT = 0.f;
    float lp = 0.f, hp = 0.f;
    float norm = 1.f, normT = 1.f;
};

class ResonatorFx final : public Effect
{
public:
    void prepare (double sampleRate) override
    {
        sr = (float) sampleRate;
        maxDelay = sr / kMinHz;
        for (auto& ch : res) for (auto& v : ch) v.line.prepare ((int) maxDelay + 16);
        kGlide = onePoleCoef (0.006f, sr);
        kSmooth = onePoleCoef (0.005f, sr);
        kAmp = onePoleCoef (0.008f, sr);
        reset();
    }

    void reset() override
    {
        for (auto& ch : res) for (auto& v : ch) { v.line.reset(); v.lp = v.hp = 0.f; }
        for (int v = 0; v < kVoices; ++v) { amp[v] = 0.f; live[v] = false; }
        first = true;
        cNote = -1.f; cChord = -1; cDecay = cDamp = cDetune = -1.f;
    }

    void setParams (const float* p, const FxContext&) override
    {
        const float note = kFxResonator[PNote].toPhys (p[PNote]);
        const int chord = clamp (kFxResonator[PChord].toIndex (p[PChord]), 0, kChordCount - 1);
        const float decay = kFxResonator[PDecay].toPhys (p[PDecay]);
        const float damp = clamp01 (p[PDamp]);
        const float detune = kFxResonator[PDetune].toPhys (p[PDetune]);
        tGain = dbToGain (kFxResonator[PInput].toPhys (p[PInput]));
        count = clamp ((int) std::lround (kFxResonator[PCount].toPhys (p[PCount])), 1, kVoices);
        tCountNorm = 1.f / std::sqrt ((float) count);

        if (note != cNote || chord != cChord || decay != cDecay || damp != cDamp || detune != cDetune)
        {
            cNote = note; cChord = chord; cDecay = decay; cDamp = damp; cDetune = detune;
            const float fcAbs = 20000.f * std::pow (0.01f, damp);
            for (int c = 0; c < 2; ++c)
                for (int v = 0; v < kVoices; ++v)
                    tune (res[c][v], note + kChordTable[chord][v] + (c ? detune : -detune) * kMaxDetuneSt, decay, fcAbs);
        }
        if (first)
        {
            first = false;
            gain = tGain; countNorm = tCountNorm;
            for (int v = 0; v < kVoices; ++v) if (v < count) { activate (v); amp[v] = 1.f; }
        }
        for (int v = 0; v < count; ++v) if (! live[v]) activate (v);
    }

    void process (float* l, float* r, int n, const FxContext&) override
    {
        float* io[2] = { l, r };
        float accL[32] = {}, accR[32] = {};
        float* acc[2] = { accL, accR };
        float g[32], cn[32];
        for (int i = 0; i < n; ++i)
        {
            gain += kSmooth * (tGain - gain); g[i] = gain;
            countNorm += kSmooth * (tCountNorm - countNorm); cn[i] = countNorm;
        }
        for (int v = 0; v < kVoices; ++v)
        {
            if (! live[v]) continue;
            const float ampT = v < count ? 1.f : 0.f;
            const float a0 = amp[v];
            for (int c = 0; c < 2; ++c)
            {
                Res& q = res[c][v];
                float a = a0;
                const float* in = io[c];
                float* out = acc[c];
                for (int i = 0; i < n; ++i)
                {
                    a += kAmp * (ampT - a);
                    q.d += kGlide * (q.dT - q.d);
                    q.lpA += kSmooth * (q.lpAT - q.lpA);
                    q.hpA += kSmooth * (q.hpAT - q.hpA);
                    q.k += kSmooth * (q.kT - q.k);
                    q.norm += kSmooth * (q.normT - q.norm);
                    const float yd = q.line.readHermite (q.d - 1.f);
                    q.lp = undenorm (q.lp + q.lpA * (yd - q.lp));
                    q.hp = undenorm (q.hp + q.hpA * (q.lp - q.hp));
                    const float s = q.lp - q.hp;
                    const float y = in[i] * g[i] * a + kLimit * softClip (q.k * s * (1.f / kLimit));
                    q.line.write (undenorm (y));
                    out[i] += s * a * q.norm;
                }
                if (c == 1) amp[v] = a;
            }
            if (ampT == 0.f && amp[v] < 1.0e-4f) kill (v);
        }
        for (int i = 0; i < n; ++i) { l[i] = accL[i] * cn[i]; r[i] = accR[i] * cn[i]; }
    }

private:
    void tune (Res& q, float st, float decay, float fcAbs)
    {
        const float fMax = std::min (10000.f, 0.2f * sr);
        float f = (float) midiToHz ((double) st);
        while (f > fMax) f *= 0.5f;
        f = std::max (f, kMinHz + 1.f);
        const float w = kTauF * f / sr;
        const float lpHz = std::min (std::max (fcAbs, 3.f * f), 0.45f * sr);
        const float al = 1.f - std::exp (-kTauF * lpHz / sr), bl = 1.f - al;
        const float ah = 1.f - std::exp (-kTauF * (f / 20.f) / sr), bh = 1.f - ah;
        const float cw = std::cos (w), sw = std::sin (w);
        // LP = al / (1 - bl z^-1);  HP = 1 - ah / (1 - bh z^-1) = (1 - ah - bh z^-1) / (1 - bh z^-1) = bh (1 - z^-1) / (1 - bh z^-1)
        const float lre = 1.f - bl * cw, lim = bl * sw;                  // LP denominator
        const float magL = al / std::sqrt (lre * lre + lim * lim);
        const float phL = -std::atan2 (lim, lre);
        const float hre = 1.f - bh * cw, him = bh * sw;                  // HP denominator
        const float nre = 1.f - cw, nim = sw;                            // HP numerator / bh
        const float magH = bh * std::sqrt ((nre * nre + nim * nim) / (hre * hre + him * him));
        const float phH = std::atan2 (nim, nre) - std::atan2 (him, hre);
        const float g = std::pow (10.f, -3.f / (f * std::max (decay, 0.01f)));
        q.kT = std::min (g / (magL * magH), 1.12f);
        q.lpAT = al; q.hpAT = ah;
        q.dT = clamp ((kTauF + phL + phH) / w, 2.f, maxDelay);
        // transient hits ring at about the level they came in at whatever the decay; sustained input
        // builds up by 1 / sqrt (1 - g^2). Split the difference.
        q.normT = std::pow (std::max (1.f - g * g, 1.0e-5f), 0.25f);
    }

    void activate (int v)
    {
        for (int c = 0; c < 2; ++c)
        {
            Res& q = res[c][v];
            q.d = q.dT; q.lpA = q.lpAT; q.hpA = q.hpAT; q.k = q.kT; q.norm = q.normT;
            q.lp = q.hp = 0.f;
        }
        live[v] = true;
    }

    // A faded-out voice stops running; wipe the part of its line a later re-activation could read.
    void kill (int v)
    {
        for (int c = 0; c < 2; ++c)
        {
            Res& q = res[c][v];
            const int w = q.line.writePos();
            const int len = q.line.capacity();
            for (int i = 0; i < len; ++i) q.line.at (w + i) = 0.f;
            q.lp = q.hp = 0.f;
        }
        amp[v] = 0.f;
        live[v] = false;
    }

    Res res[2][kVoices];
    float amp[kVoices] = {};
    bool live[kVoices] = {};
    float sr = 48000.f, maxDelay = 0.f;
    float kGlide = 0.01f, kSmooth = 0.01f, kAmp = 0.01f;
    float tGain = 0.5f, gain = 0.5f, tCountNorm = 1.f, countNorm = 1.f;
    int count = 1;
    float cNote = -1.f, cDecay = -1.f, cDamp = -1.f, cDetune = -1.f;
    int cChord = -1;
    bool first = true;
};

} // namespace resofx

std::unique_ptr<Effect> createResonatorFx() { return std::make_unique<resofx::ResonatorFx>(); }

} // namespace atm
