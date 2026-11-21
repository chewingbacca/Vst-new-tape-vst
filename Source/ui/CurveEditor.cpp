#include "CurveEditor.h"

using namespace juce;
using namespace splinter;

CurveEditor::CurveEditor (SplinterProcessor& p, bool isTimeCurve) : proc (p), isTime (isTimeCurve) {}

Point<float> CurveEditor::toScreen (float x, float y) const
{
    const auto r = plot();
    return { r.getX() + x * r.getWidth(), isTime ? r.getY() + y * r.getHeight() : r.getBottom() - y * r.getHeight() };
}
float CurveEditor::xFromScreen (float sx) const { const auto r = plot(); return jlimit (0.0f, 1.0f, (sx - r.getX()) / r.getWidth()); }
float CurveEditor::yFromScreen (float sy) const
{
    const auto r = plot();
    const float t = jlimit (0.0f, 1.0f, (sy - r.getY()) / r.getHeight());
    return isTime ? t : 1.0f - t;
}
float CurveEditor::snapX (float x, bool snap) const { return snap ? std::round (x * (float) gridDiv) / (float) gridDiv : x; }
float CurveEditor::snapY (float y, bool snap) const
{
    if (! snap) return y;
    if (! isTime) return std::round (y * 8.0f) / 8.0f;
    // time: snap to multiples of one grid cell, so a step back equals one cell (an exact repeat)
    const float cellBeats = curve.lengthBeats / (float) gridDiv;
    const float unit = cellBeats / jmax (0.25f, curve.rangeBeats);
    return jlimit (0.0f, 1.0f, std::round (y / unit) * unit);
}

void CurveEditor::tick()
{
    bool changed = false;
    if (proc.getBankVersion() != bankVersion && dragNode < 0 && tensionSeg < 0)
    {
        bankVersion = proc.getBankVersion();
        const PatternBank b = proc.getBank();
        curve = isTime ? b.time[slot] : b.vol[slot];
        changed = true;
    }
    auto& vis = proc.engine.visual;
    const bool here = (isTime ? vis.timeSlot.load() : vis.volSlot.load()) == slot;
    const double len = jmax (0.25, (double) curve.lengthBeats);
    const float ph = (float) (floorMod (vis.beat.load (std::memory_order_relaxed), len) / len);
    if (here != playingHere || (here && std::fabs (ph - phase) > 0.0005f)) changed = true;
    playingHere = here; phase = ph;
    if (changed) repaint();
}

void CurveEditor::drawThumb (Graphics& g, Rectangle<float> r, const Curve& c, bool time, Colour col)
{
    Path p;
    const int n = 48;
    for (int i = 0; i <= n; ++i)
    {
        const float x = (float) i / (float) n;
        const float y = c.eval (jmin (0.9999, (double) x));
        const float sy = time ? r.getY() + y * r.getHeight() : r.getBottom() - y * r.getHeight();
        if (i == 0) p.startNewSubPath (r.getX(), sy); else p.lineTo (r.getX() + x * r.getWidth(), sy);
    }
    g.setColour (col); g.strokePath (p, PathStrokeType (1.3f));
}

