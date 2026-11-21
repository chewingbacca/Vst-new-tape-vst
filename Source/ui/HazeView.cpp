#include "HazeView.h"

#include "../PluginProcessor.h"

namespace swoon::ui
{

namespace
{
constexpr float kHorizon = 0.57f;      // resting height of the ribbons (0 = top)
constexpr float kRibbonSwing = 0.27f;  // ribbon excursion as a fraction of the height
constexpr float kTwoPi = juce::MathConstants<float>::twoPi;

struct Orb
{
    int band;                 // 0 low, 1 mid, 2 high
    float x, y;               // resting position (0..1)
    float driftX, driftY;     // drift amplitude
    float speed, phase;       // drift rate (Hz) and phase
    float radius;             // in units of the image height
    float hue;                // position on the cool -> warm ramp
    float density;            // Wall amount at which the orb appears
};

constexpr Orb kOrbs[] = {
    { 0, 0.20f, 0.80f, 0.09f, 0.06f, 0.050f, 0.0f, 0.66f, 0.46f, 0.00f },   // lavender, low left
    { 0, 0.74f, 0.86f, 0.11f, 0.05f, 0.041f, 2.1f, 0.72f, 0.98f, 0.00f },   // peach, low right
    { 0, 0.47f, 0.98f, 0.15f, 0.04f, 0.033f, 4.4f, 0.56f, 0.36f, 0.35f },   // periwinkle, low centre
    { 1, 0.34f, 0.44f, 0.13f, 0.10f, 0.071f, 1.3f, 0.36f, 0.06f, 0.00f },   // sea-glass
    { 1, 0.60f, 0.36f, 0.11f, 0.12f, 0.063f, 3.7f, 0.33f, 0.62f, 0.00f },   // rose
    { 1, 0.87f, 0.48f, 0.07f, 0.10f, 0.079f, 5.2f, 0.29f, 0.30f, 0.30f },   // sky
    { 1, 0.08f, 0.38f, 0.06f, 0.10f, 0.057f, 0.6f, 0.27f, 0.94f, 0.55f },   // amber
    { 2, 0.27f, 0.20f, 0.10f, 0.08f, 0.110f, 2.9f, 0.165f, 0.40f, 0.00f },  // lavender
    { 2, 0.52f, 0.16f, 0.12f, 0.07f, 0.097f, 0.9f, 0.135f, 0.12f, 0.00f },  // sea-glass
    { 2, 0.78f, 0.23f, 0.10f, 0.08f, 0.123f, 4.8f, 0.155f, 0.78f, 0.20f },  // peach
    { 2, 0.93f, 0.14f, 0.05f, 0.06f, 0.088f, 1.9f, 0.115f, 0.24f, 0.50f },  // sky
    { 2, 0.42f, 0.30f, 0.14f, 0.06f, 0.104f, 5.9f, 0.105f, 0.56f, 0.70f },  // orchid
};

float follow (float current, float target, float attack, float release) noexcept
{
    return current + (target - current) * (target > current ? attack : release);
}

float shapeLevel (float linear, float gain) noexcept
{
    return juce::jlimit (0.0f, 1.0f, std::pow (juce::jmax (0.0f, linear) * gain, 0.6f));
}
} // namespace

//==============================================================================
HazeView::HazeView (SwoonAudioProcessor& p)
    : processor (p),
      light ((size_t) (kW * kH * 3), 0.0f),
      trail ((size_t) (kW * kH * 3), 0.0f),
      ribbon ((size_t) (kW * kH * 3), 0.0f),
      ribbonTrail ((size_t) (kW * kH * 3), 0.0f),
      vignette ((size_t) (kW * kH), 1.0f),
      developed ((size_t) (kW * kH * 3), 0),
      grainTile ((size_t) (kGrainSize * kGrainSize), 0),
      titleFont (serifFont (44.0f)),
      categoryFont (sansFont (10.4f, 1, 0.22f)),
      noteFont (sansFont (12.0f, 0, 0.02f))
{
    // Not opaque on purpose: under the editor's fractional scale an opaque child leaves a
    // one-pixel seam along its edges. The parent skips its own fill beneath us instead.
    setInterceptsMouseClicks (false, false);

    juce::Random random (0x5300a11);

    for (auto& d : dither)
        d = (random.nextFloat() - 0.5f) / 255.0f * 1.6f;

    for (int y = 0; y < kH; ++y)
    {
        for (int x = 0; x < kW; ++x)
        {
            const float dx = ((float) x / (float) (kW - 1) - 0.5f) * 2.0f;
            const float dy = ((float) y / (float) (kH - 1) - 0.5f) * 2.0f;
            vignette[(size_t) (y * kW + x)] = 1.0f - 0.34f * dx * dx * dx * dx - 0.26f * dy * dy;
        }
    }

    // the artwork melts into the page over its lowest 42 %
    for (int y = 0; y < kH; ++y)
    {
        const float f = juce::jlimit (0.0f, 1.0f, ((float) y / (float) (kH - 1) - 0.58f) / 0.42f);
        fadeRow[(size_t) y] = f * f * (3.0f - 2.0f * f);
    }

    for (auto& g : grainTile)
        g = (juce::int8) (random.nextInt (255) - 127);

    advance();
}

void HazeView::setSleeveText (const juce::String& newTitle, const juce::String& newCategory, const juce::String& newNote)
{
    if (title != newTitle || category != newCategory.toUpperCase() || note != newNote)
    {
        title = newTitle;
        category = newCategory.toUpperCase();
        note = newNote;
        categoryWidth = category.isEmpty() ? 0.0f : textWidth (categoryFont, category);
        repaint();
    }
}

HazeView::Rgb HazeView::ramp (float position) noexcept
{
    // cool -> warm: sea-glass, sky, lavender, rose, peach, amber (as light, so a little deeper than the UI pastels)
    static constexpr Rgb stops[] = {
        { 0.26f, 0.86f, 0.72f }, { 0.34f, 0.60f, 1.00f }, { 0.62f, 0.44f, 1.00f },
        { 1.00f, 0.38f, 0.62f }, { 1.00f, 0.60f, 0.38f }, { 1.00f, 0.80f, 0.40f },
    };

    const float p = juce::jlimit (0.0f, 0.9999f, position) * 5.0f;
    const int i = (int) p;
    const float f = p - (float) i;
    const auto& a = stops[i];
    const auto& b = stops[i + 1];
    return { a.r + (b.r - a.r) * f, a.g + (b.g - a.g) * f, a.b + (b.b - a.b) * f };
}

//==============================================================================
void HazeView::advance()
{
    const auto now = juce::Time::getMillisecondCounter();
    const float dt = firstFrame ? 0.033f : juce::jlimit (0.005f, 0.1f, (float) (now - lastTick) * 0.001f);
    lastTick = now;
    time += (double) dt;
    ++frameCounter;

    readInputs();
    renderLight();
    developFrame();

    // the un-blurred signal trace: fades out as Blur comes up
    {
        const float temperature = (warmth - 0.5f) * 0.5f;
        const auto pastel = [] (Rgb c) { return Rgb { c.r + (1.0f - c.r) * 0.35f, c.g + (1.0f - c.g) * 0.35f, c.b + (1.0f - c.b) * 0.35f }; };

        traceAlpha = std::pow (1.0f - blur, 1.6f) * (0.25f + 0.75f * juce::jmin (1.0f, level * 1.8f)) * (1.0f - 0.5f * bypassed) * 0.62f;
        traceColourL = pastel (ramp (temperature + 0.70f));
        traceColourR = pastel (ramp (temperature + 0.10f));

        for (size_t x = 0; x < (size_t) kW; ++x)
        {
            traceL[x] = (kHorizon - waveL[x] * kRibbonSwing) * (float) kH - warpY[x];
            traceR[x] = (kHorizon - waveR[x] * kRibbonSwing) * (float) kH - warpY[x];
        }
    }

    if ((frameCounter & 1) == 0)
    {
        grainX = (frameCounter * 37) & (kGrainSize - 1);
        grainY = (frameCounter * 59) & (kGrainSize - 1);
    }

    firstFrame = false;
    canvasDirty = true;
    repaint();
}

void HazeView::readInputs()
{
    const auto& viz = processor.getVizData();

    const float attack = firstFrame ? 1.0f : 0.5f, release = firstFrame ? 1.0f : 0.1f;
    low   = follow (low,   shapeLevel (viz.bandLow.load (std::memory_order_relaxed), 3.2f), attack, release);
    mid   = follow (mid,   shapeLevel (viz.bandMid.load (std::memory_order_relaxed), 4.2f), attack, release);
    high  = follow (high,  shapeLevel (viz.bandHigh.load (std::memory_order_relaxed), 11.0f), attack, release);
    level = follow (level, shapeLevel (juce::jmax (viz.outLevelL.load (std::memory_order_relaxed),
                                                   viz.outLevelR.load (std::memory_order_relaxed)), 1.6f), attack, release);

    const float m = firstFrame ? 1.0f : 0.22f;
    auto macro = [this, m] (float& value, P p) { value += (processor.getParam (p).getValue() - value) * m; };
    macro (wall, P::wall);
    macro (wash, P::wash);
    macro (glide, P::glide);
    macro (blur, P::blur);
    macro (warmth, P::warmth);
    macro (mix, P::mix);
    bypassed += ((processor.getParam (P::bypass).getValue() > 0.5f ? 1.0f : 0.0f) - bypassed) * m;

    // scope: lock to a rising zero crossing so the ribbons breathe instead of jittering
    viz.readScope (scopeL.data(), scopeR.data(), kScopeFrames);

    int start = 0;
    for (int i = 1; i < kScopeFrames - kScopeShown; ++i)
    {
        if (scopeL[(size_t) i - 1] + scopeR[(size_t) i - 1] < 0.0f && scopeL[(size_t) i] + scopeR[(size_t) i] >= 0.0f)
        {
            start = i;
            break;
        }
    }

    // Wall squares the waveform off, like the fuzz does
    const float drive = 1.8f + 11.0f * wall * wall;
    const float step = (float) (kScopeShown - 1) / (float) (kW - 1);
    const float smooth = firstFrame ? 1.0f : 0.5f;
    std::array<float, kW> shapedL, shapedR;

    for (int x = 0; x < kW; ++x)
    {
        const float pos = (float) start + (float) x * step;
        const auto i = (size_t) pos;
        const float f = pos - (float) i;
        shapedL[(size_t) x] = std::tanh ((scopeL[i] + (scopeL[i + 1] - scopeL[i]) * f) * drive);
        shapedR[(size_t) x] = std::tanh ((scopeR[i] + (scopeR[i + 1] - scopeR[i]) * f) * drive);
    }

    // soften along the ribbon (binomial), then in time
    for (int x = 0; x < kW; ++x)
    {
        const auto at = [x] (const std::array<float, kW>& values, int offset) { return values[(size_t) juce::jlimit (0, kW - 1, x + offset)]; };
        const float l = (at (shapedL, -2) + at (shapedL, 2)) * 0.0625f + (at (shapedL, -1) + at (shapedL, 1)) * 0.25f + at (shapedL, 0) * 0.375f;
        const float r = (at (shapedR, -2) + at (shapedR, 2)) * 0.0625f + (at (shapedR, -1) + at (shapedR, 1)) * 0.25f + at (shapedR, 0) * 0.375f;

        waveL[(size_t) x] += (l - waveL[(size_t) x]) * smooth;
        waveR[(size_t) x] += (r - waveR[(size_t) x]) * smooth;
    }
}

void HazeView::renderLight()
{
    const float t = (float) time;
    const float temperature = (warmth - 0.5f) * 0.5f;
    const float presence = 0.25f + 0.75f * mix;
    const float sizeScale = 0.62f + 0.9f * wash;
    const float edge = 0.2f + 0.8f * blur;          // 0.2 = bokeh disc, 1 = fully soft
    const float density = 0.25f + 0.9f * wall;
    float* const data = light.data();

    // night gradient with a horizon glow that breathes with the level
    {
        const Rgb glow = ramp (temperature + 0.42f);
        const float glowLevel = (0.06f + 0.22f * level) * presence;

        for (int y = 0; y < kH; ++y)
        {
            const float fy = (float) y / (float) (kH - 1);
            const float d = (fy - kHorizon - 0.06f) / 0.36f;
            const float k = glowLevel * std::exp (-d * d);
            const float r = 0.030f + 0.022f * fy + glow.r * k;
            const float gg = 0.022f + 0.008f * fy + glow.g * k;
            const float b = 0.078f + 0.020f * fy + glow.b * k;

            float* row = data + (size_t) (y * kW * 3);
            for (int x = 0; x < kW; ++x, row += 3)
            {
                row[0] = r;
                row[1] = gg;
                row[2] = b;
            }
        }
    }

    // light-forms
    const float bands[3] = { low, mid, high };

    for (const auto& orb : kOrbs)
    {
        const float visible = juce::jlimit (0.0f, 1.0f, (density - orb.density) * 4.0f);
        if (visible <= 0.0f)
            continue;

        const float band = bands[orb.band];
        const float intensity = visible * presence * (0.20f + 0.95f * band) * (0.62f + 0.38f * edge);
        const float radius = orb.radius * (float) kH * sizeScale * (0.82f + 0.45f * band);
        const float cx = (orb.x + orb.driftX * std::sin (kTwoPi * orb.speed * t + orb.phase)) * (float) kW;
        const float cy = (orb.y + orb.driftY * std::sin (kTwoPi * orb.speed * 0.73f * t + orb.phase * 1.7f)) * (float) kH;
        const Rgb c = ramp (temperature + orb.hue * 0.8f);

        const int x0 = juce::jmax (0, (int) (cx - radius)), x1 = juce::jmin (kW - 1, (int) (cx + radius) + 1);
        const int y0 = juce::jmax (0, (int) (cy - radius)), y1 = juce::jmin (kH - 1, (int) (cy + radius) + 1);
        const float invR2 = 1.0f / (radius * radius), invEdge = 1.0f / edge;

        for (int y = y0; y <= y1; ++y)
        {
            const float dy = (float) y - cy;
            float* px = data + (size_t) ((y * kW + x0) * 3);

            for (int x = x0; x <= x1; ++x, px += 3)
            {
                const float dx = (float) x - cx;
                const float d2 = (dx * dx + dy * dy) * invR2;
                if (d2 >= 1.0f)
                    continue;

                const float e = juce::jmin (1.0f, (1.0f - std::sqrt (d2)) * invEdge);
                const float f = e * e * (3.0f - 2.0f * e) * intensity;
                px[0] += c.r * f;
                px[1] += c.g * f;
                px[2] += c.b * f;
            }
        }
    }

    // Wash: light lingers. Instant attack, slow release per pixel.
    {
        const float decay = firstFrame ? 0.0f : 0.5f + 0.47f * wash;
        float* tr = trail.data();
        const size_t n = light.size();

        for (size_t i = 0; i < n; ++i)
            tr[i] = juce::jmax (data[i], tr[i] * decay);
    }

    // the two ribbons: left channel warm, right channel cool; mono material fuses into one pale line.
    // They keep their own, shorter trail so a moving waveform smears instead of multiplying.
    {
        std::fill (ribbon.begin(), ribbon.end(), 0.0f);
        float* const rib = ribbon.data();

        const Rgb cl = ramp (temperature + 0.70f), cr = ramp (temperature + 0.10f);
        const float width = (1.6f + 4.8f * blur) * (0.8f + 0.5f * wash);
        const float invWidth = 1.0f / width;
        const float intensity = (0.14f + 0.8f * level) * (0.7f + 0.3f * (1.0f - blur));
        const int reach = (int) width + 1;

        for (int x = 0; x < kW; ++x)
        {
            const float yl = (kHorizon - waveL[(size_t) x] * kRibbonSwing) * (float) kH;
            const float yr = (kHorizon - waveR[(size_t) x] * kRibbonSwing) * (float) kH;

            for (int pass = 0; pass < 2; ++pass)
            {
                const float yc = pass == 0 ? yl : yr;
                const Rgb& c = pass == 0 ? cl : cr;
                const int ya = juce::jmax (0, (int) yc - reach), yb = juce::jmin (kH - 1, (int) yc + reach + 1);

                for (int y = ya; y <= yb; ++y)
                {
                    const float e = 1.0f - std::abs ((float) y - yc) * invWidth;
                    if (e <= 0.0f)
                        continue;

                    const float f = e * e * (3.0f - 2.0f * e) * intensity;
                    float* px = rib + (size_t) ((y * kW + x) * 3);
                    px[0] += c.r * f;
                    px[1] += c.g * f;
                    px[2] += c.b * f;
                }
            }
        }

        const float decay = firstFrame ? 0.0f : 0.35f + 0.5f * wash;
        float* rt = ribbonTrail.data();
        float* tr = trail.data();
        float* out = light.data();          // reused as the composite the developer samples
        const size_t n = ribbon.size();

        for (size_t i = 0; i < n; ++i)
        {
            rt[i] = juce::jmax (rib[i], rt[i] * decay);
            out[i] = tr[i] + rt[i];
        }
    }
}

void HazeView::developFrame()
{
    const float t = (float) time;
    const float amplitude = glide * 5.5f;
    const float exposure = (0.72f + 0.95f * wall) * (1.0f - 0.5f * bypassed);
    const float grey = bypassed * 0.85f;

    std::array<float, kH> shiftX;
    for (int y = 0; y < kH; ++y)
        shiftX[(size_t) y] = amplitude * 0.9f * std::sin (kTwoPi * ((float) y / (float) kH * 0.8f + t * 0.083f));

    for (int x = 0; x < kW; ++x)
    {
        const float fx = (float) x / (float) kW;
        warpY[(size_t) x] = amplitude * (std::sin (kTwoPi * (fx * 1.2f + t * 0.10f))
                                         + 0.5f * std::sin (kTwoPi * (fx * 2.7f - t * 0.063f)));
    }

    const float* const src = light.data();   // composite written by renderLight()
    const float nightR = col::night.getFloatRed(), nightG = col::night.getFloatGreen(), nightB = col::night.getFloatBlue();
    const float maxX = (float) kW - 1.001f, maxY = (float) kH - 1.001f;
    int ditherIndex = frameCounter * 31;

    for (int y = 0; y < kH; ++y)
    {
        auto* line = developed.data() + (size_t) (y * kW * 3);
        const float* vig = vignette.data() + (size_t) (y * kW);
        const float fade = fadeRow[(size_t) y];

        for (int x = 0; x < kW; ++x)
        {
            const float sx = juce::jlimit (0.0f, maxX, (float) x + shiftX[(size_t) y]);
            const float sy = juce::jlimit (0.0f, maxY, (float) y + warpY[(size_t) x]);
            const int ix = (int) sx, iy = (int) sy;
            const float fx = sx - (float) ix, fy = sy - (float) iy;

            const float* p00 = src + (size_t) ((iy * kW + ix) * 3);
            const float* p10 = p00 + 3;
            const float* p01 = p00 + kW * 3;
            const float* p11 = p01 + 3;
            const float w00 = (1.0f - fx) * (1.0f - fy), w10 = fx * (1.0f - fy), w01 = (1.0f - fx) * fy, w11 = fx * fy;
            const float gain = exposure * vig[x];

            float r = (p00[0] * w00 + p10[0] * w10 + p01[0] * w01 + p11[0] * w11) * gain;
            float g = (p00[1] * w00 + p10[1] * w10 + p01[1] * w01 + p11[1] * w11) * gain;
            float b = (p00[2] * w00 + p10[2] * w10 + p01[2] * w01 + p11[2] * w11) * gain;

            // soft shoulder: bright overlaps bloom towards white instead of clipping
            r = r / (1.0f + r) * 1.3f;
            g = g / (1.0f + g) * 1.3f;
            b = b / (1.0f + b) * 1.3f;

            if (grey > 0.0f)
            {
                const float lum = 0.3f * r + 0.5f * g + 0.2f * b;
                r += (lum - r) * grey;
                g += (lum - g) * grey;
                b += (lum * 1.08f - b) * grey;
            }

            // melt into the page colour towards the bottom edge
            r += (nightR - r) * fade;
            g += (nightG - g) * fade;
            b += (nightB - b) * fade;

            const float d = dither[(size_t) (ditherIndex++ & 1023)] * (1.0f - fade);
            const auto to8 = [d] (float v) { return (juce::uint8) juce::jlimit (0.0f, 255.0f, (v + d) * 255.0f + 0.5f); };
            line[x * 3]     = to8 (b);     // PixelRGB byte order
            line[x * 3 + 1] = to8 (g);
            line[x * 3 + 2] = to8 (r);
        }

        ditherIndex += 7;
    }
}

//==============================================================================
void HazeView::prepareCanvas (int width, int height, float scale)
{
    canvas = juce::Image (juce::Image::RGB, width, height, true, juce::SoftwareImageType());
    canvasScale = scale;

    widened.assign ((size_t) (kH * width * 3), 0);
    grainRow.assign ((size_t) (width * 3), 0);
    columnIndex.resize ((size_t) width);
    columnWeight.resize ((size_t) width);

    for (int x = 0; x < width; ++x)
    {
        const float fx = juce::jlimit (0.0f, (float) kW - 1.001f, ((float) x + 0.5f) * (float) kW / (float) width - 0.5f);
        columnIndex[(size_t) x] = (int) fx;
        columnWeight[(size_t) x] = (int) ((fx - std::floor (fx)) * 256.0f);
    }
}

void HazeView::composite()
{
    const int w = canvas.getWidth(), h = canvas.getHeight();
    juce::Image::BitmapData bitmap (canvas, juce::Image::BitmapData::writeOnly);
    jassert (bitmap.pixelStride == 3);

    // 1. stretch each low-resolution row to the canvas width
    for (int sy = 0; sy < kH; ++sy)
    {
        const juce::uint8* src = developed.data() + (size_t) (sy * kW * 3);
        juce::uint8* dst = widened.data() + (size_t) (sy * w * 3);

        for (int x = 0; x < w; ++x, dst += 3)
        {
            const juce::uint8* p = src + columnIndex[(size_t) x] * 3;
            const int f = columnWeight[(size_t) x];
            dst[0] = (juce::uint8) (p[0] + (((p[3] - p[0]) * f) >> 8));
            dst[1] = (juce::uint8) (p[1] + (((p[4] - p[1]) * f) >> 8));
            dst[2] = (juce::uint8) (p[2] + (((p[5] - p[2]) * f) >> 8));
        }
    }

    // 2. blend neighbouring rows down the canvas and add grain (heavier as the Wall comes up,
    //    thinning out where the artwork melts into the page)
    //    (Grain is per device pixel, so it reads coarser on a small window: ease it off there.)
    const float grainPeak = (0.028f + 0.085f * wall) * 150.0f * juce::jlimit (0.6f, 1.15f, 0.45f + 0.55f * canvasScale);   // 8-bit levels
    const int n = w * 3;

    for (int y = 0; y < h; ++y)
    {
        const float fy = juce::jlimit (0.0f, (float) kH - 1.001f, ((float) y + 0.5f) * (float) kH / (float) h - 0.5f);
        const int iy = (int) fy;
        const int wy = (int) ((fy - (float) iy) * 256.0f);
        const juce::uint8* a = widened.data() + (size_t) (iy * n);
        const juce::uint8* b = a + n;

        const float fade = fadeRow[(size_t) iy] + (fadeRow[(size_t) iy + 1] - fadeRow[(size_t) iy]) * (fy - (float) iy);
        const int amount = juce::roundToInt (grainPeak * (1.0f - fade) * 256.0f / 127.0f);
        const juce::int8* tile = grainTile.data() + (size_t) (((y + grainY) & (kGrainSize - 1)) * kGrainSize);
        juce::int16* gr = grainRow.data();

        for (int x = 0; x < w; ++x, gr += 3)
        {
            const auto v = (juce::int16) ((tile[(x + grainX) & (kGrainSize - 1)] * amount) >> 8);
            gr[0] = v;
            gr[1] = v;
            gr[2] = v;
        }

        gr = grainRow.data();
        juce::uint8* out = bitmap.getLinePointer (y);

        for (int i = 0; i < n; ++i)
        {
            const int v = a[i] + (((b[i] - a[i]) * wy) >> 8) + gr[i];
            out[i] = (juce::uint8) juce::jlimit (0, 255, v);
        }
    }

    // 3. the signal itself, un-blurred, on top
    if (traceAlpha > 0.02f)
    {
        drawTrace (bitmap, traceR, traceColourR, traceAlpha);
        drawTrace (bitmap, traceL, traceColourL, traceAlpha);
    }
}

void HazeView::drawTrace (juce::Image::BitmapData& bitmap, const std::array<float, kW>& lowResY, Rgb colour, float alpha)
{
    const int w = bitmap.width, h = bitmap.height;
    const float toLow = (float) (kW - 1) / (float) juce::jmax (1, w - 1);
    const float toCanvas = (float) h / (float) kH;
    const float half = juce::jlimit (0.55f, 1.4f, 0.6f * canvasScale);      // about 1.2 base pixels thick
    const int add[3] = { juce::roundToInt (colour.b * alpha * 255.0f), juce::roundToInt (colour.g * alpha * 255.0f),
                         juce::roundToInt (colour.r * alpha * 255.0f) };

    const auto yAt = [&] (int x)
    {
        const float fx = juce::jmin ((float) kW - 1.001f, (float) x * toLow);
        const auto i = (size_t) fx;
        return (lowResY[i] + (lowResY[i + 1] - lowResY[i]) * (fx - (float) i)) * toCanvas;
    };

    float current = yAt (0);

    for (int x = 0; x < w; ++x)
    {
        // each column covers the vertical span to the next sample, so steep slopes stay connected
        const float next = yAt (x + 1);
        const float top = juce::jmin (current, next) - half, bottom = juce::jmax (current, next) + half;
        current = next;

        const int y0 = juce::jmax (0, (int) std::floor (top)), y1 = juce::jmin (h - 1, (int) std::floor (bottom));

        for (int y = y0; y <= y1; ++y)
        {
            const float coverage = juce::jmin ((float) y + 1.0f, bottom) - juce::jmax ((float) y, top);
            if (coverage <= 0.0f)
                continue;

            const int k = (int) (juce::jmin (1.0f, coverage) * 256.0f);
            juce::uint8* px = bitmap.getPixelPointer (x, y);
            px[0] = (juce::uint8) juce::jmin (255, px[0] + ((add[0] * k) >> 8));
            px[1] = (juce::uint8) juce::jmin (255, px[1] + ((add[1] * k) >> 8));
            px[2] = (juce::uint8) juce::jmin (255, px[2] + ((add[2] * k) >> 8));
        }
    }
}

void HazeView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const float w = bounds.getWidth(), h = bounds.getHeight();

