#include "ValueEntry.h"

namespace swoon::ui
{

ValueEntry::ValueEntry (juce::Colour a) : accent (a)
{
    setMultiLine (false);
    setReturnKeyStartsNewLine (false);
    setPopupMenuEnabled (false);
    setScrollbarsShown (false);
    setInputRestrictions (16);
    setFont (sansFont (11.4f, 1, 0.02f));
    setJustification (juce::Justification::centred);
    setIndents (2, 0);
    setBorder (juce::BorderSize<int> (0));
    setTitle ("Value");
    addListener (this);
}

ValueEntry::~ValueEntry()
{
    stopWatching();
    removeListener (this);
}

void ValueEntry::begin (const juce::String& initialText)
{
    finishing = false;
    setText (initialText, false);
    setVisible (true);
    toFront (false);
    selectAll();

    // a click anywhere else in the editor window dismisses the field
    stopWatching();
    watched = getTopLevelComponent();
    if (watched != nullptr)
        watched->addMouseListener (&clickAway, true);

    if (isShowing())
        grabKeyboardFocus();
}

void ValueEntry::stopWatching()
{
    if (watched != nullptr)
        watched->removeMouseListener (&clickAway);

    watched = nullptr;
}

void ValueEntry::finish (bool commit)
{
    if (finishing || ! isVisible())
        return;

    finishing = true;                 // hiding drops the focus, which calls back in here
    const auto text = getText();

    stopWatching();
    setVisible (false);

    if (commit && onCommit != nullptr)
        onCommit (text);

    if (onDismiss != nullptr)
        onDismiss();

    finishing = false;
}

void ValueEntry::ClickAway::mouseDown (const juce::MouseEvent& e)
{
    if (e.eventComponent != &owner && ! owner.isParentOf (e.eventComponent))
        owner.finish (false);
}

void ValueEntry::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const float r = juce::jmin (8.0f, b.getHeight() * 0.5f);

    g.setColour (col::ink.withAlpha (0.94f));
    g.fillRoundedRectangle (b, r);
    g.setColour (accent.withAlpha (0.8f));
    g.drawRoundedRectangle (b.reduced (0.5f), r, 1.0f);

    juce::TextEditor::paint (g);
}

} // namespace swoon::ui
