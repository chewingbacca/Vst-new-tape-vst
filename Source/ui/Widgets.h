// Splinter UI — parameter-bound custom widgets.
#pragma once
#include "Theme.h"
#include <juce_audio_processors/juce_audio_processors.h>

// ---------------------------------------------------------------- rotary knob
class Knob final : public juce::Component, public juce::SettableTooltipClient
{
public:
    Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& label, juce::Colour colour, bool big = false);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    void setActivity (float a) { if (std::fabs (a - activity) > 0.01f) { activity = a; repaint(); } }   // 0..1 glow
    void setBipolar (bool b) { bipolar = b; }
    juce::RangedAudioParameter& getParameter() { return param; }

private:
    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    juce::String label;
    juce::Colour colour;
    bool big, hover = false, dragging = false, bipolar = false;
    float value = 0.0f, dragStart = 0.0f, activity = 0.0f;
    void setNormalised (float v);
};

// ---------------------------------------------------------------- choice (click cycles, right click / long list opens a menu)
class ChoiceBox final : public juce::Component, public juce::SettableTooltipClient
{
public:
    ChoiceBox (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& label, juce::Colour colour);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    std::function<juce::String (int)> textForIndex;      // optional override
    bool menuOnly = false;

private:
    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    juce::String label;
    juce::Colour colour;
    int index = 0, numItems = 1;
    bool hover = false;
    void setIndex (int i);
    juce::String itemText (int i) const;
};

// ---------------------------------------------------------------- toggle
class ToggleBox final : public juce::Component, public juce::SettableTooltipClient
{
public:
    ToggleBox (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& label, juce::Colour colour);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
private:
    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    juce::String label;
    juce::Colour colour;
    bool on = false, hover = false;
};

// ---------------------------------------------------------------- plain button
class TextBtn final : public juce::Component, public juce::SettableTooltipClient
{
public:
    TextBtn (const juce::String& text, juce::Colour colour) : label (text), colour (colour) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { down = true; repaint(); }
    void mouseUp (const juce::MouseEvent& e) override { down = false; repaint(); if (getLocalBounds().contains (e.getPosition()) && onClick) onClick(); }
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    void setText (const juce::String& t) { if (t != label) { label = t; repaint(); } }
    void setLit (bool b) { if (lit != b) { lit = b; repaint(); } }
    void setColour (juce::Colour c) { colour = c; repaint(); }
    std::function<void()> onClick;
    float fontSize = 11.0f;
private:
    juce::String label;
    juce::Colour colour;
    bool hover = false, down = false, lit = false;
};
