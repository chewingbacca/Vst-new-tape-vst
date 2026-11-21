// Header: wordmark, page switch, preset bar, meters and bypass.
#pragma once

#include "ParamControls.h"

class SwoonAudioProcessor;
class PresetManager;
struct VizData;

namespace swoon::ui
{

/** Slim input / output meters with a limiter gain-reduction bar. Repaints only when a bar moves. */
class MeterView final : public juce::Component
{
public:
    explicit MeterView (const VizData&);

    void tick();
    void paint (juce::Graphics&) override;

private:
    const VizData& viz;
    float in = 0.0f, outL = 0.0f, outR = 0.0f, reduction = 0.0f;   // all 0..1
    juce::Font labelFont;
};

//==============================================================================
class Header final : public juce::Component,
                     private juce::ChangeListener
{
public:
    explicit Header (SwoonAudioProcessor&);
    ~Header() override;

    std::function<void (int)> onPageSelected;
    std::function<void()> onPresetNameClicked;

    void setPage (int newPage);
    void setBrowserOpen (bool isOpen);

    /** 30 Hz from the editor: meters and the preset "modified" marker. */
    void tick();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    enum class Zone { none, tabSleeve, tabMachine, previous, next, name };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    Zone zoneAt (juce::Point<float>) const;
    void setHover (Zone);
    void postHoverHelp();
    juce::String positionText() const;      // "12 / 39", empty when the state matches no preset

    PresetManager& presets;
    MeterView meters;
    PowerButton bypass;

    juce::Rectangle<float> tabBounds[2], presetBounds;
    int page = 0;
    bool browserOpen = false, modified = false;
    Zone hover = Zone::none, pressed = Zone::none;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Header)
};

} // namespace swoon::ui
