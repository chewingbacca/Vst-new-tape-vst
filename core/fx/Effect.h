// Effect interface. One instance = one slot (stereo, in place, fully wet; the
// slot host applies the dry/wet mix).
//
// Real-time rules: prepare() may allocate; everything else runs on the audio
// thread and must not allocate, lock or block.
#pragma once
#include "../model/Params.h"
#include <memory>

namespace atm {

constexpr int kScSources = 10;      // 0 = self, 1..8 = tracks (pre-insert), 9 = external sidechain input

struct FxContext
{
    double sampleRate = 48000.0;
    double bpm = 120.0;
    double beat = 0.0;              // song position (quarter notes) at the start of this slice
    bool   playing = false;
    // Sidechain taps for this slice (same length as the slice). Entries may be null.
    const float* scL[kScSources] = {};
    const float* scR[kScSources] = {};
};

class Effect
{
public:
    virtual ~Effect() = default;
    virtual void prepare (double sampleRate) = 0;
    virtual void reset() = 0;
    // Control-rate update, called before every process() with kFxParams normalised values.
    virtual void setParams (const float* p, const FxContext& c) = 0;
    // Process n samples in place (1 <= n <= 32).
    virtual void process (float* l, float* r, int n, const FxContext& c) = 0;
    // Buffer-capture effects (stutter, reverse, tape stop) replace the signal when engaged and
    // pass it through untouched otherwise; they return false so the host skips the dry/wet blend
    // maths when the mix is at 100 %.
    virtual bool isTransparentWhenIdle() const { return false; }
};

// Returns nullptr for Fx::None or unknown types. Not real-time safe.
std::unique_ptr<Effect> createEffect (int fxType);

} // namespace atm
