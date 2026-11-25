// Spectral freeze / blur / shift / stretch (STFT processor).
//
//   in -> ring -> [every hop: Hann window -> FFT -> spectral stages -> IFFT -> Hann window -> overlap-add] -> out
//
// FFT size N = 2048 up to 50 kHz, 4096 up to 100 kHz, 8192 above; hop N/4. Hann^2 at 75 % overlap sums
// to 1.5, so with neutral settings the output is exactly the input delayed by N samples.
// Both channels go through one complex FFT (z = L + jR, spectra separated by symmetry).
//
// Stages, per frame and per bin:
//  1. Blur / freeze. A held magnitude S follows the input magnitude with a fast attack and a slow
//     release (blur); freeze stops the update. Output = input scaled to S while the input still
//     owns the bin (>= half of S), otherwise input + (S - |input|) * tail phasor.
//     The tail phasor is synthetic: every held bin is a steady sinusoid. For every bin the per-hop
//     phase rotation is measured from two consecutive frames; a bin whose rotation agrees with its
//     louder neighbour is part of a sinusoid's main lobe ("tonal") and its tail starts from the
//     input's phase and keeps turning at the measured rate, so held partials stay clean. All other
//     bins (noise, transients - a click makes neighbouring bins disagree by exactly a quarter turn)
//     start from a random phase and turn at their bin frequency detuned by a random +-0.5 bin. That
//     removes the phase alignment between bins which would otherwise repeat the held frame as a
//     loop every N samples (the usual freeze buzz) and leaves a dense, slowly beating cluster.
//  2. Scramble: random rotation of +-pi * amount per bin and frame.
//  3. Shift / stretch. Shift alone moves every bin by the nearest whole number of bins and turns
//     all phases by the exact (fractional) amount each hop, so a partial lands exactly on f + shift.
//     Stretch uses peak-locked region shifting (Laroche & Dolson, "New phase-vocoder techniques
//     for pitch-shifting, harmonizing and other exotic effects", WASPAA 1999): the spectrum is cut
//     into regions around its local maxima, each region is moved rigidly to stretch * f_peak + shift
//     and rotated at the rate of that frequency change, which keeps every main lobe intact.
//  4. Gate (relative to the frame's strongest bin) and the lo/hi cut mask (raised-cosine edges).
//
// Stereo: left and right are processed as they are, but every random number, the tonal decision,
// the measured rotation and the shift/stretch rotations are shared by both channels, so the phase
// difference between the channels of every bin is preserved and the image does not wander.
// (Mid/side would smear a hard-panned source over both channels as soon as the two were treated
// differently by the gate.)
//
// Mappings: shift Hz = 3000 * x * |x| with x = 2 * norm - 1 (at 48 kHz: 1/8 of the spectrum);
// blur b: release tau = 8 b^3 s, attack tau = 0.12 b^2 s; gate g (norm^2): threshold
// -72 + 66 g dB below the frame peak, 0 = off; lo cut at 20 Hz and hi cut at 20 kHz = off.
#include "Effect.h"
#include "../dsp/Fft.h"
#include "../model/ParamTables.h"
#include "../util/Rng.h"
#include <vector>

namespace atm {

namespace {

enum { Freeze, Blur, Shift, Stretch, Scramble, Gate, LoCut, HiCut };

constexpr int kPhTab = 4096;
constexpr float kTiny = 1.0e-12f;
constexpr float kShiftRangeHz = 3000.f;

class SpectralFx final : public Effect
{
public:
    void prepare (double sampleRate) override
    {
        sr = sampleRate;
        N = sr <= 50000.0 ? 2048 : (sr <= 100000.0 ? 4096 : 8192);
        H = N / 4; half = N / 2;
        const size_t nb = (size_t) half + 1;
        fft.prepare (N);
        win.assign ((size_t) N, 0.f); makeHann (win.data(), N);
        for (auto* v : { &inL, &inR, &olaL, &olaR, &re, &im }) v->assign ((size_t) N, 0.f);
        for (auto* v : { &xlr, &xli, &xrr, &xri, &plr, &pli, &prr, &pri, &magL, &magR, &sL, &sR,
                         &qlr, &qli, &qrr, &qri, &rotR, &rotI, &mR, &mI, &ulr, &uli, &urr, &uri,
                         &ylr, &yli, &yrr, &yri, &zR, &zI, &mask, &pw })
            v->assign (nb, 0.f);
        cosTab.resize ((size_t) kPhTab);
        for (int i = 0; i < kPhTab; ++i) cosTab[(size_t) i] = (float) std::cos (kTau * i / kPhTab);
        hopTime = (float) (H / sr);
        binHz = (float) (sr / N);
        reset();
    }

