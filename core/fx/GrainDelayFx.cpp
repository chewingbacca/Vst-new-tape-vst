// Grain delay: a mono delay line read by up to 32 Hann-windowed grains.
//
//   in (L+R)/2 --(+)--> delay line --> grains (delay = time + spray * rnd, rate = pitch + scatter * rnd,
//                ^                      forward or reversed, random pan) --> * overlap normalisation --> out L/R
//                +---- feedback * soft clip <- DC block <- LP 7 kHz <- grain sum (mono) ----+
//
//   * A grain that reads faster than the line is written (pitch up) starts far enough back that it
//     cannot catch the write pointer during its life; a reversed grain plays the same stretch of
//     audio from its end, so it too only ever moves away from the write pointer.
//   * Grains start at regular intervals of 1 / density (spray also jitters the onsets). With
//     density * size = overlap >= 2 and no spray/pitch the windows sum to a constant: a clean delay.
//   * Output level ~ overlap^-0.75 above an overlap of 2 (between the coherent and the random-phase
//     sum); the feedback path is normalised for the coherent sum, so the loop gain never exceeds
//     `feedback` on average.
//   * Total rate (pitch + scatter) is limited to +24 semitones.
#include "Effect.h"
#include "../dsp/Delay.h"
#include "../dsp/Filters.h"
#include "../model/ParamTables.h"
#include "../util/Rng.h"

namespace atm {
namespace graindelayfx {

enum { PTime, PSize, PDensity, PSpray, PPitch, PScatter, PFeedback, PRevProb };
constexpr int kGrains = 32;
constexpr float kMaxSpraySec = 0.5f;
constexpr float kMaxRate = 4.f;
constexpr float kLineSec = 3.2f;         // max (time + spray, rate * size) + size, plus margin
constexpr float kPanSpread = 0.8f;
constexpr uint32_t kSeed = 0x6a09e667u;

struct Grain
{
    bool on = false;
    double pos = 0.0;                    // delay behind the newest sample in the line
    double step = 0.0;                   // change of that delay per sample (1 - rate, or 1 + rate reversed)
    float ph = 0.f, phInc = 0.f;
    float gl = 0.f, gr = 0.f;
};

class GrainDelayFx final : public Effect
{
public:
    void prepare (double sampleRate) override
    {
        sr = (float) sampleRate;
        line.prepare ((int) (kLineSec * sr) + 16);
        maxPos = (double) (line.capacity() - 8);
        fbLp.setCutoff (7000.f, sr);
        fbDc.prepare (sr, 15.f);
        kSmooth = onePoleCoef (0.010f, sr);
        reset();
    }

    void reset() override
    {
        line.reset();
        for (auto& g : grains) g.on = false;
        rng.seed (kSeed);
        fbLp.reset(); fbDc.reset();
        fbSig = 0.f; countdown = 0.f;
        first = true;
    }

    void setParams (const float* p, const FxContext&) override
    {
        timeS = kFxGrainDelay[PTime].toPhys (p[PTime]) * sr;
        sizeS = std::max (kFxGrainDelay[PSize].toPhys (p[PSize]) * sr, 16.f);
        const float density = kFxGrainDelay[PDensity].toPhys (p[PDensity]);
        interval = sr / density;
        spray = kFxGrainDelay[PSpray].toPhys (p[PSpray]);
        pitch = kFxGrainDelay[PPitch].toPhys (p[PPitch]);
        scatter = kFxGrainDelay[PScatter].toPhys (p[PScatter]);
        tFb = kFxGrainDelay[PFeedback].toPhys (p[PFeedback]);
        revProb = clamp01 (p[PRevProb]);
        const float overlap = std::min (density * sizeS / sr, (float) kGrains);
        tOutNorm = overlap > 2.f ? std::pow (2.f / overlap, 0.75f) : 1.f;
        tFbNorm = overlap > 2.f ? 2.f / overlap : 1.f;
        if (first) { first = false; fb = tFb; outNorm = tOutNorm; fbNorm = tFbNorm; }
    }

