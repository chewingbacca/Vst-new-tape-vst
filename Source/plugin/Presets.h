// Splinter — factory presets (built in code) and user preset files.
#pragma once
#include "PatternBank.h"
#include <juce_audio_utils/juce_audio_utils.h>

struct PresetData
{
    juce::String name, category;
    std::vector<std::pair<juce::String, float>> params;   // (id, real-world value); everything else resets to default
    PatternBank bank;
};

namespace presets
{
int numFactory();
juce::String factoryName (int index);
juce::String factoryCategory (int index);
void buildFactory (int index, PresetData& out);

// Parameters a preset never touches (they describe the session, not the sound).
bool isSessionParam (const juce::String& id);

juce::File userDirectory();
juce::Array<juce::File> userPresetFiles();
} // namespace presets
