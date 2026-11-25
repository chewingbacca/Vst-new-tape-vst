// STEPS tab: per-step lanes aligned with the grid columns of the selected track,
// one lane per parameter lock, and an inspector for the selected step(s).
#include "DetailPanel.h"

namespace ui {

using namespace atm;

namespace {

enum LaneKind { LVel, LPitch, LProb, LMicro, LLen, LRatchet, LStepMod, LLock, numBuiltIn = LLock };

struct LaneDef { const char* name; const char* help; bool bipolar; };
const LaneDef kLanes[numBuiltIn] = {
    { "VELOCITY", "Step velocity (drag to draw; off steps are dimmed but keep their value)", false },
    { "PITCH",    "Pitch offset: semitones, or scale degrees when the track is scale-quantised (-24..+24)", true },
    { "PROB",     "Chance that the step plays, re-rolled every pass but tied to song position (repeatable)", false },
    { "MICRO",    "Micro-timing: push a step up to half a step early or late", true },
    { "LENGTH",   "Gate length in steps (0.05..4)", false },
    { "RATCHET",  "Number of repeats inside the step (1..16). Curve, ramps and gate are in the inspector below.", false },
    { "STEP MOD", "Step modulation lane: a per-step value you can route to anything in the MOD tab", true },
};

class StepsPage : public Page
{
public:
    StepsPage (Ctx& c, StepGrid& g) : Page (c), grid (g)
    {
        auto setupBox = [this] (NumberBox& b, const char* cap, double lo, double hi, double step, double def, const char* help)
        {
            b.ctx = &ctx; b.caption = cap; b.minV = lo; b.maxV = hi; b.step = step; b.defaultV = def; b.help = help;
            b.onGestureStart = [this] { ctx.sess.beginTransaction ("Edit step"); };
            b.onGestureEnd = [this] { ctx.sess.endTransaction(); };
            addAndMakeVisible (b);
        };
        setupBox (velBox, "VEL", 1, 127, 1, 100, "Velocity of the selected step(s)");
        velBox.get = [this] { return (double) sel().vel; };
        velBox.set = [this] (double v) { edit ([v] (Step& s) { s.vel = (uint8_t) v; }); };
        velBox.pixelsPerStep = 1.5;
        setupBox (pitchBox, "PITCH", -48, 48, 1, 0, "Pitch offset of the selected step(s)");
        pitchBox.get = [this] { return (double) sel().pitch; };
        pitchBox.set = [this] (double v) { edit ([v] (Step& s) { s.pitch = (int8_t) v; }); };
        pitchBox.format = [] (double v) { return (v > 0 ? "+" : "") + juce::String ((int) v); };
        setupBox (probBox, "PROB", 0, 100, 5, 100, "Probability of the selected step(s)");
        probBox.get = [this] { return (double) sel().prob; };
        probBox.set = [this] (double v) { edit ([v] (Step& s) { s.prob = (uint8_t) v; }); };
        probBox.format = [] (double v) { return juce::String ((int) v) + "%"; };
        setupBox (lenBox, "LEN", 0.05, 16, 0.05, 1, "Gate length in steps");
        lenBox.get = [this] { return (double) sel().len; };
        lenBox.set = [this] (double v) { edit ([v] (Step& s) { s.len = (float) v; }); };
        lenBox.format = [] (double v) { return juce::String (v, 2); };
        lenBox.pixelsPerStep = 2;
        setupBox (microBox, "MICRO", -50, 50, 1, 0, "Micro-timing offset in percent of a step");
        microBox.get = [this] { return (double) std::lround (sel().micro * 100.f); };
        microBox.set = [this] (double v) { edit ([v] (Step& s) { s.micro = (float) v / 100.f; }); };
        microBox.format = [] (double v) { return (v > 0 ? "+" : "") + juce::String ((int) v) + "%"; };
        setupBox (ratBox, "RATCHET", 1, 16, 1, 1, "Repeats inside the step: the drill edit (Alt-drag in the grid)");
        ratBox.get = [this] { return (double) sel().ratCount; };
        ratBox.set = [this] (double v) { edit ([v] (Step& s) { s.ratCount = (uint8_t) v; if (v > 1) s.on = 1; }); };
        ratBox.format = [] (double v) { return v <= 1 ? juce::String ("OFF") : "x" + juce::String ((int) v); };
        ratBox.colour = col::lock().brighter (0.3f);
        ratBox.pixelsPerStep = 8;

        condPicker.ctx = &ctx; condPicker.caption = "CONDITION";
        condPicker.help = "Trig condition: A:B plays on pass A of every B, FILL only while Fill is held, PRE only if the previous step played, 1ST only on the first pass";
        condPicker.items = [] { juce::StringArray a; for (int i = 0; i < Cond::count; ++i) { char b[8]; condName ((uint8_t) i, b, 8); a.add (i == 0 ? "ALWAYS" : b); } return a; };
        condPicker.get = [this] { return (int) sel().cond; };
        condPicker.set = [this] (int v) { Session::Transaction tx (ctx.sess, "Condition"); edit ([v] (Step& s) { s.cond = (uint8_t) v; }); };
        addAndMakeVisible (condPicker);
        curvePicker.ctx = &ctx; curvePicker.caption = "CURVE";
        curvePicker.help = "Ratchet spacing: even, accelerating, decelerating, or a bouncing ball";
        curvePicker.items = [] { juce::StringArray a; for (int i = 0; i < RatCurve::count; ++i) a.add (juce::String (ratCurveName (i)).toUpperCase()); return a; };
        curvePicker.get = [this] { return (int) sel().ratCurve; };
        curvePicker.set = [this] (int v) { Session::Transaction tx (ctx.sess, "Ratchet curve"); edit ([v] (Step& s) { s.ratCurve = (uint8_t) v; }); };
        addAndMakeVisible (curvePicker);

        auto setupKnob = [this] (ValueKnob& k, const char* cap, const char* help, bool bip, float def)
        {
            k.ctx = &ctx; k.caption = cap; k.help = help; k.bipolar = bip; k.defaultValue = def;
            k.onGestureStart = [this] { ctx.sess.beginTransaction ("Ratchet"); };
            k.onGestureEnd = [this] { ctx.sess.endTransaction(); };
            addAndMakeVisible (k);
        };
        setupKnob (ratAmt, "Amount", "How strongly the curve bends the spacing", false, 0.5f);
        ratAmt.get = [this] { return sel().ratAmt; };
        ratAmt.set = [this] (float v) { edit ([v] (Step& s) { s.ratAmt = v; }); };
        ratAmt.format = [] (float v) { return juce::String ((int) std::lround (v * 100.f)) + "%"; };
        setupKnob (ratPitch, "Pitch Rmp", "Pitch ramp across the roll (+-24 semitones)", true, 0.5f);
        ratPitch.get = [this] { return (sel().ratPitch + 24.f) / 48.f; };
        ratPitch.set = [this] (float v) { edit ([v] (Step& s) { s.ratPitch = std::round (v * 48.f - 24.f); }); };
        ratPitch.format = [] (float v) { const int st = (int) std::lround (v * 48.f - 24.f); return (st > 0 ? "+" : "") + juce::String (st) + " st"; };
        setupKnob (ratVel, "Vel Rmp", "Velocity ramp across the roll: negative fades out, positive fades in", true, 0.5f);
        ratVel.get = [this] { return (sel().ratVel + 1.f) * 0.5f; };
        ratVel.set = [this] (float v) { edit ([v] (Step& s) { s.ratVel = v * 2.f - 1.f; }); };
        ratVel.format = [] (float v) { return juce::String (v * 2.f - 1.f, 2); };
        setupKnob (ratGate, "Gate", "Length of each repeat within its slot", false, 0.8f);
        ratGate.get = [this] { return (sel().ratGate - 0.05f) / 0.95f; };
        ratGate.set = [this] (float v) { edit ([v] (Step& s) { s.ratGate = 0.05f + v * 0.95f; }); };
        ratGate.format = [] (float v) { return juce::String ((int) std::lround ((0.05f + v * 0.95f) * 100.f)) + "%"; };
        for (auto* k : { &ratAmt, &ratPitch, &ratVel, &ratGate }) k->colour = col::lock().brighter (0.3f);

        onBtn.ctx = &ctx; onBtn.help = "Step on/off (Enter)";
        onBtn.onClick = [this] { Session::Transaction tx (ctx.sess, "Toggle step"); const bool on = ! sel().on; edit ([on] (Step& s) { s.on = on; }); };
        slideBtn.ctx = &ctx; slideBtn.help = "Slide: glide into this step without retriggering the envelopes (mono tracks)";
        slideBtn.onClick = [this] { Session::Transaction tx (ctx.sess, "Slide"); const bool on = ! sel().slide; edit ([on] (Step& s) { s.slide = on; }); };
        for (auto* b : { &onBtn, &slideBtn }) addAndMakeVisible (b);

        clearBtn.ctx = &ctx; clearBtn.help = "Reset this lane to its default for all steps (lock lanes: remove the lane)";
        clearBtn.onClick = [this] { clearLane(); };
        rndBtn.ctx = &ctx; rndBtn.help = "Randomise this lane (seeded by the track's pattern seed and the click count)";
        rndBtn.onClick = [this] { randomiseLane(); };
        rampBtn.ctx = &ctx; rampBtn.help = "Ramp this lane across the selected steps (or the whole track) from the first to the last value";
        rampBtn.onClick = [this] { rampLane(); };
        for (auto* b : { &clearBtn, &rndBtn, &rampBtn }) { b->colour = col::text(); addAndMakeVisible (b); }
    }

