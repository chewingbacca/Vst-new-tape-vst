// "New Idea": builds a whole kit and pattern set from (style, seed, tempo range).
#pragma once
#include "../model/Serialize.h"

namespace atm {

struct IdeaOptions
{
    int      style = Style::BrokenBeat;
    uint32_t seed = 1;
    float    tempoMin = 80.f, tempoMax = 180.f;
    bool     keepLocked = true;       // tracks with TrackSound::locked keep their sound and patterns
};

// Rewrites the project in place (locked tracks untouched). Returns the tempo it was written for,
// which is also stored in the project's internal tempo parameter.
float generateIdea (Project& p, const IdeaOptions& o, SampleRefIO* samples);

// Typical tempo range of a style (used as the default range in the UI).
void styleTempoRange (int style, float& lo, float& hi);

} // namespace atm
