// Renders the plugin editor to PNG files without a host, for visual review.
//
//   swoon_ui_snapshot <out.png> [--size WxH] [--scale S] [--preset N] [--ui key=value]... [--frames N]
//
// --ui sets a property on the processor's UI state tree before the editor is
// created (e.g. --ui page=1). Audio is pumped through the processor before the
// capture so meters and the visualiser have something to show.
#include "../Source/PluginProcessor.h"

#include <cmath>
#include <cstdio>

namespace
{
void pumpAudio (SwoonAudioProcessor& proc, int blocks)
{
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    static double phase = 0.0;

    for (int b = 0; b < blocks; ++b)
    {
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            // a soft two-note drone with slow amplitude movement
            const double t = phase / 48000.0;
            const float env = 0.25f + 0.2f * (float) std::sin (t * 2.3);
            const float s = env * (float) (std::sin (t * 6.2831853 * 164.8) + 0.6 * std::sin (t * 6.2831853 * 247.0)
                                           + 0.3 * std::sin (t * 6.2831853 * 330.5));
            buffer.setSample (0, i, s);
            buffer.setSample (1, i, s);
            phase += 1.0;
        }
        proc.processBlock (buffer, midi);
    }
}

void pumpMessages (int ms)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
    while (juce::Time::getMillisecondCounter() < end)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::puts ("usage: swoon_ui_snapshot <out.png> [--size WxH] [--scale S] [--preset N] [--ui key=value] [--frames N]");
        return 1;
    }

    juce::ScopedJuceInitialiser_GUI gui;

    const juce::File outFile = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]);
    int width = 0, height = 0, preset = -1, frames = 12;
    float scale = 1.0f;
    juce::StringPairArray uiProps;

    for (int i = 2; i < argc; ++i)
    {
        const juce::String arg (argv[i]);
        const juce::String next (i + 1 < argc ? argv[i + 1] : "");

        if (arg == "--size")        { width = next.upToFirstOccurrenceOf ("x", false, true).getIntValue();
                                      height = next.fromFirstOccurrenceOf ("x", false, true).getIntValue(); ++i; }
        else if (arg == "--scale")  { scale = next.getFloatValue(); ++i; }
        else if (arg == "--preset") { preset = next.getIntValue(); ++i; }
        else if (arg == "--frames") { frames = next.getIntValue(); ++i; }
        else if (arg == "--ui")     { uiProps.set (next.upToFirstOccurrenceOf ("=", false, false),
                                                   next.fromFirstOccurrenceOf ("=", false, false)); ++i; }
    }

    SwoonAudioProcessor proc;
    proc.setPlayConfigDetails (2, 2, 48000.0, 512);
    proc.prepareToPlay (48000.0, 512);

    if (preset >= 0)
        proc.setCurrentProgram (preset);

    auto uiState = proc.getUiState();
    for (const auto& key : uiProps.getAllKeys())
        uiState.setProperty (key, uiProps[key], nullptr);

    pumpAudio (proc, 100);

    std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditorIfNeeded());
    if (editor == nullptr)
        return 2;

    if (width > 0 && height > 0)
        editor->setSize (width, height);

    // Let timers run so animated parts settle, feeding audio between frames.
    for (int f = 0; f < frames; ++f)
    {
        pumpAudio (proc, 4);
        pumpMessages (40);
    }

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, scale);

    outFile.deleteFile();
    juce::FileOutputStream stream (outFile);
    if (! stream.openedOk() || ! juce::PNGImageFormat().writeImageToStream (image, stream))
    {
        std::puts ("failed to write image");
        return 3;
    }

    std::printf ("wrote %s (%dx%d)\n", outFile.getFullPathName().toRawUTF8(), image.getWidth(), image.getHeight());
    proc.editorBeingDeleted (editor.get());
    editor.reset();
    return 0;
}
