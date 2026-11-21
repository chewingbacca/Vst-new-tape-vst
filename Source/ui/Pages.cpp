#include "Pages.h"
#include "plugin/PatternBank.h"

using namespace juce;
using namespace splinter;

namespace
{
void showMenu (Component* target, const StringArray& items, int current, std::function<void (int)> onPick)
{
    PopupMenu m;
    for (int i = 0; i < items.size(); ++i) m.addItem (i + 1, items[i], true, i == current);
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (target), [onPick] (int r) { if (r > 0) onPick (r - 1); });
}

void sectionTitle (Graphics& g, const String& s, Rectangle<int> r, Colour c = theme::textDim)
{
    theme::drawLabel (g, s, r, c, 10.0f, Justification::centredLeft);
    g.setColour (theme::outline);
    g.drawHorizontalLine (r.getBottom(), (float) r.getX(), (float) r.getRight());
}
} // namespace

// ================================================================ SlotBank
SlotBank::SlotBank (SplinterProcessor& p, Kind k) : proc (p), kind (k) { setMouseCursor (MouseCursor::PointingHandCursor); }

Rectangle<float> SlotBank::pad (int i) const
{
    const float labelW = 62.0f;
    const float w = ((float) getWidth() - labelW) / (float) kNumSlots;
    return Rectangle<float> (labelW + w * (float) i, 0.0f, w, (float) getHeight()).reduced (2.0f, 2.0f);
}

void SlotBank::tick()
{
    bool changed = false;
    if (proc.getBankVersion() != bankVersion) { bankVersion = proc.getBankVersion(); bank = proc.getBank(); changed = true; }
    auto& v = proc.engine.visual;
    const int c = kind == timeKind ? v.timeSlot.load() : (kind == volKind ? v.volSlot.load() : v.seqSlot.load());
    const int p = kind == timeKind ? v.pendingTimeSlot.load() : (kind == volKind ? v.pendingVolSlot.load() : v.pendingSeqSlot.load());
    if (c != current || p != pending) changed = true;
    current = c; pending = p;
    if (pending >= 0) { ++blink; changed = true; }
    if (changed) repaint();
}

void SlotBank::paint (Graphics& g)
{
    const Colour c = colour();
    const char* names[3] = { "TIME", "VOLUME", "SEQUENCE" };
    const char* keys[3] = { "C1-B1", "C2-B2", "C3-B3" };
    theme::drawLabel (g, names[kind], Rectangle<int> (0, 8, 60, 14), c, 10.5f, Justification::centredLeft);
    theme::drawLabel (g, keys[kind], Rectangle<int> (0, 24, 60, 12), theme::textFaint, 9.0f, Justification::centredLeft, false);
    for (int i = 0; i < kNumSlots; ++i)
    {
        const auto r = pad (i);
        const bool on = i == current, pend = i == pending && ((blink / 4) & 1);
        const auto shape = theme::chamfer (r, 6.0f);
        g.setColour (on ? c.withAlpha (0.92f) : theme::panelHi); g.fillPath (shape);
        g.setColour (pend ? theme::white : (on ? c : theme::outline)); g.strokePath (shape, PathStrokeType (pend ? 2.0f : 1.0f));
        const Colour ink = on ? theme::bg : c.withAlpha (0.85f);
        const auto thumb = r.reduced (6.0f, 5.0f).withTrimmedBottom (13.0f);
        String name;
        if (kind == seqKind)
        {
            const SeqPattern& sp = bank.seq[i];
            name = sp.name;
            const float cw = thumb.getWidth() / (float) jmax (1, sp.numSteps), ch = thumb.getHeight() / (float) kNumLanes;
            for (int l = 0; l < kNumLanes; ++l)
                for (int s = 0; s < sp.numSteps; ++s)
                    if (sp.steps[l][s].on)
                    {
                        g.setColour (on ? theme::bg : theme::laneColour (l));
                        g.fillRect (Rectangle<float> (thumb.getX() + cw * (float) s, thumb.getY() + ch * (float) l, jmax (1.0f, cw - 0.5f), jmax (1.0f, ch - 0.5f)));
                    }
        }
        else
        {
            const Curve& cv = kind == timeKind ? bank.time[i] : bank.vol[i];
            name = cv.name;
            CurveEditor::drawThumb (g, thumb, cv, kind == timeKind, ink);
        }
        g.setColour (on ? theme::bg : theme::textDim); g.setFont (theme::font (9.0f, true));
        g.drawFittedText (String (i + 1) + " " + name, r.reduced (4.0f, 1.0f).removeFromBottom (12.0f).toNearestInt(), Justification::centredLeft, 1, 0.75f);
    }
}

void SlotBank::mouseDown (const MouseEvent& e)
{
    for (int i = 0; i < kNumSlots; ++i)
        if (pad (i).contains (e.position))
            proc.setParam (kind == timeKind ? pid::timeSlot : (kind == volKind ? pid::volSlot : pid::seqSlot), (float) (i + 1));
}

// ================================================================ PerformPage
PerformPage::PerformPage (SplinterProcessor& p) : Page (p), time (p, SlotBank::timeKind), vol (p, SlotBank::volKind), seq (p, SlotBank::seqKind)
{
    addAndMakeVisible (time); addAndMakeVisible (vol); addAndMakeVisible (seq);
    time.setTooltip ("Time curve slots. Click to switch (quantised); in Patterns MIDI mode keys C1-B1 select them.");
    vol.setTooltip ("Volume curve slots. Keys C2-B2 in Patterns MIDI mode.");
    seq.setTooltip ("Glitch sequence slots. Keys C3-B3 in Patterns MIDI mode.");
    quant = add<ChoiceBox> (proc.apvts, pid::quantize, "SWITCH QUANTISE", theme::white);
    midi = add<ChoiceBox> (proc.apvts, pid::midiMode, "MIDI NOTES PLAY", theme::white);
    trig = add<ChoiceBox> (proc.apvts, pid::sliceTrig, "SLICE KEYS", theme::pink);
    loopPlay = add<ToggleBox> (proc.apvts, pid::loopPlay, "LOOP PLAYS", theme::lime);
    seed = add<Knob> (proc.apvts, pid::seed, "SEED", theme::red);
    generate = add<TextBtn> ("GENERATE PATTERN", theme::red);
    newSeed = add<TextBtn> ("NEW SEED", theme::red);
    generate->setTooltip ("Writes a new, musically placed pattern into the current sequence slot. Locked lanes are kept.");
    generate->onClick = [this]
    {
        const int slot = jlimit (0, kNumSlots - 1, (int) proc.getParam (pid::seqSlot) - 1);
        proc.generatePattern (slot, (uint32_t) proc.getParam (pid::seed) * 7919U + (uint32_t) Time::getMillisecondCounter());
    };
    newSeed->onClick = [this] { proc.setParam (pid::seed, (float) (1 + Random::getSystemRandom().nextInt (999))); };
}

