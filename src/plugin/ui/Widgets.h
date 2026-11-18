// Verdant UI — small non-parameter widgets: icon/text buttons, page tabs, the help line.
#pragma once

#include "Theme.h"

namespace verdant::ui
{

//==============================================================================
class PillButton final : public juce::Component
{
public:
    enum class Icon { none, close, previous, next };

    PillButton (UiContext&, const juce::String& text, Icon icon = Icon::none);
    ~PillButton() override;

    void setHelp (const juce::String& title, const juce::String& help)  { helpTitle = title; helpText = help; }
    void setAccent (juce::Colour c)                                     { accent = c; repaint(); }
    void setText (const juce::String& t)                                { text = t; repaint(); }

    std::function<void()> onClick;

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void enablementChanged() override   { repaint(); }

private:
    UiContext& ctx;
    juce::String text, helpTitle, helpText;
    Icon icon;
    juce::Colour accent { col::leaf };
    bool hovered = false, pressed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PillButton)
};

//==============================================================================
class TabSwitch final : public juce::Component
{
public:
    TabSwitch (UiContext&, const juce::StringArray& names, const juce::StringArray& helpTexts);
    ~TabSwitch() override;

    void setSelected (int index, bool notify);
    int getSelected() const noexcept    { return selected; }

    std::function<void (int)> onChange;

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> tabBounds (int index) const;
    int tabAt (juce::Point<float>) const;

    UiContext& ctx;
    juce::StringArray names, help;
    int selected = 0, hoverTab = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TabSwitch)
};

//==============================================================================
// Bottom line: hovered control's name, live value and help text from the parameter table.
class StatusLine final : public juce::Component
{
public:
    explicit StatusLine (UiContext&);

    void setHover (juce::Component* owner, const juce::String& title, juce::RangedAudioParameter*, const juce::String& help);
    void clearHover (juce::Component* owner);
    void setIdleText (const juce::String&);
    void setRightText (const juce::String&);

    // Called from the editor timer: refreshes the live value text.
    void refresh();

    void paint (juce::Graphics&) override;

private:
    UiContext& ctx;
    juce::Component* owner = nullptr;
    juce::RangedAudioParameter* source = nullptr;
    juce::String title, valueText, helpText, idleText, rightText;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StatusLine)
};

} // namespace verdant::ui
