// Splinter UI — editor shell: top bar, waveform, macros, page tabs.
#pragma once
#include "Pages.h"
#include "WaveformView.h"

class SplinterEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SplinterEditor (SplinterProcessor&);
    ~SplinterEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void setPage (int index);
    void refreshNow() { timerCallback(); }      // one UI tick (used by the snapshot tool)

    static constexpr int baseW = 1040, baseH = 680;

private:
    // Everything lives in a fixed-size canvas that is scaled to the window.
    struct Canvas final : juce::Component
    {
        explicit Canvas (SplinterEditor& e) : owner (e) {}
        void paint (juce::Graphics& g) override { owner.paintCanvas (g); }
        void mouseDown (const juce::MouseEvent& e) override { owner.canvasMouseDown (e); }
        void mouseDrag (const juce::MouseEvent& e) override { owner.canvasMouseDrag (e); }
        SplinterEditor& owner;
    };

    void timerCallback() override;
    void paintCanvas (juce::Graphics&);
    void canvasMouseDown (const juce::MouseEvent&);
    void canvasMouseDrag (const juce::MouseEvent&);
    void layout();
    void showPresetMenu();
    void stepPreset (int delta);
    void beginSave();
    void showLoopMenu();
    void showSliceMenu();
    void refreshTexts();

    SplinterProcessor& proc;
    Canvas canvas;
    juce::TooltipWindow tooltips { this, 700 };
    WaveformView waveform;
    juce::OwnedArray<juce::Component> owned;
    Knob* macros[7] = {};
    TextBtn* tabs[5] = {};
    std::unique_ptr<Page> pages[5];
    int page = 0;

    TextBtn* presetPrev; TextBtn* presetNext; TextBtn* presetName; TextBtn* presetSave;
    ChoiceBox* source; ChoiceBox* key; ChoiceBox* scale;
    TextBtn* capture; TextBtn* play; TextBtn* sliceBtn; TextBtn* sensDown; TextBtn* sensUp; TextBtn* lenHalf; TextBtn* lenDouble; TextBtn* loopsBtn;
    juce::TextEditor nameEditor;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Rectangle<int> bpmArea, meterArea;
    double bpmDragStart = 120.0;
    float peakL = 0.0f, peakR = 0.0f, glow[splinter::kNumLanes] = {};
    juce::String lastPreset; int lastSliceMode = -1; float lastSens = -1.0f; bool lastHost = false, lastPlaying = false; double lastBpm = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SplinterEditor)
};
