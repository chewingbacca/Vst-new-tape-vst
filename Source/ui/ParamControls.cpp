#include "ParamControls.h"

namespace swoon::ui
{

namespace
{
/** Geometry of a one-cell control: the knob (or selector / switch) with its label underneath,
    centred in the cell and growing a little when the cell has room. */
struct CellMetrics
{
    explicit CellMetrics (const juce::Component& c)
    {
        const float w = (float) c.getWidth(), h = (float) c.getHeight();
        arcR = juce::jlimit (13.0f, 18.5f, juce::jmin (w * 0.27f, (h - 24.0f) * 0.36f));
        const float groupH = arcR * 2.0f + 8.0f + 15.0f;
        top = juce::jmax (0.0f, (h - groupH) * 0.5f);
        centre = { w * 0.5f, top + arcR + 2.0f };
        labelArea = { 1.0f, centre.y + arcR + 5.0f, w - 2.0f, 15.0f };
    }

    float arcR = 15.0f, top = 0.0f;
    juce::Point<float> centre;
    juce::Rectangle<float> labelArea;
};

/** Vertical mouse position in the top-level window (unaffected by the UI scale transform). */
float windowY (const juce::MouseEvent& e)
{
    if (auto* top = e.eventComponent != nullptr ? e.eventComponent->getTopLevelComponent() : nullptr)
        return e.getEventRelativeTo (top).position.y;

    return e.position.y;
}

void drawCellLabel (juce::Graphics& g, const CellMetrics& m, const juce::String& text, juce::Colour colour)
{
    g.setColour (colour);
    drawFitted (g, text, m.labelArea, juce::Justification::centred, sansFont (10.8f, 0, 0.03f), 0.74f);
}
} // namespace

//==============================================================================
ParamControl::ParamControl (juce::RangedAudioParameter& p, const juce::String& l, juce::Colour a)
    : param (p), label (l), accent (a),
      value (p.convertFrom0to1 (p.getValue())),
      attachment (p, [this] (float v)
      {
          if (! juce::exactlyEqual (v, value))
          {
              value = v;
              parameterChanged();
              if (onValueChange != nullptr)
                  onValueChange();
          }
      })
{
    setTitle (p.getName (64));
    setWantsKeyboardFocus (false);
}

bool ParamControl::showHostMenu (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu())
        return false;

    // let the host offer its own parameter menu (automation, MIDI learn...) where it has one
    if (auto* editor = findParentComponentOfClass<juce::AudioProcessorEditor>())
        if (auto* host = editor->getHostContext())
            if (auto menu = host->getContextMenuForParameter (&param))
                menu->showNativeMenu (editor->getLocalPoint (this, e.getPosition()));

    return true;
}

void ParamControl::setHover (bool shouldHover)
{
    if (hover != shouldHover)
    {
        hover = shouldHover;
        repaint();
    }
}

//==============================================================================
Knob::Knob (juce::RangedAudioParameter& p, Style s, const juce::String& l, juce::Colour a)
    : ParamControl (p, l, a), style (s)
{
}

Knob::~Knob()
{
    if (dragging)
        attachment.endGesture();   // never leave the host with an open gesture
}

void Knob::paint (juce::Graphics& g)
{
    if (style == Style::macro)
        paintMacro (g);
    else
        paintSmall (g);
}