    void reset() override
    {
        for (auto* v : { &inL, &inR, &olaL, &olaR, &plr, &pli, &prr, &pri, &sL, &sR, &qli, &qri, &rotI, &zI })
            std::fill (v->begin(), v->end(), 0.f);
        for (auto* v : { &qlr, &qrr, &rotR, &zR, &mask })
            std::fill (v->begin(), v->end(), 1.f);
        pos = 0; hopLeft = 0; zeroRun = N; anyHeld = false; prevZero = true;
        zgR = 1.f; zgI = 0.f; prevMode = 0;
        maskLo = maskHi = -1.f; maskOn = false;
        rng.seed (0x5bec7a1u);
    }

    void setParams (const float* p, const FxContext&) override
    {
        for (int i = 0; i < kFxParams; ++i) pn[i] = p[i];
    }

    void process (float* l, float* r, int n, const FxContext&) override
    {
        const int m = N - 1;
        for (int i = 0; i < n; ++i)
        {
            if (hopLeft == 0) { frame(); hopLeft = H; }
            --hopLeft;
            float a = l[i], b = r[i];
            if (! (std::fabs (a) < 1.0e6f)) a = 0.f;       // keep NaN / Inf out of the held state
            if (! (std::fabs (b) < 1.0e6f)) b = 0.f;
            zeroRun = (a == 0.f && b == 0.f) ? std::min (zeroRun + 1, N) : 0;
            l[i] = olaL[(size_t) pos]; r[i] = olaR[(size_t) pos];
            olaL[(size_t) pos] = 0.f; olaR[(size_t) pos] = 0.f;
            inL[(size_t) pos] = a; inR[(size_t) pos] = b;
            pos = (pos + 1) & m;
        }
    }

private:
    inline void phasor (int i, float& c, float& s) const       // angle = i * tau / kPhTab
    {
        c = cosTab[(size_t) (i & (kPhTab - 1))]; s = cosTab[(size_t) ((i + 3 * kPhTab / 4) & (kPhTab - 1))];
    }

    void updateMask (float loHz, float hiHz)
    {
        if (loHz == maskLo && hiHz == maskHi) return;
        maskLo = loHz; maskHi = hiHz;
        const bool loOn = loHz > 20.5f, hiOn = hiHz < 19990.f;
        maskOn = loOn || hiOn;
        if (! maskOn) return;
        const float wLo = std::max (4.f * binHz, 0.3f * loHz), wHi = std::max (4.f * binHz, 0.3f * hiHz);
        const float lo0 = loHz - 0.5f * wLo, hi0 = hiHz - 0.5f * wHi;
        for (int k = 0; k <= half; ++k)
        {
            const float f = (float) k * binHz;
            float g = 1.f;
            if (loOn) { const float t = clamp01 ((f - lo0) / wLo); g *= (t <= 0.f ? 0.f : (t >= 1.f ? 1.f : 0.5f - 0.5f * std::cos (kPiF * t))); }
            if (hiOn) { const float t = clamp01 ((f - hi0) / wHi); g *= (t <= 0.f ? 1.f : (t >= 1.f ? 0.f : 0.5f + 0.5f * std::cos (kPiF * t))); }
            mask[(size_t) k] = g;
        }
    }

    // One channel of stage 1 for one bin. Driven: (jr, ji) = phase offset for the latched tail phasor.
    // Free running: (jr, ji) = rotation per hop.
    static inline void holdBin (bool frozen, bool driven, float mag, float s, float xr, float xi,
                                float& qr, float& qi, float jr, float ji, float& ur, float& ui)
    {
        if (driven)
        {
            const float inv = 1.f / mag;
            const float a = xr * inv, b = xi * inv;
            qr = a * jr - b * ji; qi = a * ji + b * jr;
            ur = xr; ui = xi;
        }
        else if (s > 0.f)
        {
            float a = qr * jr - qi * ji, b = qr * ji + qi * jr;
            const float fix = 1.5f - 0.5f * (a * a + b * b);        // keep the phasor on the unit circle
            a *= fix; b *= fix;
            qr = a; qi = b;
            const float t = frozen ? s : std::max (0.f, s - mag);
            ur = t * a; ui = t * b;
            if (! frozen) { ur += xr; ui += xi; }
        }
        else if (frozen) { ur = 0.f; ui = 0.f; }
        else             { ur = xr; ui = xi; }
    }