void CurveEditor::paint (Graphics& g)
{
    const auto r = plot();
    const Colour accent = isTime ? theme::lime : theme::cyan;
    g.setColour (theme::bg.brighter (0.03f)); g.fillRect (r);

    // grid
    const float beats = jmax (0.25f, curve.lengthBeats);
    for (int i = 0; i <= gridDiv; ++i)
    {
        const float x = r.getX() + r.getWidth() * (float) i / (float) gridDiv;
        const float beatPos = beats * (float) i / (float) gridDiv;
        const bool onBeat = std::fabs (beatPos - std::round (beatPos)) < 1.0e-4f;
        g.setColour (onBeat ? theme::gridStrong : theme::grid.withAlpha (0.7f));
        g.drawVerticalLine (roundToInt (x), r.getY(), r.getBottom());
        if (onBeat && i < gridDiv)
            theme::drawLabel (g, String (roundToInt (beatPos) + 1), Rectangle<int> (roundToInt (x) + 3, roundToInt (r.getBottom()) + 1, 24, 11), theme::textFaint, 9.0f, Justification::centredLeft);
    }
    if (isTime)
    {
        const float range = jmax (0.25f, curve.rangeBeats);
        const float stepBeats = range > 8.0f ? 4.0f : (range > 2.0f ? 1.0f : 0.25f);
        for (float b = 0.0f; b <= range + 1.0e-4f; b += stepBeats)
        {
            const float y = r.getY() + r.getHeight() * b / range;
            g.setColour (std::fabs (std::fmod (b, 4.0f)) < 1.0e-4f ? theme::gridStrong : theme::grid.withAlpha (0.7f));
            g.drawHorizontalLine (roundToInt (y), r.getX(), r.getRight());
            theme::drawLabel (g, b == 0.0f ? String ("LIVE") : "-" + String (b, b < 1.0f ? 2 : 0), Rectangle<int> (0, roundToInt (y) - 6, (int) r.getX() - 4, 12), b == 0.0f ? accent : theme::textFaint, 9.0f, Justification::centredRight);
        }
        // guide: the slope at which playback stands still (anything steeper runs backwards)
        {
            Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (r.toNearestInt());
            g.setColour (theme::white.withAlpha (0.10f));
            const float d[2] = { 4.0f, 5.0f };
            g.drawDashedLine (Line<float> (r.getX(), r.getY(), r.getRight(), r.getY() + beats / range * r.getHeight()), d, 2, 1.0f);
        }
    }
    else
        for (int i = 0; i <= 4; ++i)
        {
            const float y = r.getBottom() - r.getHeight() * (float) i / 4.0f;
            g.setColour (i % 2 == 0 ? theme::gridStrong : theme::grid.withAlpha (0.7f));
            g.drawHorizontalLine (roundToInt (y), r.getX(), r.getRight());
            theme::drawLabel (g, String (i * 25), Rectangle<int> (0, roundToInt (y) - 6, (int) r.getX() - 4, 12), theme::textFaint, 9.0f, Justification::centredRight);
        }

    // filled area + curve
    if (curve.numNodes > 0)
    {
        Path line, fill;
        const int n = jmax (64, (int) r.getWidth());
        const float base = isTime ? r.getY() : r.getBottom();
        fill.startNewSubPath (r.getX(), base);
        float prevY = 0.0f;
        for (int i = 0; i <= n; ++i)
        {
            const float x = (float) i / (float) n;
            const float y = curve.eval (jmin (0.99999, (double) x));
            const auto pt = toScreen (x, y);
            if (i == 0) line.startNewSubPath (pt);
            else
            {
                if (std::fabs (y - prevY) > 0.02f) line.lineTo (pt.x, toScreen (x, prevY).y);     // keep steps square
                line.lineTo (pt);
            }
            fill.lineTo (pt.x, i > 0 && std::fabs (y - prevY) > 0.02f ? toScreen (x, prevY).y : pt.y);
            fill.lineTo (pt);
            prevY = y;
        }
        fill.lineTo (r.getRight(), base); fill.closeSubPath();
        g.setColour (accent.withAlpha (0.10f)); g.fillPath (fill);

        if (isTime)
        {
            // colour each stretch of the curve by the playback speed it produces
            const float k = curve.rangeBeats / jmax (0.25f, curve.lengthBeats);
            const int segs = jmax (48, (int) (r.getWidth() / 3.0f));
            for (int i = 0; i < segs; ++i)
            {
                const float x0 = (float) i / (float) segs, x1 = (float) (i + 1) / (float) segs;
                const float y0 = curve.eval ((double) x0), y1 = curve.eval (jmin (0.99999, (double) x1));
                const float speed = 1.0f - (y1 - y0) / (x1 - x0) * k;
                Colour c = theme::lime;
                if (std::fabs (y1 - y0) > 0.03f * jmax (1.0f, 1.0f / k)) c = theme::white.withAlpha (0.55f);         // a jump
                else if (speed < -0.05f) c = theme::pink;
                else if (speed < 0.08f) c = theme::white;
                else if (speed < 0.95f) c = theme::cyan;
                else if (speed > 1.05f) c = theme::amber;
                const auto a = toScreen (x0, y0), b = toScreen (x1, y1);
                g.setColour (c);
                if (std::fabs (y1 - y0) > 0.03f * jmax (1.0f, 1.0f / k)) { g.drawLine (a.x, a.y, b.x, a.y, 2.0f); g.drawLine (b.x, a.y, b.x, b.y, 1.0f); }
                else g.drawLine (Line<float> (a, b), 2.2f);
            }
        }
        else { g.setColour (accent); g.strokePath (line, PathStrokeType (2.2f)); }
    }

    // nodes
    for (int i = 0; i < curve.numNodes; ++i)
    {
        const auto p = toScreen (curve.nodes[i].x, curve.nodes[i].y);
        const bool hot = i == hoverNode || i == dragNode;
        const float s = hot ? 6.0f : 4.2f;
        Path d;
        if (curve.nodes[i].hold) d.addRectangle (p.x - s * 0.8f, p.y - s * 0.8f, s * 1.6f, s * 1.6f);
        else { d.startNewSubPath (p.x, p.y - s); d.lineTo (p.x + s, p.y); d.lineTo (p.x, p.y + s); d.lineTo (p.x - s, p.y); d.closeSubPath(); }
        g.setColour (theme::bg); g.fillPath (d);
        g.setColour (hot ? theme::white : accent); g.strokePath (d, PathStrokeType (1.6f));
    }

    // playhead
    if (playingHere)
    {
        const float x = r.getX() + phase * r.getWidth();
        g.setColour (theme::white.withAlpha (0.8f)); g.drawVerticalLine (roundToInt (x), r.getY(), r.getBottom());
        if (curve.numNodes > 0)
        {
            const auto p = toScreen (phase, curve.eval ((double) phase));
            g.setColour (theme::white); g.fillEllipse (Rectangle<float> (9.0f, 9.0f).withCentre (p));
            g.setColour (accent); g.fillEllipse (Rectangle<float> (5.0f, 5.0f).withCentre (p));
        }
    }

    // readout for the node in hand
    const int shown = dragNode >= 0 ? dragNode : hoverNode;
    if (shown >= 0 && shown < curve.numNodes)
    {
        const auto& nd = curve.nodes[shown];
        String t = "beat " + String (nd.x * beats + 1.0f, 2) + "   ";
        t << (isTime ? String (nd.y * curve.rangeBeats, 2) + " beats behind" : String (roundToInt (nd.y * 100.0f)) + " %");
        if (nd.hold) t << "   hold";
        theme::drawLabel (g, t, Rectangle<int> ((int) r.getRight() - 260, (int) r.getY() + 3, 254, 13), theme::text, 10.0f, Justification::centredRight);
    }
    g.setColour (theme::outline); g.drawRect (r, 1.0f);
}

