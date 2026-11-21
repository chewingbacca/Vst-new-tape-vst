// Splinter UI — the five pages under the macro strip.
#pragma once
#include "CurveEditor.h"
#include "StepGrid.h"
#include "Widgets.h"

class Page : public juce::Component
{
public:
    explicit Page (SplinterProcessor& p) : proc (p) {}
    virtual void tick() {}
protected:
    SplinterProcessor& proc;
    template <typename T, typename... Args> T* add (Args&&... args)
    {
        auto* c = new T (std::forward<Args> (args)...);
        owned.add (c); addAndMakeVisible (c);
        return c;
    }
    juce::OwnedArray<juce::Component> owned;
};

// ---------------------------------------------------------------- slot pads
class SlotBank final : public juce::Component, public juce::SettableTooltipClient
{
public:
    enum Kind { timeKind, volKind, seqKind };
    SlotBank (SplinterProcessor&, Kind);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void tick();
private:
    SplinterProcessor& proc;
    Kind kind;
    PatternBank bank;
    int bankVersion = -1, current = 0, pending = -1, blink = 0;
    juce::Rectangle<float> pad (int i) const;
    juce::Colour colour() const { return kind == timeKind ? theme::lime : (kind == volKind ? theme::cyan : theme::pink); }
};

class PerformPage final : public Page
{
public:
    explicit PerformPage (SplinterProcessor&);
    void resized() override;
    void paint (juce::Graphics&) override;
    void tick() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
private:
    SlotBank time, vol, seq;
    juce::Rectangle<float> fxPad (int lane) const;
    int heldPad = -1;
    float glow[splinter::kNumLanes] = {};
    juce::Component* quant = nullptr; juce::Component* midi = nullptr; juce::Component* trig = nullptr;
    juce::Component* loopPlay = nullptr; juce::Component* seed = nullptr; TextBtn* generate = nullptr; TextBtn* newSeed = nullptr;
    void setHeld (int lane);
};

class CurvesPage final : public Page
{
public:
    explicit CurvesPage (SplinterProcessor&);
    void resized() override;
    void paint (juce::Graphics&) override;
    void tick() override;
private:
    CurveEditor timeEd, volEd;
    bool showTime = true;
    TextBtn* tabTime; TextBtn* tabVol; TextBtn* prev; TextBtn* next; TextBtn* slotName; TextBtn* gridBtn; TextBtn* pencil;
    TextBtn* lengthBtn; TextBtn* rangeBtn; TextBtn* shapes;
    Knob* depthTime; Knob* depthVol; Knob* smoothTime; Knob* smoothVol;
    int lastSlot = -1, lastVersion = -1;
    CurveEditor& ed() { return showTime ? timeEd : volEd; }
    void refreshButtons();
    void insertShape (int id);
    void setShowTime (bool);
};

class SequencePage final : public Page
{
public:
    explicit SequencePage (SplinterProcessor&);
    void resized() override;
    void paint (juce::Graphics&) override;
    void tick() override;
private:
    StepGrid grid;
    TextBtn* prev; TextBtn* next; TextBtn* slotName; TextBtn* stepsBtn; TextBtn* sizeBtn; TextBtn* generate; TextBtn* clear; TextBtn* copyBtn;
    std::vector<std::vector<juce::Component*>> laneWidgets;
    int lastSlot = -1, lastVersion = -1, shownLane = -1;
    juce::Rectangle<int> panelArea;
    void showLane (int lane);
    void refreshButtons();
};

class HarmonyPage final : public Page
{
public:
    explicit HarmonyPage (SplinterProcessor&);
    void resized() override;
    void paint (juce::Graphics&) override;
    void tick() override;
private:
    juce::Component* key; juce::Component* scale; juce::Component* mode; juce::Component* gate; Knob* formant; Knob* amount; TextBtn* detect;
    struct Strip { ToggleBox* on; Knob* interval; Knob* pan; Knob* detune; Knob* level; };
    Strip strips[splinter::kNumHarmVoices];
    float semis[splinter::kNumHarmVoices] = {}, level[splinter::kNumHarmVoices] = {};
    int pitchClass = -1;
    juce::Rectangle<int> wheelArea;
};

class OutputPage final : public Page
{
public:
    explicit OutputPage (SplinterProcessor&);
    void resized() override;
    void paint (juce::Graphics&) override;
    void tick() override;
private:
    std::vector<juce::Component*> filterRow, toneRow, engineRow, sessionRow;
    float peakL = 0.0f, peakR = 0.0f, gr = 1.0f, inPeak = 0.0f;
    juce::Rectangle<int> meterArea;
};
