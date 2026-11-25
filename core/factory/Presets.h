// Factory content: voice presets (one track's sound) and kit presets (whole projects).
#pragma once
#include "../model/Serialize.h"

namespace atm {

struct VoicePresetInfo { const char* name; const char* category; int engine; int role; };
int numVoicePresets();
const VoicePresetInfo& voicePresetInfo (int index);
int findVoicePreset (const char* name);                      // -1 if not found
bool loadVoicePreset (int index, VoicePresetData& out, SampleRefIO* samples);

// Put a voice preset on a track of a project: replaces the sound and the track's own modulation
// routings, keeps the track's mixer state (mute/solo/lock), MIDI settings and output routing.
void applyVoicePreset (Project& p, int track, const VoicePresetData& v);

struct KitPresetInfo { const char* name; int style; float bpm; };
int numKitPresets();
const KitPresetInfo& kitPresetInfo (int index);
bool buildKitPreset (int index, Project& out, SampleRefIO* samples);

} // namespace atm
