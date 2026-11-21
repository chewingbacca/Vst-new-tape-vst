#include "Analysis.h"
#include "Reader.h"

namespace splinter
{
void resample (const std::vector<float>& in, std::vector<float>& out, int64_t outLength, bool periodic)
{
    const int64_t n = (int64_t) in.size();
    out.assign ((size_t) std::max<int64_t> (0, outLength), 0.0f);
    if (n <= 0 || outLength <= 0) return;
    const double step = (double) n / (double) outLength;   // input samples per output sample
    const auto& table = SincTable::get();
    const double rho = std::max (1.0, step);
    const float inv = (float) (1.0 / rho);
    const int half = (int) std::ceil (kSincHalf * rho);
    for (int64_t i = 0; i < outLength; ++i)
    {
        const double x = (double) i * step;
        const int64_t i0 = (int64_t) std::floor (x);
        const float frac = (float) (x - (double) i0);
        float acc = 0.0f, wsum = 0.0f;
        for (int k = -half + 1; k <= half; ++k)
        {
            int64_t j = i0 + k;
            if (periodic) { j %= n; if (j < 0) j += n; }
            else if (j < 0 || j >= n) continue;
            const float w = table.kernel (((float) k - frac) * inv);
            acc += w * in[(size_t) j]; wsum += w;
        }
        out[(size_t) i] = wsum > 1.0e-6f ? acc / (periodic ? wsum : std::max (wsum, 0.5f)) : 0.0f;
    }
}

namespace
{
struct Stft
{
    int n = 1024, hop = 256, frames = 0;
    std::vector<std::vector<float>> mag;   // [frame][bin]
};

Stft computeStft (const std::vector<float>& mono, int order, int hopDiv)
{
    Stft s;
    s.n = 1 << order; s.hop = s.n / hopDiv;
    FFT fft (order);
    std::vector<float> re ((size_t) s.n), im ((size_t) s.n), win ((size_t) s.n);
    for (int i = 0; i < s.n; ++i) win[(size_t) i] = (float) (0.5 - 0.5 * std::cos (kTwoPi * i / s.n));
    const int64_t len = (int64_t) mono.size();
    s.frames = (int) (len / s.hop) + 1;
    s.mag.resize ((size_t) s.frames);
    for (int f = 0; f < s.frames; ++f)
    {
        const int64_t c = (int64_t) f * s.hop;      // frame centre
        for (int i = 0; i < s.n; ++i)
        {
            const int64_t j = c - s.n / 2 + i;
            re[(size_t) i] = (j >= 0 && j < len) ? mono[(size_t) j] * win[(size_t) i] : 0.0f;
            im[(size_t) i] = 0.0f;
        }
        fft.transform (re.data(), im.data(), false);
        auto& m = s.mag[(size_t) f];
        m.resize ((size_t) s.n / 2 + 1);
        for (int k = 0; k <= s.n / 2; ++k) m[(size_t) k] = std::sqrt (re[(size_t) k] * re[(size_t) k] + im[(size_t) k] * im[(size_t) k]);
    }
    return s;
}

int orderFor (double sampleRate, int baseOrder)
{
    int o = baseOrder;
    if (sampleRate > 60000.0) ++o;
    if (sampleRate > 120000.0) ++o;
    return o;
}
} // namespace

LoopAnalysis analyseLoop (const AudioData& audio, double bpmHint)
{
    LoopAnalysis a;
    const int64_t len = audio.length();
    const double sr = audio.sampleRate;
    if (len < 2048) { a.beats = 4.0; return a; }
    const double dur = (double) len / sr;

    std::vector<float> mono ((size_t) len);
    float peak = 0.0f;
    for (int64_t i = 0; i < len; ++i) { mono[(size_t) i] = 0.5f * (audio.ch[0][(size_t) i] + audio.ch[1][(size_t) i]); peak = std::max (peak, std::fabs (mono[(size_t) i])); }
    if (peak < 1.0e-6f) { a.beats = 4.0; a.bpm = 4.0 * 60.0 / dur; return a; }
    const float normGain = 1.0f / peak;

    // ---------------- onset strength (log-magnitude spectral flux)
    const Stft st = computeStft (mono, orderFor (sr, 10), 4);
    const double frameSec = (double) st.hop / sr;
    std::vector<float> flux ((size_t) st.frames, 0.0f);
    double flatSum = 0.0, flatW = 0.0;
    const float gamma = 24.0f * normGain / (float) st.n * 4.0f;
    for (int f = 1; f < st.frames; ++f)
    {
        float sum = 0.0f;
        double logSum = 0.0, linSum = 0.0; int cnt = 0;
        for (int k = 1; k < st.n / 2; ++k)
        {
            const float cur = std::log1p (gamma * st.mag[(size_t) f][(size_t) k]);
            const float prev = std::log1p (gamma * st.mag[(size_t) f - 1][(size_t) k]);
            if (cur > prev) sum += cur - prev;
            const double m = (double) st.mag[(size_t) f][(size_t) k] + 1.0e-9;
            logSum += std::log (m); linSum += m; ++cnt;
        }
        flux[(size_t) f] = sum;
        const double flat = std::exp (logSum / cnt) / (linSum / cnt);
        flatSum += flat * linSum; flatW += linSum;
    }
    a.percussiveness = (float) clampv (flatW > 0.0 ? (flatSum / flatW) * 4.0 : 0.5, 0.0, 1.0);

    // band energies for the rise test below
    constexpr int kBands = 4;
    const double edges[kBands + 1] = { 0.0, 220.0, 1100.0, 5000.0, sr * 0.5 };
    std::vector<float> bandE[kBands];
    float bandMax[kBands] = {}, bandMaxAll = 0.0f;
    for (int b = 0; b < kBands; ++b)
    {
        bandE[b].assign ((size_t) st.frames, 0.0f);
        const int k0 = std::max (1, (int) (edges[b] * st.n / sr)), k1 = std::min (st.n / 2, (int) (edges[b + 1] * st.n / sr));
        for (int f = 0; f < st.frames; ++f)
        {
            float e = 0.0f;
            for (int k = k0; k < k1; ++k) e += st.mag[(size_t) f][(size_t) k] * st.mag[(size_t) f][(size_t) k];
            bandE[b][(size_t) f] = e; bandMax[b] = std::max (bandMax[b], e);
        }
        bandMaxAll = std::max (bandMaxAll, bandMax[b]);
    }

    // adaptive threshold: local mean over +-120 ms
    const int wnd = std::max (2, (int) (0.12 / frameSec));
    float fluxMax = 0.0f;
    for (float v : flux) fluxMax = std::max (fluxMax, v);
    std::vector<int> peakFrames;
    const int nearF = std::max (1, (int) (0.03 / frameSec));
    for (int f = 1; f < st.frames - 1; ++f)
    {
        float mean = 0.0f; int c = 0;
        for (int j = std::max (0, f - wnd); j <= std::min (st.frames - 1, f + wnd); ++j) { mean += flux[(size_t) j]; ++c; }
        mean /= (float) c;
        const float v = flux[(size_t) f];
        if (v < mean * 1.5f + 0.04f * fluxMax) continue;
        bool isMax = true;
        for (int j = std::max (0, f - nearF); j <= std::min (st.frames - 1, f + nearF); ++j) if (flux[(size_t) j] > v) { isMax = false; break; }
        if (! isMax) continue;
        if (! peakFrames.empty() && f - peakFrames.back() <= nearF) continue;
        // a real onset raises the energy of at least one band; beating between
        // the partials of a sustained chord moves the flux but not the energy
        bool rises = v > 0.5f * fluxMax;      // a big spectral change counts by itself (legato notes)
        for (int b = 0; b < kBands && ! rises; ++b)
        {
            if (bandMax[b] < 1.0e-7f * bandMaxAll) continue;
            float before = 1.0e30f, after = 0.0f;
            for (int j = std::max (0, f - 5); j <= std::max (0, f - 2); ++j) before = std::min (before, bandE[b][(size_t) j]);
            for (int j = f; j <= std::min (st.frames - 1, f + 4); ++j) after = std::max (after, bandE[b][(size_t) j]);
            float totalNow = 0.0f;
            for (int bb = 0; bb < kBands; ++bb) totalNow += bandE[bb][(size_t) std::min (st.frames - 1, f + 1)];
            const bool bigInMix = after > 2.0f * before && (after - before) > 0.05f * totalNow;
            const bool fromNothing = after > 6.0f * before + 0.02f * bandMax[b];
            if (bigInMix || fromNothing) rises = true;
        }
        if (! rises) continue;
        peakFrames.push_back (f);
    }

    // refine each onset to the start of the attack using a fast envelope
    const int envWin = std::max (4, (int) (0.001 * sr));
    for (int f : peakFrames)
    {
        const int64_t centre = (int64_t) f * st.hop;
        const int64_t lo = std::max<int64_t> (0, centre - st.n / 2), hi = std::min<int64_t> (len - 1, centre + st.n / 2);
        // envelope in 1 ms blocks
        std::vector<float> env;
        for (int64_t i = lo; i + envWin <= hi; i += envWin)
        {
            float e = 0.0f;
            for (int j = 0; j < envWin; ++j) e = std::max (e, std::fabs (mono[(size_t) (i + j)]));
            env.push_back (e);
        }
        if (env.size() < 3) continue;
        size_t best = 1; float bestRise = 0.0f, envPeak = 0.0f;
        for (size_t i = 1; i < env.size(); ++i)
        {
            const float rise = env[i] - env[i - 1];
            if (rise > bestRise) { bestRise = rise; best = i; }
            envPeak = std::max (envPeak, env[i]);
        }
        // walk back while the envelope keeps falling towards the pre-onset floor
        size_t i = best;
        while (i > 0 && env[i - 1] < env[i] && env[i - 1] > 0.1f * env[best]) --i;
        if (i > 0 && env[i - 1] < env[i]) --i;
        double t = (double) (lo + (int64_t) i * envWin) / sr;
        // exact first sample exceeding a small threshold inside that block
        const int64_t b0 = lo + (int64_t) i * envWin, b1 = std::min<int64_t> (len, b0 + 2 * envWin);
        const float thr = std::max (0.02f * envPeak, 0.25f * env[i]);
        for (int64_t j = b0; j < b1; ++j) if (std::fabs (mono[(size_t) j]) >= thr) { t = (double) j / sr; break; }
        if (! a.onsetTimes.empty() && t - a.onsetTimes.back() < 0.025) continue;
        a.onsetTimes.push_back (t);
        a.onsetStrengths.push_back (fluxMax > 0.0f ? flux[(size_t) f] / fluxMax : 0.0f);
    }

    // ---------------- tempo: the file is assumed to be a loop of whole beats
    if (bpmHint > 0.0)
    {
        a.bpm = bpmHint;
        a.beats = std::max (1.0, std::round (dur * bpmHint / 60.0));
    }
    else
    {
        static const int cands[] = { 1, 2, 4, 8, 16, 32, 64, 128, 3, 6, 12, 24, 48 };
        double bestScore = -1.0; int bestBeats = 8;
        for (int nb : cands)
        {
            const double bpm = nb * 60.0 / dur;
            if (bpm < 55.0 || bpm > 200.0) continue;
            // how well do onsets sit on this candidate's 16th-note grid?
            const double cell = dur / (nb * 4.0);
            double fit = 0.0, wsum = 0.0;
            for (size_t i = 0; i < a.onsetTimes.size(); ++i)
            {
                const double ph = a.onsetTimes[i] / cell;
                const double d = std::fabs (ph - std::round (ph)) * cell;       // seconds off grid
                const double w = (double) a.onsetStrengths[i] + 0.1;
                fit += w * std::exp (-(d * d) / (2.0 * 0.012 * 0.012));
                wsum += w;
            }
            fit = wsum > 0.0 ? fit / wsum : 0.5;
            const double lg = std::log2 (bpm / 112.0);
            const double prior = std::exp (-(lg * lg) / (2.0 * 0.55 * 0.55));
            const bool pow2 = (nb & (nb - 1)) == 0;
            const double score = prior * (0.35 + fit) * (pow2 ? 1.0 : 0.6);
            if (score > bestScore) { bestScore = score; bestBeats = nb; }
        }
        if (bestScore < 0.0)
        {
            // very short or very long file: fold into range
            double bpm = 4.0 * 60.0 / dur; int nb = 4;
            while (bpm < 70.0) { bpm *= 2.0; nb *= 2; }
            while (bpm > 180.0 && nb > 1) { bpm *= 0.5; nb /= 2; }
            bestBeats = std::max (1, nb);
        }
        a.beats = bestBeats;
        a.bpm = bestBeats * 60.0 / dur;
    }

    // ---------------- key (Krumhansl-Schmuckler on a peak chroma)
    {
        const Stft big = computeStft (mono, orderFor (sr, 12), 2);
        double chroma[12] = {};
        for (int f = 0; f < big.frames; ++f)
        {
            const auto& m = big.mag[(size_t) f];
            for (int k = 3; k < big.n / 2 - 1; ++k)
            {
                if (! (m[(size_t) k] > m[(size_t) k - 1] && m[(size_t) k] >= m[(size_t) k + 1])) continue;
                const float al = std::log (m[(size_t) k - 1] + 1.0e-12f), be = std::log (m[(size_t) k] + 1.0e-12f), ga = std::log (m[(size_t) k + 1] + 1.0e-12f);
                const float den = al - 2.0f * be + ga;
                const double kb = (double) k + (std::fabs (den) > 1.0e-9f ? clampv (0.5f * (al - ga) / den, -0.5f, 0.5f) : 0.0f);
                const double hz = kb * sr / big.n;
                if (hz < 55.0 || hz > 2000.0) continue;
                const double note = 69.0 + 12.0 * std::log2 (hz / 440.0);
                const double dev = note - std::round (note);
                chroma[floorModI ((int64_t) std::llround (note), 12)] += (double) m[(size_t) k] * (1.0 - 1.5 * std::fabs (dev)) / (1.0 + hz / 1000.0);
            }
        }
        static const double maj[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
        static const double mnr[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
        auto corr = [&] (const double* prof, int root)
        {
            double mx = 0, my = 0;
            for (int i = 0; i < 12; ++i) { mx += chroma[i]; my += prof[i]; }
            mx /= 12; my /= 12;
            double num = 0, dx = 0, dy = 0;
            for (int i = 0; i < 12; ++i)
            {
                const double x = chroma[(i + root) % 12] - mx, y = prof[i] - my;
                num += x * y; dx += x * x; dy += y * y;
            }
            return dx > 0 && dy > 0 ? num / std::sqrt (dx * dy) : 0.0;
        };
        double best = -2.0;
        for (int root = 0; root < 12; ++root)
        {
            const double cM = corr (maj, root), cm = corr (mnr, root);
            if (cM > best) { best = cM; a.key = root; a.scale = 0; }
            if (cm > best) { best = cm; a.key = root; a.scale = 1; }
        }
        a.keyConfidence = (float) clampv (best, 0.0, 1.0) * (1.0f - 0.6f * a.percussiveness);
    }
    return a;
}

SliceSet slicesFromOnsets (const LoopAnalysis& a, double loopSeconds, float sensitivity)
{
    SliceSet s;
    s.loopBeats = (float) a.beats;
    const double beatsPerSec = a.beats / std::max (1.0e-6, loopSeconds);
    const float sens = clampv (sensitivity, 0.0f, 1.0f);
    const float thr = 0.6f * (1.0f - sens);                       // relative strength needed
    const double minGap = 0.25 + (0.06 - 0.25) * (double) sens;   // beats between markers
    std::vector<std::pair<float, double>> picks;                  // (strength, beat)
    for (size_t i = 0; i < a.onsetTimes.size(); ++i)
        if (a.onsetStrengths[i] >= thr) picks.push_back ({ a.onsetStrengths[i], a.onsetTimes[i] * beatsPerSec });
    // strongest first, so a weak neighbour never displaces a real hit
    std::sort (picks.begin(), picks.end(), [] (auto& x, auto& y) { return x.first > y.first; });
    std::vector<double> kept { 0.0 };
    for (auto& pk : picks)
    {
        if ((int) kept.size() >= kMaxSlices) break;
        const double b = pk.second;
        if (b < minGap || b > a.beats - minGap * 0.5) continue;
        bool clash = false;
        for (double k : kept) if (std::fabs (k - b) < minGap) { clash = true; break; }
        if (! clash) kept.push_back (b);
    }
    std::sort (kept.begin(), kept.end());
    s.count = (int) kept.size();
    for (int i = 0; i < s.count; ++i) s.startBeat[i] = (float) kept[(size_t) i];
    return s;
}
} // namespace splinter
