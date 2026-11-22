// The whole instrument, JUCE-free: transport, sequencing, voices, modulation,
// effects and mixing. The plugin layer feeds it audio buffers, MIDI and
// transport info, and talks to it through lock-free command queues.
#pragma once
#include "Commands.h"
#include "../util/Fifo.h"
#include "../seq/SeqEngine.h"
#include "../voices/Voice.h"
#include "../fx/Effect.h"
#include "../fx/Glitcher.h"
#include "../mod/ModSources.h"
#include "../dsp/Envelope.h"
#include "../dsp/Limiter.h"
#include <atomic>
#include <memory>
#include <vector>

namespace atm {

struct TransportInfo
{
    bool   valid = false;        // host supplied position info
    bool   playing = false;
    double bpm = 120.0;
    double ppq = 0.0;            // position at the first sample of the block, in quarter notes
    double barStartPpq = 0.0;    // position of the start of the current bar
    int    tsNum = 4, tsDen = 4;
};

struct MidiMsg
{
    int     sample = 0;
    uint8_t data[3] = {};
};

struct ProcessIO
{
    int    numSamples = 0;
    float* main[2] = {};                        // required
    float* trackOut[kTracks][2] = {};           // optional direct outs (null if the bus is disabled)
    const float* sidechain[2] = {};             // optional
    const MidiMsg* midiIn = nullptr;
    int    numMidiIn = 0;
    MidiMsg* midiOut = nullptr;
    int    midiOutCapacity = 0;
    int    numMidiOut = 0;
    TransportInfo transport;
};

class AudioEngine
{
public:
    AudioEngine();
    ~AudioEngine();

    // ---- setup (not real-time)
    void prepare (double sampleRate, int maxBlockSize);
    double sampleRate() const { return sr; }

    // ---- audio thread
    void process (ProcessIO& io);
    // Direct parameter access for host automation (audio thread only).
    void setGlobalParamRt (int gp, float v) { if (gp >= 0 && gp < GP::count) rt->kit.gparams[gp] = clamp01 (v); }
    void setTrackParamRt (int t, int tp, float v) { if (t >= 0 && t < kTracks && tp >= 0 && tp < TP::count) rt->kit.tracks[t].params[tp] = clamp01 (v); }
    void setTrackMuteRt (int t, bool m) { if (t >= 0 && t < kTracks) rt->kit.tracks[t].mute = m ? 1 : 0; }
    void setFillRt (bool on) { hostFill = on; }
    void setBreakRt (bool on) { hostBreak = on; }
    void setEvolveRt (bool on) { rt->evolveOn = on ? 1 : 0; }

    // ---- message thread -> audio thread. Returns false if the queue is full.
    // Single producer: callers must serialise (the plugin layer holds a mutex).
    bool post (const Cmd& c) { return cmds.push (c); }
    size_t pendingCommands() const { return cmds.size(); }
    // ---- audio thread -> message thread
    bool popGarbage (Garbage& g) { return garbage.pop (g); }
    bool popPatternChange (PatternChange& c) { return changes.pop (c); }

    // When no audio callback is running (offline setup, tests) apply queued commands right away.
    // Must not be called while process() may be running.
    void drainCommandsOffline() { drainCommands(); }

    // ---- state published for the UI (read from any thread)
    struct Shared
    {
        std::atomic<int>    step[kTracks];           // index of the step most recently started, -1 when stopped
        std::atomic<uint32_t> trigCount[kTracks];    // increments on every note
        std::atomic<float>  trackPeak[kTracks];
        std::atomic<float>  mainPeak[2];
        std::atomic<float>  gainReduction { 1.f };
        std::atomic<int>    currentPattern { 0 };
        std::atomic<int>    queuedPattern { -1 };
        std::atomic<bool>   playing { false };
        std::atomic<bool>   fill { false }, brk { false };
        std::atomic<double> beat { 0.0 };
        std::atomic<float>  bpm { 120.f };
        std::atomic<int>    songEntry { -1 };
        std::atomic<float>  modValue[TP::count];     // live (modulated) params of the selected track
        std::atomic<float>  globalModValue[GP::count];
        std::atomic<float>  srcValue[Src::count];    // live source values for the selected track
        std::atomic<float>  cpuLoad { 0.f };
        std::atomic<uint32_t> glitchCount[kTracks];
    } shared;

    // Read-only view of the audio thread's project (tests / offline tools only).
    const Project& rtProject() const { return *rt; }

private:
    struct RtEvent
    {
        int      sample;
        uint8_t  type;       // 0 step start, 1 note on
        uint8_t  track;
        uint8_t  index;
        bool     slide;
        float    pitch, vel;
        int      gateSamples;
        uint32_t seed;
        uint32_t order;
        GlitchCmd glitch;
    };
    struct PendingOff { int samplesLeft; uint8_t ch, note; bool used; };