Rectangle<float> PerformPage::fxPad (int lane) const
{
    const float x0 = 62.0f, w = (758.0f - x0) / (float) kNumLanes;
    return Rectangle<float> (x0 + w * (float) lane, 212.0f, w, 70.0f).reduced (2.0f, 2.0f);
}

void PerformPage::resized()
{
    time.setBounds (0, 2, 758, 64); vol.setBounds (0, 70, 758, 64); seq.setBounds (0, 138, 758, 64);
    const int x = 776, w = getWidth() - x - 4;
    quant->setBounds (x, 4, w / 2 - 4, 40); midi->setBounds (x + w / 2, 4, w / 2, 40);
    trig->setBounds (x, 52, w / 2 - 4, 40); loopPlay->setBounds (x + w / 2, 66, w / 2, 26);
    seed->setBounds (x, 118, 70, 84);
    generate->setBounds (x + 78, 126, w - 78, 30); newSeed->setBounds (x + 78, 162, w - 78, 26);
}

void PerformPage::paint (Graphics& g)
{
    theme::drawLabel (g, "HOLD FX", Rectangle<int> (0, 220, 60, 14), theme::white, 10.5f, Justification::centredLeft);
    theme::drawLabel (g, "C4-C5", Rectangle<int> (0, 236, 60, 12), theme::textFaint, 9.0f, Justification::centredLeft, false);
    for (int l = 0; l < kNumLanes; ++l)
    {
        const auto r = fxPad (l);
        const Colour c = theme::laneColour (l);
        const bool down = l == heldPad;
        const auto shape = theme::chamfer (r, 8.0f);
        g.setColour (down ? c : c.withAlpha (0.10f + 0.6f * glow[l])); g.fillPath (shape);
        g.setColour (c.withAlpha (down ? 1.0f : 0.7f)); g.strokePath (shape, PathStrokeType (1.2f));
        g.setColour (down || glow[l] > 0.6f ? theme::bg : c.interpolatedWith (theme::text, 0.3f)); g.setFont (theme::font (9.5f, true));
        g.drawFittedText (String (laneName (l)).toUpperCase(), r.reduced (2.0f).toNearestInt(), Justification::centred, 2, 0.7f);
    }
    sectionTitle (g, "CHAOS", Rectangle<int> (776, 100, getWidth() - 780, 14), theme::red);
    g.setColour (theme::textFaint); g.setFont (theme::font (9.5f));
    g.drawFittedText ("Hold a pad to fire that effect right now. Chaos adds seeded variation to unlocked lanes: the same seed always plays the same way.",
                      Rectangle<int> (776, 214, getWidth() - 780, 66), Justification::topLeft, 5, 0.9f);
}

void PerformPage::setHeld (int lane)
{
    if (lane == heldPad) return;
    heldPad = lane;
    proc.uiHeldLanes.store (lane >= 0 ? (1u << lane) : 0u);
    repaint();
}

void PerformPage::mouseDown (const MouseEvent& e) { for (int l = 0; l < kNumLanes; ++l) if (fxPad (l).contains (e.position)) setHeld (l); }
void PerformPage::mouseDrag (const MouseEvent& e)
{
    if (heldPad < 0) return;
    int hit = -1;
    for (int l = 0; l < kNumLanes; ++l) if (fxPad (l).contains (e.position)) hit = l;
    if (hit >= 0) setHeld (hit);                       // slide across pads
}
void PerformPage::mouseUp (const MouseEvent&) { setHeld (-1); }

void PerformPage::tick()
{
    time.tick(); vol.tick(); seq.tick();
    bool changed = false;
    for (int l = 0; l < kNumLanes; ++l)
    {
        const float lvl = proc.engine.visual.laneLevel[l].load (std::memory_order_relaxed);
        const float gl = lvl > 0.5f ? 1.0f : glow[l] * 0.8f;
        if (std::fabs (gl - glow[l]) > 0.01f) changed = true;
        glow[l] = gl < 0.01f ? 0.0f : gl;
    }
    if (changed) repaint (0, 206, 760, 82);
}

