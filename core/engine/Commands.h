// Messages between the message thread and the audio thread.
#pragma once
#include "../model/Model.h"
#include "../seq/Scale.h"

namespace atm {

class Effect;
struct SampleData;

struct Cmd
{
    enum Type : uint16_t {
        Nop,
        SetTrackParam,      // a=track b=param f=value
        SetGlobalParam,     // b=param f=value
        SetStep,            // a=pattern b=track c=step, step
        SetTrackPattern,    // a=pattern b=track p=TrackPattern* (copied, returned)
        SetPatternMeta,     // a=pattern b=lengthBars
        SetTrackSound,      // a=track p=TrackSound* (copied, returned)
        SetTrackFlags,      // a=track b=mute c=solo
        SetProject,         // p=Project* (swapped in, old one returned)
        SetSample,          // a=track p=SampleData* (may be null; old one returned)
        SetFx,              // a=slot p=Effect* (may be null; old one returned)
        SetModSlot,         // a=index, mod
        SetTuning,          // p=Tuning* (copied, returned)
        SetSong,            // p=Song* (copied, returned)
        SelectPattern,      // a=pattern b=1 immediately / 0 at next bar
        SetFill,            // a=on
        SetBreak,           // a=on
        SetPlay,            // a=on (internal transport)
        SetEvolve,          // a=on
        SetSelectedTrack,   // a=track (which track's modulation is published)
        PreviewOn,          // a=track b=note offset (semitones) f=velocity
        PreviewOff,         // a=track
        AllNotesOff,
        SetProjectMeta,     // a=midiPatternCh, f unused, seed in u
    };
    uint16_t type = Nop;
    int16_t  a = 0, b = 0, c = 0;
    float    f = 0.f;
    uint32_t u = 0;
    void*    p = nullptr;
    Step     step;
    ModSlot  mod;
};

struct Garbage
{
    enum Kind : uint8_t { None, KTrackPattern, KTrackSound, KProject, KSample, KEffect, KTuning, KSong };
    uint8_t kind = None;
    void*   p = nullptr;
};

// Audio -> UI: a pattern track changed on the audio thread (evolve).
struct PatternChange
{
    int16_t pattern = 0, track = 0;
    TrackPattern data;
};

} // namespace atm
