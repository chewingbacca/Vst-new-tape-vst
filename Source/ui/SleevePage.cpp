#include "SleevePage.h"

#include "../PluginProcessor.h"

namespace swoon::ui
{

namespace
{
constexpr int kHazeH = 284;
constexpr int kMacroTop = 290;
constexpr int kMacroH = 156;
constexpr int kChainH = 84;

struct MacroDef
{
    P param;
    juce::Colour colour;
    bool detent;     // 0.5 = "as the preset designed it"
};
} // namespace

SleevePage::SleevePage (SwoonAudioProcessor& processor, ChainModel& model)
    : haze (processor), chain (processor, model)
{
    const MacroDef defs[] = {
        { P::wall,   col::rose,     true },
        { P::wash,   col::seaglass, true },
        { P::glide,  col::lavender, true },
        { P::blur,   col::sky,      true },
        { P::warmth, col::peach,    true },
        { P::mix,    col::fog,      false },
    };

    addAndMakeVisible (haze);

    for (const auto& d : defs)
    {
        auto knob = std::make_unique<Knob> (processor.getParam (d.param), Knob::Style::macro, def (d.param).name, d.colour);
        knob->setCentreDetent (d.detent);
        addAndMakeVisible (*knob);
        macros.push_back (std::move (knob));
    }

    addAndMakeVisible (chain);
}

void SleevePage::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();

    // the haze repaints 30 times a second and covers its own area: only fill a thin rim beneath it
    g.saveState();
    g.excludeClipRegion (haze.getBounds().reduced (3));
    g.setGradientFill (juce::ColourGradient (col::night, 0.0f, (float) kHazeH, col::ink, 0.0f, b.getHeight(), false));
    g.fillRect (b);
    g.restoreState();

    // hairline between the macros and the chain
    const float y = (float) (kMacroTop + kMacroH) + 9.0f;
    juce::ColourGradient line (juce::Colours::transparentWhite, 40.0f, y, juce::Colours::transparentWhite, b.getWidth() - 40.0f, y, false);
    line.addColour (0.5, juce::Colours::white.withAlpha (0.09f));
    g.setGradientFill (line);
    g.fillRect (40.0f, y, b.getWidth() - 80.0f, 1.0f);
}

void SleevePage::resized()
{
    haze.setBounds (0, 0, getWidth(), kHazeH);

    const int n = (int) macros.size();
    const float pitch = ((float) getWidth() - 60.0f) / (float) n;

    for (int i = 0; i < n; ++i)
    {
        const float cx = 30.0f + pitch * ((float) i + 0.5f);
        macros[(size_t) i]->setBounds (juce::roundToInt (cx - 70.0f), kMacroTop, 140, kMacroH);
    }

    chain.setBounds (22, getHeight() - kChainH - 18, getWidth() - 44, kChainH);
}

} // namespace swoon::ui