    struct TrackRt
    {
        Voice    voices[kVoicesPerTrack];
        uint64_t voiceClock = 0;
        int      voiceTag[kVoicesPerTrack] = { -1, -1, -1, -1 };   // MIDI/preview note holding the voice, -1 = sequencer
        float    lastNote = -1000.f;
        float    cur[TP::count] = {};           // final (locked + modulated) normalised params
        float    lockVal[TP::count] = {};
        bool     lockOn[TP::count] = {};
        bool     anyLock = false;
        int      gLockParam[kLockLanes]; float gLockVal[kLockLanes]; int numGLocks = 0;
        Lfo      lfo[2];
        float    stepModTarget = 0.f, stepModCur = 0.f;
        float    src[Src::count] = {};          // current per-track source values
        LinearSmooth gainL, gainR, sendA, sendB;
        Glitcher glitch;
        std::unique_ptr<Effect> fx[kInsertsPerTrack];
        LinearSmooth fxMix[kInsertsPerTrack];
        const SampleData* sample = nullptr;
        float    bufL[kMaxSlice], bufR[kMaxSlice];
        float    preL[kMaxSlice], preR[kMaxSlice];     // pre-insert tap for sidechains
        float    peak = 0.f;
        int      curStep = -1;
        bool     audible = true;
    };

    void drainCommands();
    void handleCommand (const Cmd& c);
    void trash (uint8_t kind, void* p);
    void rebuildModIndex();

    // sequencing
    void sequenceBlock (int numSamples, double b0, double bps, bool continuous);
    void sequenceSegment (double s0, double s1, double blockB0, double bps, int numSamples);
    void onBarLine (int64_t barIndex);
    SeqFrame frameForBar (int64_t barIndex);
    SeqMacros currentMacros() const;
    void pushEvent (const RtEvent& e);

    // event handling
    void handleStepStart (const RtEvent& e);
    void handleNoteOn (const RtEvent& e);
    void startVoice (int track, float note, float vel, int gateSamples, bool slide, uint32_t seed, int tag);
    void releaseTag (int track, int tag);
    void allNotesOff (bool hard);
    void clearLocks (TrackRt& tr);
    void handleMidiIn (const MidiMsg& m);
    void sendMidi (int sample, uint8_t status, uint8_t d1, uint8_t d2);
    void advancePendingOffs (int numSamples);

    // rendering
    void controlTick (int samplesUntilNext);
    void renderSlice (ProcessIO& io, int pos, int n);
    void computeTrackParams (int t);
    float sourceValue (const ModSlot& s) const;

    double sr = 48000.0;
    int    maxBlock = 512;

    std::unique_ptr<Project> rtOwner;
    Project* rt = nullptr;
    ScaleTable scale;

    SpscFifo<Cmd> cmds { 8192 };
    SpscFifo<Garbage> garbage { 8192 };
    SpscFifo<PatternChange> changes { 64 };

    TrackRt tracks[kTracks];
    std::unique_ptr<Effect> busFx[kSendBuses * kSendSlots + kMasterSlots];   // index = slot - kFirstSendSlot
    LinearSmooth busFxMix[kSendBuses * kSendSlots + kMasterSlots];
    float gcur[GP::count] = {};              // final global params (locks + modulation)
    Chaos chaos;
    EnvFollower follower;
    float gsrc[Src::count] = {};
    SoftLimiter limiter;
    LinearSmooth masterGain, retA, retB, limDrive;

    // mod matrix index: active slots grouped by destination track (kTracks = global)
    uint8_t modIndex[kTracks + 1][kModSlots];
    int     modCount[kTracks + 1] = {};

    // transport
    bool   wasPlaying = false;
    bool   internalPlay = false;
    double internalBeat = 0.0;
    double prevEndBeat = 0.0, prevRangeEnd = 0.0;
    double curBpm = 120.0, curBeat = 0.0, beatsPerSample = 0.0;
    double beatsPerBar = 4.0, barOffset = 0.0;
    bool   running = false;
    int64_t lastBarHandled = INT64_MIN;
    double frameValidFrom = -1.0e18;
    SeqFrame frame;
    bool   frameIsSong = false;
    int    pendingPattern = -1;
    bool   fillOn = false, breakOn = false, fillLatched = false, breakLatched = false;
    bool   hostFill = false, hostBreak = false, uiFill = false, uiBreak = false;
    int    selectedTrack = 0;

    // events of the current block
    static constexpr int kMaxEvents = 2048;
    std::vector<RtEvent> events;
    int    numEvents = 0;
    uint32_t eventOrder = 0;
    ProcessIO* curIO = nullptr;
    int    curSlicePos = 0;
    PendingOff pendingOffs[128];

    // control rate
    int    ctrlPhase = 0;
    uint32_t publishCounter = 0;

    // mix buffers (slice sized)
    float mixL[kMaxSlice], mixR[kMaxSlice];
    float sendL[kSendBuses][kMaxSlice], sendR[kSendBuses][kMaxSlice];
    float tmpL[kMaxSlice], tmpR[kMaxSlice];
    float zero[kMaxSlice] = {};
    float mainPeak[2] = {};
};

} // namespace atm
