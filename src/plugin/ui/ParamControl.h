// Verdant UI — base class for every control bound to one parameter of the table.
#pragma once

#include "Theme.h"

namespace verdant::ui
{

class ParamControl : public juce::Component
{
public:
    ParamControl (UiContext&, PID);
    ~ParamControl() override;

    const ParamDef& getDef() const noexcept                 { return def; }
    juce::RangedAudioParameter& getParameter() noexcept     { return param; }

    void setAccent (juce::Colour c)                         { accent = c; repaint(); }
    void setCaption (const juce::String& c)                 { caption = c; repaint(); }
    const juce::String& getCaption() const noexcept         { return caption; }

    float getPlainValue() const noexcept                    { return plain; }
    float getNormalisedValue() const noexcept               { return norm; }

    // Preferred cell width in design pixels (before scaling), used by the panel layout.
    virtual float getDesignWidth() const                    { return 58.0f; }

    // Called (message thread) after the parameter changed from anywhere.
    std::function<void()> onValueChange;

    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

protected:
    virtual void parameterValueChanged() {}

    // Caption / body / value rectangles shared by all cell-style controls, so knobs, pills
    // and switches in one row line up. The block is centred vertically in the component.
    struct Cell { juce::Rectangle<float> caption, body, value; };
    Cell cellLayout (bool macroStyle) const;

    void showHelp();

    UiContext& ctx;
    const ParamDef& def;
    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    juce::Colour accent { col::leaf };
    juce::String caption;
    float plain = 0.0f, norm = 0.0f;
    bool hovered = false;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParamControl)
};

} // namespace verdant::ui
