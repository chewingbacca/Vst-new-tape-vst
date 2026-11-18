#include "ChoicePill.h"

namespace verdant::ui
{

ChoicePill::ChoicePill (UiContext& c, PID pid)
    : ParamControl (c, pid)
{
    jassert (def.type == PType::Choice);
}

void ChoicePill::setIndex (int i)
{
    if (def.numChoices <= 0)
        return;

    i = juce::jlimit (0, def.numChoices - 1, i);
    if (i != getCurrentIndex())
        attachment.setValueAsCompleteGesture (float (i));
}

float ChoicePill::getDesignWidth() const
{
    // Measured at design scale so the layout is the same at every editor size.
    const auto font = makeFont (Type::value, 1.0f);
    float widest = juce::GlyphArrangement::getStringWidth (makeFont (Type::label, 1.0f), caption) - 6.0f;

    for (int i = 0; i < def.numChoices; ++i)
        widest = juce::jmax (widest, juce::GlyphArrangement::getStringWidth (font, def.choices[i]) + 30.0f);

    return juce::jlimit (58.0f, 150.0f, widest + 8.0f);
}

juce::Rectangle<float> ChoicePill::getPillBounds() const
{
    const auto body = cellLayout (false).body;
    const float h = juce::jmin (body.getHeight(), ctx.px (23.0f));
    return body.withSizeKeepingCentre (body.getWidth() - ctx.px (6.0f), h);
}

int ChoicePill::zoneAt (juce::Point<float> p) const
{
    const auto r = getPillBounds();
    if (! r.expanded (0.0f, ctx.px (4.0f)).contains (p))
        return 2;

    const float edge = juce::jmin (r.getWidth() * 0.28f, ctx.px (20.0f));
    if (p.x < r.getX() + edge)       return -1;
    if (p.x > r.getRight() - edge)   return 1;
    return 0;
}

void ChoicePill::mouseMove (const juce::MouseEvent& e)
{
    const int z = zoneAt (e.position);
    if (z != hoverZone)
    {
        hoverZone = z;
        repaint();
    }
}

void ChoicePill::mouseExit (const juce::MouseEvent& e)
{
    hoverZone = 2;
    ParamControl::mouseExit (e);
}

void ChoicePill::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() || def.numChoices <= 0)
        return;

    const int n = def.numChoices;

    switch (zoneAt (e.position))
    {
        case -1: setIndex ((getCurrentIndex() + n - 1) % n); break;
        case 1:  setIndex ((getCurrentIndex() + 1) % n);     break;
        case 0:
            if (ctx.showChoices)
                ctx.showChoices (*this);
            break;
        default: break;
    }
}

void ChoicePill::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    const float amount = wheel.deltaY * (wheel.isReversed ? -1.0f : 1.0f);
    wheelAccum += amount * 3.2f;
    const float whole = std::trunc (wheelAccum);

    if (std::abs (whole) >= 1.0f)
    {
        wheelAccum -= whole;
        setIndex (getCurrentIndex() + int (whole));
    }
}

void ChoicePill::paint (juce::Graphics& g)
{
    const auto capFont = ctx.font (Type::label);
    const auto valFont = ctx.font (Type::value);

    g.setFont (capFont);
    g.setColour (hovered ? col::text : col::muted);
    g.drawFittedText (caption, cellLayout (false).caption.toNearestInt(), juce::Justification::centred, 1, 0.72f);

    const auto r = getPillBounds();
    const float radius = r.getHeight() * 0.5f;
    drawWell (g, r, radius, ctx.scale, popupOpen || (hovered && hoverZone == 0), accent);

    const float edge = juce::jmin (r.getWidth() * 0.28f, ctx.px (20.0f));
    const float chev = r.getHeight() * 0.42f;
    const float thick = juce::jmax (1.2f, ctx.px (1.4f));

    auto left = juce::Rectangle<float> (r.getX() + edge * 0.18f, r.getY(), edge * 0.82f, r.getHeight()).withSizeKeepingCentre (chev, chev);
    auto right = juce::Rectangle<float> (r.getRight() - edge, r.getY(), edge * 0.82f, r.getHeight()).withSizeKeepingCentre (chev, chev);

    drawChevron (g, left, false, hovered && hoverZone == -1 ? accent : col::muted.withAlpha (hovered ? 0.85f : 0.5f), thick);
    drawChevron (g, right, true, hovered && hoverZone == 1 ? accent : col::muted.withAlpha (hovered ? 0.85f : 0.5f), thick);

    g.setFont (valFont);
    g.setColour (col::text);
    g.drawFittedText (param.getCurrentValueAsText(),
                      r.withTrimmedLeft (edge * 0.9f).withTrimmedRight (edge * 0.9f).toNearestInt(),
                      juce::Justification::centred, 1, 0.7f);
}

//==============================================================================
ChoicePopup::ChoicePopup (UiContext& c) : ctx (c)
{
    setVisible (false);
    setWantsKeyboardFocus (true);
    setInterceptsMouseClicks (true, false);
}

