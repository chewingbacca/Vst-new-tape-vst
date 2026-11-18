// The per-frame half of the garden: inputs, simulation, frame geometry and painting.
// The scene itself (plants, roots, cached layers) is built in GardenScene.cpp.
#include "GardenVisual.h"

#include "../PluginProcessor.h"
#include "GardenVisualDetail.h"

namespace verdant::ui
{

using namespace garden;

void GardenVisual::LayerPaths::clear()
{
    vein.clear();  petalPink.clear();  petalCream.clear();  core.clear();
}

//==============================================================================
GardenVisual::GardenVisual (UiContext& c, bool compactStrip)
    : ctx (c), compact (compactStrip)
{
    setOpaque (true);
    setInterceptsMouseClicks (true, false);

    for (auto& ring : ringPaths)
        ring.setUsingNonZeroWinding (false);    // two concentric ellipses make a ring

}

GardenVisual::~GardenVisual()
{
    ctx.clearHover (this);
}

void GardenVisual::mouseEnter (const juce::MouseEvent&)
{
    ctx.setHover (this, "The garden", nullptr,
                  "Leaves and blossoms follow Bloom, sway follows Growth, mist and dew Moisture, light Sunlight, "
                  "the root system Roots. Pollen rides the grains, buds light up on arp notes");
}

void GardenVisual::mouseExit (const juce::MouseEvent&)
{
    ctx.clearHover (this);
}

GardenVisual::PaintStats GardenVisual::getPaintStats() const
{
    auto result = stats;
    const auto n = size_t (juce::jmin (sampleCount, int (samples.size())));

    if (n > 0)
    {
        auto sorted = samples;
        std::sort (sorted.begin(), sorted.begin() + std::ptrdiff_t (n));
        result.medianMs = double (sorted[n / 2]);
        result.minMs = double (sorted[0]);
    }

    return result;
}

void GardenVisual::drawSprite (juce::Graphics& g, const juce::Image& img, float x, float y, float opacity) const
{
    if (opacity < 0.012f || img.isNull())
        return;

    g.setOpacity (juce::jmin (1.0f, opacity));

    if (std::abs (cacheScale - 1.0f) < 0.001f)
        g.drawImageAt (img, juce::roundToInt (x), juce::roundToInt (y));
    else
        g.drawImageTransformed (img, juce::AffineTransform::scale (1.0f / cacheScale)
                                         .translated (std::round (x * cacheScale) / cacheScale,
                                                      std::round (y * cacheScale) / cacheScale));
}

//==============================================================================
void GardenVisual::readInputs (float dt)
{
    auto& proc = ctx.proc;
    const auto& m = proc.engine.meters;
    const auto get = [&proc] (PID id) { return proc.getParam (id)->getValue(); };

    const float in = sat (readFinite (m.inLevel));
    const float out = sat (juce::jmax (readFinite (m.outLevelL), readFinite (m.outLevelR)));
    const bool bypassed = get (pBypass) >= 0.5f;

    // The envelope follower is the primary source; the peak meters keep the scene alive
    // if it is silent (and before the engine publishes it).
    float tEnergy = juce::jmax (sat (readFinite (m.envelope)), std::pow (in, 0.6f) * 0.85f);
    float tWet = juce::jmax (sat (readFinite (m.wetLevel)), std::pow (out, 0.6f) * 0.6f);

    if (bypassed)
    {
        tEnergy *= 0.35f;
        tWet = 0.0f;
    }

    const int arp = m.arpPulse.load (std::memory_order_relaxed);
    const int grain = m.grainPulse.load (std::memory_order_relaxed);
    const int stutter = m.stutterPulse.load (std::memory_order_relaxed);

    if (! inputsPrimed)
    {
        inputsPrimed = true;
        bloom.v = get (pBloom);         growth.v = get (pGrowth);       moisture.v = get (pMoisture);
        sunlight.v = get (pSunlight);   rootDepth.v = get (pRoots);     mix.v = get (pMix);
        freeze.v = get (pRevFreeze) >= 0.5f ? 1.0f : 0.0f;
        bypass.v = bypassed ? 1.0f : 0.0f;
        lastArp = arp;  lastGrain = grain;  lastStutter = stutter;
    }

    const float pt = 0.22f;
    bloom.to (get (pBloom), dt, pt, pt);
    growth.to (get (pGrowth), dt, pt, pt);
    moisture.to (get (pMoisture), dt, pt, pt);
    sunlight.to (get (pSunlight), dt, pt, pt);
    rootDepth.to (get (pRoots), dt, pt * 1.5f, pt * 1.5f);
    mix.to (get (pMix), dt, pt, pt);
    freeze.to (get (pRevFreeze) >= 0.5f ? 1.0f : 0.0f, dt, 0.55f, 0.9f);
    bypass.to (bypassed ? 1.0f : 0.0f, dt, 0.3f, 0.3f);
    grainPan = get (pGrainPan);

    energy.to (tEnergy, dt, 0.045f, 0.5f);
    wet.to (tWet, dt, 0.16f, 0.9f);
    low.to (sat (readFinite (m.low)), dt, 0.07f, 0.5f);
    mid.to (sat (readFinite (m.mid)), dt, 0.07f, 0.45f);
    high.to (sat (readFinite (m.high)), dt, 0.06f, 0.4f);
    activity.to (sat (readFinite (m.grainActivity)), dt, 0.1f, 0.7f);

    const float dk = 1.0f - std::exp (-dt / 0.35f);
    for (int i = 0; i < 4; ++i)
        drift[i] += (juce::jlimit (-1.0f, 1.0f, readFinite (m.drift[i])) - drift[i]) * dk;

    // Which stems are grown is simulation state (events below depend on it), so it is kept
    // up to date here rather than as a side effect of painting.
    for (size_t i = 0; i < stems.size(); ++i)
        tipPresence[i] = presenceOf (stems[i]);

    // --- events
    if (arp != lastArp)
    {
        const int count = juce::jlimit (1, 3, std::abs (arp - lastArp));
        const int degree = m.arpDegree.load (std::memory_order_relaxed);
        for (int i = 0; i < count; ++i)
            triggerNode (degree + i * 3);
        lastArp = arp;
    }

    if (grain != lastGrain)
    {
        // the strip is small: let only some of the grains show
        const int count = compact ? (rng.nextFloat() < 0.3f ? 1 : 0) : juce::jlimit (1, 3, std::abs (grain - lastGrain));
        for (int i = 0; i < count; ++i)
            spawnParticle (rng.nextInt (3));
        lastGrain = grain;
    }

    if (stutter != lastStutter)
    {
        shiver = 1.0f;
        lastStutter = stutter;
    }

    shiver *= std::exp (-dt / 0.22f);

    for (auto& f : nodeFlash)
        f *= std::exp (-dt / 0.42f);
}

float GardenVisual::presenceOf (const Stem& s) const noexcept
{
    // Bloom decides which stems exist at all: rank 0 is always there, rank 1 needs most of it.
    return smooth01 ((bloom.v * 1.2f + 0.32f - s.rank) / 0.3f);
}

void GardenVisual::triggerNode (int degree)
{
    const int n = juce::jmin (int (nodeStem.size()), kMaxNodes);
    if (n <= 0)
        return;

    // Degrees are folded into 16 steps: low notes light low buds, high notes the crown.
    const int d = ((degree % 16) + 16) % 16;
    const int centre = juce::roundToInt (float (d) / 15.0f * float (n - 1));

    for (int off = 0; off < n; ++off)
        for (int sgn : { 1, -1 })
        {
            const int j = centre + sgn * off;
            if (j >= 0 && j < n && tipPresence[size_t (nodeStem[size_t (j)])] > 0.3f)
            {
                nodeFlash[size_t (j)] = 1.0f;
                return;
            }
        }
}

void GardenVisual::spawnParticle (int kind)
{
    if (stems.empty())
        return;

    juce::Point<float> origin (W * (0.15f + 0.7f * rng.nextFloat()), soilY * (0.25f + 0.5f * rng.nextFloat()));

    for (int attempt = 0; attempt < 6; ++attempt)
    {
        const auto i = size_t (rng.nextInt (int (stems.size())));
        if (tipPresence[i] > 0.3f && stems[i].hanging == 0)
        {
            origin = tips[i];
            break;
        }
    }

    const float k = compact ? 0.45f : 1.0f;
    const float pan = (rng.nextFloat() * 2.0f - 1.0f) * (0.25f + 0.75f * grainPan);

    auto& p = particles[size_t (particleCursor)];
    particleCursor = (particleCursor + 1) % kMaxParticles;

    p.x = origin.x + unit * k * (rng.nextFloat() - 0.5f) * 22.0f;
    p.y = origin.y + unit * k * (rng.nextFloat() - 0.5f) * 16.0f;
    p.vx = pan * unit * k * (16.0f + 30.0f * rng.nextFloat());
    p.vy = -unit * k * (8.0f + 24.0f * rng.nextFloat());
    p.age = 0.0f;
    p.life = (compact ? 1.0f : 1.5f) + 1.8f * rng.nextFloat();
    p.size = unit * (compact ? 0.8f : 1.0f) * (1.0f + 1.3f * rng.nextFloat());
    p.wobble = kTwoPi * rng.nextFloat();
    p.kind = juce::jlimit (0, 2, kind);
}

void GardenVisual::updateParticles (float dt)
{
    const float t = float (time);
    const float sway = unit * (compact ? 4.0f : 9.0f);

    for (auto& p : particles)
    {
        if (p.life <= 0.0f)
            continue;

        p.age += dt;
        if (p.age >= p.life)
        {
            p.life = 0.0f;
            continue;
        }

        p.x += (p.vx + sway * std::sin (t * 1.3f + p.wobble)) * dt;
        p.y += p.vy * dt;
        p.vx *= 1.0f - 0.25f * dt;
        p.vy *= 1.0f - 0.3f * dt;

        if (p.x < -10.0f || p.x > W + 10.0f || p.y < -10.0f || p.y > H + 10.0f)
            p.life = 0.0f;
    }

    // a trickle of pollen even in silence; more with grain activity and level
    ambientSpawn += dt * (0.5f + 5.0f * activity.v + 2.5f * energy.v) * (compact ? 0.3f : 1.0f);
    while (ambientSpawn >= 1.0f)
    {
        ambientSpawn -= 1.0f;
        spawnParticle (rng.nextInt (3));
    }
}

void GardenVisual::advance (float dt)
{
    dt = juce::jlimit (0.0f, 0.1f, dt);
    time += double (dt);

    readInputs (dt);

    // Growth sets the tempo of the sway; a frozen reverb slows the garden to a near standstill.
    swayPhase += dt * (0.4f + 2.0f * growth.v) * (1.0f - 0.82f * freeze.v) * (1.0f + 0.5f * energy.v);
    if (swayPhase > 4096.0f)
        swayPhase -= 4096.0f * std::floor (swayPhase / 4096.0f);

    updateParticles (dt);

    // Frame-rate governor: keep the visual's share of one core bounded. At normal sizes a
    // paint takes ~3 ms and every tick repaints; at 2x scale or very large windows it thins
    // out to as little as every other tick. The motion itself is unaffected.
    repaintCredit += getRepaintRate() / kNominalFps;
    if (repaintCredit >= 1.0f)
    {
        repaintCredit = juce::jmin (repaintCredit - 1.0f, 1.0f);
        repaint();
    }
}

float GardenVisual::getRepaintRate() const noexcept
{
    return paintCostMs <= kPaintBudgetMs ? kNominalFps
                                         : juce::jlimit (kMinFps, kNominalFps, kNominalFps * kPaintBudgetMs / paintCostMs);
}

//==============================================================================
void GardenVisual::buildFrame()
{
    for (auto& l : layers)
        l.clear();

    for (auto& c : clusters)
    {
        c.lit.clear();
        c.shade.clear();
    }

    rootPath.clear();
    rootTipPath.clear();
    dewPathA.clear();
    dewPathB.clear();
    budPath.clear();

    const float t = float (time);
    const float e = energy.v;
    const float bloomV = bloom.v;
    const float breath = 1.0f + 0.075f * e + 0.035f * wet.v + 0.018f * std::sin (t * 0.75f);
    const float swayGain = 0.6f + 0.85f * growth.v + 0.7f * e;
    const float leafLevel = 0.22f + 0.86f * bloomV;
    const float dewLevel = (moisture.v - 0.12f) * 0.62f;
    const float veinMin = unit * 15.0f;

    float px[kMaxSegs + 1], py[kMaxSegs + 1], ang[kMaxSegs + 1];

    for (size_t si = 0; si < stems.size(); ++si)
    {
        const Stem& s = stems[si];
        const float presence = presenceOf (s);

        tipPresence[si] = presence;
        tips[si] = { s.x, s.y };

        if (presence <= 0.01f)
            continue;

        const int segs = s.segs;
        const float len = s.len * (0.45f + 0.55f * presence) * (1.0f + 0.03f * e);
        const float seg = len / float (segs);
        const float ph = swayPhase * s.swayRate + s.phase;
        const float d = drift[s.drift] * s.driftAmt;
        const float shiverPhase = t * 53.0f + s.phase * 9.0f;

        float x = s.x, y = s.y;
        px[0] = x;
        py[0] = y;

        for (int i = 0; i < segs; ++i)
        {
            const float tau = (float (i) + 0.5f) / float (segs);
            const float a = s.angle0 + s.curve * tau + s.curl * tau * tau * tau
                          + (s.swayAmp * swayGain * std::sin (ph - tau * 1.7f) + d) * tau
                          + shiver * 0.075f * std::sin (shiverPhase + tau * 11.0f) * tau;
            ang[i] = a;
            x += seg * std::sin (a);
            y -= seg * std::cos (a);
            px[i + 1] = x;
            py[i + 1] = y;
        }
        ang[segs] = ang[segs - 1];

        auto& lp = layers[s.layer];
        auto& cp = clusters[s.cluster];

        // stem ribbon
        {
            const float w0 = s.thick * (0.55f + 0.45f * presence);
            float nxs[kMaxSegs + 1], nys[kMaxSegs + 1];

            for (int i = 0; i <= segs; ++i)
            {
                const float taper = w0 * (1.0f - 0.86f * float (i) / float (segs));
                nxs[i] = std::cos (ang[i]) * taper;
                nys[i] = std::sin (ang[i]) * taper;
            }

            auto& ribbon = cp.shade;
            ribbon.startNewSubPath (px[0] - nxs[0], py[0] - nys[0]);
            for (int i = 1; i <= segs; ++i)
                ribbon.lineTo (px[i] - nxs[i], py[i] - nys[i]);
            for (int i = segs; i >= 0; --i)
                ribbon.lineTo (px[i] + nxs[i], py[i] + nys[i]);
            ribbon.closeSubPath();
        }

        // leaves
        const Leaf* lf = leaves.data() + s.leaf0;
        for (int k = 0; k < s.leafN; ++k)
        {
            const Leaf& l = lf[k];
            float sc = (leafLevel - l.rank) / 0.16f;
            if (sc <= 0.03f)
                continue;

            sc = smooth01 (sc) * presence * breath;

            const float f = l.t * float (segs);
            const int i = juce::jmin (int (f), segs - 1);
            const float fr = f - float (i);
            const float bx = px[i] + (px[i + 1] - px[i]) * fr;
            const float by = py[i] + (py[i + 1] - py[i]) * fr;

            const float a = ang[i] + l.side * l.angle
                          + 0.085f * swayGain * std::sin (ph * 1.9f + l.phase)
                          + shiver * 0.16f * std::sin (shiverPhase * 1.3f + l.phase);
            const float dx = std::sin (a), dy = -std::cos (a);
            const float ll = l.len * sc;

            addLeaf (l.side > 0.0f ? cp.lit : cp.shade, bx, by, dx, dy, ll, l.width);

            if (ll > veinMin)
            {
                const float w = ll * 0.022f;
                lp.vein.startNewSubPath (bx + dy * w, by - dx * w);
                lp.vein.lineTo (bx + dx * ll * 0.86f, by + dy * ll * 0.86f);
                lp.vein.lineTo (bx - dy * w, by + dx * w);
                lp.vein.closeSubPath();
            }

            if (l.dew < dewLevel && ll > unit * 6.0f)
            {
                const float r = unit * (0.9f + 1.5f * hash01 (int (si) * 131 + k)) * (compact ? 0.7f : 1.0f);
                addDot ((k & 1) != 0 ? dewPathA : dewPathB, bx + dx * ll * 0.93f, by + dy * ll * 0.93f + r * 0.6f, r);
            }
        }

        const float tx = px[segs], ty = py[segs];
        tips[si] = { tx, ty };

        // blossom
        if (s.blossom > 0.0f)
        {
            const float open = smooth01 ((bloomV * 1.3f - s.blossomRank) / 0.32f);
            const float flash = s.node >= 0 ? nodeFlash[size_t (s.node)] : 0.0f;

            if (open > 0.04f)
            {
                const float rad = s.blossom * (0.4f + 0.6f * open) * presence * (1.0f + 0.14f * e + 0.3f * flash);
                const float rot = ang[segs] + 0.12f * std::sin (ph * 0.7f);
                const float spread = 0.55f + (kPi - 0.55f) * open;
                auto& petals = s.cream != 0 ? lp.petalCream : lp.petalPink;

                for (int p = 0; p < s.petals; ++p)
                {
                    const float a = rot + spread * (2.0f * float (p) / float (s.petals - 1) - 1.0f) * (float (s.petals - 1) / float (s.petals));
                    addLeaf (petals, tx, ty, std::sin (a), -std::cos (a), rad, 0.36f);
                }

                addDot (lp.core, tx, ty, juce::jmax (1.2f, rad * 0.2f));
            }
            else
            {
                addDot (lp.core, tx, ty, juce::jmax (1.2f, s.blossom * 0.16f * presence));
            }
        }
        else if (s.node >= 0 && presence > 0.3f)
        {
            addDot (budPath, tx, ty, unit * (compact ? 0.9f : 1.5f) * (1.0f + 1.2f * nodeFlash[size_t (s.node)]));
        }
    }

    // roots
    if (! roots.empty())
    {
        const float grow = 0.1f + 0.9f * rootDepth.v;
        const float fat = 1.0f + 0.3f * low.v;
        constexpr int last = kRootPoints - 1;

        for (const Root& r : roots)
        {
            const float f = sat ((grow - r.start) / r.span);
            if (f <= 0.03f)
                continue;

            const float visible = f * float (last);
            const int full = juce::jmin (last, int (visible));
            const float part = visible - float (full);
            int n = full + 1;

            float rx[kRootPoints], ry[kRootPoints];
            for (int j = 0; j <= full; ++j)
            {
                rx[j] = r.x[j];
                ry[j] = r.y[j];
            }

            if (part > 0.02f && full < last)
            {
                rx[n] = r.x[full] + (r.x[full + 1] - r.x[full]) * part;
                ry[n] = r.y[full] + (r.y[full + 1] - r.y[full]) * part;
                ++n;
            }

            if (n < 2)
                continue;

            const float w0 = r.thick * (0.55f + 0.45f * f) * fat;
            float ox[kRootPoints], oy[kRootPoints];

            for (int j = 0; j < n; ++j)
            {
                const int a = juce::jmax (0, j - 1), b = juce::jmin (n - 1, j + 1);
                float dx = rx[b] - rx[a], dy = ry[b] - ry[a];
                const float inv = 1.0f / juce::jmax (1.0e-3f, std::sqrt (dx * dx + dy * dy));
                const float w = w0 * (1.0f - 0.8f * float (j) / float (n - 1)) * 0.5f;
                dx *= inv;
                dy *= inv;
                ox[j] = -dy * w;
                oy[j] = dx * w;
            }

            rootPath.startNewSubPath (rx[0] - ox[0], ry[0] - oy[0]);
            for (int j = 1; j < n; ++j)
                rootPath.lineTo (rx[j] - ox[j], ry[j] - oy[j]);
            for (int j = n - 1; j >= 0; --j)
                rootPath.lineTo (rx[j] + ox[j], ry[j] + oy[j]);
            rootPath.closeSubPath();

            addDot (rootTipPath, rx[n - 1], ry[n - 1], unit * (0.8f + 1.0f * low.v));
        }
    }
}

void GardenVisual::drawLayer (juce::Graphics& g, int layer, juce::Colour lowC, juce::Colour midC, juce::Colour highC)
{
    auto& lp = layers[size_t (layer)];

    // Vertical gradient: the low band colours the base, mid the body, high the crown.
    // Fills are opaque so the software renderer can take its fastest path.
    const float y0 = soilY, y1 = soilY * 0.08f;
    const auto shade = rgb (0xff0a1f17);
    const float alpha = 1.0f;

    const auto makeGradient = [&] (float darken)
    {
        juce::ColourGradient grad (mixc (lowC, shade, darken), 0.0f, y0, mixc (highC, shade, darken), 0.0f, y1, false);
        grad.addColour (0.5, mixc (midC, shade, darken));
        return grad;
    };

    g.setGradientFill (makeGradient (0.4f));
    for (const auto& c : clusters)
        if (c.layer == layer && ! c.shade.isEmpty())
            g.fillPath (c.shade);

    g.setGradientFill (makeGradient (0.0f));
    for (const auto& c : clusters)
        if (c.layer == layer && ! c.lit.isEmpty())
            g.fillPath (c.lit);

    if (! lp.vein.isEmpty())
    {
        g.setColour (mixc (midC, shade, 0.55f));
        g.fillPath (lp.vein);
    }

    const float fz = freeze.v;
    const float depth = layer == 0 ? 0.45f : 0.0f;

    if (! lp.petalPink.isEmpty())
    {
        const auto c = mixc (mixc (col::blossom, col::ice, fz * 0.75f), shade, depth);
        g.setGradientFill (juce::ColourGradient (c.brighter (0.2f).withAlpha (alpha), 0.0f, y1, c.withAlpha (alpha * 0.95f), 0.0f, y0, false));
        g.fillPath (lp.petalPink);
    }

    if (! lp.petalCream.isEmpty())
    {
        const auto c = mixc (mixc (rgb (0xfff7ecc8), col::ice, fz * 0.75f), shade, depth);
        g.setColour (c.withAlpha (alpha * 0.96f));
        g.fillPath (lp.petalCream);
    }

    if (! lp.core.isEmpty())
    {
        g.setColour (mixc (mixc (col::sun, juce::Colours::white, fz * 0.6f + high.v * 0.3f), shade, depth).withAlpha (alpha));
        g.fillPath (lp.core);
    }
}

//==============================================================================
void GardenVisual::paint (juce::Graphics& g)
{
    const double startMs = juce::Time::getMillisecondCounterHiRes();

    if (W < 8.0f || H < 8.0f || stems.empty())
    {
        g.fillAll (col::bgTop);
        return;
    }

    const float physical = juce::jlimit (0.25f, 4.0f, float (g.getInternalContext().getPhysicalPixelScaleFactor()));
    if (cacheDirty || std::abs (physical - cacheScale) > 0.01f)
        renderCaches (physical);

    g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
    drawSprite (g, staticLayer, 0.0f, 0.0f, 1.0f);

    buildFrame();

    const float t = float (time);
    const float e = energy.v;
    const float fz = freeze.v;
    const float sunV = sunlight.v;
    const float moist = moisture.v;
    const float asleep = 1.0f - 0.45f * bypass.v;

    // ---- light: sun glow and shafts through the glass
    if (! compact)
    {
        const float sunR = H * kSunRadius;
        drawSprite (g, sunSprite, sunPos.x - sunR, sunPos.y - sunR,
                    (0.10f + 0.8f * sunV) * (1.0f + 0.25f * high.v) * (1.0f - 0.5f * fz) * asleep);

        const float base = (0.02f + 0.17f * sunV * sunV) * (1.0f - 0.4f * fz) * asleep;
        if (base > 0.006f)
        {
            for (int i = 0; i < 4; ++i)
            {
                const float fi = float (i);
                const float a = base * (0.55f + 0.45f * std::sin (t * (0.13f + 0.04f * fi) + fi * 2.1f + drift[i] * 1.5f))
                                * (1.0f + 0.5f * high.v);
                if (a < 0.004f)
                    continue;

                const float topX = W * (0.50f + 0.105f * fi) + unit * 8.0f * std::sin (t * 0.07f + fi);
                const float topW = W * (0.022f + 0.008f * fi);
                const float slant = soilY * 0.42f;

                shaftPath.clear();
                shaftPath.startNewSubPath (topX, 0.0f);
                shaftPath.lineTo (topX + topW, 0.0f);
                shaftPath.lineTo (topX + topW * 2.6f - slant, soilY);
                shaftPath.lineTo (topX - topW * 1.2f - slant, soilY);
                shaftPath.closeSubPath();

                const auto c = mixc (col::sun, col::ice, fz * 0.7f);
                g.setGradientFill (juce::ColourGradient (c.withAlpha (a), 0.0f, 0.0f, c.withAlpha (0.0f), 0.0f, soilY, false));
                g.fillPath (shaftPath);
            }
        }
    }

    // ---- palette
    auto lowC  = mixc (rgb (0xff2b6243), rgb (0xff5cc07a), low.v * 0.7f + e * 0.12f);
    auto midC  = mixc (rgb (0xff58a55f), rgb (0xffa8e68c), mid.v * 0.65f + e * 0.1f);
    auto highC = mixc (mixc (rgb (0xffb7e39a), rgb (0xffe9eea6), sunV * 0.6f), rgb (0xfff8ffe2), high.v * 0.6f + e * 0.08f);

    lowC  = mixc (lowC,  rgb (0xff3f707c), fz * 0.8f);
    midC  = mixc (midC,  rgb (0xff8fcdd3), fz * 0.8f);
    highC = mixc (highC, rgb (0xffe8fbff), fz * 0.82f);

    if (bypass.v > 0.01f)
    {
        const auto grey = rgb (0xff4c6257);
        lowC = mixc (lowC, grey, bypass.v * 0.5f);
        midC = mixc (midC, grey, bypass.v * 0.5f);
        highC = mixc (highC, grey.brighter (0.5f), bypass.v * 0.5f);
    }

    const auto haze = mixc (rgb (0xff12301f), rgb (0xff1d4144), fz * 0.6f);

    // ---- far plants
    drawLayer (g, 0, mixc (lowC, haze, 0.56f), mixc (midC, haze, 0.56f), mixc (highC, haze, 0.6f));

    // ---- mist between the layers
    const float mistAmount = (0.05f + 0.75f * moist) * (0.75f + 0.5f * wet.v + 0.2f * fz);
    const float mistW = compact ? H * 5.0f : W * kMistW;
    const float mistH = compact ? H * 0.8f : H * kMistH;

    const auto drawMist = [&] (int from, int to)
    {
        for (int i = from; i < to; ++i)
        {
            const auto& b = mist[size_t (i)];
            float fx = b.x + t * b.speed;
            fx -= std::floor (fx);
            const float x = -mistW + fx * (W + mistW);
            const float y = H * b.y - mistH * 0.5f + unit * 5.0f * std::sin (t * 0.21f + b.phase);
            drawSprite (g, mistSprite, x, y, mistAmount * b.alpha * (0.8f + 0.2f * std::sin (t * 0.3f + b.phase)));
        }
    };

    drawMist (0, compact ? 1 : 2);

    // ---- the halo behind the hero plant breathes with the signal
    if (! compact)
    {
        const float halo = (0.10f + 0.5f * e + 0.35f * wet.v) * (0.45f + 0.55f * mix.v) * asleep
                         + 0.03f * std::sin (t * 0.75f);
        drawSprite (g, haloSprite, heroCentre.x - W * kHaloW * 0.5f, heroCentre.y - H * kHaloH * 0.5f, juce::jmax (0.0f, halo));
    }

    drawLayer (g, 1, lowC, midC, highC);

    // ---- roots
    if (! rootPath.isEmpty())
    {
        const auto c0 = mixc (rgb (0xffcfc89c), col::ice, fz * 0.6f);
        const float a = 0.6f + 0.3f * low.v + 0.1f * e;
        g.setGradientFill (juce::ColourGradient (c0.withAlpha (juce::jmin (0.95f, a)), 0.0f, soilY,
                                                 mixc (c0, rgb (0xff7da277), 0.6f).withAlpha (juce::jmin (0.9f, a * 0.6f)), 0.0f, H, false));
        g.fillPath (rootPath);

        g.setColour (mixc (col::sun, col::ice, fz * 0.7f).withAlpha (juce::jmin (1.0f, 0.3f + 0.7f * low.v)));
        g.fillPath (rootTipPath);
    }

    drawLayer (g, 2, mixc (lowC, juce::Colours::white, 0.03f), mixc (midC, juce::Colours::white, 0.05f), mixc (highC, juce::Colours::white, 0.08f));

    drawMist (2, kNumMist);

    // ---- resting buds, dew
    if (! budPath.isEmpty())
    {
        g.setColour (mixc (col::sun, col::ice, fz * 0.7f).withAlpha (0.55f));
        g.fillPath (budPath);
    }

    if (! dewPathA.isEmpty() || ! dewPathB.isEmpty())
    {
        const auto dewCol = mixc (rgb (0xffd9fff4), juce::Colours::white, fz * 0.5f);
        const float tw = 0.5f + 0.5f * std::sin (t * 1.1f);
        g.setColour (dewCol.withAlpha (0.5f + 0.4f * tw));
        g.fillPath (dewPathA);
        g.setColour (dewCol.withAlpha (0.9f - 0.4f * tw));
        g.fillPath (dewPathB);
    }

    // ---- arp notes light the buds
    {
        for (auto& rp : ringPaths)
            rp.clear();

        const int n = juce::jmin (int (nodeStem.size()), kMaxNodes);
        const float k = compact ? 0.5f : 1.0f;

        for (int j = 0; j < n; ++j)
        {
            const float f = nodeFlash[size_t (j)];
            if (f < 0.08f)
                continue;

            const auto p = tips[size_t (nodeStem[size_t (j)])];
            const float age = 1.0f - f;

            // soft bloom of light around the bud
            const auto c = mixc ((j & 1) != 0 ? col::sun : col::blossom, col::ice, fz * 0.7f);
            const float r = unit * k * (15.0f + 16.0f * age);
            juce::ColourGradient glow (c.withAlpha (0.85f * f), p.x, p.y, c.withAlpha (0.0f), p.x + r, p.y, true);
            glow.addColour (0.35, c.withAlpha (0.36f * f));
            g.setGradientFill (glow);
            g.fillRect (juce::Rectangle<float> (p.x - r, p.y - r, r * 2.0f, r * 2.0f));

            // expanding ring; rings share three even-odd paths by brightness so they fade out
            auto& ring = ringPaths[f > 0.5f ? 0 : (f > 0.22f ? 1 : 2)];
            const float rr = unit * (compact ? 0.3f : 1.0f) * (5.0f + 22.0f * age);
            const float thick = juce::jmax (0.9f, unit * k * (0.9f + 1.3f * f));
            ring.addEllipse (p.x - rr, p.y - rr, rr * 2.0f, rr * 2.0f);
            ring.addEllipse (p.x - rr + thick, p.y - rr + thick, (rr - thick) * 2.0f, (rr - thick) * 2.0f);

            // hot core
            addDot (ringPaths[0], p.x, p.y, unit * k * (1.2f + 2.6f * f));
        }

        const auto ringColour = mixc (rgb (0xfffff3d6), juce::Colours::white, fz);
        constexpr float ringAlpha[3] = { 0.8f, 0.42f, 0.18f };

        for (size_t i = 0; i < 3; ++i)
            if (! ringPaths[i].isEmpty())
            {
                g.setColour (ringColour.withAlpha (ringAlpha[i]));
                g.fillPath (ringPaths[i]);
            }
    }

    // ---- particles and fireflies
    {
        for (auto& h : haloPaths)
            h.clear();
        for (auto& kind : corePaths)
            for (auto& bucket : kind)
                bucket.clear();

        const auto emit = [this] (float x, float y, float size, float alpha, int kind)
        {
            if (alpha < 0.1f)
                return;

            const int bucket = alpha > 0.7f ? 2 : (alpha > 0.4f ? 1 : 0);
            addDot (corePaths[size_t (kind)][size_t (bucket)], x, y, size * (0.75f + 0.45f * alpha));

            if (bucket == 2 && ! compact)
            {
                const float r = size * 2.7f;
                haloPaths[size_t (kind)].addEllipse (x - r, y - r, r * 2.0f, r * 2.0f);
            }
        };

        for (const auto& p : particles)
        {
            if (p.life <= 0.0f)
                continue;

            const float u = p.age / p.life;
            emit (p.x, p.y, p.size, sat (u * 7.0f) * (1.0f - u * u), p.kind);
        }

        const float visible = 0.4f + 0.6f * juce::jmax (activity.v, e, mix.v * 0.5f);
        for (const auto& f : fireflies)
        {
            if (f.rank > visible)
                continue;

            const float blink = std::sin (t * f.blink + f.px * 3.0f);
            const float a = sat (blink * 1.6f) * asleep;
            emit (f.cx + f.ax * std::sin (t * f.wx + f.px), f.cy + f.ay * std::sin (t * f.wy + f.py),
                  unit * (compact ? 1.0f : 1.7f), a, 1);
        }

        const juce::Colour kinds[3] = { mixc (rgb (0xffdcf9bd), juce::Colours::white, fz * 0.7f),
                                        mixc (rgb (0xffffe9a8), juce::Colours::white, fz * 0.7f),
                                        mixc (rgb (0xffc9fff0), juce::Colours::white, fz * 0.7f) };
        constexpr float alphas[3] = { 0.32f, 0.62f, 0.95f };

        for (size_t k = 0; k < 3; ++k)
        {
            if (! haloPaths[k].isEmpty())
            {
                g.setColour (kinds[k].withAlpha (0.13f));
                g.fillPath (haloPaths[k]);
            }

            for (size_t b = 0; b < 3; ++b)
                if (! corePaths[k][b].isEmpty())
                {
                    g.setColour (kinds[k].withAlpha (alphas[b]));
                    g.fillPath (corePaths[k][b]);
                }
        }
    }

    // ---- frozen reverb: frost creeps in from the glass
    if (fz > 0.012f)
    {
        drawSprite (g, frostLayer, 0.0f, 0.0f, fz);

        glintPath.clear();
        const int glints = compact ? 14 : 30;
        for (int i = 0; i < glints; ++i)
        {
            const float tw = std::sin (t * (0.9f + 1.3f * hash01 (i * 5 + 3)) + kTwoPi * hash01 (i * 5 + 4));
            if (tw < 0.2f)
                continue;

            addStar (glintPath, W * hash01 (i * 5 + 1), H * (compact ? 1.0f : 0.8f) * hash01 (i * 5 + 2),
                     unit * (compact ? 2.5f : 4.5f) * (0.5f + tw) * (0.6f + 0.8f * hash01 (i * 5)));
        }

        g.setColour (juce::Colours::white.withAlpha (0.8f * fz));
        g.fillPath (glintPath);
    }

    // ---- frame: mask the rounded corners with the editor backdrop, then the hairline border
    {
        juce::Rectangle<float> backdrop = getLocalBounds().toFloat();
        if (ctx.editor != nullptr)
            backdrop = getLocalArea (ctx.editor, ctx.editor->getLocalBounds()).toFloat();

        g.setGradientFill (backdropGradient (backdrop));
        g.fillPath (cornerMask);

        g.setGradientFill (juce::ColourGradient (col::hairline.withAlpha (0.24f), 0.0f, 0.0f,
                                                 col::hairline.withAlpha (0.09f), 0.0f, H, false));
        g.fillPath (borderPath);
    }

    const double elapsed = juce::Time::getMillisecondCounterHiRes() - startMs;
    stats.lastMs = elapsed;
    stats.maxMs = juce::jmax (stats.maxMs, elapsed);
    stats.averageMs = (stats.averageMs * double (stats.frames) + elapsed) / double (stats.frames + 1);
    ++stats.frames;
    samples[size_t (sampleCount++ % int (samples.size()))] = float (elapsed);
    paintCostMs += 0.08f * (float (elapsed) - paintCostMs);
}

} // namespace verdant::ui
