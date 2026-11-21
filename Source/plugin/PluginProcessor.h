#pragma once
#include "LoopManager.h"
#include "Parameters.h"
#include "PatternBank.h"
#include "Presets.h"

class SplinterProcessor final : public juce::AudioProcessor
{
public:
    SplinterProcessor();
    ~SplinterProcessor() override;

    // ---- AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Splinter"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- shared with the editor
    juce::AudioProcessorValueTreeState apvts;
    LoopManager loops;
    splinter::Engine engine;
    juce::MidiKeyboardState keyboardState;

    // Pattern data. Read / modify on the message thread through these helpers.
    PatternBank getBank() const;
    template <typename Fn> void editBank (Fn&& fn, uint64_t dirtyBits)
    {
        {
            const juce::SpinLock::ScopedLockType sl (bankLock);
            fn (bank);
        }
        dirty.fetch_or (dirtyBits, std::memory_order_release);
        bankVersion.fetch_add (1);
    }
    static uint64_t timeBit (int slot) { return 1ull << slot; }
    static uint64_t volBit (int slot) { return 1ull << (12 + slot); }
    static uint64_t seqBit (int slot) { return 1ull << (24 + slot); }
    static constexpr uint64_t slicesBit = 1ull << 36;
    static constexpr uint64_t allBits = (1ull << 37) - 1;
    int getBankVersion() const { return bankVersion.load(); }

    // Slices
    void reslice();                                   // from bank.sliceMode / sensitivity
    void setSliceMode (int mode, float sensitivity);

    // Loop / capture
    void captureLiveInput();                          // grab the last N bars of input as the loop
    void applyDetectedKey();
    void handleLoopChanged (bool keepSlices);         // message thread, after new material is analysed

    // Presets
    void loadFactoryPreset (int index);
    void loadPresetData (const PresetData& d);
    bool saveUserPreset (const juce::String& name);
    bool loadUserPreset (const juce::File& f);
    juce::String getPresetName() const { return presetName; }
    int getFactoryIndex() const { return factoryIndex; }
    void generatePattern (int slot, uint32_t seed);   // "generate new pattern" for unlocked lanes

    // Parameter helpers
    void setParam (const juce::String& id, float realValue);
    float getParam (const juce::String& id) const;

    // Internal transport (used when the host supplies none, e.g. standalone)
    std::atomic<bool> internalPlaying { true };
    std::atomic<double> internalBpm { 120.0 };
    std::atomic<bool> hostTransportSeen { false };

    // Momentary effect pads in the editor: bit n holds lane n
    std::atomic<uint32_t> uiHeldLanes { 0 };

    // Editor size persistence
    std::atomic<int> editorWidth { 1040 }, editorHeight { 680 }, editorPage { 0 };

private:
    juce::ValueTree buildState (bool forPreset) const;
    void restoreState (const juce::ValueTree& t, bool isPreset);
    void syncBankToEngine();

    ParamBridge bridge;
    juce::AudioParameterBool* bypassParam = nullptr;
    splinter::EngineParams engineParams;

    PatternBank bank;
    mutable juce::SpinLock bankLock;
    std::atomic<uint64_t> dirty { allBits };
    std::atomic<int> bankVersion { 0 };

    juce::String presetName { "Init" };
    int factoryIndex = -1;

    std::vector<splinter::MidiEvent> midiScratch;
    juce::AudioBuffer<float> scratch;
    double internalPpq = 0.0;
    bool forceBypass = false;
    uint32_t appliedHeldLanes = 0;
    double currentSampleRate = 48000.0;
    uint32_t handledGeneration = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SplinterProcessor)
};
