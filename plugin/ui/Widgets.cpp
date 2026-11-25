#include "Widgets.h"

namespace ui {

using namespace atm;

namespace {
constexpr float kArcStart = -2.35619449f;      // -135 deg from 12 o'clock
constexpr float kArcEnd   =  2.35619449f;
inline float angleFor (float v) { return kArcStart + (kArcEnd - kArcStart) * juce::jlimit (0.f, 1.f, v); }

void strokeArc (juce::Graphics& g, juce::Point<float> c, float r, float a0, float a1, float thickness, juce::Colour colour)
{
    if (std::abs (a1 - a0) < 0.003f) return;
    juce::Path p;
    p.addCentredArc (c.x, c.y, r, r, 0.f, juce::jmin (a0, a1), juce::jmax (a0, a1), true);
    g.setColour (colour);
    g.strokePath (p, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
}
}

// ============================================================================ ParamWidget
juce::Colour ParamWidget::accentColour() const
{
    if (hasAccent) return accent;
    return ref.global ? col::accent() : col::track (ctx.resolveTrack (ref));
}

void ParamWidget::sync()
{
    const float s = ctx.shown (ref);
    const bool lk = ctx.shownIsLock (ref);
    const bool al = ctx.hasAnyLock (ref);
    const float md = ctx.modDepth (ref);
    const float lv = (md > 0.f || al) ? ctx.live (ref) : s;
    if (s != shown || lk != isLock || al != anyLock || md != modDepth || std::abs (lv - live) > 0.002f)
    {
        shown = s; isLock = lk; anyLock = al; modDepth = md; live = lv;
        repaint();
    }
}

void ParamWidget::mouseEnter (const juce::MouseEvent&)
{
    hover = true;
    const ParamDesc& d = ctx.desc (ref);
    ctx.hoverHelp = juce::String (d.name) + "  " + ctx.format (ref, ctx.shown (ref)) + "   " + d.help;
    repaint();
}
void ParamWidget::mouseExit (const juce::MouseEvent&) { hover = false; ctx.hoverHelp = {}; repaint(); }

void ParamWidget::beginEdit() { dragging = true; ctx.begin (ref, ctx.editsLock (ref) ? "Parameter lock" : "Parameter"); }
void ParamWidget::endEdit()   { dragging = false; ctx.end (ref); repaint(); }

void ParamWidget::itemDropped (const SourceDetails& d)
{
    dropHover = false;
    const int src = d.description.toString().fromFirstOccurrenceOf ("modsrc:", false, false).getIntValue();
    if (src <= Src::None || src >= Src::count) return;
    ModSlot m;
    m.src = (uint8_t) src; m.srcTrack = (uint8_t) ctx.track;
    m.dstTrack = (int8_t) (ref.global ? -1 : ctx.resolveTrack (ref));
    m.dstParam = (uint16_t) ref.id;
    m.amount = 0.25f;
    if (ctx.sess.addMod (m) < 0) ctx.status = "Modulation matrix is full (64 routings)";
    else ctx.status = juce::String (srcName (src)) + " -> " + ctx.desc (ref).name + "  (set the amount in the MOD tab)";
    repaint();
}

void ParamWidget::showContextMenu()
{
    juce::PopupMenu m;
    const ParamDesc& d = ctx.desc (ref);
    m.addSectionHeader (juce::String (d.name) + "  " + ctx.format (ref, ctx.shown (ref)));
    const bool canLock = ref.global || ctx.resolveTrack (ref) == ctx.track;
    m.addItem (1, "Reset to default");
    if (canLock)
    {
        m.addSeparator();
        m.addItem (2, ctx.hasStepSelection() ? "Lock current value to selected step(s)" : "Lock current value to selected step(s)  (select a step first)", ctx.hasStepSelection());
        m.addItem (3, "Clear lock on selected step(s)", ctx.hasStepSelection() && ctx.hasAnyLock (ref));
        m.addItem (4, "Clear all locks of this parameter", ctx.hasAnyLock (ref));
        m.addItem (5, "Show lock lane in STEPS tab", ctx.hasAnyLock (ref));
    }
    int id = 100;
    std::vector<int> slots;
    const int dt = ref.global ? -1 : ctx.resolveTrack (ref);
    for (int i = 0; i < kModSlots; ++i)
    {
        const ModSlot& s = ctx.proj().kit.mods[i];
        if (s.src == Src::None || s.dstTrack != dt || s.dstParam != ref.id) continue;
        if (slots.empty()) m.addSeparator();
        m.addItem (id++, juce::String ("Remove modulation: ") + srcName (s.src) + "  " + juce::String (s.amount, 2));
        slots.push_back (i);
    }
    juce::Component::SafePointer<ParamWidget> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe, slots] (int r)
    {
        if (safe == nullptr || r == 0) return;
        Ctx& c = safe->ctx; const ParamRef ref = safe->ref;
        if (r == 1) { c.begin (ref); c.set (ref, c.desc (ref).defNorm()); c.end (ref); }
        else if (r == 2)
        {
            const float v = c.base (ref);
            TrackPattern p = c.tp();
            for (int i = c.selLo(); i <= c.selHi() && i < p.length; ++i) p.setLock (i, c.destOf (ref), v);
            c.sess.setTrackPattern (c.pattern(), c.track, p);
            c.lockMode = true; ++c.uiRev;
        }
        else if (r == 3) c.clearLocks (ref, true);
        else if (r == 4) c.clearLocks (ref, false);
        else if (r == 5) { c.setTab (TabSteps); }
        else if (r >= 100 && r - 100 < (int) slots.size()) c.sess.removeMod (slots[(size_t) (r - 100)]);
    });
}

