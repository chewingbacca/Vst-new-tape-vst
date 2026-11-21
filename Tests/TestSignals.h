// Test-signal generators, a WAV writer and simple metrics for the offline harness.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace testsig
{

struct Stereo
{
    std::vector<float> l, r;
    double sampleRate = 48000.0;

    size_t size() const { return l.size(); }
    void resize (size_t n) { l.resize (n, 0.0f); r.resize (n, 0.0f); }
};

inline bool writeWav (const std::string& path, const Stereo& s)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (f == nullptr)
        return false;

    const uint32_t numFrames = static_cast<uint32_t> (s.size());
    const uint16_t channels = 2, bits = 32, format = 3;   // IEEE float
    const uint32_t rate = static_cast<uint32_t> (s.sampleRate);
    const uint32_t dataBytes = numFrames * channels * (bits / 8);
    const uint32_t byteRate = rate * channels * (bits / 8);
    const uint16_t blockAlign = static_cast<uint16_t> (channels * (bits / 8));
    const uint32_t riffSize = 4 + (8 + 16) + (8 + dataBytes);
    const uint32_t fmtSize = 16;

    std::fwrite ("RIFF", 1, 4, f);
    std::fwrite (&riffSize, 4, 1, f);
    std::fwrite ("WAVE", 1, 4, f);
    std::fwrite ("fmt ", 1, 4, f);
    std::fwrite (&fmtSize, 4, 1, f);
    std::fwrite (&format, 2, 1, f);
    std::fwrite (&channels, 2, 1, f);
    std::fwrite (&rate, 4, 1, f);
    std::fwrite (&byteRate, 4, 1, f);
    std::fwrite (&blockAlign, 2, 1, f);
    std::fwrite (&bits, 2, 1, f);
    std::fwrite ("data", 1, 4, f);
    std::fwrite (&dataBytes, 4, 1, f);

    std::vector<float> interleaved (static_cast<size_t> (numFrames) * 2);
    for (size_t i = 0; i < numFrames; ++i)
    {
        interleaved[2 * i] = s.l[i];
        interleaved[2 * i + 1] = s.r[i];
    }
    std::fwrite (interleaved.data(), sizeof (float), interleaved.size(), f);
    std::fclose (f);
    return true;
}

struct Lcg
{
    uint32_t s = 12345u;
    float next() { s = s * 1664525u + 1013904223u; return static_cast<float> (s >> 8) * (2.0f / 16777216.0f) - 1.0f; }
};

inline float dbToLin (float db) { return std::pow (10.0f, db / 20.0f); }

inline Stereo silence (double sr, double seconds)
{
    Stereo s;
    s.sampleRate = sr;
    s.resize (static_cast<size_t> (sr * seconds));
    return s;
}

inline Stereo impulse (double sr, double seconds, float level = 0.5f)
{
    Stereo s = silence (sr, seconds);
    const size_t at = static_cast<size_t> (0.05 * sr);
    if (at < s.size())
        s.l[at] = s.r[at] = level;
    return s;
}

inline Stereo sine (double sr, double seconds, double freq, float level)
{
    Stereo s = silence (sr, seconds);
    const size_t fade = static_cast<size_t> (0.01 * sr);
    for (size_t i = 0; i < s.size(); ++i)
    {
        float g = level;
        if (i < fade)
            g *= static_cast<float> (i) / static_cast<float> (fade);
        if (s.size() - i < fade)
            g *= static_cast<float> (s.size() - i) / static_cast<float> (fade);
        s.l[i] = s.r[i] = g * static_cast<float> (std::sin (2.0 * 3.141592653589793 * freq * static_cast<double> (i) / sr));
    }
    return s;
}

/** Exponential sine sweep f0 -> f1. */
inline Stereo sweep (double sr, double seconds, double f0, double f1, float level)
{
    Stereo s = silence (sr, seconds);
    const double k = std::log (f1 / f0);
    const size_t fade = static_cast<size_t> (0.02 * sr);
    for (size_t i = 0; i < s.size(); ++i)
    {
        const double t = static_cast<double> (i) / sr;
        const double phase = 2.0 * 3.141592653589793 * f0 * seconds / k * (std::exp (t / seconds * k) - 1.0);
        float g = level;
        if (i < fade)
            g *= static_cast<float> (i) / static_cast<float> (fade);
        if (s.size() - i < fade)
            g *= static_cast<float> (s.size() - i) / static_cast<float> (fade);
        s.l[i] = s.r[i] = g * static_cast<float> (std::sin (phase));
    }
    return s;
}

