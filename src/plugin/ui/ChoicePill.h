// Verdant UI — pill selector for Choice parameters, plus the in-editor list it opens.
#pragma once

#include "ParamControl.h"

namespace verdant::ui
{

class ChoicePill final : public ParamControl
{
public:
    ChoicePill (UiContext&, PID);

    int getNumChoices() const noexcept          { return def.numChoices; }
    juce::String getChoiceName (int i) const    { return juce::isPositiveAndBelow (i, def.numChoices) ? def.choices[i] : ""; }
    int getCurrentIndex() const noexcept        { return juce::jlimit (0, juce::jmax (0, def.numChoices - 1), juce::roundToInt (plain)); }
    void setIndex (int);
    juce::Colour getAccent() const noexcept     { return accent; }

    // Pill rectangle in this component's coordinates.
    juce::Rectangle<float> getPillBounds() const;

    float getDesignWidth() const override;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    void setPopupOpen (bool open)               { popupOpen = open; repaint(); }

private:
    int zoneAt (juce::Point<float>) const;   // -1 = previous, +1 = next, 0 = open list, 2 = outside

    int hoverZone = 2;
    bool popupOpen = false;
    float wheelAccum = 0.0f;
};

//==============================================================================
// A floating list of choices drawn inside the editor (no desktop window, no modal loop).
class ChoicePopup final : public juce::Component
{
public:
    explicit ChoicePopup (UiContext&);

    void show (ChoicePill&);
    void dismiss();
    bool isOpen() const noexcept    { return isVisible(); }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void resized() override;

private:
    void updateGeometry();
    int itemAt (juce::Point<float>) const;

    UiContext& ctx;
    juce::Component::SafePointer<ChoicePill> target;
    juce::Rectangle<float> list;
    float itemHeight = 24.0f;
    int hoverItem = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChoicePopup)
};

} // namespace verdant::ui