// ============================================================================ Knob
void Knob::paint (juce::Graphics& g)
{
    const ParamDesc& d = ctx.desc (ref);
    auto r = getLocalBounds();
    const int labelH = big ? 14 : 12;
    auto labelArea = r.removeFromBottom (labelH);
    const float dia = (float) juce::jmin (r.getWidth(), r.getHeight()) - 6.f;
    const juce::Point<float> c ((float) r.getCentreX(), (float) r.getCentreY() + 1.f);
    const float rad = dia * 0.5f;
    const juce::Colour ac = isLock ? col::lock() : accentColour();
    const float thick = big ? 3.4f : 2.6f;

    // body
    g.setColour (col::well());
    g.fillEllipse (c.x - rad + 3.f, c.y - rad + 3.f, dia - 6.f, dia - 6.f);
    strokeArc (g, c, rad - 1.f, kArcStart, kArcEnd, thick, col::line().brighter (hover ? 0.25f : 0.f));

    // value arc (bipolar params grow from the centre)
    const bool bipolar = d.isBipolar() || std::strcmp (d.key, "density") == 0;
    const float a = angleFor (shown);
    strokeArc (g, c, rad - 1.f, bipolar ? 0.f : kArcStart, a, thick, ac);

    // pointer
    const float pr0 = rad * 0.28f, pr1 = rad - 4.5f;
    g.setColour (col::text());
    g.drawLine (c.x + std::sin (a) * pr0, c.y - std::cos (a) * pr0, c.x + std::sin (a) * pr1, c.y - std::cos (a) * pr1, big ? 1.8f : 1.5f);

    // modulation range and live value
    if (modDepth > 0.f)
    {
        float signedSum = 0.f; ctx.modDepth (ref, &signedSum);
        const float lo = juce::jlimit (0.f, 1.f, shown - modDepth), hi = juce::jlimit (0.f, 1.f, shown + modDepth);
        strokeArc (g, c, rad + 2.2f, angleFor (lo), angleFor (hi), 1.4f, col::mod().withAlpha (0.75f));
    }
    if (modDepth > 0.f || (anyLock && ! isLock))
    {
        const float la = angleFor (live);
        g.setColour (modDepth > 0.f ? col::mod() : col::lock());
        g.fillEllipse (c.x + std::sin (la) * (rad + 2.2f) - 2.2f, c.y - std::cos (la) * (rad + 2.2f) - 2.2f, 4.4f, 4.4f);
    }
    if (anyLock)
    {
        g.setColour (col::lock());
        g.fillRect ((float) getWidth() - 7.f, 2.f, 4.f, 4.f);
    }
    if (dropHover)
    {
        g.setColour (col::mod());
        g.drawEllipse (c.x - rad - 3.f, c.y - rad - 3.f, dia + 6.f, dia + 6.f, 1.5f);
    }

    // caption / value
    const bool showValue = hover || dragging;
    drawLabel (g, showValue ? ctx.format (ref, shown) : captionText().toUpperCase(), labelArea.expanded (8, 0),
               showValue ? col::text() : (isLock ? col::lock() : col::dim()), big ? 10.5f : 9.5f);
}