inline Stereo noise (double sr, double seconds, float level, bool decorrelated = true)
{
    Stereo s = silence (sr, seconds);
    Lcg a, b;
    b.s = 987654321u;
    for (size_t i = 0; i < s.size(); ++i)
    {
        s.l[i] = level * a.next();
        s.r[i] = decorrelated ? level * b.next() : s.l[i];
    }
    return s;
}

/** Repeating short tone bursts with silence between (shows tails, gates, envelopes). */
inline Stereo bursts (double sr, double seconds, double freq, float level)
{
    Stereo s = silence (sr, seconds);
    const double period = 1.0, onTime = 0.18;
    for (size_t i = 0; i < s.size(); ++i)
    {
        const double t = static_cast<double> (i) / sr;
        const double tp = std::fmod (t, period);
        if (tp < onTime)
        {
            const double env = std::min (1.0, tp / 0.003) * std::min (1.0, (onTime - tp) / 0.02);
            s.l[i] = s.r[i] = level * static_cast<float> (env * std::sin (2.0 * 3.141592653589793 * freq * t));
        }
    }
    return s;
}

/** A single sine note with a plucked envelope: fast attack, exponential decay (T60 = decaySeconds). */
inline Stereo decayingNote (double sr, double seconds, double freq, float level, double decaySeconds = 2.5)
{
    Stereo s = silence (sr, seconds);
    for (size_t i = 0; i < s.size(); ++i)
    {
        const double t = static_cast<double> (i) / sr - 0.05;
        if (t < 0.0)
            continue;
        const double env = std::min (1.0, t / 0.002) * std::pow (10.0, -3.0 * t / decaySeconds);
        s.l[i] = s.r[i] = level * static_cast<float> (env * std::sin (2.0 * 3.141592653589793 * freq * t));
    }
    return s;
}

//==============================================================================
// Plucked-string guitar DI model (extended Karplus-Strong) for realistic dynamics.
class GuitarString
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        buffer.assign (8192, 0.0f);
    }

    void pluck (double freqHz, float velocity, Lcg& rng)
    {
        // damp whatever is ringing, then queue a new excitation
        for (auto& v : buffer)
            v *= 0.15f;
        lpState *= 0.15f;

        period = sr / freqHz;
        const int len = static_cast<int> (period);
        const float bright = 0.25f + 0.7f * velocity;                      // harder picking = brighter
        const int pickPos = std::max (1, static_cast<int> (len * 0.13));   // pick-position comb

        std::vector<float> raw (static_cast<size_t> (len));
        float lp = 0.0f;
        for (int i = 0; i < len; ++i)
        {
            lp += bright * (rng.next() - lp);
            raw[static_cast<size_t> (i)] = lp;
        }
        excite.assign (static_cast<size_t> (len), 0.0f);
        for (int i = 0; i < len; ++i)
            excite[static_cast<size_t> (i)] = (raw[static_cast<size_t> (i)]
                                               - raw[static_cast<size_t> ((i + len - pickPos) % len)]) * velocity * 0.5f;
        exPos = 0;

        decay = static_cast<float> (std::pow (0.001, 1.0 / (freqHz * (3.2 + 2.5 * velocity))));   // T60 ~ 3-6 s
        damp = static_cast<float> (0.25 + 0.2 * (1.0 - velocity) + std::min (0.25, freqHz / 4000.0));
        active = true;
        muting = false;
        muteGain = 1.0f;
    }

    void mute() { muting = true; }

    float tick()
    {
        if (! active)
            return 0.0f;

        const int size = static_cast<int> (buffer.size());
        double rp = static_cast<double> (writePos) - period;
        if (rp < 0.0)
            rp += size;
        const int i0 = static_cast<int> (rp);
        const float frac = static_cast<float> (rp - i0);
        const float a = buffer[static_cast<size_t> (i0 % size)];
        const float b = buffer[static_cast<size_t> ((i0 + 1) % size)];
        const float delayed = a + frac * (b - a);

        lpState += (1.0f - damp) * (delayed - lpState);
        float out = lpState * decay;
        if (exPos < excite.size())
            out += excite[exPos++];

        if (muting)
        {
            muteGain *= 0.9990f;
            out *= muteGain;
            if (muteGain < 1.0e-4f)
            {
                active = false;
                std::fill (buffer.begin(), buffer.end(), 0.0f);
                lpState = 0.0f;
            }
        }

        buffer[static_cast<size_t> (writePos)] = out;
        writePos = (writePos + 1) % size;
        return out;
    }

