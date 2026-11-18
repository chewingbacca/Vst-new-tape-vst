// The static half of the garden: procedural scene generation (plants, roots, arp nodes)
// and the layers that are rendered once per resize (backdrop, frost, glow sprites).
#include "GardenVisual.h"

#include "GardenVisualDetail.h"

namespace verdant::ui
{

using namespace garden;

float GardenVisual::soilAt (float x) const noexcept
{
    const float n = x / juce::jmax (1.0f, W);
    return soilY + unit * (compact ? 0.8f : 1.0f) * (3.0f * std::sin (n * 7.1f + 0.6f) + 1.8f * std::sin (n * 17.3f + 2.1f));
}

//==============================================================================
void GardenVisual::resized()
{
    buildScene();
}

void GardenVisual::buildScene()
{
    stems.clear();
    leaves.clear();
    roots.clear();
    nodeStem.clear();
    nodeFlash.fill (0.0f);
    cacheDirty = true;

    for (auto& p : particles)
        p.life = 0.0f;

    W = float (getWidth());
    H = float (getHeight());

    if (W < 8.0f || H < 8.0f)
    {
        tips.clear();
        tipPresence.clear();
        return;
    }

    unit = juce::jmax (0.5f, ctx.scale);
    soilY = H * (compact ? 0.86f : 0.765f);

    juce::Random r (0x5eed1eaf);
    const auto rnd = [&r] (float a, float b) { return a + (b - a) * r.nextFloat(); };

    struct RootBase { float x, strength; };
    std::vector<RootBase> rootBases;
    std::vector<int> nodeCandidates;

    // Every plant gets its own pair of leaf paths ("cluster"): small paths keep the
    // rasteriser's per-scanline edge lists short, which is what its cost depends on.
    int numClusters = 0;
    int currentCluster = 0;
    std::vector<int> clusterLayers;
    const auto beginCluster = [&] (int layer)
    {
        currentCluster = numClusters++;
        clusterLayers.push_back (layer);
    };

    const auto finishStem = [this, &nodeCandidates, &currentCluster] (Stem& s, bool isNode)
    {
        s.cluster = juce::uint16 (currentCluster);
        stems.push_back (s);
        if (isNode)
            nodeCandidates.push_back (int (stems.size()) - 1);
    };

    // ---------------------------------------------------------------- broad-leaved plant
    const auto addBroadleaf = [&] (float xf, float hf, int n, int layer, float spread, float rankBase,
                                   float blossomChance, float leafScale, int leafCount)
    {
        beginCluster (layer);
        const float x = xf * W;
        const float plantH = hf * soilY;

        for (int i = 0; i < n; ++i)
        {
            const float frac = n > 1 ? float (i) / float (n - 1) - 0.5f : 0.0f;
            const float af = std::abs (frac);

            Stem s;
            s.x = x + frac * plantH * 0.06f;
            s.y = soilAt (s.x) + unit;
            s.len = plantH * (1.0f - 0.55f * af) * rnd (0.9f, 1.0f);
            s.thick = juce::jmax (1.1f * unit, s.len * 0.0115f);
            s.angle0 = frac * spread * 0.55f + rnd (-0.05f, 0.05f);
            s.curve = frac * spread * 1.05f + rnd (-0.08f, 0.08f);
            s.curl = frac * 0.5f;
            s.phase = rnd (0.0f, kTwoPi);
            s.swayAmp = rnd (0.05f, 0.085f);
            s.swayRate = rnd (0.8f, 1.2f);
            s.drift = juce::uint8 (r.nextInt (4));
            s.driftAmt = rnd (0.05f, 0.12f);
            s.rank = af < 0.2f ? rankBase * 0.4f : rankBase + af * 0.9f;
            s.layer = juce::uint8 (layer);
            s.segs = juce::uint8 (compact ? 8 : kMaxSegs);
            s.leaf0 = int (leaves.size());

            const bool flower = r.nextFloat() < blossomChance;

            for (int k = 0; k < leafCount; ++k)
            {
                Leaf l;
                l.t = 0.2f + 0.72f * (float (k) + rnd (0.0f, 0.4f)) / float (leafCount);
                l.side = (k & 1) != 0 ? 1.0f : -1.0f;
                const float bell = std::sin (kPi * std::pow (l.t, 0.85f));
                l.len = s.len * (0.10f + 0.135f * bell) * leafScale * rnd (0.86f, 1.1f);
                l.width = rnd (0.30f, 0.38f);
                l.angle = rnd (0.7f, 1.05f);
                l.rank = k < 2 ? 0.0f : rnd (0.08f, 1.0f);
                l.phase = rnd (0.0f, kTwoPi);
                l.dew = r.nextFloat();
                leaves.push_back (l);
            }

            if (! flower)
            {
                Leaf l;   // terminal leaf continues the stem
                l.t = 1.0f;
                l.side = frac >= 0.0f ? 1.0f : -1.0f;
                l.len = s.len * 0.15f * leafScale;
                l.width = 0.33f;
                l.angle = 0.12f;
                l.rank = 0.0f;
                l.phase = rnd (0.0f, kTwoPi);
                l.dew = r.nextFloat();
                leaves.push_back (l);
            }
            else
            {
                s.blossom = juce::jmax (compact ? 3.0f * unit : 7.0f * unit, plantH * 0.062f * rnd (0.85f, 1.2f));
                s.blossomRank = rnd (0.15f, 0.7f);
                s.petals = 5 + r.nextInt (3);
                s.cream = juce::uint8 (r.nextFloat() < 0.35f ? 1 : 0);
            }

            s.leafN = int (leaves.size()) - s.leaf0;
            finishStem (s, true);
        }
    };

    // ---------------------------------------------------------------- fern
    const auto addFern = [&] (float xf, float hf, int n, int layer, float rankBase, int pairs)
    {
        beginCluster (layer);
        const float x = xf * W;
        const float plantH = hf * soilY;

        for (int i = 0; i < n; ++i)
        {
            const float frac = n > 1 ? float (i) / float (n - 1) - 0.5f : 0.0f;
            const float af = std::abs (frac);
            const float sgn = frac >= 0.0f ? 1.0f : -1.0f;

            Stem s;
            s.x = x + frac * plantH * 0.05f;
            s.y = soilAt (s.x) + unit;
            s.len = plantH * (1.0f - 0.3f * af) * rnd (0.86f, 1.0f);
            s.thick = juce::jmax (0.9f * unit, s.len * 0.008f);
            s.angle0 = frac * 1.25f + rnd (-0.07f, 0.07f);
            s.curve = frac * 1.1f;
            s.curl = sgn * rnd (0.5f, 1.0f);
            s.phase = rnd (0.0f, kTwoPi);
            s.swayAmp = rnd (0.06f, 0.105f);
            s.swayRate = rnd (0.85f, 1.3f);
            s.drift = juce::uint8 (r.nextInt (4));
            s.driftAmt = rnd (0.06f, 0.13f);
            s.rank = af < 0.3f ? rankBase * 0.3f : rankBase + af * 0.8f;
            s.layer = juce::uint8 (layer);
            s.segs = juce::uint8 (compact ? 8 : kMaxSegs);
            s.leaf0 = int (leaves.size());

            for (int k = 0; k < pairs; ++k)
            {
                const float t = 0.14f + 0.83f * float (k) / float (juce::jmax (1, pairs - 1));
                const float rank = rnd (0.0f, 0.5f);

                for (float side : { -1.0f, 1.0f })
                {
                    Leaf l;
                    l.t = t;
                    l.side = side;
                    l.len = s.len * (0.21f * (1.0f - 0.8f * t) + 0.016f) * rnd (0.92f, 1.08f);
                    l.width = 0.24f;
                    l.angle = 1.25f - 0.4f * t;
                    l.rank = rank;
                    l.phase = rnd (0.0f, kTwoPi);
                    l.dew = 0.35f + 0.65f * r.nextFloat();
                    leaves.push_back (l);
                }
            }

            s.leafN = int (leaves.size()) - s.leaf0;
            finishStem (s, true);
        }
    };

    // ---------------------------------------------------------------- grass tuft
    const auto addGrass = [&] (float xf, float hf, int n, int layer, float rankBase)
    {
        beginCluster (layer);
        const float x = xf * W;
        const float plantH = hf * soilY;

        for (int i = 0; i < n; ++i)
        {
            Stem s;
            s.x = x + rnd (-1.0f, 1.0f) * plantH * 0.14f;
            s.y = soilAt (s.x) + unit;
            s.len = plantH * rnd (0.45f, 1.0f);
            s.thick = juce::jmax (1.2f * unit, s.len * 0.02f);
            s.angle0 = rnd (-0.35f, 0.35f);
            s.curve = (s.angle0 > 0.0f ? 1.0f : -1.0f) * rnd (0.2f, 1.1f);
            s.phase = rnd (0.0f, kTwoPi);
            s.swayAmp = rnd (0.08f, 0.15f);
            s.swayRate = rnd (0.9f, 1.4f);
            s.drift = juce::uint8 (r.nextInt (4));
            s.driftAmt = rnd (0.08f, 0.16f);
            s.rank = rankBase + rnd (0.0f, 0.7f);
            s.layer = juce::uint8 (layer);
            s.segs = 7;
            s.leaf0 = int (leaves.size());
            s.leafN = 0;
            finishStem (s, false);
        }
    };

    // ---------------------------------------------------------------- hanging vine
    const auto addVine = [&] (float xf, float lengthFrac)
    {
        Stem s;
        s.x = xf * W;
        s.y = -2.0f * unit;
        s.len = H * lengthFrac;
        s.thick = juce::jmax (0.9f, 1.1f * unit);
        s.angle0 = kPi + rnd (-0.05f, 0.05f);
        s.curve = rnd (-0.22f, 0.22f);
        s.phase = rnd (0.0f, kTwoPi);
        s.swayAmp = rnd (0.05f, 0.085f);
        s.swayRate = rnd (0.6f, 0.9f);
        s.drift = juce::uint8 (r.nextInt (4));
        s.driftAmt = rnd (0.05f, 0.1f);
        s.rank = rnd (0.0f, 0.45f);
        s.layer = 0;
        s.hanging = 1;
        s.segs = kMaxSegs;
        s.leaf0 = int (leaves.size());

        const int count = juce::jmax (4, juce::roundToInt (s.len / (13.0f * unit)));
        for (int k = 0; k <= count; ++k)
        {
            Leaf l;
            l.t = 0.08f + 0.92f * float (k) / float (count);
            l.side = (k & 1) != 0 ? 1.0f : -1.0f;
            l.len = unit * rnd (9.0f, 13.5f) * (1.0f - 0.3f * l.t);
            l.width = 0.43f;
            l.angle = k == count ? 0.1f : rnd (1.0f, 1.3f);
            l.rank = rnd (0.0f, 0.75f);
            l.phase = rnd (0.0f, kTwoPi);
            l.dew = r.nextFloat();
            leaves.push_back (l);
        }

        s.leafN = int (leaves.size()) - s.leaf0;
        finishStem (s, false);
    };

    // ---------------------------------------------------------------- sprout
    const auto addSprout = [&] (float xf)
    {
        Stem s;
        s.x = xf * W;
        s.y = soilAt (s.x) + unit;
        s.len = unit * rnd (14.0f, 26.0f);
        s.thick = juce::jmax (1.0f, 1.1f * unit);
        s.angle0 = rnd (-0.15f, 0.15f);
        s.curve = rnd (-0.3f, 0.3f);
        s.phase = rnd (0.0f, kTwoPi);
        s.swayAmp = rnd (0.07f, 0.12f);
        s.swayRate = rnd (1.0f, 1.5f);
        s.drift = juce::uint8 (r.nextInt (4));
        s.driftAmt = 0.1f;
        s.rank = rnd (0.25f, 0.95f);
        s.layer = 2;
        s.segs = 5;
        s.leaf0 = int (leaves.size());

        for (float side : { -1.0f, 1.0f })
        {
            Leaf l;
            l.t = 0.97f;
            l.side = side;
            l.len = s.len * rnd (0.45f, 0.6f);
            l.width = 0.4f;
            l.angle = rnd (0.8f, 1.05f);
            l.rank = 0.0f;
            l.phase = rnd (0.0f, kTwoPi);
            l.dew = r.nextFloat();
            leaves.push_back (l);
        }

        s.leafN = 2;
        finishStem (s, false);
    };

    // ---------------------------------------------------------------- composition
    if (! compact)
    {
        // back layer
        addBroadleaf (0.105f, 0.70f, 3, 0, 0.9f, 0.25f, 0.0f, 1.0f, 8);
        addGrass     (0.29f,  0.42f, 5, 0, 0.3f);
        addGrass     (0.42f,  0.36f, 4, 0, 0.5f);
        addBroadleaf (0.665f, 0.60f, 3, 0, 0.8f, 0.3f, 0.0f, 0.9f, 7);
        addGrass     (0.80f,  0.34f, 4, 0, 0.5f);
        addFern      (0.905f, 0.62f, 4, 0, 0.3f, 8);

        // middle layer: the hero plant between two ferns
        addFern      (0.235f, 0.56f, 4, 1, 0.15f, 9);
        addBroadleaf (0.50f,  0.90f, 5, 1, 1.25f, 0.0f, 0.75f, 1.0f, 9);
        addFern      (0.775f, 0.50f, 4, 1, 0.2f, 9);

        // front layer
        addGrass     (0.15f,  0.24f, 6, 2, 0.2f);
        addBroadleaf (0.365f, 0.34f, 3, 2, 1.0f, 0.35f, 1.0f, 0.95f, 5);
        addGrass     (0.445f, 0.20f, 5, 2, 0.4f);
        addGrass     (0.575f, 0.18f, 5, 2, 0.5f);
        addBroadleaf (0.64f,  0.30f, 3, 2, 1.0f, 0.45f, 1.0f, 0.95f, 5);
        addGrass     (0.87f,  0.26f, 6, 2, 0.25f);
        beginCluster (2);
        addSprout (0.06f);  addSprout (0.31f);
        beginCluster (2);
        addSprout (0.70f);  addSprout (0.955f);

        // vines hanging from the frame
        beginCluster (0);
        addVine (0.032f, 0.36f);  addVine (0.074f, 0.55f);  addVine (0.128f, 0.27f);
        beginCluster (0);
        addVine (0.962f, 0.47f);  addVine (0.918f, 0.31f);  addVine (0.868f, 0.40f);

        rootBases = { { 0.50f, 1.0f }, { 0.235f, 0.78f }, { 0.775f, 0.72f }, { 0.105f, 0.7f },
                      { 0.665f, 0.6f }, { 0.905f, 0.62f }, { 0.365f, 0.5f }, { 0.64f, 0.46f } };

        heroCentre = { 0.5f * W, soilY * 0.50f };
    }
    else
    {
        // A hedge of small plants along the strip.
        const float step = juce::jmax (0.022f, H * 0.92f / W);
        int index = 0;

        for (float xf = 0.02f + step * 0.5f; xf < 0.985f; xf += step, ++index)
        {
            const float hf = rnd (0.68f, 1.0f);
            const int layer = index % 3;

            switch (index % 4)
            {
                case 0:  addBroadleaf (xf, hf, 3, layer, 1.1f, 0.2f, 0.6f, 1.4f, 4); break;
                case 1:  addGrass (xf, hf * 0.8f, 5, layer, 0.1f); break;
                case 2:  addFern (xf, hf, 3, layer, 0.15f, 6); break;
                default: addGrass (xf, hf * 0.7f, 4, layer, 0.3f); break;
            }
        }

        heroCentre = { 0.5f * W, soilY * 0.5f };
    }

    // ---------------------------------------------------------------- roots
    if (! compact)
    {
        const float depth = H - soilY - 5.0f * unit;

        const auto traceRoot = [&] (float x0, float y0, float angle, float bend, float length,
                                    float start, float span, float thick)
        {
            Root root;
            root.start = start;
            root.span = span;
            root.thick = thick;

            float x = x0, y = y0, a = angle;
            const float seg = length / float (kRootPoints - 1);

            for (int j = 0; j < kRootPoints; ++j)
            {
                root.x[j] = x;
                root.y[j] = juce::jmin (y, H - 3.0f * unit);
                a = a * bend + rnd (-0.16f, 0.16f);
                x += seg * std::sin (a);
                y += seg * std::cos (a);
            }

            roots.push_back (root);
            return root;
        };

        for (const auto& base : rootBases)
        {
            const float x0 = base.x * W;
            const float y0 = soilAt (x0) + 2.0f * unit;
            const float s = base.strength;

            const Root tap = traceRoot (x0, y0, rnd (-0.1f, 0.1f), 0.8f, depth * (0.6f + 0.36f * s), 0.0f, 1.0f, 5.0f * unit * s);

            const int laterals = 4 + r.nextInt (3);
            for (int k = 0; k < laterals; ++k)
            {
                const float df = 0.05f + 0.75f * (float (k) + rnd (0.0f, 0.6f)) / float (laterals);
                const float fi = df * float (kRootPoints - 1);
                const int i0 = juce::jmin (kRootPoints - 2, int (fi));
                const float fr = fi - float (i0);
                const float ox = tap.x[i0] + (tap.x[i0 + 1] - tap.x[i0]) * fr;
                const float oy = tap.y[i0] + (tap.y[i0 + 1] - tap.y[i0]) * fr;
                const float side = (k & 1) != 0 ? 1.0f : -1.0f;
                const float len = depth * rnd (0.8f, 1.9f) * s * (1.0f - 0.4f * df);

                const Root lat = traceRoot (ox, oy, side * rnd (1.05f, 1.5f), 0.9f, len,
                                            0.08f + df * 0.7f, 0.55f, 3.2f * unit * s);

                if (r.nextFloat() < 0.6f)
                {
                    const int j = 2 + r.nextInt (3);
                    traceRoot (lat.x[j], lat.y[j], side * rnd (0.2f, 0.7f), 0.85f, len * rnd (0.3f, 0.55f),
                               0.3f + df * 0.6f, 0.5f, 2.2f * unit * s);
                }
            }
        }
    }

    // ---------------------------------------------------------------- arp nodes, sorted low -> high
    {
        struct Candidate { int stem; float tipY; };
        std::vector<Candidate> sorted;

        for (int idx : nodeCandidates)
        {
            // Nominal tip height: integrate the resting shape.
            const Stem& s = stems[size_t (idx)];
            float y = s.y;
            for (int i = 0; i < s.segs; ++i)
            {
                const float tau = (float (i) + 0.5f) / float (s.segs);
                y -= s.len / float (s.segs) * std::cos (s.angle0 + s.curve * tau + s.curl * tau * tau * tau);
            }
            sorted.push_back ({ idx, y });
        }

        std::sort (sorted.begin(), sorted.end(), [] (const Candidate& a, const Candidate& b) { return a.tipY > b.tipY; });

        // If there are more candidates than slots, keep an even spread over the height range.
        const int total = int (sorted.size());
        const int keep = juce::jmin (total, kMaxNodes);

        for (int i = 0; i < keep; ++i)
        {
            const int pick = keep == total ? i : juce::roundToInt (float (i) * float (total - 1) / float (juce::jmax (1, keep - 1)));
            const int stemIndex = sorted[size_t (pick)].stem;
            if (stems[size_t (stemIndex)].node < 0)
            {
                stems[size_t (stemIndex)].node = int (nodeStem.size());
                nodeStem.push_back (stemIndex);
            }
        }
    }

    clusters.resize (size_t (numClusters));
    for (size_t i = 0; i < clusters.size(); ++i)
        clusters[i].layer = clusterLayers[i];

    tips.resize (stems.size());
    tipPresence.assign (stems.size(), 0.0f);

    for (size_t i = 0; i < stems.size(); ++i)
    {
        const Stem& s = stems[i];
        float x = s.x, y = s.y;
        for (int k = 0; k < s.segs; ++k)
        {
            const float tau = (float (k) + 0.5f) / float (s.segs);
            const float a = s.angle0 + s.curve * tau + s.curl * tau * tau * tau;
            x += s.len / float (s.segs) * std::sin (a);
            y -= s.len / float (s.segs) * std::cos (a);
        }
        tips[i] = { x, y };
    }

    // ---------------------------------------------------------------- mist & fireflies
    for (int i = 0; i < kNumMist; ++i)
    {
        auto& b = mist[size_t (i)];
        b.x = hash01 (i * 7 + 1);
        b.y = compact ? 0.45f + 0.3f * hash01 (i * 7 + 2)
                      : (i < 2 ? 0.40f + 0.18f * hash01 (i * 7 + 2) : 0.66f + 0.08f * hash01 (i * 7 + 2));
        b.speed = (0.006f + 0.012f * hash01 (i * 7 + 3)) * ((i & 1) != 0 ? -1.0f : 1.0f);
        b.alpha = 0.55f + 0.45f * hash01 (i * 7 + 4);
        b.phase = kTwoPi * hash01 (i * 7 + 5);
    }

    for (int i = 0; i < kNumFireflies; ++i)
    {
        auto& f = fireflies[size_t (i)];
        f.cx = W * (0.06f + 0.88f * hash01 (i * 13 + 1));
        f.cy = soilY * (compact ? 0.2f + 0.6f * hash01 (i * 13 + 2) : 0.12f + 0.74f * hash01 (i * 13 + 2));
        f.ax = W * (0.02f + 0.05f * hash01 (i * 13 + 3));
        f.ay = soilY * (0.03f + 0.08f * hash01 (i * 13 + 4));
        f.wx = 0.11f + 0.22f * hash01 (i * 13 + 5);
        f.wy = 0.09f + 0.2f * hash01 (i * 13 + 6);
        f.px = kTwoPi * hash01 (i * 13 + 7);
        f.py = kTwoPi * hash01 (i * 13 + 8);
        f.blink = 0.35f + 0.6f * hash01 (i * 13 + 9);
        f.rank = float (i) / float (kNumFireflies);
    }

    // ---------------------------------------------------------------- frame geometry
    const float radius = ctx.px (compact ? 9.0f : 12.0f);
    const auto bounds = getLocalBounds().toFloat();
    const float line = juce::jmax (1.0f, ctx.scale);

    cornerMask.clear();
    cornerMask.addRectangle (bounds);
    cornerMask.addRoundedRectangle (bounds, radius);
    cornerMask.setUsingNonZeroWinding (false);

    juce::Path outline;
    outline.addRoundedRectangle (bounds.reduced (line * 0.5f), radius);
    borderPath.clear();
    juce::PathStrokeType (line).createStrokedPath (borderPath, outline);

    sunPos = { W * 0.80f, H * 0.03f };
}

//==============================================================================
void GardenVisual::renderStatic (juce::Graphics& g) const
{
    const juce::Rectangle<float> b (0.0f, 0.0f, W, H);

    // glass / air
    {
        juce::ColourGradient air (rgb (0xff0c1f18), 0.0f, 0.0f, rgb (0xff0b1912), 0.0f, H, false);
        air.addColour (0.5, rgb (0xff12301f).interpolatedWith (rgb (0xff123229), 0.5f));
        g.setGradientFill (air);
        g.fillRect (b);
    }

    // first light on the horizon
    {
        const float rad = W * (compact ? 0.3f : 0.52f);
        juce::ColourGradient glow (rgb (0xff2f7355).withAlpha (0.50f), W * 0.5f, soilY, rgb (0xff2f7355).withAlpha (0.0f), W * 0.5f + rad, soilY, true);
        glow.addColour (0.5, rgb (0xff24604a).withAlpha (0.2f));
        g.setGradientFill (glow);
        g.fillRect (b);

        const float rad2 = H * (compact ? 2.4f : 1.05f);
        juce::ColourGradient dawn (rgb (0xff5a9a80).withAlpha (0.34f), sunPos.x, sunPos.y, rgb (0xff4a8f86).withAlpha (0.0f), sunPos.x - rad2, sunPos.y, true);
        g.setGradientFill (dawn);
        g.fillRect (b);
    }

    // greenhouse frame: mullions, a transom and gothic bay tops
    {
        const int bays = compact ? juce::jmax (8, juce::roundToInt (W / (H * 1.4f))) : 9;
        const float bayW = W / float (bays);
        const float transom = compact ? -10.0f : H * 0.175f;
        const float line = juce::jmax (1.0f, unit);

        juce::Path ribs;
        for (int i = 1; i < bays; ++i)
        {
            const float x = bayW * float (i);
            ribs.startNewSubPath (x, 0.0f);
            ribs.lineTo (x, soilY);
        }

        if (! compact)
        {
            ribs.startNewSubPath (0.0f, transom);
            ribs.lineTo (W, transom);

            for (int i = 0; i < bays; ++i)
            {
                const float x0 = bayW * float (i), x1 = x0 + bayW;
                ribs.startNewSubPath (x0, transom);
                ribs.quadraticTo (x0 + bayW * 0.08f, transom - bayW * 0.42f, x0 + bayW * 0.5f, transom - bayW * 0.5f);
                ribs.quadraticTo (x1 - bayW * 0.08f, transom - bayW * 0.42f, x1, transom);
            }
        }

        g.setGradientFill (juce::ColourGradient (col::hairline.withAlpha (compact ? 0.07f : 0.115f), 0.0f, 0.0f,
                                                 col::hairline.withAlpha (0.015f), 0.0f, soilY, false));
        g.strokePath (ribs, juce::PathStrokeType (line));

        // faint panes: alternate bays slightly lighter, like glass catching light
        for (int i = 0; i < bays; i += 2)
        {
            g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.022f), 0.0f, 0.0f,
                                                     juce::Colours::white.withAlpha (0.0f), 0.0f, soilY * 0.9f, false));
            g.fillRect (juce::Rectangle<float> (bayW * float (i), 0.0f, bayW, soilY));
        }
    }

    // soil
    {
        juce::Path soil;
        soil.startNewSubPath (0.0f, soilAt (0.0f));
        const int steps = juce::jmax (16, int (W / 12.0f));
        for (int i = 1; i <= steps; ++i)
        {
            const float x = W * float (i) / float (steps);
            soil.lineTo (x, soilAt (x));
        }

        juce::Path edge (soil);
        soil.lineTo (W, H);
        soil.lineTo (0.0f, H);
        soil.closeSubPath();

        juce::ColourGradient earth (rgb (0xff1c1c12), 0.0f, soilY, rgb (0xff0a0e0a), 0.0f, H, false);
        earth.addColour (0.35, rgb (0xff14170f));
        g.setGradientFill (earth);
        g.fillPath (soil);

        // strata and specks
        juce::Random r (0x50117);
        if (! compact)
        {
            for (int i = 0; i < 5; ++i)
            {
                juce::Path strata;
                const float y = soilY + (H - soilY) * (0.22f + 0.17f * float (i));
                strata.startNewSubPath (0.0f, y);
                for (int k = 1; k <= 24; ++k)
                {
                    const float x = W * float (k) / 24.0f;
                    strata.lineTo (x, y + unit * 2.5f * std::sin (x * 0.011f + float (i) * 1.7f) + unit * 1.2f * std::sin (x * 0.037f + float (i)));
                }
                g.setColour (rgb (0xff6e7a52).withAlpha (0.05f));
                g.strokePath (strata, juce::PathStrokeType (juce::jmax (1.0f, unit)));
            }
        }

        const int specks = compact ? 40 : 190;
        for (int i = 0; i < specks; ++i)
        {
            const float x = r.nextFloat() * W;
            const float y = soilAt (x) + 3.0f * unit + r.nextFloat() * juce::jmax (1.0f, H - soilY - 6.0f * unit);
            const float d = unit * (0.8f + 1.6f * r.nextFloat());
            g.setColour ((r.nextFloat() < 0.25f ? rgb (0xffb9c08f) : rgb (0xff58603f)).withAlpha (0.08f + 0.14f * r.nextFloat()));
            g.fillEllipse (x, y, d * (1.0f + r.nextFloat()), d);
        }

        // mossy rim
        g.setGradientFill (juce::ColourGradient (rgb (0xff7fa868).withAlpha (0.15f), 0.0f, 0.0f, rgb (0xff7fa868).withAlpha (0.15f), W, 0.0f, false));
        g.strokePath (edge, juce::PathStrokeType (juce::jmax (1.5f, unit * 3.2f)), juce::AffineTransform::translation (0.0f, unit * 1.2f));
        g.setColour (rgb (0xff9cc482).withAlpha (0.55f));
        g.strokePath (edge, juce::PathStrokeType (juce::jmax (1.0f, unit * 1.2f)));
    }

    // vignette
    {
        juce::ColourGradient v (juce::Colours::transparentBlack, W * 0.5f, H * 0.5f, juce::Colours::black.withAlpha (compact ? 0.3f : 0.42f), 0.0f, compact ? H * 0.5f : -H * 0.25f, true);
        v.addColour (0.6, juce::Colours::transparentBlack);
        g.setGradientFill (v);
        g.fillRect (b);
    }
}

