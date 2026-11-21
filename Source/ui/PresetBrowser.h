// Preset browser overlay: category filter, list with name + note, inline
// "save as" field and delete for user presets. No native dialogs, no modal loops.
#pragma once

#include "Theme.h"

class PresetManager;

namespace swoon::ui
{

class PresetBrowser final : public juce::Component,
                            private juce::ChangeListener,
                            private juce::TextEditor::Listener
{
public:
    explicit PresetBrowser (PresetManager&);
    ~PresetBrowser() override;

    std::function<void()> onClose;

    /** Refreshes the lists, scrolls to the current preset and shows the overlay. */
    void open();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusOfChildComponentChanged (FocusChangeType) override;

private:
    enum class Zone { none, outside, panel, close, category, row, rowDelete, save, scrollbar };

    struct Hit
    {
        Zone zone = Zone::none;
        int index = -1;        // category index or row index
        bool operator== (const Hit& o) const noexcept { return zone == o.zone && index == o.index; }
        bool operator!= (const Hit& o) const noexcept { return ! (*this == o); }
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void textEditorTextChanged (juce::TextEditor&) override;
    void textEditorReturnKeyPressed (juce::TextEditor&) override;
    void textEditorEscapeKeyPressed (juce::TextEditor&) override;

    void refresh();
    void close();
    void saveCurrent();
    void stepSelection (int delta);
    void ensureRowVisible (int row);
    void setScroll (float newScroll);
    float maxScroll() const;
    juce::Rectangle<float> rowBounds (int row) const;
    juce::Rectangle<float> thumbBounds() const;
    Hit hitAt (juce::Point<float>) const;
    bool saveWouldReplace() const;
    void setStatus (const juce::String&, juce::Colour);

    void paintCategories (juce::Graphics&);
    void paintList (juce::Graphics&);
    void paintFooter (juce::Graphics&);

    PresetManager& presets;
    juce::TextEditor nameField;

    juce::StringArray categories;      // "All" first
    std::vector<int> categoryCounts;
    juce::String selectedCategory { "All" };
    std::vector<int> rows;             // entry indices after filtering

    juce::Rectangle<float> panel, closeBounds, categoryArea, listArea, fieldBounds, saveBounds;
    float scroll = 0.0f;
    Hit hover, pressed;
    int armedDeleteRow = -1;
    float thumbGrabOffset = 0.0f;

    juce::String status;
    juce::Colour statusColour;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetBrowser)
};

} // namespace swoon::ui
