// Verdant — arp / shimmer: re-plays the captured input as tempo-synced, pitch-shifted
// notes quantised to a key and scale.
//
// * The input is recorded into a ring only while it is sounding, so when you stop
//   playing the arp keeps working on the last thing it heard ("Linger").
// * With Pitch Track on, a YIN tracker measures the input note; arp notes are then
//   placed exactly on scale degrees of the chosen key (and are corrected for the
//   input being out of tune). Without a confident pitch the pattern is applied as
//   scale intervals relative to the input.
// * Notes are tape-style resampled slices (pitch and speed change together), which
//   gives the sped-up, glassy articulation typical of pitch-shift arps.
#pragma once

#include "Params.h"
#include "PitchTracker.h"
#include "Util.h"

namespace verdant
{

struct ScaleDef { int n; int semis[7]; };
inline constexpr ScaleDef kScales[] = {
    { 7, { 0, 2, 4, 5, 7, 9, 11 } },  // Major
    { 7, { 0, 2, 3, 5, 7, 8, 10 } },  // Minor
    { 7, { 0, 2, 3, 5, 7, 9, 10 } },  // Dorian
    { 7, { 0, 2, 4, 6, 7, 9, 11 } },  // Lydian
    { 7, { 0, 2, 4, 5, 7, 9, 10 } },  // Mixolydian
    { 5, { 0, 2, 4, 7, 9, 0, 0 } },   // Major Pentatonic
    { 5, { 0, 3, 5, 7, 10, 0, 0 } },  // Minor Pentatonic
    { 6, { 0, 2, 4, 6, 8, 10, 0 } },  // Whole Tone
    { 3, { 0, 4, 7, 0, 0, 0, 0 } },   // Major Triad
    { 3, { 0, 3, 7, 0, 0, 0, 0 } },   // Minor Triad
    { 4, { 0, 4, 7, 11, 0, 0, 0 } },  // Major 7
    { 4, { 0, 3, 7, 10, 0, 0, 0 } },  // Minor 7
    { 4, { 0, 2, 5, 7, 0, 0, 0 } },   // Sus 2/4
    { 2, { 0, 7, 0, 0, 0, 0, 0 } },   // Fifths
    { 1, { 0, 0, 0, 0, 0, 0, 0 } },   // Octaves
};
inline constexpr int kNumScales = int (sizeof (kScales) / sizeof (kScales[0]));

// Semitone offset of ladder index idx (can be negative) from the scale root.
inline int ladderSemis (const ScaleDef& s, int idx)
{
    int oct = idx / s.n, deg = idx % s.n;
    if (deg < 0) { deg += s.n; --oct; }
    return 12 * oct + s.semis[deg];
}

class Arp
{
public:
    static constexpr int kMaxVoices = 12;

    struct Controls
    {
        bool  on = true;
        int   rate = 3, pattern = 4, range = 2;
        float gate = 1.2f, sparkle = 0.3f, hold = 0.6f;
        bool  track = true;
        int   key = 0, scale = 5;
        double beat = 0.0, samplesPerBeat = 24000.0;
    };

    void prepare (float sampleRate)
    {
        sr = sampleRate;
        tickRate = sr / 32.0f;
        ringLen = int (6.5f * sr);
        // voices may keep reading for up to 3.5 s after their source span was chosen
        recA.prepare (ringLen + int (3.7f * sr));
        recB.prepare (ringLen + int (3.7f * sr));
        lpA.set (0.30f * sr, 0.7f, sr);
        lpB.set (0.105f * sr, 0.7f, sr);
        recHp.setCutoff (70.0f, sr);
        tracker.prepare (sr);

        envAtk = 1.0f - std::exp (-1.0f / (0.001f * sr));
        envRel = 1.0f - std::exp (-1.0f / (0.060f * sr));
        gateAtk = 1.0f - std::exp (-1.0f / (0.003f * sr));
        gateRel = 1.0f - std::exp (-1.0f / (0.040f * sr));
        hangSamples = int (0.08f * sr);
        peakDecay = std::pow (0.5f, 1.0f / (4.0f * sr));
        sOn.setTime (0.03f, tickRate);
        rng.seed (0xA4B0u);
        reset();
    }