void Knob::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { showContextMenu(); return; }
    beginEdit();
    dragStart = ctx.shown (ref);
}

void Knob::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging) return;
    const ParamDesc& d = ctx.desc (ref);
    const float range = e.mods.isShiftDown() ? 1600.f : 200.f;
    float v = dragStart + (float) (-e.getDistanceFromDragStartY() + e.getDistanceFromDragStartX() / 3) / range;
    v = juce::jlimit (0.f, 1.f, v);
    if (d.isDiscrete()) v = d.toNorm (d.toPhys (v));
    ctx.set (ref, v);
    sync();
    ctx.hoverHelp = juce::String (d.name) + "  " + ctx.format (ref, ctx.shown (ref)) + "   " + d.help;
}

void Knob::mouseUp (const juce::MouseEvent&) { if (dragging) endEdit(); }

void Knob::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) return;
    ctx.begin (ref); ctx.set (ref, ctx.desc (ref).defNorm()); ctx.end (ref); sync();
}

void Knob::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const ParamDesc& d = ctx.desc (ref);
    float v = ctx.shown (ref);
    if (d.isDiscrete())
    {
        const float ph = d.toPhys (v) + (w.deltaY > 0 ? 1.f : -1.f);
        v = d.toNorm (ph);
    }
    else v += w.deltaY * (e.mods.isShiftDown() ? 0.02f : 0.12f);
    ctx.begin (ref); ctx.set (ref, v); ctx.end (ref); sync();
}

// ============================================================================ Choice
void Choice::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    auto labelArea = showCaption ? r.removeFromBottom (12) : juce::Rectangle<int>();
    auto box = r.reduced (2, showCaption ? juce::jmax (2, (r.getHeight() - 20) / 2) : 1);
    const juce::Colour ac = isLock ? col::lock() : accentColour();
    fillPanel (g, box, col::well(), 2.f);
    strokePanel (g, box, hover ? col::lineHi() : col::line(), 2.f);
    g.setColour (ac);
    g.fillRect (box.getX() + 1, box.getY() + 2, 2, box.getHeight() - 4);
    drawLabel (g, ctx.format (ref, shown), box.withTrimmedLeft (5).withTrimmedRight (2), col::text(), 10.f);
    if (anyLock) { g.setColour (col::lock()); g.fillRect ((float) getWidth() - 7.f, 2.f, 4.f, 4.f); }
    if (dropHover) strokePanel (g, box, col::mod(), 2.f);
    if (showCaption) drawLabel (g, captionText().toUpperCase(), labelArea.expanded (8, 0), isLock ? col::lock() : col::dim(), 9.5f);
}

void Choice::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { showContextMenu(); return; }
    const ParamDesc& d = ctx.desc (ref);
    juce::PopupMenu m;
    const int cur = d.toIndex (ctx.shown (ref));
    const int n = d.numSteps();
    for (int i = 0; i < n; ++i)
    {
        char buf[40]; formatParam (d, d.toNorm (d.min + (float) i), buf, 40);
        m.addItem (i + 1, buf, true, i == cur);
    }
    juce::Component::SafePointer<Choice> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe] (int r)
    {
        if (safe == nullptr || r == 0) return;
        const ParamDesc& dd = safe->ctx.desc (safe->ref);
        safe->ctx.begin (safe->ref); safe->ctx.set (safe->ref, dd.toNorm (dd.min + (float) (r - 1))); safe->ctx.end (safe->ref);
        safe->sync();
    });
}

void Choice::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    const ParamDesc& d = ctx.desc (ref);
    const float ph = d.toPhys (ctx.shown (ref)) + (w.deltaY > 0 ? -1.f : 1.f);
    ctx.begin (ref); ctx.set (ref, d.toNorm (ph)); ctx.end (ref); sync();
}