    void process (float* l, float* r, int n, const FxContext&) override
    {
        for (int i = 0; i < n; ++i)
        {
            fb += kSmooth * (tFb - fb);
            outNorm += kSmooth * (tOutNorm - outNorm);
            fbNorm += kSmooth * (tFbNorm - fbNorm);

            countdown -= 1.f;
            if (countdown <= 0.f)
            {
                spawn();
                countdown += interval * (1.f + spray * rng.next() * 0.5f);
                if (countdown < 1.f) countdown = 1.f;
            }

            float sl = 0.f, sr_ = 0.f;
            const double newest = (double) (line.writePos() - 1);
            for (auto& g : grains)
            {
                if (! g.on) continue;
                const float w = 0.5f + 0.5f * fastSin01 (g.ph - 0.25f);       // Hann
                const float s = line.atHermite (newest - g.pos) * w;
                sl += s * g.gl; sr_ += s * g.gr;
                g.pos += g.step;
                g.ph += g.phInc;
                if (g.ph >= 1.f || g.pos < 2.0 || g.pos > maxPos) g.on = false;
            }

            const float in = 0.5f * (l[i] + r[i]);
            line.write (undenorm (in + fbSig));
            // feedback for the next sample: mono sum of the grains, band-limited and bounded
            const float mono = 0.7071f * (sl + sr_) * fbNorm;
            fbSig = softClip (fb * fbDc.process (fbLp.process (mono)));
            fbLp.z = undenorm (fbLp.z);

            l[i] = sl * outNorm;
            r[i] = sr_ * outNorm;
        }
    }

private:
    void spawn()
    {
        Grain* g = nullptr;
        for (auto& c : grains) if (! c.on) { g = &c; break; }
        // draw the random numbers even if the pool is full, so the sequence does not depend on it
        const float rSpray = rng.uni(), rPitch = rng.next(), rRev = rng.uni(), rPan = rng.next();
        if (g == nullptr) return;

        const float len = sizeS;
        const float rate = std::min (semitonesToRatio (pitch + scatter * rPitch), kMaxRate);
        const bool rev = rRev < revProb;
        double d0 = (double) timeS + (double) (spray * rSpray * kMaxSpraySec * sr);
        if (rev)
        {
            // plays [now - d0, now - d0 + rate * len] backwards: starts at the newer end
            d0 = std::max (d0, (double) (rate * len) + 4.0);
            g->pos = d0 - (double) (rate * len);
            g->step = 1.0 + (double) rate;
        }
        else
        {
            if (rate > 1.f) d0 = std::max (d0, (double) ((rate - 1.f) * len) + 4.0);
            g->pos = d0;
            g->step = 1.0 - (double) rate;
        }
        g->pos = std::max (g->pos, 2.0);
        g->ph = 0.f;
        g->phInc = 1.f / len;
        const float pan = 0.5f + 0.5f * kPanSpread * rPan;                    // 0..1
        g->gl = std::cos (pan * 0.5f * kPiF);
        g->gr = std::sin (pan * 0.5f * kPiF);
        g->on = true;
    }

    DelayLine line;
    Grain grains[kGrains];
    Noise rng;
    OnePoleLP fbLp;
    DcBlocker fbDc;
    float sr = 48000.f;
    double maxPos = 0.0;
    float timeS = 12000.f, sizeS = 4000.f, interval = 2400.f, spray = 0.f, pitch = 0.f, scatter = 0.f, revProb = 0.f;
    float tFb = 0.f, fb = 0.f, tOutNorm = 1.f, outNorm = 1.f, tFbNorm = 1.f, fbNorm = 1.f;
    float kSmooth = 0.01f, fbSig = 0.f, countdown = 0.f;
    bool first = true;
};

} // namespace graindelayfx

std::unique_ptr<Effect> createGrainDelayFx() { return std::make_unique<graindelayfx::GrainDelayFx>(); }

} // namespace atm
