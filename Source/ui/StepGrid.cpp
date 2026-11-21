#include "StepGrid.h"

using namespace juce;
using namespace splinter;

String laneVarText (int lane, int var)
{
    if (var <= 0) return lane == laneChop ? "?" : String();
    switch (lane)
    {
        case laneChop: return String (var);
        case laneStutter: return stutterDivName (var - 1);
        case laneTapeStop: { static const char* n[4] = { "", "stop", "spin", "drag" }; return n[jlimit (0, 3, var)]; }
        case lanePitch: { const int d = var <= 7 ? var - 8 : var - 7; return (d > 0 ? "+" : "") + String (d); }
        case laneCrush: return "x" + String (var);
        case laneComb:
        case laneRing: return "d" + String (var);
        case laneFilter: { static const char* n[7] = { "", "LP-", "LP+", "HP+", "HP-", "BP+", "BP-" }; return n[jlimit (0, 6, var)]; }
        case laneFreeze: { static const char* n[5] = { "", "1/32", "1/16", "1/8", "1/4" }; return n[jlimit (0, 4, var)]; }
        case laneDelay: return delayDivName (var - 1);
        case laneReverb: { static const char* n[4] = { "", "S", "M", "L" }; return n[jlimit (0, 3, var)]; }
        case laneHarmony: { static const int o[9] = { 0, 1, 2, 3, 4, -1, -2, -3, -4 }; const int d = o[jlimit (0, 8, var)]; return (d > 0 ? "+" : "") + String (d); }
        default: return {};
    }
}

std::vector<std::pair<int, String>> laneVarChoices (int lane, int sliceCount)
{
    std::vector<std::pair<int, String>> v;
    auto add = [&] (int var, const String& t) { v.push_back ({ var, t }); };
    switch (lane)
    {
        case laneChop: add (0, "Random slice"); for (int i = 1; i <= jmin (kMaxSlices, sliceCount); ++i) add (i, "Slice " + String (i)); break;
        case laneStutter: add (0, "Lane default"); for (int i = 0; i < kNumStutterDivs; ++i) add (i + 1, stutterDivName (i)); break;
        case laneTapeStop: add (0, "Stop"); add (2, "Spin up"); add (3, "Half-speed drag"); break;
        case lanePitch: add (0, "Lane default"); for (int d = 7; d >= -7; --d) if (d != 0) add (d < 0 ? d + 8 : d + 7, String (d > 0 ? "+" : "") + String (d) + " scale steps"); break;
        case laneCrush: add (0, "Lane default"); add (1, "Light (12 bit)"); add (2, "Medium (8 bit)"); add (3, "Heavy (6 bit)"); add (4, "Destroyed (4 bit)"); break;
        case laneComb:
        case laneRing: add (0, "Lane default"); for (int i = 1; i <= 8; ++i) add (i, "Scale degree " + String (i)); break;
        case laneFilter: add (0, "Lane default"); add (1, "Low-pass closing"); add (2, "Low-pass opening"); add (3, "High-pass rising"); add (4, "High-pass falling"); add (5, "Band-pass up"); add (6, "Band-pass down"); break;
        case laneFreeze: add (0, "Lane default"); add (1, "1/32 grains"); add (2, "1/16 grains"); add (3, "1/8 grains"); add (4, "1/4 grains"); break;
        case laneDelay: add (0, "Lane default"); for (int i = 0; i < 6; ++i) add (i + 1, delayDivName (i)); break;
        case laneReverb: add (0, "Lane default"); add (1, "Small"); add (2, "Medium"); add (3, "Large"); break;
        case laneHarmony: add (0, "Chord as set"); for (int i = 1; i <= 8; ++i) add (i, "Chord " + laneVarText (laneHarmony, i) + " steps"); break;
        default: add (0, "On"); break;
    }
    return v;
}

StepGrid::StepGrid (SplinterProcessor& p) : proc (p) {}

Rectangle<float> StepGrid::cellBounds (int lane, int step) const
{
    const float rowH = (float) getHeight() / (float) kNumLanes;
    const float cw = (float) (getWidth() - headerW) / (float) jmax (1, pattern.numSteps);
    return { (float) headerW + cw * (float) step, rowH * (float) lane, cw, rowH };
}

bool StepGrid::hit (Point<float> p, int& lane, int& step) const
{
    const float rowH = (float) getHeight() / (float) kNumLanes;
    lane = jlimit (0, kNumLanes - 1, (int) (p.y / rowH));
    if (p.x < (float) headerW) { step = -1; return true; }
    const float cw = (float) (getWidth() - headerW) / (float) jmax (1, pattern.numSteps);
    step = jlimit (0, pattern.numSteps - 1, (int) ((p.x - (float) headerW) / cw));
    return true;
}

