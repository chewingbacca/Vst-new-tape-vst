// Renders the plugin editor to PNG files without a host, for visual review.
//
//   swoon_ui_snapshot <out.png> [--size WxH] [--scale S] [--preset N] [--ui key=value]... [--frames N]
//                     [--set paramId=value]... [--browser] [--silent]
//                     [--hover X,Y] [--click X,Y] [--drag X1,Y1,X2,Y2] [--hold X1,Y1,X2,Y2]...
//                     [--save NAME]... [--type TEXT] [--bench]
//
// --ui sets a property on the processor's UI state tree before the editor is
// created (e.g. --ui page=1). --set writes a parameter (real units, e.g.
// --set wall=0.9 --set ordWash=1). --browser opens the preset browser before
// the capture. --silent feeds silence instead of the test drone. --hover,
// --click, --drag and --hold (a drag that is not released) send synthetic mouse
// events (editor pixel coordinates, in command-line order) to whatever
// component is under the point. --save NAME stores the current settings as a
// user preset first. (User presets live under the home directory: point HOME
// at a scratch folder when testing so nothing lands in the real preset folder.)
// --type TEXT puts text into the editor's text field (the browser's "save as"
// name) before the mouse actions run. --bench prints the CPU cost of the
// animated parts. Audio is
// pumped through the processor before the capture so meters and the
// visualiser have something to show.
#include "../Source/PluginEditor.h"
#include "../Source/PluginProcessor.h"

#include <cmath>
#include <cstdio>
#include <ctime>

namespace
{
bool silent = false;

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
            // a slightly different right channel so stereo displays have something to show
            const float r = env * (float) (std::sin (t * 6.2831853 * 164.8 + 0.5) + 0.6 * std::sin (t * 6.2831853 * 247.9)
                                           + 0.3 * std::sin (t * 6.2831853 * 331.0));
            buffer.setSample (0, i, silent ? 0.0f : s);
            buffer.setSample (1, i, silent ? 0.0f : r);
            phase += 1.0;
        }
        proc.processBlock (buffer, midi);
    }
}

struct MouseAction
{
    enum class Kind { hover, click, drag, hold } kind;
    juce::Point<float> from, to;
};

juce::MouseEvent makeEvent (juce::Component& target, juce::Component& editor, juce::Point<float> editorPos,
                            juce::Point<float> editorDownPos, bool buttonDown, bool dragged)
{
    const auto local = target.getLocalPoint (&editor, editorPos);
    const auto localDown = target.getLocalPoint (&editor, editorDownPos);
    const auto now = juce::Time::getCurrentTime();

    return { juce::Desktop::getInstance().getMainMouseSource(), local,
             buttonDown ? juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier) : juce::ModifierKeys(),
             juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation,
             juce::MouseInputSource::defaultRotation, juce::MouseInputSource::defaultTiltX, juce::MouseInputSource::defaultTiltY,
             &target, &target, now, localDown, now, 1, dragged };
}

void perform (juce::Component& editor, const MouseAction& action)
{
    auto* target = editor.getComponentAt (action.from);
    if (target == nullptr)
        return;

    target->mouseEnter (makeEvent (*target, editor, action.from, action.from, false, false));
    target->mouseMove (makeEvent (*target, editor, action.from, action.from, false, false));

    if (action.kind == MouseAction::Kind::hover)
        return;

    target->mouseDown (makeEvent (*target, editor, action.from, action.from, true, false));

    if (action.kind == MouseAction::Kind::drag || action.kind == MouseAction::Kind::hold)
    {
        for (int i = 1; i <= 8; ++i)
            target->mouseDrag (makeEvent (*target, editor, action.from + (action.to - action.from) * ((float) i / 8.0f), action.from, true, true));

        if (action.kind == MouseAction::Kind::hold)
            return;

        target->mouseUp (makeEvent (*target, editor, action.to, action.from, false, true));
    }
    else
    {
        target->mouseUp (makeEvent (*target, editor, action.from, action.from, false, false));
    }

}

juce::TextEditor* findTextEditor (juce::Component& parent)
{
    for (auto* child : parent.getChildren())
    {
        if (auto* field = dynamic_cast<juce::TextEditor*> (child))
            return field;
        if (auto* nested = findTextEditor (*child))
            return nested;
    }
    return nullptr;
}

