// MOD tab: the selected track's two LFOs (with a drawable shape), step-mod
// smoothing, global chaos and envelope follower, draggable source chips, and
// the modulation matrix.
#include "DetailPanel.h"

namespace ui {

using namespace atm;

namespace {

// Draw the 32-point user shape of an LFO; the live phase is shown as a cursor.
class LfoDraw : public juce::Component
{
public:
    LfoDraw (Ctx& c, int which) : ctx (c), lfo (which) {}
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        fillPanel (g, getLocalBounds(), col::well(), 2.f);
        const float* d = ctx.ts().lfoDraw[lfo];
        const float mid = r.getCentreY(), hh = r.getHeight() * 0.45f;
        g.setColour (col::line()); g.fillRect (r.getX(), mid, r.getWidth(), 1.f);
        const float w = r.getWidth() / (float) kLfoDrawPts;
        juce::Path p;
        for (int i = 0; i <= kLfoDrawPts; ++i)
        {
            const float y = mid - d[i % kLfoDrawPts] * hh;
            if (i == 0) p.startNewSubPath (r.getX(), y); else p.lineTo (r.getX() + w * (float) i, y);
        }
        g.setColour (col::mod());
        g.strokePath (p, juce::PathStrokeType (1.6f));
        const float v = ctx.eng.shared.srcValue[Src::Lfo1 + lfo].load (std::memory_order_relaxed);
        g.setColour (col::text().withAlpha (0.8f));
        g.fillEllipse (r.getRight() - 8.f, mid - v * hh - 3.f, 6.f, 6.f);
        drawLabel (g, "DRAW", getLocalBounds().reduced (4, 2), col::faint(), 8.5f, juce::Justification::topLeft, true);
    }
    void mouseDown (const juce::MouseEvent& e) override { ctx.sess.beginTransaction ("Draw LFO"); last = -1; drawAt (e.getPosition()); }
    void mouseDrag (const juce::MouseEvent& e) override { drawAt (e.getPosition()); }
    void mouseUp (const juce::MouseEvent&) override { ctx.sess.endTransaction(); }
    void mouseEnter (const juce::MouseEvent&) override { ctx.hoverHelp = "Draw the LFO shape (used when the shape is set to Draw)"; }
    void mouseExit (const juce::MouseEvent&) override { ctx.hoverHelp = {}; }
private:
    void drawAt (juce::Point<int> p)
    {
        const int i = juce::jlimit (0, kLfoDrawPts - 1, p.x * kLfoDrawPts / juce::jmax (1, getWidth()));
        const float v = juce::jlimit (-1.f, 1.f, 1.f - 2.f * (float) p.y / (float) juce::jmax (1, getHeight()));
        TrackSound s = ctx.ts();
        const int a = last < 0 ? i : juce::jmin (i, last), b = last < 0 ? i : juce::jmax (i, last);
        for (int k = a; k <= b; ++k) s.lfoDraw[lfo][k] = v;
        last = i;
        ctx.sess.setTrackSound (ctx.track, s);
        repaint();
    }
    Ctx& ctx;
    int lfo, last = -1;
};

// One row of the matrix.
class ModRow : public juce::Component
{
public:
    ModRow (Ctx& c, int slot_) : ctx (c), slot (slot_)
    {
        amount.ctx = &ctx; amount.caption = ""; amount.bipolar = true; amount.defaultValue = 0.5f; amount.colour = col::mod();
        amount.help = "Modulation amount (bipolar). Double-click resets to zero.";
        amount.get = [this] { return (ctx.proj().kit.mods[slot].amount + 1.f) * 0.5f; };
        amount.set = [this] (float v) { ModSlot m = ctx.proj().kit.mods[slot]; m.amount = v * 2.f - 1.f; if (std::abs (m.amount) < 0.004f) m.amount = 0.0001f; ctx.sess.setModSlot (slot, m); };
        amount.format = [] (float v) { return juce::String (v * 2.f - 1.f, 2); };
        amount.onGestureStart = [this] { ctx.sess.beginTransaction ("Mod amount"); };
        amount.onGestureEnd = [this] { ctx.sess.endTransaction(); };
        addAndMakeVisible (amount);
        del.ctx = &ctx; del.help = "Remove this routing"; del.colour = col::warn();
        del.onClick = [this] { ctx.sess.removeMod (slot); };
        addAndMakeVisible (del);
    }
    void sync() { amount.sync(); repaint(); }
    void resized() override { amount.setBounds (getWidth() - 74, -2, 40, getHeight() + 10); del.setBounds (getWidth() - 28, 4, 22, getHeight() - 8); }
    void paint (juce::Graphics& g) override
    {
        const ModSlot& m = ctx.proj().kit.mods[slot];
        fillPanel (g, getLocalBounds(), col::panelHi(), 2.f);
        juce::String src = juce::String (srcName (m.src)).toUpperCase();
        if (srcIsPerTrack (m.src)) src << " T" << (int) m.srcTrack + 1;
        juce::String dst;
        if (m.dstTrack < 0) dst = "GLOBAL  " + juce::String (globalParamDesc (m.dstParam, ctx.proj().kit.fxType).name).toUpperCase();
        else dst = "T" + juce::String (m.dstTrack + 1) + "  " + juce::String (trackParamDesc (m.dstParam, ctx.ts (m.dstTrack).ctx()).name).toUpperCase();
        drawLabel (g, src, { 6, 0, 100, getHeight() }, col::mod(), 9.5f, juce::Justification::centredLeft, true);
        drawLabel (g, ">", { 106, 0, 12, getHeight() }, col::faint(), 9.5f);
        drawLabel (g, dst, { 120, 0, getWidth() - 200, getHeight() }, m.dstTrack < 0 ? col::accent() : col::track (m.dstTrack), 9.5f, juce::Justification::centredLeft, true);
        // amount bar
        const float a = juce::jlimit (-1.f, 1.f, m.amount);
        const float bx = (float) getWidth() - 120.f, bw = 40.f, cy = (float) getHeight() * 0.5f;
        g.setColour (col::well()); g.fillRect (bx, cy - 2.f, bw, 4.f);
        g.setColour (col::mod()); g.fillRect (a >= 0.f ? bx + bw * 0.5f : bx + bw * 0.5f + a * bw * 0.5f, cy - 2.f, std::abs (a) * bw * 0.5f, 4.f);
    }
    Ctx& ctx;
    int slot;
private:
    ValueKnob amount;
    Btn del { "X" };
};

class ModPage : public Page
{
public:
    explicit ModPage (Ctx& c) : Page (c), draw1 (c, 0), draw2 (c, 1)
    {
        addAndMakeVisible (draw1); addAndMakeVisible (draw2);
        static constexpr int chipSrcs[] = { Src::Lfo1, Src::Lfo2, Src::ModEnv, Src::AmpEnv, Src::StepMod, Src::Velocity, Src::NoteRandom, Src::Note,
                                            Src::Macro1, Src::Macro2, Src::Macro3, Src::Macro4, Src::ChaosX, Src::ChaosY, Src::Follower };
        for (int s : chipSrcs) { auto ch = std::make_unique<ModChip> (ctx, s); addAndMakeVisible (*ch); chips.push_back (std::move (ch)); }
        global = std::make_unique<ParamGroup> (ctx, "Chaos / Follower");
        global->rows = 1; global->accent = col::mod();
        for (int gp : { GP::ChaosType, GP::ChaosRate, GP::ChaosShape, GP::FollowAtk, GP::FollowRel, GP::FollowGain }) global->add (ParamRef::G (gp));
        addAndMakeVisible (*global);
        viewport.setViewedComponent (&list, false);
        viewport.setScrollBarsShown (true, false);
        viewport.setScrollBarThickness (6);
        addAndMakeVisible (viewport);
        filterBtn.ctx = &ctx; filterBtn.colour = col::mod();
        filterBtn.help = "Show only routings that involve the selected track, or all routings";
        filterBtn.onClick = [this] { onlyTrack = ! onlyTrack; rebuildRows(); };
        addAndMakeVisible (filterBtn);
    }

