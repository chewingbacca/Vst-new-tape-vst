// Verdant — grain cloud: scatters the (post-tape) signal into small pitched grains
// with random pan, reverse probability and a density from sparse droplets to foliage.
//
// The capture buffer is mono (each grain is a point source placed by its pan), which
// halves the cost and keeps the cloud phase-coherent. Output is the cloud only.
#pragma once

#include "Util.h"

namespace verdant
{

class GrainCloud
{
public:
    static constexpr int kMaxGrains = 64;

    struct Controls
    {
        bool  on = true;
        float density = 14.0f;     // grains per second (already macro / modulation scaled)
        float sizeMs = 140.0f;
        float scatter = 0.4f;
        float pitchSemis = 0.0f;
        float spread = 0.35f;      // probability of harmonic jumps
        float reverse = 0.25f;
        float pan = 0.8f;
        float shape = 0.5f;
        float feedback = 0.0f;
        float upBias = 0.5f;       // Sunlight: favour upward intervals
        float downBias = 0.5f;     // Roots: favour downward intervals
        bool  octavesOnly = false;
    };

    void prepare (float sampleRate)
    {
        sr = sampleRate;
        tickRate = sr / 32.0f;
        cap.prepare (int (9.3f * sr));
        capA.prepare (int (9.3f * sr));
        capB.prepare (int (9.3f * sr));
        // Grains transposed upwards read a band-limited copy (4th-order Butterworth), so even
        // a two-octave jump of bright material stays below Nyquist: no aliasing in the sparkle.
        //   ratio <= 1.3 : full band     ratio <= 2.2 : 0.20 fs     above : 0.11 fs
        aaA1.set (0.20f * sr, 0.5412f, sr); aaA2.set (0.20f * sr, 1.3066f, sr);
        aaB1.set (0.11f * sr, 0.5412f, sr); aaB2.set (0.11f * sr, 1.3066f, sr);
        fbRing.prepare (128);
        fbHp.setCutoff (140.0f, sr);
        fbLp.setCutoff (6500.0f, sr);
        sNorm.setTime (0.08f, tickRate);
        sOn.setTime (0.03f, tickRate);
        sFb.setTime (0.05f, tickRate);
        minOffset = 40.0;
        reset();
    }

    void reset()
    {
        rng.seed (0x51A7B0u);
        cap.clear();
        capA.clear(); capB.clear();
        aaA1.reset(); aaA2.reset(); aaB1.reset(); aaB2.reset();
        fbRing.clear();
        fbHp.reset(); fbLp.reset();
        for (auto& g : grains) g.active = false;
        wAbs = 0;
        untilNext = 0.0;
        numActive = 0;
        first = true; firstFb = true;
        spawnCount = 0;
        activity = 0.0f;
    }

    void control (const Controls& c)
    {
        ctl = c;
        ctl.density = clampf (c.density, 0.1f, 120.0f);
        ctl.sizeMs = clampf (c.sizeMs, 5.0f, 900.0f);

        // loudness normalisation from the expected number of overlapping grains
        const float overlap = ctl.density * ctl.sizeMs * 0.001f;
        const float shapeEnergy = lerp (0.16f, 0.30f, clampf (c.shape, 0.0f, 1.0f)); // mean(w^2 * amp^2)
        sNorm.set (std::min (1.5f, 1.0f / std::sqrt (std::max (1.0e-3f, shapeEnergy * overlap))));
        sOn.set (c.on ? 1.0f : 0.0f);
        if (first) { sNorm.snap (sNorm.target); sOn.snap (sOn.target); }

        const float on = sOn.tick();
        const float g = sNorm.tick() * on;
        if (first) { rGain.snap (g); first = false; }
        rGain.begin (g, 32);
        spawning = c.on;
        sFb.set (0.72f * clampf (c.feedback, 0.0f, 1.0f));
        if (firstFb) { sFb.snap (sFb.target); rFb.snap (sFb.target * on); firstFb = false; }
        rFb.begin (sFb.tick() * on, 32);
        activity = float (numActive) / 24.0f;
    }

    // outL/outR receive the cloud (overwritten).
    void process (const float* inL, const float* inR, float* outL, float* outR, int n)
    {
        // ---- capture (input + regrow feedback from one control block ago)
        for (int i = 0; i < n; ++i)
        {
            float x = 0.5f * (inL[i] + inR[i]);
            const float fb = fbLp.process (fbHp.process (fbRing.tap (31 - i)));
            x += rFb.next() * softClip (fb);
            cap.push (x);
            capA.push (aaA2.low (aaA1.low (x)));
            capB.push (aaB2.low (aaB1.low (x)));
            outL[i] = 0.0f;
            outR[i] = 0.0f;
        }
        wAbs += n;

        // ---- render. The block is cut at every spawn time, so a finished grain's slot is free
        // on the exact sample it ends; the result is then independent of how the host slices blocks.
        int pos = 0;
        while (true)
        {
            const bool spawnHere = spawning && untilNext < double (n);
            const int segEnd = spawnHere ? std::max (pos, int (untilNext)) : n;
            if (segEnd > pos)
            {
                render (outL, outR, pos, segEnd);
                pos = segEnd;
            }
            if (! spawnHere)
                break;

            spawn (pos, n);
            // jittered interval, mean = 1 / density
            const double interval = double (sr) / double (ctl.density);
            untilNext += interval * double (0.35f + 1.3f * rng.uni());
        }
        untilNext = spawning ? untilNext - double (n) : 0.0;

        numActive = 0;
        for (const auto& g : grains)
            numActive += g.active ? 1 : 0;

        // ---- level + feedback tap
        for (int i = 0; i < n; ++i)
        {
            const float g = rGain.next();
            outL[i] *= g;
            outR[i] *= g;
            fbRing.push (0.5f * (outL[i] + outR[i]));
        }
    }

