// Factory presets as plain data (no JUCE) so the offline harness can render them.
#pragma once

#include "Params.h"

#include <utility>
#include <vector>

namespace swoon
{

struct PresetDef
{
    const char* name;
    const char* category;   // "Dream Pop", "Glide", "Wall", "Ambient", "Texture", ...
    const char* note;       // one-line description shown in the browser
    std::vector<std::pair<P, float>> values;   // overrides on top of the parameter defaults
};

const std::vector<PresetDef>& factoryPresets();

/** Defaults + the preset's overrides. */
ParamValues presetValues (const PresetDef& preset);

/** Parameters a preset never touches (input calibration, bypass). */
bool isPresetExempt (P p);

} // namespace swoon