    void rebuild() override { lane = juce::jmin (lane, numLanes() - 1); resized(); repaint(); }

    void tick (bool projectChanged) override
    {
        const int ps = ctx.eng.shared.step[ctx.track].load (std::memory_order_relaxed);
        if (projectChanged || ps != playStep || ctx.uiRev != uiRev)
        {
            playStep = ps; uiRev = ctx.uiRev;
            for (auto* b : { &velBox, &pitchBox, &probBox, &lenBox, &microBox, &ratBox }) b->sync();
            condPicker.sync(); curvePicker.sync();
            for (auto* k : { &ratAmt, &ratPitch, &ratVel, &ratGate }) k->sync();
            onBtn.setLit (sel().on != 0); slideBtn.setLit (sel().slide != 0);
            const bool enabled = ctx.hasStepSelection();
            for (juce::Component* c : std::initializer_list<juce::Component*> { &velBox, &pitchBox, &probBox, &lenBox, &microBox, &ratBox, &condPicker, &curvePicker,
                                                                                 &ratAmt, &ratPitch, &ratVel, &ratGate, &onBtn, &slideBtn })
                c->setAlpha (enabled ? 1.f : 0.35f);
            repaint();
        }
    }

    void resized() override
    {
        const auto lb = laneArea();
        clearBtn.setBounds (lb.getRight() + 10, lb.getY(), 104, 24);
        rndBtn.setBounds (lb.getRight() + 10, lb.getY() + 28, 104, 24);
        rampBtn.setBounds (lb.getRight() + 10, lb.getY() + 56, 104, 24);
        int x = 8; const int y = getHeight() - 74;
        onBtn.setBounds (x, y + 6, 46, 24); slideBtn.setBounds (x, y + 34, 46, 24); x += 54;
        for (auto* b : { &velBox, &pitchBox, &probBox, &lenBox, &microBox }) { b->setBounds (x, y + 8, 58, 44); x += 62; }
        x += 4;
        condPicker.setBounds (x, y + 8, 96, 44); x += 104;
        ratBox.setBounds (x, y + 8, 66, 44); x += 70;
        curvePicker.setBounds (x, y + 8, 88, 44); x += 94;
        for (auto* k : { &ratAmt, &ratPitch, &ratVel, &ratGate }) { k->setBounds (x, y - 2, 60, 66); x += 62; }
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (col::bg());
        const juce::Colour tc = col::track (ctx.track);
        // lane tabs
        const auto tabs = tabArea();
        int x = tabs.getX();
        for (int i = 0; i < numLanes(); ++i)
        {
            const juce::String name = laneName (i);
            const int w = (int) textWidth (font (9.5f, true), name) + 16;
            juce::Rectangle<int> r (x, tabs.getY(), w, tabs.getHeight());
            const bool isLock = i >= numBuiltIn;
            const juce::Colour c = isLock ? col::lock() : (i == LStepMod ? col::mod() : tc);
            fillPanel (g, r, i == lane ? c : col::panelHi(), 2.f);
            drawLabel (g, name, r, i == lane ? col::bg() : (isLock ? col::lock() : col::dim()), 9.5f, juce::Justification::centred, true);
            tabRects[i] = r;
            x += w + 3;
        }
        if (numLanes() == numBuiltIn)
            drawLabel (g, "LOCK LANES APPEAR HERE: RIGHT-CLICK STEPS IN THE GRID, THEN TURN ANY KNOB", { x + 8, tabs.getY(), 520, tabs.getHeight() }, col::faint(), 9.f, juce::Justification::centredLeft);

        // lane
        const auto la = laneArea();
        fillPanel (g, la.expanded (2), col::panel(), 3.f);
        const TrackPattern& tp = ctx.tp();
        const int len = tp.length;
        float x0, cw; cellX (x0, cw);
        const bool bip = isBipolar();
        const float midY = bip ? (float) la.getCentreY() : (float) la.getBottom();
        g.setColour (col::line());
        if (bip) g.fillRect ((float) la.getX(), midY, (float) la.getWidth(), 1.f);
        for (int i = 0; i < len; ++i)
        {
            juce::Rectangle<float> cell (x0 + cw * (float) i + 1.f, (float) la.getY(), cw - 2.f, (float) la.getHeight());
            const Step& s = tp.steps[i];
            if (ctx.stepSelected (ctx.track, i)) { g.setColour (col::text().withAlpha (0.08f)); g.fillRect (cell); }
            if ((i % 4) == 0) { g.setColour (col::line().withAlpha (0.6f)); g.fillRect (cell.getX() - 1.f, cell.getY(), 1.f, cell.getHeight()); }
            bool has = true;
            const float v = laneValue (i, has);
            if (! has) continue;
            const bool active = lane >= numBuiltIn || lane == LStepMod || s.on;
            juce::Colour c = lane >= numBuiltIn ? col::lock() : (lane == LStepMod ? col::mod() : tc);
            c = c.withAlpha (active ? 0.9f : 0.22f);
            g.setColour (c);
            const float y = (float) la.getBottom() - v * (float) la.getHeight();
            if (bip) g.fillRect (cell.getX(), juce::jmin (y, midY), cell.getWidth(), juce::jmax (1.5f, std::abs (y - midY)));
            else g.fillRect (cell.getX(), y, cell.getWidth(), juce::jmax (1.5f, (float) la.getBottom() - y));
            if (cw > 30.f && active)
                drawLabel (g, valueText (i), cell.withHeight (12.f).withY (juce::jmax ((float) la.getY(), juce::jmin (y, midY) - 13.f)).getSmallestIntegerContainer(),
                           col::text().withAlpha (0.85f), 8.5f);
            if (i == playStep) { g.setColour (col::text().withAlpha (0.7f)); g.fillRect (cell.getX(), (float) la.getY() - 3.f, cell.getWidth(), 2.f); }
        }
        drawWrapped (g, laneHelp(), { la.getRight() + 10, la.getY() + 86, getWidth() - la.getRight() - 14, 60 }, col::faint(), 8.5f);
        // inspector caption
        const int iy = getHeight() - 74;
        g.setColour (col::line()); g.fillRect (8, iy - 6, getWidth() - 16, 1);
        juce::String cap = ctx.hasStepSelection() ? "STEP " + juce::String (ctx.selLo() + 1) + (ctx.selHi() != ctx.selLo() ? "-" + juce::String (ctx.selHi() + 1) : juce::String()) : juce::String ("SELECT A STEP");
        drawLabel (g, cap, { getWidth() - 220, iy - 4, 212, 12 }, col::dim(), 9.f, juce::Justification::centredRight, true);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        for (int i = 0; i < numLanes(); ++i)
            if (tabRects[i].contains (e.getPosition())) { lane = i; repaint(); return; }
        if (! laneArea().expanded (0, 4).contains (e.getPosition())) return;
        ctx.sess.beginTransaction ("Draw lane");
        drawing = true; lastDrawStep = -1;
        erase = e.mods.isPopupMenu() || e.mods.isAltDown();
        drawAt (e.getPosition());
    }
    void mouseDrag (const juce::MouseEvent& e) override { if (drawing) drawAt (e.getPosition()); }
    void mouseUp (const juce::MouseEvent&) override { if (drawing) { drawing = false; ctx.sess.endTransaction(); } }
    void mouseMove (const juce::MouseEvent& e) override
    {
        if (laneArea().contains (e.getPosition())) ctx.hoverHelp = juce::String (laneName (lane)) + ": drag to draw" + (lane >= numBuiltIn ? ", right-drag to remove locks" : ", right-drag to reset");
        else ctx.hoverHelp = {};
    }
    void mouseExit (const juce::MouseEvent&) override { ctx.hoverHelp = {}; }

private:
    juce::Rectangle<int> tabArea() const { return { 8, 6, getWidth() - 16, 20 }; }
    juce::Rectangle<int> laneArea() const
    {
        // align with the grid's step columns for the selected track
        const auto sb = getLocalArea (&grid, grid.stepsBounds (ctx.track));
        const int top = 34, bottom = getHeight() - 92;
        return { juce::jmax (8, sb.getX()), top, juce::jmax (100, sb.getWidth()), juce::jmax (40, bottom - top) };
    }
    void cellX (float& x0, float& cw) const
    {
        const auto la = laneArea();
        x0 = (float) la.getX();
        cw = (float) la.getWidth() / (float) grid.columnsFor (ctx.track);
    }
    const Step& sel() const { static const Step none; return ctx.hasStepSelection() ? ctx.tp().steps[ctx.selLo()] : none; }
    void edit (const std::function<void (Step&)>& fn)
    {
        if (! ctx.hasStepSelection()) return;
        TrackPattern p = ctx.tp();
        for (int i = ctx.selLo(); i <= ctx.selHi() && i < p.length; ++i) fn (p.steps[i]);
        ctx.sess.setTrackPattern (ctx.pattern(), ctx.track, p);
    }