void Knob::paintMacro (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    const auto textArea = area.removeFromBottom (40.0f);
    const auto centre = area.getCentre();
    const float arcR = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f - 9.0f;
    const float bodyR = arcR - 11.0f;
    const float v = proportion();
    const float angle = arcAngle (v);
    const bool active = hover || dragging;

    // bloom behind the knob, growing with the value
    fillGlow (g, centre, arcR + 26.0f, accent.withAlpha (0.05f + 0.11f * v + (active ? 0.04f : 0.0f)));

    // track
    g.setColour (juce::Colours::white.withAlpha (0.085f));
    strokeArc (g, centre, arcR, kArcStart, kArcEnd, 3.0f);

    // value arc with a soft halo
    const float from = bipolar ? arcAngle (0.5f) : kArcStart;
    if (std::abs (angle - from) > 0.002f)
    {
        g.setColour (accent.withAlpha (0.10f));
        strokeArc (g, centre, arcR, juce::jmin (from, angle), juce::jmax (from, angle), 11.0f);
        g.setColour (accent.withAlpha (0.16f));
        strokeArc (g, centre, arcR, juce::jmin (from, angle), juce::jmax (from, angle), 6.5f);
        g.setColour (accent);
        strokeArc (g, centre, arcR, juce::jmin (from, angle), juce::jmax (from, angle), 3.0f);
    }

    // centre detent mark
    if (detent)
    {
        const bool atCentre = std::abs (v - 0.5f) < 0.004f;
        const auto p = centre.getPointOnCircumference (arcR + 8.5f, 0.0f);
        g.setColour (atCentre ? accent : col::fog.withAlpha (0.32f));
        g.fillEllipse (p.x - 1.6f, p.y - 1.6f, 3.2f, 3.2f);
    }

    // body: a soft glass dome with a dished cap
    fillGlow (g, centre.translated (0.0f, bodyR * 0.34f), bodyR * 1.6f, juce::Colours::black.withAlpha (0.6f));

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff463e7a), centre.x - bodyR * 0.55f, centre.y - bodyR,
                                             juce::Colour (0xff120f26), centre.x + bodyR * 0.45f, centre.y + bodyR, false));
    g.fillEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);

    g.setColour (accent.withAlpha (active ? 0.10f : 0.05f));
    g.fillEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);

    const float capR = bodyR - 6.5f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1b1736), centre.x - capR * 0.3f, centre.y - capR,
                                             juce::Colour (0xff322b5c), centre.x + capR * 0.3f, centre.y + capR, false));
    g.fillEllipse (centre.x - capR, centre.y - capR, capR * 2.0f, capR * 2.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.35f), centre.x, centre.y - capR,
                                             juce::Colours::white.withAlpha (0.10f), centre.x, centre.y + capR, false));
    g.drawEllipse (centre.x - capR, centre.y - capR, capR * 2.0f, capR * 2.0f, 1.0f);

    // rim light from above
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.42f), centre.x - bodyR * 0.4f, centre.y - bodyR,
                                             juce::Colours::white.withAlpha (0.0f), centre.x + bodyR * 0.2f, centre.y + bodyR * 0.5f, false));
    g.drawEllipse (centre.x - bodyR + 0.5f, centre.y - bodyR + 0.5f, bodyR * 2.0f - 1.0f, bodyR * 2.0f - 1.0f, 1.0f);

    // pointer: a small light on the bevel
    const auto dot = centre.getPointOnCircumference (bodyR - 3.4f, angle);
    fillGlow (g, dot, 10.0f, accent.withAlpha (0.6f));
    g.setColour (accent.interpolatedWith (juce::Colours::white, 0.6f));
    g.fillEllipse (dot.x - 2.5f, dot.y - 2.5f, 5.0f, 5.0f);

    // name + value
    const auto nameFont = sansFont (12.0f, 1, 0.2f);
    auto nameArea = textArea.withHeight (17.0f).translated (0.1f * nameFont.getHeight(), 1.0f);
    g.setColour (col::fog.withAlpha (0.94f));
    g.setFont (nameFont);
    g.drawText (label.toUpperCase(), nameArea, juce::Justification::centred, false);

    g.setColour (active ? col::fog : col::mist.withAlpha (0.85f));
    g.setFont (sansFont (11.5f, 0, 0.02f));
    g.drawText (valueText(), textArea.withTrimmedTop (19.0f).withHeight (16.0f), juce::Justification::centred, false);
}

