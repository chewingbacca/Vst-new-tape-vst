// Verdant — the complete signal chain. JUCE-free; used by the plugin, the offline
// renderer and the unit tests.
//
//   in ─┬─────────────────────────────── dry (delayed by tape.nominalDelay) ──────────┐
//       └─ Tape ─┬───────────────┬─► bus ─► Chorus ─┬─────────────► wet ─► tone/width ─┴─► mix ─► limiter ─► out
//                ├─► Grain cloud ┘                  └─► Reverb ───┘
//                └─► Arp ──(direct)──► bus, ──(wash)──► reverb input
#pragma once

#include <atomic>

#include "Arp.h"
#include "Chorus.h"
#include "GrainCloud.h"
#include "Modulation.h"
#include "Output.h"
#include "Params.h"
#include "Reverb.h"
#include "Tape.h"
#include "Util.h"

namespace verdant
{

struct Transport
{
    double bpm     = 120.0;
    double ppq     = 0.0;    // position in quarter notes at the start of the block
    bool   playing = false;  // if false the engine free-runs its own beat clock
};

// Lock-free values published for the UI.
struct Meters
{
    std::atomic<float> inLevel { 0.0f }, outLevelL { 0.0f }, outLevelR { 0.0f };
    std::atomic<float> envelope { 0.0f }, wetLevel { 0.0f }, limiterGain { 1.0f };
    std::atomic<float> drift[4] { { 0.0f }, { 0.0f }, { 0.0f }, { 0.0f } };
    std::atomic<float> low { 0.0f }, mid { 0.0f }, high { 0.0f };
    std::atomic<float> grainActivity { 0.0f }, pitchHz { 0.0f };
    std::atomic<int>   arpPulse { 0 }, arpDegree { 0 }, grainPulse { 0 }, stutterPulse { 0 };
};

class Engine
{
public:
    static constexpr int kChunk = 32;

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    int  latencySamples() const { return tape.nominalDelay() + limiter.latency(); }

    // Latency the engine will report once prepared at this rate (lets the plugin announce a
    // sensible value before the host has told it the real sample rate).
    static int latencyForSampleRate (double sampleRate)
    {
        const float fs = float (sampleRate);
        return 4 + int (std::lround (0.003f * fs)) + std::max (8, int (std::lround (0.002f * fs))) - 1;
    }

    // Target parameter values (plain units). Call before process(); cheap.
    void setParams (const ParamValues& p) { target = p; }

    // In-place stereo processing. Any n >= 0.
    void process (float* left, float* right, int n, const Transport& transport);

    Meters meters;

private:
    void controlTick();
    void processChunk (float* l, float* r, int n);

    float sr = 48000.0f, tickRate = 1500.0f;
    int chunkPos = 0;
    bool firstBlock = true;

    ParamValues target;

    // ---- beat clock
    double beat = 0.0, beatInc = 0.0, samplesPerBeat = 24000.0;
    double freeBase = 0.0, freeInc = 0.0;       // free-running clock: base + samples * inc
    int64_t freeSamples = 0;

    // ---- modulation
    DriftLfo drift[4];
    Follower follower;
    float driftVal[4] = { 0, 0, 0, 0 };

    // ---- smoothed macro / global controls
    Smooth sBloom, sGrowth, sMoisture, sSunlight, sRoots, sMix, sBypass, sOutGain, sWidth, sDuck;
    Ramp rDry, rWet, rBypass, rInGate, rWidth, rDuck, rGrainMix, rArpDirect, rArpSend;
    Smooth sGrainMix, sArpMix, sArpWash, sRevMix, sRevOn, sTilt, sDrift;
    Ramp rRevDry, rRevWet, rRevOff;

    // ---- modules
    Tape tape;
    GrainCloud grain;
    Arp arp;
    Chorus chorus;
    Reverb reverb;
    OutputTone tone;
    Limiter limiter;

    // ---- delays for the dry and bypass paths
    Ring dryDelay[2], bypassDelay[2];

    // ---- metering
    float meterIn = 0.0f, meterOutL = 0.0f, meterOutR = 0.0f, meterWet = 0.0f;
    OnePoleLP bandLo, bandHi;
    OnePoleHP ambHp[4];   // keeps grain / arp envelopes from thumping below ~70 Hz
    float eLow = 0.0f, eMid = 0.0f, eHigh = 0.0f;
    float duckEnv = 0.0f, duckAtk = 0.0f, duckRel = 0.0f;
    float minLimiterGain = 1.0f;

public:
    // Smallest limiter gain since the last call (for tests / tuning); resets the tracker.
    float takeMinLimiterGain() { const float g = minLimiterGain; minLimiterGain = 1.0f; return g; }
};

} // namespace verdant
