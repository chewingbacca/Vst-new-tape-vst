// The project data model. Everything here is plain fixed-size data: no
// pointers, no heap. A TrackPattern or TrackSound can be memcpy'd to the audio
// thread, snapshotted for undo, and serialised field by field.
#pragma once
#include "Params.h"
#include <cstdint>
#include <cstring>
#include <type_traits>
#include "../util/Rng.h"

namespace atm {

constexpr int kMaxSteps   = 64;
constexpr int kPatterns   = 16;
constexpr int kLockLanes  = 12;
constexpr int kModSlots   = 64;
constexpr int kChainLen   = 64;
constexpr int kLfoDrawPts = 32;
constexpr int kMaxScaleNotes = 128;
constexpr int kVoicesPerTrack = 4;

// ---------------------------------------------------------------------------
namespace Cond {
enum : uint8_t {
    Always,
    Fill, NotFill, Pre, NotPre, First, NotFirst,
    AB_first,                       // A:B conditions follow, packed: for B in 2..8, A in 1..B
    // count of A:B = 2+3+...+8 = 35
    count = AB_first + 35
};
}
// Decode/encode A:B conditions. a in 1..b, b in 2..8.
inline bool condIsAB (uint8_t c) { return c >= Cond::AB_first && c < Cond::count; }
inline void condGetAB (uint8_t c, int& a, int& b)
{
    int i = c - Cond::AB_first;
    for (b = 2; b <= 8; ++b) { if (i < b) { a = i + 1; return; } i -= b; }
    a = 1; b = 2;
}
inline uint8_t condMakeAB (int a, int b)
{
    int i = 0;
    for (int bb = 2; bb < b; ++bb) i += bb;
    return (uint8_t) (Cond::AB_first + i + (a - 1));
}
void condName (uint8_t c, char* buf, int n);

namespace RatCurve { enum : uint8_t { Even, Accel, Decel, Bounce, count }; }
namespace Dir { enum : uint8_t { Forward, Reverse, PingPong, Random, Drunk, count }; }
const char* dirName (int d);
const char* ratCurveName (int c);

struct Step
{
    uint8_t on        = 0;
    uint8_t vel       = 100;      // 1..127
    int8_t  pitch     = 0;        // note offset in scale units (semitones, or scale degrees for Scala tunings)
    uint8_t prob      = 100;      // 0..100 %
    uint8_t cond      = Cond::Always;
    uint8_t slide     = 0;        // legato into this step: glide pitch, don't retrigger envelopes
    uint8_t ratCount  = 1;        // 1 = no ratchet, up to 16
    uint8_t ratCurve  = RatCurve::Even;
    float   micro     = 0.f;      // -0.5..+0.5 of a step
    float   len       = 1.f;      // gate length in steps (0.05..64)
    float   ratAmt    = 0.5f;     // curve intensity 0..1
    float   ratPitch  = 0.f;      // semitones across the roll (-24..24)
    float   ratVel    = 0.f;      // velocity ramp across the roll (-1 fade out .. +1 fade in)
    float   ratGate   = 0.8f;     // each hit's length as a fraction of its slot
};

struct LockLane
{
    uint16_t dest = kDestNone;    // TP index, or kDestGlobalBase + GP index
    uint64_t mask = 0;            // which steps carry a lock
    float    value[kMaxSteps] = {};
    bool used() const { return dest != kDestNone; }
    bool has (int step) const { return (mask >> step) & 1u; }
    void set (int step, float v) { value[step] = v; mask |= (uint64_t) 1 << step; }
    void clear (int step) { mask &= ~((uint64_t) 1 << step); }
};

namespace Gen { enum : uint8_t { Euclid, Cellular, Density, Markov, Mutate, count }; }
const char* genName (int g);
constexpr int kGenParams = 4;

struct GeneratorState
{
    uint8_t  type = Gen::Euclid;
    uint8_t  seedLocked = 0;
    uint32_t seed = 1;
    float    p[Gen::count][kGenParams] = {
        { 0.3f, 0.f, 0.3f, 0.2f },     // Euclid: pulses, rotation, accents, velocity variation
        { 0.35f, 0.3f, 0.f, 0.3f },    // Cellular: rule (0..255), initial density, generations, velocity from neighbourhood
        { 0.4f, 0.3f, 0.3f, 0.2f },    // Density: density, syncopation, clustering, ratchets
        { 0.5f, 0.5f, 0.3f, 0.f },     // Markov: adherence, pitch freedom, velocity freedom, -
        { 0.25f, 0.5f, 0.5f, 0.3f },   // Mutate: amount, rhythm weight, pitch weight, detail (ratchet/micro) weight
    };
};

struct TrackPattern
{
    uint8_t  length   = 16;       // 1..64
    uint8_t  dir      = Dir::Forward;
    uint16_t spNum    = 1;        // step length = spNum/spDen quarter notes (1/4 = a sixteenth)
    uint16_t spDen    = 4;
    float    swing    = -1.f;     // < 0: inherit the global swing; else 0.25..0.8
    uint8_t  groove   = 0;        // groove template index
    float    grooveAmt = 0.f;
    uint8_t  freeRun  = 0;        // 1: never reset (true polymeter); 0: restart every pattern cycle
    uint32_t seed     = 1;        // seeds probability, random directions and macro variations
    Step     steps[kMaxSteps];
    LockLane lanes[kLockLanes];
    float    stepMod[kMaxSteps] = {};   // -1..1
    GeneratorState gen;

