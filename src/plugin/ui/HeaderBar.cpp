#include "HeaderBar.h"

namespace verdant::ui
{

HeaderBar::HeaderBar (UiContext& c)
    : tabs (c, { "Garden", "Soil" },
            { "The living view with the six macros",
              "Every parameter of every module: tape, grains, arp, chorus, reverb, output" }),
      presetBar (c),
      meters (c),
      bypass (c, pBypass, Toggle::Style::button),
      ctx (c)
{
    bypass.setGlyph (Toggle::Glyph::power);
    bypass.setAccent (col::sun);

    addAndMakeVisible (tabs);
    addAndMakeVisible (presetBar);
    addAndMakeVisible (meters);
    addAndMakeVisible (bypass);
}

void HeaderBar::resized()
{
    const auto b = getLocalBounds().toFloat();
    const float cy = b.getCentreY() + ctx.px (1.0f);

    const auto place = [&] (juce::Component& comp, float x, float w, float h)
    {
        comp.setBounds (juce::Rectangle<float> (ctx.px (x), cy - ctx.px (h) * 0.5f, ctx.px (w), ctx.px (h)).toNearestInt());
    };

    place (tabs, 172.0f, 172.0f, 28.0f);
    place (presetBar, 372.0f, 308.0f, 30.0f);
    place (meters, 702.0f, 170.0f, 34.0f);
    place (bypass, 888.0f, 94.0f, 28.0f);
}

void HeaderBar::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const float cy = b.getCentreY() + ctx.px (1.0f);

    // leaf + wordmark
    const float glyph = ctx.px (25.0f);
    const juce::Rectangle<float> ga (ctx.px (19.0f), cy - glyph * 0.5f, glyph, glyph);

    juce::ColourGradient halo (col::leaf.withAlpha (0.16f), ga.getCentreX(), ga.getCentreY(),
                               col::leaf.withAlpha (0.0f), ga.getCentreX() + glyph * 1.2f, ga.getCentreY(), true);
    g.setGradientFill (halo);
    g.fillEllipse (ga.expanded (glyph * 0.7f));
    drawLeafGlyph (g, ga, col::leaf);

    g.setFont (ctx.font (Type::wordmark));
    g.setColour (col::text);
    g.drawText ("Verdant", juce::Rectangle<float> (ga.getRight() + ctx.px (9.0f), b.getY(), ctx.px (120.0f), b.getHeight()).translated (0.0f, ctx.px (0.5f)),
                juce::Justification::centredLeft, false);

    // hairline under the header
    const float line = juce::jmax (1.0f, ctx.scale);
    juce::ColourGradient rule (col::hairline.withAlpha (0.0f), b.getX(), 0.0f, col::hairline.withAlpha (0.0f), b.getRight(), 0.0f, false);
    rule.addColour (0.12, col::hairline.withAlpha (0.14f));
    rule.addColour (0.88, col::hairline.withAlpha (0.14f));
    g.setGradientFill (rule);
    g.fillRect (juce::Rectangle<float> (b.getX(), b.getBottom() - line, b.getWidth(), line));
}

} // namespace verdant::ui
