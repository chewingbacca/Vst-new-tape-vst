// Splinter — owns the loop material. Decoding, analysis and tempo rendering all
// happen on a background thread; the audio thread only swaps pointers.
#pragma once
#include "dsp/DemoLoops.h"
#include "dsp/Stretch.h"
#include <juce_audio_utils/juce_audio_utils.h>

struct LoopInfo
{
    enum Kind { none = 0, demo, file, capture };
    Kind kind = none;
    juce::String name, filePath, error;
    int demoIndex = -1;
    double sourceBpm = 120.0, beats = 8.0, seconds = 0.0;
    int key = 0, scale = 1;
    float keyConfidence = 0.0f, percussiveness = 0.5f;
    uint32_t generation = 0;      // bumps whenever the material or its length changes
    bool loading = false;
};

class LoopManager final : private juce::Thread, private juce::AsyncUpdater
{
public:
    LoopManager();
    ~LoopManager() override;

    // ---- message thread
    void loadDemo (int index, bool keepSlices = false);
    void loadFile (const juce::File& file, bool keepSlices = false);
    void loadAudio (splinter::AudioData&& audio, const juce::String& name, double bpmHint, LoopInfo::Kind kind, bool keepSlices = false);
    void clear();
    void setBeats (double beats);                    // user override of the musical length
    LoopInfo getInfo() const;
    std::vector<float> getPeaks() const;             // min/max pairs across the loop
    splinter::SliceSet detectSlices (float sensitivity) const;
    static bool canLoad (const juce::File& f);

    // Called on the message thread after new material has been analysed.
    std::function<void (bool keepSlices)> onLoopChanged;

    void writeState (juce::ValueTree& parent) const;
    void readState (const juce::ValueTree& parent);

    // ---- audio thread (lock free)
    void setTarget (double sampleRate, double bpm, int stretchMode) noexcept
    {
        targetSr.store (sampleRate, std::memory_order_relaxed);
        targetBpm.store (bpm, std::memory_order_relaxed);
        targetMode.store (stretchMode, std::memory_order_relaxed);
    }
    // Returns the loop to use for this block (may be nullptr).
    const splinter::RenderedLoop* acquire() noexcept
    {
        if (auto* fresh = pending.exchange (nullptr, std::memory_order_acq_rel))
        {
            if (current != nullptr && ! retired.push (current)) leaked.store (current); // never free on the audio thread
            current = fresh;
        }
        if (clearRequest.exchange (false, std::memory_order_acq_rel) && current != nullptr)
        {
            if (! retired.push (current)) leaked.store (current);
            current = nullptr;
        }
        return current;
    }

    // Blocks (message thread / tests) until the loop is rendered for the target.
    bool waitUntilReady (int timeoutMs);
    bool isBusy() const { return busy.load(); }

private:
    struct Source
    {
        splinter::AudioData audio;
        splinter::LoopAnalysis analysis;
        LoopInfo info;
        std::vector<float> peaks;
        juce::MemoryBlock encoded;      // FLAC, for saving with the project (lazy)
    };
    struct Job
    {
        enum Type { tNone, tDemo, tFile, tAudio, tEncoded } type = tNone;
        int demoIndex = 0;
        juce::File file;
        splinter::AudioData audio;
        juce::MemoryBlock data;
        juce::String name, path;
        double bpmHint = 0.0, beatsHint = 0.0;
        LoopInfo::Kind kind = LoopInfo::none;
        bool keepSlices = false;
    };

    void run() override;
    void handleAsyncUpdate() override;
    void submit (Job&& j);
    bool runJob (Job& job);
    void renderIfNeeded();
    static bool decode (juce::InputStream* stream, const juce::String& hint, splinter::AudioData& out, juce::String& error);
    static void computePeaks (Source& s);
    static juce::MemoryBlock encodeFlac (const splinter::AudioData& a);

    mutable juce::CriticalSection lock;       // guards source / job / info (never taken on the audio thread)
    std::shared_ptr<Source> source;
    std::unique_ptr<Job> queuedJob;
    LoopInfo statusInfo;
    bool notifyKeepSlices = false;
    uint32_t generationCounter = 0;

    std::atomic<double> targetSr { 0.0 }, targetBpm { 120.0 };
    std::atomic<int> targetMode { 0 };
    std::atomic<splinter::RenderedLoop*> pending { nullptr };
    std::atomic<splinter::RenderedLoop*> leaked { nullptr };
    std::atomic<bool> clearRequest { false }, busy { false };
    std::atomic<uint32_t> passCount { 0 };
    splinter::RenderedLoop* current = nullptr;            // audio thread only
    splinter::SpscQueue<splinter::RenderedLoop*, 32> retired;

    // render bookkeeping (background thread)
    uint32_t renderedGen = 0; double renderedSr = 0.0, renderedBpm = 0.0, lastSeenBpm = 0.0; int renderedMode = -1;
};
