// Verdant — factory presets. Each preset is a sparse list of overrides on top of
// the parameter defaults, so adding a parameter never breaks old presets.
#pragma once

#include <initializer_list>
#include <vector>

#include "Params.h"

namespace verdant
{

struct PresetValue { PID pid; float value; };

struct FactoryPreset
{
    const char* name;
    const char* category;     // "Subtle", "Lush", "Motion", "Deep", "Wild"
    const char* description;
    std::vector<PresetValue> values;

    ParamValues resolve() const
    {
        ParamValues p;
        for (const auto& v : values)
            p[v.pid] = v.value;
        return p;
    }
};

const std::vector<FactoryPreset>& factoryPresets();

} // namespace verdant
