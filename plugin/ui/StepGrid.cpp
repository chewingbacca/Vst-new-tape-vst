#include "StepGrid.h"
#include "seq/SeqEngine.h"
#include "gen/Generators.h"

namespace ui {

using namespace atm;

namespace {
const char* const kEngineTags[Engine::count] = { "DRM", "FM", "SMP", "GRN", "RES", "SYN" };
constexpr int kRowGap = 3;
}

StepGrid::StepGrid (Ctx& c) : ctx (c)
{
    for (auto& p : playStep) p = -1;
    setWantsKeyboardFocus (false);
}

juce::Rectangle<int> StepGrid::rowBounds (int t) const
{
    const int h = getHeight() / kTracks;
    return { 0, t * h, getWidth(), h - kRowGap };
}

juce::Rectangle<int> StepGrid::stepsBounds (int t) const
{
    return rowBounds (t).withTrimmedLeft (kHeaderW).withTrimmedRight (kInfoW);
}

int StepGrid::columnsFor (int t) const
{
    const int len = juce::jlimit (1, kMaxSteps, (int) ctx.tp (t).length);
    return ((len + 15) / 16) * 16;
}

void StepGrid::cellGeometry (int t, float& x0, float& cellW) const
{
    const auto sb = stepsBounds (t);
    x0 = (float) sb.getX();
    cellW = (float) sb.getWidth() / (float) columnsFor (t);
}

juce::Rectangle<float> StepGrid::cellBounds (int t, int i) const
{
    float x0, cw; cellGeometry (t, x0, cw);
    const auto sb = stepsBounds (t);
    return { x0 + cw * (float) i, (float) sb.getY(), cw, (float) sb.getHeight() };
}

StepGrid::Hit StepGrid::hitTest (juce::Point<int> p) const
{
    Hit h;
    const int rowH = getHeight() / kTracks;
    const int t = p.y / juce::jmax (1, rowH);
    if (t < 0 || t >= kTracks) return h;
    const auto rb = rowBounds (t);
    if (! rb.contains (p)) { h.track = t; return h; }
    h.track = t;
    if (p.x < kHeaderW)
    {
        const int bx = kHeaderW - 76;
        if (p.x >= bx && p.x < bx + 20) h.zone = Zone::Mute;
        else if (p.x >= bx + 22 && p.x < bx + 42) h.zone = Zone::Solo;
        else if (p.x >= bx + 44 && p.x < bx + 64) h.zone = Zone::Lock;
        else h.zone = Zone::Header;
        return h;
    }
    const auto sb = stepsBounds (t);
    if (p.x >= sb.getRight()) { h.zone = Zone::Info; return h; }
    float x0, cw; cellGeometry (t, x0, cw);
    const int i = (int) (((float) p.x - x0) / cw);
    if (i >= 0 && i < ctx.tp (t).length) { h.zone = Zone::Steps; h.step = i; }
    h.fy = 1.f - juce::jlimit (0.f, 1.f, (float) (p.y - sb.getY()) / (float) sb.getHeight());
    return h;
}

// ============================================================================ painting
void StepGrid::tick (bool projectChanged)
{
    bool all = projectChanged || ctx.uiRev != lastUiRev;
    lastUiRev = ctx.uiRev;
    for (int t = 0; t < kTracks; ++t)
    {
        bool dirty = false;
        const int s = ctx.eng.shared.step[t].load (std::memory_order_relaxed);
        if (s != playStep[t]) { playStep[t] = s; dirty = true; }
        const uint32_t tc = ctx.eng.shared.trigCount[t].load (std::memory_order_relaxed);
        if (tc != trigSeen[t]) { trigSeen[t] = tc; flash[t] = 1.f; dirty = true; }
        else if (flash[t] > 0.f) { flash[t] = juce::jmax (0.f, flash[t] - 0.2f); dirty = true; }
        const float pk = ctx.eng.shared.trackPeak[t].load (std::memory_order_relaxed);
        const float np = juce::jmax (pk, peak[t] * 0.82f);
        if (std::abs (np - peak[t]) > 0.003f) { peak[t] = np; dirty = true; }
        if (dirty && ! all) repaint (rowBounds (t));
    }
    if (all) repaint();
}

void StepGrid::paint (juce::Graphics& g)
{
    const auto clip = g.getClipBounds();
    for (int t = 0; t < kTracks; ++t)
        if (rowBounds (t).intersects (clip)) paintRow (g, t);
}

void StepGrid::paintRow (juce::Graphics& g, int t)
{
    const auto rb = rowBounds (t);
    const TrackSound& ts = ctx.ts (t);
    const TrackPattern& tp = ctx.tp (t);
    const juce::Colour tc = col::track (t);
    const bool sel = t == ctx.track;
    bool anySolo = false;
    for (int k = 0; k < kTracks; ++k) anySolo |= ctx.ts (k).solo != 0;
    const bool audible = ! ts.mute && (! anySolo || ts.solo);

    // ---- header
    auto hb = rb.withWidth (kHeaderW - 4);
    fillPanel (g, hb, sel ? col::panelHi() : col::panel(), 3.f);
    if (sel) { g.setColour (tc); g.fillRect (hb.getX(), hb.getY(), 3, hb.getHeight()); }
    // trigger lamp + number
    g.setColour (tc.withAlpha (audible ? 0.25f + 0.75f * flash[t] : 0.12f));
    g.fillRoundedRectangle ((float) hb.getX() + 8.f, (float) hb.getY() + 6.f, 22.f, (float) hb.getHeight() - 12.f, 2.f);
    drawLabel (g, juce::String (t + 1), { hb.getX() + 8, hb.getY() + 6, 22, hb.getHeight() - 12 },
               flash[t] > 0.5f && audible ? col::bg() : tc, 15.f, juce::Justification::centred, true);
    // name + engine tag
    const int textX = hb.getX() + 37;
    drawLabel (g, juce::String (ts.name).toUpperCase(), { textX, hb.getY() + 4, kHeaderW - 76 - textX - 4, 15 },
               audible ? col::text() : col::faint(), 11.f, juce::Justification::centredLeft, true);
    juce::String sub = kEngineTags[juce::jlimit (0, (int) Engine::count - 1, (int) ts.engine)];
    sub << "  " << juce::String (roleName (ts.role)).toUpperCase();
    if (ts.fxType[0] != Fx::None) sub << "  +" << juce::String (fxName (ts.fxType[0])).toUpperCase().substring (0, 4);
    if (ts.fxType[1] != Fx::None) sub << " +" << juce::String (fxName (ts.fxType[1])).toUpperCase().substring (0, 4);
    drawLabel (g, sub, { textX, hb.getBottom() - 18, kHeaderW - 76 - textX - 4, 13 }, col::dim(), 9.f, juce::Justification::centredLeft);
    // M / S / lock
    const int bx = kHeaderW - 76, by = hb.getCentreY() - 9;
    auto small = [&] (int x, const char* txt, bool on, juce::Colour c)
    {
        juce::Rectangle<int> r (x, by, 20, 18);
        fillPanel (g, r, on ? c : col::well(), 2.f);
        strokePanel (g, r, on ? c : col::line(), 2.f);
        drawLabel (g, txt, r, on ? col::bg() : col::dim(), 9.5f, juce::Justification::centred, true);
    };
    small (bx, "M", ts.mute != 0, col::warn());
    small (bx + 22, "S", ts.solo != 0, col::accent());
    small (bx + 44, "L", ts.locked != 0, col::lock());
    // meter
    {
        const juce::Rectangle<float> m ((float) hb.getRight() - 7.f, (float) hb.getY() + 4.f, 3.f, (float) hb.getHeight() - 8.f);
        g.setColour (col::well()); g.fillRect (m);
        const float lv = juce::jlimit (0.f, 1.f, (gainToDb (peak[t]) + 48.f) / 48.f);
        g.setColour (peak[t] > 1.f ? col::warn() : tc);
        g.fillRect (m.withTop (m.getBottom() - m.getHeight() * lv));
    }

    // ---- steps
    const auto sb = stepsBounds (t);
    float x0, cw; cellGeometry (t, x0, cw);
    const int len = juce::jlimit (1, kMaxSteps, (int) tp.length);
    const int cols = columnsFor (t);
    g.setColour (col::panel().withAlpha (0.6f));
    g.fillRect (sb);
    for (int i = 0; i < cols; ++i)
    {
        const juce::Rectangle<float> r (x0 + cw * (float) i, (float) sb.getY(), cw, (float) sb.getHeight());
        if (i >= len)
        {
            g.setColour (col::line().withAlpha (0.5f));
            g.fillRect (r.getX() + cw * 0.5f - 1.f, r.getCentreY() - 1.f, 2.f, 2.f);
            continue;
        }
        paintCell (g, t, i, r, tp.steps[i], playStep[t] == i, ctx.stepSelected (t, i));
    }

    // ---- info column
    auto ib = rb.withLeft (sb.getRight() + 4);
    fillPanel (g, ib, col::panel(), 3.f);
    const SpeedDef& sp = speedDef (speedIndex (tp.spNum, tp.spDen));
    static const char* const dirGlyph[] = { ">>", "<<", "<>", "??", "~~" };
    drawLabel (g, juce::String (len), { ib.getX() + 4, ib.getY() + 3, 30, ib.getHeight() - 6 }, tc, 15.f, juce::Justification::centred, true);
    drawLabel (g, juce::String (sp.name), { ib.getX() + 36, ib.getY() + 4, 44, 14 }, col::text(), 10.f, juce::Justification::centredLeft, true);
    juce::String l2 = dirGlyph[juce::jlimit (0, 4, (int) tp.dir)];
    if (tp.freeRun) l2 << " FREE";
    if (tp.swing >= 0.f) l2 << " S" << juce::String ((int) std::lround (tp.swing * 100.f));
    drawLabel (g, l2, { ib.getX() + 36, ib.getBottom() - 18, ib.getWidth() - 40, 13 }, col::dim(), 9.f, juce::Justification::centredLeft);
    if (tp.gen.seedLocked) { g.setColour (col::lock()); g.fillRect (ib.getRight() - 8, ib.getY() + 5, 4, 4); }
}

void StepGrid::paintCell (juce::Graphics& g, int t, int i, juce::Rectangle<float> cell, const Step& s, bool playing, bool selected)
{
    const juce::Colour tc = col::track (t);
    const TrackPattern& tp = ctx.tp (t);
    auto r = cell.reduced (1.f, 0.f);
    const bool beat = (i % 4) == 0;
    g.setColour (beat ? col::panelHi().brighter (0.05f) : col::well().brighter (0.02f));
    g.fillRoundedRectangle (r, 2.f);
    if ((i % 16) == 0 && i > 0) { g.setColour (col::lineHi()); g.fillRect (cell.getX() - 0.5f, cell.getY(), 1.f, cell.getHeight()); }

    const float cw = r.getWidth(), h = r.getHeight();
    if (s.on)
    {
        const float alpha = 0.35f + 0.65f * ((float) s.prob / 100.f);
        const int n = juce::jlimit (1, kMaxRatchets, (int) s.ratCount);
        const float baseVel = (float) s.vel / 127.f;
        const float shift = s.micro * cw;
        g.saveState();
        g.reduceClipRegion (r.getSmallestIntegerContainer());
        for (int k = 0; k < n; ++k)
        {
            const float p0 = (float) SeqEngine::ratchetPos (k, n, s.ratCurve, s.ratAmt);
            const float p1 = k + 1 < n ? (float) SeqEngine::ratchetPos (k + 1, n, s.ratCurve, s.ratAmt) : 1.f;
            float v = baseVel;
            if (n > 1)
            {
                const float u = (float) k / (float) (n - 1);
                v *= s.ratVel < 0.f ? 1.f + s.ratVel * u : 1.f - s.ratVel * (1.f - u);
            }
            v = juce::jlimit (0.06f, 1.f, v);
            const float bx = r.getX() + shift + p0 * cw + (n > 1 ? 0.5f : 1.5f);
            const float bw = juce::jmax (1.5f, (p1 - p0) * cw * (n > 1 ? juce::jlimit (0.2f, 1.f, s.ratGate) : 1.f) - (n > 1 ? 1.f : 3.f));
            const float bh = juce::jmax (3.f, (h - 9.f) * v);
            // pitch ramp tints the slices
            juce::Colour c = tc;
            if (n > 1 && s.ratPitch != 0.f) c = c.withRotatedHue (0.04f * (s.ratPitch > 0 ? 1.f : -1.f) * (float) k / (float) n);
            g.setColour (c.withAlpha (alpha));
            g.fillRoundedRectangle (bx, r.getBottom() - 4.f - bh, bw, bh, 1.5f);
        }
        if (s.prob < 100)
        {
            // hatch: this step may not play
            g.setColour (col::bg().withAlpha (0.55f));
            for (float x = r.getX() - h; x < r.getRight(); x += 5.f) g.drawLine (x, r.getBottom() - 4.f, x + h, r.getY(), 1.2f);
        }
        g.restoreState();
        // gate length
        const float gl = juce::jmin (s.len, 8.f) * cell.getWidth();
        g.setColour (tc.withAlpha (0.8f));
        g.fillRect (r.getX() + shift + 1.5f, r.getBottom() - 3.f, juce::jmax (2.f, juce::jmin (gl, cell.getWidth() * 4.f) - 3.f), 1.5f);
        if (s.slide) { g.setColour (col::text()); g.fillRect (r.getX() - 3.f, r.getBottom() - 3.f, 6.f, 1.5f); }
        // annotations
        if (cw >= 24.f)
        {
            if (s.cond != Cond::Always)
            {
                char buf[8]; condName (s.cond, buf, 8);
                drawLabel (g, buf, r.withHeight (10.f).translated (2.f, 1.f).getSmallestIntegerContainer(), col::text(), 8.5f, juce::Justification::centredLeft, true);
            }
            if (s.pitch != 0)
                drawLabel (g, (s.pitch > 0 ? "+" : "") + juce::String ((int) s.pitch), r.withHeight (10.f).translated (-2.f, 1.f).getSmallestIntegerContainer(),
                           col::text().withAlpha (0.85f), 8.5f, juce::Justification::centredRight);
            if (s.prob < 100 && s.cond == Cond::Always)
                drawLabel (g, juce::String ((int) s.prob) + "%", r.withHeight (10.f).translated (2.f, 1.f).getSmallestIntegerContainer(), col::text().withAlpha (0.8f), 8.5f, juce::Justification::centredLeft);
        }
        else if (s.pitch != 0 || s.cond != Cond::Always)
        {
            g.setColour (col::text());
            g.fillRect (r.getX() + 1.5f, r.getY() + 1.5f, 2.f, 2.f);
        }
    }
    // locks: a dot per locked parameter (up to 4)
    int locks = 0;
    for (const LockLane& l : tp.lanes) if (l.used() && l.has (i)) ++locks;
    for (int k = 0; k < juce::jmin (locks, 4); ++k)
    {
        g.setColour (col::lock());
        g.fillRect (r.getRight() - 4.f - (float) k * 4.f, r.getY() + (s.on && cw >= 24.f && s.pitch != 0 ? 12.f : 2.f), 2.5f, 2.5f);
    }
    if (playing)
    {
        g.setColour (col::text().withAlpha (0.9f));
        g.fillRect (r.getX(), r.getY(), r.getWidth(), 2.f);
        g.setColour (col::text().withAlpha (0.07f));
        g.fillRoundedRectangle (r, 2.f);
    }
    if (selected)
    {
        g.setColour (ctx.lockMode ? col::lock() : col::text());
        g.drawRoundedRectangle (r.reduced (0.5f), 2.f, ctx.lockMode ? 1.6f : 1.f);
    }
}

// ============================================================================ interaction
void StepGrid::mouseMove (const juce::MouseEvent& e)
{
    const Hit h = hitTest (e.getPosition());
    switch (h.zone)
    {
        case Zone::Steps:  ctx.hoverHelp = "Click/drag: paint   Shift-drag: velocity   Ctrl-drag: probability   Alt-drag: ratchets   Wheel: pitch   Right-click: select for parameter locks   Double-click: step menu"; break;
        case Zone::Mute:   ctx.hoverHelp = "Mute track"; break;
        case Zone::Solo:   ctx.hoverHelp = "Solo track"; break;
        case Zone::Lock:   ctx.hoverHelp = "Lock track: protected from New Idea and Evolve"; break;
        case Zone::Header: ctx.hoverHelp = "Click to select the track. Right-click for track tools. Drop an audio file to load it."; break;
        case Zone::Info:   ctx.hoverHelp = "Drag up/down to change the track length. Right-click for speed and direction."; break;
        default: ctx.hoverHelp = {}; break;
    }
}

void StepGrid::applyDrag (const Hit& h, const juce::MouseEvent& e)
{
    if (h.track != dragTrack || h.zone != Zone::Steps || h.step < 0) return;
    const int pat = ctx.pattern();
    Step s = ctx.tp (dragTrack).steps[h.step];
    const Step before = s;
    switch (mode)
    {
        case Mode::Paint:
            if (h.step == lastStep) return;
            s.on = paintOn ? 1 : 0;
            if (paintOn && ! before.on) { s.prob = 100; s.cond = Cond::Always; }
            break;
        case Mode::Velocity: s.vel = (uint8_t) juce::jlimit (1, 127, (int) std::lround (h.fy * 127.f)); s.on = 1; break;
        case Mode::Prob:     s.prob = (uint8_t) juce::jlimit (0, 100, (int) std::lround (h.fy * 20.f) * 5); break;
        case Mode::Ratchet:  s.ratCount = (uint8_t) juce::jlimit (1, 8, 1 + (int) (h.fy * 7.99f)); break;
        default: return;
    }
    lastStep = h.step;
    juce::ignoreUnused (e);
    if (std::memcmp (&s, &before, sizeof (Step)) != 0) ctx.sess.setStep (pat, dragTrack, h.step, s);
    char buf[96];
    if (mode == Mode::Velocity) std::snprintf (buf, sizeof (buf), "Step %d velocity %d", h.step + 1, (int) s.vel);
    else if (mode == Mode::Prob) std::snprintf (buf, sizeof (buf), "Step %d probability %d%%", h.step + 1, (int) s.prob);
    else if (mode == Mode::Ratchet) std::snprintf (buf, sizeof (buf), "Step %d ratchet x%d", h.step + 1, (int) s.ratCount);
    else std::snprintf (buf, sizeof (buf), "Step %d %s", h.step + 1, s.on ? "on" : "off");
    ctx.hoverHelp = buf;
}

void StepGrid::mouseDown (const juce::MouseEvent& e)
{
    const Hit h = hitTest (e.getPosition());
    if (h.track < 0) return;
    const TrackSound& ts = ctx.ts (h.track);
    switch (h.zone)
    {
        case Zone::Mute: ctx.proc.setTrackMute (h.track, ! ts.mute); return;
        case Zone::Solo: ctx.sess.setTrackFlags (h.track, ts.mute != 0, ! ts.solo); return;
        case Zone::Lock: ctx.sess.setTrackLocked (h.track, ! ts.locked); return;
        case Zone::Header:
            ctx.selectTrack (h.track);
            if (e.mods.isPopupMenu()) showHeaderMenu (h.track);
            return;
        case Zone::Info:
            ctx.selectTrack (h.track);
            if (e.mods.isPopupMenu()) { ctx.setTab (TabSeq); return; }
            mode = Mode::Length; dragTrack = h.track; lengthStart = ctx.tp (h.track).length;
            ctx.sess.beginTransaction ("Track length");
            return;
        case Zone::Steps: break;
        default: ctx.selectTrack (h.track); return;
    }
    if (h.step < 0) { ctx.selectTrack (h.track); return; }
    ctx.selectTrack (h.track);
    dragTrack = h.track; lastStep = -1;

    if (e.mods.isPopupMenu())
    {
        // select for parameter locks; shift extends; clicking the single selected step again leaves lock mode
        if (e.mods.isShiftDown() && ctx.hasStepSelection()) ctx.selectSteps (ctx.stepA, h.step, true);
        else if (ctx.lockMode && ctx.stepA == h.step && ctx.stepB == h.step) ctx.clearStepSelection();
        else ctx.selectSteps (h.step, h.step, true);
        selAnchor = ctx.stepA;
        mode = Mode::Select;
        return;
    }
    if (e.mods.isShiftDown()) mode = Mode::Velocity;
    else if (e.mods.isCommandDown() || e.mods.isCtrlDown()) mode = Mode::Prob;
    else if (e.mods.isAltDown()) mode = Mode::Ratchet;
    else { mode = Mode::Paint; paintOn = ! ctx.tp (h.track).steps[h.step].on; }
    ctx.selectSteps (h.step, h.step, false);
    ctx.sess.beginTransaction (mode == Mode::Paint ? "Paint steps" : "Edit steps");
    applyDrag (h, e);
}

void StepGrid::mouseDrag (const juce::MouseEvent& e)
{
    if (mode == Mode::None) return;
    if (mode == Mode::Length)
    {
        const int len = juce::jlimit (1, kMaxSteps, lengthStart + (-e.getDistanceFromDragStartY()) / 6);
        if (len != ctx.tp (dragTrack).length)
        {
            TrackPattern p = ctx.tp (dragTrack);
            p.length = (uint8_t) len;
            ctx.sess.setTrackPattern (ctx.pattern(), dragTrack, p);
        }
        ctx.hoverHelp = "Track length " + juce::String (len);
        return;
    }
    Hit h = hitTest (e.getPosition());
    // keep painting on the row where the drag started
    if (h.track != dragTrack)
    {
        const auto sb = stepsBounds (dragTrack);
        h = hitTest ({ e.x, juce::jlimit (sb.getY() + 1, sb.getBottom() - 2, e.y) });
        h.fy = e.y < sb.getY() ? 1.f : 0.f;
    }
    if (mode == Mode::Select)
    {
        if (h.zone == Zone::Steps && h.step >= 0 && h.step != ctx.stepB) ctx.selectSteps (selAnchor, h.step, true);
        return;
    }
    applyDrag (h, e);
}

void StepGrid::mouseUp (const juce::MouseEvent&)
{
    if (mode == Mode::Paint || mode == Mode::Velocity || mode == Mode::Prob || mode == Mode::Ratchet || mode == Mode::Length)
        ctx.sess.endTransaction();
    mode = Mode::None;
}

void StepGrid::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const Hit h = hitTest (e.getPosition());
    if (h.zone != Zone::Steps || h.step < 0) return;
    Step s = ctx.tp (h.track).steps[h.step];
    const int d = w.deltaY > 0 ? 1 : -1;
    if (e.mods.isShiftDown()) s.vel = (uint8_t) juce::jlimit (1, 127, (int) s.vel + d * 6);
    else if (e.mods.isAltDown()) s.ratCount = (uint8_t) juce::jlimit (1, 16, (int) s.ratCount + d);
    else s.pitch = (int8_t) juce::jlimit (-48, 48, (int) s.pitch + d);
    ctx.sess.setStep (ctx.pattern(), h.track, h.step, s);
}

