#include "Widgets.h"

namespace verdant::ui
{

//==============================================================================
PillButton::PillButton (UiContext& c, const juce::String& t, Icon i)
    : ctx (c), text (t), icon (i)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTitle (t);
}

PillButton::~PillButton()
{
    ctx.clearHover (this);
}

void PillButton::mouseEnter (const juce::MouseEvent&)
{
    hovered = true;
    if (helpTitle.isNotEmpty() || helpText.isNotEmpty())
        ctx.setHover (this, helpTitle, nullptr, helpText);
    repaint();
}

void PillButton::mouseExit (const juce::MouseEvent&)
{
    hovered = false;
    ctx.clearHover (this);
    repaint();
}

void PillButton::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() || ! isEnabled())
        return;

    pressed = true;
    repaint();
}

void PillButton::mouseUp (const juce::MouseEvent& e)
{
    if (! pressed)
        return;

    pressed = false;
    repaint();

    if (getLocalBounds().contains (e.getPosition()) && onClick)
        onClick();
}

void PillButton::paint (juce::Graphics& g)
{
    const float line = juce::jmax (1.0f, ctx.scale);
    const auto b = getLocalBounds().toFloat().reduced (line * 0.5f);
    const float radius = b.getHeight() * 0.5f;
    const bool enabled = isEnabled();
    const bool lit = hovered && enabled;

    if (icon == Icon::none)
    {
        g.setGradientFill (juce::ColourGradient (accent.withAlpha (lit ? 0.30f : 0.18f), b.getX(), b.getY(),
                                                 accent.withAlpha (lit ? 0.18f : 0.09f), b.getX(), b.getBottom(), false));
        g.fillRoundedRectangle (b, radius);
        g.setColour (accent.withAlpha (enabled ? (lit ? 0.85f : 0.5f) : 0.2f));
        g.drawRoundedRectangle (b, radius, line);

        g.setFont (ctx.font (Type::section));
        g.setColour (enabled ? accent.brighter (lit ? 0.35f : 0.15f) : col::faint);
        g.drawText (text.toUpperCase(), b.translated (0.0f, pressed ? line : 0.0f), juce::Justification::centred, false);
        return;
    }

    g.setColour (col::well.withAlpha (lit ? 1.0f : 0.75f));
    g.fillRoundedRectangle (b, radius);
    g.setColour (col::hairline.withAlpha (lit ? 0.4f : 0.16f));
    g.drawRoundedRectangle (b, radius, line);

    const auto colour = enabled ? (lit ? accent : col::muted) : col::faint;
    const float s = b.getHeight() * 0.36f;
    const auto ib = b.withSizeKeepingCentre (s, s);
    const float thick = juce::jmax (1.3f, ctx.px (1.5f));

    switch (icon)
    {
        case Icon::close:
        {
            juce::Path p;
            p.startNewSubPath (ib.getTopLeft());      p.lineTo (ib.getBottomRight());
            p.startNewSubPath (ib.getTopRight());     p.lineTo (ib.getBottomLeft());
            g.setColour (colour);
            g.strokePath (p, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            break;
        }
        case Icon::previous:  drawChevron (g, ib.expanded (s * 0.2f), false, colour, thick); break;
        case Icon::next:      drawChevron (g, ib.expanded (s * 0.2f), true, colour, thick);  break;
        case Icon::none:      break;
    }
}

//==============================================================================
TabSwitch::TabSwitch (UiContext& c, const juce::StringArray& n, const juce::StringArray& h)
    : ctx (c), names (n), help (h)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

TabSwitch::~TabSwitch()
{
    ctx.clearHover (this);
}

void TabSwitch::setSelected (int index, bool notify)
{
    index = juce::jlimit (0, juce::jmax (0, names.size() - 1), index);
    if (index == selected)
        return;

    selected = index;
    repaint();

    if (notify && onChange)
        onChange (selected);
}

juce::Rectangle<float> TabSwitch::tabBounds (int index) const
{
    const auto b = getLocalBounds().toFloat();
    const float w = b.getWidth() / float (juce::jmax (1, names.size()));
    return { b.getX() + w * float (index), b.getY(), w, b.getHeight() };
}

int TabSwitch::tabAt (juce::Point<float> p) const
{
    for (int i = 0; i < names.size(); ++i)
        if (tabBounds (i).contains (p))
            return i;
    return -1;
}

void TabSwitch::mouseMove (const juce::MouseEvent& e)
{
    const int t = tabAt (e.position);
    if (t != hoverTab)
    {
        hoverTab = t;
        if (t >= 0)
            ctx.setHover (this, names[t], nullptr, help[t]);
        else
            ctx.clearHover (this);
        repaint();
    }
}

void TabSwitch::mouseExit (const juce::MouseEvent&)
{
    hoverTab = -1;
    ctx.clearHover (this);
    repaint();
}

void TabSwitch::mouseDown (const juce::MouseEvent& e)
{
    const int t = tabAt (e.position);
    if (t >= 0)
        setSelected (t, true);
}

void TabSwitch::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const float line = juce::jmax (1.0f, ctx.scale);
    const float radius = b.getHeight() * 0.5f;

    g.setColour (col::well.withAlpha (0.8f));
    g.fillRoundedRectangle (b, radius);
    g.setColour (col::hairline.withAlpha (0.14f));
    g.drawRoundedRectangle (b.reduced (line * 0.5f), radius, line);

    const auto font = ctx.font (Type::section);
    g.setFont (font);

    for (int i = 0; i < names.size(); ++i)
    {
        const auto tb = tabBounds (i);
        const bool sel = i == selected;

        if (sel)
        {
            const auto pill = tb.reduced (line * 2.5f);
            g.setGradientFill (juce::ColourGradient (col::leaf.withAlpha (0.26f), pill.getX(), pill.getY(),
                                                     col::leaf.withAlpha (0.13f), pill.getX(), pill.getBottom(), false));
            g.fillRoundedRectangle (pill, pill.getHeight() * 0.5f);
            g.setColour (col::leaf.withAlpha (0.45f));
            g.drawRoundedRectangle (pill, pill.getHeight() * 0.5f, line);
        }

        g.setColour (sel ? col::leaf.brighter (0.3f) : (i == hoverTab ? col::text : col::muted));
        g.drawText (names[i].toUpperCase(), tb, juce::Justification::centred, false);
    }
}

