#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "../dsp/Engine.h"
#include "../dsp/Params.h"
#include "../dsp/Presets.h"

class VerdantProcessor final : public juce::AudioProcessor
{
public:
    VerdantProcessor();
    ~VerdantProcessor() override = default;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Verdant"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 30.0; }

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // Presets (message thread).
    struct PresetInfo { juce::String name, category, description; bool factory = true; juce::File file; };

    juce::Array<PresetInfo> getPresetList() const;
    void loadPreset (const PresetInfo&);
    void loadFactoryPreset (int index);
    bool saveUserPreset (const juce::String& name);
    bool deleteUserPreset (const PresetInfo&);
    void applyValues (const verdant::ParamValues&);
    juce::String getCurrentPresetName() const { return currentPresetName; }
    static juce::File getUserPresetDirectory();

    //==============================================================================
    juce::AudioProcessorValueTreeState apvts;
    verdant::Engine engine;

    juce::RangedAudioParameter* getParam (verdant::PID pid) const { return params[size_t (pid)]; }

    // Editor size persisted with the session.
    int editorWidth = 0, editorHeight = 0, editorPage = 0;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    std::array<std::atomic<float>*, verdant::kNumParams> raw {};
    std::array<juce::RangedAudioParameter*, verdant::kNumParams> params {};
    juce::AudioParameterBool* bypassParam = nullptr;

    int currentProgram = 0;
    juce::String currentPresetName;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VerdantProcessor)
};
