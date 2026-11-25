// Drag-out export: the edited pattern as a MIDI file or as rendered audio.
#pragma once
#include "PluginProcessor.h"

namespace exporter {

// Both write into the system temp folder and return the file (invalid File on failure).
juce::File writePatternMidi (AutomataProcessor& proc, int pattern);
juce::File renderPatternAudio (AutomataProcessor& proc, int pattern, int soloTrack /* -1 = full mix */);

} // namespace exporter
