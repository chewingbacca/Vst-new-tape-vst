#include "SampleData.h"
#include "../util/Math.h"
#include "../dsp/Fft.h"
#include <cstdio>

namespace atm {

namespace {
// 31-tap half-band low-pass (Kaiser-windowed sinc), used for the 2:1 decimation of mip levels.
struct HalfBand
{
    static constexpr int kTaps = 31;
    float h[kTaps];
    HalfBand()
    {
        const int mid = kTaps / 2;
        const double beta = 7.0;
        auto bessel0 = [] (double x) { double s = 1, t = 1; for (int k = 1; k < 30; ++k) { t *= (x / (2.0 * k)) * (x / (2.0 * k)); s += t; } return s; };
        double sum = 0;
        for (int i = 0; i < kTaps; ++i)
        {
            const int n = i - mid;
            const double sinc = n == 0 ? 0.5 : std::sin (kPi * 0.5 * n) / (kPi * n);
            const double r = (double) n / (double) mid;
            const double w = bessel0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / bessel0 (beta);
            h[i] = (float) (sinc * w); sum += h[i];
        }
        for (auto& v : h) v = (float) (v / sum);
    }
};
}

static void buildMips (SampleData& s)
{
    static const HalfBand hb;
    s.numMips = 1;
    for (int level = 1; level < SampleData::kMaxMips; ++level)
    {
        const SampleData::Mip& src = s.mip[level - 1];
        if (src.length < 64) break;
        SampleData::Mip& dst = s.mip[level];
        dst.length = src.length / 2;
        for (int c = 0; c < s.numChannels; ++c)
        {
            dst.ch[c].assign ((size_t) (dst.length + 2 * SampleData::kGuard), 0.f);
            const float* in = src.ch[c].data() + SampleData::kGuard;
            for (int i = 0; i < dst.length; ++i)
            {
                double acc = 0;
                for (int k = 0; k < HalfBand::kTaps; ++k)
                {
                    const int j = 2 * i + k - HalfBand::kTaps / 2;
                    if (j >= 0 && j < src.length) acc += (double) in[j] * hb.h[k];
                }
                dst.ch[c][(size_t) (i + SampleData::kGuard)] = (float) acc;
            }
        }
        s.numMips = level + 1;
    }
}

// Onset detector for slicing drum loops. Two stages:
//
// 1. Candidates, in the time domain. Two detection signals are followed in 0.5 ms hops:
//      band 0: the first difference of the signal (+6 dB/oct tilt): hats, snares, clicks and
//              the attack of a kick, even on top of a loud bass tail
//      band 1: the signal itself: bass-heavy hits and soft tonal notes
//    A candidate is where the energy of the next 1.5 ms exceeds 2.6x (+4 dB) the loudest 1.5 ms
//    of the preceding 5 ms (an abrupt rise, even on top of a decaying tail) and is also above
//    everything in the preceding 25 ms (the energy ripple of a low tone rises just as abruptly
//    twice per cycle). Its position is refined to the first sample that clearly leaves the
//    previous level, then moved 0.7 ms earlier so a slice starts just ahead of its attack.
//
// 2. Confirmation, in the frequency domain. Sustained chords and noise produce candidates too
//    (their waveform has bumps), but their spectrum does not change. A log-magnitude spectral
//    flux over third-octave bands (23 ms Hann frames, 2.9 ms apart, each band compared with its
//    own maximum over the preceding 6-12 ms; after Boeck & Widmer's "SuperFlux", DAFx 2013)
//    has to rise clearly above its local average where a candidate sits.
namespace {
struct OnsetCandidate { int pos; float strength; };

// Spectral flux per frame; frame f is centred on sample f * hop.
void spectralFlux (const float* const* ch, int n, double sr, std::vector<float>& flux, int& fftSize, int& hop)
{
    int size = 256;
    while ((double) size < sr * 0.016) size *= 2;
    fftSize = size; hop = size / 8;
    const int frames = n / hop + 1;
    const double fMax = std::min (16000.0, 0.45 * sr);
    const int bands = (int) (3.0 * std::log2 (fMax / 50.0)) + 1;
    std::vector<int> bandOf ((size_t) size / 2 + 1, -1);
    for (int k = 1; k <= size / 2; ++k)
    {
        const double hz = (double) k * sr / (double) size;
        if (hz <= fMax) bandOf[(size_t) k] = clamp ((int) std::floor (3.0 * std::log2 (hz / 50.0)), 0, bands - 1);
    }
    std::vector<float> window ((size_t) size), re ((size_t) size), im ((size_t) size);
    for (int i = 0; i < size; ++i) window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (kTau * i / size));
    Fft fft; fft.prepare (size);

