// Interface every sound engine implements. One instance = one voice.
//
// Real-time rules: prepare() may allocate; everything else is called on the
// audio thread and must not allocate, lock or block.
#pragma once
#include "../model/Params.h"
#include "../util/Rng.h"
#include "SampleData.h"
#include <memory>

namespace atm {

constexpr int kMaxSlice = 32;       // render() is never asked for more samples than this

struct EngineNote
{
    float    note     = 60.f;       // MIDI note, fractional; includes track tune/fine at note start
    float    velocity = 1.f;        // 0..1
    bool     legato   = false;      // slide: keep phases and internal envelopes running
    uint32_t seed     = 1;          // deterministic per-note random seed
};

class EngineBase
{
public:
    virtual ~EngineBase() = default;

    virtual void prepare (double sampleRate) = 0;
    // Silence immediately and clear all state.
    virtual void reset() = 0;
    // Called when the track's sample changes (may be nullptr). Only sample-based engines care.
    virtual void setSample (const SampleData*) {}

    // Start a note. `e` points at kEngineParams normalised (0..1) engine parameters, already
    // modulated and locked for this step. Map them with the engine's ParamDesc table.
    virtual void noteOn (const EngineNote& note, const float* e) = 0;
    // The step's gate ended.
    virtual void noteOff() {}

    // Control-rate update. Called before every render() with the current parameters and the
    // current pitch (MIDI note number including glide, tune and pitch modulation).
    virtual void control (const float* e, float note) = 0;

    // Render n samples (1 <= n <= kMaxSlice), overwriting outL (and outR if isStereo()).
    virtual void render (float* outL, float* outR, int n) = 0;

    // If false the voice copies outL to the right channel.
    virtual bool isStereo() const { return false; }
    // True once the engine can produce no more sound for this note (lets the voice free itself early).
    virtual bool finished() const { return false; }
};

std::unique_ptr<EngineBase> createEngine (int engineType);

} // namespace atm
