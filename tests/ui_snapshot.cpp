// Renders the editor to PNG files (one per page) so the UI can be reviewed
// without a DAW:  splinter_uishot <output dir> [preset index] [scale]
#include "plugin/PluginProcessor.h"
#include "ui/PluginEditor.h"

struct Head final : juce::AudioPlayHead
{
    double ppq = 0.0, bpm = 124.0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p; p.setBpm (bpm); p.setPpqPosition (ppq); p.setIsPlaying (true); p.setTimeSignature (TimeSignature { 4, 4 });
        return p;
    }
};

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File out (argc > 1 ? argv[1] : "ui-shots");
    out.createDirectory();
    const int preset = argc > 2 ? juce::String (argv[2]).getIntValue() : 14;
    const float scale = argc > 3 ? juce::String (argv[3]).getFloatValue() : 1.5f;

    std::unique_ptr<SplinterProcessor> proc (dynamic_cast<SplinterProcessor*> (createPluginFilter()));
    Head head;
    proc->setPlayHead (&head);
    proc->setPlayConfigDetails (2, 2, 48000.0, 512);
    proc->prepareToPlay (48000.0, 512);
    proc->loops.setTarget (48000.0, head.bpm, 0);
    proc->loops.waitUntilReady (30000);
    proc->handleLoopChanged (false);
    proc->loadFactoryPreset (preset);
    proc->setParam (pid::harmony, 0.6f);
    proc->setParam (pid::chaos, 0.25f);

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc->createEditor());
    auto* editor = dynamic_cast<SplinterEditor*> (ed.get());
    juce::AudioBuffer<float> buf (2, 512); juce::MidiBuffer midi;
    auto run = [&] (double beats)
    {
        const double end = head.ppq + beats;
        while (head.ppq < end)
        {
            buf.clear(); proc->processBlock (buf, midi);
            head.ppq += 512.0 * head.bpm / (60.0 * 48000.0);
            editor->refreshNow();
        }
    };
    static const char* names[5] = { "perform", "curves", "sequence", "harmony", "output" };
    for (int page = 0; page < 5; ++page)
    {
        editor->setPage (page);
        run (page == 2 ? 7.4 : 5.83);
        const auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, scale);
        juce::PNGImageFormat png;
        const juce::File f = out.getChildFile (juce::String ("splinter-") + names[page] + ".png");
        f.deleteFile();
        if (auto os = f.createOutputStream()) png.writeImageToStream (img, *os);
        std::printf ("wrote %s (%dx%d)\n", f.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
    }
    ed.reset();
    return 0;
}