// ================================================================ CurvesPage
CurvesPage::CurvesPage (SplinterProcessor& p) : Page (p), timeEd (p, true), volEd (p, false)
{
    addAndMakeVisible (timeEd); addChildComponent (volEd);
    tabTime = add<TextBtn> ("TIME", theme::lime); tabVol = add<TextBtn> ("VOLUME", theme::cyan);
    prev = add<TextBtn> ("<", theme::white); next = add<TextBtn> (">", theme::white); slotName = add<TextBtn> ("", theme::white);
    gridBtn = add<TextBtn> ("", theme::white); pencil = add<TextBtn> ("PENCIL", theme::amber);
    lengthBtn = add<TextBtn> ("", theme::white); rangeBtn = add<TextBtn> ("", theme::lime); shapes = add<TextBtn> ("INSERT SHAPE", theme::white);
    depthTime = add<Knob> (proc.apvts, pid::warp, "WARP", theme::lime); smoothTime = add<Knob> (proc.apvts, pid::declick, "EDGE FADE", theme::lime);
    depthVol = add<Knob> (proc.apvts, pid::volDepth, "DEPTH", theme::cyan); smoothVol = add<Knob> (proc.apvts, pid::volSmooth, "SMOOTH", theme::cyan);
    timeEd.setTooltip ("Time curve: height = how far behind real time playback sits. Flat = normal speed, a step down = jump back (repeat), a slope = slow down; steeper than the dashed line = reverse. "
                       "Click to add a point, drag to move, double-click to delete, alt-drag a segment to bend it, right-click for hold/step. Shift = no snapping.");
    volEd.setTooltip ("Volume curve: gain across the bar. Click to add a point, drag to move, double-click to delete, alt-drag a segment to bend it. Use the pencil to paint steps.");
    pencil->setTooltip ("Pencil: drag across the editor to paint one flat step per grid cell - the fast way to draw gates and stutters.");
    tabTime->onClick = [this] { setShowTime (true); };
    tabVol->onClick = [this] { setShowTime (false); };
    auto stepSlot = [this] (int d)
    {
        const char* id = showTime ? pid::timeSlot : pid::volSlot;
        proc.setParam (id, (float) (1 + floorModI ((int) proc.getParam (id) - 1 + d, kNumSlots)));
    };
    prev->onClick = [stepSlot] { stepSlot (-1); };
    next->onClick = [stepSlot] { stepSlot (1); };
    slotName->onClick = [this]
    {
        const PatternBank b = proc.getBank();
        StringArray names;
        for (int i = 0; i < kNumSlots; ++i) names.add (String (i + 1) + "  " + String (showTime ? b.time[i].name : b.vol[i].name));
        showMenu (slotName, names, ed().getSlot(), [this] (int i) { proc.setParam (showTime ? pid::timeSlot : pid::volSlot, (float) (i + 1)); });
    };
    gridBtn->onClick = [this]
    {
        static const int divs[6] = { 4, 8, 16, 32, 12, 24 };
        showMenu (gridBtn, { "1/4", "1/8", "1/16", "1/32", "1/8 triplet", "1/16 triplet" }, -1, [this] (int i) { timeEd.setGrid (divs[i]); volEd.setGrid (divs[i]); refreshButtons(); });
    };
    pencil->onClick = [this] { const bool b = ! ed().isPencil(); timeEd.setPencil (b); volEd.setPencil (b); refreshButtons(); };
    lengthBtn->onClick = [this]
    {
        static const float beats[4] = { 2.0f, 4.0f, 8.0f, 16.0f };
        showMenu (lengthBtn, { "1/2 bar", "1 bar", "2 bars", "4 bars" }, -1, [this] (int i) { ed().modify ([i] (Curve& c) { c.lengthBeats = beats[i]; }); refreshButtons(); });
    };
    rangeBtn->onClick = [this]
    {
        static const float beats[5] = { 1.0f, 2.0f, 4.0f, 8.0f, 16.0f };
        showMenu (rangeBtn, { "1 beat", "2 beats", "1 bar", "2 bars", "4 bars" }, -1, [this] (int i)
        {
            // keep the drawn offsets (in beats) the same where they still fit
            timeEd.modify ([i] (Curve& c) { const float k = c.rangeBeats / beats[i]; for (int n = 0; n < c.numNodes; ++n) c.nodes[n].y = jlimit (0.0f, 1.0f, c.nodes[n].y * k); c.rangeBeats = beats[i]; });
            refreshButtons();
        });
    };
    shapes->onClick = [this]
    {
        const StringArray items = showTime ? StringArray { "Straight", "Half-time", "Half-time per 2 beats", "Tape stop on the last beat", "Rewind the second half", "Repeat every grid cell",
                                                            "Repeat the first beat", "Reverse the bar", "Stutter build (last 2 beats)" }
                                           : StringArray { "Open", "Gate on the grid (50 %)", "Gate on the grid (25 %)", "Pump per beat", "Offbeats only", "Fade out", "Swell in", "Random gate" };
        showMenu (shapes, items, -1, [this] (int i) { insertShape (i); });
    };
    refreshButtons();
}

void CurvesPage::setShowTime (bool b)
{
    showTime = b; timeEd.setVisible (b); volEd.setVisible (! b);
    depthTime->setVisible (b); smoothTime->setVisible (b); depthVol->setVisible (! b); smoothVol->setVisible (! b); rangeBtn->setVisible (b);
    lastSlot = -1; refreshButtons(); repaint();
}

