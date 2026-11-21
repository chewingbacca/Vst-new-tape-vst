#include "Widgets.h"

using namespace juce;

// ================================================================ Knob
Knob::Knob (AudioProcessorValueTreeState& state, const String& paramId, const String& labelText, Colour c, bool isBig)
    : param (*state.getParameter (paramId)),
      attachment (param, [this] (float v) { value = param.convertTo0to1 (v); repaint(); }),
      label (labelText), colour (c), big (isBig)
{
    attachment.sendInitialUpdate();
    setMouseCursor (MouseCursor::UpDownResizeCursor);
}

void Knob::setNormalised (float v)
{
    attachment.setValueAsPartOfGesture (param.convertFrom0to1 (jlimit (0.0f, 1.0f, v)));
}

void Knob::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const float labelH = big ? 20.0f : 14.0f;
    auto labelArea = r.removeFromBottom (labelH);
    const float size = jmin (r.getWidth(), r.getHeight()) - (big ? 6.0f : 4.0f);
    const auto c = r.getCentre();
    const float radius = size * 0.5f;
    const float a0 = MathConstants<float>::pi * 1.25f, a1 = MathConstants<float>::pi * 2.75f;
    const float av = a0 + (a1 - a0) * value;
    const float thick = big ? 6.0f : 3.5f;

    // activity glow
    if (activity > 0.01f)
    {
        g.setColour (colour.withAlpha (0.05f + 0.10f * activity));
        g.fillEllipse (Rectangle<float> (size + 6.0f, size + 6.0f).withCentre (c));
        g.setColour (colour.withAlpha (0.55f * activity));
        g.drawEllipse (Rectangle<float> (size + 7.0f, size + 7.0f).withCentre (c), 1.5f);
    }
    // track
    Path track; track.addCentredArc (c.x, c.y, radius - thick * 0.5f, radius - thick * 0.5f, 0.0f, a0, a1, true);
    g.setColour (theme::grid.brighter (0.1f));
    g.strokePath (track, PathStrokeType (thick, PathStrokeType::curved, PathStrokeType::butt));
    // value arc
    Path arc;
    const float from = bipolar ? (a0 + a1) * 0.5f : a0;
    arc.addCentredArc (c.x, c.y, radius - thick * 0.5f, radius - thick * 0.5f, 0.0f, jmin (from, av), jmax (from, av), true);
    g.setColour (colour.withMultipliedBrightness (hover || dragging ? 1.15f : 1.0f));
    g.strokePath (arc, PathStrokeType (thick, PathStrokeType::curved, PathStrokeType::butt));

    // body: a faceted disc with a sharp pointer
    const float inner = radius - thick - (big ? 5.0f : 3.0f);
    Path body;
    const int facets = big ? 10 : 8;
    for (int i = 0; i < facets; ++i)
    {
        const float ang = av + MathConstants<float>::twoPi * (float) i / (float) facets;
        const Point<float> p (c.x + inner * std::sin (ang), c.y - inner * std::cos (ang));
        if (i == 0) body.startNewSubPath (p); else body.lineTo (p);
    }
    body.closeSubPath();
    g.setGradientFill (ColourGradient (theme::panelHi.brighter (0.18f), c.x, c.y - inner, theme::panel.darker (0.2f), c.x, c.y + inner, false));
    g.fillPath (body);
    g.setColour (theme::outline.brighter (0.2f)); g.strokePath (body, PathStrokeType (1.0f));
    Path pointer;
    const float pw = big ? 3.2f : 2.2f;
    pointer.addTriangle (c.x + inner * std::sin (av), c.y - inner * std::cos (av),
                         c.x + pw * std::cos (av) + inner * 0.25f * std::sin (av), c.y + pw * std::sin (av) - inner * 0.25f * std::cos (av),
                         c.x - pw * std::cos (av) + inner * 0.25f * std::sin (av), c.y - pw * std::sin (av) - inner * 0.25f * std::cos (av));
    g.setColour (colour); g.fillPath (pointer);

    // label, or the value while it is being touched
    const String txt = (hover || dragging) ? param.getCurrentValueAsText() : label;
    g.setColour ((hover || dragging) ? theme::text : (big ? colour : theme::textDim));
    g.setFont (theme::font (big ? 13.0f : 10.0f, true));
    g.drawText (txt, labelArea.toNearestInt(), Justification::centred, false);
}

void Knob::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu()) return;
    dragging = true; dragStart = value;
    attachment.beginGesture();
}

void Knob::mouseDrag (const MouseEvent& e)
{
    if (! dragging) return;
    const float sens = e.mods.isShiftDown() ? 0.0012f : 0.0055f;
    setNormalised (dragStart - (float) e.getDistanceFromDragStartY() * sens + (float) e.getDistanceFromDragStartX() * sens * 0.35f);
}

void Knob::mouseUp (const MouseEvent&)
{
    if (dragging) { attachment.endGesture(); dragging = false; repaint(); }
}

void Knob::mouseDoubleClick (const MouseEvent&)
{
    attachment.setValueAsCompleteGesture (param.convertFrom0to1 (param.getDefaultValue()));
}

