// Splinter — all pattern data (curves, sequences, slices) plus (de)serialisation.
#pragma once
#include "dsp/Model.h"
#include <juce_data_structures/juce_data_structures.h>

struct PatternBank
{
    splinter::Curve time[splinter::kNumSlots];
    splinter::Curve vol[splinter::kNumSlots];
    splinter::SeqPattern seq[splinter::kNumSlots];
    splinter::SliceSet slices;
    int sliceMode = 0;            // 0 transients, 1.. grid (1/4, 1/8, 1/16, 1/32), 5 manual
    float sliceSensitivity = 0.5f;

    PatternBank() { reset(); }
    void reset();                 // loads the standard curve library, empty sequences

    juce::ValueTree toTree (bool includeSlices) const;
    void fromTree (const juce::ValueTree& t, bool includeSlices);
};

namespace bankutil
{
// ---- curve builders (x in 0..1 across the pattern, y: time = fraction of range behind, volume = gain)
splinter::Curve flatTime();
splinter::Curve flatVol();
void addStairs (splinter::Curve& c, int steps, float stepHeight, float x0 = 0.0f, float x1 = 1.0f);
splinter::Curve gate (int cells, float duty, float floorLevel = 0.0f, float attack = 0.0f);
} // namespace bankutil