void StepGrid::tick()
{
    bool changed = false;
    if (proc.getBankVersion() != bankVersion)
    {
        bankVersion = proc.getBankVersion();
        const PatternBank b = proc.getBank();
        pattern = b.seq[slot]; sliceCount = b.slices.count;
        changed = true;
    }
    auto& vis = proc.engine.visual;
    const int ps = vis.seqSlot.load (std::memory_order_relaxed);
    const double beat = vis.beat.load (std::memory_order_relaxed);
    const int step = ps == slot ? floorModI ((int64_t) std::floor (beat * pattern.stepsPerBeat), jmax (1, pattern.numSteps)) : -1;
    if (step != playStep || ps != playingSlot) { playStep = step; playingSlot = ps; changed = true; }
    for (int l = 0; l < kNumLanes; ++l)
    {
        const float lvl = ps == slot ? vis.laneLevel[l].load (std::memory_order_relaxed) : 0.0f;
        const float g = lvl > 0.5f ? 1.0f : laneGlow[l] * 0.8f;
        if (std::fabs (g - laneGlow[l]) > 0.01f) changed = true;
        laneGlow[l] = g < 0.01f ? 0.0f : g;
    }
    if (changed) repaint();
}

void StepGrid::paint (Graphics& g)
{
    const float rowH = (float) getHeight() / (float) kNumLanes;
    const int n = jmax (1, pattern.numSteps);
    const float cw = (float) (getWidth() - headerW) / (float) n;
    const int spb = jmax (1, pattern.stepsPerBeat);

    for (int lane = 0; lane < kNumLanes; ++lane)
    {
        const Colour c = theme::laneColour (lane);
        const Rectangle<float> head (0.0f, rowH * (float) lane, (float) headerW - 4.0f, rowH - 2.0f);
        const bool sel = lane == selectedLane;
        g.setColour (sel ? c.withAlpha (0.22f) : theme::panelHi.withAlpha (0.6f));
        g.fillPath (theme::chamfer (head, 4.0f));
        if (laneGlow[lane] > 0.02f) { g.setColour (c.withAlpha (0.55f * laneGlow[lane])); g.fillPath (theme::chamfer (head, 4.0f)); }
        g.setColour (c); g.fillRect (head.withWidth (3.0f));
        g.setColour (sel || laneGlow[lane] > 0.5f ? theme::text : theme::textDim); g.setFont (theme::font (10.5f, true));
        g.drawText (String (laneName (lane)).toUpperCase(), head.withTrimmedLeft (9.0f).withTrimmedRight (18.0f).toNearestInt(), Justification::centredLeft, false);
        // lock
        const Rectangle<float> lock (head.getRight() - 16.0f, head.getCentreY() - 5.0f, 10.0f, 10.0f);
        const bool locked = pattern.locked[lane] != 0;
        g.setColour (locked ? theme::white : theme::textFaint.withAlpha (0.6f));
        g.fillRect (lock.withTrimmedTop (4.0f));
        Path shackle; shackle.addCentredArc (lock.getCentreX(), lock.getY() + 4.5f, 3.0f, 3.5f, 0.0f, -MathConstants<float>::halfPi, locked ? MathConstants<float>::halfPi : 0.6f, true);
        g.strokePath (shackle, PathStrokeType (1.4f));

        for (int s = 0; s < n; ++s)
        {
            const auto cell = cellBounds (lane, s).reduced (1.0f, 1.5f);
            const Step& st = pattern.steps[lane][s];
            const bool beatStart = s % spb == 0, barStart = s % (spb * 4) == 0;
            g.setColour (beatStart ? theme::panelHi.brighter (barStart ? 0.16f : 0.07f) : theme::panelHi);
            g.fillRect (cell);
            if (st.on)
            {
                const float h = cell.getHeight() * jlimit (0.18f, 1.0f, (float) st.prob / 100.0f);
                const auto bar = cell.withTop (cell.getBottom() - h);
                g.setColour (c.withAlpha (0.45f + 0.55f * (float) st.prob / 100.0f)); g.fillRect (bar);
                // tie marker to the next step of the same gesture
                if (s + 1 < n && pattern.steps[lane][s + 1].on && pattern.steps[lane][s + 1].var == st.var)
                { g.setColour (c.withAlpha (0.6f)); g.fillRect (Rectangle<float> (cell.getRight(), cell.getBottom() - 3.0f, 2.0f, 2.0f)); }
                const String vt = laneVarText (lane, st.var);
                if (vt.isNotEmpty() && cw > 17.0f)
                {
                    // on a short (low probability) bar the text sits above it, in the lane colour
                    const bool tall = st.prob >= 70;
                    g.setColour (tall ? theme::bg : c.brighter (0.3f)); g.setFont (theme::font (jmin (9.5f, cw * 0.36f), true));
                    g.drawText (vt, (tall ? cell : cell.withTrimmedBottom (h * 0.6f)).toNearestInt(), Justification::centred, false);
                }
            }
            if (s == playStep) { g.setColour (Colours::white.withAlpha (st.on ? 0.45f : 0.12f)); g.fillRect (cell); }
        }
    }
    // bar lines
    for (int s = 0; s <= n; s += spb * 4)
    {
        g.setColour (theme::gridStrong);
        g.drawVerticalLine (headerW + roundToInt (cw * (float) s), 0.0f, (float) getHeight());
    }
}

