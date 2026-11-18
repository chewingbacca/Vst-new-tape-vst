// verdant_ui_snapshot — renders the Verdant editor to PNG without a host.
//
//   --out DIR            output directory (default: current directory)
//   --name NAME          base file name (default: the page name)
//   --width W            editor width in pixels (height follows the fixed aspect ratio)
//   --page garden|soil   page to show
//   --browser            open the preset overlay
//   --fake-presets N     add N fake presets to the overlay (to test scrolling)
//   --choices ID         open the choice list of parameter ID (e.g. scale)
//   --help-for ID        show parameter ID in the help line (as if hovered)
//   --set ID=VALUE       set a parameter to a plain value (repeatable)
//   --frames N           animation frames to run before the snapshot (default 150)
//   --seq N              also write N consecutive frames NAME_000.png ... (animation check)
//   --level X            0..1 loudness of the simulated meters (0 = silence, default 0.7)
//   --audio SECONDS      run a synthetic signal through the real engine first
//   --real-meters        do not overwrite the engine meters with simulated values
//   --stutter            fire a stutter pulse shortly before the snapshot
//   --bench N            paint the visual N times and print timing (default 200)
//
// Run under a display, e.g.  xvfb-run -a verdant_ui_snapshot --out /tmp/ui --page soil
#include <cmath>
#include <cstdio>

#include <juce_audio_utils/juce_audio_utils.h>

#include "plugin/PluginEditor.h"
#include "plugin/PluginProcessor.h"

namespace
{
struct Options
{
    juce::File outDir = juce::File::getCurrentWorkingDirectory();
    juce::String name, page = "garden", choices, helpFor;
    juce::StringArray sets;
    int width = 1000, frames = 150, seq = 0, fakePresets = 0, bench = 200;
    float level = 0.7f;
    double audioSeconds = 0.0;
    bool browser = false, realMeters = false, stutter = false;
};

Options parse (const juce::StringArray& args)
{
    Options o;

    for (int i = 0; i < args.size(); ++i)
    {
        const auto& a = args[i];
        const auto next = [&] { return i + 1 < args.size() ? args[++i] : juce::String(); };

        if      (a == "--out")           o.outDir = juce::File::getCurrentWorkingDirectory().getChildFile (next());
        else if (a == "--name")          o.name = next();
        else if (a == "--width")         o.width = next().getIntValue();
        else if (a == "--page")          o.page = next().toLowerCase();
        else if (a == "--browser")       o.browser = true;
        else if (a == "--fake-presets")  o.fakePresets = next().getIntValue();
        else if (a == "--choices")       o.choices = next();
        else if (a == "--help-for")      o.helpFor = next();
        else if (a == "--set")           o.sets.add (next());
        else if (a == "--frames")        o.frames = next().getIntValue();
        else if (a == "--seq")           o.seq = next().getIntValue();
        else if (a == "--level")         o.level = next().getFloatValue();
        else if (a == "--audio")         o.audioSeconds = next().getDoubleValue();
        else if (a == "--real-meters")   o.realMeters = true;
        else if (a == "--stutter")       o.stutter = true;
        else if (a == "--bench")         o.bench = next().getIntValue();
        else std::fprintf (stderr, "unknown argument: %s\n", a.toRawUTF8());
    }

    if (o.name.isEmpty())
        o.name = o.page;

    return o;
}

// Plausible meter values for frame `frame` (30 fps), as the finished engine would publish them.
void simulateMeters (verdant::Meters& m, int frame, float level)
{
    const float t = float (frame) / 30.0f;
    const float phrase = 0.5f + 0.5f * std::sin (t * 1.9f);
    const float pluck = std::exp (-std::fmod (t, 0.5f) * 5.0f);
    const float env = level * juce::jlimit (0.0f, 1.0f, 0.25f + 0.45f * phrase + 0.3f * pluck);

    m.inLevel.store (env * 0.8f);
    m.outLevelL.store (level * (0.45f + 0.3f * phrase));
    m.outLevelR.store (level * (0.40f + 0.33f * (0.5f + 0.5f * std::sin (t * 2.3f))));
    m.envelope.store (env);
    m.wetLevel.store (level * (0.35f + 0.25f * std::sin (t * 0.7f) + 0.2f));
    m.limiterGain.store (level > 0.9f ? 0.7f + 0.2f * std::sin (t * 3.0f) : 1.0f);

    for (int i = 0; i < 4; ++i)
        m.drift[i].store (std::sin (t * (0.17f + 0.09f * float (i)) + float (i) * 1.3f));

    m.low.store (level * (0.5f + 0.4f * std::sin (t * 1.1f)));
    m.mid.store (level * (0.5f + 0.4f * std::sin (t * 1.7f + 1.0f)));
    m.high.store (level * (0.5f + 0.4f * std::sin (t * 2.3f + 2.0f)));
    m.grainActivity.store (level * 0.7f);
    m.pitchHz.store (level > 0.01f ? 220.0f * std::pow (2.0f, float ((frame / 45) % 5) * 2.0f / 12.0f) : 0.0f);

    if (level > 0.01f)
    {
        m.grainPulse.fetch_add (1 + (frame % 3 == 0 ? 1 : 0));

        if (frame % 5 == 0)
        {
            m.arpDegree.store ((frame / 5) * 3 % 16);
            m.arpPulse.fetch_add (1);
        }
    }
}

bool writePng (const juce::Image& image, const juce::File& file)
{
    file.deleteFile();
    juce::FileOutputStream stream (file);
    return stream.openedOk() && juce::PNGImageFormat().writeImageToStream (image, stream);
}
}