    double stepBeats() const { return (double) spNum / (double) (spDen == 0 ? 1 : spDen); }
    int findLane (int dest) const { for (int i = 0; i < kLockLanes; ++i) if (lanes[i].dest == dest) return i; return -1; }
    int freeLane() const { for (int i = 0; i < kLockLanes; ++i) if (! lanes[i].used() || lanes[i].mask == 0) return i; return -1; }
    // returns false if no lane is available
    bool setLock (int step, int dest, float v)
    {
        int l = findLane (dest);
        if (l < 0) { l = freeLane(); if (l < 0) return false; lanes[l] = LockLane{}; lanes[l].dest = (uint16_t) dest; }
        lanes[l].set (step, v);
        return true;
    }
    void clearLock (int step, int dest)
    {
        const int l = findLane (dest);
        if (l >= 0) { lanes[l].clear (step); if (lanes[l].mask == 0) lanes[l].dest = kDestNone; }
    }
    bool getLock (int step, int dest, float& v) const
    {
        const int l = findLane (dest);
        if (l >= 0 && lanes[l].has (step)) { v = lanes[l].value[step]; return true; }
        return false;
    }
    bool stepHasLocks (int step) const
    {
        for (auto& l : lanes) if (l.used() && l.has (step)) return true;
        return false;
    }
    int countOn() const { int n = 0; for (int i = 0; i < length; ++i) n += steps[i].on ? 1 : 0; return n; }
};

struct Pattern
{
    uint8_t lengthBars = 1;       // 1..16: cycle length for non-free-running tracks, chaining and quantised switches
    char    name[20]   = "";
    TrackPattern tracks[kTracks];
};

// ---------------------------------------------------------------------------
namespace Role { enum : uint8_t { Kick, Snare, Hat, Perc, Bass, Lead, Pad, Fx, count }; }
const char* roleName (int r);

namespace Src {
enum : uint8_t {
    None,
    Lfo1, Lfo2, ModEnv, AmpEnv, StepMod, Velocity, NoteRandom, Note,     // per-track sources (need srcTrack)
    Macro1, Macro2, Macro3, Macro4, ChaosX, ChaosY, Follower,             // global sources
    count
};
}
const char* srcName (int s);
inline bool srcIsPerTrack (int s) { return s >= Src::Lfo1 && s <= Src::Note; }

struct ModSlot
{
    uint8_t  src      = Src::None;
    uint8_t  srcTrack = 0;        // for per-track sources
    int8_t   dstTrack = 0;        // 0..7, or -1 for a global destination
    uint16_t dstParam = 0;        // TP or GP index
    float    amount   = 0.f;      // -1..1 in the normalised domain
    bool active() const { return src != Src::None && amount != 0.f; }
};

struct TrackSound
{
    uint8_t engine   = Engine::Drum;
    uint8_t poly     = 0;         // 0 mono (retrigger cut), 1 poly (4 voices)
    uint8_t mute     = 0;
    uint8_t solo     = 0;
    uint8_t locked   = 0;         // protected from New Idea / Evolve
    uint8_t role     = Role::Perc;
    uint8_t scaleOn  = 0;         // quantise this track's pitches to the project scale
    uint8_t midiOut  = 0;         // emit notes on MIDI out
    uint8_t midiCh   = 1;         // 1..16
    uint8_t baseNote = 60;        // MIDI note for pitch 0
    uint8_t directOut = 0;        // 1: route to the track's own output bus instead of the main mix
    uint8_t fxType[kInsertsPerTrack] = { Fx::None, Fx::None };
    int32_t sampleId = 0;         // handle into the sample pool (0 = none)
    char    name[24] = "";
    float   params[TP::count];
    float   lfoDraw[2][kLfoDrawPts] = {};