void StepGrid::commit()
{
    const SeqPattern p = pattern; const int s = slot;
    proc.editBank ([&] (PatternBank& b) { b.seq[s] = p; }, SplinterProcessor::seqBit (s));
    bankVersion = proc.getBankVersion();
    repaint();
}

void StepGrid::showCellMenu (int lane, int step)
{
    PopupMenu m;
    const Step st = pattern.steps[lane][step];
    PopupMenu prob;
    for (int p : { 100, 85, 70, 50, 35, 20 }) prob.addItem (1000 + p, String (p) + " %", true, st.on && st.prob == p);
    m.addSectionHeader (String (laneName (lane)) + " - step " + String (step + 1));
    for (auto& ch : laneVarChoices (lane, sliceCount))
        m.addItem (10 + ch.first, ch.second, true, st.on && st.var == ch.first);
    m.addSeparator();
    m.addSubMenu ("Probability", prob);
    m.addItem (2, "Clear step", st.on);
    m.addItem (3, "Clear lane");
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (this).withMousePosition().withMaximumNumColumns (3),
                     [safe = Component::SafePointer<StepGrid> (this), lane, step] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& st2 = safe->pattern.steps[lane][step];
        if (r == 2) st2 = Step();
        else if (r == 3) for (auto& x : safe->pattern.steps[lane]) x = Step();
        else if (r >= 1000) { st2.on = 1; st2.prob = (uint8_t) (r - 1000); }
        else if (r >= 10) { st2.on = 1; st2.var = (uint8_t) (r - 10); }
        safe->commit();
    });
}

void StepGrid::mouseDown (const MouseEvent& e)
{
    int lane, step; hit (e.position, lane, step);
    if (step < 0)
    {
        if (e.position.x > (float) headerW - 24.0f) { pattern.locked[lane] = pattern.locked[lane] ? 0 : 1; commit(); }
        else { selectedLane = lane; if (onLaneSelected) onLaneSelected (lane); repaint(); }
        return;
    }
    if (selectedLane != lane) { selectedLane = lane; if (onLaneSelected) onLaneSelected (lane); }
    if (e.mods.isPopupMenu()) { showCellMenu (lane, step); return; }
    Step& st = pattern.steps[lane][step];
    dragLane = lane; dragStep = step; dragMoved = false; probMode = false; wasOn = st.on != 0; dragStartProb = st.prob;
    if (! wasOn)
    {
        st.on = 1; st.prob = 100;
        // a new step copies the variation of its left neighbour, so drawing a run makes one gesture
        if (step > 0 && pattern.steps[lane][step - 1].on) st.var = pattern.steps[lane][step - 1].var; else st.var = 0;
        paintValue = true; commit();
    }
    else paintValue = false;
}

void StepGrid::mouseDrag (const MouseEvent& e)
{
    if (dragLane < 0) return;
    int lane, step; hit (e.position, lane, step);
    const float dy = (float) e.getDistanceFromDragStartY(), dx = (float) e.getDistanceFromDragStartX();
    if (! dragMoved && std::fabs (dy) > 5.0f && std::fabs (dy) > std::fabs (dx) && wasOn) probMode = true;
    if (std::fabs (dy) > 5.0f || std::fabs (dx) > 5.0f) dragMoved = true;
    if (probMode)
    {
        // vertical drag on an existing step sets its probability
        Step& st = pattern.steps[dragLane][dragStep];
        st.prob = (uint8_t) jlimit (5, 100, dragStartProb - roundToInt (dy * 1.2f));
        commit();
        return;
    }
    if (step >= 0 && dragMoved)
    {
        // horizontal drag paints (or erases) along the lane
        const int a = jmin (dragStep, step), b = jmax (dragStep, step);
        bool any = false;
        for (int s = a; s <= b; ++s)
        {
            Step& st = pattern.steps[dragLane][s];
            if (paintValue && ! st.on) { st.on = 1; st.prob = 100; st.var = pattern.steps[dragLane][dragStep].var; any = true; }
            else if (! paintValue && st.on) { st = Step(); any = true; }
        }
        if (any) commit();
    }
}

void StepGrid::mouseUp (const MouseEvent&)
{
    if (dragLane >= 0 && wasOn && ! dragMoved) { pattern.steps[dragLane][dragStep] = Step(); commit(); }   // plain click on a lit step clears it
    dragLane = -1;
}

void StepGrid::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    int lane, step; hit (e.position, lane, step);
    if (step < 0 || w.deltaY == 0.0f) return;
    Step& st = pattern.steps[lane][step];
    if (! st.on) return;
    const auto choices = laneVarChoices (lane, sliceCount);
    int idx = 0;
    for (size_t i = 0; i < choices.size(); ++i) if (choices[i].first == st.var) idx = (int) i;
    idx = jlimit (0, (int) choices.size() - 1, idx + (w.deltaY > 0 ? 1 : -1));
    st.var = (uint8_t) choices[(size_t) idx].first;
    commit();
}
