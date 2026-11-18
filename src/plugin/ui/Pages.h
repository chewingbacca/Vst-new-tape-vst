// Verdant UI — the two editor pages.
//   Garden: the generative visual with the macro knobs, key / scale and freeze.
//   Soil:   one panel per module, generated from the parameter table.
#pragma once

#include "GardenVisual.h"
#include "ModulePanel.h"

namespace verdant::ui
{

//==============================================================================
class GardenPage final : public juce::Component
{
public:
    explicit GardenPage (UiContext&);

    // Timer-driven read-outs (detected pitch).
    void refresh();

    void paint (juce::Graphics&) override;
    void resized() override;

    GardenVisual visual;

private:
    UiContext& ctx;
    juce::OwnedArray<Knob> macros;
    ChoicePill key, scale;
    Toggle freeze;
    juce::Rectangle<float> panel, pitchArea;
    juce::String pitchText;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GardenPage)
};

//==============================================================================
class SoilPage final : public juce::Component
{
public:
    explicit SoilPage (UiContext&);

    void resized() override;

    GardenVisual strip;

private:
    ModulePanel* find (const juce::String& group) const;

    UiContext& ctx;
    juce::OwnedArray<ModulePanel> panels;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SoilPage)
};

} // namespace verdant::ui
