// The sleeve's hero: drifting light-forms seen through fogged glass.
//
// Pipeline (all on the message thread, driven by the editor's 30 Hz timer):
//   1. renderLight()   additive float light at kW x kH: night gradient, twelve
//                      drifting light-forms, the two scope ribbons, per-pixel trails
//   2. developFrame()  displacement warp, tone-map, vignette, fade -> 8-bit kW x kH
//   3. composite()     (lazily, in paint) bilinear upscale to device pixels with
//                      film grain and the un-blurred signal trace, in one pass of
//                      plain integer loops. The upscale *is* the blur.
//   4. paint()         one untransformed blit + the title text
//
//   audio   scope -> the two (L / R) ribbons; low / mid / high levels -> the
//           three families of light-forms
//   Wall    heat, density, grain          Wash    size, bloom, lingering trails
//   Glide   slow wavering displacement    Blur    bokeh edge -> fully melted
//   Warmth  colour temperature            Mix     how much haze surrounds the dry line
#pragma once

#include "Theme.h"

class SwoonAudioProcessor;

namespace swoon::ui
{

class HazeView final : public juce::Component
{
public:
    explicit HazeView (SwoonAudioProcessor&);

    /** One animation step; driven by the editor's 30 Hz timer. */
    void advance();

    /** Title block printed over the artwork (preset name, category / note). */
    void setSleeveText (const juce::String& title, const juce::String& category, const juce::String& note);

    void paint (juce::Graphics&) override;

private:
    static constexpr int kW = 250, kH = 70;
    static constexpr int kScopeFrames = 512, kScopeShown = 150;
    static constexpr int kGrainSize = 256;      // power of two

    struct Rgb { float r, g, b; };

    void readInputs();
    void renderLight();
    void developFrame();
    void prepareCanvas (int width, int height, float scale);
    void composite();
    void drawTrace (juce::Image::BitmapData&, const std::array<float, kW>& lowResY, Rgb colour, float alpha);
    static Rgb ramp (float position) noexcept;

    SwoonAudioProcessor& processor;

    // low-resolution light
    std::vector<float> light, trail, ribbon, ribbonTrail, vignette;
    std::array<float, kH> fadeRow {};
    std::array<float, 1024> dither {};
    std::vector<juce::uint8> developed;          // kW * kH * 3, in PixelRGB byte order (b, g, r)

    // device-resolution canvas
    juce::Image canvas;
    float canvasScale = 0.0f;
    bool canvasDirty = true;
    std::vector<juce::uint8> widened;            // kH rows stretched to the canvas width
    std::vector<int> columnIndex, columnWeight;  // horizontal resampling table
    std::vector<juce::int16> grainRow;
    std::vector<juce::int8> grainTile;
    int grainX = 0, grainY = 0;

    // scope
    std::array<float, kScopeFrames> scopeL {}, scopeR {};
    std::array<float, kW> waveL {}, waveR {}, warpY {}, traceL {}, traceR {};
    Rgb traceColourL {}, traceColourR {};
    float traceAlpha = 0.0f;

    // smoothed inputs
    float low = 0.0f, mid = 0.0f, high = 0.0f, level = 0.0f;
    float wall = 0.5f, wash = 0.5f, glide = 0.5f, blur = 0.5f, warmth = 0.5f, mix = 1.0f, bypassed = 0.0f;
    bool firstFrame = true;

    double time = 0.0;
    juce::uint32 lastTick = 0;
    int frameCounter = 0;

    juce::String title, category, note;
    juce::Font titleFont, categoryFont, noteFont;
    float categoryWidth = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HazeView)
};

} // namespace swoon::ui
