// Value text for effect parameters whose meaning depends on other parameters of the same effect, or
// whose mapping the descriptor curves cannot express. Every formula here mirrors the effect's own
// mapping (see the header comment of each effect); keep them in step.
//
//   Delay      time   sync on : tempo division, index min (int (norm * 15), 14), longest first
//                     sync off: 0.001 * 2000^norm seconds (1 ms .. 2 s)
//   Tape Stop  stopt  sync on : 1/16, 1/8, 1/4, 1/2, 1 bar for norm in fifths (min (int (norm * 5), 4))
//                     sync off: descriptor default (seconds)
//   Freq Shift shift  x = 2 norm - 1,  Hz = sign (x) * 5000 * (10^(3 |x|) - 1) / 999
//   Spectral   shift  x = 2 norm - 1,  Hz = 3000 * x * |x|
#include "Effect.h"
#include "../model/ParamTables.h"
#include <cstdio>

namespace atm {

namespace {

void formatSignedHz (double hz, char* buf, int n)
{
    const double a = std::fabs (hz);
    if (a < 0.005)        std::snprintf (buf, (size_t) n, "0 Hz");
    else if (a >= 999.5)  std::snprintf (buf, (size_t) n, "%+.2f kHz", hz / 1000.0);
    else if (a >= 99.95)  std::snprintf (buf, (size_t) n, "%+.0f Hz", hz);
    else if (a >= 9.995)  std::snprintf (buf, (size_t) n, "%+.1f Hz", hz);
    else                  std::snprintf (buf, (size_t) n, "%+.2f Hz", hz);
}

void formatSeconds (double s, char* buf, int n)
{
    if (s < 0.1)      std::snprintf (buf, (size_t) n, "%.1f ms", s * 1000.0);
    else if (s < 1.0) std::snprintf (buf, (size_t) n, "%.0f ms", s * 1000.0);
    else              std::snprintf (buf, (size_t) n, "%.2f s", s);
}

} // namespace

bool formatFxParamCustom (int fxType, int index, const float* p, char* buf, int bufSize)
{
    if (p == nullptr || buf == nullptr || bufSize <= 0) return false;
    switch (fxType)
    {
        case Fx::Delay:
            if (index == 0)
            {
                const float n = clamp01 (p[0]);
                if (kFxDelay[1].toIndex (p[1]) != 0)
                {
                    // same order as kDivBeats in DelayFx.cpp
                    static const char* const kDivs[15] = { "1/1", "1/2.", "1/2", "1/2T", "1/4.", "1/4", "1/4T", "1/8.",
                                                           "1/8", "1/8T", "1/16.", "1/16", "1/16T", "1/32", "1/64" };
                    std::snprintf (buf, (size_t) bufSize, "%s", kDivs[std::min ((int) (n * 15.f), 14)]);
                }
                else formatSeconds (0.001 * std::pow (2000.0, (double) n), buf, bufSize);
                return true;
            }
            break;

        case Fx::TapeStop:
            if (index == 1 && p[4] >= 0.5f)
            {
                static const char* const kDivs[5] = { "1/16", "1/8", "1/4", "1/2", "1 bar" };
                std::snprintf (buf, (size_t) bufSize, "%s", kDivs[std::min ((int) (clamp01 (p[1]) * 5.f), 4)]);
                return true;
            }
            break;

        case Fx::FreqShift:
            if (index == 0)
            {
                const double x = (double) kFxFreqShift[0].toPhys (p[0]);
                const double hz = (x < 0 ? -1.0 : 1.0) * 5000.0 * (std::pow (10.0, 3.0 * std::fabs (x)) - 1.0) / 999.0;
                formatSignedHz (hz, buf, bufSize);
                return true;
            }
            break;

        case Fx::Spectral:
            if (index == 2)
            {
                const double x = (double) kFxSpectral[2].toPhys (p[2]);
                formatSignedHz (3000.0 * x * std::fabs (x), buf, bufSize);
                return true;
            }
            break;

        default: break;
    }
    return false;
}

} // namespace atm
