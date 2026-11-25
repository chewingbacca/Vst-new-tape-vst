// Granular: a cloud of up to 64 overlapping grains read from the track's sample.
//
//   scheduler : a phase accumulator running at `density` Hz spawns a grain whenever it passes a
//               threshold; the threshold is 1 (periodic, pitched stream) blended by `jitter`
//               towards an exponentially distributed interval (Poisson cloud). Onsets are
//               sub-sample accurate, so periodic streams have a clean pitch.
//   grain     : start = pos + scan travel + spray (wrapped inside the sample, then moved back
//               if needed so the grain fits), pitch = note + scatter (free / octaves /
//               fifths), direction by `revprob`, pan by `spread`. Read with 4-point Hermite
//               from the mip level matching its rate; reflects at the sample ends, so a read
//               can never leave the data and never jumps.
//   window    : table lookup, 65 shapes from percussive (fast rise, exponential fall) through
//               Hann to the mirror image (swell). Every window is exactly zero at both ends.
//   level     : each grain is scaled by (1 + P^2)^(-1/4), P = density * size * mean(window^2):
//               unity for sparse grains, constant power for dense clouds. A soft knee above
//               0 dBFS catches coherent pile-ups.
//
// All original work; asynchronous granular synthesis as described by Curtis Roads
// ("Microsound", MIT Press 2001).
#include "EngineBase.h"
#include "../model/ParamTables.h"

namespace atm {

namespace {
enum P { Pos, Size, Density, Spray, Scatter, Scan, RevProb, Shape, Spread, Jitter, Quant, Gain, KeyTrack };

constexpr int   kMaxGrains = 64;
constexpr int   kWinSize   = 1024;        // intervals per window (table has one more point)
constexpr int   kWinRows   = 65;          // shapes
constexpr float kBaseGain  = 0.7071f;
constexpr float kMinSemis  = -96.f, kMaxSemis = 60.f;

struct WindowTable
{
    float w[kWinRows][kWinSize + 2];      // one spare point so a rounded-up index stays inside
    float power[kWinRows];                // mean of w^2

    WindowTable()
    {
        const int half = kWinRows / 2;
        for (int row = 0; row <= half; ++row)
        {
            const double s = (double) row / (double) (kWinRows - 1);      // 0 .. 0.5
            const double k = 1.0 - 2.0 * s;                               // 1 = percussive, 0 = Hann
            const double a = 0.02 + 0.96 * s;                             // position of the peak
            const double kd = 6.0, e1 = std::exp (-kd);
            for (int i = 0; i <= kWinSize; ++i)
            {
                const double x = (double) i / (double) kWinSize;
                double v;
                if (x < a) v = 0.5 - 0.5 * std::cos (kPi * x / a);
                else
                {
                    const double f = (x - a) / (1.0 - a);
                    const double hann = 0.5 + 0.5 * std::cos (kPi * f);
                    const double expo = (std::exp (-kd * f) - e1) / (1.0 - e1);
                    v = hann + k * (expo - hann);
                }
                w[row][i] = (float) v;
            }
            w[row][0] = 0.f; w[row][kWinSize] = 0.f; w[row][kWinSize + 1] = 0.f;
        }
        for (int row = half + 1; row < kWinRows; ++row)                   // swelling shapes: mirror images
        {
            for (int i = 0; i <= kWinSize; ++i)
                w[row][i] = w[kWinRows - 1 - row][kWinSize - i];
            w[row][kWinSize + 1] = 0.f;
        }
        for (int row = 0; row < kWinRows; ++row)
        {
            double acc = 0;
            for (int i = 0; i < kWinSize; ++i) acc += (double) w[row][i] * w[row][i];
            power[row] = (float) (acc / kWinSize);
        }
    }
};

const WindowTable& windowTable()
{
    static const WindowTable t;           // built on the first prepare(), read-only afterwards
    return t;
}

inline float softLimit (float x)
{
    const float a = std::fabs (x);
    if (a <= 1.f) return x;
    const float y = 1.f + 0.8f * fastTanh ((a - 1.f) * 1.25f);
    return x < 0.f ? -y : y;
}

struct Grain
{
    int    left = 0;                      // samples still to render; 0 = free
    int    age = 0;                       // samples rendered so far
    double pos = 0, inc = 0, hi = 0;      // in frames of the grain's mip level
    float  winOff = 0, winInc = 0;        // window position = (age + winOff) * winInc
    const float* win = nullptr;
    const float* dL = nullptr;
    const float* dR = nullptr;
    float  gL = 0, gR = 0;
};
}

class GranularEngine final : public EngineBase
{
public:
    void prepare (double sampleRate) override
    {
        sr = (float) sampleRate;
        table = &windowTable();
        reset();
    }

