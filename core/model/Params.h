// Parameter ids and descriptors.
//
// Every numeric setting is stored normalised (0..1). A ParamDesc says how that
// maps to physical units. Parameter locks and modulation both work on the
// normalised value, so anything described here can be locked or modulated.
#pragma once
#include "../util/Math.h"

namespace atm {

constexpr int kTracks       = 8;
constexpr int kEngineParams = 16;
constexpr int kFxParams     = 8;     // per effect, plus one mix
constexpr int kFxStride     = kFxParams + 1;
constexpr int kInsertsPerTrack = 2;
constexpr int kSendBuses    = 2;
constexpr int kSendSlots    = 2;     // effects per send bus
constexpr int kMasterSlots  = 4;
constexpr int kNumFxSlots   = kTracks * kInsertsPerTrack + kSendBuses * kSendSlots + kMasterSlots; // 24

// ---------------------------------------------------------------------------
// Per-track parameters
namespace TP {
enum : int {
    Level, Pan, Tune, Fine, Glide,
    E0, E1, E2, E3, E4, E5, E6, E7, E8, E9, E10, E11, E12, E13, E14, E15,
    AmpA, AmpH, AmpD, AmpS, AmpR, AmpVel,
    ModA, ModD, ModS, ModR,
    FltType, FltCut, FltRes, FltEnv, FltKey,
    DrvAmt, DrvType, DrvPos,
    SendA, SendB,
    FxA0, FxA1, FxA2, FxA3, FxA4, FxA5, FxA6, FxA7, FxAMix,
    FxB0, FxB1, FxB2, FxB3, FxB4, FxB5, FxB6, FxB7, FxBMix,
    Lfo1Rate, Lfo1Shape, Lfo1Sync, Lfo1Phase, Lfo1Mode,
    Lfo2Rate, Lfo2Shape, Lfo2Sync, Lfo2Phase, Lfo2Mode,
    StepGlide, GlitchSens, Accent,
    count
};
}

// ---------------------------------------------------------------------------
// Global parameters
namespace GP {
enum : int {
    Master,
    Density, Complexity, Mutation, Chaos, Glitch, Space,      // generative macros
    Macro1, Macro2, Macro3, Macro4,                           // mod-source macros
    Swing, Tempo,
    ChaosRate, ChaosType, ChaosShape,
    FollowAtk, FollowRel, FollowGain,
    EvolveRate, EvolveAmt,
    FillAmt, BreakAmt,
    SendAReturn, SendBReturn,
    LimitOn, LimitDrive,
    SendFx0,                                                  // 4 slots x kFxStride: A0, A1, B0, B1
    MasterFx0 = SendFx0 + kSendBuses * kSendSlots * kFxStride, // 4 slots x kFxStride
    count = MasterFx0 + kMasterSlots * kFxStride
};
}

// A destination for locks / modulation: own-track param, or global param.
constexpr int kDestGlobalBase = 0x200;
constexpr int kDestNone       = 0xFFFF;
inline bool destIsGlobal (int d) { return d >= kDestGlobalBase && d != kDestNone; }
inline int  destGlobalIndex (int d) { return d - kDestGlobalBase; }

// FX slot numbering: [0,16) track inserts (track*2 + n), [16,20) sends (A0 A1 B0 B1), [20,24) master.
constexpr int kFirstSendSlot   = kTracks * kInsertsPerTrack;
constexpr int kFirstMasterSlot = kFirstSendSlot + kSendBuses * kSendSlots;
inline bool fxSlotIsInsert (int s) { return s < kFirstSendSlot; }
inline bool fxSlotIsSend (int s)   { return s >= kFirstSendSlot && s < kFirstMasterSlot; }
inline bool fxSlotIsMaster (int s) { return s >= kFirstMasterSlot; }
// For an insert slot: the first TP index of its params. For send/master: first GP index.
inline int fxSlotParamBase (int s)
{
    if (fxSlotIsInsert (s)) return (s % kInsertsPerTrack) == 0 ? (int) TP::FxA0 : (int) TP::FxB0;
    if (fxSlotIsSend (s))   return GP::SendFx0 + (s - kFirstSendSlot) * kFxStride;
    return GP::MasterFx0 + (s - kFirstMasterSlot) * kFxStride;
}
inline int fxSlotTrack (int s) { return fxSlotIsInsert (s) ? s / kInsertsPerTrack : -1; }

// ---------------------------------------------------------------------------
struct ParamDesc
{
    enum Curve : uint8_t {
        Lin,        // min + (max-min) * x
        Exp,        // min * (max/min)^x        (min > 0)
        Pow2,       // min + (max-min) * x^2
        Pow3,       // min + (max-min) * x^3
        Discrete    // integer 0..(numLabels-1) or min..max in whole steps
    };

