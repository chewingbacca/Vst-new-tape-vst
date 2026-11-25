// Generative visual: one ring per track. Each ring is that track's loop drawn
// as arcs (hits lit by velocity, ratchets as split arcs), rotating so the
// playhead is always at 12 o'clock. Polymeter shows up as rings turning at
// different rates; glitches and fills pulse the centre.
#pragma once
#include "UiContext.h"

namespace ui {

class Visualizer : public juce::Component
{
public:
    explicit Visualizer (Ctx& c) : ctx (c) { setInterceptsMouseClicks (false, false); }

    void tick()
    {
        bool dirty = false;
        for (int t = 0; t < atm::kTracks; ++t)
        {
            const int s = ctx.eng.shared.step[t].load (std::memory_order_relaxed);
            if (s != step[t]) { step[t] = s; dirty = true; }
            const uint32_t tc = ctx.eng.shared.trigCount[t].load (std::memory_order_relaxed);
            if (tc != trigs[t]) { trigs[t] = tc; flash[t] = 1.f; dirty = true; }
            else if (flash[t] > 0.01f) { flash[t] *= 0.72f; dirty = true; }
            const uint32_t gc = ctx.eng.shared.glitchCount[t].load (std::memory_order_relaxed);
            if (gc != glitches[t]) { glitches[t] = gc; glitch = 1.f; dirty = true; }
        }
        if (glitch > 0.01f) { glitch *= 0.8f; dirty = true; }
        const uint64_t r = ctx.sess.revision();
        if (r != rev) { rev = r; dirty = true; }
        if (dirty) repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (1.f);
        g.setColour (col::well());
        g.fillRoundedRectangle (b, 4.f);
        const juce::Point<float> c = b.getCentre();
        const float outer = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f - 5.f;
        const float inner = outer * 0.26f;
        const float ringW = (outer - inner) / (float) atm::kTracks;

        for (int t = 0; t < atm::kTracks; ++t)
        {
            const atm::TrackPattern& tp = ctx.tp (t);
            const atm::TrackSound& ts = ctx.ts (t);
            const int len = juce::jlimit (1, atm::kMaxSteps, (int) tp.length);
            const float r = outer - ringW * ((float) t + 0.5f);
            const float seg = juce::MathConstants<float>::twoPi / (float) len;
            const int cur = juce::jmax (0, step[t]);
            const juce::Colour tc = col::track (t);
            // faint full ring
            g.setColour (col::line().withAlpha (0.55f));
            g.drawEllipse (c.x - r, c.y - r, r * 2.f, r * 2.f, 0.6f);
            for (int i = 0; i < len; ++i)
            {
                const atm::Step& s = tp.steps[i];
                if (! s.on) continue;
                // rotate so the current step sits at the top
                const float a0 = (float) (i - cur) * seg;
                const int n = juce::jlimit (1, 8, (int) s.ratCount);
                const bool isCur = step[t] == i;
                const float alpha = ts.mute ? 0.12f : (0.25f + 0.6f * (float) s.vel / 127.f) * (0.45f + 0.55f * (float) s.prob / 100.f);
                for (int k = 0; k < n; ++k)
                {
                    const float s0 = a0 + seg * ((float) k / (float) n) + 0.012f;
                    const float s1 = a0 + seg * ((float) (k + 1) / (float) n) * (n > 1 ? 1.f : 0.86f) - 0.012f - (n > 1 ? seg * 0.25f / (float) n : 0.f);
                    juce::Path p;
                    p.addCentredArc (c.x, c.y, r, r, 0.f, s0, juce::jmax (s0 + 0.01f, s1), true);
                    g.setColour (isCur ? tc.interpolatedWith (juce::Colours::white, 0.6f * flash[t]).withAlpha (juce::jmin (1.f, alpha + 0.5f)) : tc.withAlpha (alpha));
                    g.strokePath (p, juce::PathStrokeType (juce::jmax (1.5f, ringW - 2.2f), juce::PathStrokeType::curved, juce::PathStrokeType::butt));
                }
            }
        }
        // playhead marker
        g.setColour (col::text().withAlpha (0.8f));
        g.drawLine (c.x, c.y - outer - 3.f, c.x, c.y - inner + 2.f, 0.8f);
        // centre: glitch / activity pulse
        float act = 0.f;
        for (float f : flash) act = juce::jmax (act, f);
        const float cr = inner * (0.45f + 0.35f * act + 0.3f * glitch);
        g.setColour (col::accent().withAlpha (0.25f + 0.6f * juce::jmax (act * 0.5f, glitch)));
        if (glitch > 0.05f)
        {
            // a broken square instead of a dot while a glitch is firing
            const float o = glitch * 4.f;
            g.fillRect (c.x - cr + o, c.y - cr, cr * 2.f, cr * 0.8f);
            g.fillRect (c.x - cr - o, c.y + cr * 0.1f, cr * 2.f, cr * 0.9f);
        }
        else g.fillEllipse (c.x - cr, c.y - cr, cr * 2.f, cr * 2.f);
    }

private:
    Ctx& ctx;
    int step[atm::kTracks] = { -1, -1, -1, -1, -1, -1, -1, -1 };
    uint32_t trigs[atm::kTracks] = {}, glitches[atm::kTracks] = {};
    float flash[atm::kTracks] = {};
    float glitch = 0.f;
    uint64_t rev = 0;
};

} // namespace ui