void Knob::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    const int steps = param.getNumSteps();
    const float inc = (steps > 1 && steps < 200) ? 1.0f / (float) (steps - 1) : (e.mods.isShiftDown() ? 0.004f : 0.02f);
    const float dir = w.deltaY > 0 ? 1.0f : (w.deltaY < 0 ? -1.0f : 0.0f);
    if (dir != 0.0f) attachment.setValueAsCompleteGesture (param.convertFrom0to1 (jlimit (0.0f, 1.0f, value + dir * inc)));
}

// ================================================================ ChoiceBox
ChoiceBox::ChoiceBox (AudioProcessorValueTreeState& state, const String& paramId, const String& labelText, Colour c)
    : param (*state.getParameter (paramId)),
      attachment (param, [this] (float v) { index = roundToInt (param.convertTo0to1 (v) * (float) jmax (1, numItems - 1)); repaint(); }),
      label (labelText), colour (c)
{
    numItems = jmax (2, param.getNumSteps());
    attachment.sendInitialUpdate();
    setMouseCursor (MouseCursor::PointingHandCursor);
}

String ChoiceBox::itemText (int i) const
{
    if (textForIndex) return textForIndex (i);
    return param.getText ((float) i / (float) jmax (1, numItems - 1), 64);
}

void ChoiceBox::setIndex (int i)
{
    i = ((i % numItems) + numItems) % numItems;
    attachment.setValueAsCompleteGesture (param.convertFrom0to1 ((float) i / (float) jmax (1, numItems - 1)));
}

void ChoiceBox::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    const bool hasLabel = label.isNotEmpty() && getHeight() > 30;
    if (hasLabel)
    {
        theme::drawLabel (g, label, r.removeFromTop (12.0f).toNearestInt(), theme::textFaint, 9.0f, Justification::centredLeft);
        r.removeFromTop (1.0f);
    }
    const auto p = theme::chamfer (r, 5.0f);
    g.setColour (hover ? theme::panelHi.brighter (0.08f) : theme::panelHi); g.fillPath (p);
    g.setColour (colour.withAlpha (hover ? 0.9f : 0.55f)); g.strokePath (p, PathStrokeType (1.0f));
    g.setColour (theme::text); g.setFont (theme::font (11.0f, true));
    g.drawFittedText (itemText (index), r.reduced (6.0f, 0.0f).withTrimmedRight (8.0f).toNearestInt(), Justification::centredLeft, 1, 0.8f);
    Path tri; const float cx = r.getRight() - 9.0f, cy = r.getCentreY();
    tri.addTriangle (cx - 3.5f, cy - 2.0f, cx + 3.5f, cy - 2.0f, cx, cy + 3.0f);
    g.setColour (colour); g.fillPath (tri);
}

void ChoiceBox::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu() || menuOnly || numItems > 4)
    {
        PopupMenu m;
        for (int i = 0; i < numItems; ++i) m.addItem (i + 1, itemText (i), true, i == index);
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (this), [safe = Component::SafePointer<ChoiceBox> (this)] (int r) { if (safe != nullptr && r > 0) safe->setIndex (r - 1); });
    }
    else setIndex (index + 1);
}

void ChoiceBox::mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w)
{
    if (w.deltaY > 0 && index > 0) setIndex (index - 1);
    else if (w.deltaY < 0 && index < numItems - 1) setIndex (index + 1);
}

// ================================================================ ToggleBox
ToggleBox::ToggleBox (AudioProcessorValueTreeState& state, const String& paramId, const String& labelText, Colour c)
    : param (*state.getParameter (paramId)),
      attachment (param, [this] (float v) { on = v > 0.5f; repaint(); }),
      label (labelText), colour (c)
{
    attachment.sendInitialUpdate();
    setMouseCursor (MouseCursor::PointingHandCursor);
}

void ToggleBox::paint (Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    const auto p = theme::chamfer (r, 5.0f);
    g.setColour (on ? colour.withAlpha (hover ? 0.95f : 0.85f) : (hover ? theme::panelHi.brighter (0.08f) : theme::panelHi)); g.fillPath (p);
    g.setColour (on ? colour : theme::outline.brighter (0.2f)); g.strokePath (p, PathStrokeType (1.0f));
    g.setColour (on ? theme::bg : theme::textDim); g.setFont (theme::font (10.5f, true));
    g.drawFittedText (label, getLocalBounds().reduced (3, 0), Justification::centred, 1, 0.8f);
}

void ToggleBox::mouseDown (const MouseEvent&) { attachment.setValueAsCompleteGesture (on ? 0.0f : 1.0f); }

// ================================================================ TextBtn
void TextBtn::paint (Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    const auto p = theme::chamfer (r, 5.0f);
    const bool filled = lit || down;
    g.setColour (filled ? colour.withAlpha (down ? 1.0f : 0.88f) : (hover ? theme::panelHi.brighter (0.1f) : theme::panelHi)); g.fillPath (p);
    g.setColour (colour.withAlpha (hover || filled ? 1.0f : 0.6f)); g.strokePath (p, PathStrokeType (1.0f));
    g.setColour (filled ? theme::bg : (hover ? theme::text : colour.interpolatedWith (theme::text, 0.35f)));
    g.setFont (theme::font (fontSize, true));
    g.drawFittedText (label, getLocalBounds().reduced (3, 0), Justification::centred, 1, 0.8f);
}
