#include "DemoLoops.h"
#include "StepFx.h"

namespace splinter
{
const DemoLoopInfo& demoLoopInfo (int index)
{
    static const DemoLoopInfo info[kNumDemoLoops] = {
        { "Dusty Break",  92.0,  8.0, 0, 1, true  },
        { "Glass Arp",   120.0,  8.0, 9, 1, false },   // A minor
        { "Sub Roller",  140.0,  8.0, 5, 1, false },   // F minor
        { "Warm Keys",    85.0, 16.0, 2, 1, false },   // D minor
        { "Vowel Hook",  110.0,  8.0, 0, 1, false },   // C minor
        { "Full Groove", 126.0, 16.0, 7, 1, false },   // G minor
    };
    return info[clampv (index, 0, kNumDemoLoops - 1)];
}

namespace
{
double hz (double midi) { return 440.0 * std::exp2 ((midi - 69.0) / 12.0); }

// A stereo canvas that wraps: anything rendered past the end folds back to the
// start, so every loop is seamless.
struct Canvas
{
    AudioData& a; double sr; int64_t n;
    Canvas (AudioData& d, double sampleRate, double seconds) : a (d), sr (sampleRate), n ((int64_t) std::llround (seconds * sampleRate))
    { a.sampleRate = sampleRate; a.resize (n); }
    inline void add (int64_t i, float l, float r) { i %= n; if (i < 0) i += n; a.ch[0][(size_t) i] += l; a.ch[1][(size_t) i] += r; }
};

void kick (Canvas& c, double t, float gain, double baseHz = 48.0, double decay = 0.28)
{
    const int64_t s0 = (int64_t) (t * c.sr), len = (int64_t) (decay * 2.2 * c.sr);
    double ph = 0.0;
    for (int64_t i = 0; i < len; ++i)
    {
        const double x = (double) i / c.sr;
        const double f = baseHz + 150.0 * std::exp (-x / 0.022) + 60.0 * std::exp (-x / 0.006);
        ph += f / c.sr;
        const double env = std::exp (-x / decay) * std::min (1.0, x / 0.0006);
        const double click = (x < 0.004 ? (1.0 - x / 0.004) * 0.35 * std::sin (kTwoPi * 1800.0 * x) : 0.0);
        const float v = (float) (std::tanh ((std::sin (kTwoPi * ph) * env + click) * 1.6) * gain * 0.8);
        c.add (s0 + i, v, v);
    }
}

void snare (Canvas& c, double t, float gain, Rng& rng, double decay = 0.14)
{
    const int64_t s0 = (int64_t) (t * c.sr), len = (int64_t) (decay * 5.0 * c.sr);
    Svf bp; bp.set (c.sr, 3200.0, 0.8); OnePole hp; hp.setCutoff (c.sr, 350.0);
    double ph = 0.0;
    for (int64_t i = 0; i < len; ++i)
    {
        const double x = (double) i / c.sr;
        float lp, b, h; bp.process (rng.bipolar(), lp, b, h);
        const double noise = (b * 0.9 + h * 0.5) * std::exp (-x / decay);
        ph += (185.0 + 90.0 * std::exp (-x / 0.012)) / c.sr;
        const double tone = std::sin (kTwoPi * ph) * std::exp (-x / 0.07) * 0.55;
        const float v = hp.hp ((float) ((noise + tone) * std::min (1.0, x / 0.0004))) * gain * 0.6f;
        c.add (s0 + i, v * 0.95f, v * 1.05f);
    }
}

void hat (Canvas& c, double t, float gain, Rng& rng, double decay = 0.03, float pan = 0.2f)
{
    const int64_t s0 = (int64_t) (t * c.sr), len = (int64_t) (decay * 6.0 * c.sr);
    Svf f; f.set (c.sr, 8500.0, 1.1);
    for (int64_t i = 0; i < len; ++i)
    {
        const double x = (double) i / c.sr;
        float lp, b, h; f.process (rng.bipolar(), lp, b, h);
        const float v = (float) ((h * 0.8 + b * 0.4) * std::exp (-x / decay) * std::min (1.0, x / 0.0003)) * gain * 0.35f;
        c.add (s0 + i, v * (1.0f - pan), v * (1.0f + pan));
    }
}

void clap (Canvas& c, double t, float gain, Rng& rng)
{
    for (int k = 0; k < 4; ++k)
    {
        const double tt = t + k * 0.011;
        const int64_t s0 = (int64_t) (tt * c.sr), len = (int64_t) ((k == 3 ? 0.22 : 0.02) * c.sr);
        Svf f; f.set (c.sr, 1500.0, 1.4);
        for (int64_t i = 0; i < len; ++i)
        {
            const double x = (double) i / c.sr;
            float lp, b, h; f.process (rng.bipolar(), lp, b, h);
            const float v = (float) (b * std::exp (-x / (k == 3 ? 0.06 : 0.008)) * std::min (1.0, x / 0.0003)) * gain * 0.55f;
            c.add (s0 + i, v * 1.1f, v * 0.9f);
        }
    }
}

// 808-style sub: sine with pitch settle and soft saturation
void sub (Canvas& c, double t, double dur, double midi, float gain, double glideFrom = 0.0)
{
    const int64_t s0 = (int64_t) (t * c.sr), len = (int64_t) ((dur + 0.08) * c.sr);
    double ph = 0.0; const double f1 = hz (midi), f0 = glideFrom > 0.0 ? hz (glideFrom) : f1;
    for (int64_t i = 0; i < len; ++i)
    {
        const double x = (double) i / c.sr;
        const double f = f1 + (f0 - f1) * std::exp (-x / 0.05) + f1 * 1.5 * std::exp (-x / 0.008);
        ph += f / c.sr;
        const double env = std::min (1.0, x / 0.002) * (x < dur ? std::exp (-x / 0.9) : std::exp (-dur / 0.9) * std::max (0.0, 1.0 - (x - dur) / 0.08));
        const float v = (float) (std::tanh (std::sin (kTwoPi * ph) * 2.2) * env) * gain * 0.6f;
        c.add (s0 + i, v, v);
    }
}

// plucked saw through a decaying low-pass
void pluck (Canvas& c, double t, double dur, double midi, float gain, float pan, double bright = 1.0)
{
    const int64_t s0 = (int64_t) (t * c.sr), len = (int64_t) ((dur + 0.25) * c.sr);
    Svf f; double ph = 0.0, ph2 = 0.0; const double fr = hz (midi);
    for (int64_t i = 0; i < len; ++i)
    {
        const double x = (double) i / c.sr;
        if ((i & 15) == 0) f.set (c.sr, 300.0 + 5200.0 * bright * std::exp (-x / 0.09), 1.6);
        ph += fr / c.sr; ph -= std::floor (ph);
        ph2 += fr * 1.006 / c.sr; ph2 -= std::floor (ph2);
        // band-limited-ish saw: sum of a few harmonics keeps it clean at any rate
        double s = 0.0;
        for (int h = 1; h <= 10 && fr * h < c.sr * 0.45; ++h) s += (std::sin (kTwoPi * ph * h) + std::sin (kTwoPi * ph2 * h)) / h;
        float lp, b, hh; f.process ((float) (s * 0.3), lp, b, hh);
        const double env = std::min (1.0, x / 0.001) * (x < dur ? std::exp (-x / 0.25) : std::exp (-dur / 0.25) * std::exp (-(x - dur) / 0.05));
        const float v = lp * (float) env * gain;
        c.add (s0 + i, v * (1.0f - pan), v * (1.0f + pan));
    }
}

// two-operator FM electric piano
void keys (Canvas& c, double t, double dur, double midi, float gain, float pan)
{
    const int64_t s0 = (int64_t) (t * c.sr), len = (int64_t) ((dur + 0.5) * c.sr);
    const double fr = hz (midi); double pc = 0.0, pm = 0.0;
    for (int64_t i = 0; i < len; ++i)
    {
        const double x = (double) i / c.sr;
        pc += fr / c.sr; pm += fr * 1.0 / c.sr;
        const double idx = 1.8 * std::exp (-x / 0.35) + 0.25;
        const double tine = 0.12 * std::sin (kTwoPi * pc * 14.0) * std::exp (-x / 0.03);
        const double env = std::min (1.0, x / 0.003) * (x < dur ? std::exp (-x / 1.6) : std::exp (-dur / 1.6) * std::exp (-(x - dur) / 0.12));
        const double trem = 1.0 + 0.08 * std::sin (kTwoPi * 4.7 * x + pan * 3.0);
        const float v = (float) ((std::sin (kTwoPi * pc + idx * std::sin (kTwoPi * pm)) + tine) * env * trem) * gain * 0.33f;
        c.add (s0 + i, v * (1.0f - pan), v * (1.0f + pan));
    }
}

// vocal-ish formant voice: pulse train through three resonators
void vowel (Canvas& c, double t, double dur, double midi, int vowelIdx, float gain, float pan)
{
    static const double F[5][3] = { { 730, 1090, 2440 }, { 530, 1840, 2480 }, { 270, 2290, 3010 }, { 570, 840, 2410 }, { 300, 870, 2240 } };
    const int64_t s0 = (int64_t) (t * c.sr), len = (int64_t) ((dur + 0.12) * c.sr);
    Svf f[3]; for (int k = 0; k < 3; ++k) f[k].set (c.sr, F[vowelIdx % 5][k], 9.0);
    const double fr = hz (midi); double ph = 0.0; OnePole tiltLp; tiltLp.setCutoff (c.sr, 3500.0);
    for (int64_t i = 0; i < len; ++i)
    {
        const double x = (double) i / c.sr;
        const double vib = 1.0 + 0.006 * std::sin (kTwoPi * 5.3 * x) * std::min (1.0, x / 0.15);
        ph += fr * vib / c.sr; ph -= std::floor (ph);
        double src = 0.0;
        for (int h = 1; h <= 28 && fr * h < c.sr * 0.45; ++h) src += std::sin (kTwoPi * ph * h) / std::pow ((double) h, 0.9);
        float acc = 0.0f;
        static const float amp[3] = { 1.0f, 0.55f, 0.3f };
        for (int k = 0; k < 3; ++k) { float lp, b, hh; f[k].process ((float) src * 0.08f, lp, b, hh); acc += b * amp[k]; }
        const double env = std::min (1.0, x / 0.025) * (x < dur ? 1.0 : std::max (0.0, 1.0 - (x - dur) / 0.1));
        const float v = tiltLp.lp (acc) * (float) env * gain;
        c.add (s0 + i, v * (1.0f - pan), v * (1.0f + pan));
    }
}

void addReverb (AudioData& a, double sr, float amount, float seconds)
{
    ThrowReverb rv; rv.prepare (sr); rv.set (seconds, 5000.0f);
    const int64_t n = a.length();
    std::vector<float> wl ((size_t) n, 0.0f), wr ((size_t) n, 0.0f);
    // two laps so the tail wraps around the loop seamlessly
    for (int lap = 0; lap < 2; ++lap)
        for (int64_t i = 0; i < n; ++i)
        {
            float l, r; rv.process (a.ch[0][(size_t) i], a.ch[1][(size_t) i], 1.0f, l, r);
            if (lap == 1) { wl[(size_t) i] = l; wr[(size_t) i] = r; }
        }
    for (int64_t i = 0; i < n; ++i) { a.ch[0][(size_t) i] += amount * wl[(size_t) i]; a.ch[1][(size_t) i] += amount * wr[(size_t) i]; }
}

void normalise (AudioData& a, float target)
{
    float pk = 0.0f; double dc[2] = { 0, 0 };
    const int64_t n = a.length();
    for (int c = 0; c < 2; ++c) { for (float v : a.ch[c]) dc[c] += v; dc[c] /= (double) std::max<int64_t> (1, n); }
    for (int c = 0; c < 2; ++c) for (auto& v : a.ch[c]) { v -= (float) dc[c]; pk = std::max (pk, std::fabs (v)); }
    if (pk > 1.0e-6f) for (int c = 0; c < 2; ++c) for (auto& v : a.ch[c]) v *= target / pk;
}
} // namespace

void generateDemoLoop (int index, double sampleRate, AudioData& out)
{
    const DemoLoopInfo& info = demoLoopInfo (index);
    const double beat = 60.0 / info.bpm, s16 = beat / 4.0;
    Canvas c (out, sampleRate, info.beats * beat);
    Rng rng (0xC0FFEEU + (uint32_t) index * 977U);

    switch (clampv (index, 0, kNumDemoLoops - 1))
    {
        case 0: // Dusty Break — swung two-bar break
        {
            const double swing = s16 * 0.22;
            auto at = [&] (int bar, int step) { return (bar * 16 + step) * s16 + ((step & 1) ? swing : 0.0); };
            const int kicks[2][4] = { { 0, 7, 10, -1 }, { 0, 3, 10, 13 } };
            const int snares[2][3] = { { 4, 12, -1 }, { 4, 12, 15 } };
            for (int bar = 0; bar < 2; ++bar)
            {
                for (int k : kicks[bar]) if (k >= 0) kick (c, at (bar, k), k == 0 ? 1.0f : 0.85f, 52.0, 0.2);
                for (int s : snares[bar]) if (s >= 0) snare (c, at (bar, s), s == 15 ? 0.45f : 1.0f, rng);
                snare (c, at (bar, 9), 0.22f, rng, 0.06);   // ghost
                for (int s = 0; s < 16; ++s)
                {
                    if (s % 2 == 0) hat (c, at (bar, s), s % 4 == 0 ? 0.9f : 0.6f, rng, 0.028, 0.25f);
                    else if (s == 7 || s == 11 || (bar == 1 && s == 13)) hat (c, at (bar, s), 0.4f, rng, 0.02, -0.2f);
                }
                hat (c, at (bar, 14), 0.8f, rng, 0.12, 0.3f); // open hat
            }
            addReverb (out, sampleRate, 0.06f, 0.5f);
            break;
        }
        case 1: // Glass Arp — A minor 16th arpeggio
        {
            const int chords[4][4] = { { 57, 60, 64, 69 }, { 53, 57, 60, 65 }, { 48, 55, 60, 64 }, { 55, 59, 62, 67 } }; // Am F C G
            const int order[8] = { 0, 1, 2, 3, 2, 1, 3, 2 };
            for (int s = 0; s < 32; ++s)
            {
                const int ch = s / 8;
                const int note = chords[ch][order[s % 8]] + ((s % 8) == 6 ? 12 : 0);
                pluck (c, s * s16, s16 * 0.9, note + 12, (s % 4 == 0) ? 0.5f : 0.38f, ((s % 2) ? 0.35f : -0.35f), 1.0);
                if (s % 8 == 0) pluck (c, s * s16, beat * 1.8, chords[ch][0] - 12, 0.5f, 0.0f, 0.35);
            }
            addReverb (out, sampleRate, 0.18f, 1.6f);
            break;
        }
        case 2: // Sub Roller — trap: 808 line in F minor + hats
        {
            struct N { int step; int len; int note; int glide; };
            const N line[] = { { 0, 6, 29, 0 }, { 6, 2, 29, 0 }, { 8, 4, 32, 0 }, { 12, 4, 27, 39 },
                               { 16, 6, 29, 0 }, { 22, 2, 36, 0 }, { 24, 3, 34, 0 }, { 27, 5, 32, 44 } };
            for (auto& nn : line) sub (c, nn.step * s16, nn.len * s16 * 0.95, nn.note + 12, 1.0f, nn.glide ? nn.glide + 12 : 0.0);
            for (int s = 0; s < 32; ++s)
            {
                if (s % 2 == 0) hat (c, s * s16, s % 4 == 0 ? 0.8f : 0.55f, rng, 0.022, 0.15f);
                if (s == 14 || s == 30) for (int k = 0; k < 3; ++k) hat (c, s * s16 + k * s16 / 3.0, 0.5f + 0.1f * k, rng, 0.015, -0.2f);
                if (s == 23) for (int k = 0; k < 4; ++k) hat (c, s * s16 + k * s16 / 4.0, 0.35f + 0.1f * k, rng, 0.012, 0.3f);
            }
            clap (c, 8 * s16, 0.9f, rng); clap (c, 24 * s16, 0.9f, rng);
            kick (c, 0.0, 0.7f, 45.0, 0.12); kick (c, 16 * s16, 0.7f, 45.0, 0.12); kick (c, 22 * s16, 0.6f, 45.0, 0.12);
            addReverb (out, sampleRate, 0.05f, 0.7f);
            break;
        }
        case 3: // Warm Keys — D minor progression, 4 bars
        {
            const int prog[4][4] = { { 50, 57, 60, 65 }, { 46, 53, 58, 62 }, { 48, 55, 60, 64 }, { 45, 52, 57, 61 } }; // Dm7 Bbmaj C A
            for (int bar = 0; bar < 4; ++bar)
            {
                const double t0 = bar * 4 * beat;
                for (int v = 0; v < 4; ++v)
                {
                    keys (c, t0 + v * 0.012, beat * 1.4, prog[bar][v], 0.8f, (v - 1.5f) * 0.25f);
                    keys (c, t0 + beat * 1.5 + v * 0.01, beat * 0.9, prog[bar][v], 0.55f, (1.5f - v) * 0.25f);
                    keys (c, t0 + beat * 2.5 + v * 0.01, beat * 1.3, prog[bar][v] + (v == 3 ? 2 : 0), 0.7f, (v - 1.5f) * 0.25f);
                }
                keys (c, t0, beat * 3.5, prog[bar][0] - 12, 0.9f, 0.0f);
                keys (c, t0 + beat * 3.5, beat * 0.4, prog[bar][0] - 5, 0.5f, 0.0f);
            }
            addReverb (out, sampleRate, 0.14f, 1.8f);
            break;
        }
        case 4: // Vowel Hook — C minor sung phrase
        {
            struct N { double beat; double len; int note; int vowel; };
            const N phrase[] = { { 0.0, 0.9, 60, 0 }, { 1.0, 0.45, 63, 1 }, { 1.5, 0.45, 65, 2 }, { 2.0, 1.4, 67, 0 }, { 3.5, 0.45, 65, 3 },
                                 { 4.0, 0.9, 63, 4 }, { 5.0, 0.45, 62, 1 }, { 5.5, 0.45, 60, 0 }, { 6.0, 1.9, 58, 3 } };
            for (auto& nn : phrase) vowel (c, nn.beat * beat, nn.len * beat, nn.note, nn.vowel, 0.9f, 0.0f);
            for (int b = 0; b < 8; b += 2) { pluck (c, b * beat, beat * 1.5, 36 + (b >= 4 ? -2 : 0), 0.55f, 0.0f, 0.3); }
            for (int s = 0; s < 32; s += 2) hat (c, s * s16, s % 4 == 2 ? 0.5f : 0.25f, rng, 0.02, 0.2f);
            addReverb (out, sampleRate, 0.2f, 1.5f);
            break;
        }
        default: // Full Groove — house in G minor, 4 bars
        {
            const int roots[4] = { 43, 39, 46, 41 }; // G Eb Bb F
            const int stabs[4][3] = { { 58, 62, 67 }, { 58, 63, 67 }, { 58, 62, 65 }, { 57, 60, 65 } };
            for (int bar = 0; bar < 4; ++bar)
            {
                const double t0 = bar * 16 * s16;
                for (int b = 0; b < 4; ++b)
                {
                    kick (c, t0 + b * beat, 1.0f, 50.0, 0.18);
                    hat (c, t0 + b * beat + beat * 0.5, 0.9f, rng, 0.05, 0.2f);
                    if (b % 2 == 1) clap (c, t0 + b * beat, 0.8f, rng);
                }
                for (int s = 0; s < 16; ++s) if (s % 4 != 0 && s % 2 == 1) hat (c, t0 + s * s16, 0.35f, rng, 0.015, -0.25f);
                const int bassSteps[5] = { 2, 5, 8, 11, 14 };
                for (int s : bassSteps) pluck (c, t0 + s * s16, s16 * 1.6, roots[bar] - 12 + (s == 11 ? 12 : 0), 0.9f, 0.0f, 0.45);
                const int stabSteps[3] = { 3, 6, 10 };
                for (int s : stabSteps) for (int v = 0; v < 3; ++v) pluck (c, t0 + s * s16, s16 * 1.2, stabs[bar][v], 0.3f, (v - 1) * 0.4f, 0.9);
            }
            addReverb (out, sampleRate, 0.1f, 1.2f);
            break;
        }
    }
    normalise (out, 0.7f);
}
} // namespace splinter
