// verdant_ui_snapshot — renders the Verdant editor to PNG without a host.
//
//   --out DIR            output directory (default: current directory)
//   --name NAME          base file name (default: the page name)
//   --width W            editor width in pixels (height follows the fixed aspect ratio)
//   --page garden|soil   page to show
//   --browser            open the preset overlay
//   --save-preset NAME   open the overlay and save a user preset through the UI (set HOME to a
//                        scratch directory first; this writes a real file)
//   --fake-presets N     add N fake presets to the overlay (to test scrolling)
//   --choices ID         open the choice list of parameter ID (e.g. scale)
//   --help-for ID        show parameter ID in the help line (as if hovered)
//   --value-entry ID[=TEXT]  open the typed value field of knob ID (repeatable), optionally with TEXT in it
//   --commit-entry       press Return in the open value fields before the snapshot
//   --scale S            render at desktop scale S (HiDPI): snapshot and timing use S device pixels per point
//   --set ID=VALUE       set a parameter to a plain value (repeatable)
//   --frames N           animation frames to run before the snapshot (default 150)
//   --seq N              also write N consecutive frames NAME_000.png ... (animation check)
//   --level X            0..1 loudness of the simulated meters (0 = silence, default 0.7)
//   --audio SECONDS      run a synthetic signal through the real engine first
//   --real-meters        do not overwrite the engine meters with simulated values
//   --stutter            fire a stutter pulse shortly before the snapshot
//   --bench N            paint the visual N times and print timing (default 200)
//   --hover-all          call mouseEnter on every component before the editor is destroyed
//                        (regression test for destruction-order bugs in the help line)
//   --stress SECONDS     show the editor on the desktop, run the real message loop and fuzz every
//                        parameter (and process audio) from a background thread, like a host would
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
    juce::String name, page = "garden", choices, helpFor, savePreset;
    juce::StringArray sets, valueEntries;
    float scale = 1.0f;
    bool commitEntry = false;
    int width = 1000, frames = 150, seq = 0, fakePresets = 0, bench = 200;
    float level = 0.7f;
    double audioSeconds = 0.0;
    bool browser = false, realMeters = false, stutter = false, hoverAll = false;
    double stressSeconds = 0.0;
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
        else if (a == "--save-preset")   o.savePreset = next();
        else if (a == "--fake-presets")  o.fakePresets = next().getIntValue();
        else if (a == "--choices")       o.choices = next();
        else if (a == "--help-for")      o.helpFor = next();
        else if (a == "--set")           o.sets.add (next());
        else if (a == "--value-entry")   o.valueEntries.add (next());
        else if (a == "--commit-entry")  o.commitEntry = true;
        else if (a == "--scale")         o.scale = juce::jlimit (0.5f, 4.0f, next().getFloatValue());
        else if (a == "--frames")        o.frames = next().getIntValue();
        else if (a == "--seq")           o.seq = next().getIntValue();
        else if (a == "--level")         o.level = next().getFloatValue();
        else if (a == "--audio")         o.audioSeconds = next().getDoubleValue();
        else if (a == "--real-meters")   o.realMeters = true;
        else if (a == "--stutter")       o.stutter = true;
        else if (a == "--bench")         o.bench = next().getIntValue();
        else if (a == "--hover-all")     o.hoverAll = true;
        else if (a == "--stress")        o.stressSeconds = next().getDoubleValue();
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

// Mimics a host automating everything while the editor is open: a background thread sets
// random values on every parameter and runs audio; the message thread runs the real UI.
struct Fuzzer final : public juce::Thread
{
    explicit Fuzzer (VerdantProcessor& p) : juce::Thread ("fuzz"), processor (p) {}

    void run() override
    {
        juce::Random r (0x5eed);
        juce::AudioBuffer<float> buffer (2, 256);
        juce::MidiBuffer midi;

        while (! threadShouldExit())
        {
            for (auto* p : processor.getParameters())
                p->setValue (r.nextFloat());

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                    buffer.setSample (ch, i, (r.nextFloat() - 0.5f) * 0.5f);

            processor.processBlock (buffer, midi);
            ++iterations;
            wait (5);
        }
    }

    VerdantProcessor& processor;
    std::atomic<int> iterations { 0 };
};

struct StressDriver final : public juce::Timer
{
    StressDriver (VerdantEditor& e, double seconds) : editor (e), endTime (juce::Time::getMillisecondCounterHiRes() + seconds * 1000.0) {}

    void timerCallback() override
    {
        // exercise the stateful bits of the UI while everything is being automated
        ++ticks;
        if (ticks % 7 == 0)   editor.setPage (ticks / 7 % 2);
        if (ticks % 11 == 0)  editor.setPresetBrowserOpen ((ticks / 11) % 2 == 0);
        if (ticks % 13 == 0)  editor.debugOpenChoiceList (ticks % 2 == 0 ? verdant::pScale : verdant::pKey);
        if (ticks % 5 == 0)   editor.debugOpenValueEntry (ticks % 2 == 0 ? verdant::pBloom : verdant::pRevDecay,
                                                          ticks % 3 == 0 ? "42" : (ticks % 3 == 1 ? "inf" : "nonsense"));
        if (ticks % 5 == 3)   editor.debugCloseValueEntries (ticks % 2 == 0);
        if (ticks % 17 == 0)  editor.setSize (750 + (ticks * 37) % 900, juce::roundToInt ((750 + (ticks * 37) % 900) * 0.66));

        if (juce::Time::getMillisecondCounterHiRes() >= endTime)
        {
            stopTimer();
            juce::MessageManager::getInstance()->stopDispatchLoop();
        }
    }

