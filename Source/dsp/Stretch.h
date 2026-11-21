// Splinter DSP — offline time-stretch of a loop to a target tempo.
#pragma once
#include "Analysis.h"
#include "Reader.h"

namespace splinter
{
enum class StretchMode : int { Auto = 0, Tonal, Beats, Tape };

// Renders `src` (a loop of an.beats beats) so that it is exactly an.beats long
// at targetBpm / targetSampleRate. The loop is treated as periodic so the result
// loops seamlessly. Thread: background only.
void renderLoop (const AudioData& src, const LoopAnalysis& an, double targetSampleRate, double targetBpm,
                 StretchMode mode, RenderedLoop& out);

// Exposed for tests: stretch a periodic stereo signal to outLength samples.
// onsets are input sample positions of transients to keep sharp.
void stretchTonal (const std::vector<float> in[2], std::vector<float> out[2], int64_t outLength,
                   const std::vector<double>& onsets, double sampleRate);
void stretchBeats (const std::vector<float> in[2], std::vector<float> out[2], int64_t outLength,
                   const std::vector<double>& onsets, double sampleRate);
} // namespace splinter