//==============================================================================
int main (int argc, char* argv[])
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    const auto options = parse (juce::StringArray (argv + 1, argc - 1));

    if (! options.outDir.isDirectory() && ! options.outDir.createDirectory())
    {
        std::fprintf (stderr, "cannot create %s\n", options.outDir.getFullPathName().toRawUTF8());
        return 1;
    }

    auto processor = std::make_unique<VerdantProcessor>();
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 512;
    processor->setRateAndBufferSizeDetails (sampleRate, blockSize);
    processor->prepareToPlay (sampleRate, blockSize);

    for (const auto& s : options.sets)
    {
        const auto id = s.upToFirstOccurrenceOf ("=", false, false).trim();
        const float value = s.fromFirstOccurrenceOf ("=", false, false).getFloatValue();

        if (auto* p = processor->apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
        else
            std::fprintf (stderr, "unknown parameter id: %s\n", id.toRawUTF8());
    }

    if (options.audioSeconds > 0.0)
    {
        // A decaying plucked chord, so every stage of the engine has something to chew on.
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        const int blocks = int (options.audioSeconds * sampleRate / blockSize);
        double phase[3] {};
        const double freqs[3] = { 220.0, 277.18, 329.63 };
        int n = 0;

        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < blockSize; ++i, ++n)
            {
                const double tt = std::fmod (double (n) / sampleRate, 0.75);
                const double env = std::exp (-tt * 4.0);
                double v = 0.0;
                for (int k = 0; k < 3; ++k)
                {
                    phase[k] += freqs[k] / sampleRate;
                    v += std::sin (juce::MathConstants<double>::twoPi * phase[k]);
                }
                const float sample = float (v * env * 0.25);
                buffer.setSample (0, i, sample);
                buffer.setSample (1, i, sample);
            }
            processor->processBlock (buffer, midi);
        }
    }

    int result = 0;

    {
        std::unique_ptr<juce::AudioProcessorEditor> editorOwner (processor->createEditor());
        auto* editor = dynamic_cast<VerdantEditor*> (editorOwner.get());

        if (editor == nullptr)
        {
            std::fprintf (stderr, "createEditor() did not return a VerdantEditor\n");
            return 1;
        }

        const int width = juce::jlimit (300, 4000, options.width);
        editor->setSize (width, juce::roundToInt (width * 0.66));
        editor->setPage (options.page == "soil" ? VerdantEditor::soil : VerdantEditor::garden);

        if (options.fakePresets > 0)
        {
            juce::Array<VerdantProcessor::PresetInfo> extras;
            const char* categories[] = { "Subtle", "Lush", "Motion", "Deep", "Wild" };
            const char* names[] = { "Morning Dew", "Fern Hollow", "Glasshouse Rain", "Moss Cathedral", "Pollen Drift",
                                    "Slow Tendrils", "Root Cellar", "Night Bloom", "Canopy Light", "Spore Choir" };

            for (int i = 0; i < options.fakePresets; ++i)
                extras.add ({ juce::String (names[i % 10]) + (i >= 10 ? " " + juce::String (i / 10 + 1) : juce::String()),
                              categories[i % 5], "A placeholder description to test the list layout and scrolling.", true, {} });

            extras.add ({ "My Greenhouse", "User", "User preset", false, {} });
            extras.add ({ "Late Summer Tape", "User", "User preset", false, {} });
            editor->debugSetExtraPresets (extras);
        }

        auto& meters = processor->engine.meters;
        int frame = 0;

        const auto runFrames = [&] (int count)
        {
            for (int i = 0; i < count; ++i, ++frame)
            {
                if (! options.realMeters)
                    simulateMeters (meters, frame, options.level);

                if (options.stutter && i == count - 3)
                    meters.stutterPulse.fetch_add (1);

                editor->advanceAnimation (1.0 / 30.0);
            }
        };

        runFrames (juce::jmax (1, options.frames));

        if (options.browser)
            editor->setPresetBrowserOpen (true);

        if (options.choices.isNotEmpty())
        {
            const int index = verdant::findParam (options.choices.toRawUTF8());
            if (index < 0 || ! editor->debugOpenChoiceList (verdant::PID (index)))
                std::fprintf (stderr, "no visible choice control for '%s'\n", options.choices.toRawUTF8());
        }

        if (options.helpFor.isNotEmpty())
        {
            const int index = verdant::findParam (options.helpFor.toRawUTF8());
            if (index >= 0)
                editor->debugShowHelpFor (verdant::PID (index));
            else
                std::fprintf (stderr, "unknown parameter id: %s\n", options.helpFor.toRawUTF8());
        }

        const auto snapshot = [&] { return editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f); };

        const auto file = options.outDir.getChildFile (options.name + ".png");
        if (writePng (snapshot(), file))
            std::printf ("wrote %s (%d x %d)\n", file.getFullPathName().toRawUTF8(), editor->getWidth(), editor->getHeight());
        else
        {
            std::fprintf (stderr, "failed to write %s\n", file.getFullPathName().toRawUTF8());
            result = 1;
        }

        for (int i = 0; i < options.seq; ++i)
        {
            runFrames (1);
            writePng (snapshot(), options.outDir.getChildFile (options.name + "_" + juce::String (i).paddedLeft ('0', 3) + ".png"));
        }

        // ---- paint timing of the generative visual (software renderer, into an image)
        if (options.bench > 0)
        {
            auto& visual = editor->getActiveVisual();
            juce::Image target (juce::Image::RGB, juce::jmax (1, visual.getWidth()), juce::jmax (1, visual.getHeight()), true,
                                juce::SoftwareImageType());

            for (int i = 0; i < 10; ++i)   // warm-up (caches, path storage)
            {
                runFrames (1);
                juce::Graphics g (target);
                visual.paintEntireComponent (g, false);
            }

            visual.resetPaintStats();
            for (int i = 0; i < options.bench; ++i)
            {
                runFrames (1);
                juce::Graphics g (target);
                visual.paintEntireComponent (g, false);
            }

            const auto stats = visual.getPaintStats();

            const double t0 = juce::Time::getMillisecondCounterHiRes();
            constexpr int fullFrames = 20;
            for (int i = 0; i < fullFrames; ++i)
            {
                runFrames (1);
                snapshot();
            }
            const double fullMs = (juce::Time::getMillisecondCounterHiRes() - t0) / double (fullFrames);

            std::printf ("visual %d x %d: paint avg %.2f ms, max %.2f ms over %d frames; whole editor repaint %.2f ms\n",
                         visual.getWidth(), visual.getHeight(), stats.averageMs, stats.maxMs, stats.frames, fullMs);
        }
    }

    processor.reset();
    return result;
}