juce::Point<float> parsePoint (const juce::StringArray& tokens, int first)
{
    return { tokens[first].getFloatValue(), tokens[first + 1].getFloatValue() };
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
        std::puts ("usage: swoon_ui_snapshot <out.png> [--size WxH] [--scale S] [--preset N] [--ui key=value] [--frames N]\n"
                   "                         [--set paramId=value] [--browser] [--silent]");
        return 1;
    }

    juce::ScopedJuceInitialiser_GUI gui;

    const juce::File outFile = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]);
    int width = 0, height = 0, preset = -1, frames = 12;
    float scale = 1.0f;
    bool openBrowser = false, bench = false;
    juce::StringPairArray uiProps, paramValues;
    std::vector<MouseAction> actions;
    juce::StringArray saveNames;
    juce::String typedText;

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
        else if (arg == "--set")    { paramValues.set (next.upToFirstOccurrenceOf ("=", false, false),
                                                       next.fromFirstOccurrenceOf ("=", false, false)); ++i; }
        else if (arg == "--browser") { openBrowser = true; }
        else if (arg == "--save")    { saveNames.add (next); ++i; }
        else if (arg == "--type")    { typedText = next; ++i; }
        else if (arg == "--hover" || arg == "--click" || arg == "--drag" || arg == "--hold")
        {
            const auto tokens = juce::StringArray::fromTokens (next, ",", "");
            const auto from = parsePoint (tokens, 0);
            actions.push_back ({ arg == "--hover" ? MouseAction::Kind::hover : arg == "--click" ? MouseAction::Kind::click
                                 : arg == "--drag" ? MouseAction::Kind::drag : MouseAction::Kind::hold,
                                 from, tokens.size() >= 4 ? parsePoint (tokens, 2) : from });
            ++i;
        }
        else if (arg == "--silent")  { silent = true; }
        else if (arg == "--bench")   { bench = true; }
    }

    SwoonAudioProcessor proc;
    proc.setPlayConfigDetails (2, 2, 48000.0, 512);
    proc.prepareToPlay (48000.0, 512);

    if (preset >= 0)
        proc.setCurrentProgram (preset);

    for (const auto& id : paramValues.getAllKeys())
    {
        if (auto* param = proc.getAPVTS().getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (paramValues[id].getFloatValue()));
        else
            std::printf ("unknown parameter: %s\n", id.toRawUTF8());
    }

    for (const auto& name : saveNames)
        proc.getPresetManager().saveUserPreset (name);

    auto uiState = proc.getUiState();
    for (const auto& key : uiProps.getAllKeys())
        uiState.setProperty (key, uiProps[key], nullptr);

    pumpAudio (proc, 100);

    std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditorAndMakeActive());
    if (editor == nullptr)
        return 2;

    editor->setVisible (true);   // not on the desktop, but hit-testing needs the flag

    if (width > 0 && height > 0)
        editor->setSize (width, height);


    // Let timers run so animated parts settle, feeding audio between frames.
    for (int f = 0; f < frames; ++f)
    {
        pumpAudio (proc, 4);
        pumpMessages (40);
    }

    if (openBrowser)
    {
        if (auto* swoonEditor = dynamic_cast<SwoonAudioProcessorEditor*> (editor.get()))
            swoonEditor->setBrowserOpen (true);

        pumpMessages (60);
    }

    if (typedText.isNotEmpty())
    {
        if (auto* field = findTextEditor (*editor))
            field->setText (typedText, true);

        pumpMessages (60);
    }

    for (const auto& action : actions)
    {
        perform (*editor, action);

        if (action.kind != MouseAction::Kind::hover)
        {
            pumpAudio (proc, 4);
            pumpMessages (400);   // let reorder animations and async parameter callbacks settle
        }
    }

    if (bench)
    {
        // rough cost of the animated parts: timer work alone, then timer work plus a repaint of the top half
        const auto cpuMs = [] { return (double) std::clock() * 1000.0 / (double) CLOCKS_PER_SEC; };
        const int iterations = 90;

        double start = cpuMs();
        for (int i = 0; i < iterations; ++i) { pumpAudio (proc, 2); pumpMessages (33); }
        const double idle = (cpuMs() - start) / iterations;

        const auto top = editor->getLocalBounds().removeFromTop (editor->getHeight() * 52 / 100);
        start = cpuMs();
        for (int i = 0; i < iterations; ++i)
        {
            pumpAudio (proc, 2);
            pumpMessages (33);
            editor->createComponentSnapshot (top, true, scale);
        }
        const double painted = (cpuMs() - start) / iterations;

        std::printf ("bench: %.2f ms CPU per 33 ms tick (timers + test audio), %.2f ms with a %dx%d repaint at scale %.2f\n",
                     idle, painted, top.getWidth(), top.getHeight(), (double) scale);
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
