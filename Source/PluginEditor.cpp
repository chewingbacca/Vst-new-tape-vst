#include "PluginEditor.h"

SwoonAudioProcessorEditor::SwoonAudioProcessorEditor (SwoonAudioProcessor& p)
    : AudioProcessorEditor (&p), generic (p)
{
    addAndMakeVisible (generic);
    setSize (520, 640);
}

void SwoonAudioProcessorEditor::resized()
{
    generic.setBounds (getLocalBounds());
}
