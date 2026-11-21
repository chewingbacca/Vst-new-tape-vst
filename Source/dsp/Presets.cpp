#include "Presets.h"

namespace swoon
{

bool isPresetExempt (P p)
{
    return p == P::bypass || p == P::inGain;
}

ParamValues presetValues (const PresetDef& preset)
{
    ParamValues pv;
    for (const auto& [p, value] : preset.values)
        if (! isPresetExempt (p))
            pv[p] = value;
    return pv;
}

const std::vector<PresetDef>& factoryPresets()
{
    static const std::vector<PresetDef> presets {
        { "Init", "Utility", "Default settings.", {} },
    };
    return presets;
}

} // namespace swoon