// ============================================================================ Toggle
void Toggle::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    auto labelArea = r.removeFromBottom (12);
    const bool on = shown >= 0.5f;
    const juce::Colour ac = isLock ? col::lock() : accentColour();
    auto box = juce::Rectangle<int> (0, 0, 26, 14).withCentre (r.getCentre());
    fillPanel (g, box, on ? ac.withAlpha (0.9f) : col::well(), 7.f);
    strokePanel (g, box, on ? ac : (hover ? col::lineHi() : col::line()), 7.f);
    g.setColour (on ? col::bg() : col::dim());
    g.fillEllipse ((float) (on ? box.getRight() - 12 : box.getX() + 2), (float) box.getY() + 2.f, 10.f, 10.f);
    if (anyLock) { g.setColour (col::lock()); g.fillRect ((float) getWidth() - 7.f, 2.f, 4.f, 4.f); }
    if (dropHover) strokePanel (g, box.expanded (2), col::mod(), 8.f);
    drawLabel (g, captionText().toUpperCase(), labelArea.expanded (8, 0), isLock ? col::lock() : col::dim(), 9.5f);
}

void Toggle::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { showContextMenu(); return; }
    ctx.begin (ref); ctx.set (ref, ctx.shown (ref) >= 0.5f ? 0.f : 1.f); ctx.end (ref); sync();
}

std::unique_ptr<ParamWidget> makeParamWidget (Ctx& ctx, ParamRef ref)
{
    const ParamDesc& d = ctx.desc (ref);
    if (d.key[0] == 0) return nullptr;
    if (d.isDiscrete() && d.labels != nullptr)
    {
        if (d.numLabels == 2) return std::make_unique<Toggle> (ctx, ref);
        return std::make_unique<Choice> (ctx, ref);
    }
    return std::make_unique<Knob> (ctx, ref);
}

// ============================================================================ Btn
void Btn::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().reduced (1);
    const juce::Colour c = enabled ? colour : col::faint();
    if (lit || down) fillPanel (g, r, c.withAlpha (down && ! lit ? 0.45f : 0.92f), 2.5f);
    else fillPanel (g, r, over ? col::panelHi().brighter (0.06f) : col::panelHi(), 2.5f);
    strokePanel (g, r, lit ? c : (over ? c.withAlpha (0.7f) : col::line()), 2.5f);
    drawLabel (g, text, r, lit || down ? col::bg() : (enabled ? (over ? c : col::text()) : col::faint()), textHeight, juce::Justification::centred, bold);
}

void Btn::mouseDown (const juce::MouseEvent& e)
{
    if (! enabled) return;
    if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick (e); return; }
    down = true; dragFired = false; repaint();
    if (onPressChange) onPressChange (true);
}

void Btn::mouseDrag (const juce::MouseEvent& e)
{
    if (! down || dragFired || ! onDragStart || e.getDistanceFromDragStart() < 8) return;
    dragFired = true; down = false; repaint();
    onDragStart();
}

void Btn::mouseUp (const juce::MouseEvent& e)
{
    if (! down) return;
    down = false; repaint();
    if (onPressChange) onPressChange (false);
    if (onClick && ! dragFired && getLocalBounds().contains (e.getPosition())) onClick();
}

// ============================================================================ ModChip
void ModChip::sync()
{
    const float v = ctx.eng.shared.srcValue[src].load (std::memory_order_relaxed);
    if (std::abs (v - value) > 0.01f) { value = v; repaint(); }
}

void ModChip::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().reduced (1);
    fillPanel (g, r, col::well(), 2.f);
    // live value meter behind the text
    const float v = juce::jlimit (-1.f, 1.f, value);
    const float mid = (float) r.getCentreX();
    g.setColour (col::mod().withAlpha (0.28f));
    if (src == Src::ModEnv || src == Src::AmpEnv || src == Src::Velocity || src >= Src::Macro1 && src <= Src::Macro4 || src == Src::Follower)
        g.fillRect ((float) r.getX(), (float) r.getY(), (float) r.getWidth() * juce::jmax (0.f, v), (float) r.getHeight());
    else
        g.fillRect (v >= 0.f ? mid : mid + v * (float) r.getWidth() * 0.5f, (float) r.getY(), std::abs (v) * (float) r.getWidth() * 0.5f, (float) r.getHeight());
    strokePanel (g, r, col::mod().withAlpha (0.6f), 2.f);
    drawLabel (g, juce::String (srcName (src)).toUpperCase(), r, col::mod(), 9.5f, juce::Justification::centred, true);
}

void ModChip::mouseDrag (const juce::MouseEvent& e)
{
    if (e.getDistanceFromDragStart() < 4) return;
    if (auto* dc = juce::DragAndDropContainer::findParentDragContainerFor (this))
        if (! dc->isDragAndDropActive())
        {
            juce::Image img (juce::Image::ARGB, getWidth(), getHeight(), true);
            { juce::Graphics g (img); paint (g); }
            dc->startDragging ("modsrc:" + juce::String (src), this, juce::ScaledImage (img), true);
        }
}