    TrackParamContext ctx() const { return { engine, fxType[0], fxType[1] }; }
};

struct Tuning
{
    uint8_t root = 0;             // 0..11 (C..B), tonic of the scale
    uint8_t scale = 0;            // index into the built-in scales; 255 = custom (Scala)
    char    name[32] = "Chromatic";
    // Custom (Scala) data: cents of degrees 1..count (the last is the period, usually 1200).
    int32_t customCount = 0;
    float   customCents[kMaxScaleNotes] = {};
};

struct Kit
{
    TrackSound tracks[kTracks];
    float   gparams[GP::count];
    uint8_t fxType[kNumFxSlots] = {};   // send + master slots live here; insert entries mirror TrackSound::fxType
    ModSlot mods[kModSlots];
    Tuning  tuning;
};

struct SongEntry { uint8_t pattern = 0; uint8_t repeats = 1; };
struct Song
{
    uint8_t   songMode = 0;
    uint8_t   length = 0;
    SongEntry chain[kChainLen];
};

namespace Style { enum : uint8_t { BrokenBeat, DrillBass, AmbientMelodic, ClicksCuts, Acid, count }; }
const char* styleName (int s);
const char* styleKey (int s);

struct Project
{
    char     name[40] = "Init";
    uint32_t seed = 1;            // master seed (New Idea)
    uint8_t  style = Style::BrokenBeat;
    uint8_t  currentPattern = 0;
    uint8_t  evolveOn = 0;
    uint8_t  midiPatternCh = 16;  // MIDI channel whose notes select patterns (0 = off)
    Kit      kit;
    Pattern  patterns[kPatterns];
    Song     song;
};

static_assert (std::is_trivially_copyable_v<TrackPattern>);
static_assert (std::is_trivially_copyable_v<TrackSound>);
static_assert (std::is_trivially_copyable_v<Project>);
static_assert (std::is_trivially_destructible_v<Project> && std::is_trivially_destructible_v<Kit>);
// A Project is large (~0.8 MB). Never create one on the stack (thread stacks can be 512 KB on macOS, 1 MB on Windows):
// allocate it on the heap (std::make_unique<Project>()).

// Fill a TrackSound / Kit / Project with descriptor defaults.
void initTrackSound (TrackSound& s, int engine);
void setEngineDefaults (TrackSound& s, int engine);                // only E0..E15
void setFxDefaults (float* params /* kFxStride */, int fxType);
void initKit (Kit& k);
void initProject (Project& p);
// Change the effect in a slot, resetting that slot's params to the new effect's defaults.
void setFxType (Kit& k, int slot, int fxType);
inline uint8_t getFxType (const Kit& k, int slot)
{
    return fxSlotIsInsert (slot) ? k.tracks[slot / kInsertsPerTrack].fxType[slot % kInsertsPerTrack] : k.fxType[slot];
}

// Resolve a parameter key on a track: "a.<key>" / "b.<key>" address the insert effects, then the
// engine's own keys ("model", "decay" ...), then the common keys ("cutoff", "ampd", "e3" ...). -1 if unknown.
int findTrackParam (const TrackSound& s, const char* key);
// Set a parameter from text in physical units or a label ("kick", "1.2k", "-6"). Returns false if unknown/unparseable.
bool setTrackParamText (TrackSound& s, const char* key, const char* value);
bool setTrackParamPhys (TrackSound& s, const char* key, float physical);

// Speeds offered in the UI (step length as a rational number of quarter notes).
struct SpeedDef { const char* name; uint16_t num, den; };
constexpr int kNumSpeeds = 16;
const SpeedDef& speedDef (int i);
int speedIndex (uint16_t num, uint16_t den);   // nearest table entry

// Groove templates: per-16th timing offset (fraction of a step) and velocity scale.
struct GrooveDef { const char* name; float time[16]; float vel[16]; };
constexpr int kNumGrooves = 8;
const GrooveDef& grooveDef (int i);

} // namespace atm
