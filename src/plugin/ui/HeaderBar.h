// Verdant UI — header: wordmark, page tabs, preset bar, level meters, bypass.
#pragma once

#include "Meter.h"
#include "PresetBrowser.h"
#include "Toggle.h"
#include "Widgets.h"

namespace verdant::ui
{

class HeaderBar final : public juce::Component
{
public:
    explicit HeaderBar (UiContext&);

    void paint (juce::Graphics&) override;
    void resized() override;

    TabSwitch tabs;
    PresetBar presetBar;
    LevelMeters meters;
    Toggle bypass;

private:
    UiContext& ctx;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HeaderBar)
};

} // namespace verdant::ui