    // The artwork is composited at device resolution, so it goes to the screen as a plain blit.
    // (One pixel larger than the component: at fractional scales the edge must not show through.)
    // (Capped at two device pixels per base pixel: beyond that the soft artwork gains nothing.)
    const float deviceScale = g.getInternalContext().getPhysicalPixelScaleFactor();
    const float scale = juce::jlimit (0.25f, 2.0f, deviceScale);
    const int cw = (int) std::ceil (w * scale) + 1, ch = (int) std::ceil (h * scale) + 1;

    if (canvas.isNull() || canvas.getWidth() != cw || canvas.getHeight() != ch)
    {
        prepareCanvas (cw, ch, scale);
        canvasDirty = true;
    }

    if (canvasDirty)
    {
        composite();
        canvasDirty = false;
    }

    g.setImageResamplingQuality (deviceScale > scale ? juce::Graphics::mediumResamplingQuality : juce::Graphics::lowResamplingQuality);
    g.setOpacity (1.0f);
    g.drawImageTransformed (canvas, juce::AffineTransform::scale (1.0f / scale));

    // sleeve title
    if (title.isNotEmpty())
    {
        g.setColour (col::fog.withAlpha (0.96f));
        g.setFont (titleFont);
        g.drawText (title, juce::Rectangle<float> (30.0f, h - 92.0f, w - 60.0f, 50.0f), juce::Justification::bottomLeft, true);
    }

    float x = 32.0f;
    const float y = h - 37.0f;

    if (category.isNotEmpty())
    {
        g.setColour (col::fog.withAlpha (0.7f));
        g.setFont (categoryFont);
        g.drawText (category, juce::Rectangle<float> (x, y, categoryWidth + 4.0f, 16.0f), juce::Justification::centredLeft, false);
        x += categoryWidth + 6.0f;

        if (note.isNotEmpty())
        {
            g.setColour (col::fog.withAlpha (0.3f));
            g.fillRect (x, y + 8.0f, 14.0f, 1.0f);
            x += 24.0f;
        }
    }

    if (note.isNotEmpty())
    {
        g.setColour (col::fog.withAlpha (0.62f));
        g.setFont (noteFont);
        g.drawText (note, juce::Rectangle<float> (x, y - 0.5f, w - x - 30.0f, 16.0f), juce::Justification::centredLeft, true);
    }
}

} // namespace swoon::ui
