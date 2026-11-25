// Placeholder until the factory library lands.
#include "Presets.h"
#include "../gen/Idea.h"

namespace atm {

static const VoicePresetInfo kInit = { "Init Drum", "Init", Engine::Drum, Role::Perc };
int numVoicePresets() { return 1; }
const VoicePresetInfo& voicePresetInfo (int) { return kInit; }
int findVoicePreset (const char* name) { return std::strcmp (name, kInit.name) == 0 ? 0 : -1; }
bool loadVoicePreset (int, VoicePresetData& out, SampleRefIO*)
{
    out = VoicePresetData{};
    initTrackSound (out.sound, Engine::Drum);
    return true;
}

void applyVoicePreset (Project& p, int track, const VoicePresetData& v)
{
    if (track < 0 || track >= kTracks) return;
    TrackSound& dst = p.kit.tracks[track];
    const TrackSound keep = dst;
    dst = v.sound;
    dst.mute = keep.mute; dst.solo = keep.solo; dst.locked = keep.locked;
    dst.midiOut = keep.midiOut; dst.midiCh = keep.midiCh; dst.directOut = keep.directOut;
    p.kit.fxType[track * kInsertsPerTrack] = dst.fxType[0];
    p.kit.fxType[track * kInsertsPerTrack + 1] = dst.fxType[1];
    // drop the track's own routings, then add the preset's
    for (ModSlot& m : p.kit.mods)
        if (m.src != Src::None && m.dstTrack == track && (! srcIsPerTrack (m.src) || m.srcTrack == track)) m = ModSlot{};
    for (int i = 0; i < v.numMods; ++i)
        for (ModSlot& m : p.kit.mods)
            if (m.src == Src::None)
            {
                m = v.mods[i];
                m.srcTrack = (uint8_t) track; m.dstTrack = (int8_t) track;
                break;
            }
}

static const KitPresetInfo kInitKit = { "Init", Style::BrokenBeat, 140.f };
int numKitPresets() { return 1; }
const KitPresetInfo& kitPresetInfo (int) { return kInitKit; }
bool buildKitPreset (int, Project& out, SampleRefIO*) { initProject (out); return true; }

void styleTempoRange (int style, float& lo, float& hi)
{
    switch (style)
    {
        case Style::BrokenBeat:     lo = 85.f;  hi = 140.f; break;
        case Style::DrillBass:      lo = 150.f; hi = 190.f; break;
        case Style::AmbientMelodic: lo = 70.f;  hi = 110.f; break;
        case Style::ClicksCuts:     lo = 100.f; hi = 135.f; break;
        case Style::Acid:           lo = 118.f; hi = 145.f; break;
        default:                    lo = 80.f;  hi = 180.f; break;
    }
}

float generateIdea (Project& p, const IdeaOptions& o, SampleRefIO*)
{
    p.seed = o.seed; p.style = (uint8_t) o.style;
    return 0.5f * (o.tempoMin + o.tempoMax);
}

} // namespace atm
