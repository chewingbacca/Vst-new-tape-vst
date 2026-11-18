#include "Knob.h"

namespace verdant::ui
{

namespace
{
    constexpr float kArcStart = -0.75f * juce::MathConstants<float>::pi;
    constexpr float kArcEnd   =  0.75f * juce::MathConstants<float>::pi;

    float angleFor (float n) noexcept   { return kArcStart + (kArcEnd - kArcStart) * n; }
}

Knob::Knob (UiContext& c, PID pid, Style s)
    : ParamControl (c, pid), style (s)
{
    bipolar  = isBipolar (def);
    discrete = def.type != PType::Float;
    setWantsKeyboardFocus (false);
}

Knob::~Knob()
{
    if (dragging)
        attachment.endGesture();

    if (valueEditor != nullptr)
    {
        // no callbacks into a half-destroyed knob
        valueEditor->onReturnKey = nullptr;
        valueEditor->onEscapeKey = nullptr;
        valueEditor->onFocusLost = nullptr;
        valueEditor.reset();
    }
}

//==============================================================================
juce::Rectangle<int> Knob::valueFieldBounds() const
{
    const bool macro = style == Style::macro;
    const auto value = cellLayout (macro).value;
    const float w = juce::jmin (float (getWidth()) - 2.0f, ctx.px (macro ? 88.0f : 62.0f));
    const float h = value.getHeight() + ctx.px (macro ? 8.0f : 6.0f);

    return value.withSizeKeepingCentre (w, h).toNearestInt().constrainedWithin (getLocalBounds());
}

bool Knob::isOverValue (juce::Point<float> p) const
{
    return cellLayout (style == Style::macro).value.expanded (0.0f, ctx.px (3.0f)).contains (p);
}

void Knob::styleValueEditor()
{
    if (valueEditor == nullptr)
        return;

    const auto font = ctx.font (style == Style::macro ? Type::macroValue : Type::value);
    valueEditor->setColour (juce::TextEditor::textColourId, col::text);
    valueEditor->setColour (juce::TextEditor::backgroundColourId, col::well);
    valueEditor->setColour (juce::TextEditor::outlineColourId, accent.withAlpha (0.75f));
    valueEditor->setColour (juce::TextEditor::focusedOutlineColourId, accent.withAlpha (0.9f));
    valueEditor->setColour (juce::TextEditor::highlightColourId, accent.withAlpha (0.32f));
    valueEditor->setColour (juce::TextEditor::highlightedTextColourId, col::text);
    valueEditor->setColour (juce::CaretComponent::caretColourId, accent);
    valueEditor->setFont (font);
    valueEditor->applyFontToAllText (font);
    valueEditor->setBounds (valueFieldBounds());
}

void Knob::showValueEditor()
{
    if (dragging)
    {
        dragging = false;
        attachment.endGesture();
    }

    if (valueEditor == nullptr)
    {
        valueEditor = std::make_unique<juce::TextEditor>();
        valueEditor->setMultiLine (false);
        valueEditor->setReturnKeyStartsNewLine (false);
        valueEditor->setScrollbarsShown (false);
        valueEditor->setPopupMenuEnabled (false);
        valueEditor->setSelectAllWhenFocused (true);
        valueEditor->setInputRestrictions (24);
        valueEditor->setJustification (juce::Justification::centred);
        valueEditor->setBorder ({});
        valueEditor->setIndents (0, 0);
        valueEditor->setTitle (def.name);

        // All three arrive asynchronously from the field (see TextEditor::handleCommandMessage);
        // the field itself is only hidden, never deleted, from inside them.
        valueEditor->onReturnKey = [this] { hideValueEditor (true); };
        valueEditor->onEscapeKey = [this] { hideValueEditor (false); };
        valueEditor->onFocusLost = [this]
        {
            // a stale notification must not close a field that has been reopened since
            if (editing && valueEditor != nullptr && ! valueEditor->hasKeyboardFocus (true))
                hideValueEditor (false);
        };

        addChildComponent (*valueEditor);
    }

    editing = true;
    styleValueEditor();
    valueEditor->setText (param.getCurrentValueAsText(), false);
    valueEditor->setVisible (true);
    valueEditor->toFront (false);
    valueEditor->selectAll();

    if (isShowing())
        valueEditor->grabKeyboardFocus();

    ctx.setHover (this, def.name, &param, "Type a value, Return to apply, Esc to cancel");
    repaint();
}

void Knob::hideValueEditor (bool commit)
{
    if (! editing)
        return;

    editing = false;

    if (valueEditor != nullptr)
    {
        const auto text = valueEditor->getText();
        valueEditor->setVisible (false);

        if (commit)
            commitValueText (text);
    }

    if (hovered)
        showHelp();
    else
        ctx.clearHover (this);

    repaint();
}

void Knob::setValueEditorText (const juce::String& text)
{
    if (editing && valueEditor != nullptr)
        valueEditor->setText (text, false);
}

bool Knob::commitValueText (const juce::String& rawText)
{
    const auto text = rawText.trim();
    if (text.isEmpty())
        return false;

    const float parsed = param.getValueForText (text);
    if (! std::isfinite (parsed))
        return false;

    // Text without a single digit is only accepted if the parameter's parser makes
    // something of it (e.g. "inf" for an infinite decay) rather than falling back to zero.
    if (! text.containsAnyOf ("0123456789")
        && std::abs (parsed - param.getValueForText ({})) < 1.0e-6f)
        return false;

    setFromNormalised (parsed, false);   // snaps, and wraps begin / end gesture
    return true;
}

void Knob::resized()
{
    if (editing)
        styleValueEditor();
}

//==============================================================================
void Knob::setFromNormalised (float n, bool partOfGesture)
{
    const auto& range = param.getNormalisableRange();
    const float v = range.snapToLegalValue (range.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, n)));

    if (partOfGesture)
        attachment.setValueAsPartOfGesture (v);
    else
        attachment.setValueAsCompleteGesture (v);
}