    VerdantEditor& editor;
    double endTime;
    int ticks = 0;
};

void hoverEverything (juce::Component& root)
{
    // reverse order, so the last component left "hovered" is one of the first children
    for (int i = root.getNumChildComponents(); --i >= 0;)
    {
        auto* child = root.getChildComponent (i);
        const juce::MouseEvent e (juce::Desktop::getInstance().getMainMouseSource(), {}, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                  child, child, juce::Time::getCurrentTime(), {}, juce::Time::getCurrentTime(), 0, false);
        child->mouseEnter (e);
        child->mouseMove (e);
        hoverEverything (*child);
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

        if (options.stressSeconds > 0.0)
        {
            editor->addToDesktop (0);
            editor->setVisible (true);

            Fuzzer fuzzer (*processor);
            StressDriver driver (*editor, options.stressSeconds);
            fuzzer.startThread();
            driver.startTimer (40);
            juce::MessageManager::getInstance()->runDispatchLoop();
            fuzzer.stopThread (2000);

            hoverEverything (*editor);
            std::printf ("stress: %d fuzz iterations, %d UI ticks, visual painted %d times\n",
                         fuzzer.iterations.load(), driver.ticks, editor->getActiveVisual().getPaintStats().frames);
            editor->removeFromDesktop();
            return 0;
        }

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

        if (options.savePreset.isNotEmpty())
            editor->debugSavePreset (options.savePreset);

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

        for (const auto& entry : options.valueEntries)
        {
            const auto id = entry.upToFirstOccurrenceOf ("=", false, false).trim();
            const int index = verdant::findParam (id.toRawUTF8());

            if (index < 0 || ! editor->debugOpenValueEntry (verdant::PID (index), entry.fromFirstOccurrenceOf ("=", false, false)))
                std::fprintf (stderr, "no visible knob for '%s'\n", id.toRawUTF8());
        }

        if (options.commitEntry)
        {
            editor->debugCloseValueEntries (true);

            for (const auto& entry : options.valueEntries)
                if (auto* p = processor->apvts.getParameter (entry.upToFirstOccurrenceOf ("=", false, false).trim()))
                    std::printf ("%s is now %s\n", p->getName (64).toRawUTF8(), p->getCurrentValueAsText().toRawUTF8());
        }

        const float scale = options.scale;
        const auto snapshot = [&] { return editor->createComponentSnapshot (editor->getLocalBounds(), true, scale); };

        const auto file = options.outDir.getChildFile (options.name + ".png");
        if (writePng (snapshot(), file))
            std::printf ("wrote %s (%d x %d at scale %.2f)\n", file.getFullPathName().toRawUTF8(), editor->getWidth(), editor->getHeight(), double (scale));
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

        if (options.hoverAll)
            hoverEverything (*editor);

        // ---- paint timing of the generative visual (software renderer, into an image)
        if (options.bench > 0)
        {
            auto& visual = editor->getActiveVisual();
            juce::Image target (juce::Image::RGB, juce::jmax (1, juce::roundToInt (std::ceil (float (visual.getWidth()) * scale))),
                                juce::jmax (1, juce::roundToInt (std::ceil (float (visual.getHeight()) * scale))), true,
                                juce::SoftwareImageType());

            const auto paintVisual = [&]
            {
                juce::Graphics g (target);
                g.addTransform (juce::AffineTransform::scale (scale));
                visual.paintEntireComponent (g, false);
            };

            for (int i = 0; i < 10; ++i)   // warm-up (caches, path storage)
            {
                runFrames (1);
                paintVisual();
            }

            visual.resetPaintStats();
            for (int i = 0; i < options.bench; ++i)
            {
                runFrames (1);
                paintVisual();
            }

            const auto stats = visual.getPaintStats();
            const float repaintRate = visual.getRepaintRate();

            const double t0 = juce::Time::getMillisecondCounterHiRes();
            constexpr int fullFrames = 20;
            for (int i = 0; i < fullFrames; ++i)
            {
                runFrames (1);
                snapshot();
            }
            const double fullMs = (juce::Time::getMillisecondCounterHiRes() - t0) / double (fullFrames);

            std::printf ("visual %d x %d at scale %.2f: paint median %.2f ms, avg %.2f, min %.2f, max %.2f over %d frames (repaint rate %.1f fps); whole editor repaint %.2f ms\n",
                         visual.getWidth(), visual.getHeight(), double (scale), stats.medianMs, stats.averageMs, stats.minMs, stats.maxMs, stats.frames, double (repaintRate), fullMs);
        }
    }

    processor.reset();
    return result;
}
