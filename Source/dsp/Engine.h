// Swoon DSP engine: the whole signal path behind one small interface.
// No JUCE, no allocation after prepare(), safe to call from the audio thread.
#pragma once

#include "Params.h"

#include <memory>

namespace swoon
{

struct EngineMeters
{
    float inPeak = 0.0f;          // linear, post input gain
    float outPeakL = 0.0f;        // linear, post limiter
    float outPeakR = 0.0f;
    float limiterReductionDb = 0.0f;   // >= 0
};

class Engine
{
public:
    Engine();
    ~Engine();

    /** Allocates everything. Call before process() and whenever the sample rate changes. */
    void prepare (double sampleRate, int maxBlockSize);

    /** Clears all audio state (tails, envelopes) without reallocating. */
    void reset();

    /** Host tempo, used by the tempo-synced delay times. Defaults to 120 BPM. Audio thread. */
    void setTempo (double beatsPerMinute);

    /** Constant for a given sample rate; valid after prepare(). */
    int getLatencySamples() const;

    /** In-place stereo processing. numSamples may be any size, including 0. The result does not depend on
        how the audio is split into calls. Non-finite input samples are treated as silence. */
    void process (float* left, float* right, int numSamples, const ParamValues& params);

    /** Peak meters for the block most recently processed. */
    const EngineMeters& getMeters() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace swoon
