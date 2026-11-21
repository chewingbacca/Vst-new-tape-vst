// Splinter UI — the big waveform: loop (or rolling live buffer), slice markers
// you can edit, playheads, and a flash for every glitch as it fires.
#pragma once
#include "Theme.h"
#include "plugin/PluginProcessor.h"

class WaveformView final : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    explicit WaveformView (SplinterProcessor&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hoverMarker = -1; repaint(); }
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
    void tick();                       // called by the editor timer

private:
    SplinterProcessor& proc;
    std::vector<float> peaks;          // min/max pairs
    LoopInfo info;
    splinter::SliceSet slices;
    int bankVersion = -1, hoverMarker = -1, dragMarker = -1;
    uint32_t peaksGeneration = 0xffffffff;
    bool live = false, dragOver = false;
    float playPos = 0.0f, loopGain = 0.0f, playRate = 1.0f;
    float voicePos[splinter::kMaxSliceVoices];
    float laneGlow[splinter::kNumLanes] = {};
    int laneVar[splinter::kNumLanes] = {};
    int liveCounter = 0, auditionNote = -1;
    double liveWindowBeats = 8.0;

    juce::Rectangle<float> waveArea() const;
    float beatToX (double beat) const;
    double xToBeat (float x) const;
    double loopBeats() const;
    int markerAt (juce::Point<float> p) const;
    void commitSlices();
    void refreshLivePeaks();
};