void StepGrid::mouseDoubleClick (const juce::MouseEvent& e)
{
    const Hit h = hitTest (e.getPosition());
    if (h.zone == Zone::Steps && h.step >= 0) showStepMenu (h.track, h.step);
}

void StepGrid::showStepMenu (int t, int i)
{
    juce::PopupMenu m, cond, rat, curve;
    const Step s = ctx.tp (t).steps[i];
    for (int c = 0; c < Cond::count; ++c)
    {
        char buf[8]; condName ((uint8_t) c, buf, 8);
        cond.addItem (100 + c, c == 0 ? "Always" : buf, true, s.cond == c);
    }
    static const int counts[] = { 1, 2, 3, 4, 5, 6, 8, 12, 16 };
    for (int c : counts) rat.addItem (300 + c, c == 1 ? "Off" : "x" + juce::String (c), true, s.ratCount == c);
    for (int c = 0; c < RatCurve::count; ++c) curve.addItem (400 + c, ratCurveName (c), true, s.ratCurve == c);
    m.addSectionHeader ("Step " + juce::String (i + 1));
    m.addSubMenu ("Condition", cond);
    m.addSubMenu ("Ratchet", rat);
    m.addSubMenu ("Ratchet curve", curve);
    m.addItem (1, "Slide into this step", true, s.slide != 0);
    m.addSeparator();
    m.addItem (2, "Clear parameter locks on this step", ctx.tp (t).stepHasLocks (i));
    m.addItem (3, "Reset step");
    juce::Component::SafePointer<StepGrid> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (
                         localAreaToGlobal (cellBounds (t, i).getSmallestIntegerContainer())), [safe, t, i] (int r)
    {
        if (safe == nullptr || r == 0) return;
        Ctx& c = safe->ctx;
        TrackPattern p = c.tp (t);
        Step& st = p.steps[i];
        if (r >= 100 && r < 100 + Cond::count) { st.cond = (uint8_t) (r - 100); st.on = 1; }
        else if (r >= 300 && r < 320) { st.ratCount = (uint8_t) (r - 300); st.on = 1; }
        else if (r >= 400 && r < 400 + RatCurve::count) st.ratCurve = (uint8_t) (r - 400);
        else if (r == 1) st.slide = ! st.slide;
        else if (r == 2) { for (auto& l : p.lanes) if (l.used()) { l.clear (i); if (l.mask == 0) l.dest = kDestNone; } }
        else if (r == 3) st = Step{};
        c.sess.setTrackPattern (c.pattern(), t, p);
    });
}

