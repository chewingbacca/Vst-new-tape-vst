// Splinter DSP — offline loop analysis: onsets, tempo / length, key.
// Runs on a background thread; allocates freely.
#pragma once
#include "FFT.h"
#include "Model.h"

namespace splinter
{
struct AudioData
{
    std::vector<float> ch[2];
    double sampleRate = 48000.0;
    int64_t length() const { return (int64_t) ch[0].size(); }
    void resize (int64_t n) { ch[0].assign ((size_t) n, 0.0f); ch[1].assign ((size_t) n, 0.0f); }
};

struct LoopAnalysis
{
    double bpm = 120.0;
    double beats = 8.0;
    std::vector<double> onsetTimes;      // seconds, ascending
    std::vector<float> onsetStrengths;   // 0..1 relative
    int key = 0, scale = 1;              // scale: 0 major, 1 minor (indices into scaleDef)
    float keyConfidence = 0.0f;
    float percussiveness = 0.5f;         // 0 tonal .. 1 noisy / drum like
};

// bpmHint > 0 forces the tempo (beats are derived from the length).
LoopAnalysis analyseLoop (const AudioData& audio, double bpmHint = 0.0);

// Builds slice markers from detected onsets. sensitivity 0..1 (more = more slices).
SliceSet slicesFromOnsets (const LoopAnalysis& a, double loopSeconds, float sensitivity);

// Band-limited resampling (windowed sinc). outLength samples are produced
// covering the whole input; when `periodic` the input is treated as a loop.
void resample (const std::vector<float>& in, std::vector<float>& out, int64_t outLength, bool periodic);
} // namespace splinter