void CurvesPage::insertShape (int id)
{
    const int grid = ed().getGrid();
    ed().modify ([this, id, grid] (Curve& c)
    {
        const float len = c.lengthBeats, range = c.rangeBeats;
        c.clear();
        auto beatsToY = [range] (float beats) { return jlimit (0.0f, 1.0f, beats / range); };
        if (showTime)
        {
            switch (id)
            {
                case 0: c.add (0.0f, 0.0f); c.add (1.0f, 0.0f); c.setName ("Straight"); break;
                case 1: c.add (0.0f, 0.0f); c.add (1.0f, beatsToY (len * 0.5f)); c.setName ("Half-Time"); break;
                case 2: for (float x = 0.0f; x < 0.999f; x += 2.0f / len) { c.add (x, 0.0f); c.add (jmin (1.0f, x + 2.0f / len), beatsToY (1.0f)); } c.setName ("Half x2"); break;
                case 3: { const float x0 = 1.0f - 1.0f / len; c.add (0.0f, 0.0f); c.add (x0, 0.0f, 0.3333f); c.add (1.0f, beatsToY (0.5f)); c.setName ("Tape Stop"); break; }
                case 4: c.add (0.0f, 0.0f); c.add (0.5f, 0.0f); c.add (1.0f, beatsToY (len)); c.setName ("Rewind"); break;
                case 5: for (int i = 0; i < jmin (grid, kMaxNodes / 2); ++i) { const float x = (float) i / (float) grid; c.add (x, (i & 1) ? beatsToY (len / (float) grid) : 0.0f, 0.0f, true); } c.setName ("Repeats"); break;
                case 6: for (int i = 0; i < (int) len; ++i) c.add ((float) i / len, beatsToY ((float) i), 0.0f, true); c.setName ("Beat Hold"); break;
                case 7: c.add (0.0f, 0.0f); c.add (1.0f, beatsToY (len * 2.0f)); c.setName ("Reverse"); break;
                default:
                {
                    c.add (0.0f, 0.0f, 0.0f, true);
                    const float b = 1.0f / len;                        // one beat in x
                    c.add (1.0f - 2.0f * b, 0.0f, 0.0f, true); c.add (1.0f - 1.5f * b, beatsToY (0.5f), 0.0f, true);
                    for (int i = 0; i < 2; ++i) c.add (1.0f - b + 0.25f * b * (float) i, beatsToY (1.0f + 0.25f * (float) i), 0.0f, true);
                    for (int i = 0; i < 4; ++i) c.add (1.0f - 0.5f * b + 0.125f * b * (float) i, beatsToY (1.5f + 0.125f * (float) i), 0.0f, true);
                    c.setName ("Build Up");
                }
            }
        }
        else
        {
            switch (id)
            {
                case 0: c.add (0.0f, 1.0f); c.add (1.0f, 1.0f); c.setName ("Open"); break;
                case 1: case 2:
                    for (int i = 0; i < jmin (grid, kMaxNodes / 2); ++i) { const float x = (float) i / (float) grid; c.add (x, 1.0f, 0.0f, true); c.add (x + (id == 1 ? 0.5f : 0.25f) / (float) grid, 0.0f, 0.0f, true); }
                    c.setName (id == 1 ? "Gate" : "Tight Gate"); break;
                case 3: for (int b = 0; b < (int) len; ++b) { const float x = (float) b / len; c.add (x, 0.08f, -0.55f); c.add (x + 0.8f / len, 1.0f); c.add (x + 0.999f / len, 1.0f); } c.setName ("Pump"); break;
                case 4: for (int b = 0; b < (int) len; ++b) { const float x = (float) b / len; c.add (x, 0.0f, 0.0f, true); c.add (x + 0.5f / len, 1.0f, 0.0f, true); } c.setName ("Offbeat"); break;
                case 5: c.add (0.0f, 1.0f, 0.3f); c.add (1.0f, 0.0f); c.setName ("Fade Out"); break;
                case 6: c.add (0.0f, 0.0f, 0.45f); c.add (1.0f, 1.0f); c.setName ("Swell"); break;
                default:
                {
                    Random rnd;
                    for (int i = 0; i < jmin (grid, kMaxNodes); ++i) c.add ((float) i / (float) grid, (i == 0 || rnd.nextFloat() < 0.62f) ? 1.0f : 0.0f, 0.0f, true);
                    c.setName ("Random Gate");
                }
            }
        }
    });
    refreshButtons();
}

void CurvesPage::refreshButtons()
{
    tabTime->setLit (showTime); tabVol->setLit (! showTime);
    const Curve& c = ed().getCurve();
    slotName->setText (String (ed().getSlot() + 1) + "  " + String (c.name));
    const int gd = ed().getGrid();
    gridBtn->setText ("GRID  " + String (gd == 4 ? "1/4" : gd == 8 ? "1/8" : gd == 16 ? "1/16" : gd == 32 ? "1/32" : gd == 12 ? "1/8T" : "1/16T"));
    pencil->setLit (ed().isPencil());
    lengthBtn->setText ("LENGTH  " + (c.lengthBeats < 4.0f ? String ("1/2 bar") : String (roundToInt (c.lengthBeats / 4.0f)) + (c.lengthBeats > 4.0f ? " bars" : " bar")));
    rangeBtn->setText ("DEPTH RANGE  " + (c.rangeBeats < 4.0f ? String (roundToInt (c.rangeBeats)) + (c.rangeBeats > 1.0f ? " beats" : " beat") : String (roundToInt (c.rangeBeats / 4.0f)) + (c.rangeBeats > 4.0f ? " bars" : " bar")));
}

void CurvesPage::resized()
{
    const int w = 170;
    tabTime->setBounds (0, 2, w / 2 - 2, 26); tabVol->setBounds (w / 2 + 2, 2, w / 2 - 2, 26);
    prev->setBounds (0, 34, 22, 24); slotName->setBounds (26, 34, w - 52, 24); next->setBounds (w - 22, 34, 22, 24);
    gridBtn->setBounds (0, 64, w / 2 + 12, 24); pencil->setBounds (w / 2 + 16, 64, w / 2 - 16, 24);
    lengthBtn->setBounds (0, 94, w, 24); rangeBtn->setBounds (0, 124, w, 24); shapes->setBounds (0, 154, w, 24);
    for (auto* k : { depthTime, depthVol }) k->setBounds (6, 190, 74, 92);
    for (auto* k : { smoothTime, smoothVol }) k->setBounds (90, 190, 74, 92);
    const Rectangle<int> area (w + 10, 0, getWidth() - w - 12, getHeight());
    timeEd.setBounds (area); volEd.setBounds (area);
    setShowTime (showTime);
}

void CurvesPage::paint (Graphics&) {}

void CurvesPage::tick()
{
    const int slot = jlimit (0, kNumSlots - 1, (int) proc.getParam (showTime ? pid::timeSlot : pid::volSlot) - 1);
    timeEd.setSlot (jlimit (0, kNumSlots - 1, (int) proc.getParam (pid::timeSlot) - 1));
    volEd.setSlot (jlimit (0, kNumSlots - 1, (int) proc.getParam (pid::volSlot) - 1));
    ed().tick();
    if (slot != lastSlot || proc.getBankVersion() != lastVersion) { lastSlot = slot; lastVersion = proc.getBankVersion(); refreshButtons(); }
}

