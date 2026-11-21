// Splinter DSP — the audio-rate step effects. Each one is driven by a 0..1
// activity envelope (already smoothed) so engaging and releasing never clicks.
#pragma once
#include "Common.h"

namespace splinter
{
// ---------------------------------------------------------------- TPT state variable filter
struct Svf
{
    float ic1 = 0.0f, ic2 = 0.0f, g = 0.1f, k = 1.0f, a1 = 0, a2 = 0, a3 = 0;
    void reset() { ic1 = ic2 = 0.0f; }
    void set (double sampleRate, double freq, double q)
    {
        freq = clampv (freq, 10.0, sampleRate * 0.47);
        g = (float) std::tan (kPi * freq / sampleRate);
        k = (float) (1.0 / std::max (0.3, q));
        a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    inline void process (float x, float& lp, float& bp, float& hp)
    {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1; ic2 = 2.0f * v2 - ic2;
        lp = v2; bp = v1; hp = x - k * v1 - v2;
    }
};

struct OnePole
{
    float z = 0.0f, a = 1.0f;
    void setCutoff (double sampleRate, double hz) { a = (float) (1.0 - std::exp (-kTwoPi * clampv (hz, 1.0, sampleRate * 0.49) / sampleRate)); }
    void reset() { z = 0.0f; }
    inline float lp (float x) { z += a * (x - z); return z; }
    inline float hp (float x) { z += a * (x - z); return x - z; }
};

// ---------------------------------------------------------------- bit crusher / sample-rate reducer
class Crusher
{
public:
    void prepare (double sampleRate) { sr = sampleRate; reset(); }
    void reset() { phase = 1.0f; heldL = heldR = 0.0f; }
    // bits 1..16, rateHz = target sample rate
    void set (float bits, float rateHz)
    {
        levels = std::exp2 (clampv (bits, 1.0f, 16.0f) - 1.0f);
        inc = clampv (rateHz / (float) sr, 0.0005f, 1.0f);
    }
    inline void process (float& l, float& r, float amount)
    {
        if (amount <= 0.0f) { phase = 1.0f; return; }
        phase += inc;
        if (phase >= 1.0f)
        {
            phase -= 1.0f;
            heldL = std::round (l * levels) / levels;
            heldR = std::round (r * levels) / levels;
        }
        l += amount * (heldL - l); r += amount * (heldR - r);
    }
private:
    double sr = 48000.0;
    float phase = 1.0f, inc = 1.0f, levels = 32768.0f, heldL = 0.0f, heldR = 0.0f;
};

// ---------------------------------------------------------------- tuned comb resonator
class CombResonator
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        size = 1; while (size < (int) (sampleRate / 20.0) + 8) size <<= 1;
        for (auto& b : buf) b.assign ((size_t) size, 0.0f);
        reset();
    }
    void reset() { for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f); w = 0; for (auto& d : damp) d.reset(); delaySm = 0.0f; }
    void set (float freqHz, float feedback, float dampHz)
    {
        delayTarget = clampv ((float) sr / std::max (20.0f, freqHz), 2.0f, (float) size - 4.0f);
        fb = clampv (feedback, 0.0f, 0.985f);
        for (auto& d : damp) d.setCutoff (sr, dampHz);
        if (delaySm <= 0.0f) delaySm = delayTarget;
    }
    inline void process (float& l, float& r, float amount)
    {
        if (amount <= 0.0f && idle) return;
        delaySm += 0.002f * (delayTarget - delaySm);
        const float rp = (float) w - delaySm;
        const int i0 = (int) std::floor (rp);
        const float f = rp - (float) i0;
        float in[2] = { l, r }, out[2];
        float energy = 0.0f;
        for (int c = 0; c < 2; ++c)
        {
            const float a = buf[c][(size_t) (i0 & (size - 1))], b = buf[c][(size_t) ((i0 + 1) & (size - 1))];
            const float d = damp[c].lp (a + f * (b - a));
            const float y = in[c] * amount + fb * d;      // input is gated: the tail rings out
            buf[c][(size_t) w] = y;
            out[c] = y * (1.0f - fb * 0.7f);
            energy += std::fabs (y);
        }
        w = (w + 1) & (size - 1);
        idle = amount <= 0.0f && energy < 1.0e-6f;
        l = l * (1.0f - 0.6f * amount) + out[0];
        r = r * (1.0f - 0.6f * amount) + out[1];
    }
