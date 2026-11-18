// Verdant UI — the generative greenhouse scene. Procedural plants drawn with juce::Path,
// animated by the editor timer, reacting to the engine's Meters and the macro parameters.
//
// Cost model: everything static (backdrop, greenhouse frame, soil, glow sprites, frost) is
// rendered into images on resize only. Each frame rebuilds a small, fixed set of reusable
// juce::Path objects (cleared, never reallocated once warm) and fills each of them once.
#pragma once

#include <array>
#include <vector>

#include "Theme.h"

namespace verdant::ui
{

class GardenVisual final : public juce::Component
{
public:
    GardenVisual (UiContext&, bool compactStrip);
    ~GardenVisual() override;

    // Steps the simulation (meters, parameters, particles) and requests a repaint.
    void advance (float dtSeconds);

    struct PaintStats { double lastMs = 0.0, averageMs = 0.0, medianMs = 0.0, minMs = 0.0, maxMs = 0.0; int frames = 0; };
    PaintStats getPaintStats() const;
    void resetPaintStats() noexcept             { stats = {}; sampleCount = 0; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    //==============================================================================
    static constexpr int kMaxSegs      = 12;
    static constexpr int kMaxParticles = 200;
    static constexpr int kMaxNodes     = 48;
    static constexpr int kNumFireflies = 16;
    static constexpr int kNumMist      = 3;
    static constexpr int kNumLayers    = 3;   // 0 back (+ hanging vines), 1 mid, 2 front
    static constexpr int kRootPoints   = 8;

    struct Smooth
    {
        float v = 0.0f;
        void to (float target, float dt, float attack, float release) noexcept
        {
            const float tc = target > v ? attack : release;
            v += (target - v) * (1.0f - std::exp (-dt / juce::jmax (1.0e-4f, tc)));
        }
    };

    struct Stem
    {
        float x = 0, y = 0, len = 0, thick = 1;
        float angle0 = 0, curve = 0, curl = 0;
        float phase = 0, swayAmp = 0, swayRate = 1, driftAmt = 0;
        float rank = 0;                 // bloom threshold at which the stem appears
        float blossom = 0;              // blossom radius (0 = none)
        float blossomRank = 0;
        int   petals = 0;
        int   leaf0 = 0, leafN = 0;
        int   node = -1;                // index into nodeFlash, -1 if not an arp node
        juce::uint8 layer = 1, segs = kMaxSegs, drift = 0, cream = 0, hanging = 0;
        juce::uint16 cluster = 0;
    };

    struct Leaf
    {
        float t = 0, side = 1, len = 0, width = 0.3f, angle = 0.9f;
        float rank = 0, phase = 0, dew = 1;
    };

    struct Root
    {
        float x[kRootPoints] {}, y[kRootPoints] {};
        float start = 0, span = 1, thick = 1;
    };

    struct Particle
    {
        float x = 0, y = 0, vx = 0, vy = 0;
        float age = 0, life = 0, size = 1, wobble = 0;
        int kind = 0;
    };

    struct LayerPaths
    {
        juce::Path vein, petalPink, petalCream, core;
        void clear();
    };

    struct ClusterPaths
    {
        juce::Path lit, shade;
        int layer = 1;
    };

    struct MistBlob { float x = 0, y = 0, speed = 0, alpha = 1, phase = 0; };
    struct Firefly  { float cx = 0, cy = 0, ax = 0, ay = 0, wx = 0, wy = 0, px = 0, py = 0, blink = 0, rank = 0; };

    //==============================================================================
    void buildScene();
    void renderCaches (float physicalScale);
    void renderStatic (juce::Graphics&) const;
    void renderFrost (juce::Graphics&) const;
    void readInputs (float dt);
    void updateParticles (float dt);
    void spawnParticle (int kind);
    void triggerNode (int degree);
    float presenceOf (const Stem&) const noexcept;
    void buildFrame();
    void drawLayer (juce::Graphics&, int layer, juce::Colour low, juce::Colour mid, juce::Colour high);
    void drawSprite (juce::Graphics&, const juce::Image&, float x, float y, float opacity) const;
    float soilAt (float x) const noexcept;

    //==============================================================================
    UiContext& ctx;
    const bool compact;

    // --- scene (rebuilt on resize)
    float W = 0, H = 0, soilY = 0, unit = 1;
    std::vector<Stem> stems;
    std::vector<Leaf> leaves;
    std::vector<Root> roots;
    std::vector<juce::Point<float>> tips;       // per stem, written each frame
    std::vector<float> tipPresence;             // per stem, 0 = not grown
    std::vector<int> nodeStem;                  // node index -> stem index, sorted low to high
    std::array<MistBlob, kNumMist> mist {};
    std::array<Firefly, kNumFireflies> fireflies {};
    juce::Point<float> heroCentre;

    // --- cached layers
    juce::Image staticLayer, frostLayer, sunSprite, mistSprite, haloSprite;
    float cacheScale = 0.0f;
    bool cacheDirty = true;
    juce::Path cornerMask, borderPath, shaftPath;
    juce::Point<float> sunPos;

    // --- animation state
    double time = 0.0;
    float swayPhase = 0.0f;
    Smooth bloom, growth, moisture, sunlight, rootDepth, mix;
    Smooth energy, wet, low, mid, high, activity, freeze, bypass;
    float shiver = 0.0f;
    float drift[4] {};
    bool inputsPrimed = false;
    int lastArp = 0, lastGrain = 0, lastStutter = 0;
    float ambientSpawn = 0.0f;
    float grainPan = 0.8f;

    std::array<Particle, kMaxParticles> particles {};
    int particleCursor = 0;
    std::array<float, kMaxNodes> nodeFlash {};
    juce::Random rng { 0x1eafb10dLL };

    // --- per-frame geometry (reused, never shrunk)
    std::array<LayerPaths, kNumLayers> layers;
    std::vector<ClusterPaths> clusters;                     // one per plant
    juce::Path rootPath, rootTipPath, dewPathA, dewPathB, budPath, glintPath;
    std::array<juce::Path, 3> ringPaths;                    // arp rings by brightness
    std::array<juce::Path, 3> haloPaths;                    // by particle kind
    std::array<std::array<juce::Path, 3>, 3> corePaths;     // [kind][alpha bucket]

    PaintStats stats;
    std::array<float, 256> samples {};      // recent paint times, for the median
    int sampleCount = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GardenVisual)
};

} // namespace verdant::ui