void Knob::resetToDefault()
{
    const float value = param.convertFrom0to1 (param.getDefaultValue());

    // A double-click arrives while the gesture of its second mouse-down is open:
    // never nest gestures, hosts (and JUCE's own checks) do not allow it.
    if (dragging)
    {
        dragNorm = param.getDefaultValue();
        attachment.setValueAsPartOfGesture (value);
    }
    else
    {
        attachment.setValueAsCompleteGesture (value);
    }
}

void Knob::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;

    // the second click of a double-click on the read-out opens the field: no gesture for it
    if (e.getNumberOfClicks() > 1 && isOverValue (e.position))
        return;

    if (e.mods.isAltDown() || e.mods.isCommandDown())
    {
        resetToDefault();
        return;
    }

    dragging = true;
    dragNorm = norm;
    lastDragPos = e.position;
    attachment.beginGesture();
    repaint();
}

void Knob::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging)
        return;

    const auto delta = e.position - lastDragPos;
    lastDragPos = e.position;

    const float range = ctx.px (style == Style::macro ? 240.0f : 190.0f);
    const float fine = e.mods.isShiftDown() ? 0.12f : 1.0f;

    dragNorm = juce::jlimit (0.0f, 1.0f, dragNorm + (delta.x * 0.6f - delta.y) / range * fine);
    setFromNormalised (dragNorm, true);
    showHelp();
}

void Knob::mouseUp (const juce::MouseEvent&)
{
    if (dragging)
    {
        dragging = false;
        attachment.endGesture();
        repaint();
    }
}

void Knob::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (isOverValue (e.position))
        showValueEditor();
    else
        resetToDefault();
}

