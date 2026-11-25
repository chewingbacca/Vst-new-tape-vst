// Renders the plugin editor to a PNG without a display server interaction (run under xvfb).
//
//   automata_snapshot <out.png> [--project file] [--scale 1.0] [--tab N] [--track N] [--play bars] [--size WxH]
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "HostHarness.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    juce::StringArray args;
    for (int i = 1; i < argc; ++i) args.add (argv[i]);
    if (args.isEmpty()) { std::printf ("usage: automata_snapshot <out.png> [options]\n"); return 2; }
    auto value = [&args] (const char* flag, const juce::String& def = {}) -> juce::String
    {
        const int i = args.indexOf (flag);
        return (i >= 0 && i + 1 < args.size()) ? args[i + 1] : def;
    };
    auto proc = std::make_unique<AutomataProcessor>();
    proc->setRateAndBufferSizeDetails (48000.0, 512);
    proc->prepareToPlay (48000.0, 512);
    if (args.contains ("--project")) proc->loadProjectFile (juce::File::getCurrentWorkingDirectory().getChildFile (value ("--project")));
    if (args.contains ("--json"))
    {
        bool ok = false;
        const atm::Json j = atm::Json::parse (juce::File::getCurrentWorkingDirectory().getChildFile (value ("--json")).loadFileAsString().toStdString(), &ok);
        if (ok) proc->session().fromJson (j);
    }
    const double presetBpm = harness::loadPresetArgs (*proc, args);
    if (args.contains ("--demo")) { auto p = std::make_unique<atm::Project>(); harness::demoProject (*p); proc->session().replaceProject (*p); }
    AutomataEditor::applySnapshotOptions (*proc, args);

    // run the engine for a moment so playheads, meters and modulation have something to show
    if (args.contains ("--play"))
    {
        harness::RenderOptions o; o.bars = value ("--play", "1").getDoubleValue(); o.tailSeconds = 0.0; o.bpm = presetBpm > 0.0 ? presetBpm : 120.0;
        harness::render (*proc, o);
    }

    std::unique_ptr<juce::AudioProcessorEditor> ed (proc->createEditor());
    if (auto* ae = dynamic_cast<AutomataEditor*> (ed.get())) ae->configureForSnapshot (args);
    const float scale = value ("--scale", "1").getFloatValue();
    juce::Rectangle<int> area = ed->getLocalBounds();
    if (args.contains ("--crop"))
    {
        const auto c = juce::StringArray::fromTokens (value ("--crop"), ",", "");
        if (c.size() == 4) area = juce::Rectangle<int> (c[0].getIntValue(), c[1].getIntValue(), c[2].getIntValue(), c[3].getIntValue());
    }
    const juce::Image img = ed->createComponentSnapshot (area, true, scale);
    juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (args[0]);
    out.deleteFile();
    juce::FileOutputStream os (out);
    juce::PNGImageFormat png;
    const bool ok = os.openedOk() && png.writeImageToStream (img, os);
    std::printf ("%s %dx%d\n", ok ? "wrote" : "FAILED", img.getWidth(), img.getHeight());
    ed.reset();
    return ok ? 0 : 1;
}
