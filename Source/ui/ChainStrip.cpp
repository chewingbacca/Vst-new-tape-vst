#include "ChainStrip.h"

#include "../PluginProcessor.h"
#include "ParamControls.h"

namespace swoon::ui
{

namespace
{
constexpr float kCaptionH = 20.0f;

void drawChevron (juce::Graphics& g, juce::Point<float> c, float size, juce::Colour colour)
{
    juce::Path p;
    p.startNewSubPath (c.x - size * 0.5f, c.y - size);
    p.lineTo (c.x + size * 0.5f, c.y);
    p.lineTo (c.x - size * 0.5f, c.y + size);
    g.setColour (colour);
    g.strokePath (p, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void drawCaption (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, juce::Justification just)
{
    g.setColour (col::dusk);
    g.setFont (sansFont (9.4f, 1, 0.22f));
    g.drawText (text, area, just, false);
}
} // namespace

//==============================================================================
ChainStrip::ChainStrip (SwoonAudioProcessor& p, ChainModel& m) : processor (p), model (m)
{
    for (int slot = 0; slot < kNumSlots; ++slot)
        bindPower (power[(size_t) slot], ChainModel::moduleForSlot (slot));

    bindPower (tapePower, Module::Tape);

    visualOrder = model.getOrder();
    model.addChangeListener (this);
    setTitle ("Signal chain");
}

ChainStrip::~ChainStrip()
{
    stopTimer();
    model.removeChangeListener (this);
}

void ChainStrip::bindPower (PowerState& state, Module module)
{
    if (const auto* def = powerParamFor (module))
    {
        auto& param = processor.getParam (def->index);
        state.on = param.getValue() > 0.5f;
        state.attachment = std::make_unique<juce::ParameterAttachment> (param, [this, &state] (float v)
        {
            const bool nowOn = v > 0.5f;
            if (nowOn != state.on)
            {
                state.on = nowOn;
                repaint();
            }
        });
    }
}

void ChainStrip::togglePower (PowerState& state)
{
    if (state.attachment != nullptr)
        state.attachment->setValueAsCompleteGesture (state.on ? 0.0f : 1.0f);
}

//==============================================================================
void ChainStrip::resized()
{
    auto row = getLocalBounds().toFloat().withTrimmedTop (kCaptionH);

    flipBounds = row.removeFromLeft (158.0f);
    outputBounds = row.removeFromRight (72.0f);
    row.removeFromRight (16.0f);
    tapeBounds = row.removeFromRight (72.0f);
    row.removeFromRight (30.0f);
    row.removeFromLeft (26.0f);
    chipArea = row;

    chipGap = 15.0f;
    chipW = (chipArea.getWidth() - chipGap * (float) (kNumSlots - 1)) / (float) kNumSlots;
    layoutTargets (true);
}

float ChainStrip::slotTargetX (int position) const
{
    return chipArea.getX() + (float) position * (chipW + chipGap);
}

void ChainStrip::layoutTargets (bool snap)
{
    for (int position = 0; position < kNumSlots; ++position)
    {
        const auto slot = (size_t) visualOrder[(size_t) position];
        chipTargetX[slot] = slotTargetX (position);
        if (snap)
            chipX[slot] = chipTargetX[slot];
    }
}

void ChainStrip::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (! dragging)
    {
        visualOrder = model.getOrder();
        layoutTargets (false);
        startTimerHz (60);
    }

    repaint();
}

void ChainStrip::timerCallback()
{
    bool settled = true;

    for (int slot = 0; slot < kNumSlots; ++slot)
    {
        if (dragging && slot == pressSlot)
            continue;

        auto& x = chipX[(size_t) slot];
        const float target = chipTargetX[(size_t) slot];

        if (std::abs (target - x) < 0.4f)
        {
            x = target;
        }
        else
        {
            x += (target - x) * 0.32f;
            settled = false;
        }
    }

    if (settled && ! dragging)
        stopTimer();

    repaint();
}

//==============================================================================
ChainStrip::Hit ChainStrip::hitTestAt (juce::Point<float> p, int& slot) const
{
    slot = -1;

    if (flipBounds.contains (p))
        return Hit::flip;

    if (tapeBounds.contains (p))
        return Hit::tape;

    for (int s = 0; s < kNumSlots; ++s)
    {
        if (juce::Rectangle<float> (chipX[(size_t) s], chipArea.getY(), chipW, chipArea.getHeight()).contains (p))
        {
            slot = s;
            return Hit::chip;
        }
    }

    return Hit::none;
}

void ChainStrip::setHover (Hit hit, int slot)
{
    if (hit != hoverHit || slot != hoverSlot)
    {
        hoverHit = hit;
        hoverSlot = slot;
        setMouseCursor (hit == Hit::chip ? juce::MouseCursor::DraggingHandCursor
                        : hit == Hit::none ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor);
        repaint();
    }
}

void ChainStrip::mouseMove (const juce::MouseEvent& e)
{
    int slot = -1;
    const auto hit = hitTestAt (e.position, slot);
    setHover (hit, slot);
}

void ChainStrip::mouseExit (const juce::MouseEvent&)
{
    setHover (Hit::none, -1);
}

void ChainStrip::mouseDown (const juce::MouseEvent& e)
{
    pressHit = e.mods.isPopupMenu() ? Hit::none : hitTestAt (e.position, pressSlot);
    dragging = false;

    if (pressHit == Hit::chip)
        dragGrabOffset = e.position.x - chipX[(size_t) pressSlot];

    repaint();
}

void ChainStrip::mouseDrag (const juce::MouseEvent& e)
{
    if (pressHit != Hit::chip)
        return;

    if (! dragging && std::abs (e.getDistanceFromDragStartX()) > 4)
    {
        dragging = true;
        startTimerHz (60);
    }

    if (! dragging)
        return;

    auto& x = chipX[(size_t) pressSlot];
    x = juce::jlimit (chipArea.getX() - 8.0f, chipArea.getRight() - chipW + 8.0f, e.position.x - dragGrabOffset);

    const int position = juce::jlimit (0, kNumSlots - 1, juce::roundToInt ((x - chipArea.getX()) / (chipW + chipGap)));

    ChainModel::Order next {};
    int n = 0;
    for (int slot : visualOrder)
    {
        if (slot == pressSlot)
            continue;
        if (n == position)
            next[(size_t) n++] = pressSlot;
        next[(size_t) n++] = slot;
    }
    if (n < kNumSlots)
        next[(size_t) n] = pressSlot;

    visualOrder = next;
    layoutTargets (false);
    repaint();
}

void ChainStrip::mouseUp (const juce::MouseEvent& e)
{
    const auto hit = pressHit;
    const int slot = pressSlot;
    const bool wasDragging = dragging;

    pressHit = Hit::none;
    dragging = false;

    if (wasDragging)
    {
        if (visualOrder != model.getOrder())
            model.setOrder (visualOrder);

        startTimerHz (60);
    }
    else
    {
        int upSlot = -1;
        const auto upHit = hitTestAt (e.position, upSlot);

        if (hit == Hit::flip && upHit == Hit::flip)
            model.flipFuzzWash();
        else if (hit == Hit::tape && upHit == Hit::tape)
            togglePower (tapePower);
        else if (hit == Hit::chip && upHit == Hit::chip && upSlot == slot)
            togglePower (power[(size_t) slot]);
    }

    pressSlot = -1;
    repaint();
}

//==============================================================================
void ChainStrip::drawFlip (juce::Graphics& g)
{
    const bool hovered = hoverHit == Hit::flip;
    const bool pressed = pressHit == Hit::flip;
    const bool washLast = model.isWashAfterFuzz();
    const auto b = flipBounds.reduced (0.5f, 2.0f);
    const float r = b.getHeight() * 0.5f;

    const auto fuzzColour = moduleColour (Module::Fuzz);
    const auto washColour = moduleColour (Module::Wash);
    const auto first = washLast ? fuzzColour : washColour;
    const auto second = washLast ? washColour : fuzzColour;

    // soft bloom from both ends
    fillGlow (g, { b.getX() + r, b.getCentreY() }, 46.0f, first.withAlpha (hovered ? 0.20f : 0.12f));
    fillGlow (g, { b.getRight() - r, b.getCentreY() }, 46.0f, second.withAlpha (hovered ? 0.20f : 0.12f));

    g.setGradientFill (juce::ColourGradient (col::panelHi.interpolatedWith (first, pressed ? 0.20f : 0.10f), b.getX(), b.getY(),
                                             col::panel.interpolatedWith (second, pressed ? 0.20f : 0.10f), b.getRight(), b.getBottom(), false));
    g.fillRoundedRectangle (b, r);

    g.setGradientFill (juce::ColourGradient (first.withAlpha (hovered ? 0.95f : 0.7f), b.getX(), b.getY(),
                                             second.withAlpha (hovered ? 0.95f : 0.7f), b.getRight(), b.getY(), false));
    g.drawRoundedRectangle (b, r, 1.2f);

    // "FUZZ > WASH" (current order), then the swap glyph
    auto content = b.reduced (16.0f, 0.0f);
    auto icon = content.removeFromRight (24.0f);
    const auto font = sansFont (12.6f, 2, 0.14f);
    const juce::String a = washLast ? "FUZZ" : "WASH", c = washLast ? "WASH" : "FUZZ";
    const float wa = textWidth (font, a), wc = textWidth (font, c), arrowW = 22.0f;
    float x = content.getX() + (content.getWidth() - (wa + wc + arrowW)) * 0.5f;

    g.setFont (font);
    g.setColour (first.interpolatedWith (col::fog, 0.25f));
    g.drawText (a, juce::Rectangle<float> (x, content.getY(), wa + 2.0f, content.getHeight()), juce::Justification::centredLeft, false);
    x += wa;

    const float ay = content.getCentreY();
    g.setColour (col::fog.withAlpha (0.75f));
    g.drawLine (x + 3.0f, ay, x + arrowW - 8.0f, ay, 1.3f);
    drawChevron (g, { x + arrowW - 8.5f, ay }, 3.2f, col::fog.withAlpha (0.75f));
    x += arrowW;

    g.setColour (second.interpolatedWith (col::fog, 0.25f));
    g.drawText (c, juce::Rectangle<float> (x, content.getY(), wc + 2.0f, content.getHeight()), juce::Justification::centredLeft, false);

    // swap glyph: two opposed arrows
    const auto ic = icon.getCentre().translated (3.0f, 0.0f);
    g.setColour (col::fog.withAlpha (hovered ? 1.0f : 0.6f));
    g.drawLine (ic.x - 6.0f, ic.y - 3.5f, ic.x + 5.0f, ic.y - 3.5f, 1.3f);
    drawChevron (g, { ic.x + 4.6f, ic.y - 3.5f }, 2.6f, col::fog.withAlpha (hovered ? 1.0f : 0.6f));
    g.drawLine (ic.x - 5.0f, ic.y + 3.5f, ic.x + 6.0f, ic.y + 3.5f, 1.3f);
    {
        juce::Path p;
        p.startNewSubPath (ic.x - 3.3f, ic.y + 0.9f);
        p.lineTo (ic.x - 5.9f, ic.y + 3.5f);
        p.lineTo (ic.x - 3.3f, ic.y + 6.1f);
        g.strokePath (p, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

void ChainStrip::drawChip (juce::Graphics& g, juce::Rectangle<float> b, const juce::String& name, juce::Colour colour,
                           bool on, bool hasPower, bool fixed, bool hovered, bool lifted)
{
    const float corner = 11.0f;

    if (lifted)
    {
        b = b.expanded (2.0f).translated (0.0f, -2.0f);
        fillGlow (g, b.getCentre().translated (0.0f, 10.0f), b.getWidth() * 0.8f, juce::Colours::black.withAlpha (0.6f));
    }

    if (on && ! fixed)
        fillGlow (g, { b.getCentreX(), b.getBottom() - 6.0f }, b.getWidth() * 0.62f, colour.withAlpha (lifted ? 0.24f : 0.13f));

    if (fixed)
    {
        g.setColour (col::ink.withAlpha (0.5f));
        g.fillRoundedRectangle (b, corner);
        g.setColour (on ? colour.withAlpha (hovered ? 0.6f : 0.34f) : juce::Colours::white.withAlpha (0.1f));
        g.drawRoundedRectangle (b.reduced (0.5f), corner, 1.0f);
    }
    else
    {
        const float tint = on ? (hovered || lifted ? 0.2f : 0.12f) : 0.0f;
        g.setGradientFill (juce::ColourGradient (col::panelHi.interpolatedWith (colour, tint), b.getX(), b.getY(),
                                                 col::panel.interpolatedWith (colour, tint * 0.4f), b.getX(), b.getBottom(), false));
        g.setOpacity (on ? 1.0f : 0.55f);
        g.fillRoundedRectangle (b, corner);
        g.setOpacity (1.0f);
        g.setColour (on ? colour.withAlpha (hovered || lifted ? 0.85f : 0.48f) : juce::Colours::white.withAlpha (hovered ? 0.22f : 0.1f));
        g.drawRoundedRectangle (b.reduced (0.5f), corner, 1.0f);
    }

    g.setColour (on ? col::fog : col::dusk);
    drawFitted (g, name, b.reduced (4.0f, 0.0f).withTrimmedTop (9.0f).withHeight (17.0f), juce::Justification::centred,
                sansFont (12.6f, 1, 0.04f), 0.8f);

    const juce::Rectangle<float> glyph (b.getCentreX() - 7.0f, b.getBottom() - 22.0f, 14.0f, 14.0f);
    if (hasPower)
    {
        PowerButton::drawGlyph (g, glyph.expanded (5.0f), on ? colour : col::dusk, on, 1.4f);
    }
    else
    {
        // output: three rising bars
        g.setColour (colour.withAlpha (0.6f));
        for (int i = 0; i < 3; ++i)
        {
            const float h = 4.0f + 3.0f * (float) i;
            g.fillRoundedRectangle (glyph.getX() + 1.5f + 4.2f * (float) i, glyph.getBottom() - 2.0f - h, 2.4f, h, 1.0f);
        }
    }
}

void ChainStrip::paint (juce::Graphics& g)
{
    // captions
    drawCaption (g, "ORDER", flipBounds.withY (0.0f).withHeight (kCaptionH - 6.0f).translated (16.0f, 0.0f), juce::Justification::centredLeft);
    drawCaption (g, "SIGNAL PATH", chipArea.withY (0.0f).withHeight (kCaptionH - 6.0f).translated (2.0f, 0.0f), juce::Justification::centredLeft);
    drawCaption (g, "ALWAYS LAST", tapeBounds.getUnion (outputBounds).withY (0.0f).withHeight (kCaptionH - 6.0f).translated (2.0f, 0.0f),
                 juce::Justification::centredLeft);

    g.setColour (col::dusk.withAlpha (0.8f));
    g.setFont (sansFont (10.2f, 0, 0.02f));
    g.drawText (juce::String::fromUTF8 ("drag to reorder  \xc2\xb7  click to switch on / off"), chipArea.withY (0.0f).withHeight (kCaptionH - 6.0f),
                juce::Justification::centredRight, false);

    drawFlip (g);

    // arrows between the slots and into the fixed stages
    const float cy = chipArea.getCentreY();
    for (int position = 0; position < kNumSlots - 1; ++position)
        drawChevron (g, { slotTargetX (position) + chipW + chipGap * 0.5f, cy }, 3.2f, col::dusk.withAlpha (0.75f));

    drawChevron (g, { tapeBounds.getRight() + 8.0f, cy }, 3.2f, col::dusk.withAlpha (0.75f));

    // divider before the fixed stages
    {
        const float x = (chipArea.getRight() + tapeBounds.getX()) * 0.5f;
        g.setColour (juce::Colours::white.withAlpha (0.13f));
        for (float y = chipArea.getY() + 6.0f; y < chipArea.getBottom() - 6.0f; y += 5.0f)
            g.fillEllipse (x - 0.8f, y, 1.6f, 1.6f);
    }

    drawChip (g, tapeBounds, "Tape", moduleColour (Module::Tape), tapePower.on, tapePower.attachment != nullptr, true,
              hoverHit == Hit::tape, false);
    drawChip (g, outputBounds, "Output", moduleColour (Module::Output), true, false, true, false, false);

    // reorderable chips; the lifted one is drawn last
    for (int pass = 0; pass < 2; ++pass)
    {
        for (int slot = 0; slot < kNumSlots; ++slot)
        {
            const bool lifted = dragging && slot == pressSlot;
            if (lifted != (pass == 1))
                continue;

            const auto module = ChainModel::moduleForSlot (slot);
            drawChip (g, { chipX[(size_t) slot], chipArea.getY(), chipW, chipArea.getHeight() }, moduleName (module),
                      moduleColour (module), power[(size_t) slot].on, power[(size_t) slot].attachment != nullptr, false,
                      hoverHit == Hit::chip && hoverSlot == slot, lifted);
        }
    }
}

} // namespace swoon::ui
