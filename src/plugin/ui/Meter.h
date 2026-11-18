// Verdant UI — slim input / output level meters with a limiter gain-reduction indicator.
#pragma once

#include "Theme.h"

namespace verdant { struct Meters; }

namespace verdant::ui
{

class LevelMeters final : public juce::Component
{
public:
    explicit LevelMeters (UiContext&);
    ~LevelMeters() override;

    // Reads the engine's atomics and applies display ballistics. Repaints only when needed.
    void update (const verdant::Meters&, float dtSeconds);

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    struct Bar
    {
        float level = 0.0f;   // 0..1 on the display scale
        float peak = 0.0f;
        float hold = 0.0f;    // seconds the peak tick has left before it falls

        void feed (float linear, float dt);
    };

    void drawBar (juce::Graphics&, juce::Rectangle<float>, const Bar&) const;

    UiContext& ctx;
    Bar in, outL, outR;
    float reduction = 0.0f;         // 0..1 (0..12 dB)
    float lastPainted[7] {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LevelMeters)
};

} // namespace verdant::ui
