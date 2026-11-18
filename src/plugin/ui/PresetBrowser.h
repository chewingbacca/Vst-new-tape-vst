// Verdant UI — header preset bar (previous / name / next) and the in-editor preset overlay.
#pragma once

#include "../PluginProcessor.h"
#include "Theme.h"
#include "Widgets.h"

namespace verdant::ui
{

//==============================================================================
class PresetBar final : public juce::Component
{
public:
    explicit PresetBar (UiContext&);
    ~PresetBar() override;

    // Polled from the editor timer so host program changes show up.
    void refreshName();
    void setBrowserOpen (bool open)     { browserOpen = open; repaint(); }

    std::function<void()> onToggleBrowser;
    std::function<void()> onPresetChanged;

    void step (int direction);

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    int zoneAt (juce::Point<float>) const;

    UiContext& ctx;
    juce::String name;
    int hoverZone = 2;
    bool browserOpen = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetBar)
};

//==============================================================================
class PresetBrowser final : public juce::Component
{
public:
    explicit PresetBrowser (UiContext&);
    ~PresetBrowser() override;

    void open();
    void close();
    bool isOpen() const noexcept    { return isVisible(); }

    // Re-reads the preset list (factory + user folder) and the current preset name.
    void refresh();
    void refreshCurrent();

    // Extra entries shown after the real ones; used by the snapshot tool to test scrolling.
    void setDebugExtraPresets (const juce::Array<VerdantProcessor::PresetInfo>&);

    // Types `name` into the name field and presses Save (snapshot tool / tests).
    void debugSaveAs (const juce::String& name);

    std::function<void()> onClose;
    std::function<void()> onPresetChanged;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    class List;

    void saveCurrent();
    juce::Rectangle<float> panelBounds() const;

    UiContext& ctx;
    juce::Viewport viewport;
    std::unique_ptr<List> list;
    juce::TextEditor nameEditor;
    PillButton saveButton, closeButton;
    juce::Array<VerdantProcessor::PresetInfo> debugExtras;
    juce::String message;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetBrowser)
};

} // namespace verdant::ui