void ChoicePopup::show (ChoicePill& pill)
{
    if (target != nullptr)
        target->setPopupOpen (false);

    target = &pill;
    pill.setPopupOpen (true);
    hoverItem = -1;
    updateGeometry();
    setVisible (true);
    toFront (false);

    if (isShowing())
        grabKeyboardFocus();

    repaint();
}

void ChoicePopup::dismiss()
{
    if (target != nullptr)
        target->setPopupOpen (false);

    target = nullptr;
    setVisible (false);
}

void ChoicePopup::resized()
{
    if (isVisible())
        updateGeometry();
}

void ChoicePopup::updateGeometry()
{
    if (target == nullptr)
        return;

    const auto font = ctx.font (Type::value);
    const int n = target->getNumChoices();
    itemHeight = juce::jmax (font.getHeight() * 1.75f, ctx.px (23.0f));

    float widest = 0.0f;
    for (int i = 0; i < n; ++i)
        widest = juce::jmax (widest, juce::GlyphArrangement::getStringWidth (font, target->getChoiceName (i)));

    const auto pill = getLocalArea (target.getComponent(), target->getPillBounds().toNearestInt()).toFloat();
    const float pad = ctx.px (5.0f);
    const float w = juce::jmax (pill.getWidth(), widest + ctx.px (44.0f));
    const float h = itemHeight * float (n) + pad * 2.0f;
    const float margin = ctx.px (6.0f);
    const auto bounds = getLocalBounds().toFloat().reduced (margin);

    float x = pill.getCentreX() - w * 0.5f;
    float y = pill.getBottom() + ctx.px (5.0f);

    if (y + h > bounds.getBottom())
    {
        const float above = pill.getY() - ctx.px (5.0f) - h;
        y = above >= bounds.getY() ? above : bounds.getBottom() - h;
    }

    x = juce::jlimit (bounds.getX(), juce::jmax (bounds.getX(), bounds.getRight() - w), x);
    y = juce::jmax (bounds.getY(), y);
    list = { x, y, w, juce::jmin (h, bounds.getHeight()) };
    itemHeight = (list.getHeight() - pad * 2.0f) / float (juce::jmax (1, n));
}

int ChoicePopup::itemAt (juce::Point<float> p) const
{
    if (target == nullptr || ! list.contains (p))
        return -1;

    const float pad = ctx.px (5.0f);
    const int i = int (std::floor ((p.y - list.getY() - pad) / itemHeight));
    return juce::isPositiveAndBelow (i, target->getNumChoices()) ? i : -1;
}

void ChoicePopup::mouseMove (const juce::MouseEvent& e)
{
    const int i = itemAt (e.position);
    if (i != hoverItem)
    {
        hoverItem = i;
        repaint();
    }
}

void ChoicePopup::mouseExit (const juce::MouseEvent&)
{
    if (hoverItem != -1)
    {
        hoverItem = -1;
        repaint();
    }
}

void ChoicePopup::mouseDown (const juce::MouseEvent& e)
{
    const int i = itemAt (e.position);

    if (i >= 0 && target != nullptr)
        target->setIndex (i);

    if (i >= 0 || ! list.contains (e.position))
        dismiss();
}

bool ChoicePopup::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey || key == juce::KeyPress::returnKey)
    {
        dismiss();
        return true;
    }

    if (target != nullptr && (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey))
    {
        target->setIndex (target->getCurrentIndex() + (key == juce::KeyPress::downKey ? 1 : -1));
        repaint();
        return true;
    }

    return false;
}

void ChoicePopup::paint (juce::Graphics& g)
{
    if (target == nullptr)
        return;

    const float radius = ctx.px (9.0f);
    drawSoftShadow (g, list, radius, ctx.px (14.0f), 0.38f);
    drawPanel (g, list, radius, ctx.scale, juce::Colour (0xff17352a), 0.22f);

    const auto font = ctx.font (Type::value);
    const auto accent = target->getAccent();
    const float pad = ctx.px (5.0f);
    const int current = target->getCurrentIndex();

    g.setFont (font);

    for (int i = 0; i < target->getNumChoices(); ++i)
    {
        const juce::Rectangle<float> row (list.getX() + pad, list.getY() + pad + itemHeight * float (i),
                                          list.getWidth() - pad * 2.0f, itemHeight);
        const bool isCurrent = i == current;

        if (i == hoverItem)
        {
            g.setColour (col::hairline.withAlpha (0.10f));
            g.fillRoundedRectangle (row.reduced (0.0f, 0.5f), juce::jmin (row.getHeight() * 0.5f, ctx.px (6.0f)));
        }

        const float dot = juce::jmax (3.0f, ctx.px (4.5f));
        if (isCurrent)
        {
            g.setColour (accent);
            g.fillEllipse (row.getX() + ctx.px (8.0f), row.getCentreY() - dot * 0.5f, dot, dot);
        }

        g.setColour (isCurrent ? accent.brighter (0.1f) : (i == hoverItem ? col::text : col::text.withAlpha (0.78f)));
        g.drawText (target->getChoiceName (i), row.withTrimmedLeft (ctx.px (20.0f)).withTrimmedRight (ctx.px (8.0f)),
                    juce::Justification::centredLeft, true);
    }
}

} // namespace verdant::ui
