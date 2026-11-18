// Verdant UI — custom toggles for Bool parameters.
#pragma once

#include "ParamControl.h"

namespace verdant::ui
{

class Toggle final : public ParamControl
{
public:
    enum class Style
    {
        power,      // bare switch, used in module headers
        labelled,   // caption above, switch, On/Off below (fits the knob grid)
        button      // rounded button with glyph + caption (Freeze, Bypass)
    };

    enum class Glyph { none, crystal, power };

    Toggle (UiContext&, PID, Style);

    void setGlyph (Glyph g)                 { glyph = g; repaint(); }
    bool isOn() const noexcept              { return plain >= 0.5f; }

    float getDesignWidth() const override   { return 62.0f; }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void drawSwitch (juce::Graphics&, juce::Rectangle<float> area) const;

    Style style;
    Glyph glyph = Glyph::none;
    bool pressed = false;
};

} // namespace verdant::ui
