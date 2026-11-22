#include "PluginProcessor.h"

AutomataProcessor::AutomataProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Main", juce::AudioChannelSet::stereo(), true)) {}

void AutomataProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AutomataProcessor(); }