void Knob::paintSmall (juce::Graphics& g)
{
    const CellMetrics m (*this);
    const auto centre = m.centre;
    const float arcR = m.arcR, bodyR = arcR - 5.0f;
    const float v = proportion();
    const float angle = arcAngle (v);
    const bool active = hover || dragging;

    g.setColour (juce::Colours::white.withAlpha (0.10f));
    strokeArc (g, centre, arcR, kArcStart, kArcEnd, 2.3f);

    const float from = bipolar ? arcAngle (0.5f) : kArcStart;
    if (std::abs (angle - from) > 0.002f)
    {
        g.setColour (accent.withAlpha (0.16f));
        strokeArc (g, centre, arcR, juce::jmin (from, angle), juce::jmax (from, angle), 6.0f);
        g.setColour (accent);
        strokeArc (g, centre, arcR, juce::jmin (from, angle), juce::jmax (from, angle), 2.3f);
    }

    if (bipolar)
    {
        const auto p = centre.getPointOnCircumference (arcR + 5.0f, 0.0f);
        g.setColour (col::fog.withAlpha (0.3f));
        g.fillEllipse (p.x - 1.0f, p.y - 1.0f, 2.0f, 2.0f);
    }

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3d3569), centre.x - 3.0f, centre.y - bodyR,
                                             juce::Colour (0xff1a1634), centre.x + 3.0f, centre.y + bodyR, false));
    g.fillEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);
    g.setColour (juce::Colours::white.withAlpha (active ? 0.34f : 0.15f));
    g.drawEllipse (centre.x - bodyR + 0.5f, centre.y - bodyR + 0.5f, bodyR * 2.0f - 1.0f, bodyR * 2.0f - 1.0f, 1.0f);

    const auto a = centre.getPointOnCircumference (bodyR * 0.3f, angle);
    const auto b = centre.getPointOnCircumference (bodyR - 2.4f, angle);
    g.setColour (col::fog);
    g.drawLine ({ a, b }, 1.8f);

    drawCellLabel (g, m, active ? valueText() : label, active ? col::fog : col::mist);
}

void Knob::mouseDown (const juce::MouseEvent& e)
{
    if (showHostMenu (e))
        return;

    dragging = true;
    dragProportion = proportion();
    lastDragY = windowY (e);
    attachment.beginGesture();
    repaint();
}

void Knob::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging)
        return;

    // measured in window pixels, so the feel does not change with the editor's scale
    const float range = style == Style::macro ? 240.0f : 190.0f;
    const float y = windowY (e);
    const float dy = lastDragY - y;
    lastDragY = y;

    dragProportion = juce::jlimit (0.0, 1.0, dragProportion + (double) dy / range * (e.mods.isShiftDown() ? 0.12 : 1.0));

    // soft detent: a macro passing its neutral point rests there unless shift (fine) is held
    double target = dragProportion;
    if (detent && ! e.mods.isShiftDown() && std::abs (target - 0.5) < 0.014)
        target = 0.5;

    attachment.setValueAsPartOfGesture (param.convertFrom0to1 ((float) target));
}

void Knob::mouseUp (const juce::MouseEvent&)
{
    if (! dragging)
        return;

    dragging = false;
    attachment.endGesture();
    repaint();
}

void Knob::mouseDoubleClick (const juce::MouseEvent&)
{
    const float defaultValue = param.convertFrom0to1 (param.getDefaultValue());

    if (dragging)
    {
        // the second press of the double-click has already opened a gesture: stay inside it
        dragProportion = param.getDefaultValue();
        attachment.setValueAsPartOfGesture (defaultValue);
    }
    else
    {
        attachment.setValueAsCompleteGesture (defaultValue);
    }
}

void Knob::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (dragging || juce::approximatelyEqual (wheel.deltaY, 0.0f))
        return;

    const float direction = wheel.isReversed ? -1.0f : 1.0f;
    float delta = wheel.deltaY * direction * (e.mods.isShiftDown() ? 0.06f : 0.3f);

    if (param.isDiscrete() && param.getNumSteps() > 1)
    {
        const float step = 1.0f / (float) (param.getNumSteps() - 1);
        delta = (wheel.deltaY * direction > 0.0f ? step : -step);
    }

    const float target = juce::jlimit (0.0f, 1.0f, proportion() + delta);
    attachment.setValueAsCompleteGesture (param.convertFrom0to1 (target));
}

//==============================================================================
ChoiceSelector::ChoiceSelector (juce::RangedAudioParameter& p, const juce::String& l, juce::Colour a)
    : ParamControl (p, l, a)
{
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (&p))
        choices = choice->choices;
    else
        choices = p.getAllValueStrings();
}

juce::Rectangle<float> ChoiceSelector::windowBounds() const
{
    const CellMetrics m (*this);
    const float w = juce::jmin ((float) getWidth() - 4.0f, 82.0f), h = juce::jmin (36.0f, m.arcR * 2.0f);
    return { ((float) getWidth() - w) * 0.5f, m.centre.y - h * 0.5f, w, h };
}

