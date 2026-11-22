#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "engine/Session.h"

// The plugin wrapper: buses, host parameters, transport, MIDI and state.
// All sound comes from atm::AudioEngine; all editing goes through atm::Session.
class AutomataProcessor : public juce::AudioProcessor,
                          private juce::Timer
{
public:
    AutomataProcessor();
    ~AutomataProcessor() override;

    // ---- AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;
    bool supportsDoublePrecisionProcessing() const override { return false; }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Automata"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- for the editor and offline tools
    atm::Session& session() { return *sess; }
    atm::AudioEngine& engine() { return *eng; }

    // Edits of parameters that the host can automate must go through these so the host sees them.
    void setGlobalParam (int gp, float norm);
    void setTrackParam (int track, int tp, float norm);
    void setTrackMute (int track, bool mute);
    void beginGesture (int trackOrMinus1, int param);
    void endGesture (int trackOrMinus1, int param);
    bool undo();
    bool redo();

    // Serialise / restore the whole project (same data as the plugin state).
    void saveState (juce::MemoryBlock& dest, bool embedSamples);
    bool loadState (const void* data, size_t size);
    bool saveProjectFile (const juce::File& f);
    bool loadProjectFile (const juce::File& f);

    // Load an audio file into the pool on a background thread, then assign it to a track.
    void loadSampleAsync (int track, const juce::File& file);
    // Synchronous decode (any thread except audio). Returns nullptr on failure.
    static atm::SampleData* decodeFile (const juce::File& file);

    // Message-thread housekeeping; the internal timer calls it, offline tools call it by hand.
    void housekeeping();
    float cpuLoad() const { return cpu.load (std::memory_order_relaxed); }

    juce::AudioFormatManager formats;

private:
    static BusesProperties makeBuses();
    void timerCallback() override { housekeeping(); }
    void syncHostParamsFromProject();
    juce::RangedAudioParameter* findHostParam (int kind, int a, int b) const;

    struct HostParam
    {
        enum Kind { Global, Track, Mute, Fill, Break, Evolve };
        juce::RangedAudioParameter* param = nullptr;
        int kind = Global, a = 0, b = 0;
        float lastAudio = -1.f;       // last value applied on the audio thread
        float lastMsg = -1.f;         // last value mirrored on the message thread
    };
    std::vector<HostParam> hostParams;

    std::unique_ptr<atm::AudioEngine> eng;
    std::unique_ptr<atm::Session> sess;
    juce::ThreadPool loaderPool { 1 };

    std::vector<atm::MidiMsg> midiIn, midiOut;
    juce::AudioBuffer<float> scratch;
    std::atomic<float> cpu { 0.f };
    std::atomic<bool> suppressHostSync { false };
    double currentSampleRate = 48000.0;

    JUCE_DECLARE_WEAK_REFERENCEABLE (AutomataProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutomataProcessor)
};
