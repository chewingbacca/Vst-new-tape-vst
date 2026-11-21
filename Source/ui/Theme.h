// Swoon UI theme: palette, embedded fonts and a few shared drawing helpers.
// Everything in the editor is drawn in "base" coordinates (kBaseW x kBaseH) and
// scaled by one transform, so all metrics here are base pixels.
#pragma once

#include "../dsp/Params.h"

#include <juce_audio_utils/juce_audio_utils.h>

namespace swoon::ui
{

inline constexpr int kBaseW = 1000;
inline constexpr int kBaseH = 660;
inline constexpr int kHeaderH = 54;

//==============================================================================
namespace col
{
    // dusk base
    inline const juce::Colour ink        { 0xff0b0a1b };   // deepest background
    inline const juce::Colour night      { 0xff131027 };   // page background
    inline const juce::Colour panel      { 0xff1a1634 };   // raised surfaces
    inline const juce::Colour panelHi    { 0xff241f47 };   // hover / wells
    inline const juce::Colour hairline   { 0x16ffffff };

    // text
    inline const juce::Colour fog        { 0xfff0eaf7 };   // primary
    inline const juce::Colour mist       { 0xffaea7c9 };   // secondary
    inline const juce::Colour dusk       { 0xff78729b };   // tertiary / disabled

    // bloom
    inline const juce::Colour rose       { 0xfff59ab9 };
    inline const juce::Colour peach      { 0xffffbd97 };
    inline const juce::Colour amber      { 0xfff3d391 };
    inline const juce::Colour lavender   { 0xffb9a5f7 };
    inline const juce::Colour seaglass   { 0xff8edccb };
    inline const juce::Colour sky        { 0xff9ac6f5 };
    inline const juce::Colour orchid     { 0xffdb9de9 };
    inline const juce::Colour periwinkle { 0xffa3aaf7 };
}

/** Accent colour of a module (panels, chain chips). */
juce::Colour moduleColour (Module) noexcept;

/** Display name of a module ("Glide", "Fuzz" ... "Output", "Global"). */
const char* moduleName (Module) noexcept;

/** The module's power parameter (a Bool whose id ends in "On"), or nullptr. */
const ParamDef* powerParamFor (Module) noexcept;

//==============================================================================
/** Embedded typefaces. One instance is shared by all open editors through a
    juce::SharedResourcePointer and dies with the last editor, so no typeface
    outlives JUCE's leak detector. */
struct Fonts
{
    Fonts();
    ~Fonts();

    juce::Typeface::Ptr serifItalic, sans, sansMedium, sansSemi;
    bool embedded = false;
};

/** Display serif (italic), used for the wordmark and titles. */
juce::Font serifFont (float height);

/** UI sans. weight: 0 = regular, 1 = medium, 2 = semibold. tracking is in ems. */
juce::Font sansFont (float height, int weight = 0, float tracking = 0.0f);

float textWidth (const juce::Font&, const juce::String&);

//==============================================================================
// Rotary geometry shared by every knob: 270 degree sweep, angles clockwise from 12 o'clock.
inline constexpr float kArcStart = juce::MathConstants<float>::pi * 1.25f;
inline constexpr float kArcEnd   = juce::MathConstants<float>::pi * 2.75f;
inline float arcAngle (float proportion) noexcept { return kArcStart + (kArcEnd - kArcStart) * proportion; }

void strokeArc (juce::Graphics&, juce::Point<float> centre, float radius, float fromAngle, float toAngle,
                float thickness, bool rounded = true);

/** Fills a soft radial glow (colour -> transparent). */
void fillGlow (juce::Graphics&, juce::Point<float> centre, float radius, juce::Colour);

/** Raised panel: soft vertical gradient, hairline border, faint top light. */
void drawPanel (juce::Graphics&, juce::Rectangle<float> bounds, float corner, juce::Colour tint = {}, float tintAmount = 0.0f);

/** Text that shrinks horizontally (down to minScale) before it is truncated. */
void drawFitted (juce::Graphics&, const juce::String&, juce::Rectangle<float>, juce::Justification,
                 juce::Font font, float minScale = 0.78f);

//==============================================================================
class SwoonLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    SwoonLookAndFeel();

    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;
    void drawCornerResizer (juce::Graphics&, int w, int h, bool isMouseOver, bool isMouseDragging) override;
    void fillTextEditorBackground (juce::Graphics&, int, int, juce::TextEditor&) override {}
    void drawTextEditorOutline (juce::Graphics&, int, int, juce::TextEditor&) override {}
};

} // namespace swoon::ui