    // ---- lanes
    int lockLanes (int* idx = nullptr) const
    {
        int n = 0;
        for (int l = 0; l < kLockLanes; ++l)
            if (ctx.tp().lanes[l].used() && ctx.tp().lanes[l].mask != 0) { if (idx) idx[n] = l; ++n; }
        return n;
    }
    int numLanes() const { return numBuiltIn + lockLanes(); }
    int lockLaneIndex (int laneTab) const { int idx[kLockLanes]; const int n = lockLanes (idx); const int k = laneTab - numBuiltIn; return k >= 0 && k < n ? idx[k] : -1; }
    ParamRef lockRef (int l) const
    {
        const int d = ctx.tp().lanes[l].dest;
        return destIsGlobal (d) ? ParamRef::G (destGlobalIndex (d)) : ParamRef::T (d, ctx.track);
    }
    juce::String laneName (int i) const
    {
        if (i < numBuiltIn) return kLanes[i].name;
        const int l = lockLaneIndex (i);
        return l < 0 ? juce::String ("?") : juce::String (ctx.desc (lockRef (l)).name).toUpperCase();
    }
    juce::String laneHelp() const
    {
        if (lane < numBuiltIn) return kLanes[lane].help;
        return "Parameter lock lane: the value this parameter takes on each locked step. Unlocked steps use the knob's value.";
    }
    bool isBipolar() const
    {
        if (lane < numBuiltIn) return kLanes[lane].bipolar;
        const int l = lockLaneIndex (lane);
        return l >= 0 && ctx.desc (lockRef (l)).isBipolar();
    }
    float laneValue (int i, bool& has) const
    {
        const TrackPattern& tp = ctx.tp();
        const Step& s = tp.steps[i];
        has = true;
        switch (lane)
        {
            case LVel:     return (float) s.vel / 127.f;
            case LPitch:   return juce::jlimit (0.f, 1.f, ((float) s.pitch + 24.f) / 48.f);
            case LProb:    return (float) s.prob / 100.f;
            case LMicro:   return s.micro + 0.5f;
            case LLen:     return juce::jlimit (0.f, 1.f, s.len / 4.f);
            case LRatchet: return (float) (s.ratCount - 1) / 15.f;
            case LStepMod: return (tp.stepMod[i] + 1.f) * 0.5f;
            default:
            {
                const int l = lockLaneIndex (lane);
                if (l < 0 || ! tp.lanes[l].has (i)) { has = false; return 0.f; }
                return tp.lanes[l].value[i];
            }
        }
    }
    juce::String valueText (int i) const
    {
        const TrackPattern& tp = ctx.tp();
        const Step& s = tp.steps[i];
        switch (lane)
        {
            case LVel:     return juce::String ((int) s.vel);
            case LPitch:   return (s.pitch > 0 ? "+" : "") + juce::String ((int) s.pitch);
            case LProb:    return juce::String ((int) s.prob);
            case LMicro:   return juce::String ((int) std::lround (s.micro * 100.f));
            case LLen:     return juce::String (s.len, 2);
            case LRatchet: return s.ratCount > 1 ? "x" + juce::String ((int) s.ratCount) : juce::String();
            case LStepMod: return juce::String (tp.stepMod[i], 2);
            default:
            {
                const int l = lockLaneIndex (lane);
                return l < 0 ? juce::String() : ctx.format (lockRef (l), tp.lanes[l].value[i]);
            }
        }
    }
    void setLaneValue (TrackPattern& tp, int i, float v, bool reset)
    {
        Step& s = tp.steps[i];
        v = juce::jlimit (0.f, 1.f, v);
        switch (lane)
        {
            case LVel:     s.vel = reset ? (uint8_t) 100 : (uint8_t) juce::jlimit (1, 127, (int) std::lround (v * 127.f)); break;
            case LPitch:   s.pitch = reset ? (int8_t) 0 : (int8_t) std::lround (v * 48.f - 24.f); break;
            case LProb:    s.prob = reset ? (uint8_t) 100 : (uint8_t) (std::lround (v * 20.f) * 5); break;
            case LMicro:   s.micro = reset ? 0.f : std::round ((v - 0.5f) * 100.f) / 100.f; break;
            case LLen:     s.len = reset ? 1.f : juce::jmax (0.05f, std::round (v * 4.f * 20.f) / 20.f); break;
            case LRatchet: s.ratCount = reset ? (uint8_t) 1 : (uint8_t) (1 + std::lround (v * 15.f)); break;
            case LStepMod: tp.stepMod[i] = reset ? 0.f : v * 2.f - 1.f; break;
            default:
            {
                const int l = lockLaneIndex (lane);
                if (l < 0) return;
                if (reset) tp.lanes[l].clear (i);
                else
                {
                    const ParamDesc& d = ctx.desc (lockRef (l));
                    tp.lanes[l].set (i, d.isDiscrete() ? d.toNorm (d.toPhys (v)) : v);
                }
            }
        }
    }
    void drawAt (juce::Point<int> p)
    {
        const auto la = laneArea();
        float x0, cw; cellX (x0, cw);
        const int i = (int) (((float) p.x - x0) / cw);
        TrackPattern tp = ctx.tp();
        if (i < 0 || i >= tp.length) return;
        const float v = 1.f - juce::jlimit (0.f, 1.f, (float) (p.y - la.getY()) / (float) la.getHeight());
        // fill skipped steps when dragging fast
        const int a = lastDrawStep < 0 ? i : juce::jmin (i, lastDrawStep), b = lastDrawStep < 0 ? i : juce::jmax (i, lastDrawStep);
        // keep the lock lane we are drawing in alive even if it empties
        const int keepLane = lane;
        for (int k = a; k <= b; ++k) setLaneValue (tp, k, v, erase);
        lastDrawStep = i;
        ctx.sess.setTrackPattern (ctx.pattern(), ctx.track, tp);
        lane = juce::jmin (keepLane, numLanes() - 1);
        ctx.hoverHelp = juce::String (laneName (lane)) + "  step " + juce::String (i + 1) + "  " + valueText (i);
        repaint();
    }
    void clearLane()
    {
        TrackPattern tp = ctx.tp();
        if (lane >= numBuiltIn) { const int l = lockLaneIndex (lane); if (l >= 0) tp.lanes[l] = LockLane{}; lane = 0; }
        else for (int i = 0; i < kMaxSteps; ++i) setLaneValue (tp, i, 0.f, true);
        Session::Transaction tx (ctx.sess, "Clear lane");
        ctx.sess.setTrackPattern (ctx.pattern(), ctx.track, tp);
    }
    void randomiseLane()
    {
        TrackPattern tp = ctx.tp();
        Rng rng (hashN (tp.seed, (uint32_t) lane, ++rndCount));
        const int lo = ctx.hasStepSelection() ? ctx.selLo() : 0, hi = ctx.hasStepSelection() ? ctx.selHi() : tp.length - 1;
        for (int i = lo; i <= hi && i < tp.length; ++i)
        {
            if (lane >= numBuiltIn && ! tp.lanes[juce::jmax (0, lockLaneIndex (lane))].has (i) && ! tp.steps[i].on) continue;
            float v = rng.uni();
            if (lane == LPitch) v = 0.5f + (float) rng.irange (-7, 7) / 48.f;
            if (lane == LRatchet) v = rng.chance (0.7f) ? 0.f : (float) rng.irange (1, 5) / 15.f;
            if (lane == LMicro) v = 0.5f + rng.bip() * 0.2f;
            if (lane == LVel) v = 0.35f + 0.65f * v;
            setLaneValue (tp, i, v, false);
        }
        Session::Transaction tx (ctx.sess, "Randomise lane");
        ctx.sess.setTrackPattern (ctx.pattern(), ctx.track, tp);
    }
    void rampLane()
    {
        TrackPattern tp = ctx.tp();
        const int lo = ctx.hasStepSelection() && ctx.selHi() > ctx.selLo() ? ctx.selLo() : 0;
        const int hi = ctx.hasStepSelection() && ctx.selHi() > ctx.selLo() ? ctx.selHi() : tp.length - 1;
        bool h1, h2;
        const int keep = lane;
        const float a = laneValue (lo, h1), b = laneValue (hi, h2);
        for (int i = lo; i <= hi; ++i) setLaneValue (tp, i, a + (b - a) * (float) (i - lo) / (float) juce::jmax (1, hi - lo), false);
        Session::Transaction tx (ctx.sess, "Ramp lane");
        ctx.sess.setTrackPattern (ctx.pattern(), ctx.track, tp);
        lane = keep;
    }

    StepGrid& grid;
    NumberBox velBox, pitchBox, probBox, lenBox, microBox, ratBox;
    Picker condPicker, curvePicker;
    ValueKnob ratAmt, ratPitch, ratVel, ratGate;
    Btn onBtn { "ON" }, slideBtn { "SLIDE" }, clearBtn { "CLEAR LANE" }, rndBtn { "RANDOMISE" }, rampBtn { "RAMP" };
    juce::Rectangle<int> tabRects[numBuiltIn + kLockLanes];
    int lane = LVel, playStep = -1, lastDrawStep = -1;
    uint32_t uiRev = 0, rndCount = 0;
    bool drawing = false, erase = false;
};

} // namespace

std::unique_ptr<Page> makeStepsPage (Ctx& c, StepGrid& g) { return std::make_unique<StepsPage> (c, g); }

} // namespace ui
