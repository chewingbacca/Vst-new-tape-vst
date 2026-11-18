// Verdant — tape module.
//
//   input -> granular varispeed / time-stretch -> wow & flutter delay -> saturation
//         -> age (tone, head bump, dropouts) -> stutter / repeat -> mix with clean
//
// With every control neutral the module is a pure delay of nominalDelay() samples;
// the engine delays the dry path by the same amount so dry and wet stay phase-coherent.
#pragma once

#include "Params.h"
#include "Util.h"

namespace verdant
{

class Tape
{
public:
    struct Controls
    {
        bool  on = true;
        float mix = 1.0f;
        float speedSemis = 0.0f;
        float stretch = 1.0f;
        float wow = 0.3f, flutter = 0.2f;
        float drive = 0.3f, age = 0.3f;
        float bodyDb = 0.0f;          // extra head-bump from the Roots macro
        float stutter = 0.0f;
        int   stutterLen = 3;         // index into kStutterBeats
        float motion = 1.0f;          // Growth-scaled rate multiplier for wow
        float drift = 0.0f;           // slow modulator -1..1
        double beat = 0.0;            // current position in quarter notes
        double samplesPerBeat = 24000.0;
    };

    void prepare (float sampleRate)
    {
        sr = sampleRate;
        tickRate = sr / 32.0f;

        const int capLen = int (8.5f * sr);
        for (auto& c : cap) c.prepare (capLen);

        wowBase = int (std::lround (0.003f * sr));
        for (auto& w : wowBuf) w.prepare (wowBase * 2 + 64);
        for (auto& c : cleanBuf) c.prepare (kGrainLag + wowBase + 16);
        sAA.setTime (0.05f, tickRate);

        const int stLen = int (3.2f * sr);
        for (auto& s : stBuf) s.prepare (stLen);

        grainLen = int (0.100f * sr) & ~3;      // multiple of 4 so hop is exact
        hop = grainLen / 4;
        fadeLen = std::max (8, int (0.003f * sr));

        sOn.setTime (0.03f, tickRate);
        sMix.setTime (0.03f, tickRate);
        sDrive.setTime (0.05f, tickRate);
        sAge.setTime (0.08f, tickRate);
        sWowDepth.setTime (0.1f, tickRate);
        sFlutDepth.setTime (0.1f, tickRate);
        for (auto& d : dc) d.prepare (sr);

        reset();
    }

    void reset()
    {
        rng.seed (0x7A9E11u);
        for (auto& c : cap) c.clear();
        for (auto& c : cleanBuf) c.clear();
        for (int ch = 0; ch < 2; ++ch) { aa1[ch].reset(); aa2[ch].reset(); }
        rAA.snap (0.0f);
        for (auto& w : wowBuf) w.clear();
        for (auto& s : stBuf) s.clear();
        for (auto& g : grains) g.active = false;
        for (auto& a : sat) a.reset();
        for (auto& d : dc) d.reset();
        for (auto& f : tone) f.reset();
        for (auto& f : bump) f.reset();
        wAbs = 0; stAbs = 0;
        hopCount = 0; nextGrain = 0;
        // Start as if the grain engine had always been running: three older grains are
        // already part-way through their windows, so the overlap sums to one from the
        // first sample and the first note after a reset keeps its full attack.
        for (int k = 0; k < 3; ++k)
        {
            Grain& g = grains[size_t (k)];
            g.active = true; g.unity = true; g.rate = 1.0f;
            g.phInc = 1.0f / float (grainLen);
            g.ph = 0.25f * float (3 - k);
            g.pos = -double (kGrainLag);
        }
        nextGrain = 3;
        lagStretch = 0.0; lastCycle = -1;
        wowPh = 0.11f; wowPh2 = 0.53f; flPh[0] = 0.0f; flPh[1] = 0.31f; flPh[2] = 0.67f;
        wowNoise = wowNoiseT = 0.0f; flNoise[0] = flNoise[1] = 0.0f;
        for (int c = 0; c < 2; ++c) { rDelay[c].snap (float (wowBase)); }
        dropGain = 1.0f; dropTarget = 1.0f; dropCount = 0;
        rDrop.snap (1.0f);
        st = {};
        lastGrid = -1;
        first = true;
        stutterEvents = 0;
    }