    void frame()
    {
        const float blur = kFxSpectral[Blur].toPhys (pn[Blur]);
        const bool frozen = pn[Freeze] >= 0.5f;
        if (zeroRun >= N && ! anyHeld)
        {
            // nothing in the window and nothing held: the frame would be all zeros
            if (! prevZero)
            {
                for (auto* v : { &plr, &pli, &prr, &pri }) std::fill (v->begin(), v->end(), 0.f);
                prevZero = true;
            }
            return;
        }
        prevZero = false;

        const float xs = kFxSpectral[Shift].toPhys (pn[Shift]);
        const float shiftBins = kShiftRangeHz * xs * std::fabs (xs) / binHz;
        float stretch = kFxSpectral[Stretch].toPhys (pn[Stretch]);
        if (std::fabs (stretch - 1.f) < 1.0e-3f) stretch = 1.f;
        const int mode = stretch != 1.f ? 2 : (std::fabs (shiftBins) > 1.0e-4f ? 1 : 0);
        const float scr = clamp01 (kFxSpectral[Scramble].toPhys (pn[Scramble]));
        const float gate = clamp01 (kFxSpectral[Gate].toPhys (pn[Gate]));
        updateMask (kFxSpectral[LoCut].toPhys (pn[LoCut]), kFxSpectral[HiCut].toPhys (pn[HiCut]));
        const float tauA = 0.12f * blur * blur, tauR = 8.f * blur * blur * blur;
        const float aAtt = tauA > 1.0e-5f ? 1.f - std::exp (-hopTime / tauA) : 1.f;
        const float aRel = tauR > 1.0e-5f ? 1.f - std::exp (-hopTime / tauR) : 1.f;
        // The held magnitude is a peak follower, so it overstates the level it smears; the tail (and
        // the frozen spectrum) is heard at tailG times the held value. 1 at blur 0 (an exact freeze of
        // the last frame), -6 dB at the default 0.5.
        const float tailG = 1.f / (1.f + 2.f * blur);

        // ---- analysis -------------------------------------------------------------------------
        const int m = N - 1;
        float* fr = re.data(); float* fi = im.data();
        for (int n = 0; n < N; ++n)
        {
            const size_t idx = (size_t) ((pos + n) & m);
            fr[n] = inL[idx] * win[(size_t) n]; fi[n] = inR[idx] * win[(size_t) n];
        }
        fft.forward (fr, fi);

        float* XLr = xlr.data(); float* XLi = xli.data(); float* XRr = xrr.data(); float* XRi = xri.data();
        float* PLr = plr.data(); float* PLi = pli.data(); float* PRr = prr.data(); float* PRi = pri.data();
        float* ML = magL.data(); float* MR = magR.data();
        float* mr = mR.data(); float* mi = mI.data();
        XLr[0] = fr[0]; XLi[0] = 0.f; XRr[0] = fi[0]; XRi[0] = 0.f;
        XLr[half] = fr[half]; XLi[half] = 0.f; XRr[half] = fi[half]; XRi[half] = 0.f;
        for (int k = 1; k < half; ++k)
        {
            const float a = fr[k], b = fi[k], c = fr[N - k], d = fi[N - k];
            XLr[k] = 0.5f * (a + c); XLi[k] = 0.5f * (b - d);
            XRr[k] = 0.5f * (b + d); XRi[k] = 0.5f * (c - a);
        }
        // magnitudes and the rotation since the previous frame (power-weighted over both channels)
        for (int k = 0; k <= half; ++k)
        {
            const float lr = XLr[k], li = XLi[k], rr = XRr[k], ri = XRi[k];
            ML[k] = std::sqrt (lr * lr + li * li);
            MR[k] = std::sqrt (rr * rr + ri * ri);
            const float cr = lr * PLr[k] + li * PLi[k] + rr * PRr[k] + ri * PRi[k];
            const float ci = li * PLr[k] - lr * PLi[k] + ri * PRr[k] - rr * PRi[k];
            const float nrm = cr * cr + ci * ci;
            if (nrm > 1.0e-30f) { const float inv = 1.f / std::sqrt (nrm); mr[k] = cr * inv; mi[k] = ci * inv; }
            else                { mr[k] = 0.f; mi[k] = 0.f; }
            PLr[k] = lr; PLi[k] = li; PRr[k] = rr; PRi[k] = ri;
        }

        // ---- 1. blur / freeze -----------------------------------------------------------------
        float* SL = sL.data(); float* SR = sR.data();
        float* QLr = qlr.data(); float* QLi = qli.data(); float* QRr = qrr.data(); float* QRi = qri.data();
        float* RR = rotR.data(); float* RI = rotI.data();
        const bool remap = mode != 0;
        float* ULr = remap ? ulr.data() : ylr.data(); float* ULi = remap ? uli.data() : yli.data();
        float* URr = remap ? urr.data() : yrr.data(); float* URi = remap ? uri.data() : yri.data();
        bool held = false;
        for (int k = 0; k <= half; ++k)
        {
            const float ml = ML[k], mrr = MR[k];
            bool dL = false, dR = false;
            if (! frozen)
            {
                float s = SL[k]; float d = ml - s;
                s += (d > 0.f ? aAtt : aRel) * d; if (s < 1.0e-10f && d < 0.f) s = 0.f;
                SL[k] = s; dL = ml > kTiny && ml >= tailG * s;
                s = SR[k]; d = mrr - s;
                s += (d > 0.f ? aAtt : aRel) * d; if (s < 1.0e-10f && d < 0.f) s = 0.f;
                SR[k] = s; dR = mrr > kTiny && mrr >= tailG * s;
            }
            const float sl = SL[k], sr_ = SR[k];
            held = held || sl > 0.f || sr_ > 0.f;
            float lr = 1.f, li = 0.f;                // phase offset given to a tail phasor when it is latched
            if (dL || dR)
            {
                // the input owns the bin: decide whether it belongs to a sinusoid and latch its rotation
                // the strongest of the bins k-2 .. k+2 is taken as the peak of the lobe this bin belongs to;
                // a tonal bin turns with the rotation of that peak, so a whole main lobe stays in step
                int pk = k; float top = ml + mrr;
                for (int q = std::max (0, k - 2); q <= std::min (half, k + 2); ++q)
                    if (ML[q] + MR[q] > top) { top = ML[q] + MR[q]; pk = q; }
                const float cr = mr[pk], ci = mi[pk];
                bool t = false;
                if (cr != 0.f || ci != 0.f)
                {
                    if (pk > 0 && pk < half)
                    {
                        // tonal: the peak sits within 0.75 bin of its bin frequency, its louder neighbour
                        // turns with it (a click makes neighbours differ by a quarter turn), and so does this bin
                        const float onBin = (pk & 1) ? ((pk & 2) ? -ci : ci) : ((pk & 2) ? -cr : cr);
                        const int nbr = (ML[pk - 1] + MR[pk - 1] >= ML[pk + 1] + MR[pk + 1]) ? pk - 1 : pk + 1;
                        t = onBin > 0.383f && (cr * mr[nbr] + ci * mi[nbr]) > 0.92f && (cr * mr[k] + ci * mi[k]) > 0.92f;
                    }
                }
                if (t) { RR[k] = cr; RI[k] = ci; }
                else
                {
                    // bin frequency (a quarter turn per bin and hop) detuned by up to +-0.5 bin, random start phase
                    const uint32_t bits = rng.nextBits();
                    phasor (k * (kPhTab / 4) + (((int) ((bits >> 8) & 0xfffu) - 2048) >> 2), RR[k], RI[k]);
                    phasor ((int) (bits >> 20), lr, li);
                }
            }
            const float rr = RR[k], ri = RI[k];
            holdBin (frozen, dL, ml, tailG * sl, XLr[k], XLi[k], QLr[k], QLi[k], dL ? lr : rr, dL ? li : ri, ULr[k], ULi[k]);
            holdBin (frozen, dR, mrr, tailG * sr_, XRr[k], XRi[k], QRr[k], QRi[k], dR ? lr : rr, dR ? li : ri, URr[k], URi[k]);
        }
        anyHeld = held;

        // ---- 2. scramble ----------------------------------------------------------------------
        if (scr > 0.f)
        {
            // coherent part left after a uniform +-pi*scr rotation: sinc (scr); the rest adds in power (-6 dB)
            const float c = scr < 1.f ? std::sin (kPiF * scr) / (kPiF * scr) : 0.f;
            const float comp = 1.f / std::sqrt (c * c + 0.25f * (1.f - c * c));
            const float range = scr * (float) kPhTab;
            for (int k = 1; k < half; ++k)
            {
                const float u = (float) (rng.nextBits() >> 8) * (1.f / 16777216.f) - 0.5f;
                const int i = (int) (u * range) & (kPhTab - 1);
                const float cs = cosTab[(size_t) i] * comp, sn = cosTab[(size_t) ((i + 3 * kPhTab / 4) & (kPhTab - 1))] * comp;
                float a = ULr[k], b = ULi[k];
                ULr[k] = a * cs - b * sn; ULi[k] = a * sn + b * cs;
                a = URr[k]; b = URi[k];
                URr[k] = a * cs - b * sn; URi[k] = a * sn + b * cs;
            }
        }

        // ---- 3. shift / stretch ---------------------------------------------------------------
        float* YLr = ylr.data(); float* YLi = yli.data(); float* YRr = yrr.data(); float* YRi = yri.data();
        if (mode == 0) { zgR = 1.f; zgI = 0.f; }
        else if (mode == 1)
        {
            if (prevMode == 2) { zgR = 1.f; zgI = 0.f; }
            const int D = (int) std::lround (shiftBins);
            const float frac = shiftBins - (float) D;
            const float ang = 0.5f * kPiF * (shiftBins - 4.f * std::floor (shiftBins * 0.25f));
            const float cs = std::cos (ang), sn = std::sin (ang);
            float a = zgR * cs - zgI * sn, b = zgR * sn + zgI * cs;
            const float fix = 1.5f - 0.5f * (a * a + b * b);
            zgR = a * fix; zgI = b * fix;
            const float g = ((D & 1) ? -1.f : 1.f) * (1.f + kFracComp * frac * frac);
            const float gr = zgR * g, gi = zgI * g;
            YLr[0] = YLi[0] = YRr[0] = YRi[0] = 0.f;
            YLr[half] = YLi[half] = YRr[half] = YRi[half] = 0.f;
            for (int j = 1; j < half; ++j)
            {
                const int k = j - D;
                if (k >= 1 && k < half)
                {
                    YLr[j] = ULr[k] * gr - ULi[k] * gi; YLi[j] = ULr[k] * gi + ULi[k] * gr;
                    YRr[j] = URr[k] * gr - URi[k] * gi; YRi[j] = URr[k] * gi + URi[k] * gr;
                }
                else { YLr[j] = YLi[j] = YRr[j] = YRi[j] = 0.f; }
            }
        }
        else
        {
            float* ZR = zR.data(); float* ZI = zI.data();
            if (prevMode != 2) for (int k = 0; k <= half; ++k) { ZR[k] = zgR; ZI[k] = zgI; }
            for (int k = 0; k <= half; ++k) YLr[k] = YLi[k] = YRr[k] = YRi[k] = 0.f;
            // regions around the local maxima of the held magnitude, split half way between two peaks
            int peak = -1, start = 1;
            for (int k = 2; k < half - 1; ++k)
            {
                const float e = SL[k] + SR[k];
                if (e > SL[k - 1] + SR[k - 1] && e >= SL[k + 1] + SR[k + 1]
                    && e > SL[k - 2] + SR[k - 2] && e >= SL[k + 2] + SR[k + 2])
                {
                    if (peak >= 0)
                    {
                        const int split = (peak + k + 1) >> 1;
                        moveRegion (start, split - 1, peak, stretch, shiftBins);
                        start = split;
                    }
                    peak = k;
                }
            }
            if (peak >= 0) moveRegion (start, half - 1, peak, stretch, shiftBins);
        }
        prevMode = mode;

        // ---- 4. gate and band limits ----------------------------------------------------------
        if (gate > 0.f)
        {
            float* P = pw.data();
            float top = 0.f;
            for (int k = 0; k <= half; ++k)
            {
                P[k] = YLr[k] * YLr[k] + YLi[k] * YLi[k] + YRr[k] * YRr[k] + YRi[k] * YRi[k];
                top = std::max (top, P[k]);
            }
            const float thr = top * std::pow (10.f, 0.1f * (-72.f + 66.f * gate));
            const float invThr = thr > 1.0e-30f ? 1.f / thr : 0.f;
            for (int k = 0; k <= half; ++k)
            {
                const float g = clamp01 (P[k] * invThr - 1.f);
                YLr[k] *= g; YLi[k] *= g; YRr[k] *= g; YRi[k] *= g;
            }
        }
        if (maskOn)
        {
            const float* M = mask.data();
            for (int k = 0; k <= half; ++k) { const float g = M[k]; YLr[k] *= g; YLi[k] *= g; YRr[k] *= g; YRi[k] *= g; }
        }

        // ---- synthesis ------------------------------------------------------------------------
        fr[0] = YLr[0]; fi[0] = YRr[0];
        fr[half] = YLr[half]; fi[half] = YRr[half];
        for (int k = 1; k < half; ++k)
        {
            fr[k] = YLr[k] - YRi[k];     fi[k] = YLi[k] + YRr[k];
            fr[N - k] = YLr[k] + YRi[k]; fi[N - k] = YRr[k] - YLi[k];
        }
        fft.inverse (fr, fi);
        const float norm = 2.f / 3.f;
        for (int n = 0; n < N; ++n)
        {
            const size_t idx = (size_t) ((pos + n) & m);
            const float w = win[(size_t) n] * norm;
            olaL[idx] += fr[n] * w; olaR[idx] += fi[n] * w;
        }
    }