    std::vector<float> mags ((size_t) frames * (size_t) bands, 0.f);
    float maxMag = 0.f;
    for (int f = 0; f < frames; ++f)
    {
        const int start = f * hop - size / 2;
        for (int i = 0; i < size; ++i)
        {
            const int j = start + i;
            re[(size_t) i] = (j >= 0 && j < n) ? 0.5f * (ch[0][j] + ch[1][j]) * window[(size_t) i] : 0.f;
            im[(size_t) i] = 0.f;
        }
        fft.forward (re.data(), im.data());
        float* m = &mags[(size_t) f * (size_t) bands];
        for (int k = 1; k <= size / 2; ++k)
            if (bandOf[(size_t) k] >= 0) m[bandOf[(size_t) k]] += re[(size_t) k] * re[(size_t) k] + im[(size_t) k] * im[(size_t) k];
        for (int b = 0; b < bands; ++b) { m[b] = std::sqrt (m[b]); maxMag = std::max (maxMag, m[b]); }
    }
    flux.assign ((size_t) frames, 0.f);
    if (maxMag <= 1.0e-12f) return;
    const float scale = 1000.f / maxMag;                                     // 60 dB of range below the loudest band
    for (auto& v : mags) v = std::log (1.f + scale * v);
    for (int f = 2; f < frames; ++f)
    {
        const float* cur = &mags[(size_t) f * (size_t) bands];
        float acc = 0.f;
        for (int b = 0; b < bands; ++b)
        {
            float ref = 0.f;
            for (int j = std::max (0, f - 4); j <= f - 2; ++j) ref = std::max (ref, mags[(size_t) j * (size_t) bands + (size_t) b]);
            acc += std::max (0.f, cur[b] - ref);
        }
        flux[(size_t) f] = acc;
    }
}
}