// ================================================================ SequencePage
SequencePage::SequencePage (SplinterProcessor& p) : Page (p), grid (p)
{
    addAndMakeVisible (grid);
    grid.setTooltip ("Click a cell to switch a step on or off; drag sideways to paint a run (joined steps play as one gesture). Drag up/down on a lit step for probability. "
                     "Mouse wheel or right-click chooses the step's variation. Click a lane name for its settings; the padlock protects a lane from Chaos and Generate.");
    prev = add<TextBtn> ("<", theme::white); next = add<TextBtn> (">", theme::white); slotName = add<TextBtn> ("", theme::pink);
    stepsBtn = add<TextBtn> ("", theme::white); sizeBtn = add<TextBtn> ("", theme::white);
    generate = add<TextBtn> ("GENERATE", theme::red); clear = add<TextBtn> ("CLEAR", theme::white); copyBtn = add<TextBtn> ("COPY TO NEXT", theme::white);
    auto stepSlot = [this] (int d) { proc.setParam (pid::seqSlot, (float) (1 + floorModI ((int) proc.getParam (pid::seqSlot) - 1 + d, kNumSlots))); };
    prev->onClick = [stepSlot] { stepSlot (-1); };
    next->onClick = [stepSlot] { stepSlot (1); };
    slotName->onClick = [this]
    {
        const PatternBank b = proc.getBank(); StringArray names;
        for (int i = 0; i < kNumSlots; ++i) names.add (String (i + 1) + "  " + String (b.seq[i].name));
        showMenu (slotName, names, grid.getSlot(), [this] (int i) { proc.setParam (pid::seqSlot, (float) (i + 1)); });
    };
    stepsBtn->onClick = [this]
    {
        static const int n[5] = { 8, 12, 16, 24, 32 };
        showMenu (stepsBtn, { "8 steps", "12 steps", "16 steps", "24 steps", "32 steps" }, -1, [this] (int i)
        { const int s = grid.getSlot(); proc.editBank ([&] (PatternBank& b) { b.seq[s].numSteps = n[i]; }, SplinterProcessor::seqBit (s)); });
    };
    sizeBtn->onClick = [this]
    {
        static const int spb[5] = { 2, 4, 8, 3, 6 };
        showMenu (sizeBtn, { "1/8 steps", "1/16 steps", "1/32 steps", "1/8 triplet steps", "1/16 triplet steps" }, -1, [this] (int i)
        { const int s = grid.getSlot(); proc.editBank ([&] (PatternBank& b) { b.seq[s].stepsPerBeat = spb[i]; }, SplinterProcessor::seqBit (s)); });
    };
    generate->setTooltip ("Writes a new, musically placed pattern into this slot (locked lanes are kept).");
    generate->onClick = [this] { proc.generatePattern (grid.getSlot(), (uint32_t) proc.getParam (pid::seed) * 7919U + (uint32_t) Time::getMillisecondCounter()); };
    clear->onClick = [this] { const int s = grid.getSlot(); proc.editBank ([&] (PatternBank& b) { for (int l = 0; l < kNumLanes; ++l) if (! b.seq[s].locked[l]) for (auto& st : b.seq[s].steps[l]) st = Step(); }, SplinterProcessor::seqBit (s)); };
    copyBtn->onClick = [this]
    {
        const int s = grid.getSlot(), d = (s + 1) % kNumSlots;
        proc.editBank ([&] (PatternBank& b) { const String nm (b.seq[d].name); b.seq[d] = b.seq[s]; b.seq[d].setName (nm.toRawUTF8()); }, SplinterProcessor::seqBit (d));
    };

    // per-lane settings
    laneWidgets.resize ((size_t) kNumLanes);
    auto K = [this] (int lane, const char* id, const char* label) { auto* k = add<Knob> (proc.apvts, id, label, theme::laneColour (lane)); laneWidgets[(size_t) lane].push_back (k); return k; };
    auto C = [this] (int lane, const char* id, const char* label) { auto* c = add<ChoiceBox> (proc.apvts, id, label, theme::laneColour (lane)); laneWidgets[(size_t) lane].push_back (c); };
    K (laneChop, pid::chop, "CHOP AMOUNT"); K (laneChop, pid::declick, "EDGE FADE");
    C (laneStutter, pid::stutterDiv, "DIVISION"); C (laneStutter, pid::stutterArp, "ARPEGGIO");
    K (laneStutter, pid::stutterPitch, "PITCH RAMP")->setBipolar (true); K (laneStutter, pid::stutterGate, "GATE"); K (laneStutter, pid::stutterDecay, "DECAY"); K (laneStutter, pid::stutterAccel, "ACCEL")->setBipolar (true);
    K (laneReverse, pid::warp, "WARP AMOUNT");
    K (laneTapeStop, pid::tapeTime, "STOP TIME");
    K (lanePitch, pid::pitchDegrees, "SCALE STEPS")->setBipolar (true); K (lanePitch, pid::pitchGlide, "GLIDE");
    K (laneCrush, pid::crushBits, "BITS"); K (laneCrush, pid::crushRate, "RATE");
    K (laneComb, pid::combDegree, "NOTE"); K (laneComb, pid::combOctave, "OCTAVE"); K (laneComb, pid::combFeedback, "FEEDBACK"); K (laneComb, pid::combDamp, "DAMPING");
    K (laneRing, pid::ringDegree, "NOTE"); K (laneRing, pid::ringOctave, "OCTAVE");
    C (laneFilter, pid::filterShape, "SWEEP"); K (laneFilter, pid::filterReso, "RESONANCE");
    K (laneFreeze, pid::freezeMs, "GRAIN SIZE");
    C (laneDelay, pid::delayDiv, "TIME"); K (laneDelay, pid::delayFeedback, "FEEDBACK"); K (laneDelay, pid::delayTone, "TONE"); K (laneDelay, pid::delayLevel, "LEVEL"); K (laneDelay, pid::delayPingPong, "PING-PONG");
    K (laneReverb, pid::reverbTime, "TIME"); K (laneReverb, pid::reverbDamp, "DAMPING"); K (laneReverb, pid::reverbLevel, "LEVEL");
    C (laneHarmony, pid::harmGate, "VOICES SOUND"); K (laneHarmony, pid::harmony, "HARMONY");
    grid.onLaneSelected = [this] (int lane) { showLane (lane); };
}

void SequencePage::showLane (int lane)
{
    shownLane = lane;
    for (int l = 0; l < kNumLanes; ++l) for (auto* c : laneWidgets[(size_t) l]) c->setVisible (l == lane);
    // choices on the first row(s), knobs below in a grid of three
    int y = panelArea.getY() + 24, col = 0;
    const int x0 = panelArea.getX() + 6, cw = (panelArea.getWidth() - 12) / 3;
    std::vector<Component*> knobs;
    for (auto* c : laneWidgets[(size_t) lane])
    {
        if (dynamic_cast<ChoiceBox*> (c) != nullptr) { c->setBounds (x0 + col * (panelArea.getWidth() - 12) / 2, y, (panelArea.getWidth() - 12) / 2 - 6, 38); if (++col == 2) { col = 0; y += 44; } }
        else knobs.push_back (c);
    }
    if (col != 0) y += 44;
    col = 0;
    for (auto* k : knobs) { k->setBounds (x0 + col * cw, y, cw - 4, 78); if (++col == 3) { col = 0; y += 82; } }
    repaint();
}

