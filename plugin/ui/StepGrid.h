// The main view: all 8 tracks' steps at once. Paint steps by click-drag,
// Shift = velocity, Ctrl/Cmd = probability, Alt = ratchets, right-click = select for locks.
#pragma once
#include "Widgets.h"

namespace ui {

class StepGrid : public juce::Component
{
public:
    explicit StepGrid (Ctx& c);
    void tick (bool projectChanged);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { ctx.hoverHelp = {}; }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    static constexpr int kHeaderW = 204, kInfoW = 118;
    juce::Rectangle<int> rowBounds (int t) const;
    juce::Rectangle<int> stepsBounds (int t) const;
    juce::Rectangle<float> cellBounds (int t, int i) const;
    int columnsFor (int t) const;
    // For lane editors that want to align with a track's cells.
    void cellGeometry (int t, float& x0, float& cellW) const;

private:
    enum class Zone { None, Header, Mute, Solo, Lock, Steps, Info };
    struct Hit { Zone zone = Zone::None; int track = -1, step = -1; float fy = 0.f; };
    Hit hitTest (juce::Point<int> p) const;
    void paintRow (juce::Graphics&, int t);
    void paintCell (juce::Graphics&, int t, int i, juce::Rectangle<float> r, const atm::Step& s, bool playing, bool selected);
    void applyDrag (const Hit& h, const juce::MouseEvent& e);
    void showHeaderMenu (int t);
    void showStepMenu (int t, int i);

    enum class Mode { None, Paint, Velocity, Prob, Ratchet, Select, Length };
    Ctx& ctx;
    Mode mode = Mode::None;
    bool paintOn = true;
    int dragTrack = -1, lastStep = -1, selAnchor = -1;
    int playStep[atm::kTracks];
    uint32_t trigSeen[atm::kTracks] = {};
    float flash[atm::kTracks] = {};
    float peak[atm::kTracks] = {};
    uint32_t lastUiRev = 0;
    int lengthStart = 16;
};

} // namespace ui