//==============================================================================
StatusLine::StatusLine (UiContext& c) : ctx (c)
{
    setInterceptsMouseClicks (false, false);
}

void StatusLine::setHover (juce::Component* o, const juce::String& t, juce::RangedAudioParameter* p, const juce::String& h)
{
    owner = o;
    source = p;
    title = t;
    helpText = h;
    valueText = source != nullptr ? source->getCurrentValueAsText() : juce::String();
    repaint();
}

void StatusLine::clearHover (juce::Component* o)
{
    if (o != owner || owner == nullptr)
        return;

    owner = nullptr;
    source = nullptr;
    title.clear();
    valueText.clear();
    helpText.clear();
    repaint();
}

void StatusLine::setIdleText (const juce::String& t)
{
    if (t != idleText)
    {
        idleText = t;
        repaint();
    }
}

void StatusLine::setRightText (const juce::String& t)
{
    if (t != rightText)
    {
        rightText = t;
        repaint();
    }
}

void StatusLine::refresh()
{
    if (source == nullptr)
        return;

    auto v = source->getCurrentValueAsText();
    if (v != valueText)
    {
        valueText = std::move (v);
        repaint();
    }
}

void StatusLine::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    const float line = juce::jmax (1.0f, ctx.scale);

    g.setColour (col::hairline.withAlpha (0.08f));
    g.fillRect (b.withHeight (line));

    b = b.withTrimmedLeft (ctx.px (20.0f)).withTrimmedRight (ctx.px (30.0f));

    const auto bodyFont = ctx.font (Type::body);
    const auto smallFont = ctx.font (Type::small);

    if (rightText.isNotEmpty())
    {
        const float w = juce::GlyphArrangement::getStringWidth (smallFont, rightText) + ctx.px (6.0f);
        g.setFont (smallFont);
        g.setColour (col::faint);
        g.drawText (rightText, b.removeFromRight (w), juce::Justification::centredRight, false);
        b.removeFromRight (ctx.px (16.0f));
    }

    const float glyph = ctx.px (10.0f);
    drawLeafGlyph (g, { b.getX(), b.getCentreY() - glyph * 0.5f, glyph, glyph },
                   (title.isEmpty() ? col::faint : col::leaf).withAlpha (0.9f));
    b.removeFromLeft (glyph + ctx.px (9.0f));

    if (title.isEmpty())
    {
        g.setFont (bodyFont);
        g.setColour (col::muted.withAlpha (0.8f));
        g.drawText (idleText, b, juce::Justification::centredLeft, true);
        return;
    }

    const auto titleFont = ctx.font (Type::section);
    const auto titleText = title.toUpperCase();
    const float tw = juce::GlyphArrangement::getStringWidth (titleFont, titleText);
    g.setFont (titleFont);
    g.setColour (col::leaf);
    g.drawText (titleText, b.removeFromLeft (tw + ctx.px (12.0f)), juce::Justification::centredLeft, false);

    g.setFont (bodyFont);

    if (valueText.isNotEmpty())
    {
        const float vw = juce::GlyphArrangement::getStringWidth (bodyFont, valueText);
        g.setColour (col::text);
        g.drawText (valueText, b.removeFromLeft (vw + ctx.px (4.0f)), juce::Justification::centredLeft, false);

        const float d = juce::jmax (2.0f, ctx.px (3.0f));
        auto dotArea = b.removeFromLeft (ctx.px (18.0f));
        g.setColour (col::faint);
        g.fillEllipse (dotArea.getCentreX() - d * 0.5f, dotArea.getCentreY() - d * 0.5f, d, d);
    }

    g.setColour (col::muted);
    g.drawText (helpText, b, juce::Justification::centredLeft, true);
}

} // namespace verdant::ui
