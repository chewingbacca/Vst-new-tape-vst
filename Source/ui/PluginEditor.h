#pragma once
#include "plugin/PluginProcessor.h"

class SplinterEditor final : public juce::AudioProcessorEditor
{
public:
    explicit SplinterEditor (SplinterProcessor& p) : AudioProcessorEditor (p) { setSize (600, 400); }
    void paint (juce::Graphics& g) override { g.fillAll (juce::Colours::black); }
};