    void reset() override
    {
        for (auto& g : grains) g.left = 0;
        numActive = 0; gate = false;
        phase = 0.0; thresh = 0.0; scanOff = 0.0;
    }

    void setSample (const SampleData* s) override
    {
        smp = (s != nullptr && s->length >= 4 && s->numMips >= 1) ? s : nullptr;
        reset();
    }

    void noteOn (const EngineNote& n, const float* e) override
    {
        if (! (n.legato && (gate || numActive > 0)))
        {
            reset();
            rng = Rng (n.seed);
        }
        gate = smp != nullptr;
        control (e, n.note);
    }

    void noteOff() override { gate = false; }

    void control (const float* e, float n) override
    {
        const ParamDesc* d = kGranularParams;
        note     = n;
        posN     = clamp01 (e[Pos]);
        size     = d[Size].toPhys (e[Size]);
        density  = d[Density].toPhys (e[Density]);
        spray    = d[Spray].toPhys (e[Spray]);
        scatter  = d[Scatter].toPhys (e[Scatter]);
        scan     = d[Scan].toPhys (e[Scan]);
        revProb  = clamp01 (e[RevProb]);
        winRow   = clamp ((int) std::lround (clamp01 (e[Shape]) * (float) (kWinRows - 1)), 0, kWinRows - 1);
        spread   = clamp01 (e[Spread]);
        jitter   = clamp01 (e[Jitter]);
        quant    = d[Quant].toIndex (e[Quant]);
        gain     = kBaseGain * dbToGain (d[Gain].toPhys (e[Gain]));
        keytrack = d[KeyTrack].toIndex (e[KeyTrack]) != 0;
        densInc  = (double) density / (double) sr;
        if (smp != nullptr) scanInc = (double) scan * smp->sampleRate / (double) sr;
    }

    void render (float* outL, float* outR, int n) override
    {
        for (int i = 0; i < n; ++i) { outL[i] = 0.f; outR[i] = 0.f; }
        if (smp == nullptr) return;

        const double total = (double) smp->length;
        int at = 0;
        while (at < n)
        {
            // run the scheduler up to the next onset (or the end of the slice)
            int k = at;
            bool spawnNow = false;
            if (gate)
            {
                for (;;)
                {
                    if (phase >= thresh) { spawnNow = true; break; }
                    if (k >= n) break;
                    phase += densInc;
                    scanOff += scanInc;
                    if (scanOff >= total) scanOff -= total; else if (scanOff < 0.0) scanOff += total;
                    ++k;
                }
                // an onset found while k == n belongs to the first sample of the next slice
                if (spawnNow && k >= n) spawnNow = false;
            }
            else k = n;

            if (k > at && numActive > 0) renderGrains (outL + at, outR + at, k - at);
            at = k;
            if (spawnNow)
            {
                const double over = densInc > 0.0 ? (phase - thresh) / densInc : 0.0;    // samples since the exact onset
                spawn ((float) clamp (over, 0.0, 0.999));
                phase -= thresh;
                // next interval in mean periods: 1 (periodic) .. exponential (Poisson)
                const float u = rng.uni();
                const float ex = -std::log (std::max (u, 1.0e-4f));
                thresh = std::max (0.03, (double) (1.f - jitter + jitter * ex));
            }
        }
        for (int i = 0; i < n; ++i) { outL[i] = softLimit (outL[i]); outR[i] = softLimit (outR[i]); }
    }

