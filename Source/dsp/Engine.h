// Splinter DSP — the complete audio engine (JUCE-free).
#pragma once
#include "Harmonizer.h"
#include "Model.h"
#include "Reader.h"
#include "Sequencer.h"
#include "StepFx.h"

namespace splinter
{
struct EngineParams
{
    // source / global
    int sourceMode = 0;          // 0 loop, 1 live
    int liveBars = 2;
    int midiMode = 0;            // 0 slices, 1 patterns
    int sliceTrig = 0;           // 0 gate, 1 one-shot
    bool loopPlay = true;
    int quantize = 3;            // 0 off, 1 1/16, 2 1/8, 3 1/4, 4 bar
    float declickMs = 4.0f;
    int timeSlot = 0, volSlot = 0, seqSlot = 0;
    // macros
    float chop = 0.5f, stutter = 0.5f, warp = 0.5f, harmony = 0.0f, crush = 0.5f, chaos = 0.0f, mix = 1.0f;
    int seed = 1;
    float volDepth = 1.0f, volSmoothMs = 3.0f;
    // lanes
    int stutterDiv = 3; float stutterPitch = 0.0f, stutterGate = 1.0f, stutterDecay = 0.0f, stutterAccel = 0.0f; int stutterArp = 0;
    float tapeTime = 0.8f;
    int pitchDegrees = -2; float pitchGlide = 0.0f;
    float crushBits = 8.0f, crushRate = 8000.0f;
    int combDegree = 0, combOctave = 0; float combFeedback = 0.9f, combDamp = 6000.0f;
    int ringDegree = 0, ringOctave = 1;
    int filterShape = 1; float filterReso = 0.3f;
    float freezeMs = 80.0f;
    int delayDiv = 1; float delayFeedback = 0.5f, delayTone = 5000.0f, delayLevel = 0.7f, delayPingPong = 1.0f;
    float reverbTime = 2.2f, reverbDamp = 6000.0f, reverbLevel = 0.6f;
    // harmony
    int key = 0, scale = 1, harmMode = 0; // 0 diatonic (scale steps), 1 chromatic (semitones)
    float formant = 1.0f;
    int harmGate = 0;            // 0 always, 1 follows the Harmony lane
    bool voiceOn[kNumHarmVoices] = { true, true, false, false };
    int voiceInterval[kNumHarmVoices] = { 2, 4, 7, -7 };
    float voicePan[kNumHarmVoices] = { -0.5f, 0.5f, -0.2f, 0.2f };
    float voiceDetune[kNumHarmVoices] = { 0, 0, 0, 0 };
    float voiceLevel[kNumHarmVoices] = { 0.8f, 0.7f, 0.6f, 0.6f };
    // output
    float hpFreq = 20.0f, lpFreq = 20000.0f, filterQ = 0.7f, tiltDb = 0.0f, width = 1.0f, outGainDb = 0.0f;
    bool limiter = true, bypass = false;
    bool inputMonitor = false;
};

struct TransportInfo
{
    bool playing = false;
    double ppq = 0.0;
    double bpm = 120.0;
};

struct MidiEvent { int offset = 0; bool on = false; int note = 0; float velocity = 1.0f; bool allOff = false; };

inline double delayDivBeats (int i)
{
    static const double b[6] = { 0.25, 0.5, 0.75, 1.0, 1.0 / 3.0, 2.0 / 3.0 };
    return b[clampv (i, 0, 5)];
}
inline const char* delayDivName (int i)
{
    static const char* n[6] = { "1/16", "1/8", "1/8.", "1/4", "1/8T", "1/4T" };
    return n[clampv (i, 0, 5)];
}

// Read by the UI thread; written by the audio thread. All lock free.
struct VisualState
{
    std::atomic<double> beat { 0.0 };
    std::atomic<float> loopPos { 0.0f };            // beats within the loop (or beats behind, live)
    std::atomic<float> loopGain { 0.0f };
    std::atomic<float> voicePos[kMaxSliceVoices];   // beats within loop, <0 = idle
    std::atomic<float> laneLevel[kNumLanes];        // 0..1 activity right now
    std::atomic<int> laneVar[kNumLanes];
    std::atomic<int> timeSlot { 0 }, volSlot { 0 }, seqSlot { 0 };
    std::atomic<int> pendingTimeSlot { -1 }, pendingVolSlot { -1 }, pendingSeqSlot { -1 };
    std::atomic<float> timeOffsetBeats { 0.0f }, playRate { 1.0f }, volGain { 1.0f };
    std::atomic<int> pitchClass { -1 };
    std::atomic<float> harmLevel[kNumHarmVoices];
    std::atomic<float> harmSemis[kNumHarmVoices];
    std::atomic<float> peakL { 0.0f }, peakR { 0.0f }, gainReduction { 1.0f };
    std::atomic<float> inPeak { 0.0f };
    std::atomic<bool> playing { false };
    std::atomic<double> bpm { 120.0 };
    VisualState()
    {
        for (auto& v : voicePos) v.store (-1.0f);
        for (auto& v : laneLevel) v.store (0.0f);
        for (auto& v : laneVar) v.store (0);
        for (auto& v : harmLevel) v.store (0.0f);
        for (auto& v : harmSemis) v.store (0.0f);
    }
};

class Engine
{
public:
    Engine();
    void prepare (double sampleRate, int maxBlock);
    void reset();
    int latencySamples() const { return latency; }
    double getSampleRate() const { return sr; }

