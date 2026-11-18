// Verdant UI — small helpers shared by GardenVisual.cpp (animation, painting) and
// GardenScene.cpp (scene generation, cached layers). Not part of the public UI interface.
#pragma once

#include <atomic>
#include <cmath>

#include <juce_graphics/juce_graphics.h>

namespace verdant::ui::garden
{

constexpr float kPi    = juce::MathConstants<float>::pi;
constexpr float kTwoPi = juce::MathConstants<float>::twoPi;

// repaint governor: full rate while a paint costs no more than the budget
constexpr float kNominalFps = 30.0f, kMinFps = 15.0f, kPaintBudgetMs = 6.0f;

// sprite sizes relative to the visual (kept small: blending them is a per-frame cost)
constexpr float kSunRadius = 0.44f;
constexpr float kHaloW = 0.27f, kHaloH = 0.52f;
constexpr float kMistW = 0.50f, kMistH = 0.20f;

inline float sat (float v) noexcept             { return juce::jlimit (0.0f, 1.0f, v); }
inline float smooth01 (float v) noexcept        { v = sat (v); return v * v * (3.0f - 2.0f * v); }
inline juce::Colour rgb (juce::uint32 v)        { return juce::Colour (v); }

inline juce::Colour mixc (juce::Colour a, juce::Colour b, float t)
{
    return a.interpolatedWith (b, sat (t));
}

inline float readFinite (const std::atomic<float>& a) noexcept
{
    const float v = a.load (std::memory_order_relaxed);
    return std::isfinite (v) ? v : 0.0f;
}

// Cheap deterministic hash -> 0..1
inline float hash01 (int n) noexcept
{
    auto x = juce::uint32 (n) * 0x9e3779b9u;
    x ^= x >> 15;  x *= 0x85ebca6bu;
    x ^= x >> 13;  x *= 0xc2b2ae35u;
    x ^= x >> 16;
    return float (x & 0xffffffu) / float (0x1000000);
}

inline void addDot (juce::Path& p, float x, float y, float r)
{
    if (r >= 1.7f)
    {
        p.addEllipse (x - r, y - r, r * 2.0f, r * 2.0f);
    }
    else
    {
        const float d = r * 1.25f;
        p.startNewSubPath (x, y - d);
        p.lineTo (x + d, y);
        p.lineTo (x, y + d);
        p.lineTo (x - d, y);
        p.closeSubPath();
    }
}

inline void addStar (juce::Path& p, float x, float y, float r)
{
    const float w = r * 0.16f;
    p.startNewSubPath (x, y - r);
    p.lineTo (x + w, y - w);   p.lineTo (x + r, y);
    p.lineTo (x + w, y + w);   p.lineTo (x, y + r);
    p.lineTo (x - w, y + w);   p.lineTo (x - r, y);
    p.lineTo (x - w, y - w);
    p.closeSubPath();
}

} // namespace verdant::ui::garden