private:
    std::vector<float> buffer, excite;
    size_t exPos = 0;
    double sr = 48000.0, period = 100.0;
    int writePos = 0;
    float lpState = 0.0f, decay = 0.99f, damp = 0.4f, muteGain = 1.0f;
    bool active = false, muting = false;
};

/** An ~10 s phrase: soft arpeggio, strummed chords with accents, a held chord, single notes, a final hard chord
    ringing out into silence. Mono DI in both channels, peaks around -6 dBFS, quiet passages near -30 dBFS. */
inline Stereo guitarPhrase (double sr, double seconds = 10.0)
{
    Stereo s = silence (sr, seconds);
    GuitarString strings[6];
    for (auto& g : strings)
        g.prepare (sr);

    struct Event { double time; int string; double freq; float vel; bool mute; };
    std::vector<Event> events;
    Lcg rng;

    auto midi = [] (int note) { return 440.0 * std::pow (2.0, (note - 69) / 12.0); };

    // chord shapes as MIDI notes per string (low E .. high e); -1 = not played
    const int chordE[6]    = { 40, 47, 52, 56, 59, 64 };   // E major
    const int chordAmaj7[6]= { -1, 45, 52, 56, 61, 64 };   // Amaj7
    const int chordCsm[6]  = { -1, 49, 56, 61, 64, 68 };   // C#m
    const int chordB4[6]   = { -1, 47, 54, 59, 64, 66 };   // Bsus4-ish

    auto strum = [&] (double t, const int* chord, float vel, bool down, double spread)
    {
        int idx = 0;
        for (int k = 0; k < 6; ++k)
        {
            const int st = down ? k : 5 - k;
            if (chord[st] < 0)
                continue;
            const float v = vel * (0.8f + 0.2f * (rng.next() * 0.5f + 0.5f));
            events.push_back ({ t + idx * spread + 0.002 * rng.next(), st, midi (chord[st]), v, false });
            ++idx;
        }
    };

    // 0.0 - 1.6 s: soft arpeggio on E
    const int arp[] = { 0, 2, 3, 4, 5, 4, 3, 2 };
    for (int i = 0; i < 8; ++i)
        events.push_back ({ 0.05 + i * 0.2, arp[i], midi (chordE[arp[i]]), 0.28f + 0.05f * (i % 3), false });

    // 1.7 - 5.3 s: strummed eighths at ~112 bpm with accents (down-up pattern)
    const double beat = 60.0 / 112.0 / 2.0;
    const int* prog[] = { chordE, chordE, chordAmaj7, chordAmaj7, chordCsm, chordCsm, chordB4, chordB4 };
    const float accents[] = { 1.0f, 0.45f, 0.7f, 0.5f, 0.9f, 0.4f, 0.75f, 0.55f };
    for (int i = 0; i < 14; ++i)
    {
        const bool down = (i % 2) == 0;
        const float vel = accents[i % 8] * 0.85f;
        strum (1.7 + i * beat, prog[(i / 2) % 8], vel, down, down ? 0.011 : 0.007);
    }

    // 5.5 s: big held chord
    strum (5.5, chordAmaj7, 1.0f, true, 0.016);

    // 7.0 - 8.2 s: single-note line on the high strings
    const int line[] = { 68, 66, 64, 61, 64 };
    for (int i = 0; i < 5; ++i)
        events.push_back ({ 7.0 + i * 0.26, i % 2 == 0 ? 5 : 4, midi (line[i]), 0.6f, false });

    // 8.4 s: final hard chord, then hand-muted at 9.2 s -> silence
    strum (8.4, chordE, 1.0f, true, 0.012);
    for (int st = 0; st < 6; ++st)
        events.push_back ({ 9.2, st, 0.0, 0.0f, true });

    std::sort (events.begin(), events.end(), [] (const Event& a, const Event& b) { return a.time < b.time; });

    size_t next = 0;
    // pickup + DI colouring: gentle resonant low-pass around 3.5 kHz, high-pass at 70 Hz
    float hp = 0.0f, lp1 = 0.0f, lp2 = 0.0f;
    const float hpA = 1.0f - std::exp (-2.0f * 3.14159265f * 70.0f / static_cast<float> (sr));
    const float lpA = 1.0f - std::exp (-2.0f * 3.14159265f * 3800.0f / static_cast<float> (sr));

    for (size_t i = 0; i < s.size(); ++i)
    {
        const double t = static_cast<double> (i) / sr;
        while (next < events.size() && events[next].time <= t)
        {
            const auto& e = events[next++];
            if (e.mute)
                strings[e.string].mute();
            else
                strings[e.string].pluck (e.freq, e.vel, rng);
        }

        float x = 0.0f;
        for (auto& g : strings)
            x += g.tick();

        hp += hpA * (x - hp);
        x -= hp;
        lp1 += lpA * (x - lp1);
        lp2 += lpA * (lp1 - lp2);
        s.l[i] = s.r[i] = lp2;
    }

    // normalise peak to -6 dBFS
    float peak = 1.0e-9f;
    for (float v : s.l)
        peak = std::max (peak, std::abs (v));
    const float g = 0.5f / peak;
    for (size_t i = 0; i < s.size(); ++i)
    {
        s.l[i] *= g;
        s.r[i] *= g;
    }
    return s;
}