private:
    double sr = 48000.0;
    std::vector<float> buf[2];
    OnePole damp[2];
    int size = 4096, w = 0;
    float delayTarget = 100.0f, delaySm = 0.0f, fb = 0.9f;
    bool idle = true;
};

// ---------------------------------------------------------------- ring modulator
class RingMod
{
public:
    void prepare (double sampleRate) { sr = sampleRate; phase = 0.0; }
    void set (float freqHz) { inc = clampv ((double) freqHz, 1.0, sr * 0.45) / sr; }
    inline void process (float& l, float& r, float amount)
    {
        if (amount <= 0.0f) return;
        phase += inc; if (phase >= 1.0) phase -= 1.0;
        const float c = (float) std::sin (kTwoPi * phase);
        const float m = 1.0f + amount * (c - 1.0f);
        l *= m; r *= m;
    }
private:
    double sr = 48000.0, phase = 0.0, inc = 0.01;
};

// ---------------------------------------------------------------- sweep filter
class SweepFilter
{
public:
    void prepare (double sampleRate) { sr = sampleRate; reset(); }
    void reset() { for (auto& f : svf) f.reset(); counter = 0; }
    // shape: 1 LP close, 2 LP open, 3 HP rise, 4 HP fall, 5 BP up, 6 BP down. t = 0..1 through the run.
    void set (int shape, float t, float resonance)
    {
        shapeNow = clampv (shape, 1, 6);
        static const float f0[7] = { 0, 16000.0f, 250.0f, 40.0f, 5000.0f, 200.0f, 6000.0f };
        static const float f1[7] = { 0, 250.0f, 16000.0f, 5000.0f, 40.0f, 6000.0f, 200.0f };
        t = clampv (t, 0.0f, 1.0f);
        cutoff = f0[shapeNow] * std::pow (f1[shapeNow] / f0[shapeNow], t);
        q = 0.7f + resonance * 7.0f;
    }
    inline void process (float& l, float& r, float amount)
    {
        if (amount <= 0.0f) { if (! wasIdle) { reset(); wasIdle = true; } return; }
        if (wasIdle) { cutSm = cutoff; for (auto& f : svf) f.set (sr, cutSm, q); wasIdle = false; }
        if ((counter++ & 15) == 0) { cutSm += 0.25f * (cutoff - cutSm); for (auto& f : svf) f.set (sr, cutSm, q); }
        float in[2] = { l, r };
        float out[2];
        for (int c = 0; c < 2; ++c)
        {
            float lp, bp, hp;
            svf[c].process (in[c], lp, bp, hp);
            out[c] = shapeNow <= 2 ? lp : (shapeNow <= 4 ? hp : bp * (1.0f / std::max (0.5f, q * 0.35f)) * 1.6f);
        }
        l += amount * (out[0] - l); r += amount * (out[1] - r);
    }
private:
    double sr = 48000.0;
    Svf svf[2];
    int shapeNow = 1, counter = 0;
    float cutoff = 1000.0f, cutSm = 1000.0f, q = 1.0f;
    bool wasIdle = true;
};