void StepGrid::showHeaderMenu (int t)
{
    juce::PopupMenu m;
    m.addSectionHeader ("Track " + juce::String (t + 1));
    m.addItem (1, "Copy pattern");
    m.addItem (2, "Paste pattern", ctx.clipPattern != nullptr);
    m.addItem (3, "Clear pattern");
    m.addSeparator();
    m.addItem (4, "Shift left");
    m.addItem (5, "Shift right");
    m.addItem (6, "Double length (repeat)");
    m.addItem (7, "Clear all parameter locks");
    m.addSeparator();
    m.addItem (8, "Copy sound");
    m.addItem (9, "Paste sound", ctx.clipSound != nullptr);
    juce::Component::SafePointer<StepGrid> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options(), [safe, t] (int r)
    {
        if (safe == nullptr || r == 0) return;
        Ctx& c = safe->ctx;
        TrackPattern p = c.tp (t);
        switch (r)
        {
            case 1: c.clipPattern = std::make_unique<TrackPattern> (p); return;
            case 2: if (c.clipPattern) c.sess.setTrackPattern (c.pattern(), t, *c.clipPattern); return;
            case 3: { const auto keep = p; p = TrackPattern{}; p.length = keep.length; p.spNum = keep.spNum; p.spDen = keep.spDen; p.seed = keep.seed; p.gen = keep.gen; break; }
            case 4: rotatePattern (p, -1); break;
            case 5: rotatePattern (p, 1); break;
            case 6:
            {
                const int len = p.length;
                if (len * 2 > kMaxSteps) return;
                for (int i = 0; i < len; ++i)
                {
                    p.steps[len + i] = p.steps[i]; p.stepMod[len + i] = p.stepMod[i];
                    for (auto& l : p.lanes) if (l.used() && l.has (i)) l.set (len + i, l.value[i]);
                }
                p.length = (uint8_t) (len * 2);
                break;
            }
            case 7: for (auto& l : p.lanes) l = LockLane{}; break;
            case 8: c.clipSound = std::make_unique<TrackSound> (c.ts (t)); return;
            case 9: if (c.clipSound) c.sess.setTrackSound (t, *c.clipSound); return;
            default: return;
        }
        c.sess.setTrackPattern (c.pattern(), t, p);
    });
}

} // namespace ui
