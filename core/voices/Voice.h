// One voice: engine -> drive <-> filter -> amp, with amp and mod envelopes.
#pragma once
#include "EngineBase.h"
#include "../dsp/Filters.h"
#include "../dsp/Envelope.h"
#include "../dsp/Oversample.h"

namespace atm {

struct VoiceStart
{
    float    note = 60.f;          // MIDI note after scale mapping (track Tune/Fine are added by the voice)
    float    velocity = 1.f;       // 0..1
    bool     slide = false;
    uint32_t seed = 1;
    int      gateSamples = 0;      // > 0: release after this many samples; <= 0: hold until gateOff()
    float    glideFrom = -1000.f;  // previous note of the track (for glide); < -500 = none
};

class Voice
{
public:
    void prepare (double sampleRate);
    void setSample (const SampleData* s);
    void reset();

    // p = the track's current (modulated, locked) normalised parameters, TP::count entries.
    void start (const VoiceStart& s, int engineType, const float* p);
    // Legato retarget of a sounding voice: new pitch and gate, envelopes keep running.
    void slideTo (const VoiceStart& s, const float* p);
    void gateOff();
    void kill();                               // ~3 ms fade, then idle

    // Control tick: recompute envelopes, pitch glide and filter targets. Call before render().
    void control (const float* p, int samplesUntilNextControl);
    // Adds n samples (<= kMaxSlice) into outL/outR.
    void render (float* outL, float* outR, int n);

    bool active() const { return isActive; }
    bool sounding() const { return isActive && amp.stage != Envelope::Kill && amp.stage != Envelope::Release; }
    bool dying() const { return isActive && amp.stage == Envelope::Kill; }
    bool gateHeld() const { return isActive && gateOpen; }
    float level() const { return amp.level; }
    float modEnvLevel() const { return mod.level; }
    float ampEnvLevel() const { return amp.level; }
    float velocity() const { return vel; }
    float noteNow() const { return curNote; }
    float noteRandom() const { return noteRnd; }
    uint64_t age = 0;                          // set by the owner for voice stealing

private:
    void updateFilterTargets (const float* p);
    inline float shape (float x) const;

    double sr = 48000.0;
    std::unique_ptr<EngineBase> engines[Engine::count];
    EngineBase* eng = nullptr;
    int  engineType = -1;
    bool isActive = false, gateOpen = false, firstControl = false;
    int  gateLeft = 0;
    float vel = 1.f, accent = 0.f, noteRnd = 0.f;
    float targetNote = 60.f, curNote = 60.f, glideCoef = 1.f;
    Envelope amp, mod;

    // filter
    int   fltType = 0;
    float gCur = 0.1f, gTarget = 0.1f, gStep = 0.f, kRes = 1.f, ladRes = 0.f;
    Svf   svf[2][2];
    Ladder lad[2];
    // drive
    bool  driveOn = false; int drvType = 0; bool drvPost = false;
    float drvGain = 1.f, drvComp = 1.f;
    Oversampler2x os[2];
    DcBlocker dcb[2];
    float ampGain = 1.f;
    float bufL[kMaxSlice], bufR[kMaxSlice];
};

} // namespace atm
