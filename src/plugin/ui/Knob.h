// Verdant UI — custom rotary control: arc + leaf pointer, glow proportional to value.
// Double-click the body to reset; double-click the value read-out to type a value.
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
    void resized() override;

    //==============================================================================
    // Typed value entry: an inline field in place of the read-out.
    void showValueEditor();
    void hideValueEditor (bool commit);
    bool isEditingValue() const noexcept    { return editing; }
    void setValueEditorText (const juce::String&);     // for tests / the snapshot tool

    // Applies text through the parameter's own text-to-value conversion as one complete
    // gesture. Returns false (and changes nothing) if the text cannot be understood.
    bool commitValueText (const juce::String&);

private:
    void setFromNormalised (float n, bool partOfGesture);
    void resetToDefault();
    juce::Rectangle<int> valueFieldBounds() const;
    bool isOverValue (juce::Point<float>) const;
    void styleValueEditor();

    Style style;
    bool bipolar = false, discrete = false;
    bool dragging = false;
    float dragNorm = 0.0f;
    juce::Point<float> lastDragPos;
    float wheelAccum = 0.0f;

    std::unique_ptr<juce::TextEditor> valueEditor;   // created on first use, then reused
    bool editing = false;
};

} // namespace verdant::ui