    void reset()
    {
        recA.clear(); recB.clear();
        lpA.reset(); lpB.reset(); recHp.reset();
        tracker.reset();
        for (auto& v : voices) v.active = false;
        recW = 0; recorded = 0; loudPos = 0; peakHold = 0.0f;
        env = 0.0f; gate = 0.0f; gateOpen = false; hang = 0;
        linger = 0.0f;
        lastStep = INT64_MIN; pendingOffset = -1; cursor = 0;
        lastRandom = -1; stepCount = 0;
        first = true;
        noteCount = 0; lastDegree = 0;
    }

    void control (const Controls& c)
    {
        ctl = c;
        sOn.set (c.on ? 1.0f : 0.0f);
        if (first) sOn.snap (sOn.target);
        const float on = sOn.tick();
        if (first) { rOn.snap (on); first = false; }
        rOn.begin (on, 32);

        // linger: after the input stops, the arp fades over T seconds (hold = 1: never)
        if (gateOpen)
            linger += 0.08f * (1.0f - linger);
        else if (c.hold < 0.98f)
        {
            const float T = 0.15f * std::pow (200.0f, clampf (c.hold, 0.0f, 1.0f));
            linger *= std::exp (-1.0f / (T * tickRate));
            if (linger < 1.0e-4f) linger = 0.0f;
        }

        // ---- step clock: find a step boundary inside the coming 32 samples
        cursor = 0;
        pendingOffset = -1;
        const double stepBeats = kDivBeats[std::clamp (c.rate, 0, 7)];
        const double beatInc = 1.0 / c.samplesPerBeat;
        const int64_t stepNow = int64_t (std::floor ((c.beat + 1.0e-7) / stepBeats));
        if (lastStep == INT64_MIN)
            lastStep = stepNow - 1;         // fire immediately on the first block

        if (stepNow != lastStep && stepNow != lastStep - 1)
        {
            // first block, or the transport jumped: fire right away
            pendingOffset = 0;
            pendingStep = stepNow;
        }
        else if (stepNow + 1 > lastStep)
        {
            const double nextBoundary = double (stepNow + 1) * stepBeats;
            const double samplesTo = (nextBoundary - c.beat) / beatInc;
            if (samplesTo < 31.5)
            {
                pendingOffset = std::clamp (int (std::ceil (samplesTo - 1.0e-6)), 0, 31);
                pendingStep = stepNow + 1;
            }
        }
        stepSamples = stepBeats * c.samplesPerBeat;
        trackOn = c.on && c.track;
    }

    // outL/outR receive the arp voices (overwritten).
    void process (const float* inL, const float* inR, float* outL, float* outR, int n)
    {
        // ---- record while the input is sounding; fire the step exactly on its sample
        for (int i = 0; i < n; ++i)
        {
            if (pendingOffset >= 0 && cursor + i >= pendingOffset)
            {
                lastStep = pendingStep;
                pendingOffset = -1;
                if (ctl.on && linger > 0.001f)
                    triggerStep (pendingStep, i);
            }

            const float x = 0.5f * (inL[i] + inR[i]);
            const float a = std::abs (x);
            env += (a > env ? envAtk : envRel) * (a - env);
            if (env > 0.006f)       { gateOpen = true; hang = hangSamples; }
            else if (env < 0.002f)  { if (hang > 0) --hang; else gateOpen = false; }
            const float gt = gateOpen ? 1.0f : 0.0f;
            gate += (gt > gate ? gateAtk : gateRel) * (gt - gate);
            peakHold = std::max (env, peakHold * peakDecay);

            const float xh = recHp.process (x);   // keeps enveloped rumble / DC out of the notes
            const float a1 = lpA.low (xh), b1 = lpB.low (xh);
            if (gate > 0.001f)
            {
                recA.setAt (int (recW & 0x3fffffff), a1 * gate);
                recB.setAt (int (recW & 0x3fffffff), b1 * gate);
                ++recW;
                if (recorded < int64_t (ringLen)) ++recorded;
                // remember where the sound was last "strong": decaying plucks are then
                // re-played from their body, not from their quiet tail
                if (env >= 0.5f * peakHold) loudPos = recW;
            }

            if (trackOn) tracker.push (x);

            outL[i] = 0.0f;
            outR[i] = 0.0f;
        }
        cursor += n;

        // ---- render voices
        const HannTable& hann = hannTable();
        for (auto& v : voices)
        {
            if (! v.active) continue;
            int i = v.startOffset;
            v.startOffset = 0;
            const Ring& ring = v.useB ? recB : recA;
            for (; i < n; ++i)
            {
                float w;
                if (v.ph < v.attack)
                    w = hann (0.5f * v.ph * v.invAttack);
                else
                {
                    const float f = hann (0.5f + 0.5f * (v.ph - v.attack) * v.invRelease);
                    w = f + (f * f * f - f) * v.pluck;
                }
                float s = ring.readHermite (v.pos);
                if (v.sparkle > 0.0f)
                    s += v.sparkle * recB.readHermite (v.pos2);
                s *= w;
                outL[i] += s * v.gl;
                outR[i] += s * v.gr;
                v.pos += v.inc;
                v.pos2 += 2.0 * v.inc;
                v.ph += v.phInc;
                if (v.ph >= 1.0f) { v.active = false; break; }
            }
        }

        for (int i = 0; i < n; ++i)
        {
            const float g = rOn.next();
            outL[i] *= g;
            outR[i] *= g;
        }
    }

