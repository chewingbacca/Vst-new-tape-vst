#include "Theme.h"

#if SWOON_HAS_ASSETS
 #include "BinaryData.h"
#endif

#include <cstring>

namespace swoon::ui
{

namespace
{
const Fonts* currentFonts = nullptr;

#if SWOON_HAS_ASSETS
juce::Typeface::Ptr loadEmbeddedTypeface (const char* originalFilename)
{
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
    {
        if (std::strcmp (BinaryData::originalFilenames[i], originalFilename) == 0)
        {
            int size = 0;
            if (const char* data = BinaryData::getNamedResource (BinaryData::namedResourceList[i], size))
                return juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
        }
    }
    return {};
}
#endif

juce::Typeface::Ptr withWeight (const juce::Typeface::Ptr& base, float weight)
{
    if (base == nullptr)
        return {};

    const juce::FontVariableSetting settings[] { { juce::FontFeatureTag { "wght" }, weight } };
    if (auto clone = base->cloneWithVariableSettings (settings))
        return clone;

    return base;
}
} // namespace

//==============================================================================
Fonts::Fonts()
{
   #if SWOON_HAS_ASSETS
    serifItalic = loadEmbeddedTypeface ("InstrumentSerif-Italic.ttf");

    if (auto jost = loadEmbeddedTypeface ("Jost-Variable.ttf"))
    {
        sans       = withWeight (jost, 400.0f);
        sansMedium = withWeight (jost, 500.0f);
        sansSemi   = withWeight (jost, 600.0f);
    }

    embedded = serifItalic != nullptr && sans != nullptr;
   #endif

    currentFonts = this;
}

Fonts::~Fonts()
{
    if (currentFonts == this)
        currentFonts = nullptr;
}

juce::Font serifFont (float height)
{
    if (currentFonts != nullptr && currentFonts->serifItalic != nullptr)
        return juce::Font (juce::FontOptions (currentFonts->serifItalic).withHeight (height));

    return juce::Font (juce::FontOptions (juce::Font::getDefaultSerifFontName(), height, juce::Font::italic));
}

juce::Font sansFont (float height, int weight, float tracking)
{
    juce::Typeface::Ptr tf;
    if (currentFonts != nullptr)
        tf = weight >= 2 ? currentFonts->sansSemi : weight == 1 ? currentFonts->sansMedium : currentFonts->sans;

    // Jost's line box is tall for its x-height: scale so "height" is comparable to a system sans.
    auto options = tf != nullptr ? juce::FontOptions (tf).withHeight (height * 1.18f)
                                 : juce::FontOptions (height, weight >= 1 ? juce::Font::bold : juce::Font::plain);

    return juce::Font (options.withKerningFactor (tracking));
}

float textWidth (const juce::Font& font, const juce::String& text)
{
    return juce::GlyphArrangement::getStringWidth (font, text);
}

//==============================================================================
juce::Colour moduleColour (Module m) noexcept
{
    switch (m)
    {
        case Module::Glide:   return col::lavender;
        case Module::Fuzz:    return col::rose;
        case Module::Amp:     return col::peach;
        case Module::Mod:     return col::orchid;
        case Module::Focus:   return col::sky;
        case Module::Reverse: return col::periwinkle;
        case Module::Wash:    return col::seaglass;
        case Module::Tape:    return col::amber;
        case Module::Output:  return col::fog;
        case Module::Global:  return col::mist;
    }
    return col::fog;
}

const char* moduleName (Module m) noexcept
{
    switch (m)
    {
        case Module::Glide:   return "Glide";
        case Module::Fuzz:    return "Fuzz";
        case Module::Amp:     return "Amp";
        case Module::Mod:     return "Mod";
        case Module::Focus:   return "Focus";
        case Module::Reverse: return "Reverse";
        case Module::Wash:    return "Wash";
        case Module::Tape:    return "Tape";
        case Module::Output:  return "Output";
        case Module::Global:  return "Global";
    }
    return "";
}

const ParamDef* powerParamFor (Module m) noexcept
{
    if (m == Module::Global)
        return nullptr;

    for (const auto& d : paramDefs())
        if (d.module == m && d.kind == ParamKind::Bool && juce::String (d.id).endsWith ("On"))
            return &d;

    return nullptr;
}

//==============================================================================
void strokeArc (juce::Graphics& g, juce::Point<float> c, float radius, float from, float to, float thickness, bool rounded)
{
    juce::Path p;
    p.addCentredArc (c.x, c.y, radius, radius, 0.0f, from, to, true);
    g.strokePath (p, juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                                           rounded ? juce::PathStrokeType::rounded : juce::PathStrokeType::butt));
}

void fillGlow (juce::Graphics& g, juce::Point<float> c, float radius, juce::Colour colour)
{
    juce::ColourGradient grad (colour, c.x, c.y, colour.withAlpha (0.0f), c.x + radius, c.y, true);
    grad.addColour (0.45, colour.withMultipliedAlpha (0.38f));
    g.setGradientFill (grad);
    g.fillEllipse (c.x - radius, c.y - radius, radius * 2.0f, radius * 2.0f);
}

void drawPanel (juce::Graphics& g, juce::Rectangle<float> b, float corner, juce::Colour tint, float tintAmount)
{
    const auto top = col::panel.brighter (0.06f).interpolatedWith (tint, tintAmount);
    const auto bottom = col::panel.darker (0.12f).interpolatedWith (tint, tintAmount * 0.35f);

    g.setGradientFill (juce::ColourGradient (top, b.getX(), b.getY(), bottom, b.getX(), b.getBottom(), false));
    g.fillRoundedRectangle (b, corner);

    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.11f), b.getX(), b.getY(),
                                             juce::Colours::white.withAlpha (0.03f), b.getX(), b.getBottom(), false));
    g.drawRoundedRectangle (b.reduced (0.5f), corner, 1.0f);
}

void drawFitted (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, juce::Justification just,
                 juce::Font font, float minScale)
{
    const float w = textWidth (font, text);
    if (w > area.getWidth() && w > 0.0f)
        font.setHorizontalScale (juce::jmax (minScale, area.getWidth() / w));

    g.setFont (font);
    g.drawText (text, area, just, true);
}

//==============================================================================
SwoonLookAndFeel::SwoonLookAndFeel()
{
    setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::textColourId, col::fog);
    setColour (juce::TextEditor::highlightColourId, col::lavender.withAlpha (0.35f));
    setColour (juce::TextEditor::highlightedTextColourId, col::fog);
    setColour (juce::CaretComponent::caretColourId, col::rose);
}

juce::Typeface::Ptr SwoonLookAndFeel::getTypefaceForFont (const juce::Font& font)
{
    if (currentFonts != nullptr && currentFonts->sans != nullptr
        && font.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
        return font.isBold() ? currentFonts->sansMedium : currentFonts->sans;

    return juce::LookAndFeel_V4::getTypefaceForFont (font);
}

void SwoonLookAndFeel::drawCornerResizer (juce::Graphics& g, int w, int h, bool isMouseOver, bool isMouseDragging)
{
    g.setColour (col::fog.withAlpha (isMouseOver || isMouseDragging ? 0.55f : 0.22f));

    const float fw = (float) w, fh = (float) h, d = juce::jmax (1.5f, fw * 0.11f);
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column <= row; ++column)
            g.fillEllipse (fw - (float) (column + 1) * fw * 0.26f - d * 0.5f,
                           fh - (float) (3 - row) * fh * 0.26f - d * 0.5f, d, d);
}

} // namespace swoon::ui