// ---------------------------------------------------------------- granular freeze
class GranularFreeze
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        cap = (int) (sampleRate * 1.2) + 16;
        for (auto& b : buf) b.assign ((size_t) cap, 0.0f);
        reset();
    }
    void reset() { recorded = 0; running = false; for (auto& g : grains) g.active = false; untilNext = 0; }
    // Call when a freeze gesture starts.
    void trigger (float grainSeconds, uint32_t seed)
    {
        grainLen = clampv ((int) (grainSeconds * sr), 64, cap / 2);
        recorded = 0; running = true; rng = Rng (seed | 1U);
        for (auto& g : grains) g.active = false;
        untilNext = grainLen; // first grain starts once one grain of audio exists
    }
    void stop() { running = false; }
    inline void process (float& l, float& r, float amount)
    {
        if (! running && amount <= 0.0f) return;
        if (running && recorded < 2 * grainLen && recorded < cap) { buf[0][(size_t) recorded] = l; buf[1][(size_t) recorded] = r; ++recorded; }
        if (running && --untilNext <= 0)
        {
            untilNext = grainLen / 2;
            for (auto& g : grains)
                if (! g.active)
                {
                    const int room = std::max (0, recorded - grainLen);
                    g.start = room > 0 ? rng.range (room + 1) : 0;
                    g.pos = 0; g.active = true; g.reverse = false;
                    g.pan = 0.5f + 0.35f * rng.bipolar();
                    break;
                }
        }
        float gl = 0.0f, gr = 0.0f;
        bool any = false;
        const float invLen = 1.0f / (float) grainLen;
        for (auto& g : grains)
        {
            if (! g.active) continue;
            any = true;
            const float w = 0.5f - 0.5f * std::cos ((float) kTwoPi * ((float) g.pos + 0.5f) * invLen);
            const int i = std::min (g.start + g.pos, cap - 1);
            const float sl = buf[0][(size_t) i], sr2 = buf[1][(size_t) i];
            gl += w * (sl * (1.0f - (g.pan - 0.5f) * 0.6f));
            gr += w * (sr2 * (1.0f + (g.pan - 0.5f) * 0.6f));
            if (++g.pos >= grainLen) g.active = false;
        }
        // until the first grain exists the dry signal passes; then crossfade over
        const float wet = any ? amount * frozenMix : 0.0f;
        frozenMix += ((any ? 1.0f : 0.0f) - frozenMix) * 0.004f;
        l += wet * (gl - l); r += wet * (gr - r);
        if (! running && ! any) frozenMix = 0.0f;
    }
private:
    struct Grain { int start = 0, pos = 0; float pan = 0.5f; bool active = false, reverse = false; };
    double sr = 48000.0;
    std::vector<float> buf[2];
    Grain grains[4];
    Rng rng;
    int cap = 1, recorded = 0, grainLen = 2048, untilNext = 0;
    bool running = false;
    float frozenMix = 0.0f;
};

// ---------------------------------------------------------------- delay throw
class ThrowDelay
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        size = 1; while (size < (int) (sampleRate * 2.5)) size <<= 1;
        for (auto& b : buf) b.assign ((size_t) size, 0.0f);
        reset();
    }
    void reset() { for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f); w = 0; for (auto& f : lp) f.reset(); for (auto& f : hp) f.reset(); delaySm = 0.0f; energy = 0.0f; }
    void set (float delaySamples, float feedback, float toneHz, float pingPong)
    {
        delayTarget = clampv (delaySamples, 16.0f, (float) size - 8.0f);
        if (delaySm <= 0.0f) delaySm = delayTarget;
        fb = clampv (feedback, 0.0f, 0.95f); pp = clampv (pingPong, 0.0f, 1.0f);
        for (auto& f : lp) f.setCutoff (sr, toneHz);
        for (auto& f : hp) f.setCutoff (sr, 120.0);
    }
    // send = 0..1 (the throw envelope). Returns the wet signal only.
    inline void process (float inL, float inR, float send, float& outL, float& outR)
    {
        if (send <= 0.0f && energy < 1.0e-7f) { outL = outR = 0.0f; if (! cleared) { cleared = true; } return; }
        cleared = false;
        delaySm += 0.0005f * (delayTarget - delaySm);
        const float rp = (float) w - delaySm;
        const int i0 = (int) std::floor (rp);
        const float f = rp - (float) i0;
        float d[2];
        for (int c = 0; c < 2; ++c)
        {
            const float a = buf[c][(size_t) (i0 & (size - 1))], b = buf[c][(size_t) ((i0 + 1) & (size - 1))];
            d[c] = hp[c].hp (lp[c].lp (a + f * (b - a)));
        }
        const float mono = 0.5f * (inL + inR);
        // ping-pong: the input enters on the left, repeats bounce between sides
        const float inl = send * lerp (inL, mono * 1.4f, pp);
        const float inr = send * lerp (inR, 0.0f, pp);
        buf[0][(size_t) w] = inl + fb * lerp (d[0], d[1], pp);
        buf[1][(size_t) w] = inr + fb * lerp (d[1], d[0], pp);
        w = (w + 1) & (size - 1);
        outL = d[0]; outR = d[1];
        energy = 0.9995f * energy + std::fabs (d[0]) + std::fabs (d[1]) + std::fabs (inl) + std::fabs (inr);
    }
