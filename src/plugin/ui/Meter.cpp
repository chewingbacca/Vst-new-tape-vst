#include "Meter.h"

#include "../../dsp/Engine.h"

namespace verdant::ui
{

namespace
{
    constexpr float kFloorDb = -54.0f;

    // Linear peak -> 0..1 meter position (dB-linear with a slightly expanded top).
    float toMeter (float linear) noexcept
    {
        if (! (linear > 1.0e-6f))
            return 0.0f;

        const float db = 20.0f * std::log10 (linear);
        const float n = juce::jlimit (0.0f, 1.0f, (db - kFloorDb) / -kFloorDb);
        return n * n * (3.0f - 2.0f * n) * 0.35f + n * 0.65f;
    }

    float sanitise (float v) noexcept
    {
        return std::isfinite (v) ? juce::jlimit (0.0f, 4.0f, v) : 0.0f;
    }
}

void LevelMeters::Bar::feed (float linear, float dt)
{
    const float target = toMeter (sanitise (linear));

    level = target > level ? target : juce::jmax (target, level - dt * 0.9f);

    if (target >= peak)
    {
        peak = target;
        hold = 0.9f;
    }
    else if (hold > 0.0f)
    {
        hold -= dt;
    }
    else
    {
        peak = juce::jmax (level, peak - dt * 0.5f);
    }
}

//==============================================================================
LevelMeters::LevelMeters (UiContext& c) : ctx (c) {}

LevelMeters::~LevelMeters()
{
    ctx.clearHover (this);
}

void LevelMeters::mouseEnter (const juce::MouseEvent&)
{
    ctx.setHover (this, "Levels", nullptr,
                  "Input and output peak level. GR lights up when the output limiter is reducing gain");
}

void LevelMeters::mouseExit (const juce::MouseEvent&)
{
    ctx.clearHover (this);
}

void LevelMeters::update (const verdant::Meters& m, float dt)
{
    dt = juce::jlimit (0.0f, 0.2f, dt);

    in.feed (m.inLevel.load (std::memory_order_relaxed), dt);
    outL.feed (m.outLevelL.load (std::memory_order_relaxed), dt);
    outR.feed (m.outLevelR.load (std::memory_order_relaxed), dt);

    float gain = m.limiterGain.load (std::memory_order_relaxed);
    gain = std::isfinite (gain) ? juce::jlimit (1.0e-3f, 1.0f, gain) : 1.0f;
    const float target = juce::jlimit (0.0f, 1.0f, -20.0f * std::log10 (gain) / 12.0f);
    reduction = target > reduction ? target : juce::jmax (target, reduction - dt * 0.7f);

    const float now[7] = { in.level, in.peak, outL.level, outL.peak, outR.level, outR.peak, reduction };
    const float threshold = 1.0f / juce::jmax (40.0f, float (getWidth()));
    bool changed = false;

    for (int i = 0; i < 7; ++i)
        changed = changed || std::abs (now[i] - lastPainted[i]) > threshold;

    if (changed)
    {
        for (int i = 0; i < 7; ++i)
            lastPainted[i] = now[i];
        repaint();
    }
}

void LevelMeters::drawBar (juce::Graphics& g, juce::Rectangle<float> r, const Bar& bar) const
{
    const float radius = r.getHeight() * 0.5f;

    g.setColour (col::hairline.withAlpha (0.10f));
    g.fillRoundedRectangle (r, radius);

    if (bar.level > 0.004f)
    {
        juce::ColourGradient grad (col::leaf.withAlpha (0.85f), r.getX(), 0.0f, col::blossom, r.getRight(), 0.0f, false);
        grad.addColour (0.70, col::leaf);
        grad.addColour (0.90, col::sun);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (r.withWidth (juce::jmax (r.getHeight(), r.getWidth() * bar.level)), radius);
    }

    if (bar.peak > 0.02f)
    {
        const float w = juce::jmax (1.5f, ctx.px (1.6f));
        const float x = r.getX() + juce::jlimit (0.0f, r.getWidth() - w, r.getWidth() * bar.peak - w);
        g.setColour ((bar.peak > 0.93f ? col::blossom : col::text).withAlpha (0.9f));
        g.fillRect (juce::Rectangle<float> (x, r.getY(), w, r.getHeight()));
    }
}

void LevelMeters::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    const auto font = ctx.font (Type::small);
    const float capH = font.getHeight() * 1.25f;
    const float gap = ctx.px (9.0f);
    const float barH = juce::jmax (3.0f, ctx.px (4.0f));
    const float barGap = juce::jmax (1.5f, ctx.px (2.5f));

    const float total = b.getWidth() - gap * 2.0f;
    const float grW = total * 0.2f;
    const float inW = (total - grW) * 0.5f;

    auto inArea = b.removeFromLeft (inW);
    b.removeFromLeft (gap);
    auto outArea = b.removeFromLeft (inW);
    b.removeFromLeft (gap);
    auto grArea = b;

    const float blockH = capH + barH * 2.0f + barGap + ctx.px (3.0f);
    const float top = inArea.getCentreY() - blockH * 0.5f;
    const float barsY = top + capH + ctx.px (3.0f);

    g.setFont (font);
    g.setColour (col::muted.withAlpha (0.9f));
    g.drawText ("IN", inArea.withY (top).withHeight (capH), juce::Justification::centredLeft, false);
    g.drawText ("OUT", outArea.withY (top).withHeight (capH), juce::Justification::centredLeft, false);
    g.setColour (reduction > 0.02f ? col::blossom : col::muted.withAlpha (0.9f));
    g.drawText ("GR", grArea.withY (top).withHeight (capH), juce::Justification::centredLeft, false);

    // input: one bar, vertically centred on the two output bars
    drawBar (g, { inArea.getX(), barsY + (barH + barGap) * 0.5f, inArea.getWidth(), barH }, in);
    drawBar (g, { outArea.getX(), barsY, outArea.getWidth(), barH }, outL);
    drawBar (g, { outArea.getX(), barsY + barH + barGap, outArea.getWidth(), barH }, outR);

    // gain reduction grows from the right
    {
        const juce::Rectangle<float> r (grArea.getX(), barsY + (barH + barGap) * 0.5f, grArea.getWidth(), barH);
        g.setColour (col::hairline.withAlpha (0.10f));
        g.fillRoundedRectangle (r, barH * 0.5f);

        if (reduction > 0.01f)
        {
            const float w = juce::jmax (r.getHeight(), r.getWidth() * reduction);
            g.setColour (col::blossom);
            g.fillRoundedRectangle (r.withLeft (r.getRight() - w), barH * 0.5f);
        }
    }
}

} // namespace verdant::ui