    int spawnCount = 0;
    float activity = 0.0f;

private:
    struct Grain
    {
        bool active = false;
        int band = 0;
        double pos = 0.0, inc = 1.0;
        float ph = 0.0f, phInc = 0.0f;
        float gl = 0.7f, gr = 0.7f;
        float attack = 0.5f, invAttack = 2.0f, invRelease = 2.0f, pluck = 0.0f;
    };

    float pickInterval()
    {
        if (! rng.chance (ctl.spread * 0.9f))
            return 0.0f;

        const float up = clampf (ctl.upBias, 0.0f, 1.0f), down = clampf (ctl.downBias, 0.0f, 1.0f);
        struct Opt { float semis, weight; };
        const Opt opts[] = {
            { 12.0f, 3.0f * (0.4f + 1.2f * up) },
            { -12.0f, 2.0f * (0.3f + 1.4f * down) },
            { 7.0f, ctl.octavesOnly ? 0.0f : 1.5f },
            { 19.0f, ctl.octavesOnly ? 0.0f : 0.8f * (0.5f + up) },
            { 24.0f, 0.7f * (0.2f + 1.6f * up) },
            { -5.0f, ctl.octavesOnly ? 0.0f : 0.6f * (0.5f + down) },
        };
        float total = 0.0f;
        for (const auto& o : opts) total += o.weight;
        float pick = rng.uni() * total;
        for (const auto& o : opts)
        {
            if (pick < o.weight) return o.semis;
            pick -= o.weight;
        }
        return 12.0f;
    }

    void render (float* outL, float* outR, int from, int to)
    {
        const HannTable& hann = hannTable();
        for (auto& g : grains)
        {
            if (! g.active) continue;
            const Ring& src = g.band == 0 ? cap : (g.band == 1 ? capA : capB);
            for (int i = from; i < to; ++i)
            {
                float w;
                if (g.ph < g.attack)
                    w = hann (0.5f * g.ph * g.invAttack);
                else
                {
                    const float f = hann (0.5f + 0.5f * (g.ph - g.attack) * g.invRelease);
                    w = f + (f * f * f - f) * g.pluck;
                }
                const float s = w * src.readHermite (g.pos);
                outL[i] += s * g.gl;
                outR[i] += s * g.gr;
                g.pos += g.inc;
                g.ph += g.phInc;
                if (g.ph >= 1.0f) { g.active = false; break; }
            }
        }
    }

    void spawn (int offsetInBlock, int blockLen)
    {
        // Draw every random value first: the random sequence must not depend on whether a
        // slot happens to be free.
        const float lenSamples = std::max (32.0f, ctl.sizeMs * 0.001f * sr * rng.range (0.7f, 1.3f));
        const float detune = rng.bi() * (3.0f + 9.0f * ctl.spread) * 0.01f;
        const float ratio = clampf (semitonesToRatio (ctl.pitchSemis + pickInterval() + detune), 0.25f, 4.0f);
        const bool rev = rng.chance (ctl.reverse);
        const float u = rng.uni();
        const float nearJitter = rng.uni();
        const float pan = rng.bi() * clampf (ctl.pan, 0.0f, 1.0f);
        const float amp = rng.range (0.6f, 1.0f);

        Grain* slot = nullptr;
        for (auto& g : grains)
            if (! g.active) { slot = &g; break; }
        if (slot == nullptr)
            return; // pool full (64 overlapping grains): this grain is skipped

        Grain& g = *slot;

        // how far behind "now" the grain starts
        double offset = minOffset + double (nearJitter * 0.03f * sr) + double (u * u * ctl.scatter * 3.0f * sr);
        if (! rev && ratio > 1.0f)
            offset += double ((ratio - 1.0f) * lenSamples);

        // newest written sample is wAbs-1 at the end of this block
        const double now = double (wAbs - 1 - (blockLen - 1 - offsetInBlock));
        g.pos = now - offset;
        g.inc = rev ? -double (ratio) : double (ratio);
        g.band = ratio <= 1.3f ? 0 : (ratio <= 2.2f ? 1 : 2);
        g.ph = 0.0f;
        g.phInc = 1.0f / lenSamples;

        const float shape = clampf (ctl.shape, 0.0f, 1.0f);
        // droplets need a click-free but fast onset: at least 3 ms
        g.attack = std::max (lerp (0.04f, 0.5f, shape), 0.003f * sr / lenSamples);
        g.attack = std::min (g.attack, 0.5f);
        g.invAttack = 1.0f / g.attack;
        g.invRelease = 1.0f / (1.0f - g.attack);
        g.pluck = 1.0f - shape;

        const float ang = (pan + 1.0f) * 0.25f * kPi;
        g.gl = amp * std::cos (ang) * 1.4142f;
        g.gr = amp * std::sin (ang) * 1.4142f;
        g.active = true;
        ++spawnCount;
    }

    float sr = 48000.0f, tickRate = 1500.0f;
    Controls ctl;
    Rng rng;

    Ring cap, capA, capB, fbRing;
    Svf aaA1, aaA2, aaB1, aaB2;
    int64_t wAbs = 0;
    double untilNext = 0.0, minOffset = 40.0;
    std::array<Grain, kMaxGrains> grains;
    int numActive = 0;
    bool spawning = true, first = true;

    OnePoleHP fbHp;
    OnePoleLP fbLp;

    Smooth sNorm, sOn, sFb;
    Ramp rGain, rFb;
    bool firstFb = true;
};

} // namespace verdant