void GardenVisual::renderFrost (juce::Graphics& g) const
{
    juce::Random r (0xf2057);
    juce::Path crystals;

    const std::function<void (float, float, float, float, int)> branch = [&] (float x, float y, float a, float len, int depth)
    {
        const float ex = x + std::sin (a) * len, ey = y - std::cos (a) * len;
        crystals.startNewSubPath (x, y);
        crystals.lineTo (ex, ey);

        if (depth <= 0 || len < 4.0f * unit)
            return;

        const int twigs = 2 + r.nextInt (2);
        for (int i = 1; i <= twigs; ++i)
        {
            const float f = float (i) / float (twigs + 1);
            const float bx = x + (ex - x) * f, by = y + (ey - y) * f;
            const float sub = len * (0.52f - 0.25f * f);
            branch (bx, by, a + 1.047f, sub, depth - 1);
            branch (bx, by, a - 1.047f, sub, depth - 1);
        }
    };

    const float reach = (compact ? H * 0.5f : H * 0.2f);
    const int seeds = compact ? juce::jmax (10, int (W / (H * 1.2f))) : 9;

    for (int i = 0; i < seeds; ++i)
    {
        const float f = (float (i) + r.nextFloat() * 0.6f) / float (seeds);

        // top edge, growing down; left and right edges growing inwards
        branch (W * f, 0.0f, kPi + (r.nextFloat() - 0.5f) * 0.7f, reach * (0.5f + r.nextFloat() * 0.7f), 2);

        if (! compact && i < 4)
        {
            branch (0.0f, H * (0.1f + 0.2f * float (i)), kPi * 0.5f + (r.nextFloat() - 0.5f) * 0.6f, reach * (0.5f + 0.6f * r.nextFloat()), 2);
            branch (W, H * (0.14f + 0.2f * float (i)), -kPi * 0.5f + (r.nextFloat() - 0.5f) * 0.6f, reach * (0.5f + 0.6f * r.nextFloat()), 2);
        }
    }

    // cool veil, densest at the edges
    juce::ColourGradient veil (col::ice.withAlpha (0.0f), W * 0.5f, H * 0.55f, col::ice.withAlpha (0.20f), 0.0f, compact ? H * 0.5f : -H * 0.2f, true);
    veil.addColour (0.45, col::ice.withAlpha (0.035f));
    g.setGradientFill (veil);
    g.fillRect (juce::Rectangle<float> (0.0f, 0.0f, W, H));

    g.setGradientFill (juce::ColourGradient (col::ice.withAlpha (0.55f), 0.0f, 0.0f, col::ice.withAlpha (0.16f), 0.0f, H * 0.5f, false));
    g.strokePath (crystals, juce::PathStrokeType (juce::jmax (1.0f, unit * 1.05f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void GardenVisual::renderCaches (float physicalScale)
{
    cacheScale = physicalScale;
    cacheDirty = false;

    const auto makeImage = [physicalScale] (float w, float h, bool opaque)
    {
        return juce::Image (opaque ? juce::Image::RGB : juce::Image::ARGB,
                            juce::jmax (1, juce::roundToInt (std::ceil (w * physicalScale))),
                            juce::jmax (1, juce::roundToInt (std::ceil (h * physicalScale))),
                            true, juce::SoftwareImageType());
    };

    {
        staticLayer = makeImage (W, H, true);
        juce::Graphics g (staticLayer);
        g.addTransform (juce::AffineTransform::scale (physicalScale));
        renderStatic (g);
    }

    {
        frostLayer = makeImage (W, H, false);
        juce::Graphics g (frostLayer);
        g.addTransform (juce::AffineTransform::scale (physicalScale));
        renderFrost (g);
    }

    // Soft radial sprites (drawn with a per-frame opacity): cheaper than per-frame radial gradients.
    const auto makeBlob = [&makeImage, physicalScale] (float w, float h, juce::Colour colour, float a0, float a1, float a2)
    {
        auto img = makeImage (w, h, false);
        juce::Graphics g (img);
        g.addTransform (juce::AffineTransform::scale (physicalScale));
        g.addTransform (juce::AffineTransform::scale (1.0f, h / w));

        const float rad = w * 0.5f;
        juce::ColourGradient grad (colour.withAlpha (a0), rad, rad, colour.withAlpha (0.0f), w, rad, true);
        grad.addColour (0.35, colour.withAlpha (a1));
        grad.addColour (0.7, colour.withAlpha (a2));
        g.setGradientFill (grad);
        g.fillEllipse (0.0f, 0.0f, w, w);
        return img;
    };

    if (compact)
    {
        sunSprite = {};
        haloSprite = {};
        mistSprite = makeBlob (H * 5.0f, H * 0.8f, rgb (0xffd4f3ea), 0.5f, 0.3f, 0.09f);
    }
    else
    {
        const float sunR = H * kSunRadius;
        sunSprite = makeBlob (sunR * 2.0f, sunR * 2.0f, col::sun, 0.55f, 0.24f, 0.06f);
        mistSprite = makeBlob (W * kMistW, H * kMistH, rgb (0xffd4f3ea), 0.42f, 0.27f, 0.09f);
        haloSprite = makeBlob (W * kHaloW, H * kHaloH, rgb (0xff9fe0a0), 0.45f, 0.24f, 0.06f);
    }
}

} // namespace verdant::ui
