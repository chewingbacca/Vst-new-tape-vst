#pragma once

#include "PluginProcessor.h"

// Placeholder editor: replaced by the custom UI.
class SwoonAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit SwoonAudioProcessorEditor (SwoonAudioProcessor&);
    void resized() override;

private:
    juce::GenericAudioProcessorEditor generic;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SwoonAudioProcessorEditor)
};
