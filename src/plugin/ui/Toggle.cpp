#include "Toggle.h"

namespace verdant::ui
{

Toggle::Toggle (UiContext& c, PID pid, Style s)
    : ParamControl (c, pid), style (s)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void Toggle::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;

    pressed = true;
    repaint();
}

void Toggle::mouseUp (const juce::MouseEvent& e)
{
    if (! pressed)
        return;

    pressed = false;

    if (getLocalBounds().contains (e.getPosition()))
        attachment.setValueAsCompleteGesture (isOn() ? 0.0f : 1.0f);

    repaint();
}

//==============================================================================
void Toggle::drawSwitch (juce::Graphics& g, juce::Rectangle<float> r) const
{
    const bool on = isOn();
    const float radius = r.getHeight() * 0.5f;
    const float line = juce::jmax (1.0f, ctx.scale);

    if (on)
    {
        g.setColour (accent.withAlpha (0.16f));
        g.fillRoundedRectangle (r.expanded (line * 2.0f), radius + line * 2.0f);
        g.setColour (accent.withAlpha (hovered ? 1.0f : 0.9f));
        g.fillRoundedRectangle (r, radius);
    }
    else
    {
        g.setColour (col::well);
        g.fillRoundedRectangle (r, radius);
        g.setColour (col::hairline.withAlpha (hovered ? 0.45f : 0.26f));
        g.drawRoundedRectangle (r.reduced (line * 0.5f), radius, line);
    }

    const float inset = juce::jmax (2.0f, r.getHeight() * 0.17f);
    const float d = r.getHeight() - inset * 2.0f;
    const float x = on ? r.getRight() - inset - d : r.getX() + inset;

    g.setColour (on ? juce::Colour (0xff0d1f17) : col::muted.withAlpha (hovered ? 1.0f : 0.8f));
    g.fillEllipse (x, r.getY() + inset, d, d);
}

void Toggle::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const bool on = isOn();

    switch (style)
    {
        case Style::power:
        {
            const float h = juce::jmin (b.getHeight(), ctx.px (14.0f));
            drawSwitch (g, b.withSizeKeepingCentre (juce::jmin (b.getWidth(), h * 1.86f), h));
            break;
        }

        case Style::labelled:
        {
            const auto cell = cellLayout (false);
            const auto capFont = ctx.font (Type::label);
            const auto valFont = ctx.font (Type::value);
            const auto capArea = cell.caption;
            const auto valArea = cell.value;

            const float h = juce::jmin (cell.body.getHeight() * 0.6f, ctx.px (17.0f));
            drawSwitch (g, cell.body.withSizeKeepingCentre (h * 1.9f, h));

            g.setFont (capFont);
            g.setColour (hovered ? col::text : col::muted);
            g.drawFittedText (caption, capArea.toNearestInt(), juce::Justification::centred, 1, 0.72f);

            g.setFont (valFont);
            g.setColour (on ? accent : col::muted.withAlpha (0.8f));
            g.drawFittedText (param.getCurrentValueAsText(), valArea.toNearestInt(), juce::Justification::centred, 1, 0.72f);
            break;
        }

        case Style::button:
        {
            const float line = juce::jmax (1.0f, ctx.scale);
            const auto r = b.reduced (line);
            const float radius = r.getHeight() * 0.5f;

            if (on)
            {
                g.setColour (accent.withAlpha (0.10f));
                g.fillRoundedRectangle (b, radius + line);
                g.setGradientFill (juce::ColourGradient (accent.withAlpha (0.30f), r.getX(), r.getY(),
                                                         accent.withAlpha (0.16f), r.getX(), r.getBottom(), false));
                g.fillRoundedRectangle (r, radius);
                g.setColour (accent.withAlpha (0.85f));
            }
            else
            {
                g.setColour (col::well.withAlpha (pressed ? 1.0f : 0.9f));
                g.fillRoundedRectangle (r, radius);
                g.setColour (col::hairline.withAlpha (hovered ? 0.42f : 0.2f));
            }
            g.drawRoundedRectangle (r.reduced (line * 0.5f), radius, line);

            const auto textCol = on ? accent.brighter (0.25f) : (hovered ? col::text : col::muted);
            const auto font = ctx.font (Type::section);
            const juce::String text = caption.toUpperCase();
            const float textW = juce::GlyphArrangement::getStringWidth (font, text);
            const float glyphSize = glyph == Glyph::none ? 0.0f : r.getHeight() * 0.42f;
            const float gap = glyph == Glyph::none ? 0.0f : ctx.px (7.0f);
            float x = r.getCentreX() - (textW + glyphSize + gap) * 0.5f;

            if (glyph != Glyph::none)
            {
                const juce::Rectangle<float> ga (x, r.getCentreY() - glyphSize * 0.5f, glyphSize, glyphSize);
                const auto gc = ga.getCentre();
                const float gr = glyphSize * 0.5f;
                const float stroke = juce::jmax (1.2f, glyphSize * 0.11f);
                juce::Path p;

                if (glyph == Glyph::crystal)
                {
                    for (int i = 0; i < 3; ++i)
                    {
                        const float a = juce::MathConstants<float>::pi * float (i) / 3.0f;
                        const float dx = std::sin (a) * gr, dy = std::cos (a) * gr;
                        p.startNewSubPath (gc.x - dx, gc.y - dy);
                        p.lineTo (gc.x + dx, gc.y + dy);

                        // little barbs
                        for (float sgn : { -1.0f, 1.0f })
                        {
                            const float mx = gc.x + sgn * dx * 0.62f, my = gc.y + sgn * dy * 0.62f;
                            const float bx = -dy * 0.26f, by = dx * 0.26f;
                            p.startNewSubPath (mx + bx + sgn * dx * 0.22f, my + by + sgn * dy * 0.22f);
                            p.lineTo (mx, my);
                            p.lineTo (mx - bx + sgn * dx * 0.22f, my - by + sgn * dy * 0.22f);
                        }
                    }
                }
                else
                {
                    p.addCentredArc (gc.x, gc.y, gr * 0.86f, gr * 0.86f, 0.0f, 0.6f,
                                     juce::MathConstants<float>::twoPi - 0.6f, true);
                    p.startNewSubPath (gc.x, gc.y - gr);
                    p.lineTo (gc.x, gc.y - gr * 0.1f);
                }

                g.setColour (textCol);
                g.strokePath (p, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                x += glyphSize + gap;
            }

            g.setFont (font);
            g.setColour (textCol);
            g.drawText (text, juce::Rectangle<float> (x, r.getY(), textW + ctx.px (4.0f), r.getHeight()),
                        juce::Justification::centredLeft, false);
            break;
        }
    }
}

} // namespace verdant::ui
