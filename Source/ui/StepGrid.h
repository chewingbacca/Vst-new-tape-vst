// Splinter UI — the glitch step sequencer: one lane per effect.
#pragma once
#include "Theme.h"
#include "plugin/PluginProcessor.h"

// Short text for a lane's per-step variation ("" = lane default).
juce::String laneVarText (int lane, int var);
// Entries for a variation menu: (var value, text).
std::vector<std::pair<int, juce::String>> laneVarChoices (int lane, int sliceCount);

class StepGrid final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit StepGrid (SplinterProcessor&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void tick();
    void setSlot (int s) { if (s != slot) { slot = s; bankVersion = -1; } }
    int getSlot() const { return slot; }
    int getSelectedLane() const { return selectedLane; }
    std::function<void (int)> onLaneSelected;

private:
    SplinterProcessor& proc;
    splinter::SeqPattern pattern;
    int slot = 0, bankVersion = -1, selectedLane = splinter::laneStutter, sliceCount = 16;
    int playStep = -1, playingSlot = -1;
    float laneGlow[splinter::kNumLanes] = {};
    // gesture state
    int dragLane = -1, dragStep = -1, dragStartProb = 100;
    bool paintValue = true, dragMoved = false, wasOn = false, probMode = false;

    static constexpr int headerW = 104;
    juce::Rectangle<float> cellBounds (int lane, int step) const;
    bool hit (juce::Point<float> p, int& lane, int& step) const;
    void commit();
    void showCellMenu (int lane, int step);
};
