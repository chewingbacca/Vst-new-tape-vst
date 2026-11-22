// Project <-> JSON. Message-thread only.
#pragma once
#include "Model.h"
#include "Json.h"

namespace atm {

// Samples live outside the POD model; whoever owns them translates between a
// track's sampleId and a JSON description ({"factory":"name"} / {"file":"path", ...}).
struct SampleRefIO
{
    virtual ~SampleRefIO() = default;
    virtual Json describeSample (int sampleId) = 0;
    virtual int resolveSample (const Json& description) = 0;      // returns 0 if unavailable
};

constexpr int kProjectFormatVersion = 1;

Json stepToJson (const Step& s, int index);
void stepFromJson (const Json& j, Step& s);
Json trackPatternToJson (const TrackPattern& tp);
void trackPatternFromJson (const Json& j, TrackPattern& tp);
Json trackSoundToJson (const TrackSound& ts, SampleRefIO* samples);
void trackSoundFromJson (const Json& j, TrackSound& ts, SampleRefIO* samples);
Json modSlotToJson (const ModSlot& m, const Kit& kit);
bool modSlotFromJson (const Json& j, ModSlot& m);
Json tuningToJson (const Tuning& t);
void tuningFromJson (const Json& j, Tuning& t);

Json projectToJson (const Project& p, SampleRefIO* samples);
// Starts from an initialised project, so missing fields keep their defaults. Returns false if this is not an Automata project.
bool projectFromJson (const Json& j, Project& p, SampleRefIO* samples);

// A voice preset = one TrackSound plus its track-local modulation slots.
struct VoicePresetData
{
    TrackSound sound;
    ModSlot mods[8];
    int numMods = 0;
    char category[24] = "";
};
Json voicePresetToJson (const VoicePresetData& v, SampleRefIO* samples);
bool voicePresetFromJson (const Json& j, VoicePresetData& v, SampleRefIO* samples);

} // namespace atm
