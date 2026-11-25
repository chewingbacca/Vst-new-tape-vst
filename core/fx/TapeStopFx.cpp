// Tape Stop: a virtual tape transport. The input is always recorded; the output is a read head
// whose distance behind the record head (the delay d) follows the transport speed v:
//
//   stop   v falls from 1 to 0 over the stop time along `curve`, d grows by (1 - v) per sample, so
//          pitch and speed drop together from a perfectly continuous start; the level follows the
//          speed to silence over the last few percent.
//   start  from a standstill the read head is put on the record head, so the tape starts on what
//          is coming in right now (a hit on the release step is heard from its first sample,
//          winding up to pitch); released in mid-deceleration it simply carries on from where it
//          is. v(s) = v1 + (1 - v1) (1 - (1 - s)^2) over (1 - v1) * start time. A tape can only fall
//          behind, so once the speed is exactly 1 the head (now a constant few ms late) is
//          crossfaded to the live input, and from there the output is the untouched input again.
//   wow    a slowly modulated extra delay (two incommensurate wow rates plus a little flutter).
//
// Stop time: `stopt` seconds, or with `sync` on a tempo division picked by the same knob position:
// normalised 0..0.2 = 1/16, 0.2..0.4 = 1/8, 0.4..0.6 = 1/4, 0.6..0.8 = 1/2, 0.8..1 = 1 bar
// (4 beats). Both are limited to 5 s. Stop time and curve are latched when the stop begins.
#include "Effect.h"
#include "../model/ParamTables.h"
#include <vector>

namespace atm {
namespace {

inline float smooth01 (float x) { x = clamp01 (x); return x * x * (3.f - 2.f * x); }

struct History
{
    std::vector<float> d; int64_t mask = 0;
    void prepare (int64_t minLen) { size_t n = 1024; while ((int64_t) n < minLen) n <<= 1; d.assign (n, 0.f); mask = (int64_t) n - 1; }
    void clear() { std::fill (d.begin(), d.end(), 0.f); }
    int64_t size() const { return mask + 1; }
    inline float& operator[] (int64_t i) { return d[(size_t) (i & mask)]; }
    inline float at (int64_t i) const { return d[(size_t) (i & mask)]; }
    // delay in samples behind index `now` (the sample written last). Below one sample of delay there
    // is nothing "ahead" for a 4-point kernel, so use the two-point one there; both agree at 1.
    inline float read (int64_t now, double delay) const
    {
        if (delay < 1.0) { const float a = at (now), b = at (now - 1); return a + (b - a) * (float) delay; }
        const double pos = (double) now - delay, fl = std::floor (pos);
        const int64_t i = (int64_t) fl; const float f = (float) (pos - fl);
        return hermite4 (at (i - 1), at (i), at (i + 1), at (i + 2), f);
    }
};

// Deceleration from v0 to 0 over N samples: v = v0 (e^(-k u) - e^(-k)) / (1 - e^(-k)), u = n / N.
// k -> 0 is a straight line, larger k drops fast and sags into the standstill.
struct Decel
{
    double v0 = 1.0, v = 1.0, k = 0.0, ek = 1.0, ekStep = 1.0, eEnd = 0.0, norm = 1.0;
    int64_t n = 0, N = 1;
    void start (double from, double samples, double curveK)
    {
        v0 = v = from; N = std::max<int64_t> (1, (int64_t) std::llround (samples)); n = 0; k = curveK;
        ek = 1.0; ekStep = std::exp (-k / (double) N); eEnd = std::exp (-k); norm = k > 1.0e-3 ? 1.0 / (1.0 - eEnd) : 1.0;
    }
    // returns false once the tape stands still
    bool tick()
    {
        if (++n >= N) { v = 0.0; return false; }
        if (k > 1.0e-3) { ek *= ekStep; v = v0 * (ek - eEnd) * norm; }
        else v = v0 * (1.0 - (double) n / (double) N);
        return true;
    }
};

class TapeStopFx final : public Effect
{
public:
    enum { On, StopT, StartT, Curve, Sync, Wow };

    void prepare (double sampleRate) override
    {
        sr = sampleRate;
        buf[0].prepare ((int64_t) (sr * 5.1) + 64); buf[1].prepare (buf[0].size());
        maxDelay = (double) (buf[0].size() - 16);
        wowCoef = (double) onePoleCoef (0.03f, (float) sr);
        reset();
    }

    void reset() override
    {
        buf[0].clear(); buf[1].clear();
        w = buf[0].size();
        mode = Idle; engaged = false; d = 0.0; v = 1.0;
        ghost.active = false;
        wowAmt = wowTarget; ph1 = 0.0; ph2 = 0.37; ph3 = 0.11;
    }

    bool isTransparentWhenIdle() const override { return wowTarget <= 0.0; }

    void setParams (const float* p, const FxContext& c) override
    {
        wantOn = p[On] >= 0.5f;
        const bool sync = p[Sync] >= 0.5f;
        const double bpm = clamp (c.bpm, 20.0, 999.0);
        if (p[StopT] != lastStop || sync != lastSync || bpm != lastBpm)
        {
            lastStop = p[StopT]; lastSync = sync; lastBpm = bpm;
            if (sync)
            {
                static constexpr double kBeats[5] = { 0.25, 0.5, 1.0, 2.0, 4.0 };       // 1/16, 1/8, 1/4, 1/2, 1 bar
                stopSec = kBeats[std::min (4, (int) (clamp01 (lastStop) * 5.f))] * 60.0 / bpm;
            }
            else stopSec = (double) kFxTapeStop[StopT].toPhys (lastStop);
            stopSec = clamp (stopSec, 0.005, 5.0);
        }
        if (p[StartT] != lastStart) { lastStart = p[StartT]; startSec = (double) kFxTapeStop[StartT].toPhys (lastStart); }
        curveK = 4.5 * (double) clamp01 (p[Curve]);
        const double wn = (double) clamp01 (p[Wow]);
        wowTarget = wn * wn;
    }

