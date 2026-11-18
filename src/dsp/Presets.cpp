#include "Presets.h"

namespace verdant
{

const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> presets = {
        { "Greenhouse Dawn", "Lush", "The default: warm tape, a soft cloud, slow shimmer.", {} },
    };
    return presets;
}

} // namespace verdant
