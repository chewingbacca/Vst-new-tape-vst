// Small inline text field for typing a parameter value. It lives inside the
// control it edits, commits on Return and cancels on Esc, focus loss or a click
// anywhere else. No dialogs, no modal state.
#pragma once

#include "Theme.h"

namespace swoon::ui
{

class ValueEntry final : public juce::TextEditor,
                         private juce::TextEditor::Listener
{
public:
    explicit ValueEntry (juce::Colour accent);
    ~ValueEntry() override;

    std::function<void (const juce::String&)> onCommit;
    std::function<void()> onDismiss;     // after a commit or a cancel

    /** Shows the field with `initialText` selected and takes the keyboard focus. */
    void begin (const juce::String& initialText);
    void cancel() { finish (false); }
    bool isActive() const noexcept { return isVisible(); }

    void paint (juce::Graphics&) override;

private:
    struct ClickAway final : public juce::MouseListener
    {
        explicit ClickAway (ValueEntry& o) : owner (o) {}
        void mouseDown (const juce::MouseEvent&) override;
        ValueEntry& owner;
    };

    void textEditorReturnKeyPressed (juce::TextEditor&) override { finish (true); }
    void textEditorEscapeKeyPressed (juce::TextEditor&) override { finish (false); }
    void textEditorFocusLost (juce::TextEditor&) override        { finish (false); }

    void finish (bool commit);
    void stopWatching();

    juce::Colour accent;
    ClickAway clickAway { *this };
    juce::Component::SafePointer<juce::Component> watched;
    bool finishing = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ValueEntry)
};

} // namespace swoon::ui
