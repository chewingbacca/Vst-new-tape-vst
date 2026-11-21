// The chain strip on the front page: the seven reorderable modules in their
// current order (drag to reorder, click to switch on/off), the Fuzz/Wash flip
// and the fixed Tape -> Output end stages.
#pragma once

#include "ChainModel.h"

class SwoonAudioProcessor;

namespace swoon::ui
{

class ChainStrip final : public juce::Component,
                         private juce::ChangeListener,
                         private juce::Timer
{
public:
    ChainStrip (SwoonAudioProcessor&, ChainModel&);
    ~ChainStrip() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    enum class Hit { none, flip, chip, tape };

    struct PowerState
    {
        std::unique_ptr<juce::ParameterAttachment> attachment;
        bool on = true;
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void layoutTargets (bool snap);
    float slotTargetX (int position) const;
    Hit hitTestAt (juce::Point<float>, int& slot) const;
    void setHover (Hit, int slot);
    void bindPower (PowerState&, Module);
    void togglePower (PowerState&);

    void drawFlip (juce::Graphics&);
    void drawChip (juce::Graphics&, juce::Rectangle<float>, const juce::String& name, juce::Colour colour,
                   bool on, bool hasPower, bool fixed, bool hovered, bool lifted);

    SwoonAudioProcessor& processor;
    ChainModel& model;

    ChainModel::Order visualOrder {};
    std::array<float, kNumSlots> chipX {}, chipTargetX {};
    std::array<PowerState, kNumSlots> power;
    PowerState tapePower;

    juce::Rectangle<float> flipBounds, chipArea, tapeBounds, outputBounds;
    float chipW = 70.0f, chipGap = 16.0f;

    Hit hoverHit = Hit::none;
    int hoverSlot = -1;
    Hit pressHit = Hit::none;
    int pressSlot = -1;
    bool dragging = false;
    float dragGrabOffset = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChainStrip)
};

} // namespace swoon::ui
