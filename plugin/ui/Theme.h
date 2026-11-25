// Visual language: dark, dense, technical. One typeface (JetBrains Mono, OFL)
// in two weights; one hue per track; a single lime accent for global controls.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "BinaryData.h"

namespace ui {

namespace col {
inline juce::Colour bg()        { return juce::Colour (0xff0b0d10); }
inline juce::Colour panel()     { return juce::Colour (0xff12151a); }
inline juce::Colour panelHi()   { return juce::Colour (0xff191d24); }
inline juce::Colour well()      { return juce::Colour (0xff0e1014); }
inline juce::Colour line()      { return juce::Colour (0xff252a33); }
inline juce::Colour lineHi()    { return juce::Colour (0xff363d49); }
inline juce::Colour text()      { return juce::Colour (0xffd0d6de); }
inline juce::Colour dim()       { return juce::Colour (0xff77818f); }
inline juce::Colour faint()     { return juce::Colour (0xff4a525e); }
inline juce::Colour accent()    { return juce::Colour (0xffd2f53c); }     // phosphor lime
inline juce::Colour warn()      { return juce::Colour (0xffff5a4d); }
inline juce::Colour lock()      { return juce::Colour (0xffffb23e); }     // parameter locks
inline juce::Colour mod()       { return juce::Colour (0xff53d8ff); }     // modulation

inline juce::Colour track (int t)
{
    static const juce::uint32 c[8] = { 0xffff5d52, 0xffff9a3d, 0xfff3d34a, 0xff86e05c, 0xff3fd9be, 0xff4ea6ff, 0xffa283ff, 0xffff6fcf };
    return juce::Colour (c[((t % 8) + 8) % 8]);
}
} // namespace col

inline juce::Typeface::Ptr typeface (bool bold)
{
    static juce::Typeface::Ptr regular = juce::Typeface::createSystemTypefaceFor (AutomataAssets::JetBrainsMonoRegular_ttf, AutomataAssets::JetBrainsMonoRegular_ttfSize);
    static juce::Typeface::Ptr strong  = juce::Typeface::createSystemTypefaceFor (AutomataAssets::JetBrainsMonoBold_ttf, AutomataAssets::JetBrainsMonoBold_ttfSize);
    return bold ? strong : regular;
}

inline juce::Font font (float height, bool bold = false)
{
    return juce::Font (juce::FontOptions (typeface (bold)).withHeight (height));
}

inline float textWidth (const juce::Font& f, const juce::String& s) { return juce::GlyphArrangement::getStringWidth (f, s); }

// Small-caps style label
inline void drawLabel (juce::Graphics& g, const juce::String& s, juce::Rectangle<int> r, juce::Colour c, float height = 10.f,
                       juce::Justification j = juce::Justification::centred, bool bold = false)
{
    g.setColour (c);
    g.setFont (font (height, bold));
    g.drawText (s, r, j, false);
}

// Multi-line text that wraps inside r.
inline void drawWrapped (juce::Graphics& g, const juce::String& s, juce::Rectangle<int> r, juce::Colour c, float height = 9.f)
{
    g.setColour (c);
    g.setFont (font (height));
    g.drawFittedText (s, r, juce::Justification::topLeft, juce::jmax (1, (int) ((float) r.getHeight() / (height + 2.f))), 1.0f);
}

inline void fillPanel (juce::Graphics& g, juce::Rectangle<int> r, juce::Colour fill = col::panel(), float radius = 3.f)
{
    g.setColour (fill);
    g.fillRoundedRectangle (r.toFloat(), radius);
}

inline void strokePanel (juce::Graphics& g, juce::Rectangle<int> r, juce::Colour c = col::line(), float radius = 3.f)
{
    g.setColour (c);
    g.drawRoundedRectangle (r.toFloat().reduced (0.5f), radius, 1.f);
}

} // namespace ui
