// Top section: identity, presets, transport, pattern bank, performance buttons,
// the six generative macros, New Idea, mod macros, master and the visualizer.
#pragma once
#include "Widgets.h"
#include "Visualizer.h"

namespace ui {

class PatternSlots : public juce::Component
{
public:
    explicit PatternSlots (Ctx& c) : ctx (c) {}
    void tick();
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override { ctx.hoverHelp = "Patterns: click to switch on the next bar (Shift = immediately). Right-click to copy/paste/clear. MIDI notes C1.. on the pattern channel also switch."; }
    void mouseExit (const juce::MouseEvent&) override { ctx.hoverHelp = {}; }
private:
    int slotAt (juce::Point<int> p) const;
    Ctx& ctx;
    int playing = 0, queued = -1, edit = 0, blink = 0;
    uint64_t rev = 0;
    std::unique_ptr<atm::Pattern> clip;
};

class TopBar : public juce::Component
{
public:
    explicit TopBar (Ctx& c);
    void tick (bool projectChanged);
    void paint (juce::Graphics&) override;
    void resized() override;

    // actions also reachable from keyboard shortcuts
    void togglePlay();
    void newIdea (bool newSeed);
    void stepPreset (int delta);
    void showPresetMenu();
    void saveProject();
    void loadProject();

    static constexpr int kHeight = 152;

private:
    void loadFactoryPreset (int index);
    void startMidiDrag();
    void startAudioDrag();

    Ctx& ctx;
    Btn presetPrev { "<" }, presetNext { ">" }, presetName, saveBtn { "SAVE" }, loadBtn { "LOAD" };
    Btn playBtn { "PLAY" }, fillBtn { "FILL" }, breakBtn { "BREAK" }, evolveBtn { "EVOLVE" };
    Btn undoBtn { "UNDO" }, redoBtn { "REDO" }, midiDrag { "MIDI" }, wavDrag { "WAV" }, songBtn { "SONG" };
    Btn ideaBtn { "NEW IDEA" }, diceBtn { "RND" };
    NumberBox tempoBox, seedBox, barsBox;
    Picker stylePicker;
    PatternSlots slots;
    Visualizer viz;
    std::vector<std::unique_ptr<Knob>> macroKnobs, modKnobs;
    std::unique_ptr<Knob> masterKnob, swingKnob;
    std::unique_ptr<juce::FileChooser> chooser;
    int presetIndex = -1;
    float cpuShown = 0.f, grShown = 1.f, peakL = 0.f, peakR = 0.f;
    bool hostPlaying = false;
    juce::File dragFile;
};

} // namespace ui
