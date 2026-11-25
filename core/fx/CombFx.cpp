// Tuned stereo comb filter.
//
//   y[n] = x[n] + limit (fb * LP (y[n - D]))        out = y * sqrt (1 - fb^2)
//
// D is sr / f minus the phase delay of the damping filter, read with Hermite interpolation, so
// the resonances sit on the note (positive feedback: f, 2f, 3f ...; negative feedback: f/2, 3f/2 ...
// the hollow octave-down comb). Pitch changes glide over ~20 ms; the LFO modulates the pitch of
// both channels (phase-offset between them as `spread` rises). The output is scaled by the
// white-noise gain of the comb and the loop holds a soft limiter whose ceiling follows that
// scaling, so a tone parked on a resonance saturates instead of running away.
#include "Effect.h"
#include "../dsp/Delay.h"
#include "../dsp/Filters.h"
#include "../model/ParamTables.h"

namespace atm {
namespace combfx {

enum { PNote, PFeedback, PDamp, PSpread, PModRate, PModDepth };
constexpr int kTick = 8;                 // internal control interval (independent of the host's slices)
constexpr float kMaxSpreadSt = 0.5f;     // +-50 cents
constexpr float kMaxModSt = 12.f;

class CombFx final : public Effect
{
public:
    void prepare (double sampleRate) override
    {
        sr = (float) sampleRate;
        // lowest pitch: note 12 less the LFO and the spread
        const double fMin = midiToHz (12.0 - kMaxModSt - kMaxSpreadSt - 0.5);
        maxDelay = (float) (sampleRate / fMin);
        for (auto& l : line) l.prepare ((int) maxDelay + 16);
        kNote = 1.f - std::exp (-(float) kTick / (0.006f * sr));
        kSmooth = onePoleCoef (0.005f, sr);
        for (auto& d : dc) d.prepare (sr, 5.f);
        reset();
    }

    void reset() override
    {
        for (auto& l : line) l.reset();
        for (auto& d : dc) d.reset();
        lp[0] = lp[1] = 0.f;
        phase = 0.f; tick = 0; first = true;
    }

    void setParams (const float* p, const FxContext&) override
    {
        tNote = kFxComb[PNote].toPhys (p[PNote]);
        tFb = kFxComb[PFeedback].toPhys (p[PFeedback]);
        const float damp = clamp01 (p[PDamp]);
        const float hz = std::min (20000.f * std::pow (0.02f, damp), 0.49f * sr);
        tLpA = 1.f - std::exp (-kTauF * hz / sr);
        spread = kFxComb[PSpread].toPhys (p[PSpread]);
        lfoInc = kFxComb[PModRate].toPhys (p[PModRate]) / sr;
        modSt = kFxComb[PModDepth].toPhys (p[PModDepth]) * kMaxModSt;
        if (first)
        {
            first = false;
            note = tNote; fb = tFb; lpA = tLpA;
            computeTargets();
            d[0] = dT[0]; d[1] = dT[1]; dInc[0] = dInc[1] = 0.f;
            tick = 0;
        }
    }

    void process (float* l, float* r, int n, const FxContext&) override
    {
        float* io[2] = { l, r };
        for (int i = 0; i < n; ++i)
        {
            if (tick == 0)
            {
                note += kNote * (tNote - note);
                phase += lfoInc * (float) kTick; phase -= std::floor (phase);
                computeTargets();
                dInc[0] = (dT[0] - d[0]) * (1.f / (float) kTick);
                dInc[1] = (dT[1] - d[1]) * (1.f / (float) kTick);
            }
            if (++tick >= kTick) tick = 0;

            fb += kSmooth * (tFb - fb);
            lpA += kSmooth * (tLpA - lpA);
            const float comp = std::sqrt (std::max (1.f - fb * fb, 1.0e-4f));
            const float lim = 1.5f / comp, invLim = comp * (1.f / 1.5f);

            for (int c = 0; c < 2; ++c)
            {
                d[c] += dInc[c];
                const float yd = line[c].readHermite (d[c] - 1.f);
                lp[c] = undenorm (lp[c] + lpA * (yd - lp[c]));
                const float y = io[c][i] + lim * softClip (fb * lp[c] * invLim);
                line[c].write (undenorm (y));
                io[c][i] = dc[c].process (y * comp);
            }
        }
    }

private:
    // Delay lengths for the current (smoothed) note, LFO phase and damping.
    void computeTargets()
    {
        const float quad = 0.25f * std::min (1.f, 4.f * spread);
        for (int c = 0; c < 2; ++c)
        {
            const float lfo = modSt > 0.f ? modSt * fastSin01 (phase + (c ? quad : 0.f)) : 0.f;
            const float st = note + lfo + (c ? spread : -spread) * kMaxSpreadSt;
            const float f = clamp ((float) midiToHz ((double) st), 6.f, 0.24f * sr);
            const float period = sr / f;
            // phase delay of the one-pole at the lowest resonance (f, or f/2 with inverted feedback)
            const float w = (tFb < 0.f ? kPiF : kTauF) * f / sr;
            const float b = 1.f - lpA;
            const float pd = std::atan2 (b * std::sin (w), 1.f - b * std::cos (w)) / w;
            dT[c] = clamp (period - pd, 2.f, maxDelay);
        }
    }

    DelayLine line[2];
    DcBlocker dc[2];
    float sr = 48000.f, maxDelay = 0.f;
    float kNote = 0.1f, kSmooth = 0.01f;
    float tNote = 48.f, tFb = 0.f, tLpA = 1.f, spread = 0.f, lfoInc = 0.f, modSt = 0.f;
    float note = 48.f, fb = 0.f, lpA = 1.f, phase = 0.f;
    float d[2] = { 100.f, 100.f }, dT[2] = { 100.f, 100.f }, dInc[2] = {}, lp[2] = {};
    int tick = 0;
    bool first = true;
};

} // namespace combfx

std::unique_ptr<Effect> createCombFx() { return std::make_unique<combfx::CombFx>(); }

} // namespace atm