void SequencePage::refreshButtons()
{
    const PatternBank b = proc.getBank();
    const SeqPattern& p = b.seq[grid.getSlot()];
    slotName->setText (String (grid.getSlot() + 1) + "  " + String (p.name));
    stepsBtn->setText (String (p.numSteps) + " STEPS");
    sizeBtn->setText (p.stepsPerBeat == 2 ? "1/8" : p.stepsPerBeat == 4 ? "1/16" : p.stepsPerBeat == 8 ? "1/32" : p.stepsPerBeat == 3 ? "1/8T" : "1/16T");
}

void SequencePage::resized()
{
    const int gw = 772;
    prev->setBounds (0, 2, 22, 24); slotName->setBounds (26, 2, 150, 24); next->setBounds (180, 2, 22, 24);
    stepsBtn->setBounds (214, 2, 84, 24); sizeBtn->setBounds (302, 2, 60, 24);
    generate->setBounds (gw - 286, 2, 90, 24); copyBtn->setBounds (gw - 190, 2, 110, 24); clear->setBounds (gw - 74, 2, 74, 24);
    grid.setBounds (0, 32, gw, getHeight() - 34);
    panelArea = Rectangle<int> (gw + 10, 2, getWidth() - gw - 12, getHeight() - 4);
    showLane (shownLane < 0 ? grid.getSelectedLane() : shownLane);
}

void SequencePage::paint (Graphics& g)
{
    theme::drawPanel (g, panelArea.toFloat(), theme::panel, 10.0f);
    const int lane = jmax (0, shownLane);
    theme::drawLabel (g, String (laneName (lane)).toUpperCase() + " SETTINGS", panelArea.withHeight (20).reduced (10, 0).translated (0, 3), theme::laneColour (lane), 11.0f, Justification::centredLeft);
    static const char* hints[kNumLanes] = {
        "Plays another slice in place of this step. '?' picks one at random each time, favouring slices on the same part of the beat. The Chop macro sets how often steps fire.",
        "Repeats the start of the step. Pitch ramp bends successive repeats; Arpeggio steps them through the scale. Accel shortens (or lengthens) each repeat.",
        "Plays the run backwards. The Warp macro sets how often it fires.",
        "Slows the tape to a halt across the run. Variations: spin back up, or a half-speed drag.",
        "Re-pitches the run tape-style by scale steps; Glide bends into it.",
        "Bit depth and sample-rate reduction.",
        "A resonator tuned to a note of the chosen key and scale.",
        "Ring modulation against a carrier tuned to the key.",
        "A filter sweep across the run.",
        "Grabs a grain of audio and sustains it.",
        "Sends this step into a tempo-synced delay; the tail keeps ringing.",
        "Sends this step into a reverb; the tail keeps ringing.",
        "With 'Sequenced', harmony voices only sound on these steps. Variations move the whole chord by scale steps." };
    g.setColour (theme::textFaint); g.setFont (theme::font (9.5f));
    g.drawFittedText (hints[lane], panelArea.reduced (10, 6).removeFromBottom (54), Justification::bottomLeft, 4, 0.9f);
}

void SequencePage::tick()
{
    const int slot = jlimit (0, kNumSlots - 1, (int) proc.getParam (pid::seqSlot) - 1);
    grid.setSlot (slot);
    grid.tick();
    if (slot != lastSlot || proc.getBankVersion() != lastVersion) { lastSlot = slot; lastVersion = proc.getBankVersion(); refreshButtons(); }
}

// ================================================================ HarmonyPage
HarmonyPage::HarmonyPage (SplinterProcessor& p) : Page (p)
{
    key = add<ChoiceBox> (proc.apvts, pid::key, "KEY", theme::violet); scale = add<ChoiceBox> (proc.apvts, pid::scale, "SCALE", theme::violet);
    mode = add<ChoiceBox> (proc.apvts, pid::harmMode, "INTERVALS IN", theme::violet); gate = add<ChoiceBox> (proc.apvts, pid::harmGate, "VOICES SOUND", theme::violet);
    amount = add<Knob> (proc.apvts, pid::harmony, "HARMONY", theme::violet); formant = add<Knob> (proc.apvts, pid::formant, "FORMANT", theme::violet);
    detect = add<TextBtn> ("USE THE LOOP'S KEY", theme::violet);
    detect->setTooltip ("Sets key and scale from the analysis of the loaded loop.");
    detect->onClick = [this] { const LoopInfo i = proc.loops.getInfo(); if (i.kind != LoopInfo::none) { proc.setParam (pid::key, (float) i.key); proc.setParam (pid::scale, (float) i.scale); } };
    formant->setTooltip ("Keeps the vowel / body of the sound in place while the pitch moves, so shifted voices do not turn into chipmunks or giants.");
    for (int i = 0; i < kNumHarmVoices; ++i)
    {
        auto& s = strips[i];
        s.on = add<ToggleBox> (proc.apvts, pid::voice (i, "On"), "VOICE " + String (i + 1), theme::violet);
        s.interval = add<Knob> (proc.apvts, pid::voice (i, "Interval"), "INTERVAL", theme::violet); s.interval->setBipolar (true);
        s.pan = add<Knob> (proc.apvts, pid::voice (i, "Pan"), "PAN", theme::violet); s.pan->setBipolar (true);
        s.detune = add<Knob> (proc.apvts, pid::voice (i, "Detune"), "DETUNE", theme::violet); s.detune->setBipolar (true);
        s.level = add<Knob> (proc.apvts, pid::voice (i, "Level"), "LEVEL", theme::violet);
    }
}

