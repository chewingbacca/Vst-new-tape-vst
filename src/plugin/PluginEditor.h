#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "PluginProcessor.h"

// Placeholder editor; replaced by the custom UI.
class VerdantEditor final : public juce::AudioProcessorEditor
{
public:
    explicit VerdantEditor (VerdantProcessor& p)
        : juce::AudioProcessorEditor (p), generic (p)
    {
        addAndMakeVisible (generic);
        setSize (520, 640);
    }

    void resized() override { generic.setBounds (getLocalBounds()); }

private:
    juce::GenericAudioProcessorEditor generic;
};
