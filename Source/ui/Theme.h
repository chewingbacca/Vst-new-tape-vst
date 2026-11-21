// Splinter UI — colours, fonts and drawing helpers. Everything is custom drawn.
#pragma once
#include "dsp/Model.h"
#include <juce_gui_extra/juce_gui_extra.h>

namespace theme
{
using juce::Colour;
inline const Colour bg        { 0xff101116 };
inline const Colour panel     { 0xff191b22 };
inline const Colour panelHi   { 0xff22252f };
inline const Colour outline   { 0xff2e3240 };
inline const Colour grid      { 0xff2a2d39 };
inline const Colour gridStrong { 0xff3a3f50 };
inline const Colour text      { 0xffe9ecf4 };
inline const Colour textDim   { 0xff8a90a3 };
inline const Colour textFaint { 0xff5a6073 };

inline const Colour lime   { 0xffb9f24a };   // time / warp
inline const Colour pink   { 0xffff4f8b };   // chop / stutter
inline const Colour cyan   { 0xff3ad6ff };   // volume / gates
inline const Colour amber  { 0xffffb23e };   // crush / dirt
inline const Colour violet { 0xffa884ff };   // harmony
inline const Colour red    { 0xffff5b4f };   // chaos
inline const Colour white  { 0xfff4f6fb };   // mix / neutral

inline Colour laneColour (int lane)
{
    using namespace splinter;
    switch (lane)
    {
        case laneChop: return pink;
        case laneStutter: return Colour (0xffff7a59);
        case laneReverse: return lime;
        case laneTapeStop: return Colour (0xff8fe36a);
        case lanePitch: return Colour (0xff5fe0b0);
        case laneCrush: return amber;
        case laneComb: return Colour (0xffffd24a);
        case laneRing: return Colour (0xffff9a3e);
        case laneFilter: return cyan;
        case laneFreeze: return Colour (0xff7fb8ff);
        case laneDelay: return Colour (0xff6f8cff);
        case laneReverb: return Colour (0xff8f7bff);
        case laneHarmony: return violet;
        default: return white;
    }
}

inline juce::Font font (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}

// A rectangle with two opposite corners cut off: the "splinter" shape used for
// panels, buttons and pads.
inline juce::Path chamfer (juce::Rectangle<float> r, float cut)
{
    juce::Path p;
    cut = juce::jmin (cut, r.getWidth() * 0.5f, r.getHeight() * 0.5f);
    p.startNewSubPath (r.getX() + cut, r.getY());
    p.lineTo (r.getRight(), r.getY());
    p.lineTo (r.getRight(), r.getBottom() - cut);
    p.lineTo (r.getRight() - cut, r.getBottom());
    p.lineTo (r.getX(), r.getBottom());
    p.lineTo (r.getX(), r.getY() + cut);
    p.closeSubPath();
    return p;
}

inline void drawPanel (juce::Graphics& g, juce::Rectangle<float> r, Colour fill = panel, float cut = 8.0f)
{
    const auto p = chamfer (r, cut);
    g.setColour (fill); g.fillPath (p);
    g.setColour (outline); g.strokePath (p, juce::PathStrokeType (1.0f));
}

inline void drawLabel (juce::Graphics& g, const juce::String& s, juce::Rectangle<int> r, Colour c = textDim, float size = 10.5f,
                       juce::Justification j = juce::Justification::centred, bool bold = true)
{
    g.setColour (c); g.setFont (font (size, bold));
    g.drawText (s, r, j, false);
}

inline juce::String noteName (int pc) { static const char* n[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; return n[((pc % 12) + 12) % 12]; }
} // namespace theme
