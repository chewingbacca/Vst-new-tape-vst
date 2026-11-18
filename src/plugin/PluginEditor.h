// Verdant — the plugin editor: header, Garden / Soil pages, preset overlay, help line.
// Everything is custom drawn (see src/plugin/ui) and scales from one factor.
#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "PluginProcessor.h"
#include "ui/ChoicePill.h"
#include "ui/HeaderBar.h"
#include "ui/Pages.h"
#include "ui/PresetBrowser.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

class VerdantEditor final : public juce::AudioProcessorEditor,
                            private juce::Timer
{
public:
    explicit VerdantEditor (VerdantProcessor&);
    ~VerdantEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void visibilityChanged() override;
    void parentHierarchyChanged() override;

    //==============================================================================
    enum Page { garden = 0, soil = 1 };

    void setPage (int page);
    int getPage() const noexcept    { return page; }
    void setPresetBrowserOpen (bool shouldBeOpen);

    //==============================================================================
    // Hooks for the snapshot tool / tests (the timer only runs while the editor is showing).

    // Steps meters, read-outs and the visual by `seconds` in 1/30 s frames.
    void advanceAnimation (double seconds);

    // Shows a parameter in the help line as if the mouse hovered its control.
    void debugShowHelpFor (verdant::PID);

    // Opens the choice list of the first visible pill bound to `pid`. Returns false if none.
    bool debugOpenChoiceList (verdant::PID);

    void debugSetExtraPresets (const juce::Array<VerdantProcessor::PresetInfo>&);

    // Opens the preset browser and saves the current settings as a user preset through the UI.
    void debugSavePreset (const juce::String& name);

    // Typed value entry: opens the inline field of the first visible knob bound to `pid`
    // (optionally replacing its text). Returns false if there is no such knob.
    bool debugOpenValueEntry (verdant::PID, const juce::String& text = {});

    // Closes every open value field, committing (Return) or cancelling (Escape).
    void debugCloseValueEntries (bool commit);

    verdant::ui::GardenVisual& getActiveVisual();

private:
    void timerCallback() override;
    void step (float dt);
    void updateTimerState();

    VerdantProcessor& proc;
    verdant::ui::VerdantLookAndFeel lookAndFeel;
    verdant::ui::UiContext ctx;

    // Declaration order matters: the help line and the choice list are reached through
    // ctx callbacks by the controls below, so they must outlive them (members are
    // destroyed in reverse order).
    verdant::ui::StatusLine status;
    verdant::ui::ChoicePopup choicePopup;
    verdant::ui::HeaderBar header;
    verdant::ui::GardenPage gardenPage;
    verdant::ui::SoilPage soilPage;
    verdant::ui::PresetBrowser browser;

    int page = garden;
    double lastTickMs = 0.0;
    bool constructed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VerdantEditor)
};