private:
    double sr = 48000.0;
    std::vector<float> buf[2];
    OnePole lp[2], hp[2];
    int size = 2, w = 0;
    float delayTarget = 1000.0f, delaySm = 0.0f, fb = 0.5f, pp = 1.0f, energy = 0.0f;
    bool cleared = true;
};

// ---------------------------------------------------------------- reverb throw (8-line FDN)
class ThrowReverb
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        static const double ms[kLines] = { 31.7, 37.1, 41.9, 47.3, 53.9, 61.3, 67.9, 74.3 };
        for (int i = 0; i < kLines; ++i)
        {
            len[i] = (int) (ms[i] * 0.001 * sampleRate) | 1;
            int sz = 1; while (sz < len[i] + 4) sz <<= 1;
            line[i].assign ((size_t) sz, 0.0f); mask[i] = sz - 1;
        }
        static const double apMs[kAps] = { 4.7, 7.9, 11.3, 13.7 };
        for (int i = 0; i < kAps; ++i)
            for (int c = 0; c < 2; ++c)
            {
                apLen[c][i] = (int) ((apMs[i] + (c ? 0.37 : 0.0)) * 0.001 * sampleRate) | 1;
                int sz = 1; while (sz < apLen[c][i] + 4) sz <<= 1;
                ap[c][i].assign ((size_t) sz, 0.0f); apMask[c][i] = sz - 1;
            }
        reset();
        set (2.0f, 6000.0f);
    }
    void reset()
    {
        for (auto& l : line) std::fill (l.begin(), l.end(), 0.0f);
        for (auto& c : ap) for (auto& a : c) std::fill (a.begin(), a.end(), 0.0f);
        for (auto& d : damp) d.reset();
        w = 0; energy = 0.0f;
    }
    void set (float rt60Seconds, float dampHz)
    {
        for (int i = 0; i < kLines; ++i)
        {
            gain[i] = (float) std::pow (10.0, -3.0 * (double) len[i] / (std::max (0.1f, rt60Seconds) * sr));
            damp[i].setCutoff (sr, dampHz);
        }
    }
    inline void process (float inL, float inR, float send, float& outL, float& outR)
    {
        if (send <= 0.0f && energy < 1.0e-7f) { outL = outR = 0.0f; return; }
        float x[2] = { inL * send, inR * send };
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < kAps; ++i)
            {
                auto& a = ap[c][i];
                const float d = a[(size_t) ((w - apLen[c][i]) & apMask[c][i])];
                const float v = x[c] + 0.6f * d;
                a[(size_t) (w & apMask[c][i])] = v;
                x[c] = d - 0.6f * v;
            }
        float o[kLines], sum = 0.0f;
        for (int i = 0; i < kLines; ++i)
        {
            o[i] = damp[i].lp (line[i][(size_t) ((w - len[i]) & mask[i])]) * gain[i];
            sum += o[i];
        }
        sum *= 2.0f / (float) kLines; // Householder reflection
        float e = 0.0f;
        for (int i = 0; i < kLines; ++i)
        {
            const float v = o[i] - sum + ((i & 1) ? x[1] : x[0]);
            line[i][(size_t) (w & mask[i])] = v;
            e += std::fabs (v);
        }
        ++w;
        outL = 0.5f * (o[0] - o[2] + o[4] - o[6]);
        outR = 0.5f * (o[1] - o[3] + o[5] - o[7]);
        energy = e;
    }
private:
    static constexpr int kLines = 8, kAps = 4;
    double sr = 48000.0;
    std::vector<float> line[kLines], ap[2][kAps];
    int len[kLines] = {}, mask[kLines] = {}, apLen[2][kAps] = {}, apMask[2][kAps] = {};
    float gain[kLines] = {};
    OnePole damp[kLines];
    int w = 0;
    float energy = 0.0f;
};

