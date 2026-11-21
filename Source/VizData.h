// Lock-free audio -> UI bridge: peak/band meters plus a small decimated stereo
// scope FIFO the editor can drain on its timer. Written only by the audio
// thread, read only by the message thread.
#pragma once

#include <array>
#include <atomic>
#include <cstddef>

struct VizData
{
    // Smoothed levels, linear 0..1+ (peaks decay on the audio thread).
    std::atomic<float> inLevel { 0.0f };
    std::atomic<float> outLevelL { 0.0f };
    std::atomic<float> outLevelR { 0.0f };
    std::atomic<float> limiterReductionDb { 0.0f };

    // Rough spectral balance of the output (RMS of low / mid / high bands, linear).
    std::atomic<float> bandLow { 0.0f };
    std::atomic<float> bandMid { 0.0f };
    std::atomic<float> bandHigh { 0.0f };

    // Decimated stereo scope (about 6 kHz rate), single producer / single consumer.
    static constexpr int scopeSize = 2048;   // power of two
    std::array<float, scopeSize> scopeL {};
    std::array<float, scopeSize> scopeR {};
    std::atomic<int> scopeWrite { 0 };

    void pushScope (float l, float r) noexcept
    {
        const int w = scopeWrite.load (std::memory_order_relaxed);
        scopeL[static_cast<size_t> (w)] = l;
        scopeR[static_cast<size_t> (w)] = r;
        scopeWrite.store ((w + 1) & (scopeSize - 1), std::memory_order_release);
    }

    /** Copies the most recent `count` scope frames (oldest first). UI thread. */
    void readScope (float* destL, float* destR, int count) const noexcept
    {
        const int w = scopeWrite.load (std::memory_order_acquire);
        for (int i = 0; i < count; ++i)
        {
            const int idx = (w - count + i) & (scopeSize - 1);
            destL[i] = scopeL[static_cast<size_t> (idx)];
            destR[i] = scopeR[static_cast<size_t> (idx)];
        }
    }
};