    const char* key   = "";       // serialisation / preset key
    const char* name  = "";       // short display name (<= 8 chars is best)
    float min = 0, max = 1, def = 0;   // physical
    Curve curve = Lin;
    const char* unit = "";
    const char* const* labels = nullptr;  // for Discrete with names
    int numLabels = 0;
    const char* help = "";

    int numSteps() const { return curve == Discrete ? (int) std::lround (max - min) + 1 : 0; }

    float toPhys (float n) const
    {
        n = clamp01 (n);
        switch (curve)
        {
            case Lin:  return min + (max - min) * n;
            case Exp:  return min * std::pow (max / min, n);
            case Pow2: return min + (max - min) * n * n;
            case Pow3: return min + (max - min) * n * n * n;
            case Discrete:
            {
                const int steps = numSteps();
                return min + (float) std::min ((int) (n * (float) steps), steps - 1);
            }
        }
        return n;
    }

    float toNorm (float p) const
    {
        p = clamp (p, std::min (min, max), std::max (min, max));
        switch (curve)
        {
            case Lin:  return max == min ? 0.f : (p - min) / (max - min);
            case Exp:  return std::log (p / min) / std::log (max / min);
            case Pow2: return max == min ? 0.f : std::sqrt ((p - min) / (max - min));
            case Pow3: return max == min ? 0.f : std::cbrt ((p - min) / (max - min));
            case Discrete:
            {
                const int steps = numSteps();
                return ((p - min) + 0.5f) / (float) steps;
            }
        }
        return p;
    }

    int toIndex (float n) const { return (int) std::lround (toPhys (n) - min); }
    float defNorm() const { return toNorm (def); }
    bool isDiscrete() const { return curve == Discrete; }
    bool isBipolar() const { return curve == Lin && min < 0 && max > 0 && std::fabs (min + max) < 1e-6f; }
};

// Text formatting of a normalised value ("1.20 kHz", "Saw", "-6.0 dB").  buf >= 24 chars.
void formatParam (const ParamDesc& d, float norm, char* buf, int bufSize);
// Parse "440", "440hz", "1.2k", "saw" (label) -> normalised. Returns false if unparseable.
bool parseParam (const ParamDesc& d, const char* text, float& normOut);

// ---------------------------------------------------------------------------
// Engines
namespace Engine {
enum : int { Drum, FM, Sampler, Granular, Resonator, Synth, count };
}
const char* engineName (int engine);          // "Drum", "FM" ...
const char* engineKey (int engine);           // "drum", "fm" ...
int engineFromKey (const char* key);          // -1 if unknown
const ParamDesc* engineParamDescs (int engine);   // kEngineParams entries (unused slots have empty key)

// Effects
namespace Fx {
enum : int {
    None, Stutter, Reverse, TapeStop, Crush, Distort, Comb, Resonator, FreqShift, RingMod,
    Spectral, GrainDelay, Delay, Plate, Cloud, Compressor, Filter, count
};
}
const char* fxName (int type);
const char* fxKey (int type);
int fxFromKey (const char* key);
const ParamDesc* fxParamDescs (int type);         // kFxParams entries
const ParamDesc& fxMixDesc();

// Descriptor lookup for the two id spaces. Engine and fx type are needed because
// the E* and Fx* slots change meaning with the selected engine / effect.
struct TrackParamContext { int engine = 0; int fxA = 0; int fxB = 0; };
const ParamDesc& trackParamDesc (int tp, const TrackParamContext& ctx);
// For params that do not depend on the engine / effect selection.
const ParamDesc& commonTrackParamDesc (int tp);
const ParamDesc& globalParamDesc (int gp, const uint8_t* fxTypes /* kNumFxSlots, may be null */);
// Stable serialisation keys, independent of engine ("e3", "fxa2", "cutoff" ...)
const char* trackParamKey (int tp);
const char* globalParamKey (int gp);
int trackParamFromKey (const char* key);      // -1 if unknown
int globalParamFromKey (const char* key);
// Is this track param one that should never be smoothed (switches, types)?
bool trackParamIsStepped (int tp, const TrackParamContext& ctx);

// Tempo-sync division table used by LFOs and synced effects.
struct SyncDiv { const char* name; float beats; };
constexpr int kNumSyncDivs = 22;
const SyncDiv& syncDiv (int index);
int syncDivFromNorm (float norm);

} // namespace atm