// ---------------------------------------------------------------- look-ahead limiter
// gain = boxcar average of the sliding minimum of the required gain, so the
// reduction is fully in place by the time a peak leaves the delay line: the
// output can never exceed the ceiling, and the gain moves smoothly.
class Limiter
{
public:
    void prepare (double sampleRate)
    {
        look = std::max (8, (int) (0.0015 * sampleRate));
        win = look + 1;
        size = 1; while (size < win * 2 + 4) size <<= 1;
        for (auto& d : dly) d.assign ((size_t) size, 0.0f);
        gbuf.assign ((size_t) size, 1.0f);
        mbuf.assign ((size_t) size, 1.0f);
        dq.assign ((size_t) size, 0);
        release = (float) std::exp (-1.0 / (0.08 * sampleRate));
        reset();
    }
    void reset()
    {
        for (auto& d : dly) std::fill (d.begin(), d.end(), 0.0f);
        std::fill (gbuf.begin(), gbuf.end(), 1.0f);
        std::fill (mbuf.begin(), mbuf.end(), 1.0f);
        n = 0; head = tail = 0; env = 1.0f; sum = (double) win; reduction = 1.0f;
    }
    int latency() const { return look; }
    float gainReduction() const { return reduction; }
    void setCeiling (float c) { ceiling = c; }

    inline void process (float& l, float& r)
    {
        const int m = size - 1;
        const float peak = std::max (std::fabs (l), std::fabs (r));
        const float want = peak > ceiling ? ceiling / peak : 1.0f;
        env = want < env ? want : want + (env - want) * release; // instant attack, smooth release
        const float gIn = env;
        gbuf[(size_t) (n & m)] = gIn;
        while (head != tail && gbuf[(size_t) (dq[(size_t) ((tail - 1) & m)] & m)] >= gIn) tail = (tail - 1) & m;
        dq[(size_t) tail] = n; tail = (tail + 1) & m;
        while (n - dq[(size_t) head] >= win) head = (head + 1) & m;
        const float gMin = gbuf[(size_t) (dq[(size_t) head] & m)];
        sum += (double) gMin - (double) mbuf[(size_t) ((n - win) & m)];
        mbuf[(size_t) (n & m)] = gMin;
        const float g = (float) (sum / (double) win);
        dly[0][(size_t) (n & m)] = l; dly[1][(size_t) (n & m)] = r;
        const int rd = (n - look) & m;
        l = clampv (dly[0][(size_t) rd] * g, -ceiling, ceiling);
        r = clampv (dly[1][(size_t) rd] * g, -ceiling, ceiling);
        reduction = g;
        ++n;
        if (n >= (1 << 30)) rebase();
    }
private:
    void rebase()
    {
        // keep indices small; the deque holds absolute indices
        const int shift = n & ~(size - 1);
        const int m = size - 1;
        for (int i = head; i != tail; i = (i + 1) & m) dq[(size_t) i] -= shift;
        n -= shift;
    }
    std::vector<float> dly[2], gbuf, mbuf;
    std::vector<int> dq;
    double sum = 1.0;
    int look = 64, win = 65, size = 256, n = 0, head = 0, tail = 0;
    float env = 1.0f, release = 0.999f, ceiling = 0.966f, reduction = 1.0f;
};

// ---------------------------------------------------------------- simple fixed delay
class DelayLine
{
public:
    void prepare (int maxSamples)
    {
        size = 1; while (size < maxSamples + 2) size <<= 1;
        buf.assign ((size_t) size, 0.0f); w = 0;
    }
    void reset() { std::fill (buf.begin(), buf.end(), 0.0f); }
    inline float process (float x, int delay)
    {
        buf[(size_t) w] = x;
        const float y = buf[(size_t) ((w - delay) & (size - 1))];
        w = (w + 1) & (size - 1);
        return y;
    }
private:
    std::vector<float> buf;
    int size = 2, w = 0;
};

// Tilt EQ: first-order low/high split around a pivot, opposite gains.
struct TiltEq
{
    OnePole split[2];
    float gLow = 1.0f, gHigh = 1.0f;
    void set (double sampleRate, float tiltDb)
    {
        for (auto& s : split) s.setCutoff (sampleRate, 900.0);
        gLow = dbToGain (-tiltDb); gHigh = dbToGain (tiltDb);
    }
    void reset() { for (auto& s : split) s.reset(); }
    inline void process (float& l, float& r)
    {
        const float ll = split[0].lp (l), lr = split[1].lp (r);
        l = ll * gLow + (l - ll) * gHigh;
        r = lr * gLow + (r - lr) * gHigh;
    }
};
} // namespace splinter
