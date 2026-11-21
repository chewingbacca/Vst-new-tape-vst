// The slim hint line along the bottom of the window, and the small interface
// through which whatever is under the mouse posts its one-line description.
// (A status line rather than tooltip windows: those misbehave in some hosts.)
#pragma once

#include "Theme.h"

namespace swoon::ui
{

/** Implemented by the editor. Components find it by walking up their parents. */
class HelpSink
{
public:
    virtual ~HelpSink() = default;

    /** title: what the thing is; text: one line about it; value: optional current value. */
    virtual void showHelp (const juce::String& title, const juce::String& text, const juce::String& value) = 0;
    virtual void clearHelp() = 0;
};

/** Posts a hint to the editor's status line (no-op if the component is not inside one). */
void postHelp (juce::Component& source, const juce::String& title, const juce::String& text, const juce::String& value = {});
void clearHelp (juce::Component& source);

/** Posts the parameter's name, description (swoonui::helpTextFor) and current value. */
void postParameterHelp (juce::Component& source, juce::RangedAudioParameter&);

//==============================================================================
class StatusLine final : public juce::Component
{
public:
    StatusLine();

    void setHint (const juce::String& title, const juce::String& text, const juce::String& value);

    /** Shown while nothing is hovered. */
    void setIdleText (const juce::String&);
    void clearHint();

    void paint (juce::Graphics&) override;

private:
    juce::String title, text, value, idle;
    juce::Font titleFont, textFont, valueFont;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StatusLine)
};

} // namespace swoon::ui