    void process (float* l, float* r, int n, const FxContext&) override
    {
        if (wantOn != engaged) { if (wantOn) engage(); else release(); }

        for (int i = 0; i < n; ++i)
        {
            const float inL = l[i], inR = r[i];
            buf[0][w] = inL; buf[1][w] = inR;

            switch (mode)
            {
                case Idle: case Stopped: break;
                case Stopping:
                    if (dec.tick()) { v = dec.v; d = std::min (d + 1.0 - v, maxDelay); }
                    else { v = 0.0; mode = Stopped; }
                    break;
                case Starting:
                    if (++un >= uN)
                    {
                        // up to speed: hand over from the (late) tape head to the live input
                        ghost.active = d > 1.0e-3; ghost.d = d;
                        ghost.xf = 1.f; ghost.xfStep = 1.f / (float) clamp (d, sr * 0.002, sr * 0.008);
                        mode = Idle; v = 1.0; d = 0.0;
                    }
                    else
                    {
                        const double q = 1.0 - (double) un / (double) uN;
                        v = v1 + (1.0 - v1) * (1.0 - q * q);
                        d = std::min (d + 1.0 - v, maxDelay);
                    }
                    break;
            }

            double dw = 0.0;
            if (wowAmt != 0.0 || wowTarget != 0.0)
            {
                wowAmt += wowCoef * (wowTarget - wowAmt);
                if (wowTarget == 0.0 && wowAmt < 2.0e-6) wowAmt = 0.0;
                ph1 += 0.71 / sr; if (ph1 >= 1.0) ph1 -= 1.0;
                ph2 += 1.13 / sr; if (ph2 >= 1.0) ph2 -= 1.0;
                ph3 += 6.3 / sr;  if (ph3 >= 1.0) ph3 -= 1.0;
                // peak speed deviation: about 1.8 % + 0.9 % (wow) + 0.35 % (flutter) at full depth
                dw = wowAmt * sr * (0.0003 + 0.00404 * (1.0 + (double) fastSin01 ((float) ph1))
                                           + 0.00127 * (1.0 + (double) fastSin01 ((float) ph2))
                                           + 0.000088 * (1.0 + (double) fastSin01 ((float) ph3)));
            }

            if (mode == Idle && ! ghost.active && dw == 0.0) { ++w; continue; }           // untouched

            float oL = 0.f, oR = 0.f;
            const float g = mode == Stopped ? 0.f : speedGain (v);
            if (g > 0.f)
            {
                const double dd = std::min (d + dw, maxDelay);
                oL = g * buf[0].read (w, dd); oR = g * buf[1].read (w, dd);
            }
            if (ghost.active)
            {
                ghost.xf -= ghost.xfStep;
                if (ghost.xf <= 0.f) ghost.active = false;
                else
                {
                    const float x = smooth01 (ghost.xf);
                    const double dd = std::min (ghost.d + dw, maxDelay);
                    oL += x * (buf[0].read (w, dd) - oL); oR += x * (buf[1].read (w, dd) - oR);
                }
            }
            ++w;
            l[i] = oL; r[i] = oR;
        }
    }

private:
    enum Mode { Idle, Stopping, Stopped, Starting };
    struct Ghost { bool active = false; double d = 0.0; float xf = 0.f, xfStep = 0.f; };      // full-speed head fading out under the live input

    // a tape head's output falls with the tape speed; the last ~6 % of speed fades to silence (and takes the held DC with it)
    static float speedGain (double speed) { return smooth01 ((float) (speed * (1.0 / 0.06))); }

    void engage()
    {
        engaged = true;
        if (mode == Idle) { d = 0.0; v = 1.0; }
        if (mode == Idle || mode == Starting)
        {
            dec.start (v, stopSec * sr * std::max (v, 0.02), curveK);
            mode = Stopping;
        }
    }

    void release()
    {
        engaged = false;
        if (mode == Stopping) v1 = v;                                   // carry on from where the tape is
        else if (mode == Stopped) { v1 = 0.0; d = 0.0; }                // restart on the incoming audio
        else return;
        uN = std::max<int64_t> (1, (int64_t) std::llround (startSec * sr * (1.0 - v1)));
        un = 0; v = v1;
        mode = Starting;
    }

    double sr = 48000.0, maxDelay = 0.0;
    History buf[2];
    int64_t w = 0;
    bool wantOn = false, engaged = false;

    // parameters
    double stopSec = 0.25, startSec = 0.06, curveK = 2.25, wowTarget = 0.0, lastBpm = 0.0;
    float  lastStop = -1.f, lastStart = -1.f;
    bool   lastSync = false;

    // transport
    Mode   mode = Idle;
    double d = 0.0, v = 1.0;
    Decel  dec;
    double v1 = 0.0;
    int64_t un = 0, uN = 1;
    Ghost  ghost;

    double wowAmt = 0.0, wowCoef = 0.001, ph1 = 0.0, ph2 = 0.37, ph3 = 0.11;
};

} // namespace

std::unique_ptr<Effect> createTapeStopFx() { return std::make_unique<TapeStopFx>(); }

} // namespace atm