    // Move the bins lo..hi (a region around the peak at bin kp) to their stretched / shifted place.
    void moveRegion (int lo, int hi, int kp, float stretch, float shiftBins)
    {
        // true frequency of the peak: bin centre + deviation from the latched rotation (hop = N/4,
        // so a bin offset of 1 turns the phase by a quarter turn per hop)
        float a = rotR[(size_t) kp], b = rotI[(size_t) kp];
        switch (kp & 3)
        {
            case 1: { const float t = a; a = b; b = -t; break; }
            case 2: a = -a; b = -b; break;
            case 3: { const float t = a; a = -b; b = t; break; }
            default: break;
        }
        const float dev = (a != 0.f || b != 0.f) ? std::atan2 (b, a) * (2.f / kPiF) : 0.f;
        const float df = ((float) kp + dev) * (stretch - 1.f) + shiftBins;
        const int D = (int) std::lround (df);
        const float frac = df - (float) D;
        const float ang = 0.5f * kPiF * (df - 4.f * std::floor (df * 0.25f));
        const float cs = std::cos (ang), sn = std::sin (ang);
        float zr = zR[(size_t) kp] * cs - zI[(size_t) kp] * sn, zi = zR[(size_t) kp] * sn + zI[(size_t) kp] * cs;
        const float fix = 1.5f - 0.5f * (zr * zr + zi * zi);
        zr *= fix; zi *= fix;
        const float g = ((D & 1) ? -1.f : 1.f) * (1.f + kFracComp * frac * frac);
        const float gr = zr * g, gi = zi * g;
        float* ZR = zR.data(); float* ZI = zI.data();
        const float* ULr = ulr.data(); const float* ULi = uli.data(); const float* URr = urr.data(); const float* URi = uri.data();
        float* YLr = ylr.data(); float* YLi = yli.data(); float* YRr = yrr.data(); float* YRi = yri.data();
        for (int k = lo; k <= hi; ++k)
        {
            ZR[k] = zr; ZI[k] = zi;
            const int j = k + D;
            if (j >= 1 && j < half)
            {
                YLr[j] += ULr[k] * gr - ULi[k] * gi; YLi[j] += ULr[k] * gi + ULi[k] * gr;
                YRr[j] += URr[k] * gr - URi[k] * gi; YRi[j] += URr[k] * gi + URi[k] * gr;
            }
        }
    }