// ============================================================================ NumberBox
void NumberBox::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    juce::Rectangle<int> cap;
    if (caption.isNotEmpty()) cap = r.removeFromBottom (11);
    auto box = r.reduced (1);
    fillPanel (g, box, col::well(), 2.f);
    strokePanel (g, box, col::line(), 2.f);
    const double v = get ? get() : 0.0;
    drawLabel (g, format ? format (v) : juce::String (v, step >= 1.0 ? 0 : 2), box, colour, juce::jmin (12.f, (float) box.getHeight() - 5.f), juce::Justification::centred, true);
    if (caption.isNotEmpty()) drawLabel (g, caption.toUpperCase(), cap.expanded (6, 0), col::dim(), 9.f);
}

void NumberBox::mouseDown (const juce::MouseEvent&) { dragStartV = get ? get() : 0.0; if (onGestureStart) onGestureStart(); }
void NumberBox::mouseDrag (const juce::MouseEvent& e)
{
    if (! set) return;
    const double steps = std::round ((double) (-e.getDistanceFromDragStartY()) / (e.mods.isShiftDown() ? pixelsPerStep * 4.0 : pixelsPerStep));
    set (juce::jlimit (minV, maxV, dragStartV + steps * step));
    sync();
}
void NumberBox::mouseUp (const juce::MouseEvent&) { if (onGestureEnd) onGestureEnd(); }
void NumberBox::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (! set || ! get) return;
    if (onGestureStart) onGestureStart();
    set (juce::jlimit (minV, maxV, get() + (w.deltaY > 0 ? step : -step)));
    if (onGestureEnd) onGestureEnd();
    sync();
}

// ============================================================================ Picker
void Picker::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    juce::Rectangle<int> cap;
    if (caption.isNotEmpty()) cap = r.removeFromBottom (11);
    auto box = r.reduced (1);
    fillPanel (g, box, col::well(), 2.f);
    strokePanel (g, box, col::line(), 2.f);
    const auto list = items ? items() : juce::StringArray();
    const int v = get ? get() : 0;
    g.setColour (colour);
    juce::Path tri;
    const float tx = (float) box.getRight() - 9.f, ty = (float) box.getCentreY() - 1.5f;
    tri.addTriangle (tx, ty, tx + 6.f, ty, tx + 3.f, ty + 4.f);
    g.fillPath (tri);
    drawLabel (g, juce::isPositiveAndBelow (v, list.size()) ? list[v] : juce::String ("-"), box.withTrimmedLeft (6).withTrimmedRight (11), colour,
               juce::jmin (11.f, (float) box.getHeight() - 5.f), juce::Justification::centredLeft, true);
    if (caption.isNotEmpty()) drawLabel (g, caption.toUpperCase(), cap.expanded (6, 0), col::dim(), 9.f);
}

void Picker::mouseDown (const juce::MouseEvent&)
{
    const auto list = items ? items() : juce::StringArray();
    juce::PopupMenu m;
    const int cur = get ? get() : -1;
    for (int i = 0; i < list.size(); ++i)
    {
        if (list[i].startsWith ("--")) { m.addSectionHeader (list[i].substring (2)); continue; }
        m.addItem (i + 1, list[i], true, i == cur);
    }
    juce::Component::SafePointer<Picker> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMaximumNumColumns (4), [safe] (int r)
    {
        if (safe != nullptr && r > 0 && safe->set) { safe->set (r - 1); safe->sync(); }
    });
}

void Picker::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (! get || ! set || ! items) return;
    const auto list = items();
    int v = get();
    for (int tries = 0; tries < list.size(); ++tries)
    {
        v = juce::jlimit (0, list.size() - 1, v + (w.deltaY > 0 ? -1 : 1));
        if (! list[v].startsWith ("--")) break;
    }
    if (! list[v].startsWith ("--")) set (v);
    sync();
}