    void rebuild() override
    {
        const juce::Colour tc = col::track (ctx.track);
        for (int l = 0; l < 2; ++l)
        {
            auto& gr = lfoGroups[l];
            gr = std::make_unique<ParamGroup> (ctx, l == 0 ? "LFO 1" : "LFO 2");
            gr->rows = 1; gr->accent = tc;
            const int base = l == 0 ? (int) TP::Lfo1Rate : (int) TP::Lfo2Rate;
            for (int k = 0; k < 5; ++k) gr->add (ParamRef::T (base + k));
            addAndMakeVisible (*gr);
        }
        stepGroup = std::make_unique<ParamGroup> (ctx, "Step mod");
        stepGroup->rows = 1; stepGroup->accent = col::mod();
        stepGroup->add (ParamRef::T (TP::StepGlide));
        addAndMakeVisible (*stepGroup);
        rebuildRows();
        resized();
    }

    void tick (bool changed) override
    {
        for (auto& c : chips) c->sync();
        for (auto& g : lfoGroups) if (g) g->sync();
        if (stepGroup) stepGroup->sync();
        global->sync();
        draw1.repaint (draw1.getWidth() - 10, 0, 10, draw1.getHeight());
        draw2.repaint (draw2.getWidth() - 10, 0, 10, draw2.getHeight());
        if (changed)
        {
            int n = 0;
            for (int i = 0; i < kModSlots; ++i) n += visibleSlot (i) ? 1 : 0;
            if (n != (int) rows.size()) rebuildRows();
            for (auto& r : rows) r->sync();
            draw1.repaint(); draw2.repaint();
            filterBtn.setText (onlyTrack ? "TRACK " + juce::String (ctx.track + 1) : juce::String ("ALL"));
            repaint();
        }
    }