int CurveEditor::nodeAt (Point<float> p) const
{
    int best = -1; float bd = 9.0f;
    for (int i = 0; i < curve.numNodes; ++i)
    {
        const float d = toScreen (curve.nodes[i].x, curve.nodes[i].y).getDistanceFrom (p);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

int CurveEditor::segmentAt (Point<float> p) const
{
    const float x = xFromScreen (p.x);
    for (int i = 0; i + 1 < curve.numNodes; ++i)
        if (x > curve.nodes[i].x && x < curve.nodes[i + 1].x && ! curve.nodes[i].hold)
        {
            const float y = curve.eval ((double) x);
            if (std::fabs (toScreen (x, y).y - p.y) < 10.0f) return i;
        }
    return -1;
}

void CurveEditor::sortNodes()
{
    std::stable_sort (curve.nodes, curve.nodes + curve.numNodes, [] (const CurveNode& a, const CurveNode& b) { return a.x < b.x; });
}

void CurveEditor::commit()
{
    curve.hint = 0;
    const Curve c = curve; const int s = slot;
    if (isTime) proc.editBank ([&] (PatternBank& b) { b.time[s] = c; }, SplinterProcessor::timeBit (s));
    else proc.editBank ([&] (PatternBank& b) { b.vol[s] = c; }, SplinterProcessor::volBit (s));
    bankVersion = proc.getBankVersion();
    repaint();
}

void CurveEditor::modify (const std::function<void (Curve&)>& fn) { fn (curve); sortNodes(); commit(); }

void CurveEditor::paintCell (Point<float> p)
{
    // pencil: every grid cell the mouse crosses becomes a flat step at the cursor height
    const int cell = jlimit (0, gridDiv - 1, (int) (xFromScreen (p.x) * (float) gridDiv));
    const float x0 = (float) cell / (float) gridDiv, x1 = (float) (cell + 1) / (float) gridDiv;
    const float y = snapY (yFromScreen (p.y), true);
    const float after = curve.numNodes > 0 ? curve.eval (jmin (0.99999, (double) x1 + 1.0e-6)) : (isTime ? 0.0f : 1.0f);
    bool afterHasNode = false;
    for (int i = 0; i < curve.numNodes; ++i) if (std::fabs (curve.nodes[i].x - x1) < 1.0e-5f) afterHasNode = true;
    int w = 0;
    for (int i = 0; i < curve.numNodes; ++i)
        if (curve.nodes[i].x < x0 - 1.0e-5f || curve.nodes[i].x > x1 - 1.0e-5f) curve.nodes[w++] = curve.nodes[i];
    curve.numNodes = w;
    curve.add (x0, y, 0.0f, true);
    if (! afterHasNode && x1 < 0.9999f) curve.add (x1, after, 0.0f, true);
    sortNodes(); commit();
    lastPaintCell = cell;
}

void CurveEditor::mouseMove (const MouseEvent& e)
{
    const int n = nodeAt (e.position);
    if (n != hoverNode) { hoverNode = n; repaint(); }
}

void CurveEditor::mouseDown (const MouseEvent& e)
{
    const int n = nodeAt (e.position);
    if (e.mods.isPopupMenu())
    {
        PopupMenu m;
        if (n >= 0)
        {
            m.addItem (1, "Hold (step)", true, curve.nodes[n].hold != 0);
            m.addItem (2, "Reset tension");
            m.addItem (3, "Delete point", curve.numNodes > 1);
        }
        m.addItem (4, "Clear curve");
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (this).withMousePosition(), [safe = Component::SafePointer<CurveEditor> (this), n] (int r)
        {
            if (safe == nullptr || r == 0) return;
            auto& c = safe->curve;
            if (r == 1 && n < c.numNodes) c.nodes[n].hold = c.nodes[n].hold ? 0 : 1;
            else if (r == 2 && n < c.numNodes) c.nodes[n].tension = 0.0f;
            else if (r == 3 && n < c.numNodes) { for (int i = n; i + 1 < c.numNodes; ++i) c.nodes[i] = c.nodes[i + 1]; --c.numNodes; }
            else if (r == 4) { c.clear(); const float y = safe->isTime ? 0.0f : 1.0f; c.add (0.0f, y); c.add (1.0f, y); }
            safe->commit();
        });
        return;
    }
    if (pencil && n < 0) { lastPaintCell = -1; paintCell (e.position); return; }
    if (n >= 0) { dragNode = n; return; }
    if (e.mods.isAltDown() || e.mods.isCommandDown())
    {
        tensionSeg = segmentAt (e.position);
        if (tensionSeg >= 0) { tensionStart = curve.nodes[tensionSeg].tension; return; }
    }
    // add a point
    if (curve.numNodes < kMaxNodes && plot().expanded (6.0f).contains (e.position))
    {
        const bool snap = ! e.mods.isShiftDown();
        const float x = snapX (xFromScreen (e.position.x), snap), y = snapY (yFromScreen (e.position.y), snap);
        curve.add (x, y);
        // keep it after any existing point at the same x (so it becomes the "after the jump" value)
        sortNodes();
        dragNode = -1;
        for (int i = curve.numNodes - 1; i >= 0; --i) if (curve.nodes[i].x == x && curve.nodes[i].y == y) { dragNode = i; break; }
        commit();
    }
}

void CurveEditor::mouseDrag (const MouseEvent& e)
{
    if (pencil && dragNode < 0 && tensionSeg < 0)
    {
        const int cell = jlimit (0, gridDiv - 1, (int) (xFromScreen (e.position.x) * (float) gridDiv));
        if (cell != lastPaintCell || true) paintCell (e.position);
        return;
    }
    if (tensionSeg >= 0 && tensionSeg < curve.numNodes)
    {
        const float dir = (isTime ? 1.0f : -1.0f) * (curve.nodes[tensionSeg + 1].y > curve.nodes[tensionSeg].y ? 1.0f : -1.0f);
        curve.nodes[tensionSeg].tension = jlimit (-1.0f, 1.0f, tensionStart + dir * (float) e.getDistanceFromDragStartY() * 0.012f);
        commit(); return;
    }
    if (dragNode < 0 || dragNode >= curve.numNodes) return;
    const bool snap = ! e.mods.isShiftDown();
    float x = snapX (xFromScreen (e.position.x), snap);
    const float y = snapY (yFromScreen (e.position.y), snap);
    // a point cannot pass its neighbours (it may sit exactly on one: that makes a jump)
    const float lo = dragNode > 0 ? curve.nodes[dragNode - 1].x : 0.0f;
    const float hi = dragNode + 1 < curve.numNodes ? curve.nodes[dragNode + 1].x : 1.0f;
    x = jlimit (lo, hi, x);
    curve.nodes[dragNode].x = x; curve.nodes[dragNode].y = y;
    commit();
}

void CurveEditor::mouseUp (const MouseEvent&) { dragNode = -1; tensionSeg = -1; lastPaintCell = -1; }

void CurveEditor::mouseDoubleClick (const MouseEvent& e)
{
    const int n = nodeAt (e.position);
    if (n >= 0 && curve.numNodes > 1)
    {
        for (int i = n; i + 1 < curve.numNodes; ++i) curve.nodes[i] = curve.nodes[i + 1];
        --curve.numNodes; hoverNode = -1; commit();
    }
}
