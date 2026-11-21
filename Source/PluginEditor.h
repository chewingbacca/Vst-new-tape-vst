// Swoon editor. Everything is laid out in base coordinates (1000 x 660) inside
// `content`, which is scaled as a whole to the window, so the UI is resolution
// independent and freely resizable at a fixed aspect ratio.
//
// Editor state kept in SwoonAudioProcessor::getUiState():
//   "page"   int  0 = sleeve (front page), 1 = machine (module pages)
//   "width"  int  editor width in pixels
//   "height" int  editor height in pixels
#pragma once

#include "PluginProcessor.h"
#include "ui/ChainModel.h"
#include "ui/Header.h"
#include "ui/MachinePage.h"
#include "ui/PresetBrowser.h"
#include "ui/SleevePage.h"
#include "ui/Theme.h"

class SwoonAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                        private juce::Timer,
                                        private juce::ChangeListener
{
public:
    explicit SwoonAudioProcessorEditor (SwoonAudioProcessor&);
    ~SwoonAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    int getPage() const noexcept { return page; }
    void setPage (int newPage);

    bool isBrowserOpen() const noexcept { return browser.isVisible(); }
    void setBrowserOpen (bool shouldBeOpen);

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void updateSleeveText();

    SwoonAudioProcessor& proc;

    juce::SharedResourcePointer<swoon::ui::Fonts> fonts;   // before anything that draws text
    swoon::ui::SwoonLookAndFeel lookAndFeel;
    swoon::ui::ChainModel chain;

    juce::Component content;
    swoon::ui::Header header;
    swoon::ui::SleevePage sleeve;
    swoon::ui::MachinePage machine;
    swoon::ui::PresetBrowser browser;

    int page = 0;
    bool constructed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SwoonAudioProcessorEditor)
};
