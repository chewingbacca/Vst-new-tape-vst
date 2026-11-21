// Custom-drawn parameter controls. Each one owns a juce::ParameterAttachment,
// so gestures, host automation and preset loads all go through the parameter
// and come back on the message thread without any polling.
#pragma once

#include "Theme.h"

namespace swoon::ui
{

//==============================================================================
class ParamControl : public juce::Component
{
public:
    ParamControl (juce::RangedAudioParameter&, const juce::String& label, juce::Colour accent);

    juce::RangedAudioParameter& getParameter() noexcept { return param; }
    bool isOn() const noexcept { return value > 0.5f; }

    /** Called (message thread) after the value changed, from the UI or from outside. */
    std::function<void()> onValueChange;

protected:
    virtual void parameterChanged() { repaint(); }

    float proportion() const { return param.convertTo0to1 (value); }
    juce::String valueText() const { return param.getCurrentValueAsText(); }
    void setHover (bool);

    /** On a right-click, shows the host's parameter menu if it has one. Returns true for right-clicks. */
    bool showHostMenu (const juce::MouseEvent&);

    void mouseEnter (const juce::MouseEvent&) override { setHover (true); }
    void mouseExit (const juce::MouseEvent&) override  { setHover (false); }

    juce::RangedAudioParameter& param;
    juce::String label;
    juce::Colour accent;
    float value = 0.0f;            // real (denormalised) parameter value
    bool hover = false;
    juce::ParameterAttachment attachment;
};

//==============================================================================
/** Rotary control: vertical drag, shift = fine, double-click = default, wheel. */
class Knob final : public ParamControl
{
public:
    enum class Style { macro, small };

    Knob (juce::RangedAudioParameter&, Style, const juce::String& label, juce::Colour accent);
    ~Knob() override;

    /** Shows a small mark at 12 o'clock (macros: 0.5 = "as the preset designed it"). */
    void setCentreDetent (bool shouldShow) { detent = shouldShow; repaint(); }

    /** Draws the value arc from the centre outwards. */
    void setBipolar (bool shouldBeBipolar) { bipolar = shouldBeBipolar; repaint(); }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void paintMacro (juce::Graphics&);
    void paintSmall (juce::Graphics&);

    Style style;
    bool detent = false, bipolar = false, dragging = false;
    double dragProportion = 0.0;
    float lastDragY = 0.0f;
};

//==============================================================================
/** One-cell stepped selector for Choice parameters: click the right/left part
    to step, drag or wheel to move through the list. */
class ChoiceSelector final : public ParamControl
{
public:
    ChoiceSelector (juce::RangedAudioParameter&, const juce::String& label, juce::Colour accent);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    juce::Rectangle<float> windowBounds() const;
    int index() const;
    void setIndex (int newIndex, bool wrap);

    juce::StringArray choices;
    bool dragged = false, overLeft = false;
    int dragStartIndex = 0;
    float wheelAccumulator = 0.0f;
};

//==============================================================================
/** Small labelled switch for Bool parameters. */
class Toggle final : public ParamControl
{
public:
    Toggle (juce::RangedAudioParameter&, const juce::String& label, juce::Colour accent);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
};

//==============================================================================
/** Round power button. With `inverted` the glyph is lit while the parameter is off (bypass). */
class PowerButton final : public ParamControl
{
public:
    PowerButton (juce::RangedAudioParameter&, juce::Colour accent, bool inverted = false);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    static void drawGlyph (juce::Graphics&, juce::Rectangle<float> area, juce::Colour colour, bool lit, float thickness);

private:
    bool inverted;
};

} // namespace swoon::ui
