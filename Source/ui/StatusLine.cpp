#include "StatusLine.h"

#include "HelpText.h"

namespace swoon::ui
{

void postHelp (juce::Component& source, const juce::String& title, const juce::String& text, const juce::String& value)
{
    if (auto* sink = source.findParentComponentOfClass<HelpSink>())
        sink->showHelp (title, text, value);
}

void clearHelp (juce::Component& source)
{
    if (auto* sink = source.findParentComponentOfClass<HelpSink>())
        sink->clearHelp();
}

void postParameterHelp (juce::Component& source, juce::RangedAudioParameter& param)
{
    if (auto* sink = source.findParentComponentOfClass<HelpSink>())
        sink->showHelp (param.getName (64), swoonui::helpTextFor (paramFromId (param.paramID.toStdString())),
                        param.getCurrentValueAsText());
}

//==============================================================================
StatusLine::StatusLine()
    : titleFont (sansFont (11.4f, 1, 0.04f)),
      textFont (sansFont (11.4f, 0, 0.02f)),
      valueFont (sansFont (11.6f, 1, 0.03f))
{
    setInterceptsMouseClicks (false, false);
    setTitle ("Hint");
}

void StatusLine::setHint (const juce::String& newTitle, const juce::String& newText, const juce::String& newValue)
{
    if (title != newTitle || text != newText || value != newValue)
    {
        title = newTitle;
        text = newText;
        value = newValue;
        repaint();
    }
}

void StatusLine::setIdleText (const juce::String& newIdle)
{
    if (idle != newIdle)
    {
        idle = newIdle;
        repaint();
    }
}

void StatusLine::clearHint()
{
    setHint ({}, {}, {});
}

void StatusLine::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();

    g.setColour (col::ink);
    g.fillRect (b);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.fillRect (0.0f, 0.0f, b.getWidth(), 1.0f);

    auto area = b.withTrimmedLeft (22.0f).withTrimmedRight (30.0f).translated (0.0f, 0.5f);   // the resize grip lives on the right

    if (title.isEmpty() && text.isEmpty())
    {
        g.setColour (col::dusk);
        g.setFont (textFont);
        g.drawText (idle, area, juce::Justification::centredLeft, true);
        return;
    }

    if (value.isNotEmpty())
    {
        const float w = textWidth (valueFont, value) + 4.0f;
        g.setColour (col::fog);
        g.setFont (valueFont);
        g.drawText (value, area.removeFromRight (w), juce::Justification::centredRight, false);
        area.removeFromRight (16.0f);
    }

    if (title.isNotEmpty())
    {
        const float w = juce::jmin (area.getWidth(), textWidth (titleFont, title) + 2.0f);
        g.setColour (col::fog);
        g.setFont (titleFont);
        g.drawText (title, area.removeFromLeft (w), juce::Justification::centredLeft, true);

        if (text.isNotEmpty() && area.getWidth() > 30.0f)
        {
            // a short rule between name and description
            g.setColour (col::fog.withAlpha (0.25f));
            g.fillRect (area.getX() + 9.0f, area.getCentreY() - 0.5f, 12.0f, 1.0f);
            area.removeFromLeft (30.0f);
        }
    }

    if (text.isNotEmpty())
    {
        g.setColour (col::mist);
        g.setFont (textFont);
        g.drawText (text, area, juce::Justification::centredLeft, true);
    }
}

} // namespace swoon::ui
