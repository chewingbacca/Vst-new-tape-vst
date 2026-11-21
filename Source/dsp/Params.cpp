#include "Params.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace swoon
{

const std::array<ParamDef, kNumParams>& paramDefs()
{
    static const std::array<ParamDef, kNumParams> defs { {
#define X(name, id, label, module, kind, lo, hi, def, mid, unit, choices) \
        ParamDef { P::name, id, label, Module::module, ParamKind::kind, lo, hi, def, mid, unit, choices },
        SWOON_PARAM_LIST(X)
#undef X
    } };
    return defs;
}

ParamValues::ParamValues()
{
    const auto& d = paramDefs();
    for (size_t i = 0; i < v.size(); ++i)
        v[i] = d[i].def;
}

P paramFromId (const std::string& id)
{
    for (const auto& d : paramDefs())
        if (id == d.id)
            return d.index;
    return P::Count;
}

static float skewExponent (const ParamDef& d)
{
    if (d.skewMid <= d.min || d.skewMid >= d.max)
        return 1.0f;
    // Same convention as juce::NormalisableRange::setSkewForCentre
    return std::log (0.5f) / std::log ((d.skewMid - d.min) / (d.max - d.min));
}

float toNormalised (P p, float real)
{
    const auto& d = def (p);
    float prop = (std::clamp (real, d.min, d.max) - d.min) / (d.max - d.min);
    const float e = skewExponent (d);
    return e == 1.0f ? prop : std::pow (prop, e);
}

float fromNormalised (P p, float n)
{
    const auto& d = def (p);
    n = std::clamp (n, 0.0f, 1.0f);
    const float e = skewExponent (d);
    if (e != 1.0f && n > 0.0f)
        n = std::exp (std::log (n) / e);
    float real = d.min + (d.max - d.min) * n;
    if (d.kind != ParamKind::Float)
        real = std::round (real);
    return real;
}

const char* slotName (Slot s)
{
    switch (s)
    {
        case Slot::Glide:   return "Glide";
        case Slot::Fuzz:    return "Fuzz";
        case Slot::Amp:     return "Amp";
        case Slot::Mod:     return "Mod";
        case Slot::Focus:   return "Focus";
        case Slot::Reverse: return "Reverse";
        case Slot::Wash:    return "Wash";
    }
    return "";
}

std::array<int, kNumSlots> resolveOrder (const ParamValues& pv)
{
    std::array<int, kNumSlots> order {};
    for (int i = 0; i < kNumSlots; ++i)
        order[static_cast<size_t> (i)] = i;

    std::stable_sort (order.begin(), order.end(), [&pv] (int a, int b)
    {
        return std::lround (pv[kOrderParams[static_cast<size_t> (a)]])
             < std::lround (pv[kOrderParams[static_cast<size_t> (b)]]);
    });
    return order;
}

} // namespace swoon