void Knob::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (dragging)
        return;

    const float amount = (std::abs (wheel.deltaY) > std::abs (wheel.deltaX) ? wheel.deltaY : -wheel.deltaX)
                         * (wheel.isReversed ? -1.0f : 1.0f);
    if (std::abs (amount) < 1.0e-6f)
        return;

    if (discrete)
    {
        const float steps = juce::jmax (1.0f, def.max - def.min);
        wheelAccum += amount * 3.2f;
        const float whole = std::trunc (wheelAccum);
        if (std::abs (whole) >= 1.0f)
        {
            wheelAccum -= whole;
            setFromNormalised (norm + whole / steps, false);
        }
    }
    else
    {
        const float fine = e.mods.isShiftDown() ? 0.2f : 1.0f;
        setFromNormalised (norm + amount * 0.16f * fine, false);
    }
}

//==============================================================================
void Knob::paint (juce::Graphics& g)
{
    const bool macro = style == Style::macro;
    const auto cell = cellLayout (macro);

    const auto capFont = ctx.font (macro ? Type::macroName : Type::label);
    const auto valFont = ctx.font (macro ? Type::macroValue : Type::value);

    const auto capArea = cell.caption;
    const auto valArea = cell.value;
    const auto kb = cell.body;
    const float diameter = juce::jmin (kb.getWidth() - ctx.px (macro ? 24.0f : 8.0f), kb.getHeight() - ctx.px (macro ? 8.0f : 0.0f));
    const float R = diameter * 0.5f;
    const auto c = kb.getCentre();

    if (R < 3.0f)
        return;

    const float th = macro ? juce::jmax (2.5f, R * 0.085f) : juce::jmax (2.0f, R * 0.135f);
    const float arcR = R - th * 0.5f;
    const float bodyR = R - th - juce::jmax (1.5f, R * (macro ? 0.13f : 0.15f));
    const bool active = hovered || dragging;

    // --- glow, proportional to value
    {
        const float amount = bipolar ? std::abs (norm - param.getDefaultValue()) * 2.0f : norm;
        const float glowR = juce::jmin (R * 1.5f, float (getWidth()) * 0.5f, kb.getHeight() * 0.5f + ctx.px (macro ? 7.0f : 4.0f));
        const float alpha = (macro ? 0.05f + 0.20f * amount : 0.02f + 0.11f * amount) + (active ? 0.04f : 0.0f);

        juce::ColourGradient glow (accent.withAlpha (alpha), c.x, c.y, accent.withAlpha (0.0f), c.x + glowR, c.y, true);
        glow.addColour (0.55, accent.withAlpha (alpha * 0.55f));
        g.setGradientFill (glow);
        g.fillEllipse (c.x - glowR, c.y - glowR, glowR * 2.0f, glowR * 2.0f);
    }

    // --- tick dots (macro knobs): lit up to the value
    if (macro)
    {
        constexpr int ticks = 21;
        const float tickR = R + juce::jmax (3.5f, R * 0.115f);
        const float d = juce::jmax (1.4f, ctx.px (1.7f));

        for (int i = 0; i < ticks; ++i)
        {
            const float tn = float (i) / float (ticks - 1);
            const float a = angleFor (tn);
            const bool major = i % 5 == 0;
            const bool lit = bipolar ? (tn - param.getDefaultValue()) * (norm - param.getDefaultValue()) > 0.0f
                                           && std::abs (tn - param.getDefaultValue()) <= std::abs (norm - param.getDefaultValue()) + 0.001f
                                     : tn <= norm + 0.001f;
            const float size = major ? d * 1.35f : d;
            g.setColour (lit ? accent.withAlpha (major ? 0.85f : 0.55f) : col::hairline.withAlpha (major ? 0.26f : 0.14f));
            g.fillEllipse (c.x + std::sin (a) * tickR - size * 0.5f, c.y - std::cos (a) * tickR - size * 0.5f, size, size);
        }
    }

    // --- track
    {
        juce::Path track;
        track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, kArcStart, kArcEnd, true);
        g.setColour (col::hairline.withAlpha (0.15f));
        g.strokePath (track, juce::PathStrokeType (th, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // --- value arc (from the default for bipolar parameters)
    const float valueAngle = angleFor (norm);
    {
        const float from = bipolar ? angleFor (param.getDefaultValue()) : kArcStart;

        if (std::abs (valueAngle - from) > 0.012f)
        {
            juce::Path arc;
            arc.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, juce::jmin (from, valueAngle), juce::jmax (from, valueAngle), true);
            g.setColour (active ? accent.brighter (0.15f) : accent.withMultipliedAlpha (0.94f));
            g.strokePath (arc, juce::PathStrokeType (th, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        if (bipolar)
        {
            const float a = angleFor (param.getDefaultValue());
            const float d = juce::jmax (1.6f, th * 0.7f);
            const float r0 = R + d * 0.9f;
            g.setColour (col::muted.withAlpha (0.9f));
            g.fillEllipse (c.x + std::sin (a) * r0 - d * 0.5f, c.y - std::cos (a) * r0 - d * 0.5f, d, d);
        }
    }

    // --- body
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff224836), c.x, c.y - bodyR,
                                                 juce::Colour (0xff0f2119), c.x, c.y + bodyR, false));
        g.fillEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);

        const float line = juce::jmax (1.0f, ctx.scale);
        g.setGradientFill (juce::ColourGradient (col::hairline.withAlpha (active ? 0.42f : 0.28f), c.x, c.y - bodyR,
                                                 col::hairline.withAlpha (0.03f), c.x, c.y + bodyR * 0.6f, false));
        g.drawEllipse (c.x - bodyR + line * 0.5f, c.y - bodyR + line * 0.5f,
                       bodyR * 2.0f - line, bodyR * 2.0f - line, line);
    }

    // --- a shallow dish on the big knobs
    if (macro)
    {
        const float dish = bodyR * 0.74f;
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.11f), c.x, c.y - dish,
                                                 juce::Colours::white.withAlpha (0.035f), c.x, c.y + dish, false));
        g.fillEllipse (c.x - dish, c.y - dish, dish * 2.0f, dish * 2.0f);
    }

    // --- leaf pointer
    {
        const float dx = std::sin (valueAngle), dy = -std::cos (valueAngle);
        const float start = bodyR * (macro ? 0.08f : 0.06f);
        const float len = bodyR * (macro ? 0.84f : 0.86f);
        const float bx = c.x + dx * start, by = c.y + dy * start;

        juce::Path leaf;
        addLeaf (leaf, bx, by, dx, dy, len, macro ? 0.29f : 0.30f);

        const auto tipCol = accent.interpolatedWith (juce::Colours::white, 0.12f + 0.2f * norm);
        g.setGradientFill (juce::ColourGradient (tipCol, bx + dx * len, by + dy * len,
                                                 accent.darker (0.45f).withAlpha (0.9f), bx, by, false));
        g.fillPath (leaf);

        if (macro)
        {
            juce::Path vein;
            vein.startNewSubPath (bx + dx * len * 0.08f, by + dy * len * 0.08f);
            vein.lineTo (bx + dx * len * 0.78f, by + dy * len * 0.78f);
            g.setColour (juce::Colour (0xff0f2119).withAlpha (0.5f));
            g.strokePath (vein, juce::PathStrokeType (juce::jmax (1.0f, bodyR * 0.04f), juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded));
        }
    }

    // --- text
    g.setFont (capFont);
    g.setColour (active ? col::text : col::muted);
    g.drawFittedText (macro ? caption.toUpperCase() : caption, capArea.toNearestInt(),
                      juce::Justification::centred, 1, 0.72f);

    if (! editing)   // the inline field covers the read-out while typing
    {
        g.setFont (valFont);
        g.setColour (dragging ? accent.brighter (0.2f) : col::text.withAlpha (macro ? 0.95f : 0.88f));
        g.drawFittedText (param.getCurrentValueAsText(), valArea.toNearestInt(), juce::Justification::centred, 1, 0.72f);
    }
}

} // namespace verdant::ui