int ChoiceSelector::index() const
{
    return juce::jlimit (0, juce::jmax (0, choices.size() - 1), juce::roundToInt (value - param.getNormalisableRange().start));
}

void ChoiceSelector::setIndex (int newIndex, bool wrap)
{
    const int n = juce::jmax (1, choices.size());
    newIndex = wrap ? ((newIndex % n) + n) % n : juce::jlimit (0, n - 1, newIndex);

    if (newIndex != index())
        attachment.setValueAsCompleteGesture (param.getNormalisableRange().start + (float) newIndex);
}

void ChoiceSelector::paint (juce::Graphics& g)
{
    const auto win = windowBounds();
    const int n = choices.size();
    const int current = index();

    g.setColour (col::ink.withAlpha (0.62f));
    g.fillRoundedRectangle (win, 7.0f);
    g.setColour (hover ? accent.withAlpha (0.55f) : juce::Colours::white.withAlpha (0.11f));
    g.drawRoundedRectangle (win.reduced (0.5f), 7.0f, 1.0f);

    // position pips
    if (n > 1)
    {
        const float spacing = juce::jmin (7.0f, (win.getWidth() - 16.0f) / (float) n);
        float x = win.getCentreX() - spacing * (float) (n - 1) * 0.5f;
        const float y = win.getBottom() - 5.5f;

        for (int i = 0; i < n; ++i, x += spacing)
        {
            g.setColour (i == current ? accent : juce::Colours::white.withAlpha (0.16f));
            const float d = i == current ? 3.2f : 2.4f;
            g.fillEllipse (x - d * 0.5f, y - d * 0.5f, d, d);
        }
    }

    // current choice, on two lines when it is long
    const auto text = choices[current];
    auto textArea = win.reduced (6.0f, 0.0f).withTrimmedTop (2.0f).withTrimmedBottom (n > 1 ? 9.0f : 2.0f);
    auto font = sansFont (10.8f, 1, 0.01f);
    g.setColour (col::fog);

    const int space = text.indexOfChar (' ');
    if (space > 0 && textWidth (font, text) > textArea.getWidth())
    {
        // break at the space nearest the middle
        int split = space;
        for (int i = space; i >= 0; i = text.indexOfChar (i + 1, ' '))
            if (std::abs (i - text.length() / 2) < std::abs (split - text.length() / 2))
                split = i;

        font = sansFont (9.6f, 1, 0.01f);
        const auto upper = textArea.withHeight (textArea.getHeight() * 0.5f).translated (0.0f, 1.2f);
        drawFitted (g, text.substring (0, split), upper, juce::Justification::centred, font, 0.8f);
        drawFitted (g, text.substring (split + 1), upper.translated (0.0f, upper.getHeight() - 1.6f), juce::Justification::centred, font, 0.8f);
    }
    else
    {
        drawFitted (g, text, textArea, juce::Justification::centred, font, 0.75f);
    }

    // step hints
    if (hover && n > 1)
    {
        const float cy = win.getCentreY() - 3.0f;
        juce::Path left, right;
        left.startNewSubPath (win.getX() + 6.0f, cy - 3.0f);
        left.lineTo (win.getX() + 3.4f, cy);
        left.lineTo (win.getX() + 6.0f, cy + 3.0f);
        right.startNewSubPath (win.getRight() - 6.0f, cy - 3.0f);
        right.lineTo (win.getRight() - 3.4f, cy);
        right.lineTo (win.getRight() - 6.0f, cy + 3.0f);

        const juce::PathStrokeType stroke (1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        g.setColour (col::fog.withAlpha (overLeft ? 0.9f : 0.3f));
        g.strokePath (left, stroke);
        g.setColour (col::fog.withAlpha (overLeft ? 0.3f : 0.9f));
        g.strokePath (right, stroke);
    }

    drawCellLabel (g, CellMetrics (*this), label, hover ? col::fog : col::mist);
}

void ChoiceSelector::mouseMove (const juce::MouseEvent& e)
{
    const bool left = e.position.x < (float) getWidth() * 0.36f;
    if (left != overLeft)
    {
        overLeft = left;
        repaint();
    }
}

void ChoiceSelector::mouseDown (const juce::MouseEvent& e)
{
    showHostMenu (e);
    dragged = false;
    dragStartIndex = index();
}

void ChoiceSelector::mouseDrag (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;

    if (std::abs (e.getDistanceFromDragStartY()) > 5)
        dragged = true;

    if (dragged)
        setIndex (dragStartIndex - juce::roundToInt ((float) e.getDistanceFromDragStartY() / 14.0f), false);
}

void ChoiceSelector::mouseUp (const juce::MouseEvent& e)
{
    if (! dragged && ! e.mods.isPopupMenu() && getLocalBounds().contains (e.getPosition()))
        setIndex (index() + (e.position.x < (float) getWidth() * 0.36f ? -1 : 1), true);
}

void ChoiceSelector::mouseDoubleClick (const juce::MouseEvent&) {}

void ChoiceSelector::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    wheelAccumulator += wheel.deltaY * (wheel.isReversed ? -1.0f : 1.0f);

    if (std::abs (wheelAccumulator) >= 0.08f)
    {
        setIndex (index() + (wheelAccumulator > 0.0f ? 1 : -1), false);
        wheelAccumulator = 0.0f;
    }
}