    int nominalDelay() const { return kGrainLag + wowBase; }

    // Called once per 32-sample control block.
    void control (const Controls& c)
    {
        ctl = c;
        sOn.set (c.on ? 1.0f : 0.0f);
        sMix.set (c.mix);
        sDrive.set (c.drive);
        sAge.set (c.age);

        // pitch deviation -> delay excursion (seconds)
        const float wowHz = 0.55f * c.motion * (1.0f + 0.25f * c.drift);
        const float wowDev = 0.008f * std::pow (clampf (c.wow, 0.0f, 1.0f), 1.6f);
        const float flDev  = 0.0025f * std::pow (clampf (c.flutter, 0.0f, 1.0f), 1.5f);
        sWowDepth.set (std::min (0.0024f, wowDev / (kTwoPi * std::max (0.05f, wowHz))));
        sFlutDepth.set (flDev / (kTwoPi * 8.0f));

        if (first)
        {
            sOn.snap (sOn.target); sMix.snap (sMix.target); sDrive.snap (sDrive.target);
            sAge.snap (sAge.target); sWowDepth.snap (sWowDepth.target); sFlutDepth.snap (sFlutDepth.target);
        }

        const float on = sOn.tick(), mix = sMix.tick();
        const float drive = sDrive.tick(), age = sAge.tick();
        const float wowA = sWowDepth.tick(), flA = sFlutDepth.tick();

        // ---- varispeed
        rate = std::abs (c.speedSemis) < 0.005f ? 1.0f : semitonesToRatio (clampf (c.speedSemis, -12.0f, 12.0f));
        stretch = clampf (c.stretch, 1.0f, 8.0f);
        texture = clampf (std::abs (rate - 1.0f) * 4.0f + (stretch - 1.0f), 0.0f, 1.0f);

        // Speeding up reads the tape faster than it was written; band-limit what is recorded
        // (4th-order Butterworth at 0.42 fs / ratio) so nothing folds back as aliasing.
        sAA.set (rate > 1.02f ? 1.0f : 0.0f);
        if (first) sAA.snap (sAA.target);
        const float aaFc = 0.42f * sr / std::max (1.0f, rate);
        for (int ch = 0; ch < 2; ++ch)
        {
            aa1[ch].set (aaFc, 0.5412f, sr);
            aa2[ch].set (aaFc, 1.3066f, sr);
        }
        rAA.begin (sAA.tick(), 32);

        // stretch cycle: the head snaps back to "now" once per bar
        const int cycle = int (std::floor ((c.beat + 1.0e-7) / 4.0));
        if (cycle != lastCycle) { lastCycle = cycle; lagStretch = 0.0; }
        if (stretch <= 1.001f) lagStretch = 0.0;
        maxStretchLag = double (7.0f * sr) - double (grainLen) * 2.5;

        // ---- wow & flutter (control-rate LFOs, per-sample ramps)
        const float dt = 1.0f / tickRate;
        wowPh  += wowHz * dt;           wowPh  -= std::floor (wowPh);
        wowPh2 += wowHz * 0.37f * dt;   wowPh2 -= std::floor (wowPh2);
        if (rng.chance (wowHz * dt * 2.0f)) wowNoiseT = rng.bi();
        wowNoise += clampf (kTwoPi * wowHz * 0.8f * dt, 0.0f, 1.0f) * (wowNoiseT - wowNoise);
        static constexpr float flHz[3] = { 6.1f, 9.4f, 13.7f };
        for (int k = 0; k < 3; ++k) { flPh[k] += flHz[k] * dt; flPh[k] -= std::floor (flPh[k]); }
        for (int ch = 0; ch < 2; ++ch)
            flNoise[ch] += 0.08f * (rng.bi() - flNoise[ch]);

        for (int ch = 0; ch < 2; ++ch)
        {
            const float off = ch == 0 ? 0.0f : 0.02f;   // slight azimuth-like offset on the right
            const float w = 0.7f * fastSin01 (wowPh + off) + 0.2f * fastSin01 (wowPh2 + off) + 0.35f * wowNoise;
            const float f = 0.5f * fastSin01 (flPh[0]) + 0.35f * fastSin01 (flPh[1] + off * 3.0f)
                          + 0.15f * fastSin01 (flPh[2]) + 1.2f * flNoise[ch];
            const float d = float (wowBase) + sr * (wowA * w + flA * f);
            const float lim = float (wowBase) - 4.0f;
            const float target = clampf (d, float (wowBase) - lim, float (wowBase) + lim);
            if (first) rDelay[ch].snap (target);
            rDelay[ch].begin (target, 32);
        }

        // ---- saturation
        satPre  = lerp (0.5f, 5.0f, drive * drive);
        satPost = (1.0f + 0.6f * drive) / satPre;
        satEven = 0.30f * drive;      // asymmetry -> even harmonics; vanishes at silence (no DC bias)

        // ---- age: tone, head bump, dropouts
        const float fc = 21000.0f * std::pow (0.15f, age);
        for (int ch = 0; ch < 2; ++ch)
        {
            tone[ch].set (fc, 0.68f, sr);
            bump[ch].setLow (95.0f, 3.0f * age + c.bodyDb, sr);
        }
        if (dropCount > 0) --dropCount;
        else
        {
            dropTarget = 1.0f;
            const float perSec = 1.6f * age * age;
            if (rng.chance (perSec * dt))
            {
                dropTarget = 1.0f - age * rng.range (0.25f, 0.6f);
                dropCount = int (rng.range (0.03f, 0.12f) * tickRate);
            }
        }
        dropGain += 0.12f * (dropTarget - dropGain);
        if (first) rDrop.snap (dropGain);
        rDrop.begin (dropGain, 32);

        // ---- stutter trigger (on its own beat grid)
        const double sliceBeats = kStutterBeats[clampi (c.stutterLen, 0, 6)];
        const double gridBeats = sliceBeats > 0.0 ? sliceBeats : 0.5;
        const int grid = int (std::floor ((c.beat + 1.0e-7) / gridBeats));
        if (grid != lastGrid)
        {
            const bool jumpedFar = lastGrid >= 0 && std::abs (grid - lastGrid) > 1;
            lastGrid = grid;
            const float p = 0.6f * c.stutter * c.stutter;
            if (! st.active && ! jumpedFar && on > 0.5f && p > 0.0f && rng.chance (p))
                startStutter (sliceBeats, c.samplesPerBeat);
        }

        const float m = on * mix;
        if (first) { rMix.snap (m); first = false; }
        rMix.begin (m, 32);
    }