void HarmonyPage::resized()
{
    key->setBounds (0, 2, 70, 40); scale->setBounds (76, 2, 124, 40);
    mode->setBounds (0, 50, 200, 40); gate->setBounds (0, 98, 200, 40);
    detect->setBounds (0, 146, 200, 24);
    amount->setBounds (10, 182, 84, 100); formant->setBounds (106, 182, 84, 100);
    const int x0 = 220, sw = 136;
    for (int i = 0; i < kNumHarmVoices; ++i)
    {
        const int x = x0 + i * (sw + 6);
        auto& s = strips[i];
        s.on->setBounds (x + 8, 8, sw - 16, 24);
        s.interval->setBounds (x + 4, 60, 64, 84); s.level->setBounds (x + 68, 60, 64, 84);
        s.pan->setBounds (x + 4, 150, 64, 84); s.detune->setBounds (x + 68, 150, 64, 84);
    }
    wheelArea = Rectangle<int> (x0 + 4 * (sw + 6) + 8, 2, getWidth() - (x0 + 4 * (sw + 6) + 12), getHeight() - 4);
}

void HarmonyPage::paint (Graphics& g)
{
    const int x0 = 220, sw = 136;
    const int k = (int) proc.getParam (pid::key), sc = (int) proc.getParam (pid::scale);
    const bool chromatic = proc.getParam (pid::harmMode) > 0.5f;
    for (int i = 0; i < kNumHarmVoices; ++i)
    {
        const Rectangle<float> r ((float) (x0 + i * (sw + 6)), 0.0f, (float) sw, (float) getHeight() - 2.0f);
        theme::drawPanel (g, r, theme::panel, 10.0f);
        if (level[i] > 0.01f) { g.setColour (theme::violet.withAlpha (0.12f * level[i])); g.fillPath (theme::chamfer (r, 10.0f)); }
        // what this voice is doing right now
        const bool on = proc.getParam (pid::voice (i, "On")) > 0.5f;
        const int iv = (int) proc.getParam (pid::voice (i, "Interval"));
        String t = chromatic ? String (iv > 0 ? "+" : "") + String (iv) + " semitones" : String (iv > 0 ? "+" : "") + String (iv) + " scale steps";
        theme::drawLabel (g, t, Rectangle<int> ((int) r.getX(), 36, sw, 12), on ? theme::text : theme::textFaint, 9.5f);
        String now = level[i] > 0.01f ? String ("now ") + (semis[i] >= 0 ? "+" : "") + String (roundToInt (semis[i])) + " st" : String ("idle");
        theme::drawLabel (g, now, Rectangle<int> ((int) r.getX(), 240, sw, 12), level[i] > 0.01f ? theme::violet : theme::textFaint, 9.5f);
        const Rectangle<float> bar (r.getX() + 12.0f, 262.0f, r.getWidth() - 24.0f, 5.0f);
        g.setColour (theme::grid); g.fillRect (bar);
        g.setColour (theme::violet); g.fillRect (bar.withWidth (bar.getWidth() * jlimit (0.0f, 1.0f, level[i]) * proc.getParam (pid::voice (i, "Level"))));
    }

    // note wheel: scale notes, the note heard in the loop, and where each voice lands
    const auto wa = wheelArea.toFloat();
    theme::drawPanel (g, wa, theme::panel, 10.0f);
    const auto c = wa.getCentre().translated (0.0f, 6.0f);
    const float R = jmin (wa.getWidth(), wa.getHeight()) * 0.36f;
    theme::drawLabel (g, "KEY OF " + theme::noteName (k) + " " + String (scaleDef (sc).name).toUpperCase(), wheelArea.withHeight (18).translated (0, 4), theme::violet, 10.0f);
    bool inScale[12] = {};
    for (int i = 0; i < scaleDef (sc).count; ++i) inScale[(k + scaleDef (sc).steps[i]) % 12] = true;
    for (int pc = 0; pc < 12; ++pc)
    {
        const float a = MathConstants<float>::twoPi * (float) pc / 12.0f;
        const Point<float> p (c.x + R * std::sin (a), c.y - R * std::cos (a));
        const bool heard = pc == pitchClass;
        bool voiced = false;
        if (pitchClass >= 0) for (int v = 0; v < kNumHarmVoices; ++v) if (level[v] > 0.01f && floorModI (pitchClass + roundToInt (semis[v]), 12) == pc) voiced = true;
        const float sz = heard ? 26.0f : (inScale[pc] ? 21.0f : 14.0f);
        const auto dot = Rectangle<float> (sz, sz).withCentre (p);
        Path d; d.addPolygon (p, 6, sz * 0.5f, a);
        g.setColour (heard ? theme::white : (voiced ? theme::violet : (inScale[pc] ? theme::panelHi.brighter (0.25f) : theme::panelHi)));
        g.fillPath (d);
        if (pc == k) { g.setColour (theme::violet); g.strokePath (d, PathStrokeType (1.6f)); }
        g.setColour (heard || voiced ? theme::bg : (inScale[pc] ? theme::text : theme::textFaint)); g.setFont (theme::font (inScale[pc] ? 10.0f : 8.5f, true));
        g.drawText (theme::noteName (pc), dot.toNearestInt(), Justification::centred, false);
    }
    theme::drawLabel (g, pitchClass >= 0 ? "hearing " + theme::noteName (pitchClass) : String (proc.getParam (pid::harmony) > 0.001f ? "listening..." : "turn Harmony up"),
                      Rectangle<int> ((int) c.x - 60, (int) c.y - 7, 120, 14), pitchClass >= 0 ? theme::white : theme::textFaint, 10.0f);
}

void HarmonyPage::tick()
{
    auto& v = proc.engine.visual;
    bool changed = false;
    const int pc = v.pitchClass.load (std::memory_order_relaxed);
    if (pc != pitchClass) { pitchClass = pc; changed = true; }
    for (int i = 0; i < kNumHarmVoices; ++i)
    {
        const float s = v.harmSemis[i].load (std::memory_order_relaxed), l = v.harmLevel[i].load (std::memory_order_relaxed);
        if (s != semis[i] || std::fabs (l - level[i]) > 0.01f) changed = true;
        semis[i] = s; level[i] = l;
    }
    static int slow = 0;
    if (changed || (++slow % 10) == 0) repaint();
}