//==============================================================================
Toggle::Toggle (juce::RangedAudioParameter& p, const juce::String& l, juce::Colour a)
    : ParamControl (p, l, a)
{
}

void Toggle::paint (juce::Graphics& g)
{
    const bool on = isOn();
    const float w = 34.0f, h = 18.0f;
    const CellMetrics m (*this);
    const juce::Rectangle<float> pill (((float) getWidth() - w) * 0.5f, m.centre.y - h * 0.5f, w, h);

    if (on)
        fillGlow (g, pill.getCentre(), 26.0f, accent.withAlpha (0.22f));

    g.setColour (on ? accent.withAlpha (0.26f) : col::ink.withAlpha (0.62f));
    g.fillRoundedRectangle (pill, h * 0.5f);
    g.setColour (on ? accent.withAlpha (0.9f) : juce::Colours::white.withAlpha (hover ? 0.3f : 0.13f));
    g.drawRoundedRectangle (pill.reduced (0.5f), h * 0.5f, 1.0f);

    const float d = h - 7.0f;
    const float x = on ? pill.getRight() - d - 3.5f : pill.getX() + 3.5f;
    g.setColour (on ? accent.interpolatedWith (juce::Colours::white, 0.4f) : col::dusk);
    g.fillEllipse (x, pill.getCentreY() - d * 0.5f, d, d);

    drawCellLabel (g, m, label, hover || on ? col::fog : col::mist);
}

void Toggle::mouseDown (const juce::MouseEvent& e)
{
    showHostMenu (e);
}

void Toggle::mouseUp (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu() && getLocalBounds().contains (e.getPosition()))
        attachment.setValueAsCompleteGesture (isOn() ? 0.0f : 1.0f);
}

//==============================================================================
PowerButton::PowerButton (juce::RangedAudioParameter& p, juce::Colour a, bool isInverted)
    : ParamControl (p, {}, a), inverted (isInverted)
{
}

void PowerButton::drawGlyph (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour, bool lit, float thickness)
{
    // the glyph sits in the middle 60 % of the area; the rest is room for its glow
    const auto c = area.getCentre();
    const float half = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f;
    const float r = half * 0.6f - thickness * 0.5f;

    if (lit)
        fillGlow (g, c, half, colour.withAlpha (0.42f));

    g.setColour (colour);
    strokeArc (g, c, r, 0.62f, juce::MathConstants<float>::twoPi - 0.62f, thickness);
    g.drawLine (c.x, c.y - r - thickness * 0.3f, c.x, c.y - r * 0.15f, thickness);
}

void PowerButton::paint (juce::Graphics& g)
{
    const bool lit = isOn() != inverted;
    const auto colour = lit ? accent : (hover ? col::mist : col::dusk);
    drawGlyph (g, getLocalBounds().toFloat(), colour, lit, juce::jmax (1.4f, (float) getWidth() * 0.055f));
}

void PowerButton::mouseDown (const juce::MouseEvent& e)
{
    showHostMenu (e);
}

void PowerButton::mouseUp (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu() && getLocalBounds().contains (e.getPosition()))
        attachment.setValueAsCompleteGesture (isOn() ? 0.0f : 1.0f);
}

} // namespace swoon::ui