    // ---- data, set from the audio thread at block boundaries
    void setTimeCurve (int slot, const Curve& c) { timeCurves[clampv (slot, 0, kNumSlots - 1)] = c; }
    void setVolCurve (int slot, const Curve& c) { volCurves[clampv (slot, 0, kNumSlots - 1)] = c; }
    void setSeqPattern (int slot, const SeqPattern& p) { seqs[clampv (slot, 0, kNumSlots - 1)] = p; ++seqVersion; }
    void setSlices (const SliceSet& s) { loopSlices = s; ++seqVersion; }
    void setLoop (const RenderedLoop* l) { loop = l; }
    const RenderedLoop* getLoop() const { return loop; }
    const LiveRing& getLiveRing() const { return ring; }

    void process (const float* inL, const float* inR, float* outL, float* outR, int numSamples,
                  const TransportInfo& transport, const EngineParams& params,
                  const MidiEvent* midi, int numMidi);

    VisualState visual;

    // sample position (ring absolute index) at the start of a bar that ended most
    // recently, for capture. Returns false if not enough has been recorded.
    double currentBeat() const { return beat; }
    double currentBpm() const { return bpm; }

private:
    struct SliceVoice
    {
        bool active = false, releasing = false;
        int note = -1, slice = 0;
        double srcStart = 0.0;     // source beat where the slice begins
        double warpAtStart = 0.0;  // warped time at trigger
        double endLen = 0.0;       // one-shot length in beats (0 = until note off)
        float velocity = 1.0f;
        bool fresh = false;
        Reader reader;
        Fader gain;
    };
    struct HeldRun { bool down = false; double start = 0.0, end = -1.0e18; int var = 0; uint32_t rnd = 0; };

    void handleMidi (const MidiEvent& e, const EngineParams& p);
    void updateBlockParams (const EngineParams& p);
    const LaneRun& laneRun (int lane, double t, bool post);
    double warpTime (double t, float& gain, const EngineParams& p, bool allowFuture);
    double chopMap (double t, const EngineParams& p, bool live);
    const SliceSet& activeSlices (bool live) const { return live ? liveSlices : loopSlices; }
    static double quantBeats (int q)
    {
        static const double b[5] = { 0.0, 0.25, 0.5, 1.0, 4.0 };
        return b[clampv (q, 0, 4)];
    }

    double sr = 48000.0;
    int latency = 0, harmLatency = 2048, limLatency = 64;

    // data
    Curve timeCurves[kNumSlots], volCurves[kNumSlots];
    SeqPattern seqs[kNumSlots];
    SliceSet loopSlices, liveSlices;
    const RenderedLoop* loop = nullptr;
    uint32_t seqVersion = 1;

    // clock
    double beat = 0.0, bpm = 120.0;
    bool wasPlaying = false;
    std::vector<double> clockDelay;   // beat value per sample, delayed for the post stage
    std::vector<uint8_t> playDelay;
    int clockMask = 0, clockW = 0;

    // slots (warp stage clock / post stage clock)
    int curTimeSlot = 0, curSeqSlotWarp = 0, curSeqSlotPost = 0, curVolSlot = 0;
    int lastParamTimeSlot = -1, lastParamVolSlot = -1, lastParamSeqSlot = -1;
    int wantTimeSlot = 0, wantVolSlot = 0, wantSeqSlot = 0;

    SeqSettings seqWarp, seqPost;
    LaneEvaluator evalWarp[kNumLanes], evalPost[kNumLanes];
    HeldRun held[kNumLanes];
    LaneRun heldScratch;
    double chopCacheStart = -1.0e18; int chopCacheSlice = 0; uint32_t chopCacheVersion = 0;
    double freezeRunStart = -1.0e18;
    double beatInc = 0.0, spbNow = 24000.0;
    bool playingNow = false;
    uint32_t heldCounter = 0;
    float lastMacroKey[8] = {};

    // sources and readers
    LiveRing ring;
    Reader loopReader, dryReader;
    SliceVoice voices[kMaxSliceVoices];
    Fader loopGain, warpGain, dryGain;
    int lastChopSlice = -1;

    // harmony
    Harmonizer harm;
    DelayLine mainDelay[2], inDelay[2], loopDryDelay[2], bypassDelay[2];
    Fader harmEnv;
    float harmVoiceGain[kNumHarmVoices] = {};
    int harmUpdateCounter = 0;
    float harmIdleTime = 0.0f;

    // step fx
    Crusher crusher; CombResonator comb; RingMod ring_; SweepFilter sweep; GranularFreeze freeze;
    ThrowDelay delayFx; ThrowReverb reverbFx;
    Fader laneEnv[kNumLanes];
    bool freezeWasOn = false, delayTimeSet = false, reverbSet = false, firstBlock = true;
    int fxUpdateCounter = 0;
    uint32_t lastActiveMask = 0;

    // volume + output
    Smooth2 volSlew;
    Svf outHp[2], outLp[2];
    TiltEq tilt;
    Limiter limiter;
    LinearSmoother mixSm, widthSm, gainSm, volDepthSm, warpDepthSm, bypassSm, delayLevelSm, reverbLevelSm, limiterSm;
    float lastHp = -1, lastLp = -1, lastQ = -1, lastTilt = -99;
    float peakL = 0.0f, peakR = 0.0f, inPeakHold = 0.0f;
};
} // namespace splinter
