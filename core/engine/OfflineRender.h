// Offline rendering of a project through a private engine instance (used for
// drag-out audio export, factory breaks and tests). Not for the audio thread.
#pragma once
#include "AudioEngine.h"
#include <vector>

namespace atm {

struct OfflineOptions
{
    double sampleRate = 48000.0;
    double bpm = 140.0;
    double bars = 4.0;              // how much of the timeline to play (4/4 bars)
    double tailSeconds = 2.0;       // extra time after the transport stops, for tails
    double startBar = 0.0;
    int    soloTrack = -1;          // >= 0: only this track sounds
    bool   trimTail = true;         // cut trailing silence (below -80 dB) from the tail
};

// samples: kTracks entries (may be null). Output is stereo.
void renderOffline (const Project& project, const SampleData* const* samples, const OfflineOptions& o,
                    std::vector<float>& left, std::vector<float>& right);

struct ExportNote
{
    double beat = 0.0, lengthBeats = 0.25;
    int    track = 0;
    float  note = 60.f;             // MIDI note (may be fractional for microtonal scales)
    float  velocity = 1.f;
};

// The notes a pattern produces over `bars` bars, with the project's current macro settings.
void collectPatternNotes (const Project& project, int pattern, double bars, std::vector<ExportNote>& out);

} // namespace atm