// ============================================================================ ValueKnob
void ValueKnob::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    auto labelArea = r.removeFromBottom (12);
    const float dia = (float) juce::jmin (r.getWidth(), r.getHeight()) - 6.f;
    const juce::Point<float> c ((float) r.getCentreX(), (float) r.getCentreY() + 1.f);
    const float rad = dia * 0.5f;
    const float v = get ? get() : 0.f;
    g.setColour (col::well());
    g.fillEllipse (c.x - rad + 3.f, c.y - rad + 3.f, dia - 6.f, dia - 6.f);
    strokeArc (g, c, rad - 1.f, kArcStart, kArcEnd, 2.6f, col::line().brighter (over ? 0.25f : 0.f));
    const float a = angleFor (v);
    strokeArc (g, c, rad - 1.f, bipolar ? 0.f : kArcStart, a, 2.6f, colour);
    g.setColour (col::text());
    g.drawLine (c.x + std::sin (a) * rad * 0.28f, c.y - std::cos (a) * rad * 0.28f, c.x + std::sin (a) * (rad - 4.5f), c.y - std::cos (a) * (rad - 4.5f), 1.5f);
    const bool showValue = (over || dragging) && format;
    drawLabel (g, showValue ? format (v) : caption.toUpperCase(), labelArea.expanded (8, 0), showValue ? col::text() : col::dim(), 9.5f);
}
void ValueKnob::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) return;
    dragging = true; dragStart = get ? get() : 0.f;
    if (onGestureStart) onGestureStart();
}
void ValueKnob::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging || ! set) return;
    const float range = e.mods.isShiftDown() ? 1600.f : 200.f;
    set (snap (dragStart + (float) (-e.getDistanceFromDragStartY() + e.getDistanceFromDragStartX() / 3) / range));
    sync(); showHelp();
}
void ValueKnob::mouseUp (const juce::MouseEvent&) { if (dragging) { dragging = false; if (onGestureEnd) onGestureEnd(); repaint(); } }
void ValueKnob::mouseDoubleClick (const juce::MouseEvent&)
{
    if (! set) return;
    if (onGestureStart) onGestureStart();
    set (defaultValue);
    if (onGestureEnd) onGestureEnd();
    sync();
}
void ValueKnob::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (! set || ! get) return;
    if (onGestureStart) onGestureStart();
    const float d = steps > 1 ? (w.deltaY > 0 ? 1.f : -1.f) / (float) (steps - 1) : w.deltaY * (e.mods.isShiftDown() ? 0.02f : 0.12f);
    set (snap (get() + d));
    if (onGestureEnd) onGestureEnd();
    sync();
}

// ============================================================================ ParamGroup
ParamWidget* ParamGroup::add (ParamRef r, const juce::String& captionOverride)
{
    auto w = makeParamWidget (ctx, r);
    if (w == nullptr) return nullptr;
    if (captionOverride.isNotEmpty()) w->setCaption (captionOverride);
    w->setAccent (accent);
    addAndMakeVisible (*w);
    widgets.push_back (std::move (w));
    return widgets.back().get();
}

int ParamGroup::idealWidth (int nRows) const
{
    int customW = 0;
    for (auto& c : customs) customW += c.second + 4;
    const int cols = ((int) widgets.size() + nRows - 1) / juce::jmax (1, nRows);
    return juce::jmax (cols * cellW + customW + 10, (int) textWidth (font (10.f, true), title) + 24);
}

void ParamGroup::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    fillPanel (g, r, col::panel(), 3.f);
    g.setColour (accent.withAlpha (0.85f));
    g.fillRect (r.getX() + 6, r.getY() + 5, 3, 9);
    drawLabel (g, title.toUpperCase(), r.removeFromTop (18).withTrimmedLeft (13), col::dim(), 10.f, juce::Justification::centredLeft, true);
}

void ParamGroup::resized()
{
    auto r = getLocalBounds().withTrimmedTop (18).reduced (5, 0);
    int x = r.getX();
    for (auto& c : customs) { c.first->setBounds (x, r.getY(), c.second, r.getHeight() - 4); x += c.second + 4; }
    const int nRows = juce::jmax (1, rows);
    const int cols = juce::jmax (1, ((int) widgets.size() + nRows - 1) / nRows);
    const int cw = juce::jmin (cellW, juce::jmax (30, (r.getRight() - x) / cols));
    const int ch = juce::jmin (cellH, r.getHeight() / nRows);
    for (int i = 0; i < (int) widgets.size(); ++i)
        widgets[(size_t) i]->setBounds (x + (i % cols) * cw, r.getY() + (i / cols) * ch, cw, ch);
}

} // namespace ui
