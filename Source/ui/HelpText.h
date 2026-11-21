// One-line parameter descriptions for the status line.
#pragma once

#include "../dsp/Params.h"

#include <juce_audio_utils/juce_audio_utils.h>

namespace swoonui
{

/** A short description of a parameter. Ids without a hand-written entry get a
    generic line derived from the parameter table (kind, range, choices). */
juce::String helpTextFor (swoon::P);

} // namespace swoonui
