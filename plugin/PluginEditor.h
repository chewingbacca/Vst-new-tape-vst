#pragma once
#include "PluginProcessor.h"

// Placeholder editor until the custom UI lands.
class AutomataEditor : public juce::AudioProcessorEditor
{
public:
    explicit AutomataEditor (AutomataProcessor& p) : AudioProcessorEditor (p), generic (p)
    {
        addAndMakeVisible (generic);
        setSize (640, 480);
    }
    void resized() override { generic.setBounds (getLocalBounds()); }
private:
    juce::GenericAudioProcessorEditor generic;
};
