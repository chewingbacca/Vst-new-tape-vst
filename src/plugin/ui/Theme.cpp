#include "Theme.h"

namespace verdant::ui
{

//==============================================================================
juce::Font makeFont (Type t, float scale)
{
    struct Spec { float height; bool bold; float tracking; float minHeight; };

    Spec s { 12.0f, false, 0.0f, 8.5f };

    switch (t)
    {
        case Type::wordmark:   s = { 25.0f, false, 0.045f, 14.0f }; break;
        case Type::title:      s = { 15.0f, false, 0.02f,  11.0f }; break;
        case Type::section:    s = { 11.5f, true,  0.16f,   8.6f }; break;
        case Type::macroName:  s = { 12.0f, true,  0.18f,   9.0f }; break;
        case Type::macroValue: s = { 14.5f, false, 0.02f,  10.5f }; break;
        case Type::label:      s = { 11.5f, false, 0.01f,   8.8f }; break;
        case Type::value:      s = { 11.5f, false, 0.0f,    8.8f }; break;
        case Type::body:       s = { 12.5f, false, 0.01f,   9.5f }; break;
        case Type::small:      s = { 9.5f,  true,  0.16f,   7.6f }; break;
    }

    const float h = juce::jmax (s.minHeight, s.height * scale);

    return juce::Font (juce::FontOptions()
                           .withHeight (h)
                           .withStyle (s.bold ? "Bold" : "Regular")
                           .withKerningFactor (s.tracking));
}

juce::Colour accentForGroup (juce::StringRef group)
{
    struct Entry { const char* name; juce::uint32 argb; };
    static constexpr Entry table[] = {
        { "Macro",  0xffbfe6a0 },
        { "Global", 0xffcfe3b4 },
        { "Tape",   0xfff2d68a },
        { "Grain",  0xffbfe6a0 },
        { "Arp",    0xfff0b3c2 },
        { "Chorus", 0xff9fe3cf },
        { "Reverb", 0xffa9d4f0 },
        { "Output", 0xffd8e6c4 },
    };

    for (const auto& e : table)
        if (group == juce::StringRef (e.name))
            return juce::Colour (e.argb);

    return col::leaf;
}

bool isBipolar (const ParamDef& d)
{
    return (d.type == PType::Float || d.type == PType::Int)
        && d.min < 0.0f && d.max > 0.0f && std::abs (d.def) < 1.0e-6f;
}

//==============================================================================
void addLeaf (juce::Path& p, float bx, float by, float dx, float dy, float len, float hw)
{
    const float nx = -dy, ny = dx;
    const auto X = [&] (float u, float v) { return bx + (dx * u + nx * v * hw) * len; };
    const auto Y = [&] (float u, float v) { return by + (dy * u + ny * v * hw) * len; };

    p.startNewSubPath (bx, by);
    p.cubicTo (X (0.18f, -1.10f), Y (0.18f, -1.10f), X (0.62f, -0.95f), Y (0.62f, -0.95f), X (1.0f, 0.0f), Y (1.0f, 0.0f));
    p.cubicTo (X (0.70f, 0.80f), Y (0.70f, 0.80f), X (0.22f, 1.05f), Y (0.22f, 1.05f), bx, by);
    p.closeSubPath();
}

void drawLeafGlyph (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour c, float veinAlpha)
{
    const float size = juce::jmin (area.getWidth(), area.getHeight());
    const auto centre = area.getCentre();

    // Tip up-right, stalk down-left.
    const float dx = 0.64f, dy = -0.77f;
    const float len = size * 0.98f;
    const float bx = centre.x - dx * len * 0.5f;
    const float by = centre.y - dy * len * 0.5f;

    juce::Path leaf;
    addLeaf (leaf, bx, by, dx, dy, len, 0.36f);

    g.setGradientFill (juce::ColourGradient (c.brighter (0.25f), bx + dx * len, by + dy * len,
                                             c.darker (0.25f), bx, by, false));
    g.fillPath (leaf);

    juce::Path vein;
    vein.startNewSubPath (bx - dx * len * 0.12f, by - dy * len * 0.12f);
    vein.lineTo (bx + dx * len * 0.80f, by + dy * len * 0.80f);
    g.setColour (col::bgTop.withAlpha (veinAlpha));
    g.strokePath (vein, juce::PathStrokeType (juce::jmax (1.0f, size * 0.07f), juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));
}

void drawPanel (juce::Graphics& g, juce::Rectangle<float> area, float corner, float scale,
                juce::Colour fill, float borderAlpha)
{
    g.setGradientFill (juce::ColourGradient (fill.brighter (0.035f), area.getX(), area.getY(),
                                             fill.darker (0.10f), area.getX(), area.getBottom(), false));
    g.fillRoundedRectangle (area, corner);

    const float line = juce::jmax (1.0f, scale);
    g.setGradientFill (juce::ColourGradient (col::hairline.withAlpha (borderAlpha * 1.5f), area.getX(), area.getY(),
                                             col::hairline.withAlpha (borderAlpha * 0.55f), area.getX(), area.getBottom(), false));
    g.drawRoundedRectangle (area.reduced (line * 0.5f), corner, line);
}

void drawWell (juce::Graphics& g, juce::Rectangle<float> area, float corner, float scale,
               bool highlighted, juce::Colour accent)
{
    g.setColour (col::well.withAlpha (0.92f));
    g.fillRoundedRectangle (area, corner);

    const float line = juce::jmax (1.0f, scale);
    g.setColour (highlighted ? accent.withAlpha (0.55f) : col::hairline.withAlpha (0.16f));
    g.drawRoundedRectangle (area.reduced (line * 0.5f), corner, line);
}

void drawSoftShadow (juce::Graphics& g, juce::Rectangle<float> area, float corner, float spread, float alpha)
{
    constexpr int steps = 6;
    for (int i = steps; i >= 1; --i)
    {
        const float f = float (i) / float (steps);
        g.setColour (juce::Colours::black.withAlpha (alpha * (1.0f - f) * 0.5f + alpha * 0.06f));
        g.fillRoundedRectangle (area.expanded (spread * f).translated (0.0f, spread * 0.35f), corner + spread * f);
    }
}

void drawChevron (juce::Graphics& g, juce::Rectangle<float> area, bool right, juce::Colour c, float thickness)
{
    const auto ctr = area.getCentre();
    const float h = juce::jmin (area.getHeight(), area.getWidth() * 2.0f) * 0.5f;
    const float w = h * 0.5f;
    const float dir = right ? 1.0f : -1.0f;

    juce::Path p;
    p.startNewSubPath (ctr.x - dir * w * 0.5f, ctr.y - h * 0.5f);
    p.lineTo (ctr.x + dir * w * 0.5f, ctr.y);
    p.lineTo (ctr.x - dir * w * 0.5f, ctr.y + h * 0.5f);

    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::ColourGradient backdropGradient (juce::Rectangle<float> b)
{
    juce::ColourGradient grad (col::bgTop, b.getX(), b.getY(), col::bgBottom, b.getX(), b.getBottom(), false);
    grad.addColour (0.55, juce::Colour (0xff0e1f18));
    return grad;
}

//==============================================================================
VerdantLookAndFeel::VerdantLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, col::bgTop);
    setColour (juce::TextEditor::backgroundColourId, col::well);
    setColour (juce::TextEditor::textColourId, col::text);
    setColour (juce::TextEditor::highlightColourId, col::leaf.withAlpha (0.28f));
    setColour (juce::TextEditor::highlightedTextColourId, col::text);
    setColour (juce::TextEditor::outlineColourId, col::hairline.withAlpha (0.18f));
    setColour (juce::TextEditor::focusedOutlineColourId, col::leaf.withAlpha (0.6f));
    setColour (juce::CaretComponent::caretColourId, col::leaf);
    setColour (juce::ScrollBar::thumbColourId, col::leaf.withAlpha (0.35f));
}

void VerdantLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height,
                                        bool vertical, int thumbStart, int thumbSize, bool over, bool down)
{
    const auto track = juce::Rectangle<int> (x, y, width, height).toFloat();
    const float thickness = (vertical ? track.getWidth() : track.getHeight()) * 0.42f;

    auto bar = vertical ? track.withSizeKeepingCentre (thickness, track.getHeight())
                        : track.withSizeKeepingCentre (track.getWidth(), thickness);

    g.setColour (col::hairline.withAlpha (0.06f));
    g.fillRoundedRectangle (bar, thickness * 0.5f);

    if (thumbSize > 0)
    {
        auto thumb = vertical ? bar.withY (float (thumbStart)).withHeight (float (thumbSize))
                              : bar.withX (float (thumbStart)).withWidth (float (thumbSize));
        g.setColour (col::leaf.withAlpha (down ? 0.65f : (over ? 0.5f : 0.32f)));
        g.fillRoundedRectangle (thumb, thickness * 0.5f);
    }
}

void VerdantLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& ed)
{
    const auto r = juce::Rectangle<int> (width, height).toFloat();
    g.setColour (ed.findColour (juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle (r, float (height) * 0.5f);
}

void VerdantLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& ed)
{
    const auto r = juce::Rectangle<int> (width, height).toFloat().reduced (0.5f);
    const bool focused = ed.hasKeyboardFocus (true) && ! ed.isReadOnly();
    g.setColour (ed.findColour (focused ? juce::TextEditor::focusedOutlineColourId : juce::TextEditor::outlineColourId));
    g.drawRoundedRectangle (r, float (height) * 0.5f, 1.0f);
}

void VerdantLookAndFeel::drawCornerResizer (juce::Graphics& g, int w, int h, bool over, bool dragging)
{
    const float alpha = dragging ? 0.75f : (over ? 0.55f : 0.28f);
    g.setColour (col::leaf.withAlpha (alpha));

    const float fw = float (w), fh = float (h);
    const float d = juce::jmax (1.5f, fw * 0.12f);

    // Three dots along the diagonal plus two inner ones: a small seed cluster.
    const juce::Point<float> pts[] = { { 0.78f, 0.78f }, { 0.78f, 0.48f }, { 0.48f, 0.78f },
                                       { 0.78f, 0.18f }, { 0.18f, 0.78f } };
    for (const auto& p : pts)
        g.fillEllipse (p.x * fw - d * 0.5f, p.y * fh - d * 0.5f, d, d);
}

} // namespace verdant::ui
