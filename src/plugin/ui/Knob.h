// Verdant UI — custom rotary control: arc + leaf pointer, glow proportional to value.
#pragma once

#include "ParamControl.h"

namespace verdant::ui
{

class Knob final : public ParamControl
{
public:
    enum class Style { macro, compact };

    Knob (UiContext&, PID, Style);
    ~Knob() override;

    float getDesignWidth() const override   { return style == Style::macro ? 104.0f : 58.0f; }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void setFromNormalised (float n, bool partOfGesture);
    void resetToDefault();

    Style style;
    bool bipolar = false, discrete = false;
    bool dragging = false;
    float dragNorm = 0.0f;
    juce::Point<float> lastDragPos;
    float wheelAccum = 0.0f;
};

} // namespace verdant::ui