//==============================================================================
struct Metrics
{
    double peak = 0.0, rms = 0.0, dc = 0.0, tailRms = 0.0, maxStep = 0.0, corr = 1.0;
    long badSamples = 0, overs = 0;
};

inline Metrics measure (const Stereo& s, double tailSeconds = 0.5)
{
    Metrics m;
    const size_t n = s.size();
    if (n == 0)
        return m;

    double sumSq = 0.0, sumL = 0.0, sumR = 0.0, sumLR = 0.0, sumLL = 0.0, sumRR = 0.0, tailSq = 0.0;
    const size_t tailStart = n - std::min (n, static_cast<size_t> (tailSeconds * s.sampleRate));
    float prevL = 0.0f, prevR = 0.0f;

    for (size_t i = 0; i < n; ++i)
    {
        const float l = s.l[i], r = s.r[i];
        if (! std::isfinite (l) || ! std::isfinite (r))
        {
            ++m.badSamples;
            continue;
        }
        const double a = std::max (std::abs (l), std::abs (r));
        m.peak = std::max (m.peak, a);
        if (a >= 0.999)
            ++m.overs;
        sumSq += 0.5 * (static_cast<double> (l) * l + static_cast<double> (r) * r);
        sumL += l;
        sumR += r;
        sumLR += static_cast<double> (l) * r;
        sumLL += static_cast<double> (l) * l;
        sumRR += static_cast<double> (r) * r;
        if (i >= tailStart)
            tailSq += 0.5 * (static_cast<double> (l) * l + static_cast<double> (r) * r);
        m.maxStep = std::max (m.maxStep, static_cast<double> (std::max (std::abs (l - prevL), std::abs (r - prevR))));
        prevL = l;
        prevR = r;
    }

    m.rms = std::sqrt (sumSq / static_cast<double> (n));
    m.dc = std::max (std::abs (sumL), std::abs (sumR)) / static_cast<double> (n);
    m.tailRms = std::sqrt (tailSq / static_cast<double> (std::max<size_t> (1, n - tailStart)));
    m.corr = sumLR / std::sqrt (std::max (1.0e-30, sumLL * sumRR));
    return m;
}

inline double toDb (double x) { return 20.0 * std::log10 (std::max (x, 1.0e-12)); }

} // namespace testsig