    void resized() override
    {
        // left: LFOs
        int y = 26;
        for (int l = 0; l < 2; ++l)
        {
            if (lfoGroups[l]) lfoGroups[l]->setBounds (10, y, 300, 82);
            (l == 0 ? draw1 : draw2).setBounds (314, y + 18, 110, 58);
            y += 88;
        }
        if (stepGroup) stepGroup->setBounds (10, y, 70, 82);
        global->setBounds (86, y, 338, 82);
        // middle: chips
        int cx = 440, cy = 26;
        for (size_t i = 0; i < chips.size(); ++i)
        {
            chips[i]->setBounds (cx, cy, 92, 22);
            cy += 25;
            if (i == 7) { cx += 98; cy = 26; }
        }
        // right: matrix
        filterBtn.setBounds (getWidth() - 96, 4, 84, 18);
        viewport.setBounds (650, 26, getWidth() - 662, getHeight() - 34);
        layoutRows();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (col::bg());
        drawLabel (g, "TRACK " + juce::String (ctx.track + 1) + " MODULATORS", { 10, 6, 300, 16 }, col::track (ctx.track), 10.f, juce::Justification::centredLeft, true);
        drawLabel (g, "SOURCES  -  DRAG ONTO ANY CONTROL", { 440, 6, 210, 16 }, col::mod(), 10.f, juce::Justification::centredLeft, true);
        drawLabel (g, "MATRIX  " + juce::String (countActive()) + "/" + juce::String (kModSlots), { 650, 6, 300, 16 }, col::mod(), 10.f, juce::Justification::centredLeft, true);
        drawWrapped (g, "Per-track sources (LFO, envelopes, step mod, velocity, note) come from the track selected when you drop them. "
                      "Right-click a control to remove its modulation.",
                   { 440, 232, 200, 60 }, col::faint(), 8.5f);
        if (rows.empty())
            drawLabel (g, "NO ROUTINGS YET", viewport.getBounds().withHeight (40), col::faint(), 10.f);
        g.setColour (col::line());
        g.fillRect (432, 12, 1, getHeight() - 24);
        g.fillRect (642, 12, 1, getHeight() - 24);
    }

private:
    bool visibleSlot (int i) const
    {
        const ModSlot& m = ctx.proj().kit.mods[i];
        if (m.src == Src::None) return false;
        if (! onlyTrack) return true;
        return m.dstTrack == ctx.track || (srcIsPerTrack (m.src) && m.srcTrack == ctx.track);
    }
    int countActive() const { int n = 0; for (auto& m : ctx.proj().kit.mods) n += m.src != Src::None ? 1 : 0; return n; }
    void rebuildRows()
    {
        rows.clear();
        for (int i = 0; i < kModSlots; ++i)
            if (visibleSlot (i)) { auto r = std::make_unique<ModRow> (ctx, i); list.addAndMakeVisible (*r); rows.push_back (std::move (r)); }
        layoutRows();
        repaint();
    }
    void layoutRows()
    {
        const int w = juce::jmax (100, viewport.getWidth() - 8);
        int y = 0;
        for (auto& r : rows) { r->setBounds (0, y, w, 26); y += 29; }
        list.setSize (w, juce::jmax (y, 10));
    }

    LfoDraw draw1, draw2;
    std::unique_ptr<ParamGroup> lfoGroups[2], stepGroup, global;
    std::vector<std::unique_ptr<ModChip>> chips;
    juce::Viewport viewport;
    juce::Component list;
    std::vector<std::unique_ptr<ModRow>> rows;
    Btn filterBtn { "ALL" };
    bool onlyTrack = false;
};

} // namespace

std::unique_ptr<Page> makeModPage (Ctx& c) { return std::make_unique<ModPage> (c); }

} // namespace ui
