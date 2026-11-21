#pragma once

#include "PresetManager.h"
#include "VizData.h"
#include "dsp/Engine.h"
#include "dsp/Params.h"

#include <juce_audio_utils/juce_audio_utils.h>

class SwoonAudioProcessor final : public juce::AudioProcessor
{
public:
    SwoonAudioProcessor();
    ~SwoonAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    using AudioProcessor::processBlockBypassed;
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    //==============================================================================
    const juce::String getName() const override { return "Swoon"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // Interface for the editor
    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts; }
    PresetManager& getPresetManager() noexcept { return *presetManager; }
    const VizData& getVizData() const noexcept { return viz; }

    /** Returns the JUCE parameter for a table entry (never null). */
    juce::RangedAudioParameter& getParam (swoon::P p) const noexcept { return *rangedParams[static_cast<size_t> (p)]; }

    /** Editor-only persistent state (window size, current page...). Message thread. */
    juce::ValueTree getUiState();

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    void processInternal (juce::AudioBuffer<float>&, bool hostBypassed);
    void updateViz (const float* l, const float* r, int n);

    juce::AudioProcessorValueTreeState apvts;
    std::array<std::atomic<float>*, swoon::kNumParams> rawParams {};
    std::array<juce::RangedAudioParameter*, swoon::kNumParams> rangedParams {};
    juce::AudioProcessorParameter* bypassParam = nullptr;

    swoon::Engine engine;
    swoon::ParamValues paramValues;
    juce::AudioBuffer<float> stereoScratch;

    std::unique_ptr<PresetManager> presetManager;

    // visualisation
    VizData viz;
    struct VizState
    {
        float lp1 = 0.0f, lp2 = 0.0f;           // band-split one-poles
        float eLow = 0.0f, eMid = 0.0f, eHigh = 0.0f;
        float peakIn = 0.0f, peakL = 0.0f, peakR = 0.0f;
        float lowCoeff = 0.0f, highCoeff = 0.0f, envCoeff = 0.0f, peakDecay = 0.0f;
        int decim = 8, decimCount = 0;
        float accL = 0.0f, accR = 0.0f;
    } vizState;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SwoonAudioProcessor)
};