    void process (const float* inL, const float* inR, float* outL, float* outR, int n)
    {
        const HannTable& hann = hannTable();
        const float* in[2] = { inL, inR };
        float* out[2] = { outL, outR };

        for (int i = 0; i < n; ++i)
        {
            const float aa = rAA.next();
            for (int ch = 0; ch < 2; ++ch)
            {
                const float x = in[ch][i];
                cleanBuf[ch].push (x);
                const float lp = aa2[ch].low (aa1[ch].low (x));
                cap[ch].push (x + (lp - x) * aa);
            }
            ++wAbs;

            // ---------------------------------------------- granular varispeed / stretch
            if (hopCount <= 0)
            {
                spawnGrain();
                hopCount = hop;
            }
            --hopCount;
            if (stretch > 1.001f)
            {
                lagStretch += 1.0 - 1.0 / double (stretch);
                if (lagStretch > maxStretchLag) lagStretch = 0.0;
            }

            float g0 = 0.0f, g1 = 0.0f;
            for (auto& g : grains)
            {
                if (! g.active) continue;
                const float w = hann (g.ph);
                if (g.unity)
                {
                    const int64_t p = int64_t (g.pos);
                    g0 += w * cap[0].atAbs (p);
                    g1 += w * cap[1].atAbs (p);
                }
                else
                {
                    g0 += w * cap[0].readHermite (g.pos);
                    g1 += w * cap[1].readHermite (g.pos);
                }
                g.pos += double (g.rate);
                g.ph += g.phInc;
                if (g.ph >= 1.0f) g.active = false;
            }
            const float gGain = 0.5f;
            float y[2] = { g0 * gGain, g1 * gGain };

            const float drop = rDrop.next();
            const float mix = rMix.next();

            for (int ch = 0; ch < 2; ++ch)
            {
                // ------------------------------------------ wow & flutter
                wowBuf[ch].push (y[ch]);
                float v = wowBuf[ch].tapHermite (rDelay[ch].next());

                // ------------------------------------------ saturation
                const float u = v * satPre;
                v = sat[ch].process (u + satEven * u * u / (1.0f + u * u)) * satPost;
                v = dc[ch].process (v);

                // ------------------------------------------ age
                v = tone[ch].low (v);
                v = bump[ch].process (v) * drop;

                y[ch] = v;
            }

            // ---------------------------------------------- stutter
            stutterProcess (y[0], y[1]);

            // ---------------------------------------------- mix against the time-aligned clean signal
            const int nd = nominalDelay();
            for (int ch = 0; ch < 2; ++ch)
            {
                const float clean = cleanBuf[ch].tap (nd);
                out[ch][i] = clean + (y[ch] - clean) * mix;
            }
        }
    }

