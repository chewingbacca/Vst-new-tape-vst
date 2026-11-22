// Immutable sample data shared between the message thread (owner) and the
// audio thread (raw pointer reader). Built off the audio thread.
#pragma once
#include <vector>
#include <cstdint>

namespace atm {

struct SampleData
{
    int    id = 0;
    int    numChannels = 1;         // 1 or 2
    int    length = 0;              // frames at mip level 0
    double sampleRate = 44100.0;
    float  rootNote = 60.f;         // MIDI note at which the sample plays at its natural speed
    float  peak = 1.f;
    char   name[64] = "";

    // Mip level k holds the sample decimated by 2^k (half-band filtered each time).
    // Level 0 is the original. Every level has kGuard zero frames of padding on
    // both sides, so interpolators never need bounds checks: frame i of level k
    // is mip[k].ch[c][i + kGuard].
    static constexpr int kGuard = 8;
    static constexpr int kMaxMips = 5;
    struct Mip { std::vector<float> ch[2]; int length = 0; };
    Mip mip[kMaxMips];
    int numMips = 0;

    // Detected onsets (frame positions at level 0), ascending, first is 0.
    std::vector<int> transients;

    const float* data (int level, int channel) const
    {
        return mip[level].ch[channel < numChannels ? channel : 0].data() + kGuard;
    }
};

// Build a SampleData from interleaved-free channel data (copies). Computes mips,
// peak and transients. Not real-time safe (allocates).
SampleData* makeSampleData (const float* const* channels, int numChannels, int length, double sampleRate,
                            const char* name, float rootNote = 60.f);

} // namespace atm