    float pitchHz() const { return trackOn ? tracker.currentHz() : 0.0f; }

    int noteCount = 0, lastDegree = 0;

private:
    struct Voice
    {
        bool active = false, useB = false;
        int startOffset = 0;
        double pos = 0.0, pos2 = 0.0, inc = 1.0;
        float ph = 0.0f, phInc = 0.0f, sparkle = 0.0f;
        float gl = 0.7f, gr = 0.7f;
        float attack = 0.1f, invAttack = 10.0f, invRelease = 1.1f, pluck = 0.5f;
    };

    void triggerStep (int64_t step, int offset)
    {
        const int64_t anchor = loudPos;
        const ScaleDef& sc = kScales[std::clamp (ctl.scale, 0, kNumScales - 1)];
        const int range = std::clamp (ctl.range, 1, 4);
        const bool bloom = ctl.pattern == 4;

        // 7- and 6-note scales are walked in thirds so they sound like arpeggios, not runs
        const int stride = (sc.n >= 6 && ctl.pattern != 3) ? 2 : 1;
        const int lowest = range == 3 ? -sc.n : (range == 4 ? -2 * sc.n : 0);
        const int span = sc.n * range;                  // ladder positions covered
        const int M = span / stride + 1;                // number of pattern notes

        int k = 0;
        float vel = 1.0f;
        const int64_t pos = step;
        auto mod = [] (int64_t a, int64_t m) { return int (((a % m) + m) % m); };

        switch (ctl.pattern)
        {
            case 0: k = mod (pos, M); break;                                    // up
            case 1: k = M - 1 - mod (pos, M); break;                            // down
            case 2:                                                             // up-down
            {
                const int period = std::max (1, 2 * M - 2);
                const int i = mod (pos, period);
                k = i < M ? i : period - i;
                break;
            }
            case 3:                                                             // random
            {
                k = rng.irange (M);
                if (k == lastRandom && M > 1) k = (k + 1 + rng.irange (M - 1)) % M;
                lastRandom = k;
                break;
            }
            default:                                                            // bloom
            {
                const int i = mod (pos, M + 2);
                if (i >= M) return;                                             // the breath between blooms
                k = i;
                vel = 1.0f - 0.45f * float (k) / float (std::max (1, M - 1));
                break;
            }
        }
        if (! bloom)
            vel = (mod (pos, 4) == 0 ? 1.0f : 0.8f) * rng.range (0.9f, 1.0f);

        const int idx = lowest + k * stride;
        float semis = float (ladderSemis (sc, idx));

        // ---- pitch tracking: land exactly on the key's scale degrees
        const float inMidi = ctl.track ? tracker.currentMidi() : -1.0f;
        if (inMidi > 0.0f)
        {
            const float rel = inMidi - float (ctl.key);
            const int oct = int (std::floor (rel / 12.0f));
            int bestIdx = 0; float bestDist = 1.0e9f;
            for (int o = oct - 1; o <= oct + 1; ++o)
                for (int d = 0; d < sc.n; ++d)
                {
                    const float tone = float (12 * o + sc.semis[d]);
                    const float dist = std::abs (tone - rel);
                    if (dist < bestDist) { bestDist = dist; bestIdx = o * sc.n + d; }
                }
            semis = float (ladderSemis (sc, bestIdx + idx)) - rel;
        }

        const float ratio = clampf (semitonesToRatio (semis), 0.25f, 4.0f);

        // ---- duration and source span
        double dur = std::clamp (double (ctl.gate) * stepSamples * (bloom ? 2.5 : 1.0), 0.03 * double (sr), 3.5 * double (sr));
        const double maxSpan = double (std::min (recorded - (recW - loudPos), int64_t (ringLen - 64))) - 16.0;
        if (double (ratio) * dur > maxSpan) dur = maxSpan / double (ratio);
        if (dur < 0.03 * double (sr)) return;   // nothing (or not enough) recorded yet

        Voice* slot = nullptr;
        for (auto& v : voices)
            if (! v.active) { slot = &v; break; }
        if (slot == nullptr) return;
        Voice& v = *slot;

        v.inc = double (ratio);
        v.pos = double (anchor) - double (ratio) * dur - 8.0;
        v.useB = ratio > 2.2f;
        v.sparkle = 0.0f;
        if (ctl.sparkle > 0.001f && 2.0 * double (ratio) * dur <= maxSpan)
        {
            v.sparkle = 0.7f * ctl.sparkle;
            v.pos2 = double (anchor) - 2.0 * double (ratio) * dur - 8.0;
        }
        else
            v.pos2 = v.pos;

        v.ph = 0.0f;
        v.phInc = float (1.0 / dur);
        v.attack = clampf (float (0.008 * double (sr) / dur), 0.02f, 0.3f);
        if (bloom) v.attack = clampf (float (0.06 * double (sr) / dur), 0.05f, 0.4f);
        v.invAttack = 1.0f / v.attack;
        v.invRelease = 1.0f / (1.0f - v.attack);
        v.pluck = bloom ? 0.25f : 0.55f;
        v.startOffset = offset;

        // stereo: alternate sides, bloom opens outwards
        float pan = ((stepCount & 1) ? 0.45f : -0.45f) + 0.15f * rng.bi();
        if (bloom) pan = ((k & 1) ? 1.0f : -1.0f) * (0.15f + 0.75f * float (k) / float (std::max (1, M - 1)));
        ++stepCount;
        const float ang = (clampf (pan, -1.0f, 1.0f) + 1.0f) * 0.25f * kPi;
        const float overlap = std::max (1.0f, ctl.gate * (bloom ? 2.5f : 1.0f));
        const float amp = vel * linger / std::sqrt (overlap);
        v.gl = amp * std::cos (ang) * 1.4142f;
        v.gr = amp * std::sin (ang) * 1.4142f;
        v.active = true;

        ++noteCount;
        lastDegree = k;
    }

    float sr = 48000.0f, tickRate = 1500.0f;
    Controls ctl;
    Rng rng;

    Ring recA, recB;
    Svf lpA, lpB;
    OnePoleHP recHp;
    int ringLen = 0;
    int64_t recW = 0, recorded = 0, loudPos = 0;
    float peakHold = 0.0f, peakDecay = 1.0f;

    float env = 0.0f, gate = 0.0f, envAtk = 0, envRel = 0, gateAtk = 0, gateRel = 0;
    bool gateOpen = false;
    int hang = 0, hangSamples = 0;
    float linger = 0.0f;

    PitchTracker tracker;
    bool trackOn = true;

    std::array<Voice, kMaxVoices> voices;
    int64_t lastStep = INT64_MIN, pendingStep = 0;
    int pendingOffset = -1, cursor = 0, lastRandom = -1, stepCount = 0;
    double stepSamples = 12000.0;

    Smooth sOn;
    Ramp rOn;
    bool first = true;
};

} // namespace verdant
