// Front page ("the sleeve"): the haze artwork, six macros and the chain strip.
#pragma once

#include "ChainStrip.h"
#include "HazeView.h"
#include "ParamControls.h"

class SwoonAudioProcessor;

namespace swoon::ui
{

class SleevePage final : public juce::Component
{
public:
    SleevePage (SwoonAudioProcessor&, ChainModel&);

    HazeView& getHaze() noexcept { return haze; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    HazeView haze;
    std::vector<std::unique_ptr<Knob>> macros;
    ChainStrip chain;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SleevePage)
};

} // namespace swoon::ui