    int stutterEvents = 0;

private:
    static constexpr int kGrainLag = 4;
    static int clampi (int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

    struct Grain
    {
        bool active = false, unity = false;
        double pos = 0.0;
        float rate = 1.0f, ph = 0.0f, phInc = 0.0f;
    };

    void spawnGrain()
    {
        const Grain* ref = nullptr;                 // the most recently started grain
        {
            const int prev = (nextGrain + int (grains.size()) - 1) % int (grains.size());
            if (grains[size_t (prev)].active) ref = &grains[size_t (prev)];
        }
        Grain& g = grains[size_t (nextGrain)];
        nextGrain = (nextGrain + 1) % int (grains.size());

        const float r = rate;
        double lag = double (kGrainLag) + double (std::max (0.0f, r - 1.0f)) * double (grainLen) + lagStretch;
        const bool unity = (r == 1.0f) && (lag == std::floor (lag)) && texture <= 0.0f;

        // Waveform-similarity alignment (WSOLA, Verhelst & Roelands 1993): when the grains do
        // not all read the same spot (varispeed or stretch), slide the new grain back by up to
        // 12 ms to where it lines up best with what the previous grain is about to play. The
        // overlaps then add in phase: a clean transposition instead of comb-filtered roughness.
        if (! unity && ref != nullptr)
            lag += double (alignmentOffset (ref->pos, double (wAbs - 1) - lag));

        g.active = true;
        g.rate = r;
        g.ph = 0.0f;
        g.phInc = 1.0f / float (grainLen);
        g.pos = double (wAbs - 1) - lag;
        g.unity = unity;
    }

    // Returns how many samples further back (0 .. searchLen) the new grain should start.
    int alignmentOffset (double refPos, double newPos) const
    {
        const int stride = std::max (1, int (sr / 24000.0f));          // work at about 24 kHz
        const int corrLen = int (0.012f * sr), searchLen = int (0.012f * sr);
        const int64_t a0 = int64_t (refPos), b0 = int64_t (newPos);

        float bestScore = -1.0e30f;
        int best = 0;
        for (int d = 0; d <= searchLen; d += stride)
        {
            float corr = 0.0f, energy = 1.0e-9f;
            for (int j = 0; j < corrLen; j += stride)
            {
                const float a = cap[0].atAbs (a0 + j) + cap[1].atAbs (a0 + j);
                const float b = cap[0].atAbs (b0 - d + j) + cap[1].atAbs (b0 - d + j);
                corr += a * b;
                energy += b * b;
            }
            const float score = corr / std::sqrt (energy);
            if (score > bestScore) { bestScore = score; best = d; }
        }
        return best;
    }

    //==============================================================================
    struct StutterState
    {
        bool active = false;
        int64_t sliceStart = 0;
        int sliceLen = 0, repeats = 0, rep = 0, pos = 0, fadeIn = 0;
        double readPos = 0.0, rate = 1.0, rateMul = 1.0;
        float gain = 1.0f, gainMul = 1.0f;
    };

    void startStutter (double sliceBeats, double samplesPerBeat)
    {
        if (sliceBeats <= 0.0)
        {
            static constexpr double opts[] = { 0.5, 0.25, 0.25, 0.125, 1.0 / 6.0, 1.0 / 3.0 };
            sliceBeats = opts[rng.irange (6)];
        }
        const int len = clampi (int (sliceBeats * samplesPerBeat), int (0.02f * sr), int (1.0f * sr));
        static constexpr int reps[] = { 2, 3, 4, 4, 6, 8 };
        int nrep = reps[rng.irange (6)];
        while (nrep > 2 && double (nrep) * double (len) > 2.0 * double (sr)) --nrep;

        st.active = true;
        st.sliceStart = stAbs;
        st.sliceLen = len;
        st.repeats = nrep;
        st.rep = 0;
        st.pos = 0;
        st.rate = 1.0;
        st.gain = 1.0f;
        const float u = rng.uni();
        st.rateMul = u < 0.70f ? 1.0 : (u < 0.82f ? 0.9439 : (u < 0.88f ? 0.8909 : 1.0595));
        st.gainMul = rng.chance (0.5f) ? 1.0f : 0.9f;
        ++stutterEvents;
    }

    inline void stutterProcess (float& l, float& r)
    {
        stBuf[0].push (l);
        stBuf[1].push (r);
        ++stAbs;

        float live = 1.0f;
        if (st.fadeIn > 0)
        {
            live = 1.0f - float (st.fadeIn) / float (fadeLen);
            --st.fadeIn;
        }

        if (! st.active)
        {
            l *= live; r *= live;
            return;
        }

        if (st.rep == 0)
        {
            // first pass plays live; fade it out at the very end of the slice
            const int left = st.sliceLen - st.pos;
            if (left < fadeLen) live *= float (left) / float (fadeLen);
            l *= live; r *= live;
        }
        else
        {
            const float edge = std::min (1.0f, std::min (float (st.pos), float (st.sliceLen - st.pos)) / float (fadeLen));
            const float g = edge * st.gain;
            l = g * stBuf[0].readHermite (st.readPos);
            r = g * stBuf[1].readHermite (st.readPos);
            st.readPos += st.rate;
        }

        if (++st.pos >= st.sliceLen)
        {
            st.pos = 0;
            if (++st.rep >= st.repeats)
            {
                st.active = false;
                st.fadeIn = fadeLen;
            }
            else
            {
                st.readPos = double (st.sliceStart);
                if (st.rep > 1) { st.rate *= st.rateMul; st.gain *= st.gainMul; }
            }
        }
    }

    //==============================================================================
    float sr = 48000.0f, tickRate = 1500.0f;
    Controls ctl;
    Rng rng;

    Ring cap[2], cleanBuf[2], wowBuf[2], stBuf[2];
    Svf aa1[2], aa2[2];
    Smooth sAA;
    Ramp rAA;
    int64_t wAbs = 0, stAbs = 0;

    std::array<Grain, 8> grains;
    int grainLen = 4800, hop = 1200, hopCount = 0, nextGrain = 0;
    float rate = 1.0f, stretch = 1.0f, texture = 0.0f;
    double lagStretch = 0.0, maxStretchLag = 0.0;
    int lastCycle = -1;

    int wowBase = 144;
    float wowPh = 0, wowPh2 = 0, flPh[3] = { 0, 0, 0 }, wowNoise = 0, wowNoiseT = 0, flNoise[2] = { 0, 0 };
    Ramp rDelay[2];

    AdaaTanh sat[2];
    DcBlocker dc[2];
    float satPre = 1.0f, satPost = 1.0f, satEven = 0.0f;

    Svf tone[2];
    Shelf1 bump[2];
    float dropGain = 1.0f, dropTarget = 1.0f;
    int dropCount = 0;
    Ramp rDrop, rMix;

    StutterState st;
    int lastGrid = -1, fadeLen = 144;

    Smooth sOn, sMix, sDrive, sAge, sWowDepth, sFlutDepth;
    bool first = true;
};

} // namespace verdant