    // A region placed up to half a bin away from its true frequency loses up to 0.86 dB in the
    // overlap-add (Hann^2 at 75 %); 1 + c d^2 makes that up.
    static constexpr float kFracComp = 0.42f;

    struct Bits
    {
        uint32_t s = 1u;
        void seed (uint32_t v) { s = hash32 (v) | 1u; }
        inline uint32_t nextBits() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    };

    double sr = 48000.0;
    int N = 2048, H = 512, half = 1024;
    float hopTime = 0.01f, binHz = 23.4f;
    Fft fft;
    std::vector<float> win, inL, inR, olaL, olaR, re, im;
    std::vector<float> xlr, xli, xrr, xri, plr, pli, prr, pri, magL, magR, sL, sR;
    std::vector<float> qlr, qli, qrr, qri, rotR, rotI, mR, mI, ulr, uli, urr, uri, ylr, yli, yrr, yri, zR, zI, mask, pw;
    std::vector<float> cosTab;
    float pn[kFxParams] = {};
    int pos = 0, hopLeft = 0, zeroRun = 0, prevMode = 0;
    bool anyHeld = false, prevZero = true, maskOn = false;
    float maskLo = -1.f, maskHi = -1.f;
    float zgR = 1.f, zgI = 0.f;
    Bits rng;
};

} // namespace

std::unique_ptr<Effect> createSpectralFx() { return std::make_unique<SpectralFx>(); }

} // namespace atm
