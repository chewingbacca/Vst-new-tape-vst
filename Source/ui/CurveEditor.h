// Splinter UI — drawable curve editor for time (offset behind real time) and
// volume (gain) patterns.
#pragma once
#include "Theme.h"
#include "plugin/PluginProcessor.h"

class CurveEditor final : public juce::Component, public juce::SettableTooltipClient
{
public:
    CurveEditor (SplinterProcessor&, bool isTimeCurve);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hoverNode = -1; repaint(); }
    void tick();

    void setSlot (int s) { if (s != slot) { slot = s; bankVersion = -1; } }
    int getSlot() const { return slot; }
    void setGrid (int divisionsPerBar) { gridDiv = divisionsPerBar; repaint(); }
    int getGrid() const { return gridDiv; }
    void setPencil (bool b) { pencil = b; setMouseCursor (b ? juce::MouseCursor::CrosshairCursor : juce::MouseCursor::NormalCursor); }
    bool isPencil() const { return pencil; }
    const splinter::Curve& getCurve() const { return curve; }
    void modify (const std::function<void (splinter::Curve&)>& fn);     // edit + commit

    // Draws a small thumbnail of any curve (used by the slot pads too).
    static void drawThumb (juce::Graphics&, juce::Rectangle<float>, const splinter::Curve&, bool isTime, juce::Colour);

private:
    SplinterProcessor& proc;
    const bool isTime;
    splinter::Curve curve;
    int slot = 0, bankVersion = -1, gridDiv = 16, hoverNode = -1, dragNode = -1, tensionSeg = -1;
    bool pencil = false, playingHere = false;
    float phase = 0.0f, tensionStart = 0.0f;
    int lastPaintCell = -1;

    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced (34.0f, 8.0f).withTrimmedBottom (10.0f).withTrimmedRight (-22.0f); }
    juce::Point<float> toScreen (float x, float y) const;
    float xFromScreen (float sx) const;
    float yFromScreen (float sy) const;
    float snapX (float x, bool snap) const;
    float snapY (float y, bool snap) const;
    int nodeAt (juce::Point<float> p) const;
    int segmentAt (juce::Point<float> p) const;
    void commit();
    void paintCell (juce::Point<float> p);
    void sortNodes();
};
