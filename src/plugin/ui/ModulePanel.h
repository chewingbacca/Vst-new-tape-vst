// Verdant UI — one panel per parameter group, built from the parameter table.
#pragma once

#include "ChoicePill.h"
#include "Knob.h"
#include "Toggle.h"

namespace verdant::ui
{

class ModulePanel final : public juce::Component
{
public:
    // Builds controls for every parameter whose group matches, except the ids in `exclude`.
    ModulePanel (UiContext&, const juce::String& group, const juce::Array<int>& exclude = {});

    void setRows (int numRows)                  { if (numRows != rows) { rows = juce::jmax (1, numRows); resized(); } }
    const juce::String& getGroup() const        { return group; }
    int getNumControls() const                  { return controls.size(); }

    // Width (design px) the widest row would like with the current number of rows.
    float getDesignWidth() const;

    // Height (design px) needed for the header plus the current number of rows.
    float getDesignHeight() const;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

private:
    juce::Array<juce::Range<int>> splitRows() const;
    bool isActive() const   { return power == nullptr || power->isOn(); }
    juce::Rectangle<float> contentArea() const;

    UiContext& ctx;
    juce::String group, title;
    juce::Colour accent;
    std::unique_ptr<Toggle> power;
    juce::OwnedArray<ParamControl> controls;
    int rows = 2;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModulePanel)
};

} // namespace verdant::ui
