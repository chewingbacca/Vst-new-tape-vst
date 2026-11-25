#pragma once
#include "PluginProcessor.h"
#include "ui/UiContext.h"

namespace ui { class MainView; }

// The editor: a fixed-layout main view scaled to the window size.
class AutomataEditor : public juce::AudioProcessorEditor,
                       public juce::DragAndDropContainer,
                       public juce::FileDragAndDropTarget,
                       private juce::Timer
{
public:
    explicit AutomataEditor (AutomataProcessor&);
    ~AutomataEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    // offline snapshots
    static void applySnapshotOptions (AutomataProcessor&, const juce::StringArray& args);
    void configureForSnapshot (const juce::StringArray& args);
    void refreshNow();

    static constexpr int kBaseW = 1400, kBaseH = 860;

private:
    void timerCallback() override;

    AutomataProcessor& proc;
    ui::Ctx ctx;
    std::unique_ptr<ui::MainView> view;
    uint64_t lastRev = 0;
    bool fillKeyDown = false, breakKeyDown = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutomataEditor)
};