// ================================================================ OutputPage
OutputPage::OutputPage (SplinterProcessor& p) : Page (p)
{
    auto K = [this] (std::vector<Component*>& row, const char* id, const char* label, Colour c, bool bipolar = false)
    { auto* k = add<Knob> (proc.apvts, id, label, c); k->setBipolar (bipolar); row.push_back (k); };
    K (filterRow, pid::hpFreq, "HIGH-PASS", theme::cyan); K (filterRow, pid::lpFreq, "LOW-PASS", theme::cyan); K (filterRow, pid::filterQ, "RESONANCE", theme::cyan);
    K (toneRow, pid::tilt, "TILT", theme::amber, true); K (toneRow, pid::width, "WIDTH", theme::amber); K (toneRow, pid::outGain, "OUTPUT", theme::white, true);
    K (engineRow, pid::declick, "EDGE FADE", theme::lime); K (engineRow, pid::volDepth, "GATE DEPTH", theme::cyan); K (engineRow, pid::volSmooth, "GATE SMOOTH", theme::cyan);
    K (engineRow, pid::liveBars, "CAPTURE BARS", theme::pink);
    sessionRow.push_back (add<ChoiceBox> (proc.apvts, pid::stretchMode, "TIME-STRETCH", theme::lime));
    sessionRow.push_back (add<ChoiceBox> (proc.apvts, pid::quantize, "SWITCH QUANTISE", theme::white));
    sessionRow.push_back (add<ChoiceBox> (proc.apvts, pid::midiMode, "MIDI NOTES PLAY", theme::white));
    sessionRow.push_back (add<ChoiceBox> (proc.apvts, pid::sliceTrig, "SLICE KEYS", theme::pink));
    sessionRow.push_back (add<ToggleBox> (proc.apvts, pid::limiter, "LIMITER", theme::red));
    sessionRow.push_back (add<ToggleBox> (proc.apvts, pid::inputMonitor, "INPUT MONITOR", theme::white));
    sessionRow.push_back (add<ToggleBox> (proc.apvts, pid::loopPlay, "LOOP PLAYS", theme::lime));
    sessionRow.push_back (add<ToggleBox> (proc.apvts, pid::bypass, "BYPASS", theme::amber));
}

void OutputPage::resized()
{
    int x = 8;
    for (auto* c : filterRow) { c->setBounds (x, 26, 80, 100); x += 84; }
    x += 24;
    for (auto* c : toneRow) { c->setBounds (x, 26, 80, 100); x += 84; }
    x = 8;
    for (auto* c : engineRow) { c->setBounds (x, 172, 80, 100); x += 84; }
    const int sx = 560;
    for (int i = 0; i < 4; ++i) sessionRow[(size_t) i]->setBounds (sx + (i % 2) * 150, 26 + (i / 2) * 48, 142, 40);
    for (int i = 4; i < 8; ++i) sessionRow[(size_t) i]->setBounds (sx + ((i - 4) % 2) * 150, 138 + ((i - 4) / 2) * 34, 142, 26);
    meterArea = Rectangle<int> (880, 6, getWidth() - 888, getHeight() - 12);
}

void OutputPage::paint (Graphics& g)
{
    sectionTitle (g, "FILTER", Rectangle<int> (8, 4, 244, 16), theme::cyan);
    sectionTitle (g, "TONE & LEVEL", Rectangle<int> (284, 4, 244, 16), theme::amber);
    sectionTitle (g, "ENGINE", Rectangle<int> (8, 150, 328, 16), theme::lime);
    sectionTitle (g, "SESSION", Rectangle<int> (560, 4, 292, 16), theme::white);
    g.setColour (theme::textFaint); g.setFont (theme::font (9.5f));
    g.drawFittedText ("Latency " + String (proc.getLatencySamples()) + " samples (" + String (1000.0 * proc.getLatencySamples() / jmax (1.0, proc.getSampleRate()), 1)
                      + " ms), reported to the host. It buys formant-preserving harmony voices and a limiter that cannot overshoot.",
                      Rectangle<int> (560, 214, 292, 60), Justification::topLeft, 4, 0.9f);

    // meters
    theme::drawPanel (g, meterArea.toFloat(), theme::panel, 10.0f);
    auto m = meterArea.reduced (14, 26);
    theme::drawLabel (g, "IN", Rectangle<int> (m.getX(), meterArea.getY() + 6, 20, 14), theme::textDim, 9.0f);
    theme::drawLabel (g, "OUT", Rectangle<int> (m.getX() + 30, meterArea.getY() + 6, 44, 14), theme::textDim, 9.0f);
    theme::drawLabel (g, "GR", Rectangle<int> (m.getX() + 84, meterArea.getY() + 6, 20, 14), theme::textDim, 9.0f);
    auto bar = [&] (int x, float level, Colour c, bool fromTop)
    {
        const Rectangle<float> r ((float) x, (float) m.getY(), 16.0f, (float) m.getHeight());
        g.setColour (theme::grid); g.fillRect (r);
        const float db = jlimit (-60.0f, 0.0f, gainToDb (level));
        const float frac = fromTop ? jlimit (0.0f, 1.0f, -db / 24.0f) : (db + 60.0f) / 60.0f;
        g.setColour (c);
        g.fillRect (fromTop ? r.withHeight (r.getHeight() * frac) : r.withTop (r.getBottom() - r.getHeight() * frac));
    };
    bar (m.getX() + 2, inPeak, theme::textDim, false);
    bar (m.getX() + 32, peakL, peakL > 0.95f ? theme::red : theme::lime, false);
    bar (m.getX() + 52, peakR, peakR > 0.95f ? theme::red : theme::lime, false);
    bar (m.getX() + 86, gr, theme::red, true);
}

void OutputPage::tick()
{
    auto& v = proc.engine.visual;
    const float l = jmax (v.peakL.load(), peakL * 0.86f), r = jmax (v.peakR.load(), peakR * 0.86f), i = jmax (v.inPeak.load(), inPeak * 0.86f), g = v.gainReduction.load();
    if (std::fabs (l - peakL) + std::fabs (r - peakR) + std::fabs (i - inPeak) + std::fabs (g - gr) > 1.0e-4f) repaint (meterArea);
    peakL = l; peakR = r; inPeak = i; gr = g;
}