    bool isStereo() const override { return true; }
    bool finished() const override { return ! gate && numActive == 0; }

private:
    void spawn (float frac)
    {
        // always draw the same number of values so the random sequence does not depend on the pool state
        const float rPos = rng.bip(), rPitch = rng.bip(), rRev = rng.uni(), rPan = rng.bip();

        Grain* g = nullptr;
        for (auto& c : grains) if (c.left <= 0) { g = &c; break; }
        if (g == nullptr) return;

        const SampleData& s = *smp;
        const int nSamp = std::max (16, (int) (size * sr + 0.5f));

        // pitch
        float off = scatter * rPitch;
        if (quant == 1) off = 12.f * std::floor (off * (1.f / 12.f) + 0.5f);
        else if (quant == 2)
        {
            const float oct = std::floor (off * (1.f / 12.f));
            const float r = off - 12.f * oct;
            off = 12.f * oct + (r < 3.5f ? 0.f : (r < 9.5f ? 7.f : 12.f));
        }
        const float st = (keytrack ? note - s.rootNote : 0.f) + off;
        const double inc0 = (s.sampleRate / (double) sr) * (double) std::exp2 (clamp (st, kMinSemis, kMaxSemis) * (1.f / 12.f));

        // mip level: the lowest one this rate can read without folding much of its top octave
        int level = 0;
        while (level < s.numMips - 1 && inc0 > 1.3 * (double) (1 << level)) ++level;
        const double scale = 1.0 / (double) (1 << level);
        const int levelLen = s.mip[level].length;
        if (levelLen < 4) return;

        // position (level-0 frames): wrap into the sample, then pull back so the grain fits
        const double usable = std::min ((double) (s.length - 1), (double) (levelLen - 1) * (double) (1 << level));
        const double span = inc0 * (double) nSamp;
        double p = (double) posN * usable + scanOff + (double) (spray * rPos) * 0.5 * usable;
        p -= std::floor (p / usable) * usable;
        if (span < usable) p = std::min (p, usable - span);
        const bool rev = rRev < revProb;
        if (rev) p = std::min (p + span, usable);

        g->hi  = (double) (levelLen - 1);
        g->inc = (rev ? -inc0 : inc0) * scale;
        g->pos = p * scale + (double) frac * g->inc;
        reflect (*g);
        g->dL = s.data (level, 0);
        g->dR = s.data (level, 1);

        g->win = table->w[winRow];
        g->winInc = (float) kWinSize / (float) nSamp;
        g->winOff = frac;
        g->age = 0;
        g->left = nSamp;

        // level
        const float overlap = std::min (density * size, (float) kMaxGrains);
        const float pw = overlap * table->power[winRow];
        const float amp = gain / std::sqrt (std::sqrt (1.f + pw * pw));
        const float th = (spread * rPan + 1.f) * (kPiF * 0.25f);
        g->gL = amp * 1.41421356f * std::cos (th);
        g->gR = amp * 1.41421356f * std::sin (th);
        ++numActive;
    }

    static inline void reflect (Grain& g)
    {
        if (g.pos > g.hi)      { g.pos = 2.0 * g.hi - g.pos; g.inc = -g.inc; if (g.pos < 0.0) g.pos = 0.0; }
        else if (g.pos < 0.0)  { g.pos = -g.pos; g.inc = -g.inc; if (g.pos > g.hi) g.pos = g.hi; }
    }

    void renderGrains (float* outL, float* outR, int n)
    {
        const bool stereoSample = smp->numChannels > 1;
        for (auto& g : grains)
        {
            if (g.left <= 0) continue;
            const int m = std::min (n, g.left);
            double p = g.pos, inc = g.inc;
            const double hi = g.hi;
            const int age = g.age;
            const float wo = g.winOff, wi = g.winInc, gL = g.gL, gR = g.gR;
            const float* win = g.win;
            const float* dL = g.dL;
            const float* dR = g.dR;
            for (int i = 0; i < m; ++i)
            {
                const float wp = ((float) (age + i) + wo) * wi;          // from a counter: no drift, never past the table
                const int wIdx = (int) wp;
                const float w = win[wIdx] + (wp - (float) wIdx) * (win[wIdx + 1] - win[wIdx]);
                const int ip = (int) p;
                const float t = (float) (p - (double) ip);
                const float l = hermite4 (dL[ip - 1], dL[ip], dL[ip + 1], dL[ip + 2], t);
                outL[i] += l * w * gL;
                if (stereoSample) outR[i] += hermite4 (dR[ip - 1], dR[ip], dR[ip + 1], dR[ip + 2], t) * w * gR;
                else              outR[i] += l * w * gR;
                p += inc;
                if (p > hi)       { p = 2.0 * hi - p; inc = -inc; if (p < 0.0) p = 0.0; }
                else if (p < 0.0) { p = -p; inc = -inc; if (p > hi) p = hi; }
            }
            g.pos = p; g.inc = inc; g.age = age + m;
            g.left -= m;
            if (g.left <= 0) --numActive;
        }
    }

    float sr = 48000.f;
    const SampleData* smp = nullptr;
    const WindowTable* table = nullptr;
    Grain grains[kMaxGrains];
    int   numActive = 0;
    bool  gate = false;
    Rng   rng { 1 };
    double phase = 0, thresh = 0, densInc = 0, scanOff = 0, scanInc = 0;

    // control
    float note = 60.f, posN = 0.25f, size = 0.08f, density = 30.f, spray = 0.f, scatter = 0.f, scan = 0.f;
    float revProb = 0.f, spread = 0.5f, jitter = 0.3f, gain = kBaseGain;
    int   winRow = kWinRows / 2, quant = 0;
    bool  keytrack = true;
};

std::unique_ptr<EngineBase> createGranularEngine() { return std::make_unique<GranularEngine>(); }

} // namespace atm