static void detectTransients (SampleData& s)
{
    s.transients.clear();
    s.transients.push_back (0);
    const int n = s.length;
    const double sr = s.sampleRate;
    const int hop    = std::max (4, (int) std::lround (sr * 0.0005));
    const int look   = 3;                                                    // hops per energy window
    const int past   = std::max (2 * look, (int) std::lround (sr * 0.025) / hop);
    const int recent = std::max (look + 1, (int) std::lround (sr * 0.005) / hop);
    const int minGap = std::max (hop, (int) (sr * 0.03));                    // closer onsets belong to the same hit
    const int preRoll = (int) (sr * 0.0007);
    const int frames = n / hop;
    const int numA = frames - look + 1;
    if (numA < past || n < (int) (sr * 0.1)) return;

    const float* ch[2] = { s.data (0, 0), s.data (0, s.numChannels > 1 ? 1 : 0) };
    auto mag = [&ch] (int band, int i)
    {
        if (band == 0) return std::max (std::fabs (ch[0][i] - ch[0][i - 1]), std::fabs (ch[1][i] - ch[1][i - 1]));   // i - 1 >= -1: guard frame
        return std::max (std::fabs (ch[0][i]), std::fabs (ch[1][i]));
    };

    // ---- 1. candidates
    // energy per hop, then per window of `look` hops
    std::vector<float> win[2];
    float floorE[2] = { 0.f, 0.f };
    for (int b = 0; b < 2; ++b)
    {
        std::vector<float> e ((size_t) frames, 0.f);
        for (int f = 0; f < frames; ++f)
        {
            double acc = 0;
            for (int i = f * hop; i < (f + 1) * hop; ++i)
                for (int c = 0; c < 2; ++c)
                {
                    const double v = b == 0 ? (double) ch[c][i] - ch[c][i - 1] : (double) ch[c][i];
                    acc += v * v;
                }
            e[(size_t) f] = (float) (acc / (2.0 * hop));
        }
        win[b].assign ((size_t) numA, 0.f);
        float mx = 0.f;
        for (int f = 0; f < numA; ++f)
        {
            float a = 0.f;
            for (int k = 0; k < look; ++k) a += e[(size_t) (f + k)];
            win[b][(size_t) f] = a;
            mx = std::max (mx, a);
        }
        floorE[b] = mx * 0.0009f;                                            // 30 dB below the loudest window of this band
    }
    if (floorE[0] <= 1.0e-16f && floorE[1] <= 1.0e-16f) return;

    std::vector<OnsetCandidate> cands;
    const float ratio = 2.6f;
    const int candGap = std::max (1, (int) (sr * 0.006) / hop);
    for (int f = look; f < numA;)
    {
        int band = -1; float ref = 0.f;
        for (int b = 0; b < 2 && band < 0; ++b)
        {
            if (floorE[b] <= 1.0e-16f) continue;
            float rNear = 0.f, rFar = 0.f;
            for (int j = std::max (0, f - past); j <= f - look; ++j)
            {
                rFar = std::max (rFar, win[b][(size_t) j]);
                if (j >= f - recent) rNear = std::max (rNear, win[b][(size_t) j]);
            }
            const float now = win[b][(size_t) f];
            if (now > ratio * (rNear + floorE[b]) && now > 1.1f * rFar) { band = b; ref = rNear; }
        }
        if (band < 0) { ++f; continue; }

        // how big the rise gets once the window covers the attack
        float top = 0.f;
        for (int k = 0; k <= look && f + k < numA; ++k) top = std::max (top, win[band][(size_t) (f + k)]);
        const float strength = top / (ref + floorE[band]);

        // refine: first sample that rises clearly above what came before
        const int a = std::max (0, (f - 1) * hop), end = std::min (n, (f + look + 1) * hop);
        const int pkEnd = std::min (n, a + (int) (sr * 0.004));
        float pk = 0.f;
        for (int i = a; i < pkEnd; ++i) pk = std::max (pk, mag (band, i));
        const float refAmp = std::sqrt (ref / (float) look);
        const float level = std::min (std::max (3.5f * refAmp, 0.2f * pk), 0.8f * pk);
        int on = f * hop;
        for (int i = a; i < end; ++i) if (mag (band, i) > level) { on = i; break; }

        cands.push_back ({ std::max (0, on - preRoll), strength });
        f = std::max (f + 1, on / hop + candGap);
    }
    if (cands.empty()) return;

    // ---- 2. confirmation
    std::vector<float> flux;
    int fftSize = 0, fluxHop = 1;
    spectralFlux (ch, n, sr, flux, fftSize, fluxHop);
    const int numFlux = (int) flux.size();
    const int before = (int) (sr * 0.1) / fluxHop, after = (int) (sr * 0.07) / fluxHop;
    int last = 0;
    std::vector<float> scratch;
    float lastStrength = 0.f;
    for (const auto& c : cands)
    {
        if (c.pos + fftSize / 2 >= n) break;                                 // the end of the file is not an onset
        // frames whose window has the candidate in its leading half respond to it
        const int f0 = std::max (0, (c.pos - fftSize / 2) / fluxHop + 1), f1 = std::min (numFlux - 1, (c.pos + fftSize / 8) / fluxHop);
        float peak = 0.f; int at = f0;
        for (int f = f0; f <= f1; ++f) if (flux[(size_t) f] > peak) { peak = flux[(size_t) f]; at = f; }
        // the median of the surroundings is the flux of "nothing happening" (loud hits nearby do not move it)
        scratch.assign (flux.begin() + std::max (0, at - before), flux.begin() + std::min (numFlux - 1, at + after) + 1);
        std::nth_element (scratch.begin(), scratch.begin() + (std::ptrdiff_t) (scratch.size() / 2), scratch.end());
        const float mean = scratch[scratch.size() / 2];
        if (peak < 3.0f + 1.5f * mean) continue;
        if (c.pos - last < minGap)
        {
            // two candidates for one hit: keep the more abrupt one
            const size_t num = s.transients.size();
            if (num > 1 && c.strength > lastStrength && c.pos - s.transients[num - 2] >= minGap)
            {
                s.transients.back() = c.pos;
                last = c.pos; lastStrength = c.strength;
            }
            continue;
        }
        s.transients.push_back (c.pos);
        last = c.pos; lastStrength = c.strength;
    }
}

SampleData* makeSampleData (const float* const* channels, int numChannels, int length, double sampleRate, const char* name, float rootNote)
{
    auto* s = new SampleData();
    s->numChannels = clamp (numChannels, 1, 2);
    s->length = std::max (0, length);
    s->sampleRate = sampleRate > 1000.0 ? sampleRate : 44100.0;
    s->rootNote = rootNote;
    std::snprintf (s->name, sizeof (s->name), "%s", name ? name : "");
    float pk = 0.f;
    s->mip[0].length = s->length;
    for (int c = 0; c < s->numChannels; ++c)
    {
        s->mip[0].ch[c].assign ((size_t) (s->length + 2 * SampleData::kGuard), 0.f);
        for (int i = 0; i < s->length; ++i)
        {
            float v = channels[c][i];
            if (! std::isfinite (v)) v = 0.f;
            s->mip[0].ch[c][(size_t) (i + SampleData::kGuard)] = v;
            pk = std::max (pk, std::fabs (v));
        }
    }
    s->peak = pk;
    buildMips (*s);
    detectTransients (*s);
    return s;
}

} // namespace atm
