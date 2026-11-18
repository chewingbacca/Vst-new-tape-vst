// Verdant UI — palette, type scale, shared drawing helpers and the context object
// every component receives (scale factor, processor access, hover/help routing).
#pragma once

#include <functional>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "../../dsp/Params.h"

class VerdantProcessor;

namespace verdant::ui
{

//==============================================================================
namespace col
{
    inline const juce::Colour bgTop      { 0xff0a1712 };
    inline const juce::Colour bgBottom   { 0xff10231a };
    inline const juce::Colour panel      { 0xff132b20 };
    inline const juce::Colour panelHi    { 0xff183628 };
    inline const juce::Colour well       { 0xff0c1c15 };   // recessed surfaces (pills, text fields)
    inline const juce::Colour hairline   { 0xffb9d7a8 };   // use with low alpha
    inline const juce::Colour leaf       { 0xffbfe6a0 };
    inline const juce::Colour dew        { 0xff9fe3cf };
    inline const juce::Colour sun        { 0xfff2d68a };
    inline const juce::Colour blossom    { 0xfff0b3c2 };
    inline const juce::Colour ice        { 0xffcfeff6 };
    inline const juce::Colour text       { 0xffe6f0dc };
    inline const juce::Colour muted      { 0xff8fa893 };
    inline const juce::Colour faint      { 0xff5d7565 };
}

// Design size of the editor; everything is derived from (width / kBaseWidth).
inline constexpr int   kBaseWidth  = 1000;
inline constexpr int   kBaseHeight = 660;
inline constexpr float kMinScale   = 0.75f;
inline constexpr float kMaxScale   = 1.70f;

//==============================================================================
enum class Type
{
    wordmark,     // "Verdant"
    title,        // overlay titles, preset name
    section,      // module headers (upper-case, tracked)
    macroName,    // macro knob captions (upper-case, tracked)
    macroValue,
    label,        // parameter names
    value,        // parameter values
    body,         // help line, descriptions
    small         // tiny upper-case captions
};

juce::Font makeFont (Type, float scale);

// Accent colour for a parameter group ("Tape", "Grain", ...). Unknown groups get the leaf green.
juce::Colour accentForGroup (juce::StringRef group);

// True for parameters whose default sits inside the range on a zero crossing (Tilt, Speed, Pitch, Gain).
bool isBipolar (const ParamDef&);

//==============================================================================
// Drawing helpers.

// Appends an almond-shaped leaf. (dx, dy) is the unit direction from base to tip.
void addLeaf (juce::Path&, float baseX, float baseY, float dx, float dy, float length, float halfWidthRatio);

// The small logo / bullet glyph: a leaf with a midrib, pointing up-right.
void drawLeafGlyph (juce::Graphics&, juce::Rectangle<float> area, juce::Colour, float veinAlpha = 0.55f);

// Rounded panel with a soft top highlight and a hairline border.
void drawPanel (juce::Graphics&, juce::Rectangle<float> area, float cornerRadius, float scale,
                juce::Colour fill = col::panel, float borderAlpha = 0.12f);

// A recessed pill / field background.
void drawWell (juce::Graphics&, juce::Rectangle<float> area, float cornerRadius, float scale,
               bool highlighted = false, juce::Colour accent = col::leaf);

// Soft shadow under floating panels (cheap: a few expanding translucent rounded rects).
void drawSoftShadow (juce::Graphics&, juce::Rectangle<float> area, float cornerRadius, float spread, float alpha);

void drawChevron (juce::Graphics&, juce::Rectangle<float> area, bool pointsRight, juce::Colour, float thickness);

// Editor backdrop gradient (in editor coordinates).
juce::ColourGradient backdropGradient (juce::Rectangle<float> editorBounds);

//==============================================================================
class ChoicePill;

// Shared by every component of one editor instance.
struct UiContext
{
    explicit UiContext (VerdantProcessor& p) : proc (p) {}

    VerdantProcessor& proc;
    float scale = 1.0f;

    float px (float designPixels) const noexcept      { return designPixels * scale; }
    int   pxi (float designPixels) const noexcept     { return juce::roundToInt (designPixels * scale); }
    juce::Font font (Type t) const                    { return makeFont (t, scale); }

    // Help line routing. `owner` identifies who set the text so a late mouseExit from one
    // control cannot clear the text of the control the mouse has just entered.
    std::function<void (juce::Component* owner, const juce::String& title,
                        juce::RangedAudioParameter* valueSource, const juce::String& help)> hoverBegin;
    std::function<void (juce::Component* owner)> hoverEnd;

    // Opens the in-editor choice list for a pill.
    std::function<void (ChoicePill&)> showChoices;

    // The editor component (for backdrop-aligned painting of opaque children).
    juce::Component* editor = nullptr;

    void setHover (juce::Component* owner, const juce::String& title,
                   juce::RangedAudioParameter* p, const juce::String& help) const
    {
        if (hoverBegin) hoverBegin (owner, title, p, help);
    }

    void clearHover (juce::Component* owner) const
    {
        if (hoverEnd) hoverEnd (owner);
    }
};

//==============================================================================
// Look-and-feel for the few stock JUCE widgets used (scroll bars, text editor, corner resizer).
class VerdantLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    VerdantLookAndFeel();

    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height,
                        bool isScrollbarVertical, int thumbStartPosition, int thumbSize,
                        bool isMouseOver, bool isMouseDown) override;
    void fillTextEditorBackground (juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawCornerResizer (juce::Graphics&, int w, int h, bool isMouseOver, bool isMouseDragging) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VerdantLookAndFeel)
};

} // namespace verdant::ui
